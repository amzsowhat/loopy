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

void SignalAnalysisView::setSnapshot(SignalDiagnostics::SignalSnapshot next)
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
    setTooltip("Render a 24-bit WAV and drag it to the DAW timeline");
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

