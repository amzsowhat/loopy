#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <vector>

enum class TextureStructure
{
    // Stable stored values. The names pre-date the style redesign; they now select three
    // source-agnostic resynthesis geometries and never select a sample-playback path.
    automatic = 0,
    continuous = 1,
    particles = 2
};

struct TextureSynthesisSettings
{
    float durationSeconds = 24.0f;
    float variation = 0.72f;
    // 0 keeps the source's measured amount of movement, 1 removes macro envelope movement.
    // The source timeline itself is never replayed or stretched.
    float flatten = 0.72f;
    // Style depth. The legacy field name remains stable for DAW state compatibility.
    float sourceMatch = 0.85f;
    TextureStructure structure = TextureStructure::automatic;
    uint32_t seed = 0x4c535501u;
};

struct TextureSynthesisResult
{
    juce::AudioBuffer<float> audio;
    std::vector<int> analysisFrameStarts;
    float truePeakDbtp = -100.0f;
    TextureStructure usedStructure = TextureStructure::continuous;
    bool containsOnlyFiniteSamples = false;
};

class TextureSynthesizer
{
public:
    // Offline-only synthesis. This function allocates and must never run on the audio thread.
    static TextureSynthesisResult synthesize(const juce::AudioBuffer<float>& source,
                                             double sampleRate,
                                             TextureSynthesisSettings settings);
};

