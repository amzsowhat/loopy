#include "PluginEditor.h"

#include <utility>

namespace
{
constexpr auto background = 0xff0b1016;
constexpr auto panel = 0xff121a24;
constexpr auto panelRaised = 0xff17222e;
constexpr auto border = 0xff263545;
constexpr auto textPrimary = 0xfff2f6fa;
constexpr auto textMuted = 0xff8d9aaa;
constexpr auto searchBlue = 0xff4ea1ff;
constexpr auto loopGreen = 0xff51d6a5;
constexpr auto warningAmber = 0xffffb65c;
}

LoopSurgeonLookAndFeel::LoopSurgeonLookAndFeel()
{
    setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    setColour(juce::TextButton::textColourOffId, juce::Colour(textPrimary));
    setColour(juce::TextButton::textColourOnId, juce::Colour(textPrimary));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263443));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315b78));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelRaised));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(border));
    setColour(juce::ComboBox::textColourId, juce::Colour(textPrimary));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffa9b7c6));
    setColour(juce::Slider::trackColourId, juce::Colour(0xff2b3948));
    setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2b3948));
    setColour(juce::Slider::thumbColourId, juce::Colour(loopGreen));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(textPrimary));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(panelRaised));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(border));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(panelRaised));
    setColour(juce::PopupMenu::textColourId, juce::Colour(textPrimary));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff294766));
}

void LoopSurgeonLookAndFeel::drawButtonBackground(
    juce::Graphics& graphics, juce::Button& button, const juce::Colour& baseColour,
    const bool highlighted, const bool down)
{
    auto colour = baseColour;
    if (!button.isEnabled())
        colour = colour.withMultipliedAlpha(0.42f);
    else if (down)
        colour = colour.darker(0.16f);
    else if (highlighted)
        colour = colour.brighter(0.10f);

    if (button.getToggleState())
        colour = button.findColour(juce::TextButton::buttonOnColourId);

    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    graphics.setColour(colour);
    graphics.fillRoundedRectangle(bounds, 7.0f);
    graphics.setColour(colour.brighter(0.22f).withAlpha(button.isEnabled() ? 0.65f : 0.25f));
    graphics.drawRoundedRectangle(bounds, 7.0f, 1.0f);
}

juce::Font LoopSurgeonLookAndFeel::getTextButtonFont(juce::TextButton&, const int buttonHeight)
{
    return juce::Font(juce::FontOptions(
        juce::jlimit(12.0f, 15.0f, static_cast<float>(buttonHeight) * 0.38f),
        juce::Font::bold));
}

juce::Font LoopSurgeonLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions(13.0f));
}

void LoopWaveformView::setWaveform(std::vector<float> newPeaks)
{
    peaks = std::move(newPeaks);
    repaint();
}

void LoopWaveformView::setRotation(const float proportion)
{
    const auto next = proportion < 0.0f ? -1.0f
        : juce::jlimit(sourceIn, sourceOut, proportion);
    if (rotation == next)
        return;
    rotation = next;
    repaint();
}

void LoopWaveformView::setSourceRange(const float newStart, const float newEnd)
{
    sourceIn = juce::jlimit(0.0f, 0.995f, newStart);
    sourceOut = juce::jlimit(sourceIn + 0.005f, 1.0f, newEnd);
    repaint();
    if (onSourceRangeEdited)
        onSourceRangeEdited();
}

bool LoopWaveformView::isEditingRotation() const noexcept
{
    return dragTarget == DragTarget::rotation;
}

void LoopWaveformView::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(0xff0d141c));
    graphics.fillRoundedRectangle(bounds, 8.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    for (int division = 1; division < 8; ++division)
    {
        const auto x = bounds.getX() + bounds.getWidth() * static_cast<float>(division) / 8.0f;
        graphics.setColour(juce::Colour(0xff263545).withAlpha(division == 4 ? 0.65f : 0.35f));
        graphics.drawVerticalLine(juce::roundToInt(x), bounds.getY() + 24.0f,
                                  bounds.getBottom() - 24.0f);
    }

    if (peaks.empty())
    {
        graphics.setColour(juce::Colour(textMuted));
        graphics.setFont(juce::FontOptions(14.0f));
        graphics.drawText("Waveform appears after an audio file is loaded",
                          getLocalBounds().reduced(24), juce::Justification::centred);
        return;
    }

    const auto sourceX = bounds.getX() + bounds.getWidth() * sourceIn;
    const auto sourceRight = bounds.getX() + bounds.getWidth() * sourceOut;
    graphics.setColour(juce::Colour(searchBlue).withAlpha(0.10f));
    graphics.fillRect(juce::Rectangle<float>(
        sourceX, bounds.getY(), sourceRight - sourceX, bounds.getHeight()));
    graphics.setColour(juce::Colour(0xff05080c).withAlpha(0.56f));
    graphics.fillRect(bounds.withWidth(juce::jmax(0.0f, sourceX - bounds.getX())));
    graphics.fillRect(bounds.withX(sourceRight)
                          .withWidth(juce::jmax(0.0f, bounds.getRight() - sourceRight)));

    juce::Path waveform;
    const auto centre = bounds.getCentreY();
    for (size_t index = 0; index < peaks.size(); ++index)
    {
        const auto x = bounds.getX() + bounds.getWidth() * static_cast<float>(index)
                                      / static_cast<float>(juce::jmax<size_t>(1, peaks.size() - 1));
        const auto amplitude = peaks[index] * (bounds.getHeight() - 50.0f) * 0.46f;
        waveform.startNewSubPath(x, centre - amplitude);
        waveform.lineTo(x, centre + amplitude);
    }
    graphics.setColour(juce::Colour(0xffa4b3c3).withAlpha(0.88f));
    graphics.strokePath(waveform, juce::PathStrokeType(1.0f));

    const auto drawMarker = [&] (const float proportion, const juce::String& label,
                                 const juce::Colour colour)
    {
        const auto x = bounds.getX() + bounds.getWidth() * proportion;
        graphics.setColour(colour);
        graphics.drawLine(x, bounds.getY(), x, bounds.getBottom() - 22.0f, 2.0f);
        juce::Path triangle;
        triangle.addTriangle(x - 5.0f, bounds.getY(), x + 5.0f, bounds.getY(),
                             x, bounds.getY() + 7.0f);
        graphics.fillPath(triangle);

        const auto labelWidth = label.length() > 9 ? 82.0f : 72.0f;
        auto labelX = juce::jlimit(bounds.getX() + 3.0f, bounds.getRight() - labelWidth - 3.0f,
                                   x - labelWidth * 0.5f);
        juce::Rectangle<float> tag(labelX, bounds.getY() + 3.0f, labelWidth, 18.0f);
        graphics.setColour(colour.withAlpha(0.20f));
        graphics.fillRoundedRectangle(tag, 4.0f);
        graphics.setColour(colour);
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawText(label, tag, juce::Justification::centred);
    };

    drawMarker(sourceIn, "SOURCE IN", juce::Colour(searchBlue));
    drawMarker(sourceOut, "SOURCE OUT", juce::Colour(searchBlue));
    if (rotation >= 0.0f)
        drawMarker(rotation, "LOOP START", juce::Colour(loopGreen));
}

void LoopWaveformView::mouseDown(const juce::MouseEvent& event)
{
    const auto width = static_cast<float>(juce::jmax(1, getWidth()));
    const auto position = juce::jlimit(0.0f, 1.0f, event.position.x / width);
    const auto handleDistance = 12.0f / width;
    const auto inDistance = std::abs(position - sourceIn);
    const auto outDistance = std::abs(position - sourceOut);
    const auto rotationDistance = std::abs(position - rotation);
    if (rotation >= 0.0f && rotationDistance <= handleDistance * 1.4f)
        dragTarget = DragTarget::rotation;
    else if (event.mods.isShiftDown() && position > sourceIn && position < sourceOut
        && juce::jmin(inDistance, outDistance) > handleDistance)
        dragTarget = DragTarget::sourceRange;
    else
        dragTarget = inDistance <= outDistance ? DragTarget::sourceIn : DragTarget::sourceOut;

    dragAnchor = position;
    dragStartIn = sourceIn;
    dragStartOut = sourceOut;
}

void LoopWaveformView::mouseDrag(const juce::MouseEvent& event)
{
    const auto position = juce::jlimit(0.0f, 1.0f,
        event.position.x / static_cast<float>(juce::jmax(1, getWidth())));
    if (dragTarget == DragTarget::sourceIn)
        sourceIn = juce::jlimit(0.0f, sourceOut - 0.005f, position);
    else if (dragTarget == DragTarget::sourceOut)
        sourceOut = juce::jlimit(sourceIn + 0.005f, 1.0f, position);
    else if (dragTarget == DragTarget::sourceRange)
    {
        const auto length = dragStartOut - dragStartIn;
        const auto start = juce::jlimit(0.0f, 1.0f - length,
                                        dragStartIn + position - dragAnchor);
        sourceIn = start;
        sourceOut = start + length;
    }
    else if (dragTarget == DragTarget::rotation)
        rotation = juce::jlimit(sourceIn + 0.001f, sourceOut - 0.001f, position);
    repaint();
    if ((dragTarget == DragTarget::sourceIn
         || dragTarget == DragTarget::sourceOut
         || dragTarget == DragTarget::sourceRange)
        && onSourceRangeEdited)
        onSourceRangeEdited();
}

void LoopWaveformView::mouseUp(const juce::MouseEvent&)
{
    const auto committedRotation = isEditingRotation();
    dragTarget = DragTarget::none;
    if (committedRotation && onRotationCommitted)
        onRotationCommitted();
}

void LoopQualityView::setScores(const float quality, const float repair,
                                const float spectrum, const float phase,
                                const float stereo, const float transient)
{
    scores = { quality, repair, spectrum, phase, stereo, transient };
    repaint();
}

void LoopQualityView::setTextureMode(const bool shouldUseTextureLabels)
{
    if (textureMode == shouldUseTextureLabels)
        return;
    textureMode = shouldUseTextureLabels;
    repaint();
}

void LoopQualityView::paint(juce::Graphics& graphics)
{
    constexpr std::array<const char*, 6> textureLabels {
        "CLOSURE", "LOUDNESS", "TIMBRE", "PHASE", "POSITION", "REPEAT"
    };
    constexpr std::array<const char*, 6> seamLabels {
        "LOOP START", "REPAIR", "SPECTRUM", "PHASE", "IMAGE", "CONTINUITY"
    };
    const auto& labels = textureMode ? textureLabels : seamLabels;
    auto row = getLocalBounds();
    const auto gap = 7;
    const auto width = (row.getWidth() - gap * 5) / 6;
    for (size_t index = 0; index < scores.size(); ++index)
    {
        auto cell = row.removeFromLeft(width);
        if (index + 1 < scores.size())
            row.removeFromLeft(gap);
        auto labelArea = cell.removeFromTop(15);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.setColour(juce::Colour(textMuted));
        graphics.drawText(labels[index], labelArea.removeFromLeft(labelArea.getWidth() - 28),
                          juce::Justification::centredLeft);
        graphics.setColour(juce::Colour(textPrimary));
        graphics.drawText(juce::String(scores[index], 0), labelArea,
                          juce::Justification::centredRight);

        auto track = cell.withHeight(5).toFloat();
        graphics.setColour(juce::Colour(0xff263545));
        graphics.fillRoundedRectangle(track, 2.5f);
        const auto score = juce::jlimit(0.0f, 100.0f, scores[index]);
        const auto colour = score >= 75.0f ? juce::Colour(loopGreen)
                           : score >= 55.0f ? juce::Colour(warningAmber)
                                            : juce::Colour(0xffe46d6d);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(
            track.withWidth(track.getWidth() * score / 100.0f), 2.5f);
    }
}

void SignalAnalysisView::setSnapshot(RenderQuality::SignalSnapshot next)
{
    snapshot = std::move(next);
    repaint();
}

void SignalAnalysisView::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bounds, 7.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);
    if (!snapshot.valid)
        return;

    auto content = bounds.reduced(10.0f, 7.0f);
    auto spectrumArea = content.removeFromLeft(content.getWidth() * 0.62f);
    content.removeFromLeft(10.0f);
    auto phaseArea = content.removeFromLeft(juce::jmin(content.getWidth() * 0.48f,
                                                       content.getHeight()));
    content.removeFromLeft(10.0f);
    auto meterArea = content;

    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.setColour(juce::Colour(textMuted));
    graphics.drawText("SPECTRUM  SOURCE / LOOP", spectrumArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    for (int line = 1; line < 4; ++line)
    {
        const auto y = spectrumArea.getY() + spectrumArea.getHeight() * line / 4.0f;
        graphics.setColour(juce::Colour(border).withAlpha(0.45f));
        graphics.drawHorizontalLine(juce::roundToInt(y), spectrumArea.getX(),
                                    spectrumArea.getRight());
    }
    const auto drawSpectrum = [&] (const auto& values, const juce::Colour colour,
                                   const float thickness)
    {
        juce::Path path;
        for (size_t point = 0; point < values.size(); ++point)
        {
            const auto x = spectrumArea.getX() + spectrumArea.getWidth()
                * static_cast<float>(point) / static_cast<float>(values.size() - 1u);
            const auto y = spectrumArea.getBottom()
                - spectrumArea.getHeight() * juce::jlimit(0.0f, 1.0f, values[point]);
            if (point == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(thickness));
    };
    drawSpectrum(snapshot.sourceSpectrum, juce::Colour(0xff778696), 1.0f);
    drawSpectrum(snapshot.outputSpectrum, juce::Colour(loopGreen), 1.5f);

    graphics.setColour(juce::Colour(textMuted));
    graphics.drawText("PHASE", phaseArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    const auto phaseSquare = phaseArea.withSizeKeepingCentre(
        juce::jmin(phaseArea.getWidth(), phaseArea.getHeight()),
        juce::jmin(phaseArea.getWidth(), phaseArea.getHeight()));
    graphics.setColour(juce::Colour(border));
    graphics.drawRect(phaseSquare, 1.0f);
    graphics.setColour(juce::Colour(border).withAlpha(0.55f));
    graphics.drawLine(phaseSquare.getCentreX(), phaseSquare.getY(),
                      phaseSquare.getCentreX(), phaseSquare.getBottom());
    graphics.drawLine(phaseSquare.getX(), phaseSquare.getCentreY(),
                      phaseSquare.getRight(), phaseSquare.getCentreY());
    graphics.setColour(juce::Colour(loopGreen).withAlpha(0.60f));
    for (const auto& point : snapshot.phasePoints)
    {
        const auto side = 0.5f * (point[0] - point[1]);
        const auto mid = 0.5f * (point[0] + point[1]);
        const auto x = phaseSquare.getCentreX() + side * phaseSquare.getWidth() * 0.45f;
        const auto y = phaseSquare.getCentreY() - mid * phaseSquare.getHeight() * 0.45f;
        graphics.fillEllipse(x - 0.75f, y - 0.75f, 1.5f, 1.5f);
    }

    graphics.setColour(juce::Colour(textMuted));
    graphics.drawText("CORRELATION", meterArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    auto correlationTrack = meterArea.removeFromTop(9.0f).reduced(0.0f, 2.0f);
    graphics.setColour(juce::Colour(0xff263545));
    graphics.fillRoundedRectangle(correlationTrack, 2.5f);
    const auto correlation = juce::jlimit(-1.0f, 1.0f, snapshot.outputCorrelation);
    const auto markerX = correlationTrack.getX()
        + 0.5f * (correlation + 1.0f) * correlationTrack.getWidth();
    graphics.setColour(correlation < 0.0f ? juce::Colour(warningAmber)
                                          : juce::Colour(loopGreen));
    graphics.fillRoundedRectangle(markerX - 2.0f, correlationTrack.getY() - 2.0f,
                                  4.0f, correlationTrack.getHeight() + 4.0f, 2.0f);
    graphics.setColour(juce::Colour(textPrimary));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(juce::String(correlation, 2), meterArea.removeFromTop(18.0f),
                      juce::Justification::centredLeft);
    const auto imbalance = snapshot.outputImbalanceDb;
    const auto side = imbalance > 0.25f ? "L " : imbalance < -0.25f ? "R " : "C ";
    graphics.setColour(juce::Colour(textMuted));
    graphics.drawText("POSITION  " + juce::String(side)
                          + juce::String(std::abs(imbalance), 1) + " dB",
                      meterArea.removeFromTop(16.0f), juce::Justification::centredLeft);
}

RenderDragButton::RenderDragButton()
    : juce::TextButton("Drag Loop to DAW")
{
    setTooltip("Drag the approved 24-bit WAV result to the DAW timeline");
}

void RenderDragButton::mouseDown(const juce::MouseEvent& event)
{
    dragStarted = false;
    juce::TextButton::mouseDown(event);
}

void RenderDragButton::mouseDrag(const juce::MouseEvent& event)
{
    juce::TextButton::mouseDrag(event);
    if (dragStarted || event.getDistanceFromDragStart() < 5 || !prepareFile)
        return;

    const auto file = prepareFile();
    if (!file.existsAsFile())
    {
        if (reportStatus)
            reportStatus("Could not prepare a DAW render");
        return;
    }

    dragStarted = juce::DragAndDropContainer::performExternalDragDropOfFiles(
        { file.getFullPathName() }, false, this);
    if (reportStatus)
        reportStatus(dragStarted ? "Drop the loop on the DAW timeline"
                                 : "The host did not start the file drag");
}

void RenderDragButton::mouseUp(const juce::MouseEvent& event)
{
    juce::TextButton::mouseUp(event);
    dragStarted = false;
}

LoopSurgeonAudioProcessorEditor::LoopSurgeonAudioProcessorEditor(
    LoopSurgeonAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("LOOP SURGEON", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    addAndMakeVisible(titleLabel);

    versionLabel.setText("0.9.0 PRE-RELEASE", juce::dontSendNotification);
    versionLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setColour(juce::Label::textColourId, juce::Colour(loopGreen));
    addAndMakeVisible(versionLabel);

    dropLabel.setText({}, juce::dontSendNotification);
    dropLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    dropLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    addAndMakeVisible(dropLabel);

    sourceLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    sourceLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(sourceLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(loopGreen));
    statusLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(statusLabel);

    addAndMakeVisible(waveformView);
    addAndMakeVisible(signalAnalysisView);
    addAndMakeVisible(qualityView);
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
    rangeLabel.setFont(juce::FontOptions(11.5f));
    addAndMakeVisible(rangeLabel);

    analyzeRangeButton.onClick = [this]
    {
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
    analyzeRangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff246e91));
    analyzeRangeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff246e91));
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
    previewTransportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff287b60));
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
    importButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff246e91));
    addAndMakeVisible(importButton);

    exportButton.onClick = [this] { chooseExportFile(); };
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff287b60));
    addAndMakeVisible(exportButton);

    dragToDawButton.prepareFile = [this] { return prepareDawDragFile(); };
    dragToDawButton.reportStatus = [this] (const juce::String& message)
    {
        lastMessage = message;
    };
    dragToDawButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff246e91));
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
    configureSlider(sourceMatchSlider, sourceMatchLabel, "REBUILD");
    crossfadeSlider.setTooltip(
        "Maximum overlap used to repair the source's original end-to-start seam");
    durationSlider.setTooltip(
        "Exact Texture Loop length in seconds; click the value to type it");
    repairDurationSlider.setTooltip(
        "Exact R&R output length inside Source In/Out; Selection keeps the complete range");
    flattenSlider.setTooltip(
        "Controls how strongly macro dynamics and one-shot movement are stabilised");
    sourceMatchSlider.setTooltip(
        "Transformation depth: increases reconstruction support or event re-organisation while retaining source material identity");
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
    sourceMatchAttachment = std::make_unique<SliderAttachment>(
        parameters, "sourceMatch", sourceMatchSlider);

    modeLabel.setText("MODE", juce::dontSendNotification);
    modeLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
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

    textureStructureLabel.setText("STYLE", juce::dontSendNotification);
    textureStructureLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    textureStructureLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(textureStructureLabel);
    textureStructureBox.addItem("Organism", 1);
    textureStructureBox.addItem("Spectral Drift", 2);
    textureStructureBox.addItem("Fracture", 3);
    textureStructureBox.setTooltip(
        "Three resynthesis geometries; all generate new audio from the analysed material model and never schedule source clips");
    textureStructureBox.onChange = [this]
    {
        if (processor.getSourceName().isNotEmpty())
            lastMessage = "Style changed - press Generate to apply";
    };
    addAndMakeVisible(textureStructureBox);
    textureStructureAttachment = std::make_unique<ComboBoxAttachment>(
        parameters, "textureStructure", textureStructureBox);

    setResizable(true, true);
    setResizeLimits(920, 760, 1320, 940);
    setSize(1040, 800);
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
    graphics.setColour(juce::Colour(0xffdbe4ed));
    graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    graphics.drawText(title, heading, juce::Justification::centredLeft);
    graphics.setColour(accent);
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(static_cast<float>(card.getX() + 14),
                               static_cast<float>(card.getY() + 34), 44.0f, 2.0f),
        1.0f);
}

void LoopSurgeonAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    juce::ColourGradient gradient(juce::Colour(0xff111923), 0.0f, 0.0f,
                                  juce::Colour(background), 0.0f,
                                  static_cast<float>(getHeight()), false);
    graphics.setGradientFill(gradient);
    graphics.fillAll();

    drawCard(graphics, sourceCard, "SOURCE", juce::Colour(searchBlue));
    drawCard(graphics, waveformCard, "SOURCE RANGE", juce::Colour(searchBlue));
    drawCard(graphics, auditionCard, "AUDITION", juce::Colour(loopGreen));
    drawCard(graphics, finishCard, "GENERATION", juce::Colour(loopGreen));

    graphics.setColour(juce::Colour(border));
    graphics.drawHorizontalLine(footerArea.getY(), static_cast<float>(footerArea.getX()),
                                static_cast<float>(footerArea.getRight()));
}

void LoopSurgeonAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    auto header = area.removeFromTop(44);
    titleLabel.setBounds(header.removeFromLeft(360));
    versionLabel.setBounds(header.removeFromRight(180));

    area.removeFromTop(6);
    sourceCard = area.removeFromTop(80);
    area.removeFromTop(10);
    waveformCard = area.removeFromTop(244);
    area.removeFromTop(10);

    auto bottom = area.removeFromTop(237);
    const auto leftWidth = (bottom.getWidth() - 10) / 2;
    auditionCard = bottom.removeFromLeft(leftWidth);
    bottom.removeFromLeft(10);
    finishCard = bottom;
    area.removeFromTop(8);
    footerArea = area;

    auto sourceContent = sourceCard.reduced(14);
    sourceContent.removeFromTop(27);
    auto sourceText = sourceContent.removeFromLeft(360);
    dropLabel.setBounds({});
    sourceLabel.setBounds(sourceText.removeFromTop(28));
    auto sourceActions = sourceContent;
    clearButton.setBounds(sourceActions.removeFromRight(122).reduced(0, 5));
    sourceActions.removeFromRight(8);
    captureButton.setBounds(sourceActions.removeFromRight(148).reduced(0, 5));
    sourceActions.removeFromRight(8);
    importButton.setBounds(sourceActions.removeFromRight(146).reduced(0, 5));

    auto waveContent = waveformCard.reduced(14);
    waveContent.removeFromTop(27);
    waveformView.setBounds(waveContent.removeFromTop(166));
    waveContent.removeFromTop(8);
    auto rangeRow = waveContent.removeFromTop(35);
    resetRangeButton.setBounds(rangeRow.removeFromRight(120));
    rangeRow.removeFromRight(8);
    analyzeRangeButton.setBounds(rangeRow.removeFromRight(176));
    rangeRow.removeFromRight(12);
    rangeLabel.setBounds(rangeRow);

    auto audition = auditionCard.reduced(14);
    audition.removeFromTop(28);
    candidateBox.setBounds(audition.removeFromTop(34));
    audition.removeFromTop(8);
    auto transport = audition.removeFromTop(38);
    previewTransportButton.setBounds(transport.removeFromLeft(116));
    transport.removeFromLeft(8);
    originalPreviewButton.setBounds(transport.removeFromLeft(86));
    transport.removeFromLeft(6);
    loopPreviewButton.setBounds(transport.removeFromLeft(104));
    audition.removeFromTop(10);
    auto auditionMix = audition.removeFromTop(40);
    mixLabel.setBounds(auditionMix.removeFromLeft(112));
    mixSlider.setBounds(auditionMix);
    audition.removeFromTop(8);
    regenerateButton.setBounds(audition.removeFromTop(34).removeFromLeft(150));

    auto finish = finishCard.reduced(14);
    finish.removeFromTop(28);
    auto modeRow = finish.removeFromTop(34);
    modeLabel.setBounds(modeRow.removeFromLeft(112));
    generationModeBox.setBounds(modeRow);
    finish.removeFromTop(7);
    auto structureRow = finish.removeFromTop(34);
    textureStructureLabel.setBounds(structureRow.removeFromLeft(112));
    textureStructureBox.setBounds(structureRow);
    finish.removeFromTop(5);
    auto durationRow = finish.removeFromTop(36);
    durationLabel.setBounds(durationRow.removeFromLeft(112));
    durationSlider.setBounds(durationRow);
    repairDurationLabel.setBounds(durationLabel.getBounds());
    repairDurationSlider.setBounds(durationSlider.getBounds());
    auto seamRow = durationLabel.getBounds().getUnion(durationSlider.getBounds());
    crossfadeLabel.setBounds(seamRow.removeFromLeft(112));
    crossfadeSlider.setBounds(seamRow);
    finish.removeFromTop(5);
    auto flattenRow = finish.removeFromTop(36);
    flattenLabel.setBounds(flattenRow.removeFromLeft(112));
    flattenSlider.setBounds(flattenRow);
    finish.removeFromTop(5);
    auto matchRow = finish.removeFromTop(36);
    sourceMatchLabel.setBounds(matchRow.removeFromLeft(112));
    sourceMatchSlider.setBounds(matchRow);

    auto footer = footerArea;
    exportButton.setBounds(footer.removeFromRight(140).reduced(0, 7));
    footer.removeFromRight(8);
    dragToDawButton.setBounds(footer.removeFromRight(184).reduced(0, 7));
    footer.removeFromRight(14);
    statusLabel.setBounds(footer.removeFromTop(25));
    qualityView.setBounds(footer.removeFromBottom(27));
    footer.removeFromTop(3);
    footer.removeFromBottom(3);
    signalAnalysisView.setBounds(footer);
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
    qualityView.setTextureMode(ready ? textureResult : selectedTextureMode);
    if (ready && !textureResult && !waveformView.isEditingRotation())
        waveformView.setRotation(processor.getRotationProportion());
    else if (!ready || textureResult)
        waveformView.setRotation(-1.0f);
    const auto approved = ready && processor.hasPassedQualityGate();
    signalAnalysisView.setSnapshot(ready ? processor.getSignalSnapshot()
                                         : RenderQuality::SignalSnapshot {});
    exportButton.setEnabled(approved);
    dragToDawButton.setEnabled(approved);
    candidateBox.setEnabled(ready);
    previewTransportButton.setEnabled(ready);
    originalPreviewButton.setEnabled(ready);
    loopPreviewButton.setEnabled(ready);
    analyzeRangeButton.setEnabled(source.isNotEmpty() && state != LoopEngine::State::analysing);
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
    sourceMatchLabel.setVisible(!directModeSelected);
    sourceMatchSlider.setVisible(!directModeSelected);
    textureStructureLabel.setVisible(!directModeSelected);
    textureStructureBox.setVisible(!directModeSelected);
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
        juce::Colour(previewing ? 0xff8a4949 : 0xff287b60));

    if (lastMessage.isNotEmpty())
    {
        statusLabel.setText(lastMessage, juce::dontSendNotification);
        lastMessage.clear();
    }
    else
    {
        switch (state)
        {
            case LoopEngine::State::empty:
                statusLabel.setText("No source",
                                    juce::dontSendNotification);
                break;
            case LoopEngine::State::sourceReady:
                statusLabel.setText(
                    "Source ready - set range and mode, then Generate",
                    juce::dontSendNotification);
                break;
            case LoopEngine::State::armed:
                statusLabel.setText("Waiting for DAW capture",
                                    juce::dontSendNotification);
                break;
            case LoopEngine::State::capturing:
                statusLabel.setText(
                    "Capturing " + juce::String(
                        juce::roundToInt(processor.getCaptureProgress() * 100.0f)) + "%",
                    juce::dontSendNotification);
                break;
            case LoopEngine::State::analysing:
                statusLabel.setText(
                    juce::String("Generating  ") + juce::String(juce::roundToInt(
                        processor.getAnalysisProgress() * 100.0f)) + "%",
                                    juce::dontSendNotification);
                break;
            case LoopEngine::State::ready:
                statusLabel.setText(
                    processor.isLowConfidence()
                        ? "Loop ready — low confidence: compare candidates carefully"
                        : "Loop ready — press Preview, then Export when satisfied",
                    juce::dontSendNotification);
                break;
            case LoopEngine::State::failed:
                statusLabel.setText(
                    "No reliable loop found — widen the blue range or try different material",
                    juce::dontSendNotification);
                break;
        }
    }

    if (state == LoopEngine::State::ready)
    {
        const auto passedGate = processor.hasPassedQualityGate();
        statusLabel.setColour(juce::Label::textColourId,
                              juce::Colour(passedGate ? loopGreen : warningAmber));
        statusLabel.setText(
            juce::String(textureResult ? "Texture Loop ready"
                                       : "Rotate & Repair loop ready")
                + (passedGate
                    ? " - QC PASS " + juce::String(processor.getRenderQualityScore(), 0)
                    : " - QC BLOCKED: regenerate or refine"),
            juce::dontSendNotification);
    }
    else
    {
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(loopGreen));
    }

    qualityView.setScores(
        processor.getSeamQuality(),
        textureResult ? processor.getLevelScore() : processor.getRepairScore(),
        processor.getSpectrumScore(), processor.getPhaseScore(),
        processor.getStereoScore(),
        textureResult ? processor.getRepeatSafetyScore()
                      : processor.getPeriodicityScore());
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
    const auto selectedMode = generationModeBox.getSelectedItemIndex();
    analyzeRangeButton.setButtonText(
        selectedMode == 0 ? "Repair Selected Loop"
                          : "Generate Texture Loop");
}

void LoopSurgeonAudioProcessorEditor::configureSlider(
    juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 25);
    slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff2d3c4b));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(loopGreen));
    addAndMakeVisible(slider);
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(label);
}
