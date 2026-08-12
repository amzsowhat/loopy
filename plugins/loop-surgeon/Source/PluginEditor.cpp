#include "PluginEditor.h"

#include <utility>

namespace
{
constexpr auto background = 0xff0b0d10;
constexpr auto panel = 0xff15191e;
constexpr auto panelRaised = 0xff1d232a;
constexpr auto border = 0xff343b45;
constexpr auto textPrimary = 0xfff1eee7;
constexpr auto textMuted = 0xffa6adb7;
constexpr auto searchBlue = 0xff7f9fc9;
constexpr auto loopGreen = 0xff82b5a5;
constexpr auto warningAmber = 0xffd79a52;
constexpr auto primaryAmber = 0xffd98c3f;
}

LoopSurgeonAudioProcessorEditor::LoopSurgeonAudioProcessorEditor(
    LoopSurgeonAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("LOOP SURGEON", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    addAndMakeVisible(titleLabel);

    versionLabel.setText("LOCAL TEST BUILD",
                         juce::dontSendNotification);
    versionLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setColour(juce::Label::textColourId, juce::Colour(loopGreen));
    addAndMakeVisible(versionLabel);

    dropLabel.setText({}, juce::dontSendNotification);
    dropLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    dropLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    addAndMakeVisible(dropLabel);

    sourceLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    sourceLabel.setFont(juce::FontOptions(14.0f));
    sourceLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(sourceLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(loopGreen));
    statusLabel.setFont(juce::FontOptions(14.5f, juce::Font::bold));
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
    rangeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c3cf));
    rangeLabel.setFont(juce::FontOptions(13.0f));
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
    analyzeRangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(primaryAmber));
    analyzeRangeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(primaryAmber));
    analyzeRangeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff17120d));
    analyzeRangeButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17120d));
    analyzeRangeButton.setTooltip("Generate from the selected source range");
    addAndMakeVisible(analyzeRangeButton);

    resetRangeButton.onClick = [this]
    {
        waveformView.setSourceRange(0.0f, 1.0f);
        lastMessage = "Using the full source";
    };
    addAndMakeVisible(resetRangeButton);

    previewTransportButton.onClick = [this]
    {
        const auto shouldPlay = !processor.isPreviewPlaying();
        processor.setPreviewPlaying(shouldPlay);
        lastMessage = shouldPlay ? "Preview started" : "Preview stopped";
    };
    previewTransportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff426f63));
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
    addAndMakeVisible(regenerateButton);

    importButton.onClick = [this] { chooseImportFile(); };
    importButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff435d78));
    addAndMakeVisible(importButton);

    exportButton.onClick = [this] { chooseExportFile(); };
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff426f63));
    addAndMakeVisible(exportButton);

    dragToDawButton.prepareFile = [this] { return prepareDawDragFile(); };
    dragToDawButton.reportStatus = [this] (const juce::String& message)
    {
        lastMessage = message;
    };
    dragToDawButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff435d78));
    addAndMakeVisible(dragToDawButton);

    captureButton.onClick = [this]
    {
        processor.setPreviewPlaying(false);
        processor.beginCapture();
        lastMessage = "DAW input capture armed";
    };
    addAndMakeVisible(captureButton);

    clearButton.onClick = [this]
    {
        processor.setPreviewPlaying(false);
        processor.clearLoop();
        lastMessage = "Result cleared - source retained";
        updatePrimaryAction();
    };
    clearButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3b4652));
    addAndMakeVisible(clearButton);

    configureSlider(crossfadeSlider, crossfadeLabel, "SEAM REPAIR");
    configureSlider(mixSlider, mixLabel, "AUDITION MIX");
    configureSlider(durationSlider, durationLabel, "OUTPUT LENGTH");
    configureSlider(repairDurationSlider, repairDurationLabel, "FINAL LENGTH");
    configureSlider(flattenSlider, flattenLabel, "STABILITY");
    configureSlider(dynamicsCrushSlider, dynamicsCrushLabel, "CRUSH");
    configureSlider(sourceMatchSlider, sourceMatchLabel, "TRANSFORM");
    configureSlider(characterAmountSlider, characterAmountLabel, "EXTRA MIX");
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
    modeLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    modeLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(modeLabel);
    generationModeBox.addItem("Rotate & Repair", 1);
    generationModeBox.addItem("Texture Loop", 2);
    generationModeBox.onChange = [this]
    {
        updatePrimaryAction();
        if (processor.getSourceName().isNotEmpty())
            lastMessage = "Mode changed - press Generate to apply";
    };
    addAndMakeVisible(generationModeBox);
    modeAttachment = std::make_unique<ComboBoxAttachment>(
        parameters, "generationMode", generationModeBox);

    textureStructureLabel.setText("MOTION", juce::dontSendNotification);
    textureStructureLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    textureStructureLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
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
    characterLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    characterLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
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

    setResizable(true, true);
    setResizeLimits(1040, 900, 1440, 1120);
    setSize(1120, 960);
    updatePrimaryAction();
    startTimerHz(12);
}

LoopSurgeonAudioProcessorEditor::~LoopSurgeonAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void LoopSurgeonAudioProcessorEditor::drawCard(
    juce::Graphics& graphics, const juce::Rectangle<int> card,
    const juce::String& title, const juce::Colour accent) const
{
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(card.toFloat(), 10.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(card.toFloat().reduced(0.5f), 10.0f, 1.0f);

    auto heading = card.reduced(14).removeFromTop(23);
    graphics.setColour(juce::Colour(textPrimary));
    graphics.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    graphics.drawText(title, heading, juce::Justification::centredLeft);
    graphics.setColour(accent);
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(static_cast<float>(card.getX() + 14),
                               static_cast<float>(card.getY() + 34), 44.0f, 2.0f),
        1.0f);
}

void LoopSurgeonAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    juce::ColourGradient gradient(juce::Colour(0xff171a1f), 0.0f, 0.0f,
                                  juce::Colour(background), 0.0f,
                                  static_cast<float>(getHeight()), false);
    graphics.setGradientFill(gradient);
    graphics.fillAll();

    drawCard(graphics, sourceCard, "SOURCE", juce::Colour(searchBlue));
    drawCard(graphics, waveformCard, "SELECT RANGE", juce::Colour(searchBlue));
    drawCard(graphics, finishCard, "SHAPE", juce::Colour(warningAmber));
    drawCard(graphics, auditionCard, "RESULT", juce::Colour(loopGreen));

    graphics.setColour(juce::Colour(border));
    graphics.drawHorizontalLine(footerArea.getY(), static_cast<float>(footerArea.getX()),
                                static_cast<float>(footerArea.getRight()));
}

void LoopSurgeonAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    auto header = area.removeFromTop(48);
    titleLabel.setBounds(header.removeFromLeft(420));
    versionLabel.setBounds(header.removeFromRight(210));

    area.removeFromTop(8);
    sourceCard = area.removeFromTop(92);
    area.removeFromTop(10);
    auto designRow = area.removeFromTop(350);
    const auto waveformWidth = juce::roundToInt(
        static_cast<float>(designRow.getWidth() - 10) * 0.60f);
    waveformCard = designRow.removeFromLeft(waveformWidth);
    designRow.removeFromLeft(10);
    finishCard = designRow;
    area.removeFromTop(10);
    primaryActionArea = area.removeFromTop(62);
    analyzeRangeButton.setBounds(primaryActionArea.reduced(0, 3));
    area.removeFromTop(10);
    auditionCard = area.removeFromTop(220);
    area.removeFromTop(8);
    footerArea = area;

    auto sourceContent = sourceCard.reduced(14);
    sourceContent.removeFromTop(30);
    auto sourceText = sourceContent.removeFromLeft(
        juce::jmin(470, sourceContent.getWidth() / 2));
    dropLabel.setBounds({});
    sourceLabel.setBounds(sourceText);
    auto sourceActions = sourceContent;
    clearButton.setBounds(sourceActions.removeFromRight(132));
    sourceActions.removeFromRight(8);
    captureButton.setBounds(sourceActions.removeFromRight(164));
    sourceActions.removeFromRight(8);
    importButton.setBounds(sourceActions.removeFromRight(160));

    auto waveContent = waveformCard.reduced(14);
    waveContent.removeFromTop(30);
    auto rangeRow = waveContent.removeFromBottom(40);
    waveContent.removeFromTop(8);
    waveformView.setBounds(waveContent);
    resetRangeButton.setBounds(rangeRow.removeFromRight(132));
    rangeRow.removeFromRight(12);
    rangeLabel.setBounds(rangeRow);

    auto finish = finishCard.reduced(14);
    finish.removeFromTop(30);
    const auto takeControlRow = [&finish]
    {
        auto row = finish.removeFromTop(33);
        finish.removeFromTop(3);
        return row;
    };
    auto modeRow = takeControlRow();
    modeLabel.setBounds(modeRow.removeFromLeft(126));
    generationModeBox.setBounds(modeRow);
    auto structureRow = takeControlRow();
    textureStructureLabel.setBounds(structureRow.removeFromLeft(126));
    textureStructureBox.setBounds(structureRow);
    repairDurationLabel.setBounds(textureStructureLabel.getBounds());
    repairDurationSlider.setBounds(textureStructureBox.getBounds());
    auto durationRow = takeControlRow();
    durationLabel.setBounds(durationRow.removeFromLeft(126));
    durationSlider.setBounds(durationRow);
    auto seamRow = durationLabel.getBounds().getUnion(durationSlider.getBounds());
    crossfadeLabel.setBounds(seamRow.removeFromLeft(126));
    crossfadeSlider.setBounds(seamRow);
    auto flattenRow = takeControlRow();
    flattenLabel.setBounds(flattenRow.removeFromLeft(126));
    flattenSlider.setBounds(flattenRow);
    auto crushRow = takeControlRow();
    dynamicsCrushLabel.setBounds(crushRow.removeFromLeft(126));
    dynamicsCrushSlider.setBounds(crushRow);
    auto matchRow = takeControlRow();
    sourceMatchLabel.setBounds(matchRow.removeFromLeft(126));
    sourceMatchSlider.setBounds(matchRow);
    auto characterRow = takeControlRow();
    characterLabel.setBounds(characterRow.removeFromLeft(126));
    characterBox.setBounds(characterRow);
    auto characterAmountRow = takeControlRow();
    characterAmountLabel.setBounds(characterAmountRow.removeFromLeft(126));
    characterAmountSlider.setBounds(characterAmountRow);

    auto audition = auditionCard.reduced(14);
    audition.removeFromTop(30);
    statusLabel.setBounds(audition.removeFromTop(26));
    audition.removeFromTop(4);
    candidateBox.setBounds(audition.removeFromTop(34));
    audition.removeFromTop(6);
    auto transport = audition.removeFromTop(38);
    previewTransportButton.setBounds(transport.removeFromLeft(126));
    transport.removeFromLeft(8);
    originalPreviewButton.setBounds(transport.removeFromLeft(96));
    transport.removeFromLeft(6);
    loopPreviewButton.setBounds(transport.removeFromLeft(116));
    transport.removeFromLeft(18);
    mixLabel.setBounds(transport.removeFromLeft(118));
    mixSlider.setBounds(transport);
    audition.removeFromTop(7);
    auto outputActions = audition.removeFromTop(38);
    regenerateButton.setBounds(outputActions.removeFromLeft(164));
    outputActions.removeFromLeft(8);
    dragToDawButton.setBounds(outputActions.removeFromLeft(196));
    outputActions.removeFromLeft(8);
    exportButton.setBounds(outputActions.removeFromLeft(150));

    signalAnalysisView.setBounds(footerArea);
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
    regenerateButton.setVisible(selectedTextureMode);
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
    previewTransportButton.setButtonText(previewing ? "Stop" : "Preview");
    previewTransportButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour(previewing ? 0xffa95f5b : 0xff426f63));

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
        juce::Colour(state == LoopEngine::State::failed ? warningAmber : loopGreen));
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
            "GENERATING  " + juce::String(juce::roundToInt(
                processor.getAnalysisProgress() * 100.0f)) + "%");
        return;
    }
    if (processor.getSourceName().isEmpty())
    {
        analyzeRangeButton.setButtonText("LOAD AUDIO");
        return;
    }
    const auto selectedMode = generationModeBox.getSelectedItemIndex();
    analyzeRangeButton.setButtonText(
        selectedMode == 0 ? "GENERATE ROTATE & REPAIR"
                          : "GENERATE TEXTURE LOOP");
}

void LoopSurgeonAudioProcessorEditor::configureSlider(
    juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 25);
    slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff3b414a));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(loopGreen));
    addAndMakeVisible(slider);
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(label);
}
