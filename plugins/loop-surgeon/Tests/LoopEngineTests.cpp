#include "LoopAnalyzer.h"
#include "LoopEngine.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace
{
bool expect(const bool condition, const char* message)
{
    if (condition)
        return true;

    std::cerr << "FAILED: " << message << '\n';
    return false;
}

bool waitForReady(LoopEngine& engine)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        if (engine.getState() == LoopEngine::State::ready)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}
}

int main()
{
    bool passed = true;

    juce::AudioBuffer<float> realisticAnalysis(2, 62400);
    for (int sample = 0; sample < realisticAnalysis.getNumSamples(); ++sample)
    {
        const auto phase = juce::MathConstants<float>::twoPi
                           * static_cast<float>(sample % 12000) / 12000.0f;
        realisticAnalysis.setSample(0, sample, 0.7f * std::sin(phase));
        realisticAnalysis.setSample(1, sample, 0.6f * std::sin(phase + 0.12f));
    }
    const auto analysisStart = std::chrono::steady_clock::now();
    const auto realisticResult = LoopAnalyzer::findBestLoop(realisticAnalysis, 48000.0, 48000, 7200);
    const auto analysisDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - analysisStart);
    passed &= expect(realisticResult.endSample > realisticResult.startSample,
                     "48 kHz analysis should select valid boundaries");
    passed &= expect(realisticResult.spectrum >= 0.0f && realisticResult.spectrum <= 100.0f,
                     "48 kHz spectral analysis should be normalized");
    passed &= expect(analysisDuration < std::chrono::seconds(5),
                     "one-second background analysis should remain bounded");

    constexpr auto testSampleRate = 1000.0;
    LoopEngine engine;
    engine.prepare(testSampleRate, 64, 2);
    engine.setLoopLengthSeconds(0.1f);
    engine.setCrossfadeMilliseconds(5.0f);
    engine.beginCapture();

    juce::AudioBuffer<float> capture(2, 400);
    for (int sample = 0; sample < capture.getNumSamples(); ++sample)
    {
        const auto phase = juce::MathConstants<float>::twoPi
                           * static_cast<float>(sample % 100) / 100.0f;
        capture.setSample(0, sample, std::sin(phase));
        capture.setSample(1, sample, 0.8f * std::sin(phase + 0.08f));
    }

    engine.process(capture, 1.0f);
    passed &= expect(engine.getState() == LoopEngine::State::analysing
                         || engine.getState() == LoopEngine::State::ready,
                     "capture should transition to background analysis");
    passed &= expect(waitForReady(engine), "analysis should publish a loop");
    passed &= expect(engine.getCapturedSampleCount() > 0,
                     "analysis should select a non-empty loop");
    passed &= expect(engine.getCaptureProgress() == 1.0f,
                     "capture progress should complete");
    passed &= expect(engine.getSeamQuality() >= 0.0f && engine.getSeamQuality() <= 100.0f,
                     "overall seam quality should be normalized");
    passed &= expect(engine.getSpectrumScore() >= 0.0f && engine.getSpectrumScore() <= 100.0f,
                     "spectrum score should be normalized");
    passed &= expect(engine.getPhaseScore() >= 0.0f && engine.getPhaseScore() <= 100.0f,
                     "phase score should be normalized");
    passed &= expect(engine.getStereoScore() >= 0.0f && engine.getStereoScore() <= 100.0f,
                     "stereo score should be normalized");

    juce::AudioBuffer<float> playback(2, 64);
    playback.clear();
    engine.process(playback, 1.0f);
    bool containsSignal = false;
    for (int sample = 0; sample < playback.getNumSamples(); ++sample)
    {
        const auto value = playback.getSample(0, sample);
        containsSignal = containsSignal || std::abs(value) > 1.0e-5f;
        passed &= expect(std::isfinite(value), "playback must contain finite samples");
    }
    passed &= expect(containsSignal, "playback should emit the selected loop");

    const auto savedState = engine.createLoopState();
    passed &= expect(!savedState.isEmpty(), "captured loop should serialize");

    LoopEngine restored;
    restored.prepare(testSampleRate, 64, 2);
    passed &= expect(restored.restoreLoopState(savedState.getData(), savedState.getSize()),
                     "captured loop should restore");
    passed &= expect(restored.getState() == LoopEngine::State::ready,
                     "restored loop should be immediately ready");
    passed &= expect(restored.getCapturedSampleCount() == engine.getCapturedSampleCount(),
                     "restored loop length should match");

    engine.clear();
    playback.clear();
    engine.process(playback, 1.0f);
    passed &= expect(engine.getState() == LoopEngine::State::empty,
                     "clear should return the engine to empty without clearing large buffers");

    if (!passed)
        return 1;

    std::cout << "LoopSurgeon engine tests passed; 48 kHz analysis took "
              << analysisDuration.count() << " ms\n";
    return 0;
}
