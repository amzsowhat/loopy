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
    void clearLoop() noexcept { loopEngine.clear(); }
    [[nodiscard]] LoopEngine::State getLoopState() const noexcept { return loopEngine.getState(); }
    [[nodiscard]] float getCaptureProgress() const noexcept { return loopEngine.getCaptureProgress(); }
    [[nodiscard]] float getSeamQuality() const noexcept { return loopEngine.getSeamQuality(); }
    [[nodiscard]] float getWaveformScore() const noexcept { return loopEngine.getWaveformScore(); }
    [[nodiscard]] float getLevelScore() const noexcept { return loopEngine.getLevelScore(); }
    [[nodiscard]] float getSlopeScore() const noexcept { return loopEngine.getSlopeScore(); }
    [[nodiscard]] float getSpectrumScore() const noexcept { return loopEngine.getSpectrumScore(); }
    [[nodiscard]] float getPhaseScore() const noexcept { return loopEngine.getPhaseScore(); }
    [[nodiscard]] float getStereoScore() const noexcept { return loopEngine.getStereoScore(); }
    [[nodiscard]] float getTransientScore() const noexcept { return loopEngine.getTransientScore(); }
    [[nodiscard]] float getPeriodicityScore() const noexcept { return loopEngine.getPeriodicityScore(); }
    [[nodiscard]] float getRepairScore() const noexcept { return loopEngine.getRepairScore(); }
    [[nodiscard]] bool isLowConfidence() const noexcept { return loopEngine.isLowConfidence(); }
    [[nodiscard]] juce::String getSourceName() const { return loopEngine.getSourceName(); }
    [[nodiscard]] double getSourceDurationSeconds() const { return loopEngine.getSourceDurationSeconds(); }
    bool analyzeSourceRange(float start, float end);
    bool regenerateTexture(float start, float end);
    bool setManualLoopRange(float start, float end) { return loopEngine.setManualLoopRange(start, end); }
    [[nodiscard]] int getCandidateCount() const { return loopEngine.getCandidateCount(); }
    [[nodiscard]] uint64_t getCandidateRevision() const noexcept { return loopEngine.getCandidateRevision(); }
    [[nodiscard]] uint64_t getSourceRevision() const noexcept { return loopEngine.getSourceRevision(); }
    [[nodiscard]] juce::String getCandidateDescription(int index) const { return loopEngine.getCandidateDescription(index); }
    void selectCandidate(int index) { loopEngine.selectCandidate(index); }
    [[nodiscard]] std::vector<float> getWaveformPreview() const { return loopEngine.getWaveformPreview(); }
    [[nodiscard]] float getLoopStartProportion() const noexcept { return loopEngine.getLoopStartProportion(); }
    [[nodiscard]] float getLoopEndProportion() const noexcept { return loopEngine.getLoopEndProportion(); }
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
