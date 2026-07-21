#include "LoopAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr auto pi = 3.14159265358979323846;

struct Candidate
{
    LoopAnalysisResult result;
    float cheapScore = 0.0f;
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

float calculateLevelScore(const juce::AudioBuffer<float>& audio,
                          const int start,
                          const int end,
                          const int window)
{
    const auto headRms = sampleRms(audio, start, window);
    const auto tailRms = sampleRms(audio, end - window, window);
    const auto scale = juce::jmax(1.0e-7, 0.5 * (headRms + tailRms));
    return clampScore(100.0 * std::exp(-2.5 * std::abs(headRms - tailRms) / scale));
}

float calculateSlopeScore(const juce::AudioBuffer<float>& audio,
                          const int start,
                          const int end)
{
    double error = 0.0;
    double scale = 0.0;

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto headSlope = static_cast<double>(audio.getSample(channel, start + 1)
                                                   - audio.getSample(channel, start));
        const auto tailSlope = static_cast<double>(audio.getSample(channel, end - 1)
                                                   - audio.getSample(channel, end - 2));
        error += std::abs(headSlope - tailSlope);
        scale += 0.5 * (std::abs(headSlope) + std::abs(tailSlope));
    }

    return clampScore(100.0 * std::exp(-2.0 * error / juce::jmax(1.0e-7, scale)));
}

float calculatePhaseScore(const juce::AudioBuffer<float>& audio,
                          const int start,
                          const int end,
                          const int window)
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

    const auto correlation = juce::jlimit(-1.0, 1.0, dot / denominator);
    return clampScore(50.0 * (correlation + 1.0));
}

double stereoCorrelation(const juce::AudioBuffer<float>& audio,
                         const int start,
                         const int window)
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

float calculateStereoScore(const juce::AudioBuffer<float>& audio,
                           const int start,
                           const int end,
                           const int window)
{
    if (audio.getNumChannels() < 2)
        return 100.0f;

    const auto head = stereoCorrelation(audio, start, window);
    const auto tail = stereoCorrelation(audio, end - window, window);
    return clampScore(100.0 * (1.0 - 0.5 * std::abs(head - tail)));
}

std::array<double, 8> spectralSignature(const juce::AudioBuffer<float>& audio,
                                        const int start,
                                        const int window)
{
    std::array<double, 8> signature {};
    const auto channelScale = 1.0 / static_cast<double>(juce::jmax(1, audio.getNumChannels()));

    for (size_t band = 0; band < signature.size(); ++band)
    {
        const auto cycles = static_cast<double>(band + 1);
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
            const auto phase = 2.0 * pi * cycles * static_cast<double>(index)
                               / static_cast<double>(window);
            real += mono * hann * std::cos(phase);
            imaginary -= mono * hann * std::sin(phase);
        }
        signature[band] = std::sqrt(real * real + imaginary * imaginary) + 1.0e-9;
    }

    return signature;
}

float calculateSpectrumScore(const juce::AudioBuffer<float>& audio,
                             const int start,
                             const int end,
                             const int window)
{
    const auto head = spectralSignature(audio, start, window);
    const auto tail = spectralSignature(audio, end - window, window);
    double error = 0.0;
    for (size_t band = 0; band < head.size(); ++band)
        error += std::abs(std::log(head[band]) - std::log(tail[band]));
    error /= static_cast<double>(head.size());
    return clampScore(100.0 * std::exp(-0.9 * error));
}

LoopAnalysisResult evaluateCandidate(const juce::AudioBuffer<float>& audio,
                                     const int start,
                                     const int end,
                                     const int requestedLoopSamples,
                                     const int window,
                                     const bool includeSpectrum)
{
    LoopAnalysisResult result;
    result.startSample = start;
    result.endSample = end;
    result.level = calculateLevelScore(audio, start, end, window);
    result.slope = calculateSlopeScore(audio, start, end);
    result.phase = calculatePhaseScore(audio, start, end, window);
    result.stereo = calculateStereoScore(audio, start, end, window);
    result.spectrum = includeSpectrum ? calculateSpectrumScore(audio, start, end, window) : 0.0f;

    const auto durationError = std::abs((end - start) - requestedLoopSamples)
                               / static_cast<double>(juce::jmax(1, requestedLoopSamples));
    const auto durationScore = clampScore(100.0 * std::exp(-8.0 * durationError));
    result.overall = 0.22f * result.level
                     + 0.13f * result.slope
                     + 0.25f * result.phase
                     + 0.10f * result.stereo
                     + 0.05f * durationScore
                     + (includeSpectrum ? 0.25f * result.spectrum : 0.0f);
    return result;
}
}

LoopAnalysisResult LoopAnalyzer::findBestLoop(const juce::AudioBuffer<float>& capturedAudio,
                                               const double sampleRate,
                                               const int requestedLoopSamples,
                                               const int searchRadiusSamples)
{
    LoopAnalysisResult fallback;
    fallback.endSample = juce::jmin(capturedAudio.getNumSamples(), requestedLoopSamples);

    if (capturedAudio.getNumChannels() == 0 || capturedAudio.getNumSamples() < 8)
        return fallback;

    const auto window = juce::jlimit(8,
                                     juce::jmin(512, capturedAudio.getNumSamples() / 4),
                                     juce::roundToInt(sampleRate * 0.012));
    const auto step = juce::jmax(1, juce::roundToInt(sampleRate * 0.005));
    const auto radius = juce::jlimit(0, capturedAudio.getNumSamples() / 3, searchRadiusSamples);
    const auto minimumLength = juce::jmax(window * 2, requestedLoopSamples - radius);
    const auto maximumLength = requestedLoopSamples + radius;
    std::vector<Candidate> finalists;
    finalists.reserve(12);

    for (int start = 0; start <= radius; start += step)
    {
        for (int length = minimumLength; length <= maximumLength; length += step)
        {
            const auto end = start + length;
            if (end > capturedAudio.getNumSamples() || end - start < window * 2)
                continue;

            Candidate candidate;
            candidate.result = evaluateCandidate(capturedAudio, start, end,
                                                 requestedLoopSamples, window, false);
            candidate.cheapScore = candidate.result.overall;
            finalists.push_back(candidate);
            std::push_heap(finalists.begin(), finalists.end(), [] (const Candidate& left,
                                                                    const Candidate& right)
            {
                return left.cheapScore > right.cheapScore;
            });

            if (finalists.size() > 12)
            {
                std::pop_heap(finalists.begin(), finalists.end(), [] (const Candidate& left,
                                                                       const Candidate& right)
                {
                    return left.cheapScore > right.cheapScore;
                });
                finalists.pop_back();
            }
        }
    }

    auto best = fallback;
    best.overall = -1.0f;
    for (const auto& candidate : finalists)
    {
        const auto detailed = evaluateCandidate(capturedAudio,
                                                candidate.result.startSample,
                                                candidate.result.endSample,
                                                requestedLoopSamples,
                                                window,
                                                true);
        if (detailed.overall > best.overall)
            best = detailed;
    }

    return best.overall < 0.0f ? fallback : best;
}
