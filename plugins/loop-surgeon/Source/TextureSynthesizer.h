#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <vector>

struct TextureSynthesisSettings
{
    float durationSeconds = 24.0f;
    float variation = 0.72f;
    uint32_t seed = 0x4c535501u;
};

struct TextureSynthesisResult
{
    juce::AudioBuffer<float> audio;
    std::vector<int> sourceGrainStarts;
    float transitionQuality = 0.0f;
    float closureQuality = 0.0f;
    float spectrumPreservation = 0.0f;
    float stereoPreservation = 0.0f;
    float diversity = 0.0f;
    float transientPreservation = 0.0f;
};

class TextureSynthesizer
{
public:
    // Offline-only synthesis. This function allocates and must never run on the audio thread.
    static TextureSynthesisResult synthesize(const juce::AudioBuffer<float>& source,
                                             double sampleRate,
                                             TextureSynthesisSettings settings);
};
