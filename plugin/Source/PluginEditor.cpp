#include "PluginEditor.h"

#include <utility>

namespace
{
constexpr auto ink = LoopSurgeonTheme::text;
constexpr auto softInk = LoopSurgeonTheme::secondary;
constexpr auto paper = LoopSurgeonTheme::text;
constexpr auto acidYellow = LoopSurgeonTheme::accent;
constexpr auto fadedCoral = LoopSurgeonTheme::accent;
constexpr auto dustyViolet = LoopSurgeonTheme::accent;
constexpr auto mint = LoopSurgeonTheme::accent;
constexpr auto charcoal = LoopSurgeonTheme::surface;
}

LoopSurgeonAudioProcessorEditor::LoopSurgeonAudioProcessorEditor(
    LoopSurgeonAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("loopy", juce::dontSendNotification);
    titleLabel.setComponentID("displayTitle");
    titleLabel.setFont(lookAndFeel.getDisplayFont(34.0f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(paper));
    addAndMakeVisible(titleLabel);

    versionLabel.setText("0.14 / LOCAL TEST",
                         juce::dontSendNotification);
    versionLabel.setFont(lookAndFeel.getHandFont(11.5f));
    versionLabel.setJustificationType(juce::Justification::centredLeft);
    versionLabel.setColour(juce::Label::textColourId, juce::Colour(softInk));
    addAndMakeVisible(versionLabel);

    dropLabel.setText({}, juce::dontSendNotification);
    dropLabel.setFont(lookAndFeel.getHandFont(14.0f));
    dropLabel.setColour(juce::Label::textColourId, juce::Colour(ink));
    addAndMakeVisible(dropLabel);

    sourceLabel.setColour(juce::Label::textColourId, juce::Colour(ink));
    sourceLabel.setFont(lookAndFeel.getHandFont(16.0f));
    sourceLabel.setJustificationType(juce::Justification::centredLeft);
    sourceLabel.setMinimumHorizontalScale(0.7f);
    sourceLabel.setText("NO SOURCE", juce::dontSendNotification);
    addAndMakeVisible(sourceLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(ink));
    statusLabel.setFont(lookAndFeel.getHandFont(15.0f));
    statusLabel.setComponentID("statusReadout");
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    addAndMakeVisible(waveformView);
    addAndMakeVisible(signalAnalysisView);
    waveformView.onSourceRangeEdited = [this]
    {
        waveformView.setRotation(-1.0f);
        updateRangeLabel();
        updatePrimaryAction();
    };
    waveformView.onRotationCommitted = [this]
    {
        if (processor.getLastUsedGenerationMode() != LoopEngine::GenerationMode::rotateRepair)
            return;
        if (processor.setManualRotationPoint(waveformView.getRotation()))
        {
            const auto range = waveformView.getSourceOut() - waveformView.getSourceIn();
            const auto normalised = range > 0.0001f
                ? (waveformView.getRotation() - waveformView.getSourceIn()) / range
                : 0.5f;
            repairLoopStartSlider.setValue(normalised, juce::sendNotificationSync);
            joinPositionSlider.setValue(normalised, juce::dontSendNotification);
            lastMessage = "Loop start updated - audition the repaired join";
        }
        else
            lastMessage = "Loop start must remain inside the blue source range";
    };
    waveformView.setTooltip(
        "Drag IN/OUT from the edge facing the selected range. Hold Alt for fine movement; Shift-drag inside the range to move it; double-click for Full Source.");
    rangeLabel.setColour(juce::Label::textColourId, juce::Colour(ink));
    rangeLabel.setJustificationType(juce::Justification::centredLeft);
    rangeLabel.setFont(lookAndFeel.getHandFont(12.0f));
    addAndMakeVisible(rangeLabel);

    analyzeRangeButton.onClick = [this]
    {
        if (processor.getSourceName().isEmpty())
        {
            chooseImportFile();
            return;
        }
        const auto directMode = generationModeBox.getSelectedItemIndex() == 0;
        const auto selectedSeconds = processor.getSourceDurationSeconds()
            * static_cast<double>(waveformView.getSourceOut() - waveformView.getSourceIn());
        const auto requestedRepairSeconds = processor.getParameterState()
            .getRawParameterValue("repairDuration")->load();
        if (directMode && requestedRepairSeconds > 0.05f
            && requestedRepairSeconds > selectedSeconds)
        {
            lastMessage = "Final Length exceeds Source In/Out - shorten it or widen the range";
            return;
        }
        if (processor.analyzeSourceRange(waveformView.getSourceIn(),
                                         waveformView.getSourceOut()))
        {
            lastMessage = "Generating from the selected source range...";
        }
        else
        {
            lastMessage = "Load a source before generating";
        }
        updatePrimaryAction();
    };
    analyzeRangeButton.setComponentID("artGenerate");
    analyzeRangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(acidYellow));
    analyzeRangeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(acidYellow));
    analyzeRangeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(ink));
    analyzeRangeButton.setColour(juce::TextButton::textColourOnId, juce::Colour(ink));
    analyzeRangeButton.setTooltip("Generate from the selected source range");
    addAndMakeVisible(analyzeRangeButton);

    resetRangeButton.onClick = [this]
    {
        waveformView.setSourceRange(0.0f, 1.0f);
        lastMessage = "Using the full source";
    };
    resetRangeButton.setComponentID("artChip");
    addAndMakeVisible(resetRangeButton);

    previewTransportButton.onClick = [this]
    {
        const auto shouldPlay = !processor.isPreviewPlaying();
        processor.setPreviewPlaying(shouldPlay);
        lastMessage = shouldPlay ? "Preview started" : "Preview stopped";
    };
    previewTransportButton.setComponentID("artTail");
    previewTransportButton.setColour(juce::TextButton::buttonColourId,
                                     juce::Colour(fadedCoral));
    addAndMakeVisible(previewTransportButton);

    originalPreviewButton.setClickingTogglesState(true);
    loopPreviewButton.setClickingTogglesState(true);
    originalPreviewButton.setRadioGroupId(101);
    loopPreviewButton.setRadioGroupId(101);
    originalPreviewButton.onClick = [this]
    {
        processor.setPreviewMode(LoopEngine::PreviewMode::original);
        processor.setPreviewPlaying(true);
        lastMessage = "Auditioning source selection";
    };
    loopPreviewButton.onClick = [this]
    {
        processor.setPreviewMode(LoopEngine::PreviewMode::loop);
        processor.setPreviewPlaying(true);
        lastMessage = "Auditioning generated result";
    };
    loopPreviewButton.setToggleState(true, juce::dontSendNotification);
    originalPreviewButton.setComponentID("artChip");
    loopPreviewButton.setComponentID("artChip");
    addAndMakeVisible(originalPreviewButton);
    addAndMakeVisible(loopPreviewButton);

    candidateBox.setTextWhenNothingSelected("Generated variations");
    candidateBox.setTooltip("Select a generated variation");
    candidateBox.onChange = [this]
    {
        if (candidateBox.getSelectedItemIndex() >= 0)
        {
            processor.selectCandidate(candidateBox.getSelectedItemIndex());
            processor.setPreviewMode(LoopEngine::PreviewMode::loop);
            processor.setPreviewPlaying(true);
            lastMessage = "Auditioning selected variation";
            updatePrimaryAction();
        }
    };
    addAndMakeVisible(candidateBox);

    regenerateButton.onClick = [this]
    {
        if (processor.regenerateTexture(waveformView.getSourceIn(),
                                        waveformView.getSourceOut()))
            lastMessage = "Generating two new variations...";
        else
            lastMessage = "Load a source before creating a new variation";
    };
    regenerateButton.setComponentID("artChip");
    addAndMakeVisible(regenerateButton);

    importButton.onClick = [this] { chooseImportFile(); };
    importButton.setComponentID("artHeader");
    importButton.setColour(juce::TextButton::buttonColourId, juce::Colour(acidYellow));
    addAndMakeVisible(importButton);

    exportButton.onClick = [this] { chooseExportFile(); };
    exportButton.setComponentID("artTail");
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(mint));
    addAndMakeVisible(exportButton);

    dragToDawButton.prepareFile = [this] { return prepareDawDragFile(); };
    dragToDawButton.setComponentID("artTail");
    dragToDawButton.reportStatus = [this] (const juce::String& message)
    {
        lastMessage = message;
    };
    dragToDawButton.setColour(juce::TextButton::buttonColourId, juce::Colour(dustyViolet));
    addAndMakeVisible(dragToDawButton);

    captureButton.onClick = [this]
    {
        processor.setPreviewPlaying(false);
        processor.beginCapture();
        lastMessage = "DAW input capture armed";
    };
    captureButton.setComponentID("artHeader");
    addAndMakeVisible(captureButton);

    clearButton.onClick = [this]
    {
        processor.setPreviewPlaying(false);
        processor.clearLoop();
        lastMessage = "Result cleared - source retained";
        updatePrimaryAction();
    };
    clearButton.setComponentID("artHeader");
    clearButton.setColour(juce::TextButton::buttonColourId, juce::Colour(paper));
    addAndMakeVisible(clearButton);
    clearButton.setVisible(false);

    configureSlider(crossfadeSlider, crossfadeLabel, "SEAM");
    configureSlider(mixSlider, mixLabel, "AUDITION");
    configureSlider(durationSlider, durationLabel, "LENGTH");
    configureSlider(repairDurationSlider, repairDurationLabel, "LENGTH");
    configureSlider(flattenSlider, flattenLabel, "STABILITY");
    configureSlider(dynamicsCrushSlider, dynamicsCrushLabel, "CRUSH");
    configureSlider(sourceMatchSlider, sourceMatchLabel, "TRANSFORM");
    configureSlider(repairLoopStartSlider, repairLoopStartLabel, "LOOP START");
    configureSlider(characterAmountSlider, characterAmountLabel, "EXTRA MIX");
    durationSlider.setMechanism(IllustratedRotaryControl::Mechanism::length);
    repairDurationSlider.setMechanism(IllustratedRotaryControl::Mechanism::length);
    flattenSlider.setMechanism(IllustratedRotaryControl::Mechanism::stability);
    crossfadeSlider.setMechanism(IllustratedRotaryControl::Mechanism::stability);
    dynamicsCrushSlider.setMechanism(IllustratedRotaryControl::Mechanism::crush);
    dynamicsCrushSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    mixSlider.setMechanism(IllustratedRotaryControl::Mechanism::crush);
    sourceMatchSlider.setMechanism(IllustratedRotaryControl::Mechanism::transform);
    repairLoopStartSlider.setMechanism(IllustratedRotaryControl::Mechanism::loopStart);
    joinPositionSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    joinPositionSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    joinPositionSlider.setPopupDisplayEnabled(false, false, this);
    joinPositionSlider.setRange(0.0, 1.0, 0.0001);
    joinPositionSlider.setComponentID("artStrip");
    joinPositionSlider.setName("JOIN POSITION");
    joinPositionSlider.setPathKind(LoopSurgeonUi::StripPathKind::join);
    joinPositionSlider.setTooltip(
        "Move the repaired internal join; the LOOP marker in the waveform follows it");
    joinPositionSlider.onValueChange = [this]
    {
        if (generationModeBox.getSelectedItemIndex() != 0
            || processor.getLoopState() != LoopEngine::State::ready)
            return;
        repairLoopStartSlider.setValue(joinPositionSlider.getValue(),
                                       juce::sendNotificationSync);
    };
    addAndMakeVisible(joinPositionSlider);
    characterAmountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    characterAmountSlider.setComponentID("artStrip");
    characterAmountSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    characterAmountSlider.setPopupDisplayEnabled(false, false, this);
    characterAmountSlider.setPathKind(LoopSurgeonUi::StripPathKind::extra);
    crossfadeSlider.setTooltip(
        "Maximum overlap used to repair the source's original end-to-start seam");
    durationSlider.setTooltip(
        "Exact Texture Loop length in seconds; click the value to type it");
    repairDurationSlider.setTooltip(
        "Exact R&R output length inside Source In/Out; Selection keeps the complete range");
    flattenSlider.setTooltip(
        "Controls how strongly macro dynamics and one-shot movement are stabilised");
    dynamicsCrushSlider.setTooltip(
        "Flattens smaller local ADSR rises and dips after the texture is built; 0% preserves the current result exactly");
    sourceMatchSlider.setTooltip(
        "Transformation depth: increases non-linear source traversal while retaining the selected material's waveform identity");
    repairLoopStartSlider.setTooltip(
        "Coarse R&R join position inside Source In/Out; Join Position below is the fine control for this same boundary");
    repairLoopStartSlider.onValueChange = [this]
    {
        if (generationModeBox.getSelectedItemIndex() != 0
            || processor.getLoopState() != LoopEngine::State::ready)
            return;
        const auto local = static_cast<float>(repairLoopStartSlider.getValue());
        const auto proportion = waveformView.getSourceIn()
            + local * (waveformView.getSourceOut() - waveformView.getSourceIn());
        waveformView.setRotation(proportion);
        joinPositionSlider.setValue(local, juce::dontSendNotification);
        if (processor.setManualRotationPoint(proportion))
            lastMessage = "Loop join moved - audition the repaired boundary";
    };
    characterAmountSlider.setTooltip(
        "Blends the selected optional character after the Natural texture; Off is a complete bypass");
    auto& parameters = processor.getParameterState();
    crossfadeAttachment = std::make_unique<SliderAttachment>(
        parameters, "crossfadeMs", crossfadeSlider);
    mixAttachment = std::make_unique<SliderAttachment>(parameters, "mix", mixSlider);
    durationAttachment = std::make_unique<SliderAttachment>(
        parameters, "textureDuration", durationSlider);
    repairDurationAttachment = std::make_unique<SliderAttachment>(
        parameters, "repairDuration", repairDurationSlider);
    flattenAttachment = std::make_unique<SliderAttachment>(
        parameters, "flatten", flattenSlider);
    dynamicsCrushAttachment = std::make_unique<SliderAttachment>(
        parameters, "textureCrush", dynamicsCrushSlider);
    sourceMatchAttachment = std::make_unique<SliderAttachment>(
        parameters, "sourceMatch", sourceMatchSlider);
    repairLoopStartAttachment = std::make_unique<SliderAttachment>(
        parameters, "repairLoopStart", repairLoopStartSlider);
    characterAmountAttachment = std::make_unique<SliderAttachment>(
        parameters, "textureCharacterAmount", characterAmountSlider);

    modeLabel.setText("MODE", juce::dontSendNotification);
    modeLabel.setFont(lookAndFeel.getDisplayFont(12.0f));
    modeLabel.setColour(juce::Label::textColourId, juce::Colour(paper));
    addAndMakeVisible(modeLabel);
    generationModeBox.addItem("Rotate & Repair", 1);
    generationModeBox.addItem("Texture Loop", 2);
    generationModeBox.onChange = [this]
    {
        updatePrimaryAction();
        applyModeLayout();
        updateArtworkStates();
        repaint();
        if (processor.getSourceName().isNotEmpty())
            lastMessage = "Mode changed - press Generate to apply";
    };
    addAndMakeVisible(generationModeBox);
    modeAttachment = std::make_unique<ComboBoxAttachment>(
        parameters, "generationMode", generationModeBox);

    rotateModeButton.setComponentID("artHeader");
    rotateModeButton.setButtonText("ROTATE & REPAIR");
    rotateModeButton.setTooltip("Rotate & Repair: automate the conventional loop repair workflow");
    rotateModeButton.onClick = [this]
    {
        generationModeBox.setSelectedItemIndex(0, juce::sendNotificationSync);
    };
    addAndMakeVisible(rotateModeButton);
    textureModeButton.setComponentID("artHeader");
    textureModeButton.setButtonText("TEXTURE LOOP");
    textureModeButton.setTooltip("Texture Loop: construct a long evolving loop from selected material");
    textureModeButton.onClick = [this]
    {
        generationModeBox.setSelectedItemIndex(1, juce::sendNotificationSync);
    };
    addAndMakeVisible(textureModeButton);

    textureStructureLabel.setText("MOTION", juce::dontSendNotification);
    textureStructureLabel.setFont(lookAndFeel.getDisplayFont(11.0f));
    textureStructureLabel.setColour(juce::Label::textColourId, juce::Colour(ink));
    addAndMakeVisible(textureStructureLabel);
    textureStructureBox.addItem("Flow", 1);
    textureStructureBox.addItem("Drift", 2);
    textureStructureBox.addItem("Fracture", 3);
    textureStructureBox.setTooltip(
        "Three source-traversal scales; none adds oscillators, spectral delay, pitch shifting or reverse playback");
    textureStructureBox.onChange = [this]
    {
        if (processor.getSourceName().isNotEmpty())
            lastMessage = "Motion changed - Generate to apply";
        updateArtworkStates();
    };
    addAndMakeVisible(textureStructureBox);
    textureStructureAttachment = std::make_unique<ComboBoxAttachment>(
        parameters, "textureStructure", textureStructureBox);

    motionSelectorSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    motionSelectorSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    motionSelectorSlider.setPopupDisplayEnabled(false, false, this);
    motionSelectorSlider.setRange(0.0, 2.0, 1.0);
    motionSelectorSlider.setComponentID("artStrip");
    motionSelectorSlider.setName("MOTION");
    motionSelectorSlider.setPathKind(LoopSurgeonUi::StripPathKind::motion);
    motionSelectorSlider.onValueChange = [this]
    {
        textureStructureBox.setSelectedItemIndex(
            juce::roundToInt(motionSelectorSlider.getValue()),
            juce::sendNotificationSync);
    };
    addAndMakeVisible(motionSelectorSlider);

    // Motion is one three-position illustrated strip. Invisible buttons over
    // its thirds would steal drag gestures, so the slider owns the whole slot.

    characterLabel.setText("EXTRA", juce::dontSendNotification);
    characterLabel.setFont(lookAndFeel.getDisplayFont(11.0f));
    characterLabel.setColour(juce::Label::textColourId, juce::Colour(ink));
    addAndMakeVisible(characterLabel);
    characterBox.addItem("Off", 1);
    characterBox.addItem("Patina", 2);
    characterBox.addItem("Bloom", 3);
    characterBox.addItem("Fray", 4);
    characterBox.setTooltip(
        "Optional post-texture character. Off leaves the Natural result untouched");
    characterBox.onChange = [this]
    {
        characterAmountSlider.setEnabled(characterBox.getSelectedItemIndex() > 0);
        if (processor.getSourceName().isNotEmpty())
            lastMessage = "Extra changed - Generate to apply";
        updateArtworkStates();
    };
    addAndMakeVisible(characterBox);
    characterAttachment = std::make_unique<ComboBoxAttachment>(
        parameters, "textureCharacter", characterBox);

    const auto configureExtraHit = [this] (juce::TextButton& button,
                                           const int textureIndex,
                                           const int repairIndex,
                                           const juce::String& tooltip)
    {
        button.setComponentID("artChoice");
        button.setTooltip(tooltip);
        button.onClick = [this, textureIndex, repairIndex]
        {
            if (generationModeBox.getSelectedItemIndex() == 0)
            {
                if (repairIndex < processor.getCandidateCount())
                {
                    processor.selectCandidate(repairIndex);
                    displayedRepairCandidate = repairIndex;
                }
            }
            else
            {
                const auto nextIndex = characterBox.getSelectedItemIndex() == textureIndex
                    ? 0 : textureIndex;
                characterBox.setSelectedItemIndex(nextIndex,
                                                  juce::sendNotificationSync);
            }
        };
        addAndMakeVisible(button);
    };
    configureExtraHit(extraPatinaButton, 1, 0,
                      "Persistent selection: Patina in Texture Loop; repair candidate A in Rotate & Repair");
    configureExtraHit(extraBloomButton, 2, 1,
                      "Persistent selection: Bloom in Texture Loop; repair candidate B in Rotate & Repair");
    configureExtraHit(extraFrayButton, 3, 2,
                      "Persistent selection: Fray in Texture Loop; repair candidate C in Rotate & Repair");
    extraPatinaButton.setStateLabel("PATINA");
    extraBloomButton.setStateLabel("BLOOM");
    extraFrayButton.setStateLabel("FRAY");

    applyModeLayout();
    const auto directModeSelected = generationModeBox.getSelectedItemIndex() == 0;
    durationLabel.setVisible(!directModeSelected);
    durationSlider.setVisible(!directModeSelected);
    repairDurationLabel.setVisible(directModeSelected);
    repairDurationSlider.setVisible(directModeSelected);
    flattenLabel.setVisible(!directModeSelected);
    flattenSlider.setVisible(!directModeSelected);
    dynamicsCrushLabel.setVisible(!directModeSelected);
    dynamicsCrushSlider.setVisible(!directModeSelected);
    sourceMatchLabel.setVisible(!directModeSelected);
    sourceMatchSlider.setVisible(!directModeSelected);
    repairLoopStartLabel.setVisible(false);
    repairLoopStartSlider.setVisible(directModeSelected);
    textureStructureLabel.setVisible(!directModeSelected);
    textureStructureBox.setVisible(!directModeSelected);
    characterLabel.setVisible(!directModeSelected);
    characterBox.setVisible(!directModeSelected);
    characterAmountLabel.setVisible(!directModeSelected);
    characterAmountSlider.setVisible(!directModeSelected);
    characterAmountSlider.setEnabled(!directModeSelected
        && characterBox.getSelectedItemIndex() > 0);
    crossfadeLabel.setVisible(directModeSelected);
    crossfadeSlider.setVisible(directModeSelected);
    candidateBox.setVisible(false);
    originalPreviewButton.setVisible(false);
    loopPreviewButton.setVisible(false);
    regenerateButton.setVisible(false);
    signalAnalysisView.setVisible(false);

    titleLabel.setVisible(true);
    versionLabel.setVisible(false);
    dropLabel.setVisible(false);
    sourceLabel.setVisible(true);
    modeLabel.setVisible(false);
    generationModeBox.setVisible(false);
    textureStructureLabel.setVisible(false);
    textureStructureBox.setVisible(false);
    characterLabel.setVisible(false);
    characterBox.setVisible(false);
    crossfadeLabel.setVisible(false);
    mixLabel.setVisible(false);
    durationLabel.setVisible(false);
    repairDurationLabel.setVisible(false);
    flattenLabel.setVisible(false);
    dynamicsCrushLabel.setVisible(false);
    sourceMatchLabel.setVisible(false);
    repairLoopStartLabel.setVisible(false);
    characterAmountLabel.setVisible(false);

    setResizable(true, false);
    setResizeLimits(1088, 704, 1632, 1056);
    if (auto* editorConstrainer = getConstrainer())
        editorConstrainer->setFixedAspectRatio(17.0 / 11.0);
    setSize(1360, 880);
    updatePrimaryAction();
    startTimerHz(12);

#if JUCE_DEBUG
    // Developer-only, opt-in visual regression capture. It is inert in normal
    // runs and absent from Release builds. This lets the illustrated UI be
    // reviewed even when the Windows desktop is unavailable to capture tools.
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable(
        "LOOP_SURGEON_UI_SNAPSHOT", {});
    if (snapshotPath.isNotEmpty())
    {
        const auto snapshotMode = juce::SystemStats::getEnvironmentVariable(
            "LOOP_SURGEON_UI_SNAPSHOT_MODE", "texture");
        generationModeBox.setSelectedItemIndex(snapshotMode == "repair" ? 0 : 1,
                                                juce::sendNotificationSync);
        const auto sourcePath = juce::SystemStats::getEnvironmentVariable(
            "LOOP_SURGEON_UI_SNAPSHOT_SOURCE", {});
        if (juce::File(sourcePath).existsAsFile())
            importFile(juce::File(sourcePath));
        if (juce::SystemStats::getEnvironmentVariable(
                "LOOP_SURGEON_UI_SNAPSHOT_EXTREME", "0") == "1")
        {
            durationSlider.setValue(56.0, juce::sendNotificationSync);
            flattenSlider.setValue(0.96, juce::sendNotificationSync);
            dynamicsCrushSlider.setValue(0.92, juce::sendNotificationSync);
            sourceMatchSlider.setValue(0.18, juce::sendNotificationSync);
            motionSelectorSlider.setValue(2.0, juce::sendNotificationSync);
            characterBox.setSelectedItemIndex(2, juce::sendNotificationSync);
            characterAmountSlider.setValue(0.82, juce::sendNotificationSync);
        }
        const auto shouldGenerate = juce::SystemStats::getEnvironmentVariable(
            "LOOP_SURGEON_UI_SNAPSHOT_GENERATE", "0") == "1";
        juce::Component::SafePointer<LoopSurgeonAudioProcessorEditor> safeThis(this);
        if (shouldGenerate && sourcePath.isNotEmpty())
            juce::Timer::callAfterDelay(180, [safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->analyzeRangeButton.triggerClick();
            });
        juce::Timer::callAfterDelay(shouldGenerate ? 6500 : 900,
                                    [safeThis, snapshotPath]
        {
            if (safeThis == nullptr)
                return;
            const auto image = safeThis->createComponentSnapshot(
                safeThis->getLocalBounds(), true, 1.0f);
            const auto outputFile = juce::File(snapshotPath);
            outputFile.deleteFile();
            auto stream = outputFile.createOutputStream();
            if (stream != nullptr)
            {
                juce::PNGImageFormat().writeImageToStream(image, *stream);
                stream->flush();
            }
        });
    }
#endif
}

LoopSurgeonAudioProcessorEditor::~LoopSurgeonAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void LoopSurgeonAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(LoopSurgeonTheme::background));
    const auto scale = static_cast<float>(getWidth()) / LoopSurgeonTheme::width;
    juce::Graphics::ScopedSaveState designSpace(graphics);
    graphics.addTransform(juce::AffineTransform::scale(scale));
    const auto panel = [&] (juce::Rectangle<float> area)
    {
        graphics.setColour(juce::Colour(LoopSurgeonTheme::surface));
        graphics.fillRoundedRectangle(area, 10.0f);
        graphics.setColour(juce::Colour(LoopSurgeonTheme::line).withAlpha(0.65f));
        graphics.drawRoundedRectangle(area.reduced(0.5f), 10.0f, 1.0f);
    };
    panel({40, 124, 840, 300});
    panel({40, 448, 840, 220});
    panel({40, 692, 840, 80});
    panel({904, 124, 416, 648});
    graphics.setColour(juce::Colour(LoopSurgeonTheme::line));
    graphics.drawHorizontalLine(104, 40.0f, 1320.0f);
    graphics.setColour(juce::Colour(LoopSurgeonTheme::secondary));
    graphics.setFont(lookAndFeel.getDisplayFont(14.0f));
    graphics.drawText("SOURCE", juce::Rectangle<float>(64, 144, 200, 28), juce::Justification::centredLeft);
    graphics.drawText("PARAMETERS", juce::Rectangle<float>(64, 466, 200, 24), juce::Justification::centredLeft);
    graphics.drawText("OUTPUT", juce::Rectangle<float>(928, 144, 200, 28), juce::Justification::centredLeft);
    const auto repair = generationModeBox.getSelectedItemIndex() == 0;
    graphics.drawText(repair ? "REPAIR OPTIONS" : "CHARACTER",
        juce::Rectangle<float>(928, 598, 240, 26), juce::Justification::centredLeft);
    if (!repair)
    {
        graphics.setColour(juce::Colour(LoopSurgeonTheme::accent));
        graphics.setFont(lookAndFeel.getHandFont(14.0f));
        graphics.drawText(characterBox.getSelectedItemIndex() > 0 ? characterBox.getText().toUpperCase() : "OFF",
            juce::Rectangle<float>(1174, 598, 122, 26), juce::Justification::centredRight);
    }
}

void LoopSurgeonAudioProcessorEditor::resized()
{
    const auto sx = static_cast<float>(getWidth()) / LoopSurgeonTheme::width;
    const auto sy = static_cast<float>(getHeight()) / LoopSurgeonTheme::height;
    const auto box = [sx, sy] (const float x, const float y,
                              const float width, const float height)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
                                    juce::roundToInt(width * sx),
                                    juce::roundToInt(height * sy));
    };

    titleLabel.setBounds(box(40.0f, 28.0f, 416.0f, 52.0f));
    versionLabel.setBounds({});
    sourceLabel.setBounds(box(64.0f, 181.0f, 792.0f, 26.0f));
    rotateModeButton.setBounds(box(904.0f, 34.0f, 202.0f, 44.0f));
    textureModeButton.setBounds(box(1114.0f, 34.0f, 206.0f, 44.0f));
    importButton.setBounds(box(548.0f, 141.0f, 144.0f, 34.0f));
    captureButton.setBounds(box(704.0f, 141.0f, 152.0f, 34.0f));
    clearButton.setBounds({});
    dropLabel.setBounds({});

    waveformView.setBounds(box(64.0f, 219.0f, 792.0f, 180.0f));
    rangeLabel.setBounds({});
    resetRangeButton.setBounds({});

    modeLabel.setBounds({});
    generationModeBox.setBounds({});
    textureStructureLabel.setBounds({});
    textureStructureBox.setBounds({});
    characterLabel.setBounds({});
    characterBox.setBounds({});
    applyModeLayout();

    statusLabel.setBounds(box(928.0f, 526.0f, 368.0f, 52.0f));
    candidateBox.setBounds(box(68.0f, 327.0f, 180.0f, 30.0f));
    originalPreviewButton.setBounds(box(252.0f, 327.0f, 72.0f, 30.0f));
    loopPreviewButton.setBounds(box(326.0f, 327.0f, 72.0f, 30.0f));
    regenerateButton.setBounds(box(402.0f, 327.0f, 96.0f, 30.0f));
    signalAnalysisView.setBounds(box(64.0f, 219.0f, 792.0f, 180.0f));

    primaryActionArea = box(928.0f, 180.0f, 368.0f, 332.0f);
    analyzeRangeButton.setBounds(primaryActionArea);
    previewTransportButton.setBounds(box(40.0f, 808.0f, 164.0f, 48.0f));
    dragToDawButton.setBounds(box(224.0f, 808.0f, 656.0f, 48.0f));
    exportButton.setBounds(box(904.0f, 808.0f, 416.0f, 48.0f));

    motionFlowButton.setBounds({});
    motionDriftButton.setBounds({});
    motionFractureButton.setBounds({});
    motionSelectorSlider.setBounds(box(64.0f, 705.0f, 792.0f, 54.0f));
    joinPositionSlider.setBounds(box(64.0f, 705.0f, 792.0f, 54.0f));
    extraOffButton.setBounds({});
    extraPatinaButton.setBounds(box(928.0f, 640.0f, 114.0f, 44.0f));
    extraBloomButton.setBounds(box(1055.0f, 640.0f, 114.0f, 44.0f));
    extraFrayButton.setBounds(box(1182.0f, 640.0f, 114.0f, 44.0f));
}

void LoopSurgeonAudioProcessorEditor::applyModeLayout()
{
    const auto repair = generationModeBox.getSelectedItemIndex() == 0;
    for (auto* control : std::array<juce::Slider*, 4> {
             &repairDurationSlider, &crossfadeSlider, &mixSlider, &repairLoopStartSlider })
        control->setVisible(repair);
    for (auto* control : std::array<juce::Slider*, 4> {
             &durationSlider, &flattenSlider, &dynamicsCrushSlider, &sourceMatchSlider })
        control->setVisible(!repair);
    joinPositionSlider.setVisible(repair);
    motionSelectorSlider.setVisible(!repair);
    characterAmountSlider.setVisible(!repair);
    const auto sx = static_cast<float>(getWidth()) / LoopSurgeonTheme::width;
    const auto sy = static_cast<float>(getHeight()) / LoopSurgeonTheme::height;
    const auto box = [sx, sy] (const float x, const float y,
                              const float width, const float height)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
                                    juce::roundToInt(width * sx),
                                    juce::roundToInt(height * sy));
    };
    const auto setKnob = [&] (juce::Label& label, juce::Slider& slider,
                              const float x, const float y,
                              const float width, const float height)
    {
        label.setBounds({});
        slider.setBounds(box(x, y, width, height));
    };

    if (generationModeBox.getSelectedItemIndex() == 0)
    {
        repairDurationSlider.setName("FINAL LENGTH");
        crossfadeSlider.setName("SEAM");
        mixSlider.setName("AUDITION");
        repairLoopStartSlider.setName("LOOP START");
        setKnob(repairDurationLabel, repairDurationSlider,
                64.0f, 504.0f, 184.0f, 150.0f);
        setKnob(crossfadeLabel, crossfadeSlider,
                264.0f, 504.0f, 184.0f, 150.0f);
        setKnob(mixLabel, mixSlider,
                464.0f, 504.0f, 184.0f, 150.0f);
        setKnob(repairLoopStartLabel, repairLoopStartSlider,
                664.0f, 504.0f, 184.0f, 150.0f);
        joinPositionSlider.setVisible(true);
    }
    else
    {
        durationSlider.setName("LENGTH");
        flattenSlider.setName("STABILITY");
        dynamicsCrushSlider.setName("CRUSH");
        sourceMatchSlider.setName("TRANSFORM");
        setKnob(durationLabel, durationSlider,
                64.0f, 504.0f, 184.0f, 150.0f);
        setKnob(flattenLabel, flattenSlider,
                264.0f, 504.0f, 184.0f, 150.0f);
        setKnob(dynamicsCrushLabel, dynamicsCrushSlider,
                464.0f, 504.0f, 184.0f, 150.0f);
        setKnob(sourceMatchLabel, sourceMatchSlider,
                664.0f, 504.0f, 184.0f, 150.0f);
        repairLoopStartSlider.setBounds({});
        characterAmountSlider.setBounds(box(928.0f, 705.0f, 368.0f, 48.0f));
        mixSlider.setBounds({});
        joinPositionSlider.setVisible(false);
    }
}

bool LoopSurgeonAudioProcessorEditor::isInterestedInFileDrag(
    const juce::StringArray& files)
{
    return files.size() == 1
           && juce::File(files[0]).hasFileExtension("wav;aif;aiff;flac;ogg");
}

void LoopSurgeonAudioProcessorEditor::filesDropped(
    const juce::StringArray& files, int, int)
{
    if (files.size() == 1)
        importFile(juce::File(files[0]));
}

void LoopSurgeonAudioProcessorEditor::importFile(const juce::File& file)
{
    processor.setPreviewPlaying(false);
    lastMessage = processor.importAudioFile(file);
    if (lastMessage.isEmpty())
        lastMessage = "Source loaded";
    updatePrimaryAction();
}

void LoopSurgeonAudioProcessorEditor::chooseImportFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Choose source audio", juce::File {}, "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [this] (const juce::FileChooser& chooser)
                             {
                                 const auto file = chooser.getResult();
                                 if (file.existsAsFile())
                                     importFile(file);
                             });
}

void LoopSurgeonAudioProcessorEditor::chooseExportFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Export generated audio",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("loopy.wav"),
        "*.wav");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this] (const juce::FileChooser& chooser)
                             {
                                 const auto file = chooser.getResult();
                                 if (file != juce::File {})
                                 {
                                     lastMessage = processor.exportLoopFile(file);
                                     if (lastMessage.isEmpty())
                                         lastMessage = "24-bit WAV exported";
                                 }
                             });
}

juce::File LoopSurgeonAudioProcessorEditor::prepareDawDragFile()
{
    auto renderDirectory = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory)
        .getChildFile("Sound VST Project")
        .getChildFile("loopy")
        .getChildFile("DAW Renders");
    if (renderDirectory.createDirectory().failed())
        return {};

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto file = renderDirectory.getNonexistentChildFile(
        "loopy-" + stamp, ".wav", false);
    const auto error = processor.exportLoopFile(file);
    if (error.isNotEmpty())
    {
        lastMessage = error;
        return {};
    }
    return file;
}

void LoopSurgeonAudioProcessorEditor::timerCallback()
{
    const auto source = processor.getSourceName();
    const auto sourceReady = source.isNotEmpty();
    if (displayedSourceReady != sourceReady)
    {
        displayedSourceReady = sourceReady;
        repaint();
    }
    sourceLabel.setText(source.isEmpty() ? "No source loaded" : source,
                        juce::dontSendNotification);
    const auto candidateCount = processor.getCandidateCount();
    const auto candidateRevision = processor.getCandidateRevision();
    const auto sourceRevision = processor.getSourceRevision();
    const auto sourceChanged = source != displayedSource
                               || sourceRevision != displayedSourceRevision;
    if (sourceChanged)
    {
        displayedSource = source;
        displayedSourceRevision = sourceRevision;
        waveformView.setWaveform(processor.getWaveformPreview());
        waveformView.setSourceRange(
            processor.getAnalysisRangeStartProportion(),
            processor.getAnalysisRangeEndProportion());
        waveformView.setRotation(-1.0f);
    }
    if (sourceChanged || candidateCount != displayedCandidateCount
        || candidateRevision != displayedCandidateRevision)
    {
        displayedCandidateCount = candidateCount;
        displayedCandidateRevision = candidateRevision;
        candidateBox.clear(juce::dontSendNotification);
        for (int index = 0; index < candidateCount; ++index)
            candidateBox.addItem(processor.getCandidateDescription(index), index + 1);
        if (candidateCount > 0)
            candidateBox.setSelectedItemIndex(0, juce::dontSendNotification);
    }

    updateRangeLabel();
    updatePrimaryAction();
    waveformView.setDurationSeconds(processor.getSourceDurationSeconds());

    const auto state = processor.getLoopState();
    const auto ready = state == LoopEngine::State::ready;
    if (displayedResultReady != ready)
    {
        displayedResultReady = ready;
        repaint();
    }
    const auto textureResult = ready
        && processor.getLastUsedGenerationMode()
               == LoopEngine::GenerationMode::textureLoop;
    const auto selectedTextureMode = generationModeBox.getSelectedItemIndex() == 1;
    candidateBox.setTextWhenNothingSelected(selectedTextureMode
        ? "Texture variations" : "Repair options");
    if (ready && !textureResult && !waveformView.isEditingRotation())
    {
        const auto rotation = processor.getRotationProportion();
        waveformView.setRotation(rotation);
        const auto range = waveformView.getSourceOut() - waveformView.getSourceIn();
        const auto local = range > 0.0001f
            ? juce::jlimit(0.0f, 1.0f,
                (rotation - waveformView.getSourceIn()) / range)
            : 0.5f;
        joinPositionSlider.setValue(local, juce::dontSendNotification);
        repairLoopStartSlider.setValue(local, juce::dontSendNotification);
    }
    else if (!ready || textureResult)
        waveformView.setRotation(-1.0f);
    signalAnalysisView.setSnapshot(ready ? processor.getSignalSnapshot()
                                         : SignalDiagnostics::SignalSnapshot {});
    exportButton.setEnabled(ready);
    dragToDawButton.setEnabled(ready);
    candidateBox.setEnabled(ready);
    previewTransportButton.setEnabled(ready);
    originalPreviewButton.setEnabled(ready);
    loopPreviewButton.setEnabled(ready);
    analyzeRangeButton.setEnabled(state != LoopEngine::State::armed
                                  && state != LoopEngine::State::capturing
                                  && state != LoopEngine::State::analysing);
    analyzeRangeButton.setWorking(state == LoopEngine::State::analysing);
    resetRangeButton.setEnabled(source.isNotEmpty());
    regenerateButton.setEnabled(ready
        && processor.getLastUsedGenerationMode()
               == LoopEngine::GenerationMode::textureLoop);
    candidateBox.setVisible(false);
    originalPreviewButton.setVisible(false);
    loopPreviewButton.setVisible(false);
    regenerateButton.setVisible(false);
    signalAnalysisView.setVisible(false);
    waveformView.setVisible(true);
    rangeLabel.setVisible(false);
    resetRangeButton.setVisible(false);
    statusLabel.setVisible(true);
    previewTransportButton.setVisible(true);
    dragToDawButton.setVisible(true);
    exportButton.setVisible(true);
    clearButton.setEnabled(source.isNotEmpty() || ready);

    const auto directModeSelected = generationModeBox.getSelectedItemIndex() == 0;
    durationLabel.setVisible(false);
    durationSlider.setVisible(!directModeSelected);
    repairDurationLabel.setVisible(false);
    repairDurationSlider.setVisible(directModeSelected);
    flattenLabel.setVisible(false);
    flattenSlider.setVisible(!directModeSelected);
    dynamicsCrushLabel.setVisible(false);
    dynamicsCrushSlider.setVisible(!directModeSelected);
    sourceMatchLabel.setVisible(false);
    sourceMatchSlider.setVisible(!directModeSelected);
    repairLoopStartSlider.setVisible(directModeSelected);
    repairLoopStartSlider.setEnabled(directModeSelected && ready);
    textureStructureLabel.setVisible(false);
    textureStructureBox.setVisible(false);
    characterLabel.setVisible(false);
    characterBox.setVisible(false);
    characterAmountLabel.setVisible(false);
    characterAmountSlider.setVisible(!directModeSelected);
    characterAmountSlider.setEnabled(characterBox.getSelectedItemIndex() > 0);
    crossfadeLabel.setVisible(false);
    crossfadeSlider.setVisible(directModeSelected);
    mixLabel.setVisible(false);
    mixSlider.setVisible(directModeSelected);
    joinPositionSlider.setVisible(directModeSelected);
    joinPositionSlider.setEnabled(directModeSelected && ready);
    motionSelectorSlider.setVisible(!directModeSelected);
    motionFlowButton.setVisible(false);
    motionDriftButton.setVisible(false);
    motionFractureButton.setVisible(false);
    extraPatinaButton.setVisible(true);
    extraBloomButton.setVisible(true);
    extraFrayButton.setVisible(true);
    extraPatinaButton.setEnabled(!directModeSelected || candidateCount > 0);
    extraBloomButton.setEnabled(!directModeSelected || candidateCount > 1);
    extraFrayButton.setEnabled(!directModeSelected || candidateCount > 2);

    const auto previewMode = processor.getPreviewMode();
    originalPreviewButton.setToggleState(previewMode == LoopEngine::PreviewMode::original,
                                         juce::dontSendNotification);
    loopPreviewButton.setToggleState(previewMode == LoopEngine::PreviewMode::loop,
                                     juce::dontSendNotification);
    const auto previewing = processor.isPreviewPlaying();
    previewTransportButton.setButtonText(previewing ? "STOP" : "PREVIEW");
    previewTransportButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour(previewing ? fadedCoral : acidYellow));
    updateArtworkStates();

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (lastMessage.isNotEmpty())
    {
        transientStatusMessage = lastMessage;
        transientStatusExpiresAtMs = nowMs + 2400.0;
        lastMessage.clear();
    }

    const auto stateOwnsStatus = state == LoopEngine::State::armed
                                 || state == LoopEngine::State::capturing
                                 || state == LoopEngine::State::analysing;
    const auto showTransient = !stateOwnsStatus
                               && transientStatusMessage.isNotEmpty()
                               && nowMs < transientStatusExpiresAtMs;
    juce::String statusText;
    if (showTransient)
    {
        statusText = transientStatusMessage;
    }
    else
    {
        if (nowMs >= transientStatusExpiresAtMs)
            transientStatusMessage.clear();
        switch (state)
        {
            case LoopEngine::State::empty:
                statusText = "No source";
                break;
            case LoopEngine::State::sourceReady:
                statusText = "Source ready - set range and mode, then Generate";
                break;
            case LoopEngine::State::armed:
                statusText = "Waiting for DAW capture";
                break;
            case LoopEngine::State::capturing:
                statusText = "Capturing " + juce::String(
                    juce::roundToInt(processor.getCaptureProgress() * 100.0f)) + "%";
                break;
            case LoopEngine::State::analysing:
                statusText = juce::String("Generating  ")
                    + juce::String(juce::roundToInt(
                        processor.getAnalysisProgress() * 100.0f)) + "%";
                break;
            case LoopEngine::State::ready:
                statusText = textureResult
                    ? "Texture Loop ready - audition Generated"
                    : "Rotate & Repair ready - audition the join";
                break;
            case LoopEngine::State::failed:
                statusText = "No reliable loop found - widen the blue range or try different material";
                break;
        }
    }
    statusLabel.setColour(juce::Label::textColourId,
        juce::Colour(state == LoopEngine::State::failed ? fadedCoral : ink));
    statusLabel.setText(statusText, juce::dontSendNotification);
    repaint();
}

void LoopSurgeonAudioProcessorEditor::updateRangeLabel()
{
    const auto duration = processor.getSourceDurationSeconds();
    const auto start = duration * waveformView.getSourceIn();
    const auto end = duration * waveformView.getSourceOut();
    const auto repairedOutput = processor.getLoopState() == LoopEngine::State::ready
        && processor.getLastUsedGenerationMode() == LoopEngine::GenerationMode::rotateRepair
        ? juce::String("     OUTPUT  ")
              + juce::String(processor.getRenderedDurationSeconds(), 2) + " s"
        : juce::String {};
    rangeLabel.setText(
        "IN  " + juce::String(start, 2) + " s     OUT  " + juce::String(end, 2)
            + " s     SELECTED  " + juce::String(juce::jmax(0.0, end - start), 2) + " s"
            + repairedOutput,
        juce::dontSendNotification);
}

void LoopSurgeonAudioProcessorEditor::updatePrimaryAction()
{
    const auto state = processor.getLoopState();
    if (state == LoopEngine::State::analysing)
    {
        analyzeRangeButton.setButtonText(
            juce::String(juce::roundToInt(
                processor.getAnalysisProgress() * 100.0f)) + "%");
        return;
    }
    if (processor.getSourceName().isEmpty())
    {
        analyzeRangeButton.setButtonText("LOAD AUDIO");
        return;
    }
    analyzeRangeButton.setButtonText("GENERATE");
}

void LoopSurgeonAudioProcessorEditor::updateArtworkStates()
{
    const auto repairMode = generationModeBox.getSelectedItemIndex() == 0;
    rotateModeButton.setToggleState(repairMode, juce::dontSendNotification);
    textureModeButton.setToggleState(!repairMode, juce::dontSendNotification);

    const auto motion = textureStructureBox.getSelectedItemIndex();
    motionSelectorSlider.setValue(motion, juce::dontSendNotification);
    motionFlowButton.setToggleState(!repairMode && motion == 0,
                                    juce::dontSendNotification);
    motionDriftButton.setToggleState(!repairMode && motion == 1,
                                     juce::dontSendNotification);
    motionFractureButton.setToggleState(!repairMode && motion == 2,
                                        juce::dontSendNotification);

    const auto character = characterBox.getSelectedItemIndex();
    extraPatinaButton.setToggleState(!repairMode && character == 1,
                                     juce::dontSendNotification);
    extraBloomButton.setToggleState(!repairMode && character == 2,
                                    juce::dontSendNotification);
    extraFrayButton.setToggleState(!repairMode && character == 3,
                                   juce::dontSendNotification);
    if (repairMode)
    {
        extraPatinaButton.setStateLabel("A");
        extraBloomButton.setStateLabel("B");
        extraFrayButton.setStateLabel("C");
        extraPatinaButton.setToggleState(processor.getCandidateCount() > 0 && displayedRepairCandidate == 0,
                                         juce::dontSendNotification);
        extraBloomButton.setToggleState(processor.getCandidateCount() > 1 && displayedRepairCandidate == 1,
                                        juce::dontSendNotification);
        extraFrayButton.setToggleState(processor.getCandidateCount() > 2 && displayedRepairCandidate == 2,
                                         juce::dontSendNotification);
    }
    else
    {
        extraPatinaButton.setStateLabel("PATINA");
        extraBloomButton.setStateLabel("BLOOM");
        extraFrayButton.setStateLabel("FRAY");
    }
}

void LoopSurgeonAudioProcessorEditor::configureSlider(
    juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setName(text);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.20f,
                               juce::MathConstants<float>::pi * 2.80f, true);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled(false, false, this);
    slider.setWantsKeyboardFocus(false);
    slider.setComponentID("artKnob");
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(ink));
    slider.setColour(juce::Slider::textBoxBackgroundColourId,
                     juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId,
                     juce::Colours::transparentBlack);
    addAndMakeVisible(slider);
    label.setText(text, juce::dontSendNotification);
    label.setFont(lookAndFeel.getDisplayFont(10.8f));
    label.setColour(juce::Label::textColourId, juce::Colour(ink));
    addAndMakeVisible(label);
}
