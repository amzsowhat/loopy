#include "RenderQuality.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
float cubicInterpolate(const float previous, const float current,
                       const float next, const float following,
                       const float position)
{
    const auto a = -0.5f * previous + 1.5f * current
                   - 1.5f * next + 0.5f * following;
    const auto b = previous - 2.5f * current + 2.0f * next - 0.5f * following;
    const auto c = -0.5f * previous + 0.5f * next;
    return ((a * position + b) * position + c) * position + current;
}

std::array<float, RenderQuality::spectrumPointCount> analyseSpectrum(
    const juce::AudioBuffer<float>& audio, const double sampleRate)
{
    std::array<float, RenderQuality::spectrumPointCount> result {};
    if (audio.getNumChannels() == 0 || audio.getNumSamples() < 64 || sampleRate <= 0.0)
        return result;

    constexpr auto windowSize = 512;
    constexpr auto frameCount = 8;
    const auto usableWindow = juce::jmin(windowSize, audio.getNumSamples());
    const auto maximumStart = juce::jmax(0, audio.getNumSamples() - usableWindow);
    const auto minimumFrequency = 24.0;
    const auto maximumFrequency = juce::jmax(
        minimumFrequency, juce::jmin(20000.0, sampleRate * 0.48));
    auto maximumDb = -160.0f;
    for (size_t band = 0; band < result.size(); ++band)
    {
        const auto proportion = static_cast<double>(band)
                                / static_cast<double>(result.size() - 1u);
        const auto frequency = minimumFrequency
            * std::pow(maximumFrequency / minimumFrequency, proportion);
        auto energy = 0.0;
        for (int frame = 0; frame < frameCount; ++frame)
        {
            const auto start = frame * maximumStart / juce::jmax(1, frameCount - 1);
            auto real = 0.0;
            auto imaginary = 0.0;
            for (int sample = 0; sample < usableWindow; ++sample)
            {
                auto mono = 0.0;
                for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                    mono += audio.getSample(channel, start + sample);
                mono /= static_cast<double>(audio.getNumChannels());
                const auto window = 0.5 - 0.5 * std::cos(
                    juce::MathConstants<double>::twoPi * sample
                    / static_cast<double>(juce::jmax(1, usableWindow - 1)));
                const auto phase = juce::MathConstants<double>::twoPi * frequency
                                   * sample / sampleRate;
                real += mono * window * std::cos(phase);
                imaginary -= mono * window * std::sin(phase);
            }
            energy += real * real + imaginary * imaginary;
        }
        const auto db = juce::Decibels::gainToDecibels(static_cast<float>(
            std::sqrt(energy / frameCount) / juce::jmax(1, usableWindow)), -160.0f);
        result[band] = db;
        maximumDb = juce::jmax(maximumDb, db);
    }
    for (auto& value : result)
        value = juce::jlimit(0.0f, 1.0f, (value - maximumDb + 60.0f) / 60.0f);
    return result;
}

struct BiquadCoefficients
{
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadState
{
    double z1 = 0.0;
    double z2 = 0.0;

    double process(const double input, const BiquadCoefficients& coefficients)
    {
        const auto output = coefficients.b0 * input + z1;
        z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
        z2 = coefficients.b2 * input - coefficients.a2 * output;
        return output;
    }
};

BiquadCoefficients makeKWeightShelf(const double sampleRate)
{
    constexpr auto gainDb = 3.999843853973347;
    constexpr auto q = 0.7071752369554196;
    constexpr auto exponent = 0.4996667741545416;
    const auto frequency = juce::jmin(1681.974450955533, sampleRate * 0.45);
    const auto k = std::tan(juce::MathConstants<double>::pi * frequency / sampleRate);
    const auto vh = std::pow(10.0, gainDb / 20.0);
    const auto vb = std::pow(vh, exponent);
    const auto denominator = 1.0 + k / q + k * k;
    return {
        (vh + vb * k / q + k * k) / denominator,
        2.0 * (k * k - vh) / denominator,
        (vh - vb * k / q + k * k) / denominator,
        2.0 * (k * k - 1.0) / denominator,
        (1.0 - k / q + k * k) / denominator
    };
}

BiquadCoefficients makeKWeightHighPass(const double sampleRate)
{
    constexpr auto frequency = 38.13547087602444;
    constexpr auto q = 0.5003270373238773;
    const auto k = std::tan(juce::MathConstants<double>::pi
                            * juce::jmin(frequency, sampleRate * 0.45) / sampleRate);
    const auto denominator = 1.0 + k / q + k * k;
    return {
        1.0 / denominator,
        -2.0 / denominator,
        1.0 / denominator,
        2.0 * (k * k - 1.0) / denominator,
        (1.0 - k / q + k * k) / denominator
    };
}
}

namespace RenderQuality
{
bool repairNonFiniteAndRemoveDc(juce::AudioBuffer<float>& audio)
{
    auto allFinite = true;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        auto mean = 0.0;
        auto finiteSamples = 0;
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto value = audio.getSample(channel, sample);
            if (std::isfinite(value))
            {
                mean += value;
                ++finiteSamples;
            }
            else
            {
                audio.setSample(channel, sample, 0.0f);
                allFinite = false;
            }
        }
        mean /= static_cast<double>(juce::jmax(1, finiteSamples));
        const auto dc = static_cast<float>(mean);
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample(channel, sample, audio.getSample(channel, sample) - dc);
    }
    return allFinite;
}

float estimateCircularTruePeak(const juce::AudioBuffer<float>& audio)
{
    const auto samples = audio.getNumSamples();
    if (samples < 1)
        return 0.0f;
    const auto wrap = [samples] (int sample)
    {
        sample %= samples;
        return sample < 0 ? sample + samples : sample;
    };

    auto peak = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto previous = audio.getSample(channel, wrap(sample - 1));
            const auto current = audio.getSample(channel, sample);
            const auto next = audio.getSample(channel, wrap(sample + 1));
            const auto following = audio.getSample(channel, wrap(sample + 2));
            peak = juce::jmax(peak, std::abs(current));
            for (int phase = 1; phase < 4; ++phase)
                peak = juce::jmax(peak, std::abs(cubicInterpolate(
                    previous, current, next, following,
                    static_cast<float>(phase) * 0.25f)));
        }
    return peak;
}

float applyCircularTruePeakCeiling(juce::AudioBuffer<float>& audio,
                                   const float ceilingDbtp)
{
    const auto ceiling = juce::Decibels::decibelsToGain(ceilingDbtp);
    auto peak = estimateCircularTruePeak(audio);
    if (peak > ceiling)
    {
        audio.applyGain(ceiling / peak);
        peak = estimateCircularTruePeak(audio);
    }
    return juce::Decibels::gainToDecibels(juce::jmax(1.0e-9f, peak));
}

float calculateRms(const juce::AudioBuffer<float>& audio)
{
    auto energy = 0.0;
    auto count = 0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto value = audio.getSample(channel, sample);
            energy += static_cast<double>(value) * value;
            ++count;
        }
    return std::sqrt(static_cast<float>(energy / static_cast<double>(juce::jmax(1, count))));
}

float estimateIntegratedLoudnessDb(const juce::AudioBuffer<float>& audio,
                                   const double sampleRate)
{
    if (audio.getNumChannels() == 0 || audio.getNumSamples() == 0 || sampleRate <= 0.0)
        return -100.0f;
    if (sampleRate < 8000.0)
        return -0.691f + 20.0f * std::log10(juce::jmax(1.0e-9f, calculateRms(audio)));

    juce::AudioBuffer<float> weighted(audio.getNumChannels(), audio.getNumSamples());
    const auto shelf = makeKWeightShelf(sampleRate);
    const auto highPass = makeKWeightHighPass(sampleRate);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        BiquadState shelfState;
        BiquadState highPassState;
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto filtered = highPassState.process(
                shelfState.process(audio.getSample(channel, sample), shelf), highPass);
            weighted.setSample(channel, sample, static_cast<float>(filtered));
        }
    }

    const auto blockSamples = juce::jlimit(
        1, audio.getNumSamples(), juce::roundToInt(sampleRate * 0.4));
    const auto hopSamples = juce::jmax(1, juce::roundToInt(sampleRate * 0.1));
    std::vector<double> blockEnergies;
    for (int start = 0; start < weighted.getNumSamples(); start += hopSamples)
    {
        const auto count = juce::jmin(blockSamples, weighted.getNumSamples() - start);
        if (count < juce::jmin(blockSamples, juce::roundToInt(sampleRate * 0.1)))
            break;
        auto energy = 0.0;
        for (int channel = 0; channel < weighted.getNumChannels(); ++channel)
            for (int sample = 0; sample < count; ++sample)
            {
                const auto value = static_cast<double>(weighted.getSample(channel, start + sample));
                energy += value * value;
            }
        blockEnergies.push_back(energy / static_cast<double>(count));
    }
    if (blockEnergies.empty())
        return -100.0f;

    constexpr auto offset = -0.691;
    const auto absoluteGateEnergy = std::pow(10.0, (-70.0 - offset) / 10.0);
    auto absoluteTotal = 0.0;
    auto absoluteCount = 0;
    for (const auto energy : blockEnergies)
        if (energy >= absoluteGateEnergy)
        {
            absoluteTotal += energy;
            ++absoluteCount;
        }
    if (absoluteCount == 0)
        return -100.0f;
    const auto absoluteMean = absoluteTotal / absoluteCount;
    const auto relativeGateEnergy = absoluteMean * 0.1;
    auto gatedTotal = 0.0;
    auto gatedCount = 0;
    for (const auto energy : blockEnergies)
        if (energy >= absoluteGateEnergy && energy >= relativeGateEnergy)
        {
            gatedTotal += energy;
            ++gatedCount;
        }
    if (gatedCount == 0)
        return -100.0f;
    return static_cast<float>(offset + 10.0 * std::log10(
        gatedTotal / static_cast<double>(gatedCount)));
}

float calculateStereoCorrelation(const juce::AudioBuffer<float>& audio)
{
    if (audio.getNumChannels() < 2 || audio.getNumSamples() == 0)
        return 1.0f;
    auto dot = 0.0;
    auto leftEnergy = 0.0;
    auto rightEnergy = 0.0;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto left = static_cast<double>(audio.getSample(0, sample));
        const auto right = static_cast<double>(audio.getSample(1, sample));
        dot += left * right;
        leftEnergy += left * left;
        rightEnergy += right * right;
    }
    const auto denominator = std::sqrt(leftEnergy * rightEnergy);
    return denominator > 1.0e-12
        ? static_cast<float>(juce::jlimit(-1.0, 1.0, dot / denominator)) : 1.0f;
}

float calculateStereoLevelImbalanceDb(const juce::AudioBuffer<float>& audio)
{
    if (audio.getNumChannels() < 2 || audio.getNumSamples() == 0)
        return 0.0f;
    const auto left = audio.getRMSLevel(0, 0, audio.getNumSamples());
    const auto right = audio.getRMSLevel(1, 0, audio.getNumSamples());
    return juce::Decibels::gainToDecibels(juce::jmax(1.0e-9f, left))
           - juce::Decibels::gainToDecibels(juce::jmax(1.0e-9f, right));
}

SignalSnapshot analyseSourceAndOutput(const juce::AudioBuffer<float>& source,
                                      const juce::AudioBuffer<float>& output,
                                      const double sampleRate)
{
    SignalSnapshot result;
    if (source.getNumSamples() == 0 || output.getNumSamples() == 0)
        return result;
    result.sourceSpectrum = analyseSpectrum(source, sampleRate);
    result.outputSpectrum = analyseSpectrum(output, sampleRate);
    result.sourceCorrelation = calculateStereoCorrelation(source);
    result.outputCorrelation = calculateStereoCorrelation(output);
    result.sourceImbalanceDb = calculateStereoLevelImbalanceDb(source);
    result.outputImbalanceDb = calculateStereoLevelImbalanceDb(output);

    auto peak = 1.0e-6f;
    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        for (int sample = 0; sample < output.getNumSamples(); ++sample)
            peak = juce::jmax(peak, std::abs(output.getSample(channel, sample)));
    for (size_t point = 0; point < result.phasePoints.size(); ++point)
    {
        const auto sample = static_cast<int>(point)
                            * juce::jmax(0, output.getNumSamples() - 1)
                            / static_cast<int>(result.phasePoints.size() - 1u);
        const auto left = output.getSample(0, sample) / peak;
        const auto right = output.getNumChannels() >= 2
            ? output.getSample(1, sample) / peak : left;
        result.phasePoints[point] = { left, right };
    }
    result.valid = true;
    return result;
}
}
