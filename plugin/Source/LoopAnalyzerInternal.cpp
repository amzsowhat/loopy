#include "LoopAnalyzerInternal.h"

#include "SignalDiagnostics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace loopAnalyzerInternal
{
constexpr auto pi = 3.14159265358979323846;

struct Candidate
{
    LoopAnalysisResult result;
    float coarseFitness = 0.0f;
};

float clampSimilarity(const double value)
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

float calculateLevelSimilarity(const juce::AudioBuffer<float>& audio, const int start,
                          const int end, const int window)
{
    const auto head = sampleRms(audio, start, window);
    const auto tail = sampleRms(audio, end - window, window);
    const auto scale = juce::jmax(1.0e-7, 0.5 * (head + tail));
    return clampSimilarity(100.0 * std::exp(-2.5 * std::abs(head - tail) / scale));
}

float calculateSlopeSimilarity(const juce::AudioBuffer<float>& audio, const int start, const int end)
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
    return clampSimilarity(100.0 * std::exp(-2.0 * error / juce::jmax(1.0e-7, scale)));
}

float calculateWaveformSimilarity(const juce::AudioBuffer<float>& audio, const int start,
                             const int end, const int window)
{
    double error = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        error += std::abs(static_cast<double>(audio.getSample(channel, end - 1)
                                             - audio.getSample(channel, start)));
    const auto scale = sampleRms(audio, start, window)
                       + sampleRms(audio, end - window, window) + 1.0e-7;
    return clampSimilarity(100.0 * std::exp(-3.0 * error
                                       / (scale * juce::jmax(1, audio.getNumChannels()))));
}

float calculatePhaseSimilarity(const juce::AudioBuffer<float>& audio, const int start,
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
    return clampSimilarity(50.0 * (juce::jlimit(-1.0, 1.0, dot / denominator) + 1.0));
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

float calculateStereoSimilarity(const juce::AudioBuffer<float>& audio, const int start,
                           const int end, const int window)
{
    if (audio.getNumChannels() < 2)
        return 100.0f;
    return clampSimilarity(100.0 * (1.0 - 0.5 * std::abs(stereoCorrelation(audio, start, window)
                                                    - stereoCorrelation(audio, end - window, window))));
}

std::array<double, 16> spectralSignature(const juce::AudioBuffer<float>& audio,
                                         const int start, const int window)
{
    constexpr std::array<int, 16> cycles {
        1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 80, 96, 128, 160
    };
    std::array<double, cycles.size()> signature {};
    const auto channelScale = 1.0 / static_cast<double>(juce::jmax(1, audio.getNumChannels()));
    for (size_t band = 0; band < signature.size(); ++band)
    {
        double real = 0.0;
        double imaginary = 0.0;
        const auto cycle = juce::jlimit(1, juce::jmax(1, window / 2 - 1), cycles[band]);
        for (int index = 0; index < window; ++index)
        {
            double mono = 0.0;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                mono += static_cast<double>(audio.getSample(channel, start + index));
            mono *= channelScale;
            const auto hann = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(index)
                                                  / static_cast<double>(window - 1));
            const auto phase = 2.0 * pi * static_cast<double>(cycle) * index / window;
            real += mono * hann * std::cos(phase);
            imaginary -= mono * hann * std::sin(phase);
        }
        signature[band] = std::sqrt(real * real + imaginary * imaginary) + 1.0e-9;
    }
    return signature;
}

float calculateSpectrumSimilarity(const juce::AudioBuffer<float>& audio, const int start,
                             const int end, const int window)
{
    const auto head = spectralSignature(audio, start, window);
    const auto tail = spectralSignature(audio, end - window, window);
    double error = 0.0;
    for (size_t band = 0; band < head.size(); ++band)
        error += std::abs(std::log(head[band]) - std::log(tail[band]));
    return clampSimilarity(100.0 * std::exp(-0.9 * error / static_cast<double>(head.size())));
}

float calculateTransientSimilarity(const juce::AudioBuffer<float>& audio, const int start,
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
    return clampSimilarity(100.0 * std::exp(-1.5 * mismatch / juce::jmax(1.0e-7, scale)));
}

float calculateRepairSimilarity(const juce::AudioBuffer<float>& audio, const int start,
                                const int end, const int requestedFade,
                                const float phaseSimilarity)
{
    const auto rawSamples = end - start;
    const auto fade = juce::jlimit(0, rawSamples / 3, requestedFade);
    if (fade < 2)
        return 0.6f * calculateWaveformSimilarity(audio, start, end, juce::jmin(64, rawSamples / 4))
               + 0.4f * calculateSlopeSimilarity(audio, start, end);

    const auto renderedSamples = rawSamples - fade;
    const auto middleSamples = rawSamples - 2 * fade;
    if (renderedSamples < 8 || middleSamples < 0)
        return 0.0f;

    const auto useLinearFade = phaseSimilarity >= 75.0f;
    const auto repairedSample = [&] (const int channel, const int position)
    {
        if (position < middleSamples)
            return audio.getSample(channel, start + fade + position);

        const auto index = position - middleSamples;
        const auto alpha = static_cast<float>(index + 1) / static_cast<float>(fade + 1);
        const auto tailGain = useLinearFade ? 1.0f - alpha
                                            : std::cos(alpha * juce::MathConstants<float>::halfPi);
        const auto headGain = useLinearFade ? alpha
                                            : std::sin(alpha * juce::MathConstants<float>::halfPi);
        return tailGain * audio.getSample(channel, end - fade + index)
               + headGain * audio.getSample(channel, start + index);
    };

    const auto comparison = juce::jlimit(4, 96, renderedSamples / 4);
    double dot = 0.0;
    double headEnergy = 0.0;
    double tailEnergy = 0.0;
    double jump = 0.0;
    double slopeError = 0.0;
    double derivativeScale = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto first = static_cast<double>(repairedSample(channel, 0));
        const auto second = static_cast<double>(repairedSample(channel, 1));
        const auto last = static_cast<double>(repairedSample(channel, renderedSamples - 1));
        const auto previous = static_cast<double>(repairedSample(channel, renderedSamples - 2));
        jump += std::abs(last - first);
        slopeError += std::abs((second - first) - (last - previous));
        derivativeScale += 0.5 * (std::abs(second - first) + std::abs(last - previous));

        for (int index = 0; index < comparison; ++index)
        {
            const auto head = static_cast<double>(repairedSample(channel, index));
            const auto tail = static_cast<double>(
                repairedSample(channel, renderedSamples - comparison + index));
            dot += head * tail;
            headEnergy += head * head;
            tailEnergy += tail * tail;
        }
    }

    const auto rmsScale = std::sqrt((headEnergy + tailEnergy)
                                    / static_cast<double>(juce::jmax(1, 2 * comparison
                                        * audio.getNumChannels()))) + 1.0e-8;
    const auto jumpSimilarity = clampSimilarity(100.0 * std::exp(
        -2.7 * jump / (rmsScale * juce::jmax(1, audio.getNumChannels()))));
    const auto repairedSlopeSimilarity = clampSimilarity(100.0 * std::exp(
        -1.8 * slopeError / juce::jmax(1.0e-8, derivativeScale)));
    const auto denominator = std::sqrt(headEnergy * tailEnergy);
    const auto correlation = denominator > 1.0e-12
                                 ? juce::jlimit(-1.0, 1.0, dot / denominator)
                                 : 0.0;
    const auto contextSimilarity = clampSimilarity(50.0 * (correlation + 1.0));

    return 0.45f * jumpSimilarity + 0.20f * repairedSlopeSimilarity
           + 0.35f * contextSimilarity;
}

float calculateRotationSafety(const juce::AudioBuffer<float>& audio,
                              const double sampleRate,
                              const int cut)
{
    const auto window = juce::jlimit(
        32, audio.getNumSamples() / 6,
        juce::roundToInt(sampleRate * 0.035));
    if (cut < window || cut + window > audio.getNumSamples())
        return 0.0f;

    const auto before = sampleRms(audio, cut - window, window);
    const auto after = sampleRms(audio, cut, window);
    const auto levelScale = juce::jmax(1.0e-8, 0.5 * (before + after));
    const auto levelSimilarity = clampSimilarity(
        100.0 * std::exp(-2.2 * std::abs(before - after) / levelScale));
    const auto spectrumSimilarity = calculateSpectrumSimilarity(
        audio, cut - window, cut + window, window);
    const auto stereoSimilarity = calculateStereoSimilarity(
        audio, cut - window, cut + window, window);

    auto jump = 0.0;
    auto derivativeEnergy = 0.0;
    auto derivativeCount = 0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        jump += std::abs(static_cast<double>(audio.getSample(channel, cut)
                                            - audio.getSample(channel, cut - 1)));
        for (int sample = cut - window + 1; sample < cut + window; ++sample)
        {
            const auto difference = static_cast<double>(audio.getSample(channel, sample)
                - audio.getSample(channel, sample - 1));
            derivativeEnergy += difference * difference;
            ++derivativeCount;
        }
    }
    const auto derivativeRms = std::sqrt(
        derivativeEnergy / static_cast<double>(juce::jmax(1, derivativeCount)));
    const auto boundarySimilarity = clampSimilarity(100.0 * std::exp(
        -1.8 * jump / juce::jmax(1.0e-8,
            derivativeRms * static_cast<double>(audio.getNumChannels()))));
    const auto position = static_cast<double>(cut)
                          / static_cast<double>(audio.getNumSamples());
    const auto centralityPreference = clampSimilarity(
        100.0 * std::exp(-2.0 * std::abs(position - 0.5)));
    return 0.28f * levelSimilarity + 0.30f * spectrumSimilarity
           + 0.14f * stereoSimilarity + 0.20f * boundarySimilarity
           + 0.08f * centralityPreference;
}

LoopAnalysisResult evaluateCandidate(const juce::AudioBuffer<float>& audio, const int start,
                                     const int end, const int requestedVisibleSamples,
                                     const int window, const bool detailed,
                                     const float periodicitySimilarity,
                                     const int repairOverlapSamples,
                                     const double sourceRms, const double sampleRate)
{
    const auto effectiveWindow = detailed ? window : juce::jmin(window, 64);
    const auto longWindow = detailed
        ? juce::jlimit(effectiveWindow,
                       juce::jmax(effectiveWindow, (end - start) / 4),
                       juce::roundToInt(sampleRate * 0.04))
        : effectiveWindow;
    LoopAnalysisResult result;
    result.startSample = start;
    result.endSample = end;
    result.repairOverlapSamples = juce::jlimit(0, (end - start) / 3, repairOverlapSamples);
    const auto waveformSimilarity = calculateWaveformSimilarity(
        audio, start, end, effectiveWindow);
    const auto levelSimilarity = detailed
        ? 0.55f * calculateLevelSimilarity(audio, start, end, effectiveWindow)
              + 0.45f * calculateLevelSimilarity(audio, start, end, longWindow)
        : calculateLevelSimilarity(audio, start, end, effectiveWindow);
    const auto slopeSimilarity = calculateSlopeSimilarity(audio, start, end);
    const auto phaseSimilarity = detailed
        ? 0.60f * calculatePhaseSimilarity(audio, start, end, effectiveWindow)
              + 0.40f * calculatePhaseSimilarity(audio, start, end, longWindow)
        : calculatePhaseSimilarity(audio, start, end, effectiveWindow);
    const auto stereoSimilarity = detailed
        ? 0.60f * calculateStereoSimilarity(audio, start, end, effectiveWindow)
              + 0.40f * calculateStereoSimilarity(audio, start, end, longWindow)
        : calculateStereoSimilarity(audio, start, end, effectiveWindow);
    const auto transientSimilarity = detailed
        ? 0.65f * calculateTransientSimilarity(audio, start, end, effectiveWindow)
              + 0.35f * calculateTransientSimilarity(audio, start, end, longWindow)
        : calculateTransientSimilarity(audio, start, end, effectiveWindow);
    const auto spectrumSimilarity = detailed
        ? calculateSpectrumSimilarity(audio, start, end, longWindow) : 0.0f;
    const auto repairSimilarity = calculateRepairSimilarity(
        audio, start, end, result.repairOverlapSamples, phaseSimilarity);
    result.preferLinearRepairFade = phaseSimilarity >= 75.0f;
    const auto visibleSamples = end - start - result.repairOverlapSamples;
    const auto durationError = std::abs(visibleSamples - requestedVisibleSamples)
                               / static_cast<double>(juce::jmax(1, requestedVisibleSamples));
    const auto durationSimilarity = clampSimilarity(100.0 * std::exp(-8.0 * durationError));
    const auto localRms = 0.5 * (sampleRms(audio, start, effectiveWindow)
                                 + sampleRms(audio, end - effectiveWindow, effectiveWindow));
    const auto activity = clampSimilarity(
        100.0 * localRms / juce::jmax(1.0e-8, sourceRms * 0.35));

    const auto weighted = detailed
        ? 0.13f * waveformSimilarity + 0.09f * levelSimilarity
              + 0.06f * slopeSimilarity + 0.12f * phaseSimilarity
              + 0.06f * stereoSimilarity + 0.11f * transientSimilarity
              + 0.13f * spectrumSimilarity + 0.20f * repairSimilarity
              + 0.06f * periodicitySimilarity + 0.04f * durationSimilarity
        : 0.18f * waveformSimilarity + 0.11f * levelSimilarity
              + 0.08f * slopeSimilarity + 0.16f * phaseSimilarity
              + 0.07f * stereoSimilarity + 0.13f * transientSimilarity
              + 0.20f * repairSimilarity + 0.07f * periodicitySimilarity;
    const auto weakestCritical = juce::jmin(
        waveformSimilarity, phaseSimilarity, transientSimilarity, repairSimilarity);
    result.candidateFitness = (0.82f * weighted + 0.18f * weakestCritical)
                              * (0.65f + 0.35f * activity / 100.0f);
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
                                              return left.coarseFitness < right.coarseFitness;
                                          });
    if (candidate.coarseFitness > weakest->coarseFitness)
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
        periods.push_back({ lag * hop, clampSimilarity(50.0 * (correlation + 1.0)) });
    }
    std::sort(periods.begin(), periods.end(), [] (const auto& left, const auto& right)
    {
        return left.periodicitySimilarity > right.periodicitySimilarity;
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
                                  const int maximumRepairOverlapSamples,
                                  const double sourceRms)
{
    const auto window = juce::jlimit(16, juce::jmin(512, audio.getNumSamples() / 4),
                                     juce::roundToInt(sampleRate * 0.012));
    const auto startStep = juce::jmax(1, juce::roundToInt(sampleRate * 0.02));
    const auto lengthStep = juce::jmax(1, juce::roundToInt(sampleRate * 0.0025));
    const auto preferredRepair = juce::jmax(0, maximumRepairOverlapSamples);
    const auto requestedSourceSpan = period.samples + preferredRepair;
    std::vector<Candidate> finalists;
    for (int start = 0; start + requestedSourceSpan - refinementRadius < audio.getNumSamples(); start += startStep)
    {
        for (int offset = -refinementRadius; offset <= refinementRadius; offset += lengthStep)
        {
            const auto end = start + requestedSourceSpan + offset;
            if (end > audio.getNumSamples() || end - start < window * 2)
                continue;
            Candidate candidate;
            candidate.result = evaluateCandidate(audio, start, end, period.samples,
                                                  window, false, period.periodicitySimilarity,
                                                  preferredRepair,
                                                  sourceRms, sampleRate);
            candidate.coarseFitness = candidate.result.candidateFitness;
            keepBest(finalists, std::move(candidate), 16);
        }
    }
    LoopAnalysisResult best;
    best.candidateFitness = -1.0f;
    std::vector<int> repairOptions { preferredRepair };
    if (preferredRepair >= 4)
    {
        repairOptions.push_back(juce::jmax(2, preferredRepair / 3));
        repairOptions.push_back(juce::jmax(2, 2 * preferredRepair / 3));
    }
    std::sort(repairOptions.begin(), repairOptions.end());
    repairOptions.erase(std::unique(repairOptions.begin(), repairOptions.end()),
                        repairOptions.end());

    for (const auto& candidate : finalists)
    {
        const auto visibleSamples = candidate.result.endSample
                                    - candidate.result.startSample - preferredRepair;
        for (const auto repair : repairOptions)
        {
            const auto adjustedEnd = candidate.result.startSample + visibleSamples + repair;
            if (adjustedEnd > audio.getNumSamples())
                continue;
            const auto detailed = evaluateCandidate(audio, candidate.result.startSample,
                                                    adjustedEnd, period.samples,
                                                    window, true,
                                                    period.periodicitySimilarity, repair,
                                                    sourceRms, sampleRate);
            if (detailed.candidateFitness > best.candidateFitness)
                best = detailed;
        }
    }
    if (best.candidateFitness < 0.0f)
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
            candidate.result = evaluateCandidate(audio, start, end, period.samples,
                                                  window, false, period.periodicitySimilarity,
                                                  best.repairOverlapSamples,
                                                  sourceRms, sampleRate);
            candidate.coarseFitness = candidate.result.candidateFitness;
            keepBest(sampleFinalists, std::move(candidate), 16);
        }
    }
    for (const auto& candidate : sampleFinalists)
    {
        const auto detailed = evaluateCandidate(audio, candidate.result.startSample,
                                                candidate.result.endSample, period.samples,
                                                window, true, period.periodicitySimilarity,
                                                best.repairOverlapSamples,
                                                sourceRms, sampleRate);
        if (detailed.candidateFitness > best.candidateFitness)
            best = detailed;
    }
    return best;
}
}
