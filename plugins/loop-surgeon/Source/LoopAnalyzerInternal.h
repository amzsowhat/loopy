#pragma once

#include "LoopAnalyzer.h"

#include <vector>

namespace loopAnalyzerInternal
{
struct PeriodCandidate
{
    int samples = 0;
    float periodicitySimilarity = 0.0f;
};

double sampleRms(const juce::AudioBuffer<float>& audio, int start, int length);

float calculateRotationSafety(const juce::AudioBuffer<float>& audio,
                              double sampleRate,
                              int cut);

std::vector<PeriodCandidate> findPeriods(const juce::AudioBuffer<float>& audio,
                                         double sampleRate,
                                         int minimumSamples,
                                         int maximumSamples);

LoopAnalysisResult searchAtPeriod(const juce::AudioBuffer<float>& audio,
                                  double sampleRate,
                                  PeriodCandidate period,
                                  int refinementRadius,
                                  int maximumRepairOverlapSamples,
                                  double sourceRms);

LoopAnalysisResult evaluateCandidate(const juce::AudioBuffer<float>& audio,
                                     int start,
                                     int end,
                                     int requestedVisibleSamples,
                                     int window,
                                     bool detailed,
                                     float periodicitySimilarity,
                                     int repairOverlapSamples,
                                     double sourceRms,
                                     double sampleRate);
}
