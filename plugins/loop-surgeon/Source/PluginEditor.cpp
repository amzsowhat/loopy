#include "PluginEditor.h"

#include <utility>

namespace
{
constexpr auto ink = 0xff101014;
constexpr auto softInk = 0xffa8a3a0;
constexpr auto paper = 0xfff5f0de;
constexpr auto acidYellow = 0xfff0ee63;
constexpr auto fadedCoral = 0xffff625f;
constexpr auto dustyViolet = 0xff8367e8;
constexpr auto mint = 0xff57e0b5;
constexpr auto charcoal = 0xff222128;
}

LoopSurgeonAudioProcessorEditor::LoopSurgeonAudioProcessorEditor(
    LoopSurgeonAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("LOOP SURGEON", juce::dontSendNotification);
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

    sourceLabel.setColour(juce::Label::textColourId, juce::Colour(softInk));
    sourceLabel.setFont(lookAndFeel.getHandFont(13.0f));
    sourceLabel.setJustificationType(juce::Justification::centred);
    sourceLabel.setMinimumHorizontalScale(0.7f);
    sourceLabel.setText("NO SOURCE", juce::dontSendNotification);
    addAndMakeVisible(sourceLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(ink));
    statusLabel.setFont(lookAndFeel.getHandFont(13.5f));
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
            lastMessage = "Loop start updated - audition the repaired join";
        else
            lastMessage = "Loop start must remain inside the blue source range";
    };
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
    analyzeRangeButton.setComponentID("primaryAction");
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
    resetRangeButton.setComponentID("textAction");
    addAndMakeVisible(resetRangeButton);

    previewTransportButton.onClick = [this]
    {
        const auto shouldPlay = !processor.isPreviewPlaying();
        processor.setPreviewPlaying(shouldPlay);
        lastMessage = shouldPlay ? "Preview started" : "Preview stopped";
    };
    previewTransportButton.setComponentID("tailAction");
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
    originalPreviewButton.setComponentID("segmented");
    loopPreviewButton.setComponentID("segmented");
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
    regenerateButton.setComponentID("textAction");
    addAndMakeVisible(regenerateButton);

    importButton.onClick = [this] { chooseImportFile(); };
    importButton.setComponentID("headerAction");
    importButton.setColour(juce::TextButton::buttonColourId, juce::Colour(acidYellow));
    addAndMakeVisible(importButton);

    exportButton.onClick = [this] { chooseExportFile(); };
    exportButton.setComponentID("tailAction");
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(mint));
    addAndMakeVisible(exportButton);

    dragToDawButton.prepareFile = [this] { return prepareDawDragFile(); };
    dragToDawButton.setComponentID("tailAction");
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
    captureButton.setComponentID("headerAction");
    addAndMakeVisible(captureButton);

    clearButton.onClick = [this]
    {
        processor.setPreviewPlaying(false);
        processor.clearLoop();
        lastMessage = "Result cleared - source retained";
        updatePrimaryAction();
    };
    clearButton.setComponentID("headerAction");
    clearButton.setColour(juce::TextButton::buttonColourId, juce::Colour(paper));
    addAndMakeVisible(clearButton);

    configureSlider(crossfadeSlider, crossfadeLabel, "SEAM");
    configureSlider(mixSlider, mixLabel, "AUDITION");
    configureSlider(durationSlider, durationLabel, "LENGTH");
    configureSlider(repairDurationSlider, repairDurationLabel, "LENGTH");
    configureSlider(flattenSlider, flattenLabel, "STABILITY");
    configureSlider(dynamicsCrushSlider, dynamicsCrushLabel, "CRUSH");
    configureSlider(sourceMatchSlider, sourceMatchLabel, "TRANSFORM");
    configureSlider(characterAmountSlider, characterAmountLabel, "EXTRA MIX");
    mixSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
    characterAmountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    characterAmountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
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
        if (processor.getSourceName().isNotEmpty())
            lastMessage = "Mode changed - press Generate to apply";
    };
    addAndMakeVisible(generationModeBox);
    modeAttachment = std::make_unique<ComboBoxAttachment>(
        parameters, "generationMode", generationModeBox);

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
    };
    addAndMakeVisible(textureStructureBox);
    textureStructureAttachment = std::make_unique<ComboBoxAttachment>(
        parameters, "textureStructure", textureStructureBox);

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
    };
    addAndMakeVisible(characterBox);
    characterAttachment = std::make_unique<ComboBoxAttachment>(
        parameters, "textureCharacter", characterBox);

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

    setResizable(true, true);
    setResizeLimits(1050, 630, 1536, 922);
    if (auto* editorConstrainer = getConstrainer())
        editorConstrainer->setFixedAspectRatio(5.0 / 3.0);
    setSize(1200, 720);
    updatePrimaryAction();
    startTimerHz(12);
}

LoopSurgeonAudioProcessorEditor::~LoopSurgeonAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void LoopSurgeonAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(ink));
    const auto sx = static_cast<float>(getWidth()) / 1200.0f;
    const auto sy = static_cast<float>(getHeight()) / 720.0f;
    const auto box = [sx, sy] (const float x, const float y,
                               const float width, const float height)
    {
        return juce::Rectangle<float>(x * sx, y * sy,
                                      width * sx, height * sy);
    };

    graphics.setColour(juce::Colour(0xff18171d));
    graphics.fillRect(box(0.0f, 0.0f, 1200.0f, 92.0f));
    graphics.setColour(juce::Colour(acidYellow));
    graphics.fillRect(box(0.0f, 92.0f, 1200.0f, 4.0f));

    graphics.setColour(juce::Colour(0xfff0ee63));
    graphics.fillRect(box(0.0f, 96.0f, 1200.0f, 604.0f));
    graphics.setColour(juce::Colour(0xff57e0b5));
    graphics.fillRect(box(0.0f, 96.0f, 18.0f, 604.0f));
    graphics.setColour(juce::Colour(0xffff625f));
    graphics.fillRect(box(18.0f, 96.0f, 8.0f, 604.0f));

    const auto& field = lookAndFeel.getFieldImage();
    if (field.isValid())
    {
        juce::Graphics::ScopedSaveState state(graphics);
        graphics.reduceClipRegion(box(26.0f, 96.0f, 1174.0f, 604.0f).toNearestInt());
        graphics.setOpacity(displayedResultReady ? 0.96f
                            : displayedSourceReady ? 0.88f : 0.78f);
        graphics.drawImage(field, box(42.0f, 108.0f, 1116.0f, 570.0f),
                           juce::RectanglePlacement::centred);
        graphics.setOpacity(1.0f);
    }

    const auto nodeGlow = [&] (const juce::Point<float> centre,
                               const bool active, const juce::Colour colour)
    {
        if (!active)
            return;
        graphics.setColour(colour.withAlpha(0.16f));
        graphics.fillEllipse(juce::Rectangle<float>(68.0f * sx, 68.0f * sy)
                                 .withCentre(centre));
        graphics.setColour(colour.withAlpha(0.76f));
        graphics.drawEllipse(juce::Rectangle<float>(58.0f * sx, 58.0f * sy)
                                 .withCentre(centre), 2.0f * sx);
    };
    nodeGlow(box(384.0f, 292.0f, 1.0f, 1.0f).getCentre(),
             displayedSourceReady, juce::Colour(mint));
    nodeGlow(box(956.0f, 348.0f, 1.0f, 1.0f).getCentre(),
             displayedSourceReady, juce::Colour(fadedCoral));
    nodeGlow(box(758.0f, 582.0f, 1.0f, 1.0f).getCentre(),
             displayedResultReady, juce::Colour(dustyViolet));

    graphics.setColour(juce::Colour(0xff18171d));
    graphics.fillRect(box(0.0f, 700.0f, 1200.0f, 20.0f));
}

void LoopSurgeonAudioProcessorEditor::resized()
{
    const auto sx = static_cast<float>(getWidth()) / 1200.0f;
    const auto sy = static_cast<float>(getHeight()) / 720.0f;
    const auto box = [sx, sy] (const float x, const float y,
                              const float width, const float height)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
                                    juce::roundToInt(width * sx),
                                    juce::roundToInt(height * sy));
    };

    titleLabel.setBounds(box(46.0f, 13.0f, 350.0f, 45.0f));
    versionLabel.setBounds(box(46.0f, 701.0f, 280.0f, 17.0f));
    sourceLabel.setBounds(box(292.0f, 22.0f, 226.0f, 43.0f));
    importButton.setBounds(box(732.0f, 26.0f, 112.0f, 34.0f));
    captureButton.setBounds(box(858.0f, 26.0f, 140.0f, 34.0f));
    clearButton.setBounds(box(1012.0f, 26.0f, 104.0f, 34.0f));
    dropLabel.setBounds({});

    waveformView.setBounds(box(140.0f, 190.0f, 374.0f, 142.0f));
    rangeLabel.setBounds(box(148.0f, 334.0f, 274.0f, 24.0f));
    resetRangeButton.setBounds(box(420.0f, 334.0f, 94.0f, 24.0f));

    modeLabel.setBounds(box(536.0f, 15.0f, 54.0f, 16.0f));
    generationModeBox.setBounds(box(536.0f, 32.0f, 184.0f, 34.0f));
    textureStructureLabel.setBounds(box(620.0f, 192.0f, 116.0f, 16.0f));
    textureStructureBox.setBounds(box(620.0f, 208.0f, 116.0f, 32.0f));
    characterLabel.setBounds(box(650.0f, 250.0f, 116.0f, 16.0f));
    characterBox.setBounds(box(650.0f, 266.0f, 116.0f, 32.0f));
    applyModeLayout();

    statusLabel.setBounds(box(88.0f, 430.0f, 604.0f, 26.0f));
    candidateBox.setBounds(box(140.0f, 344.0f, 180.0f, 30.0f));
    originalPreviewButton.setBounds(box(322.0f, 344.0f, 64.0f, 30.0f));
    loopPreviewButton.setBounds(box(386.0f, 344.0f, 64.0f, 30.0f));
    regenerateButton.setBounds(box(450.0f, 344.0f, 94.0f, 30.0f));
    signalAnalysisView.setBounds(box(140.0f, 190.0f, 374.0f, 142.0f));

    primaryActionArea = box(526.0f, 438.0f, 176.0f, 176.0f);
    analyzeRangeButton.setBounds(primaryActionArea);
    previewTransportButton.setBounds(box(698.0f, 626.0f, 70.0f, 42.0f));
    dragToDawButton.setBounds(box(782.0f, 610.0f, 76.0f, 42.0f));
    exportButton.setBounds(box(846.0f, 586.0f, 70.0f, 42.0f));
}

void LoopSurgeonAudioProcessorEditor::applyModeLayout()
{
    const auto sx = static_cast<float>(getWidth()) / 1200.0f;
    const auto sy = static_cast<float>(getHeight()) / 720.0f;
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
        label.setBounds(box(x, y, width, 18.0f));
        label.setJustificationType(juce::Justification::centred);
        slider.setBounds(box(x, y + 15.0f, width, height));
    };
    const auto setStrip = [&] (juce::Label& label, juce::Slider& slider,
                               const float y)
    {
        label.setBounds(box(768.0f, y, 88.0f, 30.0f));
        label.setJustificationType(juce::Justification::centredLeft);
        slider.setBounds(box(856.0f, y, 250.0f, 30.0f));
    };

    if (generationModeBox.getSelectedItemIndex() == 0)
    {
        setKnob(repairDurationLabel, repairDurationSlider,
                775.0f, 130.0f, 110.0f, 112.0f);
        setKnob(crossfadeLabel, crossfadeSlider,
                900.0f, 150.0f, 110.0f, 112.0f);
        setStrip(mixLabel, mixSlider, 416.0f);
    }
    else
    {
        setKnob(durationLabel, durationSlider,
                775.0f, 130.0f, 110.0f, 100.0f);
        setKnob(flattenLabel, flattenSlider,
                900.0f, 150.0f, 110.0f, 100.0f);
        setKnob(dynamicsCrushLabel, dynamicsCrushSlider,
                775.0f, 250.0f, 110.0f, 100.0f);
        setKnob(sourceMatchLabel, sourceMatchSlider,
                900.0f, 274.0f, 110.0f, 100.0f);
        setStrip(characterAmountLabel, characterAmountSlider, 416.0f);
        setStrip(mixLabel, mixSlider, 448.0f);
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
            .getChildFile("Loop Surgeon.wav"),
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
        .getChildFile("Loop Surgeon")
        .getChildFile("DAW Renders");
    if (renderDirectory.createDirectory().failed())
        return {};

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto file = renderDirectory.getNonexistentChildFile(
        "Loop-Surgeon-" + stamp, ".wav", false);
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
        waveformView.setRotation(processor.getRotationProportion());
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
    resetRangeButton.setEnabled(source.isNotEmpty());
    regenerateButton.setEnabled(ready
        && processor.getLastUsedGenerationMode()
               == LoopEngine::GenerationMode::textureLoop);
    candidateBox.setVisible(ready);
    originalPreviewButton.setVisible(ready);
    loopPreviewButton.setVisible(ready);
    regenerateButton.setVisible(ready && selectedTextureMode);
    signalAnalysisView.setVisible(ready);
    waveformView.setVisible(!ready);
    rangeLabel.setVisible(!ready);
    resetRangeButton.setVisible(!ready);
    previewTransportButton.setVisible(ready);
    dragToDawButton.setVisible(ready);
    exportButton.setVisible(ready);
    clearButton.setEnabled(source.isNotEmpty() || ready);

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
    textureStructureLabel.setVisible(!directModeSelected);
    textureStructureBox.setVisible(!directModeSelected);
    characterLabel.setVisible(!directModeSelected);
    characterBox.setVisible(!directModeSelected);
    characterAmountLabel.setVisible(!directModeSelected);
    characterAmountSlider.setVisible(!directModeSelected);
    characterAmountSlider.setEnabled(characterBox.getSelectedItemIndex() > 0);
    crossfadeLabel.setVisible(directModeSelected);
    crossfadeSlider.setVisible(directModeSelected);

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

void LoopSurgeonAudioProcessorEditor::configureSlider(
    juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.20f,
                               juce::MathConstants<float>::pi * 2.80f, true);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 19);
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

