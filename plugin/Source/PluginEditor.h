#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>

#include "PluginProcessor.h"
#include "UiInteractionModel.h"
#include "UiTheme.h"

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

private:
    juce::Typeface::Ptr displayTypeface;
    juce::Typeface::Ptr handTypeface;
};

class LoopWaveformView final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    LoopWaveformView();

    void setWaveform(std::vector<float> newPeaks);
    void setRotation(float proportion);
    void setSourceRange(float newStart, float newEnd);
    void setDurationSeconds(double seconds);
    [[nodiscard]] float getSourceIn() const noexcept { return sourceIn; }
    [[nodiscard]] float getSourceOut() const noexcept { return sourceOut; }
    [[nodiscard]] float getRotation() const noexcept { return rotation; }
    [[nodiscard]] bool isEditingRotation() const noexcept;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    std::function<void()> onSourceRangeEdited;
    std::function<void()> onRotationCommitted;

private:
    [[nodiscard]] juce::Path createAperturePath() const;
    [[nodiscard]] juce::Rectangle<float> getApertureBounds() const;
    void updateMarkerPopup(float proportion, const juce::String& prefix,
                           juce::Colour colour);
    void hideMarkerPopup();
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
    double durationSeconds = 0.0;
    DragTarget dragTarget = DragTarget::none;
    juce::Label markerPopup;
};

class SignalAnalysisView final : public juce::Component
{
public:
    void setSnapshot(SignalDiagnostics::SignalSnapshot next);
    void paint(juce::Graphics&) override;

private:
    SignalDiagnostics::SignalSnapshot snapshot;
};

class TailActionButton : public juce::TextButton,
                         private juce::Timer
{
public:
    using juce::TextButton::TextButton;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

protected:
    [[nodiscard]] float getPressOffset() const noexcept { return pressProgress * 2.0f; }

private:
    void timerCallback() override;
    void ensureAnimationRunning();
    float hoverProgress = 0.0f;
    float pressProgress = 0.0f;
    float hoverTarget = 0.0f;
    float pressTarget = 0.0f;
    double lastFrameMs = 0.0;
};

class RenderDragButton final : public TailActionButton
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

class IllustratedRotaryControl final : public juce::Slider,
                                       private juce::Timer
{
public:
    enum class Mechanism
    {
        length,
        stability,
        crush,
        transform,
        loopStart
    };

    using juce::Slider::Slider;
    void setMechanism(Mechanism next) noexcept { mechanism = next; repaint(); }
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void ensureAnimationRunning();
    [[nodiscard]] juce::String getFixedValueText();
    [[nodiscard]] juce::String getTextFromValue(double) const;
    Mechanism mechanism = Mechanism::length;
    float hoverProgress = 0.0f;
    float pressProgress = 0.0f;
    float hoverTarget = 0.0f;
    float pressTarget = 0.0f;
    double lastFrameMs = 0.0;
};

class GenerateArtworkButton final : public juce::TextButton
{
public:
    explicit GenerateArtworkButton(const juce::String& text) : juce::TextButton(text) {}
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    void setWorking(bool next) { if (working != next) { working = next; repaint(); } }

private:
    bool working = false;
};

class ArtworkChoiceButton final : public juce::TextButton,
                                  private juce::Timer
{
public:
    explicit ArtworkChoiceButton(int spriteIndex = 0) : index(spriteIndex) {}
    void setStateLabel(juce::String next) { stateLabel = std::move(next); repaint(); }
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    void timerCallback() override;
    void ensureAnimationRunning();
    int index = 0;
    juce::String stateLabel;
    float hoverProgress = 0.0f;
    float pressProgress = 0.0f;
    float selectedProgress = 0.0f;
    float hoverTarget = 0.0f;
    float pressTarget = 0.0f;
    double lastFrameMs = 0.0;
};

class IllustratedStripControl final : public juce::Slider,
                                      private juce::Timer
{
public:
    using juce::Slider::Slider;
    void setPathKind(LoopSurgeonUi::StripPathKind next) noexcept
    {
        pathKind = next;
        repaint();
    }
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void ensureAnimationRunning();
    void updateValueFromPosition(juce::Point<float> position);
    [[nodiscard]] juce::Rectangle<float> trackBounds() const;
    [[nodiscard]] juce::Point<float> pointOnTrack(float normalised) const;
    [[nodiscard]] juce::Point<float> tangentOnTrack(float normalised) const;
    [[nodiscard]] juce::String getFixedValueText();
    [[nodiscard]] juce::String getTextFromValue(double) const;
    LoopSurgeonUi::StripPathKind pathKind = LoopSurgeonUi::StripPathKind::join;
    float hoverProgress = 0.0f;
    float pressProgress = 0.0f;
    float hoverTarget = 0.0f;
    float pressTarget = 0.0f;
    double lastFrameMs = 0.0;
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
    void updateArtworkStates();

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
    GenerateArtworkButton analyzeRangeButton { "Generate" };
    juce::TextButton resetRangeButton { "FULL SOURCE" };
    juce::TextButton regenerateButton { "NEW VARIATION" };
    TailActionButton previewTransportButton { "PREVIEW" };
    juce::TextButton originalPreviewButton { "SOURCE" };
    juce::TextButton loopPreviewButton { "RESULT" };
    juce::TextButton importButton { "LOAD AUDIO" };
    RenderDragButton dragToDawButton;
    TailActionButton exportButton { "SAVE WAV" };
    juce::TextButton captureButton { "RECORD INPUT" };
    juce::TextButton clearButton { "CLEAR" };
    juce::TextButton rotateModeButton;
    juce::TextButton textureModeButton;
    juce::TextButton motionFlowButton;
    juce::TextButton motionDriftButton;
    juce::TextButton motionFractureButton;
    juce::TextButton extraOffButton;
    ArtworkChoiceButton extraPatinaButton { 0 };
    ArtworkChoiceButton extraBloomButton { 1 };
    ArtworkChoiceButton extraFrayButton { 2 };
    IllustratedRotaryControl crossfadeSlider;
    IllustratedRotaryControl mixSlider;
    IllustratedRotaryControl durationSlider;
    IllustratedRotaryControl repairDurationSlider;
    IllustratedRotaryControl flattenSlider;
    IllustratedRotaryControl dynamicsCrushSlider;
    IllustratedRotaryControl sourceMatchSlider;
    IllustratedRotaryControl repairLoopStartSlider;
    IllustratedStripControl characterAmountSlider;
    IllustratedStripControl joinPositionSlider;
    IllustratedStripControl motionSelectorSlider;
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
    juce::Label repairLoopStartLabel;
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
    std::unique_ptr<SliderAttachment> repairLoopStartAttachment;
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
    bool displayedResultReady = false;
    bool displayedSourceReady = false;
    int displayedRepairCandidate = 0;

    juce::Rectangle<int> primaryActionArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessorEditor)
};
