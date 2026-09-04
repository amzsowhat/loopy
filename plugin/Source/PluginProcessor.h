#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
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
    void setPreviewMode(LoopEngine::PreviewMode mode) noexcept { loopEngine.setPreviewMode(mode); }
    [[nodiscard]] LoopEngine::PreviewMode getPreviewMode() const noexcept { return loopEngine.getPreviewMode(); }
    void setPreviewPlaying(bool shouldPlay) noexcept { loopEngine.setPreviewPlaying(shouldPlay); }
    [[nodiscard]] bool isPreviewPlaying() const noexcept { return loopEngine.isPreviewPlaying(); }
    juce::String importAudioFile(const juce::File& file);
    juce::String exportLoopFile(const juce::File& file) const;
    void clearLoop() { loopEngine.clear(); }
    [[nodiscard]] LoopEngine::State getLoopState() const noexcept { return loopEngine.getState(); }
    [[nodiscard]] float getCaptureProgress() const noexcept { return loopEngine.getCaptureProgress(); }
    [[nodiscard]] float getAnalysisProgress() const noexcept
    {
        return loopEngine.getAnalysisProgress();
    }
    [[nodiscard]] juce::String getSourceName() const { return loopEngine.getSourceName(); }
    [[nodiscard]] double getSourceDurationSeconds() const { return loopEngine.getSourceDurationSeconds(); }
    [[nodiscard]] double getRenderedDurationSeconds() const noexcept
    {
        return loopEngine.getRenderedDurationSeconds();
    }
    bool analyzeSourceRange(float start, float end);
    bool regenerateTexture(float start, float end);
    bool setManualRotationPoint(float proportion)
    {
        return loopEngine.setManualRotationPoint(proportion);
    }
    [[nodiscard]] int getCandidateCount() const { return loopEngine.getCandidateCount(); }
    [[nodiscard]] uint64_t getCandidateRevision() const noexcept { return loopEngine.getCandidateRevision(); }
    [[nodiscard]] uint64_t getSourceRevision() const noexcept { return loopEngine.getSourceRevision(); }
    [[nodiscard]] juce::String getCandidateDescription(int index) const { return loopEngine.getCandidateDescription(index); }
    void selectCandidate(int index) { loopEngine.selectCandidate(index); }
    [[nodiscard]] std::vector<float> getWaveformPreview() const { return loopEngine.getWaveformPreview(); }
    [[nodiscard]] float getRotationProportion() const noexcept
    {
        return loopEngine.getRotationProportion();
    }
    [[nodiscard]] float getAnalysisRangeStartProportion() const noexcept
    {
        return loopEngine.getAnalysisRangeStartProportion();
    }
    [[nodiscard]] float getAnalysisRangeEndProportion() const noexcept
    {
        return loopEngine.getAnalysisRangeEndProportion();
    }
    [[nodiscard]] SignalDiagnostics::SignalSnapshot getSignalSnapshot() const
    {
        return loopEngine.getSignalSnapshot();
    }
    [[nodiscard]] LoopEngine::GenerationMode getLastUsedGenerationMode() const noexcept
    {
        return loopEngine.getLastUsedGenerationMode();
    }

    juce::AudioProcessorValueTreeState& getParameterState() noexcept { return parameters; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void syncGenerationControlsForAnalysis() noexcept;

    LoopEngine loopEngine;
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<bool> captureRequested { false };
    double currentSampleRate = 44100.0;
    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessor)
};
