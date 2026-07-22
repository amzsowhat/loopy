#include "PluginEditor.h"

void LoopWaveformView::setWaveform(std::vector<float> newPeaks)
{
    peaks = std::move(newPeaks);
    repaint();
}

void LoopWaveformView::setLoop(const float newStart, const float newEnd)
{
    if (dragTarget == DragTarget::loopIn || dragTarget == DragTarget::loopOut)
        return;
    loopStart = juce::jlimit(0.0f, 1.0f, newStart);
    loopEnd = juce::jlimit(loopStart, 1.0f, newEnd);
    repaint();
}

void LoopWaveformView::setSourceRange(const float newStart, const float newEnd)
{
    sourceIn = juce::jlimit(0.0f, 0.99f, newStart);
    sourceOut = juce::jlimit(sourceIn + 0.01f, 1.0f, newEnd);
    repaint();
    if (onSourceRangeEdited)
        onSourceRangeEdited();
}

void LoopWaveformView::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(0xff20262e));
    graphics.fillRoundedRectangle(bounds, 6.0f);
    if (peaks.empty())
        return;
    graphics.setColour(juce::Colour(0x224fa3ff));
    graphics.fillRect(bounds.withX(bounds.getX() + bounds.getWidth() * sourceIn)
                          .withWidth(bounds.getWidth() * (sourceOut - sourceIn)));
    graphics.setColour(juce::Colour(0x3357c79a));
    graphics.fillRect(bounds.withX(bounds.getX() + bounds.getWidth() * loopStart)
                          .withWidth(bounds.getWidth() * (loopEnd - loopStart)));
    juce::Path path;
    const auto centre = bounds.getCentreY();
    for (size_t index = 0; index < peaks.size(); ++index)
    {
        const auto x = bounds.getX() + bounds.getWidth() * static_cast<float>(index)
                                      / static_cast<float>(peaks.size() - 1);
        const auto amplitude = peaks[index] * bounds.getHeight() * 0.45f;
        path.startNewSubPath(x, centre - amplitude);
        path.lineTo(x, centre + amplitude);
    }
    graphics.setColour(juce::Colour(0xff91a0b0));
    graphics.strokePath(path, juce::PathStrokeType(1.0f));
    graphics.setColour(juce::Colour(0xff57c79a));
    const auto startX = bounds.getX() + bounds.getWidth() * loopStart;
    const auto endX = bounds.getX() + bounds.getWidth() * loopEnd;
    graphics.drawLine(startX, bounds.getY(), startX, bounds.getBottom(), 2.0f);
    graphics.drawLine(endX, bounds.getY(), endX, bounds.getBottom(), 2.0f);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText("LOOP IN", juce::Rectangle<float>(startX + 4.0f, bounds.getBottom() - 18.0f,
                                                         52.0f, 16.0f),
                      juce::Justification::left);
    graphics.drawText("LOOP OUT", juce::Rectangle<float>(endX - 60.0f, bounds.getBottom() - 18.0f,
                                                          56.0f, 16.0f),
                      juce::Justification::right);

    graphics.setColour(juce::Colour(0xff4fa3ff));
    const auto inX = bounds.getX() + bounds.getWidth() * sourceIn;
    const auto outX = bounds.getX() + bounds.getWidth() * sourceOut;
    graphics.drawLine(inX, bounds.getY(), inX, bounds.getBottom(), 3.0f);
    graphics.drawLine(outX, bounds.getY(), outX, bounds.getBottom(), 3.0f);
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText("IN", juce::Rectangle<float>(inX + 4.0f, bounds.getY() + 2.0f, 24.0f, 16.0f),
                      juce::Justification::left);
    graphics.drawText("OUT", juce::Rectangle<float>(outX - 32.0f, bounds.getY() + 2.0f, 28.0f, 16.0f),
                      juce::Justification::right);
}

void LoopWaveformView::mouseDown(const juce::MouseEvent& event)
{
    const auto width = static_cast<float>(juce::jmax(1, getWidth()));
    const auto position = juce::jlimit(0.0f, 1.0f, event.position.x / width);
    const auto handleDistance = 8.0f / width;
    const auto loopInDistance = std::abs(position - loopStart);
    const auto loopOutDistance = std::abs(position - loopEnd);
    if (juce::jmin(loopInDistance, loopOutDistance) <= handleDistance)
        dragTarget = loopInDistance <= loopOutDistance ? DragTarget::loopIn : DragTarget::loopOut;
    else if (std::abs(position - sourceIn) <= handleDistance)
        dragTarget = DragTarget::sourceIn;
    else if (std::abs(position - sourceOut) <= handleDistance)
        dragTarget = DragTarget::sourceOut;
    else if (position > sourceIn && position < sourceOut)
        dragTarget = DragTarget::range;
    else
        dragTarget = std::abs(position - sourceIn) < std::abs(position - sourceOut)
                         ? DragTarget::sourceIn : DragTarget::sourceOut;
    dragAnchor = position;
    dragStartIn = sourceIn;
    dragStartOut = sourceOut;
}

void LoopWaveformView::mouseDrag(const juce::MouseEvent& event)
{
    const auto position = juce::jlimit(0.0f, 1.0f,
        event.position.x / static_cast<float>(juce::jmax(1, getWidth())));
    if (dragTarget == DragTarget::sourceIn)
        sourceIn = juce::jlimit(0.0f, sourceOut - 0.01f, position);
    else if (dragTarget == DragTarget::sourceOut)
        sourceOut = juce::jlimit(sourceIn + 0.01f, 1.0f, position);
    else if (dragTarget == DragTarget::range)
    {
        const auto length = dragStartOut - dragStartIn;
        const auto start = juce::jlimit(0.0f, 1.0f - length, dragStartIn + position - dragAnchor);
        sourceIn = start;
        sourceOut = start + length;
    }
    else if (dragTarget == DragTarget::loopIn)
        loopStart = juce::jlimit(sourceIn, loopEnd - 0.001f, position);
    else if (dragTarget == DragTarget::loopOut)
        loopEnd = juce::jlimit(loopStart + 0.001f, sourceOut, position);
    repaint();
    if ((dragTarget == DragTarget::loopIn || dragTarget == DragTarget::loopOut)
        && onLoopRangeEdited)
        onLoopRangeEdited();
    else if (onSourceRangeEdited)
        onSourceRangeEdited();
}

void LoopWaveformView::mouseUp(const juce::MouseEvent&)
{
    if ((dragTarget == DragTarget::loopIn || dragTarget == DragTarget::loopOut)
        && onLoopRangeCommitted)
        onLoopRangeCommitted();
    dragTarget = DragTarget::none;
}

LoopSurgeonAudioProcessorEditor::LoopSurgeonAudioProcessorEditor(
    LoopSurgeonAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner)
{
    titleLabel.setText("LOOP SURGEON", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff2f2f2));
    addAndMakeVisible(titleLabel);

    dropLabel.setText("Drop WAV / AIFF / FLAC here", juce::dontSendNotification);
    dropLabel.setJustificationType(juce::Justification::centred);
    dropLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd6dbe2));
    addAndMakeVisible(dropLabel);

    sourceLabel.setJustificationType(juce::Justification::centred);
    sourceLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9ea8b5));
    addAndMakeVisible(sourceLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7fe0b2));
    addAndMakeVisible(statusLabel);
    metricsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaeb7c2));
    metricsLabel.setFont(juce::FontOptions(12.0f));
    metricsLabel.setMinimumHorizontalScale(0.62f);
    addAndMakeVisible(metricsLabel);
    addAndMakeVisible(waveformView);
    waveformView.onSourceRangeEdited = [this] { updateRangeLabel(); };
    waveformView.onLoopRangeEdited = [this] { updateRangeLabel(); };
    waveformView.onLoopRangeCommitted = [this]
    {
        if (!processor.setManualLoopRange(waveformView.getLoopIn(), waveformView.getLoopOut()))
            lastMessage = "Load and analyse a source before editing Loop In/Out";
    };

    rangeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaeb7c2));
    addAndMakeVisible(rangeLabel);
    analyzeRangeButton.onClick = [this]
    {
        if (!processor.analyzeSourceRange(waveformView.getSourceIn(), waveformView.getSourceOut()))
            lastMessage = "Load a source before analysing a range";
    };
    analyzeRangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff315d86));
    addAndMakeVisible(analyzeRangeButton);
    resetRangeButton.onClick = [this]
    {
        waveformView.setSourceRange(0.0f, 1.0f);
    };
    addAndMakeVisible(resetRangeButton);

    originalPreviewButton.setClickingTogglesState(true);
    loopPreviewButton.setClickingTogglesState(true);
    originalPreviewButton.setRadioGroupId(101);
    loopPreviewButton.setRadioGroupId(101);
    originalPreviewButton.onClick = [this]
    {
        processor.setPreviewMode(LoopEngine::PreviewMode::original);
    };
    loopPreviewButton.onClick = [this]
    {
        processor.setPreviewMode(LoopEngine::PreviewMode::loop);
    };
    loopPreviewButton.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(originalPreviewButton);
    addAndMakeVisible(loopPreviewButton);

    candidateBox.setTextWhenNothingSelected("Automatic best candidate");
    candidateBox.onChange = [this]
    {
        if (candidateBox.getSelectedItemIndex() >= 0)
            processor.selectCandidate(candidateBox.getSelectedItemIndex());
    };
    addAndMakeVisible(candidateBox);

    importButton.onClick = [this] { chooseImportFile(); };
    importButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e7d63));
    addAndMakeVisible(importButton);
    exportButton.onClick = [this] { chooseExportFile(); };
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e7d63));
    addAndMakeVisible(exportButton);
    captureButton.onClick = [this] { processor.beginCapture(); };
    captureButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff404854));
    addAndMakeVisible(captureButton);
    clearButton.onClick = [this] { processor.clearLoop(); };
    clearButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff404854));
    addAndMakeVisible(clearButton);

    configureSlider(crossfadeSlider, crossfadeLabel, "Seam repair");
    configureSlider(mixSlider, mixLabel, "Preview mix");
    auto& parameters = processor.getParameterState();
    crossfadeAttachment = std::make_unique<SliderAttachment>(parameters, "crossfadeMs", crossfadeSlider);
    mixAttachment = std::make_unique<SliderAttachment>(parameters, "mix", mixSlider);

    setResizable(true, true);
    setResizeLimits(720, 650, 1100, 900);
    setSize(800, 700);
    startTimerHz(10);
}

LoopSurgeonAudioProcessorEditor::~LoopSurgeonAudioProcessorEditor() { stopTimer(); }

void LoopSurgeonAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff15191f));
    auto dropArea = getLocalBounds().reduced(24).withTrimmedTop(54).removeFromTop(110);
    graphics.setColour(juce::Colour(0xff252b34));
    graphics.fillRoundedRectangle(dropArea.toFloat(), 10.0f);
    graphics.setColour(juce::Colour(0xff586372));
    graphics.drawRoundedRectangle(dropArea.toFloat(), 10.0f, 1.0f);
}

void LoopSurgeonAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    titleLabel.setBounds(area.removeFromTop(38));
    auto drop = area.removeFromTop(110).reduced(10);
    dropLabel.setBounds(drop.removeFromTop(26));
    sourceLabel.setBounds(drop.removeFromTop(22));
    drop.removeFromTop(4);
    importButton.setBounds(drop.removeFromTop(32).withSizeKeepingCentre(170, 32));

    area.removeFromTop(12);
    waveformView.setBounds(area.removeFromTop(140));
    area.removeFromTop(8);
    auto rangeRow = area.removeFromTop(32);
    rangeLabel.setBounds(rangeRow.removeFromLeft(360));
    analyzeRangeButton.setBounds(rangeRow.removeFromLeft(155));
    rangeRow.removeFromLeft(8);
    resetRangeButton.setBounds(rangeRow.removeFromLeft(100));
    area.removeFromTop(6);
    candidateBox.setBounds(area.removeFromTop(32).withWidth(320));
    area.removeFromTop(6);
    auto previewRow = area.removeFromTop(30);
    originalPreviewButton.setBounds(previewRow.removeFromLeft(105));
    previewRow.removeFromLeft(6);
    loopPreviewButton.setBounds(previewRow.removeFromLeft(105));
    area.removeFromTop(4);
    statusLabel.setBounds(area.removeFromTop(28));
    metricsLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);

    auto controls = area.removeFromTop(132);
    auto crossfade = controls.removeFromLeft(150);
    crossfadeLabel.setBounds(crossfade.removeFromTop(22));
    crossfadeSlider.setBounds(crossfade);
    auto mix = controls.removeFromLeft(150);
    mixLabel.setBounds(mix.removeFromTop(22));
    mixSlider.setBounds(mix);

    auto actions = controls.reduced(8, 18);
    exportButton.setBounds(actions.removeFromTop(38));
    actions.removeFromTop(8);
    captureButton.setBounds(actions.removeFromTop(32));
    actions.removeFromTop(6);
    clearButton.setBounds(actions.removeFromTop(30).withWidth(100));
}

bool LoopSurgeonAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;aif;aiff;flac;ogg");
}

void LoopSurgeonAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() == 1)
        importFile(juce::File(files[0]));
}

void LoopSurgeonAudioProcessorEditor::importFile(const juce::File& file)
{
    lastMessage = processor.importAudioFile(file);
}

void LoopSurgeonAudioProcessorEditor::chooseImportFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Choose source audio", juce::File {},
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg");
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
    fileChooser = std::make_unique<juce::FileChooser>("Export seamless loop",
                                                       juce::File::getSpecialLocation(
                                                           juce::File::userDocumentsDirectory)
                                                           .getChildFile("Loop Surgeon.wav"),
                                                       "*.wav");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this] (const juce::FileChooser& chooser)
                             {
                                 const auto file = chooser.getResult();
                                 if (file != juce::File {})
                                     lastMessage = processor.exportLoopFile(file);
                             });
}

void LoopSurgeonAudioProcessorEditor::timerCallback()
{
    const auto source = processor.getSourceName();
    sourceLabel.setText(source.isEmpty() ? "No source loaded" : source, juce::dontSendNotification);
    const auto candidateCount = processor.getCandidateCount();
    const auto candidateRevision = processor.getCandidateRevision();
    const auto sourceRevision = processor.getSourceRevision();
    const auto sourceChanged = source != displayedSource || sourceRevision != displayedSourceRevision;
    if (sourceChanged)
    {
        displayedSource = source;
        displayedSourceRevision = sourceRevision;
        waveformView.setWaveform(processor.getWaveformPreview());
        waveformView.setSourceRange(0.0f, 1.0f);
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
    waveformView.setLoop(processor.getLoopStartProportion(), processor.getLoopEndProportion());
    exportButton.setEnabled(processor.getLoopState() == LoopEngine::State::ready);
    candidateBox.setEnabled(processor.getLoopState() == LoopEngine::State::ready);
    const auto previewMode = processor.getPreviewMode();
    originalPreviewButton.setToggleState(previewMode == LoopEngine::PreviewMode::original,
                                         juce::dontSendNotification);
    loopPreviewButton.setToggleState(previewMode == LoopEngine::PreviewMode::loop,
                                     juce::dontSendNotification);
    if (lastMessage.isNotEmpty())
    {
        statusLabel.setText(lastMessage, juce::dontSendNotification);
        lastMessage.clear();
        return;
    }
    switch (processor.getLoopState())
    {
        case LoopEngine::State::empty: statusLabel.setText("Import an audio material", juce::dontSendNotification); break;
        case LoopEngine::State::armed: statusLabel.setText("Waiting for DAW capture", juce::dontSendNotification); break;
        case LoopEngine::State::capturing:
            statusLabel.setText("Capturing " + juce::String(juce::roundToInt(processor.getCaptureProgress() * 100.0f)) + "%",
                                juce::dontSendNotification); break;
        case LoopEngine::State::analysing:
            statusLabel.setText("Searching periods and repairing the best seam...", juce::dontSendNotification); break;
        case LoopEngine::State::ready:
            statusLabel.setText(processor.isLowConfidence() ? "Loop ready - low confidence, audition carefully"
                                                             : "Loop ready",
                                juce::dontSendNotification); break;
        case LoopEngine::State::failed:
            statusLabel.setText("No reliable loop found in this selection", juce::dontSendNotification); break;
    }
    metricsLabel.setText("Seam " + juce::String(processor.getSeamQuality(), 0)
                             + "  Wave " + juce::String(processor.getWaveformScore(), 0)
                             + "  Period " + juce::String(processor.getPeriodicityScore(), 0)
                             + "  Spectrum " + juce::String(processor.getSpectrumScore(), 0)
                             + "  Phase " + juce::String(processor.getPhaseScore(), 0)
                             + "  Stereo " + juce::String(processor.getStereoScore(), 0)
                             + "  Transient " + juce::String(processor.getTransientScore(), 0),
                         juce::dontSendNotification);
}

void LoopSurgeonAudioProcessorEditor::updateRangeLabel()
{
    const auto duration = processor.getSourceDurationSeconds();
    rangeLabel.setText("IN " + juce::String(duration * waveformView.getSourceIn(), 3)
                           + " s  OUT " + juce::String(duration * waveformView.getSourceOut(), 3)
                           + " s    LOOP " + juce::String(duration * waveformView.getLoopIn(), 3)
                           + " - " + juce::String(duration * waveformView.getLoopOut(), 3) + " s",
                       juce::dontSendNotification);
}

void LoopSurgeonAudioProcessorEditor::configureSlider(juce::Slider& slider, juce::Label& label,
                                                       const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 24);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff57c79a));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff4a5260));
    addAndMakeVisible(slider);
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffd6dbe2));
    addAndMakeVisible(label);
}
