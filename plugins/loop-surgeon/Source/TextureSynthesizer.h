#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <vector>

enum class TextureStructure
{
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
    // Transformation depth. The legacy field name remains stable for DAW state compatibility.
    float sourceMatch = 0.85f;
    TextureStructure structure = TextureStructure::automatic;
    uint32_t seed = 0x4c535501u;
};

struct TextureSynthesisResult
{
    juce::AudioBuffer<float> audio;
    std::vector<int> analysisFrameStarts;
    float transitionQuality = 0.0f;
    float closureQuality = 0.0f;
    float spectrumPreservation = 0.0f;
    float stereoPreservation = 0.0f;
    float loudnessPreservation = 0.0f;
    float phasePreservation = 0.0f;
    float positionPreservation = 0.0f;
    float diversity = 0.0f;
    float transientPreservation = 0.0f;
    float macroStability = 0.0f;
    float repeatSafety = 0.0f;
    float materialIdentity = 0.0f;
    float localFrameIdentity = 0.0f;
    float noiseCollapseSafety = 0.0f;
    float sourceSpectralFlatness = 0.0f;
    float outputSpectralFlatness = 0.0f;
    float structureConfidence = 0.0f;
    float truePeakDbtp = -100.0f;
    float qualityScore = 0.0f;
    TextureStructure usedStructure = TextureStructure::continuous;
    bool containsOnlyFiniteSamples = false;
    bool passedQualityGate = false;
};

class TextureSynthesizer
{
public:
    // Offline-only synthesis. This function allocates and must never run on the audio thread.
    static TextureSynthesisResult synthesize(const juce::AudioBuffer<float>& source,
                                             double sampleRate,
                                             TextureSynthesisSettings settings);
};
