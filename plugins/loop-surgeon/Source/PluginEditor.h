#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

class LoopWaveformView final : public juce::Component
{
public:
    void setWaveform(std::vector<float> newPeaks);
    void setLoop(float newStart, float newEnd);
    void setSourceRange(float newStart, float newEnd);
    [[nodiscard]] float getSourceIn() const noexcept { return sourceIn; }
    [[nodiscard]] float getSourceOut() const noexcept { return sourceOut; }
    [[nodiscard]] float getLoopIn() const noexcept { return loopStart; }
    [[nodiscard]] float getLoopOut() const noexcept { return loopEnd; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void()> onSourceRangeEdited;
    std::function<void()> onLoopRangeEdited;
    std::function<void()> onLoopRangeCommitted;

private:
    enum class DragTarget { none, sourceIn, sourceOut, range, loopIn, loopOut };
    std::vector<float> peaks;
    float loopStart = 0.0f;
    float loopEnd = 0.0f;
    float sourceIn = 0.0f;
    float sourceOut = 1.0f;
    float dragAnchor = 0.0f;
    float dragStartIn = 0.0f;
    float dragStartOut = 1.0f;
    DragTarget dragTarget = DragTarget::none;
};

class LoopSurgeonAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              public juce::FileDragAndDropTarget,
                                              private juce::Timer
{
public:
    explicit LoopSurgeonAudioProcessorEditor(LoopSurgeonAudioProcessor&);
    ~LoopSurgeonAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void importFile(const juce::File& file);
    void chooseImportFile();
    void chooseExportFile();
    void configureSlider(juce::Slider&, juce::Label&, const juce::String&);

    LoopSurgeonAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label dropLabel;
    juce::Label sourceLabel;
    juce::Label statusLabel;
    juce::Label metricsLabel;
    LoopWaveformView waveformView;
    juce::ComboBox candidateBox;
    juce::Label rangeLabel;
    juce::TextButton analyzeRangeButton { "Analyze Selection" };
    juce::TextButton resetRangeButton { "Full Source" };
    juce::TextButton originalPreviewButton { "Original" };
    juce::TextButton loopPreviewButton { "Loop" };
    juce::TextButton importButton { "Import Audio" };
    juce::TextButton exportButton { "Export Loop WAV" };
    juce::TextButton captureButton { "Use DAW Input" };
    juce::TextButton clearButton { "Clear" };
    juce::Slider crossfadeSlider;
    juce::Slider mixSlider;
    juce::Label crossfadeLabel;
    juce::Label mixLabel;
    std::unique_ptr<SliderAttachment> crossfadeAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String lastMessage;
    juce::String displayedSource;
    int displayedCandidateCount = -1;
    uint64_t displayedCandidateRevision = 0;
    uint64_t displayedSourceRevision = 0;

    void updateRangeLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessorEditor)
};
