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

    constexpr auto automaticSampleRate = 2000.0;
    constexpr auto truePeriod = 800;
    juce::AudioBuffer<float> repeatedMaterial(2, truePeriod * 6);
    for (int sample = 0; sample < repeatedMaterial.getNumSamples(); ++sample)
    {
        const auto position = sample % truePeriod;
        const auto envelope = 0.35f + 0.55f * std::sin(
            juce::MathConstants<float>::twoPi * static_cast<float>(position) / truePeriod);
        const auto carrier = std::sin(juce::MathConstants<float>::twoPi
                                      * 13.0f * static_cast<float>(position) / truePeriod);
        repeatedMaterial.setSample(0, sample, envelope * carrier);
        repeatedMaterial.setSample(1, sample, 0.8f * envelope * std::sin(
            juce::MathConstants<float>::twoPi * 13.0f * static_cast<float>(position) / truePeriod + 0.1f));
    }
    const auto automatic = LoopAnalyzer::analyzeSource(repeatedMaterial, automaticSampleRate,
                                                        500, 1200, 3);
    passed &= expect(!automatic.candidates.empty(),
                     "automatic source analysis should produce candidates");
    if (!automatic.candidates.empty())
    {
        const auto detectedLength = automatic.candidates.front().endSample
                                    - automatic.candidates.front().startSample;
        passed &= expect(std::abs(detectedLength - truePeriod) < 100,
                         "automatic source analysis should recover the repeated period");
        passed &= expect(automatic.candidates.front().transient >= 0.0f
                             && automatic.candidates.front().transient <= 100.0f,
                         "transient seam score should be normalized");
    }
    const auto overlapAware = LoopAnalyzer::analyzeSource(repeatedMaterial, automaticSampleRate,
                                                           500, 1200, 3, 50);
    passed &= expect(!overlapAware.candidates.empty(),
                     "overlap-aware analysis should produce candidates");
    if (!overlapAware.candidates.empty())
    {
        const auto selectedRepair = overlapAware.candidates.front().repairOverlapSamples;
        const auto renderedLength = overlapAware.candidates.front().endSample
                                    - overlapAware.candidates.front().startSample - selectedRepair;
        passed &= expect(selectedRepair > 0 && selectedRepair <= 50,
                         "automatic analysis should choose a repair window within the user maximum");
        passed &= expect(std::abs(renderedLength - truePeriod) < 100,
                         "crossfade overlap must not shorten the intended loop period");
        passed &= expect(overlapAware.candidates.front().repair >= 0.0f
                             && overlapAware.candidates.front().repair <= 100.0f,
                         "post-repair seam score should be normalized");
    }
    const auto exactRange = LoopAnalyzer::evaluateFixedRange(repeatedMaterial,
                                                              automaticSampleRate, 0, truePeriod);
    const auto brokenRange = LoopAnalyzer::evaluateFixedRange(repeatedMaterial,
                                                               automaticSampleRate, 0, truePeriod - 137);
    passed &= expect(exactRange.waveform > brokenRange.waveform,
                     "direct boundary score should prefer the sample-continuous period");
    const auto repairedExact = LoopAnalyzer::evaluateFixedRange(
        repeatedMaterial, automaticSampleRate, 0, truePeriod + 50, 50);
    const auto repairedBroken = LoopAnalyzer::evaluateFixedRange(
        repeatedMaterial, automaticSampleRate, 0, truePeriod - 137 + 50, 50);
    passed &= expect(repairedExact.repair > repairedBroken.repair,
                     "post-render repair scoring should reject a misleading bad seam");

    juce::AudioBuffer<float> silenceTrap(1, truePeriod * 6);
    silenceTrap.clear();
    for (int sample = truePeriod; sample < silenceTrap.getNumSamples(); ++sample)
    {
        const auto position = sample % truePeriod;
        const auto tone = 0.5f * std::sin(
            juce::MathConstants<float>::twoPi * 9.0f * static_cast<float>(position)
            / truePeriod);
        const auto texture = 0.18f * std::sin(
            juce::MathConstants<float>::twoPi * 23.0f * static_cast<float>(position)
            / truePeriod + 0.3f);
        silenceTrap.setSample(0, sample, tone + texture);
    }
    const auto silenceTrapReport = LoopAnalyzer::analyzeSource(
        silenceTrap, automaticSampleRate, 650, 1150, 3, 50);
    passed &= expect(!silenceTrapReport.candidates.empty(),
                     "quality corpus with a silent prefix should still produce an active loop");
    if (!silenceTrapReport.candidates.empty())
        passed &= expect(silenceTrapReport.candidates.front().startSample >= truePeriod / 2,
                         "silence activity penalty should prevent a perfect-score silent loop");

    LoopEngine rangeEngine;
    rangeEngine.prepare(automaticSampleRate, 64, 2);
    rangeEngine.submitSource(repeatedMaterial, "repeated-material.wav");
    passed &= expect(waitForReady(rangeEngine), "imported source should be analysed in background");
    passed &= expect(rangeEngine.getCandidateCount() > 0,
                     "imported source should expose alternate candidates");
    passed &= expect(rangeEngine.getWaveformPreview().size() == 320,
                     "imported source should publish a bounded waveform preview");
    passed &= expect(rangeEngine.reanalyzeSourceRange(0.3f, 0.9f),
                     "source In/Out selection should queue reanalysis");
    passed &= expect(waitForReady(rangeEngine), "selected source range should finish analysis");
    passed &= expect(rangeEngine.getLoopStartProportion() >= 0.29f
                         && rangeEngine.getLoopEndProportion() <= 0.91f,
                     "automatic loop must remain inside user Source In/Out selection");
    passed &= expect(rangeEngine.setManualLoopRange(0.4f, 0.55f),
                     "draggable Loop In/Out should publish a manually refined loop");
    passed &= expect(std::abs(rangeEngine.getLoopStartProportion() - 0.4f) < 0.002f
                         && std::abs(rangeEngine.getLoopEndProportion() - 0.55f) < 0.002f,
                     "manual Loop In/Out should retain the requested visible range");
    const auto manualRendered = rangeEngine.createRenderedLoop();
    passed &= expect(std::abs(manualRendered.getNumSamples()
                              - juce::roundToInt(0.15 * repeatedMaterial.getNumSamples())) < 3,
                     "manual loop crossfade must preserve the visible Loop In/Out duration");
    rangeEngine.setPreviewMode(LoopEngine::PreviewMode::original);
    rangeEngine.setPreviewPlaying(true);
    juce::AudioBuffer<float> originalPreview(2, 64);
    originalPreview.clear();
    rangeEngine.process(originalPreview, 1.0f);
    passed &= expect(originalPreview.getMagnitude(0, 0, 64) > 0.0f
                         && originalPreview.getMagnitude(1, 0, 64) > 0.0f,
                     "Original A/B mode should audition imported stereo source");

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

    const auto rendered = engine.createRenderedLoop();
    passed &= expect(rendered.getNumSamples() > 0,
                     "seam repair should produce exportable loop audio");
    if (rendered.getNumSamples() > 1)
    {
        const auto boundaryJump = std::abs(rendered.getSample(0, rendered.getNumSamples() - 1)
                                           - rendered.getSample(0, 0));
        passed &= expect(boundaryJump < 0.25f,
                         "rendered loop boundary should not contain an obvious jump");
    }

    juce::AudioBuffer<float> stoppedPlayback(2, 64);
    stoppedPlayback.clear();
    engine.process(stoppedPlayback, 1.0f);
    passed &= expect(stoppedPlayback.getMagnitude(0, 0, 64) == 0.0f,
                     "preview should remain stopped until the user explicitly starts it");

    engine.setPreviewPlaying(true);
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
    if (rendered.getNumSamples() >= playback.getNumSamples())
    {
        double previewExportError = 0.0;
        for (int channel = 0; channel < playback.getNumChannels(); ++channel)
        {
            for (int sample = 0; sample < playback.getNumSamples(); ++sample)
                previewExportError += std::abs(playback.getSample(channel, sample)
                                               - rendered.getSample(channel, sample));
        }
        passed &= expect(previewExportError < 1.0e-5,
                         "preview should start on the exact same samples as exported audio");
    }
    if (!containsSignal)
    {
        const auto diagnosticLoop = engine.createRenderedLoop();
        std::cerr << "playback diagnostic: samples=" << engine.getCapturedSampleCount()
                  << " seam=" << engine.getSeamQuality()
                  << " rendered=" << (diagnosticLoop.getNumSamples() > 0
                      ? diagnosticLoop.getMagnitude(0, 0, diagnosticLoop.getNumSamples()) : -1.0f)
                  << '\n';
    }

    engine.setPreviewPlaying(false);
    juce::AudioBuffer<float> dryAfterStop(2, 64);
    dryAfterStop.clear();
    dryAfterStop.addSample(0, 7, 0.25f);
    dryAfterStop.addSample(1, 11, -0.2f);
    engine.process(dryAfterStop, 1.0f);
    passed &= expect(std::abs(dryAfterStop.getSample(0, 7) - 0.25f) < 1.0e-7f
                         && std::abs(dryAfterStop.getSample(1, 11) + 0.2f) < 1.0e-7f,
                     "Stop should immediately restore untouched DAW input");

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
