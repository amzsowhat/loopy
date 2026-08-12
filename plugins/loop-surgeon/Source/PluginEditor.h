#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

class LoopSurgeonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LoopSurgeonLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted,
                        bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosition, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool separator, bool active, bool highlighted,
                           bool ticked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    [[nodiscard]] juce::Font getDisplayFont(float height) const;
    [[nodiscard]] juce::Font getHandFont(float height) const;
    [[nodiscard]] const juce::Image& getMachineSkin() const noexcept { return machineSkin; }

private:
    juce::Image machineSkin;
    juce::Image knobImage;
    juce::Image generateButtonImage;
    juce::Typeface::Ptr displayTypeface;
    juce::Typeface::Ptr handTypeface;
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

class SignalAnalysisView final : public juce::Component
{
public:
    void setSnapshot(SignalDiagnostics::SignalSnapshot next);
    void paint(juce::Graphics&) override;

private:
    SignalDiagnostics::SignalSnapshot snapshot;
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
    void applyModeLayout();

    LoopSurgeonAudioProcessor& processor;
    LoopSurgeonLookAndFeel lookAndFeel;
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label dropLabel;
    juce::Label sourceLabel;
    juce::Label statusLabel;
    LoopWaveformView waveformView;
    SignalAnalysisView signalAnalysisView;
    juce::ComboBox candidateBox;
    juce::Label rangeLabel;
    juce::TextButton analyzeRangeButton { "Generate" };
    juce::TextButton resetRangeButton { "FULL SOURCE" };
    juce::TextButton regenerateButton { "NEW VARIATION" };
    juce::TextButton previewTransportButton { "PREVIEW" };
    juce::TextButton originalPreviewButton { "SOURCE" };
    juce::TextButton loopPreviewButton { "RESULT" };
    juce::TextButton importButton { "LOAD AUDIO" };
    RenderDragButton dragToDawButton;
    juce::TextButton exportButton { "SAVE WAV" };
    juce::TextButton captureButton { "RECORD INPUT" };
    juce::TextButton clearButton { "CLEAR" };
    juce::Slider crossfadeSlider;
    juce::Slider mixSlider;
    juce::Slider durationSlider;
    juce::Slider repairDurationSlider;
    juce::Slider flattenSlider;
    juce::Slider dynamicsCrushSlider;
    juce::Slider sourceMatchSlider;
    juce::Slider characterAmountSlider;
    juce::ComboBox generationModeBox;
    juce::ComboBox textureStructureBox;
    juce::ComboBox characterBox;
    juce::Label crossfadeLabel;
    juce::Label mixLabel;
    juce::Label durationLabel;
    juce::Label repairDurationLabel;
    juce::Label flattenLabel;
    juce::Label dynamicsCrushLabel;
    juce::Label sourceMatchLabel;
    juce::Label characterAmountLabel;
    juce::Label modeLabel;
    juce::Label textureStructureLabel;
    juce::Label characterLabel;
    std::unique_ptr<SliderAttachment> crossfadeAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> durationAttachment;
    std::unique_ptr<SliderAttachment> repairDurationAttachment;
    std::unique_ptr<SliderAttachment> flattenAttachment;
    std::unique_ptr<SliderAttachment> dynamicsCrushAttachment;
    std::unique_ptr<SliderAttachment> sourceMatchAttachment;
    std::unique_ptr<SliderAttachment> characterAmountAttachment;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    std::unique_ptr<ComboBoxAttachment> textureStructureAttachment;
    std::unique_ptr<ComboBoxAttachment> characterAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String lastMessage;
    juce::String transientStatusMessage;
    double transientStatusExpiresAtMs = 0.0;
    juce::String displayedSource;
    int displayedCandidateCount = -1;
    uint64_t displayedCandidateRevision = 0;
    uint64_t displayedSourceRevision = 0;

    juce::Rectangle<int> primaryActionArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessorEditor)
};
