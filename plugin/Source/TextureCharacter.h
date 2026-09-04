#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

enum class TextureCharacter
{
    off = 0,
    patina = 1,
    bloom = 2,
    fray = 3
};

class TextureCharacterProcessor
{
public:
    // Offline-only optional colour stage. Off or zero amount is an exact bypass.
    static void apply(juce::AudioBuffer<float>& audio,
                      double sampleRate,
                      TextureCharacter character,
                      float amount,
                      uint32_t seed);
};
