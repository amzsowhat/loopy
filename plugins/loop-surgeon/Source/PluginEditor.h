#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

#include <array>

class LoopSurgeonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LoopSurgeonLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
};

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
    enum class DragTarget { none, sourceIn, sourceOut, sourceRange, loopIn, loopOut };
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

class LoopQualityView final : public juce::Component
{
public:
    void setScores(float quality, float repair, float spectrum,
                   float phase, float stereo, float transient);
    void paint(juce::Graphics&) override;

private:
    std::array<float, 6> scores {};
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
    void updateRangeLabel();
    void updatePrimaryAction();
    void drawCard(juce::Graphics&, juce::Rectangle<int>, const juce::String&,
                  const juce::String&, juce::Colour) const;

    LoopSurgeonAudioProcessor& processor;
    LoopSurgeonLookAndFeel lookAndFeel;
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label dropLabel;
    juce::Label sourceLabel;
    juce::Label selectionHelpLabel;
    juce::Label previewHelpLabel;
    juce::Label statusLabel;
    LoopWaveformView waveformView;
    LoopQualityView qualityView;
    juce::ComboBox candidateBox;
    juce::Label rangeLabel;
    juce::TextButton analyzeRangeButton { "Find Best Loop" };
    juce::TextButton resetRangeButton { "Reset Search" };
    juce::TextButton previewTransportButton { "Preview" };
    juce::TextButton originalPreviewButton { "Original" };
    juce::TextButton loopPreviewButton { "Loop" };
    juce::TextButton importButton { "Choose Audio..." };
    juce::TextButton exportButton { "Export Loop WAV" };
    juce::TextButton captureButton { "Record DAW Input" };
    juce::TextButton clearButton { "Clear Session" };
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
    bool manualLoopEdited = false;
    bool sourceRangeEdited = false;

    juce::Rectangle<int> sourceCard;
    juce::Rectangle<int> workflowArea;
    juce::Rectangle<int> waveformCard;
    juce::Rectangle<int> auditionCard;
    juce::Rectangle<int> finishCard;
    juce::Rectangle<int> footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessorEditor)
};
