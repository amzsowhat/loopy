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
    void setRotation(float proportion);
    void setSourceRange(float newStart, float newEnd);
    [[nodiscard]] float getSourceIn() const noexcept { return sourceIn; }
    [[nodiscard]] float getSourceOut() const noexcept { return sourceOut; }
    [[nodiscard]] float getRotation() const noexcept { return rotation; }
    [[nodiscard]] bool isEditingRotation() const noexcept;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void()> onSourceRangeEdited;
    std::function<void()> onRotationCommitted;

private:
    enum class DragTarget
    {
        none,
        sourceIn,
        sourceOut,
        sourceRange,
        rotation
    };
    std::vector<float> peaks;
    float rotation = -1.0f;
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
    void setTextureMode(bool shouldUseTextureLabels);
    void paint(juce::Graphics&) override;

private:
    std::array<float, 6> scores {};
    bool textureMode = true;
};

class SignalAnalysisView final : public juce::Component
{
public:
    void setSnapshot(RenderQuality::SignalSnapshot next);
    void paint(juce::Graphics&) override;

private:
    RenderQuality::SignalSnapshot snapshot;
};

class RenderDragButton final : public juce::TextButton
{
public:
    RenderDragButton();

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<juce::File()> prepareFile;
    std::function<void(const juce::String&)> reportStatus;

private:
    bool dragStarted = false;
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
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void importFile(const juce::File& file);
    void chooseImportFile();
    void chooseExportFile();
    juce::File prepareDawDragFile();
    void configureSlider(juce::Slider&, juce::Label&, const juce::String&);
    void updateRangeLabel();
    void updatePrimaryAction();
    void drawCard(juce::Graphics&, juce::Rectangle<int>, const juce::String&,
                  juce::Colour) const;

    LoopSurgeonAudioProcessor& processor;
    LoopSurgeonLookAndFeel lookAndFeel;
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label dropLabel;
    juce::Label sourceLabel;
    juce::Label statusLabel;
    LoopWaveformView waveformView;
    SignalAnalysisView signalAnalysisView;
    LoopQualityView qualityView;
    juce::ComboBox candidateBox;
    juce::Label rangeLabel;
    juce::TextButton analyzeRangeButton { "Generate" };
    juce::TextButton resetRangeButton { "Full Source" };
    juce::TextButton regenerateButton { "New Variation" };
    juce::TextButton previewTransportButton { "Preview" };
    juce::TextButton originalPreviewButton { "Source" };
    juce::TextButton loopPreviewButton { "Generated" };
    juce::TextButton importButton { "Choose Audio..." };
    RenderDragButton dragToDawButton;
    juce::TextButton exportButton { "Save WAV..." };
    juce::TextButton captureButton { "Record DAW Input" };
    juce::TextButton clearButton { "Clear Result" };
    juce::Slider crossfadeSlider;
    juce::Slider mixSlider;
    juce::Slider durationSlider;
    juce::Slider repairDurationSlider;
    juce::Slider flattenSlider;
    juce::Slider sourceMatchSlider;
    juce::ComboBox generationModeBox;
    juce::Label crossfadeLabel;
    juce::Label mixLabel;
    juce::Label durationLabel;
    juce::Label repairDurationLabel;
    juce::Label flattenLabel;
    juce::Label sourceMatchLabel;
    juce::Label modeLabel;
    std::unique_ptr<SliderAttachment> crossfadeAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> durationAttachment;
    std::unique_ptr<SliderAttachment> repairDurationAttachment;
    std::unique_ptr<SliderAttachment> flattenAttachment;
    std::unique_ptr<SliderAttachment> sourceMatchAttachment;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String lastMessage;
    juce::String displayedSource;
    int displayedCandidateCount = -1;
    uint64_t displayedCandidateRevision = 0;
    uint64_t displayedSourceRevision = 0;

    juce::Rectangle<int> sourceCard;
    juce::Rectangle<int> waveformCard;
    juce::Rectangle<int> auditionCard;
    juce::Rectangle<int> finishCard;
    juce::Rectangle<int> footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessorEditor)
};
