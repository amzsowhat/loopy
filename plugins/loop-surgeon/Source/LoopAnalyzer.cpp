#include "LoopAnalyzer.h"

#include "SignalDiagnostics.h"

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
    float coarseFitness = 0.0f;
};

struct PeriodCandidate
{
    int samples = 0;
    float periodicitySimilarity = 0.0f;
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
    const auto waveformSimilarity = calculateWavã®½¶‰žËkºwµçE±åé•M½ÕÉ”¡½¹ÍÐ©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø˜…Õ‘¥¼°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ‘½Õ‰±”Í…µÁ±•I…Ñ”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¥¹Ðµ¥¹¥µÕµ1½½ÁM…µÁ±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¥¹Ðµ…á¥µÕµ1½½ÁM…µÁ±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ¥¹Ðµ…á¥µÕµ…¹‘¥‘…Ñ•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ¥¹ÐÉ•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤)ì(€€€1½½Á¹…±åÍ¥ÍI•Á½ÉÐÉ•Á½ÉÐì(€€€¥˜€¡…Õ‘¥¼¹•Ñ9Õµ¡…¹¹•±Ì ¤€ôô€Àñð…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤€ð€ÌÈñðÍ…µÁ±•I…Ñ”€ðô€À¸À¤(€€€€€€€É•ÑÕÉ¸É•Á½ÉÐì(€€€µ¥¹¥µÕµ1½½ÁM…µÁ±•Ì€ô©Õ”èé©±¥µ¥Ð ÄØ°…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤€¼€È°µ¥¹¥µÕµ1½½ÁM…µÁ±•Ì¤ì(€€€µ…á¥µÕµ1½½ÁM…µÁ±•Ì€ô©Õ”èé©±¥µ¥Ð¡µ¥¹¥µÕµ1½½ÁM…µÁ±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤€´€Ä€´©Õ”èé©µ…à À°É•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€µ…á¥µÕµ1½½ÁM…µÁ±•Ì¤ì(€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ•IµÌ€ôÍ…µÁ±•IµÌ¡…Õ‘¥¼°€À°…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤¤ì(€€€½¹ÍÐ…ÕÑ¼Á•É¥½‘Ì€ô™¥¹‘A•É¥½‘Ì¡…Õ‘¥¼°Í…µÁ±•I…Ñ”°µ¥¹¥µÕµ1½½ÁM…µÁ±•Ì°µ…á¥µÕµ1½½ÁM…µÁ±•Ì¤ì(€€€™½È€¡½¹ÍÐ…ÕÑ¼˜Á•É¥½€èÁ•É¥½‘Ì¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼É…‘¥ÕÌ€ô©Õ”èé©µ¥¸¡©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸Àà¤°Á•É¥½¹Í…µÁ±•Ì€¼€ÄÈ¤ì(€€€€€€€…ÕÑ¼É•ÍÕ±Ð€ôÍ•…É¡ÑA•É¥½¡…Õ‘¥¼°Í…µÁ±•I…Ñ”°Á•É¥½°É…‘¥ÕÌ°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É•Á…¥É=Ù•É±…ÁM…µÁ±•Ì°Í½ÕÉ•IµÌ¤ì(€€€€€€€¥˜€¡É•ÍÕ±Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€øô€À¸Á˜¤(€€€€€€€€€€€É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹ÁÕÍ¡}‰…¬¡É•ÍÕ±Ð¤ì(€€€ô(€€€ÍÑèéÍ½ÉÐ¡É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹‰•¥¸ ¤°É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹•¹ ¤°mt€¡½¹ÍÐ…ÕÑ¼˜±•™Ð°½¹ÍÐ…ÕÑ¼˜É¥¡Ð¤(€€€ì(€€€€€€€É•ÑÕÉ¸±•™Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€øÉ¥¡Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌì(€€€ô¤ì(€€€ÍÑèéÙ•Ñ½Èñ1½½Á¹…±åÍ¥ÍI•ÍÕ±Ðø‘¥Ù•ÉÍ”ì(€€€½¹ÍÐ…ÕÑ¼‘ÕÁ±¥…Ñ•MÑ…ÉÑQ½±•É…¹”€ô©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÀÔ¤ì(€€€™½È€¡½¹ÍÐ…ÕÑ¼˜…¹‘¥‘…Ñ”€èÉ•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼…¹‘¥‘…Ñ•1•¹Ñ €ô…¹‘¥‘…Ñ”¹•¹‘M…µÁ±”€´…¹‘¥‘…Ñ”¹ÍÑ…ÉÑM…µÁ±”(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€´…¹‘¥‘…Ñ”¹É•Á…¥É=Ù•É±…ÁM…µÁ±•Ìì(€€€€€€€½¹ÍÐ…ÕÑ¼‘ÕÁ±¥…Ñ”€ôÍÑèé…¹å}½˜¡‘¥Ù•ÉÍ”¹‰•¥¸ ¤°‘¥Ù•ÉÍ”¹•¹ ¤°l™t€¡½¹ÍÐ…ÕÑ¼˜­•ÁÐ¤(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼­•ÁÑ1•¹Ñ €ô­•ÁÐ¹•¹‘M…µÁ±”€´­•ÁÐ¹ÍÑ…ÉÑM…µÁ±”€´­•ÁÐ¹É•Á…¥É=Ù•É±…ÁM…µÁ±•Ìì(€€€€€€€€€€€É•ÑÕÉ¸ÍÑèé…‰Ì¡…¹‘¥‘…Ñ”¹ÍÑ…ÉÑM…µÁ±”€´­•ÁÐ¹ÍÑ…ÉÑM…µÁ±”¤€ð‘ÕÁ±¥…Ñ•MÑ…ÉÑQ½±•É…¹”(€€€€€€€€€€€€€€€€€€€˜˜ÍÑèé…‰Ì¡…¹‘¥‘…Ñ•1•¹Ñ €´­•ÁÑ1•¹Ñ ¤(€€€€€€€€€€€€€€€€€€€€€€€€€€ð©Õ”èé©µ…à à°…¹‘¥‘…Ñ•1•¹Ñ €¼€ÐÀ¤ì(€€€€€€€ô¤ì(€€€€€€€¥˜€ …‘ÕÁ±¥…Ñ”¤(€€€€€€€€€€€‘¥Ù•ÉÍ”¹ÁÕÍ¡}‰…¬¡…¹‘¥‘…Ñ”¤ì(€€€€€€€¥˜€¡‘¥Ù•ÉÍ”¹Í¥é” ¤€øôÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡©Õ”èé©µ…à Ä°µ…á¥µÕµ…¹‘¥‘…Ñ•Ì¤¤¤(€€€€€€€€€€€‰É•…¬ì(€€€ô(€€€É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì€ôÍÑèéµ½Ù”¡‘¥Ù•ÉÍ”¤ì(€€€É•ÑÕÉ¸É•Á½ÉÐì)ô()1½½Á¹…±åÍ¥ÍI•Á½ÉÐ1½½Á¹…±åé•Èèé…¹…±åé•I½Ñ…Ñ•I•Á…¥È (€€€½¹ÍÐ©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø˜…Õ‘¥¼°(€€€½¹ÍÐ‘½Õ‰±”Í…µÁ±•I…Ñ”°(€€€½¹ÍÐ¥¹Ðµ…á¥µÕµ…¹‘¥‘…Ñ•Ì°(€€€½¹ÍÐ¥¹Ðµ…á¥µÕµI•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤)ì(€€€1½½Á¹…±åÍ¥ÍI•Á½ÉÐÉ•Á½ÉÐì(€€€½¹ÍÐ…ÕÑ¼Í…µÁ±•Ì€ô…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤ì(€€€¥˜€¡…Õ‘¥¼¹•Ñ9Õµ¡…¹¹•±Ì ¤€ôô€ÀñðÍ…µÁ±•Ì€ð€ÈÔØñðÍ…µÁ±•I…Ñ”€ðô€À¸À¤(€€€€€€€É•ÑÕÉ¸É•Á½ÉÐì((€€€½¹ÍÐ…ÕÑ¼µ…á¥µÕµ…‘”€ô©Õ”èé©±¥µ¥Ð (€€€€€€€€À°Í…µÁ±•Ì€¼€à°µ…á¥µÕµI•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤ì(€€€ÍÑèéÙ•Ñ½Èñ¥¹ÐøÉ•Á…¥É=ÁÑ¥½¹Ìì€Àôì(€€€™½È€¡½¹ÍÐ…ÕÑ¼µ¥±±¥Í•½¹‘Ì€èì€ÈÀ°€ÐÀ°€àÀ°€ÄÐÀ°€ÈÈÀô¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼É•Á…¥È€ô©Õ”èé©µ¥¸ (€€€€€€€€€€€µ…á¥µÕµ…‘”°©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨µ¥±±¥Í•½¹‘Ì€¨€À¸ÀÀÄ¤¤ì(€€€€€€€¥˜€¡É•Á…¥È€øô€È¤(€€€€€€€€€€€É•Á…¥É=ÁÑ¥½¹Ì¹ÁÕÍ¡}‰…¬¡É•Á…¥È¤ì(€€€ô(€€€ÍÑèéÍ½ÉÐ¡É•Á…¥É=ÁÑ¥½¹Ì¹‰•¥¸ ¤°É•Á…¥É=ÁÑ¥½¹Ì¹•¹ ¤¤ì(€€€É•Á…¥É=ÁÑ¥½¹Ì¹•É…Í”¡ÍÑèéÕ¹¥ÅÕ”¡É•Á…¥É=ÁÑ¥½¹Ì¹‰•¥¸ ¤°É•Á…¥É=ÁÑ¥½¹Ì¹•¹ ¤¤°(€€€€€€€€€€€€€€€€€€€€€€€É•Á…¥É=ÁÑ¥½¹Ì¹•¹ ¤¤ì((€€€1½½Á¹…±åÍ¥ÍI•ÍÕ±Ð‰•ÍÑI•Á…¥Èì(€€€‰•ÍÑI•Á…¥È¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€ô€´Ä¸Á˜ì(€€€™½È€¡½¹ÍÐ…ÕÑ¼É•Á…¥È€èÉ•Á…¥É=ÁÑ¥½¹Ì¤(€€€ì(€€€€€€€…ÕÑ¼…¹‘¥‘…Ñ”€ô•Ù…±Õ…Ñ•¥á•‘I…¹”¡…Õ‘¥¼°Í…µÁ±•I…Ñ”°€À°Í…µÁ±•Ì°É•Á…¥È¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼É•µ½Ù•‘É…Ñ¥½¸€ôÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡É•Á…¥È¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¼ÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡Í…µÁ±•Ì¤ì(€€€€€€€…¹‘¥‘…Ñ”¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€´ô€ÄÈ¸Á˜€¨É•µ½Ù•‘É…Ñ¥½¸ì(€€€€€€€¥˜€¡…¹‘¥‘…Ñ”¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€ø‰•ÍÑI•Á…¥È¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ¤(€€€€€€€€€€€‰•ÍÑI•Á…¥È€ô…¹‘¥‘…Ñ”ì(€€€ô(€€€¥˜€¡‰•ÍÑI•Á…¥È¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€ð€À¸Á˜¤(€€€€€€€É•ÑÕÉ¸É•Á½ÉÐì((€€€½¹ÍÐ…ÕÑ¼Õ…É€ô©Õ”èé©±¥µ¥Ð (€€€€€€€€ÌÈ°©Õ”èé©µ…à ÌÈ°Í…µÁ±•Ì€¼€Ì¤°(€€€€€€€©Õ”èé©µ…à¡‰•ÍÑI•Á…¥È¹É•Á…¥É=Ù•É±…ÁM…µÁ±•Ì€¬€ÌÈ°(€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÌÔ¤¤¤ì(€€€½¹ÍÐ…ÕÑ¼™¥ÉÍÑÕÐ€ô©Õ”èé©µ¥¸¡Í…µÁ±•Ì€´€Ä°Õ…É¤ì(€€€½¹ÍÐ…ÕÑ¼±…ÍÑÕÐ€ô©Õ”èé©µ…à¡™¥ÉÍÑÕÐ°Í…µÁ±•Ì€´Õ…É¤ì(€€€½¹ÍÐ…ÕÑ¼ÍÑ•À€ô©Õ”èé©µ…à Ä°©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÀÄ¤¤ì(€€€ÍÑÉÕÐI½Ñ…Ñ¥½¹…¹‘¥‘…Ñ”(€€€ì(€€€€€€€¥¹ÐÕÐ€ô€Àì(€€€€€€€™±½…ÐÍ…™•Ñä€ô€À¸Á˜ì(€€€ôì(€€€ÍÑèéÙ•Ñ½ÈñI½Ñ…Ñ¥½¹…¹‘¥‘…Ñ”øÉ½Ñ…Ñ¥½¹Ìì(€€€™½È€¡¥¹ÐÕÐ€ô™¥ÉÍÑÕÐìÕÐ€ðô±…ÍÑÕÐìÕÐ€¬ôÍÑ•À¤(€€€€€€€É½Ñ…Ñ¥½¹Ì¹ÁÕÍ¡}‰…¬¡ìÕÐ°…±Õ±…Ñ•I½Ñ…Ñ¥½¹M…™•Ñä¡…Õ‘¥¼°Í…µÁ±•I…Ñ”°ÕÐ¤ô¤ì(€€€ÍÑèéÍ½ÉÐ¡É½Ñ…Ñ¥½¹Ì¹‰•¥¸ ¤°É½Ñ…Ñ¥½¹Ì¹•¹ ¤°mt€¡½¹ÍÐ…ÕÑ¼˜±•™Ð°½¹ÍÐ…ÕÑ¼˜É¥¡Ð¤(€€€ì(€€€€€€€É•ÑÕÉ¸±•™Ð¹Í…™•Ñä€øÉ¥¡Ð¹Í…™•Ñäì(€€€ô¤ì((€€€½¹ÍÐ…ÕÑ¼‘¥ÍÑ¥¹Ñ¥ÍÑ…¹”€ô©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÐÀ¤ì(€€€™½È€¡½¹ÍÐ…ÕÑ¼˜É½Ñ…Ñ¥½¸€èÉ½Ñ…Ñ¥½¹Ì¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼‘ÕÁ±¥…Ñ”€ôÍÑèé…¹å}½˜ (€€€€€€€€€€€É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹‰•¥¸ ¤°É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹•¹ ¤°l™t€¡½¹ÍÐ…ÕÑ¼˜­•ÁÐ¤(€€€€€€€€€€€ì(€€€€€€€€€€€€€€€É•ÑÕÉ¸ÍÑèé…‰Ì¡­•ÁÐ¹É½Ñ…Ñ¥½¹M…µÁ±”€´É½Ñ…Ñ¥½¸¹ÕÐ¤€ð‘¥ÍÑ¥¹Ñ¥ÍÑ…¹”ì(€€€€€€€€€€€ô¤ì(€€€€€€€¥˜€¡‘ÕÁ±¥…Ñ”¤(€€€€€€€€€€€½¹Ñ¥¹Õ”ì(€€€€€€€…ÕÑ¼É•ÍÕ±Ð€ô‰•ÍÑI•Á…¥Èì(€€€€€€€É•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”€ô€Àì(€€€€€€€É•ÍÕ±Ð¹•¹‘M…µÁ±”€ôÍ…µÁ±•Ìì(€€€€€€€É•ÍÕ±Ð¹É½Ñ…Ñ¥½¹M…µÁ±”€ôÉ½Ñ…Ñ¥½¸¹ÕÐì(€€€€€€€É•ÍÕ±Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€ô€À¸Øá˜€¨‰•ÍÑI•Á…¥È¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¬€À¸ÌÉ˜€¨É½Ñ…Ñ¥½¸¹Í…™•Ñäì(€€€€€€€É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹ÁÕÍ¡}‰…¬¡É•ÍÕ±Ð¤ì(€€€€€€€¥˜€¡É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹Í¥é” ¤(€€€€€€€€€€€€øôÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡©Õ”èé©µ…à Ä°µ…á¥µÕµ…¹‘¥‘…Ñ•Ì¤¤¤(€€€€€€€€€€€‰É•…¬ì(€€€ô((€€€É•ÑÕÉ¸É•Á½ÉÐì)ô()1½½Á¹…±åÍ¥ÍI•Á½ÉÐ1½½Á¹…±åé•Èèé…¹…±åé•I½Ñ…Ñ•I•Á…¥Éá…Ð (€€€½¹ÍÐ©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø˜…Õ‘¥¼°(€€€½¹ÍÐ‘½Õ‰±”Í…µÁ±•I…Ñ”°(€€€½¹ÍÐ¥¹ÐÑ…É•Ñ=ÕÑÁÕÑM…µÁ±•Ì°(€€€½¹ÍÐ¥¹Ðµ…á¥µÕµ…¹‘¥‘…Ñ•Ì°(€€€½¹ÍÐ¥¹Ðµ…á¥µÕµI•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤)ì(€€€1½½Á¹…±åÍ¥ÍI•Á½ÉÐÉ•Á½ÉÐì(€€€½¹ÍÐ…ÕÑ¼Í…µÁ±•Ì€ô…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤ì(€€€¥˜€¡…Õ‘¥¼¹•Ñ9Õµ¡…¹¹•±Ì ¤€ôô€ÀñðÍ…µÁ±•I…Ñ”€ðô€À¸À(€€€€€€€ñðÑ…É•Ñ=ÕÑÁÕÑM…µÁ±•Ì€ð€ÈÔØñðÑ…É•Ñ=ÕÑÁÕÑM…µÁ±•Ì€øÍ…µÁ±•Ì¤(€€€€€€€É•ÑÕÉ¸É•Á½ÉÐì((€€€½¹ÍÐ…ÕÑ¼µ…á¥µÕµ…‘”€ô©Õ”èé©±¥µ¥Ð (€€€€€€€€À°Ñ…É•Ñ=ÕÑÁÕÑM…µÁ±•Ì€¼€à°µ…á¥µÕµI•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤ì(€€€ÍÑèéÙ•Ñ½Èñ¥¹ÐøÉ•Á…¥É=ÁÑ¥½¹Ìì€Àôì(€€€™½È€¡½¹ÍÐ…ÕÑ¼µ¥±±¥Í•½¹‘Ì€èì€ÈÀ°€ÐÀ°€àÀ°€ÄÐÀ°€ÈÈÀô¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼É•Á…¥È€ô©Õ”èé©µ¥¸ (€€€€€€€€€€€µ…á¥µÕµ…‘”°©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨µ¥±±¥Í•½¹‘Ì€¨€À¸ÀÀÄ¤¤ì(€€€€€€€¥˜€¡É•Á…¥È€øô€È€˜˜Ñ…É•Ñ=ÕÑÁÕÑM…µÁ±•Ì€¬É•Á…¥È€ðôÍ…µÁ±•Ì¤(€€€€€€€€€€€É•Á…¥É=ÁÑ¥½¹Ì¹ÁÕÍ¡}‰…¬¡É•Á…¥È¤ì(€€€ô(€€€ÍÑèéÍ½ÉÐ¡É•Á…¥É=ÁÑ¥½¹Ì¹‰•¥¸ ¤°É•Á…¥É=ÁÑ¥½¹Ì¹•¹ ¤¤ì(€€€É•Á…¥É=ÁÑ¥½¹Ì¹•É…Í”¡ÍÑèéÕ¹¥ÅÕ”¡É•Á…¥É=ÁÑ¥½¹Ì¹‰•¥¸ ¤°É•Á…¥É=ÁÑ¥½¹Ì¹•¹ ¤¤°(€€€€€€€€€€€€€€€€€€€€€€€É•Á…¥É=ÁÑ¥½¹Ì¹•¹ ¤¤ì((€€€ÍÑÉÕÐ]¥¹‘½Ý…¹‘¥‘…Ñ”(€€€ì(€€€€€€€1½½Á¹…±åÍ¥ÍI•ÍÕ±ÐÉ•ÍÕ±Ðì(€€€ôì(€€€ÍÑèéÙ•Ñ½Èñ]¥¹‘½Ý…¹‘¥‘…Ñ”øÝ¥¹‘½ÝÌì(€€€™½È€¡½¹ÍÐ…ÕÑ¼É•Á…¥È€èÉ•Á…¥É=ÁÑ¥½¹Ì¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼ÍÁ…¸€ôÑ…É•Ñ=ÕÑÁÕÑM…µÁ±•Ì€¬É•Á…¥Èì(€€€€€€€½¹ÍÐ…ÕÑ¼µ…á¥µÕµMÑ…ÉÐ€ôÍ…µÁ±•Ì€´ÍÁ…¸ì(€€€€€€€½¹ÍÐ…ÕÑ¼ÍÑ•À€ô©Õ”èé©µ…à (€€€€€€€€€€€€Ä°µ…á¥µÕµMÑ…ÉÐ€ø€À€ü©Õ”èé©µ…à¡©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÀÈÔ¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€µ…á¥µÕµMÑ…ÉÐ€¼€Ðà¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€è€Ä¤ì(€€€€€€€™½È€¡¥¹ÐÍÑ…ÉÐ€ô€ÀììÍÑ…ÉÐ€ô©Õ”èé©µ¥¸¡µ…á¥µÕµMÑ…ÉÐ°ÍÑ…ÉÐ€¬ÍÑ•À¤¤(€€€€€€€ì(€€€€€€€€€€€…ÕÑ¼…¹‘¥‘…Ñ”€ô•Ù…±Õ…Ñ•¥á•‘I…¹” (€€€€€€€€€€€€€€€…Õ‘¥¼°Í…µÁ±•I…Ñ”°ÍÑ…ÉÐ°ÍÑ…ÉÐ€¬ÍÁ…¸°É•Á…¥È¤ì(€€€€€€€€€€€…¹‘¥‘…Ñ”¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€´ô€Ô¸Á˜€¨ÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡É•Á…¥È¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¼ÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡ÍÁ…¸¤ì(€€€€€€€€€€€Ý¥¹‘½ÝÌ¹ÁÕÍ¡}‰…¬¡ì…¹‘¥‘…Ñ”ô¤ì(€€€€€€€€€€€¥˜€¡ÍÑ…ÉÐ€ôôµ…á¥µÕµMÑ…ÉÐ¤(€€€€€€€€€€€€€€€‰É•…¬ì(€€€€€€€ô(€€€ô(€€€¥˜€¡Ý¥¹‘½ÝÌ¹•µÁÑä ¤¤(€€€€€€€É•ÑÕÉ¸É•Á½ÉÐì((€€€ÍÑèéÍ½ÉÐ¡Ý¥¹‘½ÝÌ¹‰•¥¸ ¤°Ý¥¹‘½ÝÌ¹•¹ ¤°mt€¡½¹ÍÐ…ÕÑ¼˜±•™Ð°½¹ÍÐ…ÕÑ¼˜É¥¡Ð¤(€€€ì(€€€€€€€É•ÑÕÉ¸±•™Ð¹É•ÍÕ±Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€øÉ¥¡Ð¹É•ÍÕ±Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌì(€€€ô¤ì(€€€¥˜€¡Ý¥¹‘½ÝÌ¹Í¥é” ¤€ø€ÄÁÔ¤(€€€€€€€Ý¥¹‘½ÝÌ¹É•Í¥é” ÄÁÔ¤ì((€€€ÍÑèéÙ•Ñ½Èñ1½½Á¹…±åÍ¥ÍI•ÍÕ±Ðø…¹‘¥‘…Ñ•Ìì(€€€™½È€¡…ÕÑ¼Ý¥¹‘½Ü€èÝ¥¹‘½ÝÌ¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼ÍÁ…¸€ôÝ¥¹‘½Ü¹É•ÍÕ±Ð¹•¹‘M…µÁ±”€´Ý¥¹‘½Ü¹É•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”ì(€€€€€€€½¹ÍÐ…ÕÑ¼Õ…É€ô©Õ”èé©±¥µ¥Ð (€€€€€€€€€€€€ÌÈ°©Õ”èé©µ…à ÌÈ°ÍÁ…¸€¼€Ì¤°(€€€€€€€€€€€©Õ”èé©µ…à¡Ý¥¹‘½Ü¹É•ÍÕ±Ð¹É•Á…¥É=Ù•É±…ÁM…µÁ±•Ì€¬€ÌÈ°(€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÈÔ¤¤¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼™¥ÉÍÑÕÐ€ôÝ¥¹‘½Ü¹É•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”€¬Õ…Éì(€€€€€€€½¹ÍÐ…ÕÑ¼±…ÍÑÕÐ€ôÝ¥¹‘½Ü¹É•ÍÕ±Ð¹•¹‘M…µÁ±”€´Õ…Éì(€€€€€€€¥˜€¡™¥ÉÍÑÕÐ€øô±…ÍÑÕÐ¤(€€€€€€€€€€€½¹Ñ¥¹Õ”ì(€€€€€€€½¹ÍÐ…ÕÑ¼ÕÑMÑ•À€ô©Õ”èé©µ…à Ä°©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÀÄ¤¤ì(€€€€€€€…ÕÑ¼‰•ÍÑÕÐ€ô™¥ÉÍÑÕÐì(€€€€€€€…ÕÑ¼‰•ÍÑM…™•Ñä€ô€´Ä¸Á˜ì(€€€€€€€™½È€¡¥¹ÐÕÐ€ô™¥ÉÍÑÕÐìÕÐ€ðô±…ÍÑÕÐìÕÐ€¬ôÕÑMÑ•À¤(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼Í…™•Ñä€ô…±Õ±…Ñ•I½Ñ…Ñ¥½¹M…™•Ñä¡…Õ‘¥¼°Í…µÁ±•I…Ñ”°ÕÐ¤ì(€€€€€€€€€€€¥˜€¡Í…™•Ñä€ø‰•ÍÑM…™•Ñä¤(€€€€€€€€€€€ì(€€€€€€€€€€€€€€€‰•ÍÑM…™•Ñä€ôÍ…™•Ñäì(€€€€€€€€€€€€€€€‰•ÍÑÕÐ€ôÕÐì(€€€€€€€€€€€ô(€€€€€€€ô(€€€€€€€Ý¥¹‘½Ü¹É•ÍÕ±Ð¹É½Ñ…Ñ¥½¹M…µÁ±”€ô‰•ÍÑÕÐì(€€€€€€€Ý¥¹‘½Ü¹É•ÍÕ±Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€ô€À¸Øá˜€¨Ý¥¹‘½Ü¹É•ÍÕ±Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¬€À¸ÌÉ˜€¨©Õ”èé©µ…à À¸Á˜°‰•ÍÑM…™•Ñä¤ì(€€€€€€€…¹‘¥‘…Ñ•Ì¹ÁÕÍ¡}‰…¬¡Ý¥¹‘½Ü¹É•ÍÕ±Ð¤ì(€€€ô(€€€ÍÑèéÍ½ÉÐ¡…¹‘¥‘…Ñ•Ì¹‰•¥¸ ¤°…¹‘¥‘…Ñ•Ì¹•¹ ¤°mt€¡½¹ÍÐ…ÕÑ¼˜±•™Ð°½¹ÍÐ…ÕÑ¼˜É¥¡Ð¤(€€€ì(€€€€€€€É•ÑÕÉ¸±•™Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€øÉ¥¡Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌì(€€€ô¤ì(€€€½¹ÍÐ…ÕÑ¼‘¥ÍÑ¥¹Ñ¥ÍÑ…¹”€ô©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÈÀ¤ì(€€€™½È€¡½¹ÍÐ…ÕÑ¼˜…¹‘¥‘…Ñ”€è…¹‘¥‘…Ñ•Ì¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼‘ÕÁ±¥…Ñ”€ôÍÑèé…¹å}½˜ (€€€€€€€€€€€É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹‰•¥¸ ¤°É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹•¹ ¤°l™t€¡½¹ÍÐ…ÕÑ¼˜­•ÁÐ¤(€€€€€€€€€€€ì(€€€€€€€€€€€€€€€É•ÑÕÉ¸ÍÑèé…‰Ì¡­•ÁÐ¹ÍÑ…ÉÑM…µÁ±”€´…¹‘¥‘…Ñ”¹ÍÑ…ÉÑM…µÁ±”¤€ð‘¥ÍÑ¥¹Ñ¥ÍÑ…¹”(€€€€€€€€€€€€€€€€€€€€€€€˜˜ÍÑèé…‰Ì¡­•ÁÐ¹É½Ñ…Ñ¥½¹M…µÁ±”€´…¹‘¥‘…Ñ”¹É½Ñ…Ñ¥½¹M…µÁ±”¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ð‘¥ÍÑ¥¹Ñ¥ÍÑ…¹”ì(€€€€€€€€€€€ô¤ì(€€€€€€€¥˜€ …‘ÕÁ±¥…Ñ”¤(€€€€€€€€€€€É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹ÁÕÍ¡}‰…¬¡…¹‘¥‘…Ñ”¤ì(€€€€€€€¥˜€¡É•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹Í¥é” ¤(€€€€€€€€€€€€øôÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡©Õ”èé©µ…à Ä°µ…á¥µÕµ…¹‘¥‘…Ñ•Ì¤¤¤(€€€€€€€€€€€‰É•…¬ì(€€€ô(€€€É•ÑÕÉ¸É•Á½ÉÐì)ô()©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø1½½Á¹…±åé•ÈèéÉ•¹‘•ÉI½Ñ…Ñ•I•Á…¥È (€€€½¹ÍÐ©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø˜Í½ÕÉ”°(€€€½¹ÍÐ1½½Á¹…±åÍ¥ÍI•ÍÕ±Ð˜É•ÍÕ±Ð¤)ì(€€€¥˜€¡Í½ÕÉ”¹•Ñ9Õµ¡…¹¹•±Ì ¤€ôô€ÀñðÉ•ÍÕ±Ð¹É½Ñ…Ñ¥½¹M…µÁ±”€ð€À(€€€€€€€ñðÉ•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”€ð€ÀñðÉ•ÍÕ±Ð¹•¹‘M…µÁ±”€øÍ½ÕÉ”¹•Ñ9ÕµM…µÁ±•Ì ¤(€€€€€€€ñðÉ•ÍÕ±Ð¹•¹‘M…µÁ±”€´É•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”€ð€ÌÈ¤(€€€€€€€É•ÑÕÉ¸íôì((€€€½¹ÍÐ…ÕÑ¼É…¹•MÑ…ÉÐ€ôÉ•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”ì(€€€½¹ÍÐ…ÕÑ¼É…¹•M…µÁ±•Ì€ôÉ•ÍÕ±Ð¹•¹‘M…µÁ±”€´É…¹•MÑ…ÉÐì(€€€½¹ÍÐ…ÕÑ¼É½Ñ…Ñ¥½¸€ô©Õ”èé©±¥µ¥Ð (€€€€€€€É…¹•MÑ…ÉÐ€¬€Ä°É•ÍÕ±Ð¹•¹‘M…µÁ±”€´€Ä°É•ÍÕ±Ð¹É½Ñ…Ñ¥½¹M…µÁ±”¤ì(€€€½¹ÍÐ…ÕÑ¼É•±…Ñ¥Ù•I½Ñ…Ñ¥½¸€ôÉ½Ñ…Ñ¥½¸€´É…¹•MÑ…ÉÐì(€€€½¹ÍÐ…ÕÑ¼™…‘”€ô©Õ”èé©±¥µ¥Ð (€€€€€€€€À°©Õ”èé©µ¥¸¡É•±…Ñ¥Ù•I½Ñ…Ñ¥½¸°É…¹•M…µÁ±•Ì€´É•±…Ñ¥Ù•I½Ñ…Ñ¥½¸¤°(€€€€€€€É•ÍÕ±Ð¹É•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤ì(€€€½¹ÍÐ…ÕÑ¼É•¹‘•É•‘M…µÁ±•Ì€ôÉ…¹•M…µÁ±•Ì€´™…‘”ì(€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…ÐøÉ•¹‘•É•¡Í½ÕÉ”¹•Ñ9Õµ¡…¹¹•±Ì ¤°É•¹‘•É•‘M…µÁ±•Ì¤ì(€€€½¹ÍÐ…ÕÑ¼Ñ…¥±1•¹Ñ €ôÉ…¹•M…µÁ±•Ì€´É•±…Ñ¥Ù•I½Ñ…Ñ¥½¸ì(€€€½¹ÍÐ…ÕÑ¼ÁÉ•™¥á1•¹Ñ €ôÑ…¥±1•¹Ñ €´™…‘”ì(€€€½¹ÍÐ…ÕÑ¼ÍÕ™™¥á1•¹Ñ €ôÉ•±…Ñ¥Ù•I½Ñ…Ñ¥½¸€´™…‘”ì(€€€½¹ÍÐ…ÕÑ¼ÕÍ•1¥¹•…É…‘”€ôÉ•ÍÕ±Ð¹ÁÉ•™•É1¥¹•…ÉI•Á…¥É…‘”ì((€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ðÉ•¹‘•É•¹•Ñ9Õµ¡…¹¹•±Ì ¤ì€¬­¡…¹¹•°¤(€€€ì(€€€€€€€¥˜€¡ÁÉ•™¥á1•¹Ñ €ø€À¤(€€€€€€€€€€€É•¹‘•É•¹½ÁåÉ½´¡¡…¹¹•°°€À°Í½ÕÉ”°¡…¹¹•°°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É½Ñ…Ñ¥½¸°ÁÉ•™¥á1•¹Ñ ¤ì(€€€€€€€™½È€¡¥¹ÐÍ…µÁ±”€ô€ÀìÍ…µÁ±”€ð™…‘”ì€¬­Í…µÁ±”¤(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼Á½Í¥Ñ¥½¸€ôÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡Í…µÁ±”€¬€Ä¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¼ÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡™…‘”€¬€Ä¤ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼Ñ…¥±…¥¸€ôÕÍ•1¥¹•…É…‘”€ü€Ä¸Á˜€´Á½Í¥Ñ¥½¸(€€€€€€€€€€€€€€€€èÍÑèé½Ì¡Á½Í¥Ñ¥½¸€¨©Õ”èé5…Ñ¡½¹ÍÑ…¹ÑÌñ™±½…Ðøèé¡…±™A¤¤ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼¡•…‘…¥¸€ôÕÍ•1¥¹•…É…‘”€üÁ½Í¥Ñ¥½¸(€€€€€€€€€€€€€€€€èÍÑèéÍ¥¸¡Á½Í¥Ñ¥½¸€¨©Õ”èé5…Ñ¡½¹ÍÑ…¹ÑÌñ™±½…Ðøèé¡…±™A¤¤ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼Ñ…¥°€ôÍ½ÕÉ”¹•ÑM…µÁ±” (€€€€€€€€€€€€€€€¡…¹¹•°°É•ÍÕ±Ð¹•¹‘M…µÁ±”€´™…‘”€¬Í…µÁ±”¤ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼¡•…€ôÍ½ÕÉ”¹•ÑM…µÁ±” (€€€€€€€€€€€€€€€¡…¹¹•°°É…¹•MÑ…ÉÐ€¬Í…µÁ±”¤ì(€€€€€€€€€€€É•¹‘•É•¹Í•ÑM…µÁ±”¡¡…¹¹•°°ÁÉ•™¥á1•¹Ñ €¬Í…µÁ±”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€Ñ…¥±…¥¸€¨Ñ…¥°€¬¡•…‘…¥¸€¨¡•…¤ì(€€€€€€€ô(€€€€€€€¥˜€¡ÍÕ™™¥á1•¹Ñ €ø€À¤(€€€€€€€€€€€É•¹‘•É•¹½ÁåÉ½´¡¡…¹¹•°°ÁÉ•™¥á1•¹Ñ €¬™…‘”°Í½ÕÉ”°¡…¹¹•°°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É…¹•MÑ…ÉÐ€¬™…‘”°ÍÕ™™¥á1•¹Ñ ¤ì(€€€ô((€€€©Õ”èé¥¹½É•U¹ÕÍ•¡M¥¹…±¥…¹½ÍÑ¥ÌèéÉ•Á…¥É9½¹¥¹¥Ñ•¹‘I•µ½Ù•Œ¡É•¹‘•É•¤¤ì(€€€©Õ”èé¥¹½É•U¹ÕÍ•¡M¥¹…±¥…¹½ÍÑ¥Ìèé…ÁÁ±å¥ÉÕ±…ÉQÉÕ•A•…­•¥±¥¹œ¡É•¹‘•É•°€´Ä¸Á˜¤¤ì(€€€É•ÑÕÉ¸É•¹‘•É•ì)ô()1½½Á¹…±åÍ¥ÍI•ÍÕ±Ð1½½Á¹…±åé•Èèé™¥¹‘	•ÍÑ1½½À¡½¹ÍÐ©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø˜…Õ‘¥¼°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ‘½Õ‰±”Í…µÁ±•I…Ñ”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ¥¹ÐÉ•ÅÕ•ÍÑ•‘1½½ÁM…µÁ±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ¥¹ÐÍ•…É¡I…‘¥ÕÍM…µÁ±•Ì¤)ì(€€€1½½Á¹…±åÍ¥ÍI•ÍÕ±Ð™…±±‰…¬ì(€€€™…±±‰…¬¹•¹‘M…µÁ±”€ô©Õ”èé©µ¥¸¡…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤°É•ÅÕ•ÍÑ•‘1½½ÁM…µÁ±•Ì¤ì(€€€¥˜€¡…Õ‘¥¼¹•Ñ9Õµ¡…¹¹•±Ì ¤€ôô€Àñð…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤€ð€à¤(€€€€€€€É•ÑÕÉ¸™…±±‰…¬ì(€€€½¹ÍÐ…ÕÑ¼Á•É¥½€ôA•É¥½‘…¹‘¥‘…Ñ”ìÉ•ÅÕ•ÍÑ•‘1½½ÁM…µÁ±•Ì°€ÄÀÀ¸Á˜ôì(€€€½¹ÍÐ…ÕÑ¼É•ÍÕ±Ð€ôÍ•…É¡ÑA•É¥½¡…Õ‘¥¼°Í…µÁ±•I…Ñ”°Á•É¥½°Í•…É¡I…‘¥ÕÍM…µÁ±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€À°Í…µÁ±•IµÌ¡…Õ‘¥¼°€À°…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤¤¤ì(€€€É•ÑÕÉ¸É•ÍÕ±Ð¹…¹‘¥‘…Ñ•¥Ñ¹•ÍÌ€ð€À¸Á˜€ü™…±±‰…¬€èÉ•ÍÕ±Ðì)ô()1½½Á¹…±åÍ¥ÍI•ÍÕ±Ð1½½Á¹…±åé•Èèé•Ù…±Õ…Ñ•¥á•‘I…¹”¡½¹ÍÐ©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø˜…Õ‘¥¼°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ‘½Õ‰±”Í…µÁ±•I…Ñ”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ¥¹ÐÍÑ…ÉÑM…µÁ±”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ¥¹Ð•¹‘M…µÁ±”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ¥¹ÐÉ•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤)ì(€€€1½½Á¹…±åÍ¥ÍI•ÍÕ±Ð•µÁÑäì(€€€¥˜€¡…Õ‘¥¼¹•Ñ9Õµ¡…¹¹•±Ì ¤€ôô€ÀñðÍÑ…ÉÑM…µÁ±”€ð€Àñð•¹‘M…µÁ±”€ø…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤(€€€€€€€ñð•¹‘M…µÁ±”€´ÍÑ…ÉÑM…µÁ±”€ð€ÌÈ¤(€€€€€€€É•ÑÕÉ¸•µÁÑäì(€€€½¹ÍÐ…ÕÑ¼Ý¥¹‘½Ü€ô©Õ”èé©±¥µ¥Ð ÄØ°©Õ”èé©µ¥¸ ÔÄÈ°€¡•¹‘M…µÁ±”€´ÍÑ…ÉÑM…µÁ±”¤€¼€Ð¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÀÄÈ¤¤ì(€€€½¹ÍÐ…ÕÑ¼É•Á…¥È€ô©Õ”èé©±¥µ¥Ð À°€¡•¹‘M…µÁ±”€´ÍÑ…ÉÑM…µÁ±”¤€¼€Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤ì(€€€É•ÑÕÉ¸•Ù…±Õ…Ñ•…¹‘¥‘…Ñ”¡…Õ‘¥¼°ÍÑ…ÉÑM…µÁ±”°•¹‘M…µÁ±”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€•¹‘M…µÁ±”€´ÍÑ…ÉÑM…µÁ±”€´É•Á…¥È°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€Ý¥¹‘½Ü°ÑÉÕ”°€ÄÀÀ¸Á˜°É•Á…¥È°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€Í…µÁ±•IµÌ¡…Õ‘¥¼°€À°…Õ‘¥¼¹•Ñ9ÕµM…µÁ±•Ì ¤¤°Í…µÁ±•I…Ñ”¤ì)ô(