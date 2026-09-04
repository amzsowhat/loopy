#include "TextureCharacter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
float bufferRms(const juce::AudioBuffer<float>& audio)
{
    if (audio.getNumChannels() <= 0 || audio.getNumSamples() <= 0)
        return 0.0f;
    double energy = 0.0;
    auto count = 0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto value = static_cast<double>(audio.getSample(channel, sample));
            energy += value * value;
            ++count;
        }
    return static_cast<float>(std::sqrt(
        energy / static_cast<double>(juce::jmax(1, count))));
}

void matchRms(const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet)
{
    const auto dryRms = bufferRms(dry);
    const auto wetRms = bufferRms(wet);
    if (dryRms > 1.0e-7f && wetRms > 1.0e-7f)
        wet.applyGain(juce::jlimit(0.42f, 2.4f, dryRms / wetRms));
}

void blend(juce::AudioBuffer<float>& audio,
           const juce::AudioBuffer<float>& wet,
           const float amount)
{
    const auto mix = juce::jlimit(0.0f, 1.0f, amount);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto dryValue = audio.getSample(channel, sample);
            const auto wetValue = wet.getSample(channel, sample);
            audio.setSample(channel, sample, dryValue + mix * (wetValue - dryValue));
        }
}

juce::AudioBuffer<float> makePatina(const juce::AudioBuffer<float>& dry,
                                    const double sampleRate)
{
    juce::AudioBuffer<float> wet(dry);
    const auto coefficient = static_cast<float>(
        std::exp(-1.0 / juce::jmax(1.0, sampleRate * 0.018)));
    constexpr auto drive = 2.35f;
    const auto normalizer = 1.0f / std::tanh(drive);
    for (int channel = 0; channel < wet.getNumChannels(); ++channel)
    {
        auto memory = 0.0f;
        for (int pass = 0; pass < 4; ++pass)
            for (int sample = 0; sample < wet.getNumSamples(); ++sample)
            {
                const auto input = dry.getSample(channel, sample);
                memory = coefficient * memory + (1.0f - coefficient) * input;
                if (pass == 3)
                {
                    const auto memorySkew = input + 0.38f * memory
                                            + 0.12f * (input - memory);
                    wet.setSample(channel, sample,
                        std::tanh(drive * memorySkew) * normalizer);
                }
            }
    }
    matchRms(dry, wet);
    return wet;
}

uint32_t nextRandom(uint32_t& state) noexcept
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

juce::AudioBuffer<float> makeBloom(const juce::AudioBuffer<float>& dry,
                                   const double sampleRate,
                                   uint32_t seed)
{
    juce::AudioBuffer<float> wet(dry.getNumChannels(), dry.getNumSamples());
    const auto samples = dry.getNumSamples();
    if (samples <= 0)
        return wet;

    std::array<int, 6> delays {};
    constexpr std::array<float, 6> seconds { 0.0f, 0.013f, 0.027f,
                                             0.043f, 0.067f, 0.097f };
    auto randomState = seed == 0 ? 1u : seed;
    for (size_t tap = 0; tap < delays.size(); ++tap)
    {
        const auto jitter = tap == 0 ? 0
            : static_cast<int>(nextRandom(randomState) % 19u) - 9;
        delays[tap] = juce::jlimit(0, samples - 1,
            juce::roundToInt(sampleRate * seconds[tap]) + jitter);
    }
    constexpr std::array<float, 6> weights { 0.38f, 0.20f, 0.16f,
                                             0.11f, 0.09f, 0.06f };
    for (int channel = 0; channel < wet.getNumChannels(); ++channel)
        for (int sample = 0; sample < samples; ++sample)
        {
            auto value = 0.0f;
            for (size_t tap = 0; tap < delays.size(); ++tap)
            {
                auto index = sample - delays[tap];
                if ((tap & 1u) != 0u)
                    index = sample + delays[tap];
                index %= samples;
                if (index < 0)
                    index += samples;
                value += weights[tap] * dry.getSample(channel, index);
            }
            wet.setSample(channel, sample, value);
        }
    matchRms(dry, wet);
    return wet;
}

juce::AudioBuffer<float> makeFray(const juce::AudioBuffer<float>& dry,
                                  const double sampleRate)
{
    juce::AudioBuffer<float> wet(dry.getNumChannels(), dry.getNumSamples());
    const auto samples = dry.getNumSamples();
    const auto radius = juce::jlimit(2, juce::jmax(2, samples / 8),
        juce::roundToInt(sampleRate * 0.0025));
    for (int channel = 0; channel < wet.getNumChannels(); ++channel)
        for (int sample = 0; sample < samples; ++sample)
        {
            auto smooth = 0.0f;
            constexpr std::array<int, 5> offsets { -2, -1, 0, 1, 2 };
            constexpr std::array<float, 5> weights { 0.10f, 0.22f, 0.36f, 0.22f, 0.10f };
            for (size_t point = 0; point < offsets.size(); ++point)
            {
                auto index = sample + offsets[point] * radius;
                index %= samples;
                if (index < 0)
                    index += samples;
                smooth += weights[point] * dry.getSample(channel, index);
            }
            const auto detail = dry.getSample(channel, sample) - smooth;
            wet.setSample(channel, sample,
                0.42f * smooth + 0.58f * std::tanh(3.4f * detail));
        }
    matchRms(dry, wet);
    return wet;
}
}

void TextureCharacterProcessor::apply(juce::AudioBuffer<float>& audio,
                                      const double sampleRate,
                                      const TextureCharacter character,
                                      const float amount,
                                      const uint32_t seed)
{
    const auto mix = juce::jlimit(0.0f, 1.0f, amount);
    if (character == TextureCharacter::off || mix <= 0.0f
        || audio.getNumChannels() <= 0 || audio.getNumSamples() <= 0)
        return;

    juce::AudioBuffer<float> wet;
    switch (character)
    {
        case TextureCharacter::off:
            return;
        case TextureCharacter::patina:
            wet = makePatina(audio, sampleRate);
            break;
        case TextureCharacter::bloom:
            wet = makeBloom(audio, sampleRate, seed);
            break;
        case TextureCharacter::fray:
            wet = makeFray(audio, sampleRate);
            break;
    }
    blend(audio, wet, mix);
}
