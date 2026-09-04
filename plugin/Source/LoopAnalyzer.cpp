#include "LoopAnalyzer.h"
#include "LoopAnalyzerInternal.h"
#include "SignalDiagnostics.h"

using namespace loopAnalyzerInternal;

LoopAnalysisReport LoopAnalyzer::analyzeSource(const juce::AudioBuffer<float>& audio,
                                               const double sampleRate,
                                               int minimumLoopSamples,
                                               int maximumLoopSamples,
                                               const int maximumCandidates,
                                               const int repairOverlapSamples)
{
    LoopAnalysisReport report;
    if (audio.getNumChannels() == 0 || audio.getNumSamples() < 32 || sampleRate <= 0.0)
        return report;
    minimumLoopSamples = juce::jlimit(16, audio.getNumSamples() / 2, minimumLoopSamples);
    maximumLoopSamples = juce::jlimit(minimumLoopSamples,
                                      audio.getNumSamples() - 1 - juce::jmax(0, repairOverlapSamples),
                                      maximumLoopSamples);
    const auto sourceRms = sampleRms(audio, 0, audio.getNumSamples());
    const auto periods = findPeriods(audio, sampleRate, minimumLoopSamples, maximumLoopSamples);
    for (const auto& period : periods)
    {
        const auto radius = juce::jmin(juce::roundToInt(sampleRate * 0.08), period.samples / 12);
        auto result = searchAtPeriod(audio, sampleRate, period, radius,
                                     repairOverlapSamples, sourceRms);
        if (result.candidateFitness >= 0.0f)
            report.candidates.push_back(result);
    }
    std::sort(report.candidates.begin(), report.candidates.end(), [] (const auto& left, const auto& right)
    {
        return left.candidateFitness > right.candidateFitness;
    });
    std::vector<LoopAnalysisResult> diverse;
    const auto duplicateStartTolerance = juce::roundToInt(sampleRate * 0.05);
    for (const auto& candidate : report.candidates)
    {
        const auto candidateLength = candidate.endSample - candidate.startSample
                                     - candidate.repairOverlapSamples;
        const auto duplicate = std::any_of(diverse.begin(), diverse.end(), [&] (const auto& kept)
        {
            const auto keptLength = kept.endSample - kept.startSample - kept.repairOverlapSamples;
            return std::abs(candidate.startSample - kept.startSample) < duplicateStartTolerance
                   && std::abs(candidateLength - keptLength)
                          < juce::jmax(8, candidateLength / 40);
        });
        if (!duplicate)
            diverse.push_back(candidate);
        if (diverse.size() >= static_cast<size_t>(juce::jmax(1, maximumCandidates)))
            break;
    }
    report.candidates = std::move(diverse);
    return report;
}

LoopAnalysisReport LoopAnalyzer::analyzeRotateRepair(
    const juce::AudioBuffer<float>& audio,
    const double sampleRate,
    const int maximumCandidates,
    const int maximumRepairOverlapSamples)
{
    LoopAnalysisReport report;
    const auto samples = audio.getNumSamples();
    if (audio.getNumChannels() == 0 || samples < 256 || sampleRate <= 0.0)
        return report;

    const auto maximumFade = juce::jlimit(
        0, samples / 8, maximumRepairOverlapSamples);
    std::vector<int> repairOptions { 0 };
    for (const auto milliseconds : { 20, 40, 80, 140, 220 })
    {
        const auto repair = juce::jmin(
            maximumFade, juce::roundToInt(sampleRate * milliseconds * 0.001));
        if (repair >= 2)
            repairOptions.push_back(repair);
    }
    std::sort(repairOptions.begin(), repairOptions.end());
    repairOptions.erase(std::unique(repairOptions.begin(), repairOptions.end()),
                        repairOptions.end());

    LoopAnalysisResult bestRepair;
    bestRepair.candidateFitness = -1.0f;
    for (const auto repair : repairOptions)
    {
        auto candidate = evaluateFixedRange(audio, sampleRate, 0, samples, repair);
        const auto removedFraction = static_cast<float>(repair)
                                     / static_cast<float>(samples);
        candidate.candidateFitness -= 12.0f * removedFraction;
        if (candidate.candidateFitness > bestRepair.candidateFitness)
            bestRepair = candidate;
    }
    if (bestRepair.candidateFitness < 0.0f)
        return report;

    const auto guard = juce::jlimit(
        32, juce::jmax(32, samples / 3),
        juce::jmax(bestRepair.repairOverlapSamples + 32,
                   juce::roundToInt(sampleRate * 0.35)));
    const auto firstCut = juce::jmin(samples - 1, guard);
    const auto lastCut = juce::jmax(firstCut, samples - guard);
    const auto step = juce::jmax(1, juce::roundToInt(sampleRate * 0.01));
    struct RotationCandidate
    {
        int cut = 0;
        float safety = 0.0f;
    };
    std::vector<RotationCandidate> rotations;
    for (int cut = firstCut; cut <= lastCut; cut += step)
        rotations.push_back({ cut, calculateRotationSafety(audio, sampleRate, cut) });
    std::sort(rotations.begin(), rotations.end(), [] (const auto& left, const auto& right)
    {
        return left.safety > right.safety;
    });

    const auto distinctDistance = juce::roundToInt(sampleRate * 0.40);
    for (const auto& rotation : rotations)
    {
        const auto duplicate = std::any_of(
            report.candidates.begin(), report.candidates.end(), [&] (const auto& kept)
            {
                return std::abs(kept.rotationSample - rotation.cut) < distinctDistance;
            });
        if (duplicate)
            continue;
        auto result = bestRepair;
        result.startSample = 0;
        result.endSample = samples;
        result.rotationSample = rotation.cut;
        result.candidateFitness = 0.68f * bestRepair.candidateFitness
                                  + 0.32f * rotation.safety;
        report.candidates.push_back(result);
        if (report.candidates.size()
            >= static_cast<size_t>(juce::jmax(1, maximumCandidates)))
            break;
    }

    return report;
}

LoopAnalysisReport LoopAnalyzer::analyzeRotateRepairExact(
    const juce::AudioBuffer<float>& audio,
    const double sampleRate,
    const int targetOutputSamples,
    const int maximumCandidates,
    const int maximumRepairOverlapSamples)
{
    LoopAnalysisReport report;
    const auto samples = audio.getNumSamples();
    if (audio.getNumChannels() == 0 || sampleRate <= 0.0
        || targetOutputSamples < 256 || samples < 256)
        return report;

    const auto repetitionCount = juce::jmax(
        1, (targetOutputSamples + samples - 1) / samples);
    const auto cycleOutputSamples = juce::jlimit(
        256, samples, juce::roundToInt(
            static_cast<double>(targetOutputSamples) / repetitionCount));
    const auto maximumFade = juce::jlimit(
        0, cycleOutputSamples / 8, maximumRepairOverlapSamples);
    std::vector<int> repairOptions { 0 };
    for (const auto milliseconds : { 20, 40, 80, 140, 220 })
    {
        const auto repair = juce::jmin(
            maximumFade, juce::roundToInt(sampleRate * milliseconds * 0.001));
        if (repair >= 2 && cycleOutputSamples + repair <= samples)
            repairOptions.push_back(repair);
    }
    std::sort(repairOptions.begin(), repairOptions.end());
    repairOptions.erase(std::unique(repairOptions.begin(), repairOptions.end()),
                        repairOptions.end());

    struct WindowCandidate
    {
        LoopAnalysisResult result;
    };
    std::vector<WindowCandidate> windows;
    for (const auto repair : repairOptions)
    {
        const auto span = cycleOutputSamples + repair;
        const auto maximumStart = samples - span;
        const auto step = juce::jmax(
            1, maximumStart > 0 ? juce::jmax(juce::roundToInt(sampleRate * 0.025),
                                             maximumStart / 48)
                                    : 1);
        for (int start = 0;; start = juce::jmin(maximumStart, start + step))
        {
            auto candidate = evaluateFixedRange(
                audio, sampleRate, start, start + span, repair);
            candidate.candidateFitness -= 5.0f * static_cast<float>(repair)
                                          / static_cast<float>(span);
            windows.push_back({ candidate });
            if (start == maximumStart)
                break;
        }
    }
    if (windows.empty())
        return report;

    std::sort(windows.begin(), windows.end(), [] (const auto& left, const auto& right)
    {
        return left.result.candidateFitness > right.result.candidateFitness;
    });
    if (windows.size() > 10u)
        windows.resize(10u);

    std::vector<LoopAnalysisResult> candidates;
    for (auto window : windows)
    {
        const auto span = window.result.endSample - window.result.startSample;
        const auto guard = juce::jlimit(
            32, juce::jmax(32, span / 3),
            juce::jmax(window.result.repairOverlapSamples + 32,
                       juce::roundToInt(sampleRate * 0.25)));
        const auto firstCut = window.result.startSample + guard;
        const auto lastCut = window.result.endSample - guard;
        if (firstCut >= lastCut)
            continue;
        const auto cutStep = juce::jmax(1, juce::roundToInt(sampleRate * 0.01));
        auto bestCut = firstCut;
        auto bestSafety = -1.0f;
        for (int cut = firstCut; cut <= lastCut; cut += cutStep)
        {
            const auto safety = calculateRotationSafety(audio, sampleRate, cut);
            if (safety > bestSafety)
            {
                bestSafety = safety;
                bestCut = cut;
            }
        }
        window.result.rotationSample = bestCut;
        window.result.targetOutputSamples = targetOutputSamples;
        window.result.repetitionCount = repetitionCount;
        window.result.candidateFitness = 0.68f * window.result.candidateFitness
                                         + 0.32f * juce::jmax(0.0f, bestSafety);
        candidates.push_back(window.result);
    }
    std::sort(candidates.begin(), candidates.end(), [] (const auto& left, const auto& right)
    {
        return left.candidateFitness > right.candidateFitness;
    });
    const auto distinctDistance = juce::roundToInt(sampleRate * 0.20);
    for (const auto& candidate : candidates)
    {
        const auto duplicate = std::any_of(
            report.candidates.begin(), report.candidates.end(), [&] (const auto& kept)
            {
                return std::abs(kept.startSample - candidate.startSample) < distinctDistance
                       && std::abs(kept.rotationSample - candidate.rotationSample)
                              < distinctDistance;
            });
        if (!duplicate)
            report.candidates.push_back(candidate);
        if (report.candidates.size()
            >= static_cast<size_t>(juce::jmax(1, maximumCandidates)))
            break;
    }
    return report;
}

juce::AudioBuffer<float> LoopAnalyzer::renderRotateRepair(
    const juce::AudioBuffer<float>& source,
    const LoopAnalysisResult& result)
{
    if (source.getNumChannels() == 0 || result.rotationSample < 0
        || result.startSample < 0 || result.endSample > source.getNumSamples()
        || result.endSample - result.startSample < 32)
        return {};

    const auto rangeStart = result.startSample;
    const auto rangeSamples = result.endSample - rangeStart;
    const auto rotation = juce::jlimit(
        rangeStart + 1, result.endSample - 1, result.rotationSample);
    const auto relativeRotation = rotation - rangeStart;
    const auto fade = juce::jlimit(
        0, juce::jmin(relativeRotation, rangeSamples - relativeRotation),
        result.repairOverlapSamples);
    const auto renderedSamples = rangeSamples - fade;
    juce::AudioBuffer<float> rendered(source.getNumChannels(), renderedSamples);
    const auto tailLength = rangeSamples - relativeRotation;
    const auto prefixLength = tailLength - fade;
    const auto suffixLength = relativeRotation - fade;
    const auto useLinearFade = result.preferLinearRepairFade;

    for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
    {
        if (prefixLength > 0)
            rendered.copyFrom(channel, 0, source, channel,
                              rotation, prefixLength);
        for (int sample = 0; sample < fade; ++sample)
        {
            const auto position = static_cast<float>(sample + 1)
                                  / static_cast<float>(fade + 1);
            const auto tailGain = useLinearFade ? 1.0f - position
                : std::cos(position * juce::MathConstants<float>::halfPi);
            const auto headGain = useLinearFade ? position
                : std::sin(position * juce::MathConstants<float>::halfPi);
            const auto tail = source.getSample(
                channel, result.endSample - fade + sample);
            const auto head = source.getSample(
                channel, rangeStart + sample);
            rendered.setSample(channel, prefixLength + sample,
                               tailGain * tail + headGain * head);
        }
        if (suffixLength > 0)
            rendered.copyFrom(channel, prefixLength + fade, source, channel,
                              rangeStart + fade, suffixLength);
    }

    const auto targetSamples = juce::jmax(
        renderedSamples, result.targetOutputSamples);
    if (targetSamples > renderedSamples)
    {
        juce::AudioBuffer<float> extended(rendered.getNumChannels(), targetSamples);
        const auto repetitions = juce::jmax(2, result.repetitionCount);
        const auto phaseAdvance = static_cast<double>(renderedSamples * repetitions)
                                  / static_cast<double>(targetSamples);
        for (int channel = 0; channel < extended.getNumChannels(); ++channel)
        {
            for (int sample = 0; sample < targetSamples; ++sample)
            {
                const auto phase = std::fmod(sample * phaseAdvance,
                                             static_cast<double>(renderedSamples));
                const auto first = static_cast<int>(phase);
                const auto next = (first + 1) % renderedSamples;
                const auto fraction = static_cast<float>(phase - first);
                extended.setSample(channel, sample,
                    juce::jmap(fraction, rendered.getSample(channel, first),
                              rendered.getSample(channel, next)));
            }
        }
        rendered = std::move(extended);
    }

    juce::ignoreUnused(SignalDiagnostics::repairNonFiniteAndRemoveDc(rendered));
    juce::ignoreUnused(SignalDiagnostics::applyCircularTruePeakCeiling(rendered, -1.0f));
    return rendered;
}

LoopAnalysisResult LoopAnalyzer::findBestLoop(const juce::AudioBuffer<float>& audio,
                                               const double sampleRate,
                                               const int requestedLoopSamples,
                                               const int searchRadiusSamples)
{
    LoopAnalysisResult fallback;
    fallback.endSample = juce::jmin(audio.getNumSamples(), requestedLoopSamples);
    if (audio.getNumChannels() == 0 || audio.getNumSamples() < 8)
        return fallback;
    const auto period = PeriodCandidate { requestedLoopSamples, 100.0f };
    const auto result = searchAtPeriod(audio, sampleRate, period, searchRadiusSamples,
                                       0, sampleRms(audio, 0, audio.getNumSamples()));
    return result.candidateFitness < 0.0f ? fallback : result;
}

LoopAnalysisResult LoopAnalyzer::evaluateFixedRange(const juce::AudioBuffer<float>& audio,
                                                     const double sampleRate,
                                                     const int startSample,
                                                     const int endSample,
                                                     const int repairOverlapSamples)
{
    LoopAnalysisResult empty;
    if (audio.getNumChannels() == 0 || startSample < 0 || endSample > audio.getNumSamples()
        || endSample - startSample < 32)
        return empty;
    const auto window = juce::jlimit(16, juce::jmin(512, (endSample - startSample) / 4),
                                     juce::roundToInt(sampleRate * 0.012));
    const auto repair = juce::jlimit(0, (endSample - startSample) / 3,
                                     repairOverlapSamples);
    return evaluateCandidate(audio, startSample, endSample,
                             endSample - startSample - repair,
                             window, true, 100.0f, repair,
                             sampleRms(audio, 0, audio.getNumSamples()), sampleRate);
}
