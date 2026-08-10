#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

struct LoopAnalysisResult
{
    int startSample = 0;
    int endSample = 0;
    float overall = 0.0f;
    float waveform = 0.0f;
    float level = 0.0f;
    float slope = 0.0f;
    float spectrum = 0.0f;
    float phase = 0.0f;
    float stereo = 0.0f;
    float transient = 0.0f;
    float periodicity = 0.0f;
    float repair = 0.0f;
    int repairOverlapSamples = 0;
    // >= 0 marks full-selection Rotate & Repair. start/end remain the source range.
    int rotationSample = -1;
};

struct LoopAnalysisReport
{
    std::vector<LoopAnalysisResult> candidates;
    bool lowConfidence = false;
};

class LoopAnalyzer
{
public:
    // Searches a whole source automatically. Period candidates are estimated first,
    // then start/end pairs are refined using time, spectral and stereo seam evidence.
    [[nodiscard]] static LoopAnalysisReport analyzeSource(
        const juce::AudioBuffer<float>& sourceAudio,
        double sampleRate,
        int minimumLoopSamples,
        int maximumLoopSamples,
        int maximumCandidates = 3,
        int repairOverlapSamples = 0);

    // Preserves the selected source as a long-form loop. The original end/start seam is moved
    // inside the result and crossfaded; the final loop boundary uses adjacent source samples.
    [[nodiscard]] static LoopAnalysisReport analyzeRotateRepair(
        const juce::AudioBuffer<float>& sourceAudio,
        double sampleRate,
        int maximumCandidates = 3,
        int maximumRepairOverlapSamples = 0);

    // Searches inside the user selection for a contiguous source span whose repaired render is
    // exactly targetOutputSamples long. No repetition or time stretching is introduced.
    [[nodiscard]] static LoopAnalysisReport analyzeRotateRepairExact(
        const juce::AudioBuffer<float>& sourceAudio,
        double sampleRate,
        int targetOutputSamples,
        int maximumCandidates = 3,
        int maximumRepairOverlapSamples = 0);

    [[nodiscard]] static juce::AudioBuffer<float> renderRotateRepair(
        const juce::AudioBuffer<float>& sourceAudio,
        const LoopAnalysisResult& result);

    // Kept for host-range capture where the requested duration is already known.
    [[nodiscard]] static LoopAnalysisResult findBestLoop(
        const juce::AudioBuffer<float>& capturedAudio,
        double sampleRate,
        int requestedLoopSamples,
        int searchRadiusSamples);

    [[nodiscard]] static LoopAnalysisResult evaluateFixedRange(
        const juce::AudioBuffer<float>& sourceAudio,
        double sampleRate,
        int startSample,
        int endSample,
        int repairOverlapSamples = 0);
};
