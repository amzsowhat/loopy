#include "LoopAnalyzer.h"
#include "LoopEngine.h"
#include "TextureSynthesizer.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace
{
bool expect(const bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool waitForReady(LoopEngine& engine)
{
    for (int attempt = 0; attempt < 500; ++attempt)
    {
        if (engine.getState() == LoopEngine::State::ready)
            return true;
        if (engine.getState() == LoopEngine::State::failed)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
}

int main()
{
    bool passed = true;
    constexpr auto sampleRate = 4000.0;
    constexpr auto period = 1200;
    juce::AudioBuffer<float> repeated(2, period * 6);
    for (int sample = 0; sample < repeated.getNumSamples(); ++sample)
    {
        const auto phase = juce::MathConstants<float>::twoPi
                           * static_cast<float>(sample % period)
                           / static_cast<float>(period);
        repeated.setSample(0, sample, 0.55f * std::sin(7.0f * phase)
                                          + 0.18f * std::sin(19.0f * phase));
        repeated.setSample(1, sample, 0.48f * std::sin(7.0f * phase + 0.08f)
                                          + 0.20f * std::sin(19.0f * phase - 0.11f));
    }

    const auto automatic = LoopAnalyzer::analyzeSource(
        repeated, sampleRate, 800, 1600, 3, 80);
    passed &= expect(!automatic.candidates.empty(),
                     "R&R analysis should find candidates");

    const auto exact = LoopAnalyzer::analyzeRotateRepairExact(
        repeated, sampleRate, 4000, 3, 160);
    passed &= expect(!exact.candidates.empty(),
                     "R&R should find an exact-duration repair");
    if (!exact.candidates.empty())
        passed &= expect(LoopAnalyzer::renderRotateRepair(
            repeated, exact.candidates.front()).getNumSamples() == 4000,
            "R&R final length must be sample-exact");

    LoopEngine engine;
    engine.prepare(sampleRate, 64, 2);
    engine.setGenerationMode(LoopEngine::GenerationMode::rotateRepair);
    engine.submitSource(repeated, "periodic-probe.wav");
    passed &= expect(engine.reanalyzeSourceRange(0.1f, 0.9f),
                     "R&R should accept Source In/Out");
    passed &= expect(waitForReady(engine),
                     "R&R analysis should complete");
    passed &= expect(engine.getCandidateCount() > 0,
                     "R&R should expose seam candidates");
    passed &= expect(engine.setManualRotationPoint(0.4f),
                     "R&R should accept a manual loop start");

    const auto state = engine.createLoopState();
    LoopEngine restored;
    restored.prepare(sampleRate, 64, 2);
    passed &= expect(restored.restoreLoopState(state.getData(), state.getSize()),
                     "saved DAW state should restore source and output");
    passed &= expect(restored.getSignalSnapshot().valid,
                     "restored output should include signal diagnostics");

    TextureSynthesisSettings textureSettings;
    textureSettings.durationSeconds = 4.0f;
    textureSettings.variation = 0.61f;
    textureSettings.flatten = 0.74f;
    textureSettings.sourceMatch = 0.82f;
    textureSettings.structure = TextureStructure::automatic;
    textureSettings.seed = 0x12345678u;
    const auto textureA = TextureSynthesizer::synthesize(
        repeated, sampleRate, textureSettings);
    const auto textureB = TextureSynthesizer::synthesize(
        repeated, sampleRate, textureSettings);
    passed &= expect(textureA.audio.getNumSamples() == 16000,
                     "Texture output length must be sample-exact");
    passed &= expect(textureA.containsOnlyFiniteSamples,
                     "Texture output must contain finite samples");
    passed &= expect(textureA.analysisFrameStarts.size() > 2u,
                     "Texture construction should traverse multiple source regions");
    auto deterministic = textureA.audio.getNumChannels() == textureB.audio.getNumChannels()
                         && textureA.audio.getNumSamples() == textureB.audio.getNumSamples();
    for (int channel = 0; deterministic && channel < textureA.audio.getNumChannels(); ++channel)
        for (int sample = 0; deterministic && sample < textureA.audio.getNumSamples(); ++sample)
            deterministic = textureA.audio.getSample(channel, sample)
                            == textureB.audio.getSample(channel, sample);
    passed &= expect(deterministic,
                     "Texture construction must be deterministic for a stored seed");

    LoopEngine textureEngine;
    textureEngine.prepare(sampleRate, 64, 2);
    textureEngine.setGenerationMode(LoopEngine::GenerationMode::textureLoop);
    textureEngine.setTextureDurationSeconds(4.0f);
    textureEngine.submitSource(repeated, "source-probe.wav");
    passed &= expect(textureEngine.reanalyzeSourceRange(0.1f, 0.9f),
                     "Texture mode should accept Source In/Out");
    passed &= expect(waitForReady(textureEngine),
                     "Texture generation should complete inside LoopEngine");
    passed &= expect(textureEngine.getLastUsedGenerationMode()
                         == LoopEngine::GenerationMode::textureLoop,
                     "Texture mode should remain active after generation");
    passed &= expect(textureEngine.createRenderedLoop().getNumSamples() == 16000,
                     "Texture mode should expose the requested loop to the DAW");

    if (!passed)
        return 1;
    std::cout << "Loop Surgeon tests passed\n";
    return 0;
}
