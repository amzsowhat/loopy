#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

struct LoopAnalysisResult
{
    int startSample = 0;
    int endSample = 0;
    float overall = 0.0f;
    float level = 0.0f;
    float slope = 0.0f;
    float spectrum = 0.0f;
    float phase = 0.0f;
    float stereo = 0.0f;
};

class LoopAnalyzer
{
public:
    [[nodiscard]] static LoopAnalysisResult findBestLoop(
        const juce::AudioBuffer<float>& capturedAudio,
        double sampleRate,
        int requestedLoopSamples,
        int searchRadiusSamples);
};
