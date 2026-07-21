#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LoopEngine.h"

class LoopSurgeonAudioProcessor final : public juce::AudioProcessor
{
public:
    LoopSurgeonAudioProcessor();
    ~LoopSurgeonAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void beginCapture() noexcept;
    void clearLoop() noexcept { loopEngine.clear(); }
    [[nodiscard]] LoopEngine::State getLoopState() const noexcept { return loopEngine.getState(); }
    [[nodiscard]] float getCaptureProgress() const noexcept { return loopEngine.getCaptureProgress(); }
    [[nodiscard]] float getSeamQuality() const noexcept { return loopEngine.getSeamQuality(); }
    [[nodiscard]] float getLevelScore() const noexcept { return loopEngine.getLevelScore(); }
    [[nodiscard]] float getSlopeScore() const noexcept { return loopEngine.getSlopeScore(); }
    [[nodiscard]] float getSpectrumScore() const noexcept { return loopEngine.getSpectrumScore(); }
    [[nodiscard]] float getPhaseScore() const noexcept { return loopEngine.getPhaseScore(); }
    [[nodiscard]] float getStereoScore() const noexcept { return loopEngine.getStereoScore(); }

    juce::AudioProcessorValueTreeState& getParameterState() noexcept { return parameters; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    LoopEngine loopEngine;
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<bool> captureRequested { false };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessor)
};
