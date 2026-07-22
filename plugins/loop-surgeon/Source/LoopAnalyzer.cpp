#include "LoopAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
constexpr auto pi = 3.14159265358979323846;

struct Candidate
{
    LoopAnalysisResult result;
    float cheapScore = 0.0f;
};

struct PeriodCandidate
{
    int samples = 0;
    float score = 0.0f;
};

float clampScore(const double value)
{
    return static_cast<float>(juce::jlimit(0.0, 100.0, value));
}

double sampleRms(const juce::AudioBuffer<float>& audio, const int start, const int length)
{
    double energy = 0.0;
    int count = 0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto* samples = audio.getReadPointer(channel, start);
        for (int index = 0; index < length; ++index)
        {
            const auto value = static_cast<double>(samples[index]);
            energy += value * value;
            ++count;
        }
    }
    return std::sqrt(energy / static_cast<double>(juce::jmax(1, count)));
}

float calculateLevelScore(const juce::AudioBuffer<float>& audio, const int start,
                          const int end, const int window)
{
    const auto head = sampleRms(audio, start, window);
    const auto tail = sampleRms(audio, end - window, window);
    const auto scale = juce::jmax(1.0e-7, 0.5 * (head + tail));
    return clampScore(100.0 * std::exp(-2.5 * std::abs(head - tail) / scale));
}

float calculateSlopeScore(const juce::AudioBuffer<float>& audio, const int start, const int end)
{
    double error = 0.0;
    double scale = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto head = static_cast<double>(audio.getSample(channel, start + 1)
                                              - audio.getSample(channel, start));
        const auto tail = static_cast<double>(audio.getSample(channel, end - 1)
                                              - audio.getSample(channel, end - 2));
        error += std::abs(head - tail);
        scale += 0.5 * (std::abs(head) + std::abs(tail));
    }
    return clampScore(100.0 * std::exp(-2.0 * error / juce::jmax(1.0e-7, scale)));
}

float calculateWaveformScore(const juce::AudioBuffer<float>& audio, const int start,
                             const int end, const int window)
{
    double error = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        error += std::abs(static_cast<double>(audio.getSample(channel, end - 1)
                                             - audio.getSample(channel, start)));
    const auto scale = sampleRms(audio, start, window)
                       + sampleRms(audio, end - window, window) + 1.0e-7;
    return clampScore(100.0 * std::exp(-3.0 * error
                                       / (scale * juce::jmax(1, audio.getNumChannels()))));
}

float calculatePhaseScore(const juce::AudioBuffer<float>& audio, const int start,
                          const int end, const int window)
{
    double dot = 0.0;
    double headEnergy = 0.0;
    double tailEnergy = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        for (int index = 0; index < window; ++index)
        {
            const auto head = static_cast<double>(audio.getSample(channel, start + index));
            const auto tail = static_cast<double>(audio.getSample(channel, end - window + index));
            dot += head * tail;
            headEnergy += head * head;
            tailEnergy += tail * tail;
        }
    }
    const auto denominator = std::sqrt(headEnergy * tailEnergy);
    if (denominator < 1.0e-12)
        return 100.0f;
    return clampScore(50.0 * (juce::jlimit(-1.0, 1.0, dot / denominator) + 1.0));
}

double stereoCorrelation(const juce::AudioBuffer<float>& audio, const int start, const int window)
{
    if (audio.getNumChannels() < 2)
        return 1.0;
    double dot = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    for (int index = 0; index < window; ++index)
    {
        const auto left = static_cast<double>(audio.getSample(0, start + index));
        const auto right = static_cast<double>(audio.getSample(1, start + index));
        dot += left * right;
        leftEnergy += left * left;
        rightEnergy += right * right;
    }
    const auto denominator = std::sqrt(leftEnergy * rightEnergy);
    return denominator < 1.0e-12 ? 1.0 : juce::jlimit(-1.0, 1.0, dot / denominator);
}

float calculateStereoScore(const juce::AudioBuffer<float>& audio, const int start,
                           const int end, const int window)
{
    if (audio.getNumChannels() < 2)
        return 100.0f;
    return clampScore(100.0 * (1.0 - 0.5 * std::abs(stereoCorrelation(audio, start, window)
                                                    - stereoCorrelation(audio, end - window, window))));
}

std::array<double, 8> spectralSignature(const juce::AudioBuffer<float>& audio,
                                        const int start, const int window)
{
    std::array<double, 8> signature {};
    const auto channelScale = 1.0 / static_cast<double>(juce::jmax(1, audio.getNumChannels()));
    for (size_t band = 0; band < signature.size(); ++band)
    {
        double real = 0.0;
        double imaginary = 0.0;
        for (int index = 0; index < window; ++index)
        {
            double mono = 0.0;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                mono += static_cast<double>(audio.getSample(channel, start + index));
            mono *= channelScale;
            const auto hann = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(index)
                                                  / static_cast<double>(window - 1));
            const auto phase = 2.0 * pi * static_cast<double>(band + 1) * index / window;
            real += mono * hann * std::cos(phase);
            imaginary -= mono * hann * std::sin(phase);
        }
        signature[band] = std::sqrt(real * real + imaginary * imaginary) + 1.0e-9;
    }
    return signature;
}

float calculateSpectrumScore(const juce::AudioBuffer<float>& audio, const int start,
                             const int end, const int window)
{
    const auto head = spectralSignature(audio, start, window);
    const auto tail = spectralSignature(audio, end - window, window);
    double error = 0.0;
    for (size_t band = 0; band < head.size(); ++band)
        error += std::abs(std::log(head[band]) - std::log(tail[band]));
    return clampScore(100.0 * std::exp(-0.9 * error / static_cast<double>(head.size())));
}

float calculateTransientScore(const juce::AudioBuffer<float>& audio, const int start,
                              const int end, const int window)
{
    double mismatch = 0.0;
    double scale = 0.0;
    const auto comparison = juce::jmin(window - 1, 64);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        for (int index = 1; index <= comparison; ++index)
        {
            const auto headDelta = std::abs(audio.getSample(channel, start + index)
                                            - audio.getSample(channel, start + index - 1));
            const auto tailDelta = std::abs(audio.getSample(channel, end - window + index)
                                            - audio.getSample(channel, end - window + index - 1));
            mismatch += std::abs(headDelta - tailDelta);
            scale += 0.5 * (headDelta + tailDelta);
        }
    }
    return clampScore(100.0 * std::exp(-1.5 * mismatch / juce::jmax(1.0e-7, scale)));
}

LoopAnalysisResult evaluateCandidate(const juce::AudioBuffer<float>& audio, const int start,
                                     const int end, const int requestedLoopSamples,
                                     const int window, const bool detailed,
                                     const float periodicity)
{
    const auto effectiveWindow = detailed ? window : juce::jmin(window, 64);
    LoopAnalysisResult result;
    result.startSample = start;
    result.endSample = end;
    result.waveform = calculateWaveformScore(audio, start, end, effectiveWindow);
    result.level = calculateLevelScore(audio, start, end, effectiveWindow);
    result.slope = calculateSlopeScore(audio, start, end);
    result.phase = calculatePhaseScore(audio, start, end, effectiveWindow);
    result.stereo = calculateStereoScore(audio, start, end, effectiveWindow);
    result.transient = calculateTransientScore(audio, start, end, effectiveWindow);
    result.periodicity = periodicity;
    result.spectrum = detailed ? calculateSpectrumScore(audio, start, end, window) : 0.0f;
    const auto durationError = std::abs((end - start) - requestedLoopSamples)
                               / static_cast<double>(juce::jmax(1, requestedLoopSamples));
    const auto duration = clampScore(100.0 * std::exp(-8.0 * durationError));
    result.overall = 0.18f * result.waveform + 0.13f * result.level
                     + 0.10f * result.slope + 0.17f * result.phase
                     + 0.08f * result.stereo + 0.12f * result.transient
                     + 0.04f * duration + 0.06f * result.periodicity
                     + (detailed ? 0.12f * result.spectrum : 0.0f);
    return result;
}

void keepBest(std::vector<Candidate>& finalists, Candidate candidate, const size_t maximum)
{
    if (finalists.size() < maximum)
    {
        finalists.push_back(std::move(candidate));
        return;
    }
    const auto weakest = std::min_element(finalists.begin(), finalists.end(),
                                          [] (const auto& left, const auto& right)
                                          {
                                              return left.cheapScore < right.cheapScore;
                                          });
    if (candidate.cheapScore > weakest->cheapScore)
        *weakest = std::move(candidate);
}

std::vector<PeriodCandidate> findPeriods(const juce::AudioBuffer<float>& audio,
                                         const double sampleRate,
                                         const int minimumSamples,
                                         const int maximumSamples)
{
    const auto hop = juce::jmax(1, juce::roundToInt(sampleRate / 100.0));
    const auto frames = audio.getNumSamples() / hop;
    constexpr std::array<int, 4> spectralCycles { 1, 3, 8, 20 };
    constexpr auto featureCount = spectralCycles.size() + 2;
    using FeatureFrame = std::array<double, featureCount>;
    std::array<std::vector<double>, spectralCycles.size()> cosines;
    std::array<std::vector<double>, spectralCycles.size()> sines;
    for (size_t band = 0; band < spectralCycles.size(); ++band)
    {
        cosines[band].resize(static_cast<size_t>(hop));
        sines[band].resize(static_cast<size_t>(hop));
        for (int index = 0; index < hop; ++index)
        {
            const auto phase = 2.0 * pi * spectralCycles[band] * index / hop;
            cosines[band][static_cast<size_t>(index)] = std::cos(phase);
            sines[band][static_cast<size_t>(index)] = std::sin(phase);
        }
    }

    std::vector<FeatureFrame> features(static_cast<size_t>(frames));
    for (int frame = 0; frame < frames; ++frame)
    {
        auto& feature = features[static_cast<size_t>(frame)];
        std::array<double, spectralCycles.size()> real {};
        std::array<double, spectralCycles.size()> imaginary {};
        double energy = 0.0;
        double derivative = 0.0;
        double previous = 0.0;
        for (int index = 0; index < hop; ++index)
        {
            double mono = 0.0;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                mono += audio.getSample(channel, frame * hop + index);
            mono /= juce::jmax(1, audio.getNumChannels());
            energy += mono * mono;
            if (index > 0)
                derivative += std::abs(mono - previous);
            previous = mono;
            for (size_t band = 0; band < spectralCycles.size(); ++band)
            {
                real[band] += mono * cosines[band][static_cast<size_t>(index)];
                imaginary[band] -= mono * sines[band][static_cast<size_t>(index)];
            }
        }
        feature[0] = std::log(std::sqrt(energy / hop) + 1.0e-9);
        feature[1] = std::log(derivative / juce::jmax(1, hop - 1) + 1.0e-9);
        for (size_t band = 0; band < spectralCycles.size(); ++band)
            feature[band + 2] = std::log(std::sqrt(real[band] * real[band]
                                                   + imaginary[band] * imaginary[band]) + 1.0e-9);
    }

    for (size_t featureIndex = 0; featureIndex < featureCount; ++featureIndex)
    {
        double mean = 0.0;
        for (const auto& frame : features)
            mean += frame[featureIndex];
        mean /= juce::jmax<size_t>(1, features.size());
        double variance = 0.0;
        for (const auto& frame : features)
            variance += (frame[featureIndex] - mean) * (frame[featureIndex] - mean);
        const auto scale = std::sqrt(variance / juce::jmax<size_t>(1, features.size())) + 1.0e-9;
        for (auto& frame : features)
            frame[featureIndex] = (frame[featureIndex] - mean) / scale;
    }

    const auto minimumLag = juce::jmax(1, minimumSamples / hop);
    const auto maximumLag = juce::jmin(frames - 2, maximumSamples / hop);
    std::vector<PeriodCandidate> periods;
    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        double dot = 0.0;
        double firstEnergy = 0.0;
        double secondEnergy = 0.0;
        for (int index = 0; index + lag < frames; ++index)
        {
            for (size_t featureIndex = 0; featureIndex < featureCount; ++featureIndex)
            {
                const auto first = features[static_cast<size_t>(index)][featureIndex];
                const auto second = features[static_cast<size_t>(index + lag)][featureIndex];
                dot += first * second;
                firstEnergy += first * first;
                secondEnergy += second * second;
            }
        }
        const auto denominator = std::sqrt(firstEnergy * secondEnergy);
        const auto correlation = denominator > 1.0e-12 ? juce::jlimit(-1.0, 1.0, dot / denominator) : 0.0;
        periods.push_back({ lag * hop, clampScore(50.0 * (correlation + 1.0)) });
    }
    std::sort(periods.begin(), periods.end(), [] (const auto& left, const auto& right)
    {
        return left.score > right.score;
    });
    std::vector<PeriodCandidate> distinct;
    for (const auto& period : periods)
    {
        const auto duplicate = std::any_of(distinct.begin(), distinct.end(), [&] (const auto& kept)
        {
            return std::abs(kept.samples - period.samples) < juce::jmax(hop * 3, kept.samples / 20);
        });
        if (!duplicate)
            distinct.push_back(period);
        if (distinct.size() == 6)
            break;
    }
    return distinct;
}

LoopAnalysisResult searchAtPeriod(const juce::AudioBuffer<float>& audio, const double sampleRate,
                                  const PeriodCandidate period, const int refinementRadius,
                                  const int repairOverlapSamples)
{
    const auto window = juce::jlimit(16, juce::jmin(512, audio.getNumSamples() / 4),
                                     juce::roundToInt(sampleRate * 0.012));
    const auto startStep = juce::jmax(1, juce::roundToInt(sampleRate * 0.02));
    const auto lengthStep = juce::jmax(1, juce::roundToInt(sampleRate * 0.0025));
    const auto requestedSourceSpan = period.samples + juce::jmax(0, repairOverlapSamples);
    std::vector<Candidate> finalists;
    for (int start = 0; start + requestedSourceSpan - refinementRadius < audio.getNumSamples(); start += startStep)
    {
        for (int offset = -refinementRadius; offset <= refinementRadius; offset += lengthStep)
        {
            const auto end = start + requestedSourceSpan + offset;
            if (end > audio.getNumSamples() || end - start < window * 2)
                continue;
            Candidate candidate;
            candidate.result = evaluateCandidate(audio, start, end, requestedSourceSpan,
                                                  window, false, period.score);
            candidate.cheapScore = candidate.result.overall;
            keepBest(finalists, std::move(candidate), 16);
        }
    }
    LoopAnalysisResult best;
    best.overall = -1.0f;
    for (const auto& candidate : finalists)
    {
        const auto detailed = evaluateCandidate(audio, candidate.result.startSample,
                                                candidate.result.endSample, requestedSourceSpan,
                                                window, true, period.score);
        if (detailed.overall > best.overall)
            best = detailed;
    }
    if (best.overall < 0.0f)
        return best;

    // The broad search is millisecond-scale for speed. Refine the best pair at
    // single-sample resolution so the published boundary is not grid-limited.
    const auto sampleRadius = juce::jlimit(1, 128, juce::roundToInt(sampleRate * 0.0025));
    std::vector<Candidate> sampleFinalists;
    for (int startOffset = -sampleRadius; startOffset <= sampleRadius; ++startOffset)
    {
        const auto start = best.startSample + startOffset;
        if (start < 0)
            continue;
        for (int endOffset = -sampleRadius; endOffset <= sampleRadius; ++endOffset)
        {
            const auto end = best.endSample + endOffset;
            if (end > audio.getNumSamples() || end - start < window * 2)
                continue;
            Candidate candidate;
            candidate.result = evaluateCandidate(audio, start, end, requestedSourceSpan,
                                                  window, false, period.score);
            candidate.cheapScore = candidate.result.overall;
            keepBest(sampleFinalists, std::move(candidate), 16);
        }
    }
    for (const auto& candidate : sampleFinalists)
    {
        const auto detailed = evaluateCandidate(audio, candidate.result.startSample,
                                                candidate.result.endSample, requestedSourceSpan,
                                                window, true, period.score);
        if (detailed.overall > best.overall)
            best = detailed;
    }
    return best;
}
}

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
    const auto periods = findPeriods(audio, sampleRate, minimumLoopSamples, maximumLoopSamples);
    for (const auto& period : periods)
    {
        const auto radius = juce::jmin(juce::roundToInt(sampleRate * 0.08), period.samples / 12);
        auto result = searchAtPeriod(audio, sampleRate, period, radius, repairOverlapSamples);
        if (result.overall >= 0.0f)
        {
            result.repairOverlapSamples = repairOverlapSamples;
            report.candidates.push_back(result);
        }
    }
    std::sort(report.candidates.begin(), report.candidates.end(), [] (const auto& left, const auto& right)
    {
        return left.overall > right.overall;
    });
    if (report.candidates.size() > static_cast<size_t>(juce::jmax(1, maximumCandidates)))
        report.candidates.resize(static_cast<size_t>(juce::jmax(1, maximumCandidates)));
    report.lowConfidence = report.candidates.empty()
                           || report.candidates.front().overall < 62.0f
                           || report.candidates.front().periodicity < 58.0f;
    return report;
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
    const auto result = searchAtPeriod(audio, sampleRate, period, searchRadiusSamples, 0);
    return result.overall < 0.0f ? fallback : result;
}

LoopAnalysisResult LoopAnalyzer::evaluateFixedRange(const juce::AudioBuffer<float>& audio,
                                                     const double sampleRate,
                                                     const int startSample,
                                                     const int endSample)
{
    LoopAnalysisResult empty;
    if (audio.getNumChannels() == 0 || startSample < 0 || endSample > audio.getNumSamples()
        || endSample - startSample < 32)
        return empty;
    const auto window = juce::jlimit(16, juce::jmin(512, (endSample - startSample) / 4),
                                     juce::roundToInt(sampleRate * 0.012));
    return evaluateCandidate(audio, startSample, endSample, endSample - startSample,
                             window, true, 100.0f);
}
