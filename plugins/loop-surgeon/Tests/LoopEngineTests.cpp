#include "LoopEngine.h"

#include <cmath>
#include <iostream>

namespace
{
bool expect(const bool condition, const char* message)
{
    if (condition)
        return true;

    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool passed = true;
    LoopEngine engine;
    engine.prepare(1000.0, 64, 1);
    engine.setLoopLengthSeconds(0.01f);
    engine.setCrossfadeMilliseconds(2.0f);
    engine.beginCapture();

    juce::AudioBuffer<float> capture(1, 10);
    for (int sample = 0; sample < capture.getNumSamples(); ++sample)
        capture.setSample(0, sample, std::sin(juce::MathConstants<float>::twoPi
                                              * static_cast<float>(sample) / 10.0f));

    engine.process(capture, 1.0f);
    passed &= expect(engine.getState() == LoopEngine::State::ready,
                     "capture should transition to ready");
    passed &= expect(engine.getCapturedSampleCount() == 10,
                     "capture should contain the configured number of samples");
    passed &= expect(engine.getCaptureProgress() == 1.0f,
                     "capture progress should complete");
    passed &= expect(engine.getSeamQuality() >= 0.0f && engine.getSeamQuality() <= 100.0f,
                     "seam quality should be normalized");

    juce::AudioBuffer<float> playback(1, 64);
    playback.clear();
    engine.process(playback, 1.0f);

    bool containsSignal = false;
    for (int sample = 0; sample < playback.getNumSamples(); ++sample)
    {
        const auto value = playback.getSample(0, sample);
        containsSignal = containsSignal || std::abs(value) > 1.0e-5f;
        passed &= expect(std::isfinite(value), "playback must contain finite samples");
    }
    passed &= expect(containsSignal, "playback should emit the captured loop");

    engine.clear();
    playback.clear();
    engine.process(playback, 1.0f);
    passed &= expect(engine.getState() == LoopEngine::State::empty,
                     "clear should return the engine to empty");

    if (!passed)
        return 1;

    std::cout << "LoopSurgeon engine tests passed\n";
    return 0;
}

