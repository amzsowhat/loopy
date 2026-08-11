#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace SignalDiagnostics
{
constexpr size_t spectrumPointCount = 48;
constexpr size_t phasePointCount = 96;

struct SignalSnapshot
{
    std::array<float, spectrumPointCount> sourceSpectrum {};
    std::array<float, spectrumPointCount> outputSpectrum {};
    std::array<std::array<float, 2>, phasePointCount> phasePoints {};
    float sourceCorrelation = 1.0f;
    float outputCorrelation = 1.0f;
    float sourceImbalanceDb = 0.0f;
    float outputImbalanceDb = 0.0f;
    bool valid = false;
};

// Offline render utilities. These functions may scan or modify complete buffers and must never
// be called from the real-time audio thread.
[[nodiscard]] bool repairNonFiniteAndRemoveDc(juce::AudioBuffer<float>& audio);
[[nodiscard]] float estimateCircularTruePeak(const juce::AudioBuffer<float>& audio);
[[nodiscard]] float applyCircularTruePeakCeiling(juce::AudioBuffer<float>& audio,
                                                 float ceilingDbtp = -1.0f);
[[nodiscard]] float calculateRms(const juce::AudioBuffer<float>& audio);
[[nodiscard]] float estimateIntegratedLoudnessDb(
    const juce::AudioBuffer<float>& audio, double sampleRate);
[[nodiscard]] float calculateStereoCorrelation(const juce::AudioBuffer<float>& audio);
[[nodiscard]] float calculateStereoLevelImbalanceDb(const juce::AudioBuffer<float>& audio);
[[nodiscard]] SignalSnapshot analyseSourceAndOutput(
    const juce::AudioBuffer<float>& source,
    const juce::AudioBuffer<float>& output,
    double sampleRate);
}

