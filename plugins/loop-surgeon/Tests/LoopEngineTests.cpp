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

    if (!passed)
        return 1;
    std::cout << "Loop Surgeon tests passed\n";
    return 0;
}

