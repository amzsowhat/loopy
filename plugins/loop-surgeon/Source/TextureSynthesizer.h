#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "TextureCharacter.h"

#include <cstdint>
#include <vector>

enum class TextureStructure
{
    // Stable stored values. They select source-agnostic traversal scales; no value selects
    // a named material, oscillator, spectral-delay or reversed-playback path.
    automatic = 0,
    continuous = 1,
    particles = 2
};

struct TextureSynthesisSettings
{
    float durationSeconds = 24.0f;
    float variation = 0.72f;
    // 0 keeps the source's macro movement, 1 removes more of its long envelope.
    float flatten = 0.72f;
    // Post-construction local-envelope flattening. Zero is an exact bypass so existing
    // sessions and the approved 0.10 sound remain unchanged.
    float dynamicsCrush = 0.0f;
    // Transformation depth. The legacy field name remains stable for DAW state compatibility.
    float sourceMatch = 0.85f;
    TextureStructure structure = TextureStructure::automatic;
    TextureCharacter character = TextureCharacter::off;
    float characterAmount = 0.5f;
    uint32_t seed = 0x4c535501u;
};

struct TextureSynthesisResult
{
    juce::AudioBuffer<float> audio;
    std::vector<int> analysisFrameStarts;
    float truePeakDbtp = -100.0f;
    TextureStructure usedStructure = TextureStructure::continuous;
    TextureCharacter usedCharacter = TextureCharacter::off;
    bool containsOnlyFiniteSamples = false;
};

class TextureSynthesizer
{
public:
    // Offline-only, phase-coherent time-domain texture construction. It uses the selected
    // source's own waveform, never synthetic noise, oscillator banks, pitch shifting or reverse.
    // This function allocates and must never run on the audio thread.
    static TextureSynthesisResult synthesize(const juce::AudioBuffer<float>& source,
                                             double sampleRate,
                                             TextureSynthesisSettings settings);
};
