#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

struct LoopAnalysisResult
{
    int startSample = 0;
    int endSample = 0;
    // Internal search value only. It orders candidate joins and is never presented as
    // a product-quality rating or used to block export.
    float candidateFitness = 0.0f;
    bool preferLinearRepairFade = false;
    int repairOverlapSamples = 0;
    // The repaired source cycle may be repeated to reach a longer exact output.
    int targetOutputSamples = 0;
    int repetitionCount = 1;
    // >= 0 marks LOOP mode. start/end remain the source range.
    int rotationSample = -1;
};

struct LoopAnalysisReport
{
    std::vector<LoopAnalysisResult> candidates;
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

    // For shorter/equal output, searches the selection for an exact repaired cycle.
    // For longer output, derives a source-sized repaired cycle and repeats it an integer
    // number of times so the final boundary lands on the same phase.
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
