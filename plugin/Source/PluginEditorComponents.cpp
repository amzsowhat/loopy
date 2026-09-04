#include "PluginEditor.h"

#include "BinaryData.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>

namespace
{
constexpr auto black = LoopSurgeonTheme::background;
constexpr auto charcoal = LoopSurgeonTheme::surface;
constexpr auto sulfur = LoopSurgeonTheme::accent;
constexpr auto mint = LoopSurgeonTheme::accent;
constexpr auto coral = LoopSurgeonTheme::accent;
constexpr auto violet = LoopSurgeonTheme::secondary;
constexpr auto ivory = LoopSurgeonTheme::text;
constexpr auto dimIvory = LoopSurgeonTheme::secondary;

constexpr float transitionEpsilon = 0.002f;

float deltaSecondsSince(double& lastFrameMs)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto delta = lastFrameMs <= 0.0 ? 1.0 / 60.0
                                          : juce::jlimit(0.0, 0.05,
                                              (now - lastFrameMs) / 1000.0);
    lastFrameMs = now;
    return static_cast<float>(delta);
}

bool isSettled(const float current, const float target) noexcept
{
    return std::abs(current - target) < transitionEpsilon;
}

// A static projection of the mathematical Mobius surface. No bitmap or animation.
void paintMobiusGeometry(juce::Graphics& graphics, const juce::Rectangle<float> bounds)
{
    struct Vertex { juce::Point<float> point; float depth; };
    struct Face { juce::Path path; float depth; float tone; };
    std::array<Face, 144> faces;
    const auto project = [&] (const float u, const float v)
    {
        const auto radius = 1.0f + v * std::cos(u * 0.5f);
        const auto x = radius * std::cos(u);
        const auto y = radius * std::sin(u);
        const auto z = v * std::sin(u * 0.5f);
        const auto scale = juce::jmin(bounds.getWidth() * 0.37f, bounds.getHeight() * 0.54f);
        return Vertex { { bounds.getCentreX() + x * scale,
                          bounds.getCentreY() + (y * 0.48f - z * 0.88f) * scale },
                        y * 0.88f + z * 0.48f };
    };
    for (size_t i = 0; i < faces.size(); ++i)
    {
        const auto u = static_cast<float>(i) * juce::MathConstants<float>::twoPi / faces.size();
        const auto v = static_cast<float>(i + 1) * juce::MathConstants<float>::twoPi / faces.size();
        const auto p = project(u, -0.3f), q = project(u, 0.3f);
        const auto r = project(v, 0.3f), s = project(v, -0.3f);
        auto& face = faces[i];
        face.path.startNewSubPath(p.point);
        face.path.lineTo(q.point); face.path.lineTo(r.point); face.path.lineTo(s.point);
        face.path.closeSubPath();
        face.depth = (p.depth + q.depth + r.depth + s.depth) * 0.25f;
        face.tone = 0.5f + 0.5f * std::sin(u * 0.5f - 0.8f);
    }
    std::sort(faces.begin(), faces.end(), [] (const Face& a, const Face& b)
    {
        return a.depth < b.depth;
    });
    for (const auto& face : faces)
    {
        graphics.setColour(juce::Colour(0xff6f5546).interpolatedWith(
            juce::Colour(LoopSurgeonTheme::accent), face.tone));
        graphics.fillPath(face.path);
        graphics.strokePath(face.path, juce::PathStrokeType(1.0f));
    }
}

void drawMobius(juce::Graphics& graphics, const juce::Rectangle<float> bounds)
{
    // Supersample the static emblem once per size, not once per frame.
    static juce::Image emblem;
    const auto width = juce::jmax(1, juce::roundToInt(bounds.getWidth() * 3.0f));
    const auto height = juce::jmax(1, juce::roundToInt(bounds.getHeight() * 3.0f));
    if (emblem.getWidth() != width || emblem.getHeight() != height)
    {
        emblem = juce::Image(juce::Image::ARGB, width, height, true);
        juce::Graphics layer(emblem);
        paintMobiusGeometry(layer, emblem.getBounds().toFloat());
    }
    graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    graphics.drawImage(emblem, bounds);
}
}

LoopSurgeonLookAndFeel::LoopSurgeonLookAndFeel()
{


    displayTypeface = juce::Typeface::createSystemTypefaceFor(
        LoopSurgeonAssets::SpaceGroteskBold_ttf,
        LoopSurgeonAssets::SpaceGroteskBold_ttfSize);
    handTypeface = juce::Typeface::createSystemTypefaceFor(
        LoopSurgeonAssets::SpaceGroteskMedium_ttf,
        LoopSurgeonAssets::SpaceGroteskMedium_ttfSize);

    setColour(juce::Label::textColourId, juce::Colour(ivory));
    setColour(juce::TextButton::textColourOffId, juce::Colour(ivory));
    setColour(juce::TextButton::textColourOnId, juce::Colour(black));
    setColour(juce::TextButton::buttonColourId, juce::Colour(charcoal));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(sulfur));
    setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::textColourId, juce::Colour(ivory));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(mint));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(black));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(black));
    setColour(juce::PopupMenu::textColourId, juce::Colour(ivory));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(mint));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(black));
}

void LoopSurgeonLookAndFeel::drawButtonBackground(
    juce::Graphics& graphics, juce::Button& button, const juce::Colour&,
    const bool highlighted, const bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const auto selected = button.getToggleState();
    graphics.setColour(juce::Colour(selected ? LoopSurgeonTheme::selected
                                           : highlighted ? 0xff2c3034 : LoopSurgeonTheme::surface));
    graphics.fillRoundedRectangle(bounds, 6.0f);
    graphics.setColour(juce::Colour(selected ? LoopSurgeonTheme::accent : LoopSurgeonTheme::line));
    graphics.drawRoundedRectangle(bounds, 6.0f, down ? 1.5f : 1.0f);
}

void LoopSurgeonLookAndFeel::drawButtonText(
    juce::Graphics& graphics, juce::TextButton& button, const bool, const bool down)
{
    graphics.setColour(juce::Colour(button.getToggleState() ? sulfur : ivory)
        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
    graphics.setFont(getDisplayFont(16.0f));
    graphics.drawText(button.getButtonText(),
        button.getLocalBounds().reduced(8, 4).translated(0, down ? 1 : 0),
        juce::Justification::centred);
}

void LoopSurgeonLookAndFeel::drawComboBox(
    juce::Graphics& graphics, const int width, const int height, const bool,
    const int, const int, const int, const int, juce::ComboBox&)
{
    graphics.setColour(juce::Colour(charcoal));
    graphics.fillRoundedRectangle(0.5f, 0.5f, width - 1.0f, height - 1.0f, 5.0f);
    graphics.setColour(juce::Colour(LoopSurgeonTheme::line));
    graphics.drawRoundedRectangle(0.5f, 0.5f, width - 1.0f, height - 1.0f, 5.0f, 1.0f);
}

void LoopSurgeonLookAndFeel::drawPopupMenuBackground(
    juce::Graphics& graphics, const int width, const int height)
{
    graphics.fillAll(juce::Colour(black));
    graphics.setColour(juce::Colour(mint));
    graphics.drawRect(juce::Rectangle<int>(0, 0, width, height), 1);
}

void LoopSurgeonLookAndFeel::drawPopupMenuItem(
    juce::Graphics& graphics, const juce::Rectangle<int>& area,
    const bool separator, const bool active, const bool highlighted,
    const bool ticked, const bool hasSubMenu, const juce::String& text,
    const juce::String& shortcutText, const juce::Drawable*,
    const juce::Colour*)
{
    if (separator)
    {
        graphics.setColour(juce::Colour(ivory).withAlpha(0.14f));
        graphics.drawHorizontalLine(area.getCentreY(),
                                    static_cast<float>(area.getX() + 8),
                                    static_cast<float>(area.getRight() - 8));
        return;
    }
    if (highlighted && active)
    {
        graphics.setColour(juce::Colour(mint));
        graphics.fillRect(area.reduced(2, 1));
    }
    graphics.setColour(juce::Colour(highlighted ? black : ivory)
                           .withMultipliedAlpha(active ? 1.0f : 0.35f));
    graphics.setFont(getPopupMenuFont());
    auto textArea = area.reduced(12, 0);
    if (ticked)
        graphics.drawText("+", textArea.removeFromLeft(18),
                          juce::Justification::centredLeft);
    graphics.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);
    if (hasSubMenu)
        graphics.drawText(">", textArea.removeFromRight(15),
                          juce::Justification::centredRight);
    juce::ignoreUnused(shortcutText);
}

juce::Label* LoopSurgeonLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setColour(juce::Label::textColourId, juce::Colour(ivory));
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::TextEditor::textColourId, juce::Colour(black));
    label->setColour(juce::TextEditor::backgroundColourId, juce::Colour(ivory));
    label->setColour(juce::TextEditor::outlineColourId, juce::Colour(mint));
    label->setJustificationType(juce::Justification::centred);
    return label;
}

void LoopSurgeonLookAndFeel::drawLabel(juce::Graphics& graphics, juce::Label& label)
{
    if (label.getComponentID() == "artReadout")
    {
        graphics.setColour(juce::Colour(LoopSurgeonTheme::inset));
        graphics.fillRect(label.getLocalBounds());
        graphics.setColour(juce::Colour(ivory));
        graphics.setFont(getHandFont(14.0f));
        graphics.drawText(label.getText(), label.getLocalBounds(), juce::Justification::centred);
        return;
    }
    graphics.setColour(label.findColour(juce::Label::textColourId)
        .withMultipliedAlpha(label.isEnabled() ? 1.0f : 0.45f));
    graphics.setFont(label.getFont());
    graphics.drawFittedText(label.getText(), label.getLocalBounds().reduced(2),
                            label.getJustificationType(), label.getComponentID() == "statusReadout" ? 2 : 1);
}

juce::Font LoopSurgeonLookAndFeel::getTextButtonFont(
    juce::TextButton& button, const int buttonHeight)
{
    const auto id = button.getComponentID();
    if (id == "primaryAction")
        return getDisplayFont(juce::jlimit(15.0f, 22.0f,
            static_cast<float>(buttonHeight) * 0.28f));
    if (id == "tailAction")
        return getDisplayFont(9.5f);
    return getDisplayFont(juce::jlimit(11.0f, 14.0f,
        static_cast<float>(buttonHeight) * 0.34f));
}

juce::Font LoopSurgeonLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return getHandFont(14.0f);
}

juce::Font LoopSurgeonLookAndFeel::getLabelFont(juce::Label& label)
{
    if (label.getComponentID() == "displayTitle")
        return getDisplayFont(label.getFont().getHeight());
    return getHandFont(juce::jmax(11.0f, label.getFont().getHeight()));
}

juce::Font LoopSurgeonLookAndFeel::getPopupMenuFont()
{
    return getHandFont(15.0f);
}

juce::Font LoopSurgeonLookAndFeel::getDisplayFont(const float height) const
{
    return displayTypeface != nullptr
        ? juce::Font(juce::FontOptions(displayTypeface).withHeight(height))
        : juce::Font(juce::FontOptions(height, juce::Font::bold));
}

juce::Font LoopSurgeonLookAndFeel::getHandFont(const float height) const
{
    return handTypeface != nullptr
        ? juce::Font(juce::FontOptions(handTypeface).withHeight(height))
        : juce::Font(juce::FontOptions(height));
}

LoopWaveformView::LoopWaveformView()
{
    markerPopup.setComponentID("artReadout");
    markerPopup.setJustificationType(juce::Justification::centred);
    markerPopup.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    markerPopup.setColour(juce::Label::backgroundColourId,
                          juce::Colour(black).withAlpha(0.96f));
    markerPopup.setColour(juce::Label::outlineColourId,
                          juce::Colour(mint).withAlpha(0.92f));
    markerPopup.setColour(juce::Label::textColourId, juce::Colour(ivory));
    markerPopup.setBorderSize(juce::BorderSize<int>(1));
    markerPopup.setInterceptsMouseClicks(false, false);
    addChildComponent(markerPopup);
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

void LoopWaveformView::setDurationSeconds(const double seconds)
{
    durationSeconds = juce::jmax(0.0, seconds);
    repaint();
}

bool LoopWaveformView::isEditingRotation() const noexcept
{
    return dragTarget == DragTarget::rotation;
}

void LoopWaveformView::updateMarkerPopup(const float proportion,
                                         const juce::String& prefix,
                                         const juce::Colour colour)
{
    const auto seconds = durationSeconds * static_cast<double>(proportion);
    markerPopup.setText(prefix + "   " + juce::String(seconds, 3) + " s",
                        juce::dontSendNotification);
    markerPopup.setColour(juce::Label::outlineColourId, colour);

    const auto bounds = getApertureBounds().reduced(10.0f, 8.0f);
    const auto width = 170.0f;
    const auto height = 34.0f;
    const auto popupX = bounds.getCentreX() - width * 0.5f;
    const auto popupY = bounds.getBottom() - height - 4.0f;
    markerPopup.setBounds(juce::roundToInt(popupX), juce::roundToInt(popupY),
                          juce::roundToInt(width), juce::roundToInt(height));
    markerPopup.setVisible(true);
    markerPopup.toFront(false);
}

void LoopWaveformView::hideMarkerPopup()
{
    markerPopup.setVisible(false);
}

juce::Rectangle<float> LoopWaveformView::getApertureBounds() const
{
    return getLocalBounds().toFloat().reduced(5.0f, 4.0f);
}

juce::Path LoopWaveformView::createAperturePath() const
{
    juce::Path aperture;
    aperture.addRectangle(getApertureBounds());
    return aperture;
}

void LoopWaveformView::paint(juce::Graphics& graphics)
{
    const auto aperture = createAperturePath();
    auto bounds = getApertureBounds().reduced(10.0f, 8.0f);
    juce::Graphics::ScopedSaveState clipped(graphics);
    graphics.reduceClipRegion(aperture);

    graphics.setColour(juce::Colour(LoopSurgeonTheme::inset));
    graphics.fillPath(aperture);

    if (peaks.empty())
    {
        graphics.setColour(juce::Colour(ivory).withAlpha(0.88f));
        if (const auto* skin = dynamic_cast<const LoopSurgeonLookAndFeel*>(&getLookAndFeel()))
            graphics.setFont(skin->getHandFont(19.0f));
        graphics.drawText("Drop audio here", getLocalBounds().reduced(36),
                          juce::Justification::centred);
        return;
    }

    const auto sourceX = bounds.getX() + bounds.getWidth() * sourceIn;
    const auto sourceRight = bounds.getX() + bounds.getWidth() * sourceOut;
    graphics.setColour(juce::Colour(mint).withAlpha(0.15f));
    graphics.fillRect(juce::Rectangle<float>(sourceX, bounds.getY(),
                                              sourceRight - sourceX,
                                              bounds.getHeight()));
    graphics.setColour(juce::Colour(black).withAlpha(0.46f));
    graphics.fillRect(bounds.withWidth(juce::jmax(0.0f, sourceX - bounds.getX())));
    graphics.fillRect(bounds.withX(sourceRight)
                          .withWidth(juce::jmax(0.0f, bounds.getRight() - sourceRight)));

    juce::Path waveform;
    const auto centre = bounds.getCentreY();
    for (size_t index = 0; index < peaks.size(); ++index)
    {
        const auto x = bounds.getX() + bounds.getWidth()
            * static_cast<float>(index)
            / static_cast<float>(juce::jmax<size_t>(1, peaks.size() - 1));
        const auto amplitude = peaks[index] * (bounds.getHeight() - 40.0f) * 0.46f;
        waveform.startNewSubPath(x, centre - amplitude);
        waveform.lineTo(x, centre + amplitude);
    }
    graphics.setColour(juce::Colour(sulfur).withAlpha(0.96f));
    graphics.strokePath(waveform, juce::PathStrokeType(1.35f));

    const auto drawMarker = [&] (const float proportion, const juce::Colour colour)
    {
        const auto x = bounds.getX() + bounds.getWidth() * proportion;
        graphics.setColour(colour);
        graphics.drawLine(x, bounds.getY() + 6.0f, x, bounds.getBottom() - 6.0f, 1.5f);
        graphics.fillRoundedRectangle(x - 4.0f, bounds.getCentreY() - 10.0f, 8.0f, 20.0f, 3.0f);

    };

    drawMarker(sourceIn, juce::Colour(mint));
    drawMarker(sourceOut, juce::Colour(mint));
    if (rotation >= 0.0f)
        drawMarker(rotation, juce::Colour(coral));
}

void LoopWaveformView::mouseDown(const juce::MouseEvent& event)
{
    const auto bounds = getApertureBounds().reduced(10.0f, 8.0f);
    const auto position = juce::jlimit(0.0f, 1.0f,
        (event.position.x - bounds.getX()) / juce::jmax(1.0f, bounds.getWidth()));
    const auto handleDistance = 20.0f / juce::jmax(1.0f, bounds.getWidth());
    const auto inDistance = std::abs(position - sourceIn);
    const auto outDistance = std::abs(position - sourceOut);
    const auto rotationDistance = std::abs(position - rotation);
    if (rotation >= 0.0f && rotationDistance <= handleDistance * 1.4f)
        dragTarget = DragTarget::rotation;
    else if (event.mods.isShiftDown() && position > sourceIn && position < sourceOut
        && juce::jmin(inDistance, outDistance) > handleDistance)
        dragTarget = DragTarget::sourceRange;
    else
        dragTarget = inDistance <= outDistance ? DragTarget::sourceIn
                                               : DragTarget::sourceOut;
    dragAnchor = position;
    dragStartIn = sourceIn;
    dragStartOut = sourceOut;

    if (dragTarget == DragTarget::sourceIn)
        updateMarkerPopup(sourceIn, "IN", juce::Colour(mint));
    else if (dragTarget == DragTarget::sourceOut)
        updateMarkerPopup(sourceOut, "OUT", juce::Colour(mint));
    else if (dragTarget == DragTarget::rotation)
        updateMarkerPopup(rotation, "LOOP", juce::Colour(coral));
}

void LoopWaveformView::mouseDrag(const juce::MouseEvent& event)
{
    const auto bounds = getApertureBounds().reduced(10.0f, 8.0f);
    auto position = juce::jlimit(0.0f, 1.0f,
        (event.position.x - bounds.getX()) / juce::jmax(1.0f, bounds.getWidth()));
    if (event.mods.isAltDown())
        position = juce::jlimit(0.0f, 1.0f,
            dragAnchor + (position - dragAnchor) * 0.12f);
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
    if (dragTarget == DragTarget::sourceIn)
        updateMarkerPopup(sourceIn, "IN", juce::Colour(mint));
    else if (dragTarget == DragTarget::sourceOut)
        updateMarkerPopup(sourceOut, "OUT", juce::Colour(mint));
    else if (dragTarget == DragTarget::rotation)
        updateMarkerPopup(rotation, "LOOP", juce::Colour(coral));
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
    hideMarkerPopup();
    if (committedRotation && onRotationCommitted)
        onRotationCommitted();
}

void LoopWaveformView::mouseDoubleClick(const juce::MouseEvent&)
{
    setSourceRange(0.0f, 1.0f);
    hideMarkerPopup();
}

void SignalAnalysisView::setSnapshot(SignalDiagnostics::SignalSnapshot next)
{
    snapshot = std::move(next);
    repaint();
}

void SignalAnalysisView::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(black).withAlpha(0.74f));
    graphics.fillRoundedRectangle(bounds, 8.0f);
    graphics.setColour(juce::Colour(ivory).withAlpha(0.12f));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
    if (!snapshot.valid)
    {
        graphics.setColour(juce::Colour(dimIvory).withAlpha(0.58f));
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawText("RESULT MONITOR", getLocalBounds(),
                          juce::Justification::centred);
        return;
    }

    auto content = bounds.reduced(10.0f, 7.0f);
    auto spectrumArea = content.removeFromLeft(content.getWidth() * 0.62f);
    content.removeFromLeft(10.0f);
    auto phaseArea = content.removeFromLeft(juce::jmin(content.getWidth() * 0.48f,
                                                       content.getHeight()));
    content.removeFromLeft(10.0f);
    auto meterArea = content;

    graphics.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    graphics.setColour(juce::Colour(dimIvory));
    graphics.drawText("SPECTRUM  SOURCE / RESULT", spectrumArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    for (int line = 1; line < 4; ++line)
    {
        const auto y = spectrumArea.getY() + spectrumArea.getHeight() * line / 4.0f;
        graphics.setColour(juce::Colour(ivory).withAlpha(0.09f));
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
    drawSpectrum(snapshot.sourceSpectrum, juce::Colour(violet), 1.0f);
    drawSpectrum(snapshot.outputSpectrum, juce::Colour(mint), 1.6f);

    graphics.setColour(juce::Colour(dimIvory));
    graphics.drawText("PHASE", phaseArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    const auto phaseSquare = phaseArea.withSizeKeepingCentre(
        juce::jmin(phaseArea.getWidth(), phaseArea.getHeight()),
        juce::jmin(phaseArea.getWidth(), phaseArea.getHeight()));
    graphics.setColour(juce::Colour(ivory).withAlpha(0.24f));
    graphics.drawRect(phaseSquare, 1.0f);
    graphics.drawLine(phaseSquare.getCentreX(), phaseSquare.getY(),
                      phaseSquare.getCentreX(), phaseSquare.getBottom());
    graphics.drawLine(phaseSquare.getX(), phaseSquare.getCentreY(),
                      phaseSquare.getRight(), phaseSquare.getCentreY());
    graphics.setColour(juce::Colour(mint).withAlpha(0.78f));
    for (const auto& point : snapshot.phasePoints)
    {
        const auto side = 0.5f * (point[0] - point[1]);
        const auto mid = 0.5f * (point[0] + point[1]);
        const auto x = phaseSquare.getCentreX() + side * phaseSquare.getWidth() * 0.45f;
        const auto y = phaseSquare.getCentreY() - mid * phaseSquare.getHeight() * 0.45f;
        graphics.fillEllipse(x - 0.75f, y - 0.75f, 1.5f, 1.5f);
    }

    graphics.setColour(juce::Colour(dimIvory));
    graphics.drawText("CORRELATION", meterArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    auto track = meterArea.removeFromTop(9.0f).reduced(0.0f, 2.0f);
    graphics.setColour(juce::Colour(ivory).withAlpha(0.15f));
    graphics.fillRoundedRectangle(track, 2.5f);
    const auto correlation = juce::jlimit(-1.0f, 1.0f, snapshot.outputCorrelation);
    const auto markerX = track.getX() + 0.5f * (correlation + 1.0f) * track.getWidth();
    graphics.setColour(correlation < 0.0f ? juce::Colour(coral)
                                          : juce::Colour(sulfur));
    graphics.fillRoundedRectangle(markerX - 2.0f, track.getY() - 2.0f,
                                  4.0f, track.getHeight() + 4.0f, 2.0f);
    graphics.setColour(juce::Colour(ivory));
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText(juce::String(correlation, 2), meterArea.removeFromTop(18.0f),
                      juce::Justification::centredLeft);
    const auto imbalance = snapshot.outputImbalanceDb;
    const auto side = imbalance > 0.25f ? "L " : imbalance < -0.25f ? "R " : "C ";
    graphics.setColour(juce::Colour(dimIvory));
    graphics.drawText("POSITION  " + juce::String(side)
                          + juce::String(std::abs(imbalance), 1) + " dB",
                      meterArea.removeFromTop(16.0f), juce::Justification::centredLeft);
}

void TailActionButton::ensureAnimationRunning()
{
    if (!isTimerRunning())
    {
        lastFrameMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
}

void TailActionButton::mouseEnter(const juce::MouseEvent& event)
{
    hoverTarget = 1.0f;
    ensureAnimationRunning();
    juce::TextButton::mouseEnter(event);
}

void TailActionButton::mouseExit(const juce::MouseEvent& event)
{
    hoverTarget = 0.0f;
    pressTarget = 0.0f;
    ensureAnimationRunning();
    juce::TextButton::mouseExit(event);
}

void TailActionButton::mouseDown(const juce::MouseEvent& event)
{
    pressTarget = 1.0f;
    ensureAnimationRunning();
    juce::TextButton::mouseDown(event);
}

void TailActionButton::mouseUp(const juce::MouseEvent& event)
{
    juce::TextButton::mouseUp(event);
    pressTarget = 0.0f;
    ensureAnimationRunning();
}

void TailActionButton::timerCallback()
{
    const auto delta = deltaSecondsSince(lastFrameMs);
    hoverProgress = LoopSurgeonUi::advanceTransition(
        hoverProgress, hoverTarget, delta, hoverTarget > hoverProgress ? 0.20f : 0.25f);
    pressProgress = LoopSurgeonUi::advanceTransition(
        pressProgress, pressTarget, delta, pressTarget > pressProgress ? 0.12f : 0.18f);
    repaint();
    if (isSettled(hoverProgress, hoverTarget)
        && isSettled(pressProgress, pressTarget))
        stopTimer();
}

void TailActionButton::paintButton(juce::Graphics& graphics, const bool highlighted, const bool down)
{
    if (auto* skin = dynamic_cast<LoopSurgeonLookAndFeel*>(&getLookAndFeel()))
    {
        skin->drawButtonBackground(graphics, *this, juce::Colour(charcoal), highlighted, down);
        skin->drawButtonText(graphics, *this, highlighted, down);
    }
}

RenderDragButton::RenderDragButton()
    : TailActionButton("DRAG TO DAW")
{
    setTooltip("Render a 24-bit WAV and drag it to the DAW timeline");
}

void RenderDragButton::mouseDown(const juce::MouseEvent& event)
{
    dragStarted = false;
    TailActionButton::mouseDown(event);
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
    TailActionButton::mouseUp(event);
    dragStarted = false;
}

void IllustratedRotaryControl::ensureAnimationRunning()
{
    if (!isTimerRunning())
    {
        lastFrameMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
}

void IllustratedRotaryControl::mouseEnter(const juce::MouseEvent& event)
{
    hoverTarget = 1.0f;
    ensureAnimationRunning();
    juce::Slider::mouseEnter(event);
}

void IllustratedRotaryControl::mouseExit(const juce::MouseEvent& event)
{
    hoverTarget = 0.0f;
    pressTarget = 0.0f;
    ensureAnimationRunning();
    juce::Slider::mouseExit(event);
}

void IllustratedRotaryControl::mouseDown(const juce::MouseEvent& event)
{
    pressTarget = 1.0f;
    ensureAnimationRunning();
    juce::Slider::mouseDown(event);
}

void IllustratedRotaryControl::mouseUp(const juce::MouseEvent& event)
{
    juce::Slider::mouseUp(event);
    pressTarget = 0.0f;
    ensureAnimationRunning();
}

void IllustratedRotaryControl::timerCallback()
{
    const auto delta = deltaSecondsSince(lastFrameMs);
    hoverProgress = LoopSurgeonUi::advanceTransition(
        hoverProgress, hoverTarget, delta, hoverTarget > hoverProgress ? 0.20f : 0.26f);
    pressProgress = LoopSurgeonUi::advanceTransition(
        pressProgress, pressTarget, delta, pressTarget > pressProgress ? 0.12f : 0.17f);
    repaint();
    if (isSettled(hoverProgress, hoverTarget)
        && isSettled(pressProgress, pressTarget))
        stopTimer();
}

juce::String IllustratedRotaryControl::getFixedValueText()
{
    if (getName() == "FINAL LENGTH")
        return getValue() <= 0.0 ? "SELECTION" : juce::String(getValue(), 1) + " s";
    if (getName() == "LENGTH")
        return juce::String(getValue(), 1) + " s";
    if (getName() == "SEAM")
        return juce::String(getValue(), 1) + " ms";
    if (getName() == "LOOP START" || getName() == "STABILITY"
        || getName() == "CRUSH" || getName() == "TRANSFORM"
        || getName() == "AUDITION")
        return juce::String(getValue() * 100.0, 1) + "%";
    return getTextFromValue(getValue());
}

void IllustratedRotaryControl::paint(juce::Graphics& graphics)
{
    const auto* skin = dynamic_cast<const LoopSurgeonLookAndFeel*>(&getLookAndFeel());
    if (skin == nullptr) return;
    const auto bounds = getLocalBounds().toFloat();
    const auto alpha = isEnabled() ? 1.0f : 0.4f;
    const auto centre = juce::Point<float>(bounds.getCentreX(), 73.0f);
    constexpr float radius = 33.0f;
    const auto normalised = static_cast<float>(valueToProportionOfLength(getValue()));
    const auto angle = juce::jmap(normalised, -2.35f, 2.35f);
    juce::Path track, amount;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, -2.35f, 2.35f, true);
    amount.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, -2.35f, angle, true);
    graphics.setColour(juce::Colour(LoopSurgeonTheme::line).withAlpha(alpha));
    graphics.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    graphics.setColour(juce::Colour(sulfur).withAlpha(alpha));
    graphics.strokePath(amount, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto inner = centre.getPointOnCircumference(18.0f, angle);
    const auto outer = centre.getPointOnCircumference(26.0f, angle);
    graphics.drawLine({inner, outer}, 2.0f);
    graphics.setColour(juce::Colour(dimIvory).withAlpha(alpha));
    graphics.setFont(skin->getDisplayFont(15.0f));
    graphics.drawText(getName(), bounds.withHeight(24.0f), juce::Justification::centred);
    graphics.setColour(juce::Colour(ivory).withAlpha(alpha));
    graphics.setFont(skin->getHandFont(23.0f));
    graphics.drawText(getFixedValueText(), bounds.withY(116.0f).withHeight(30.0f),
                      juce::Justification::centred);
}

juce::String IllustratedRotaryControl::getTextFromValue(const double value) const
{
    if (getName() == "LENGTH" || getName() == "FINAL LENGTH")
        return getName() + "  |  " + juce::String(value, 1) + " s";
    if (getName() == "SEAM")
        return "SEAM  |  " + juce::String(value, 1) + " ms";
    return getName() + "  |  " + juce::String(value * 100.0, 1) + "%";
}

void GenerateArtworkButton::paintButton(juce::Graphics& graphics, const bool highlighted, const bool down)
{
    const auto* skin = dynamic_cast<const LoopSurgeonLookAndFeel*>(&getLookAndFeel());
    if (skin == nullptr) return;
    const auto bounds = getLocalBounds().toFloat();
    drawMobius(graphics, bounds.reduced(28.0f, 0.0f).withHeight(230.0f));
    const auto action = bounds.withY(bounds.getBottom() - 56.0f).withHeight(52.0f).reduced(1.0f);
    graphics.setColour(juce::Colour(sulfur).withMultipliedBrightness(down ? 0.82f : highlighted ? 1.08f : 1.0f));
    graphics.fillRoundedRectangle(action, 6.0f);
    graphics.setColour(juce::Colour(black));
    graphics.setFont(skin->getDisplayFont(18.0f));
    graphics.drawText(working ? "GENERATING..." : getButtonText().toUpperCase(),
                      action.translated(0.0f, down ? 1.0f : 0.0f), juce::Justification::centred);
}

void ArtworkChoiceButton::ensureAnimationRunning()
{
    if (!isTimerRunning())
    {
        lastFrameMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
}

void ArtworkChoiceButton::mouseEnter(const juce::MouseEvent& event)
{
    hoverTarget = 1.0f;
    ensureAnimationRunning();
    juce::TextButton::mouseEnter(event);
}

void ArtworkChoiceButton::mouseExit(const juce::MouseEvent& event)
{
    hoverTarget = 0.0f;
    pressTarget = 0.0f;
    ensureAnimationRunning();
    juce::TextButton::mouseExit(event);
}

void ArtworkChoiceButton::mouseDown(const juce::MouseEvent& event)
{
    pressTarget = 1.0f;
    ensureAnimationRunning();
    juce::TextButton::mouseDown(event);
}

void ArtworkChoiceButton::mouseUp(const juce::MouseEvent& event)
{
    juce::TextButton::mouseUp(event);
    pressTarget = 0.0f;
    ensureAnimationRunning();
}

void ArtworkChoiceButton::timerCallback()
{
    const auto delta = deltaSecondsSince(lastFrameMs);
    const auto selectedTarget = getToggleState() ? 1.0f : 0.0f;
    hoverProgress = LoopSurgeonUi::advanceTransition(
        hoverProgress, hoverTarget, delta, 0.19f);
    pressProgress = LoopSurgeonUi::advanceTransition(
        pressProgress, pressTarget, delta, pressTarget > pressProgress ? 0.12f : 0.16f);
    selectedProgress = LoopSurgeonUi::advanceTransition(
        selectedProgress, selectedTarget, delta, 0.20f);
    repaint();
    if (isSettled(hoverProgress, hoverTarget)
        && isSettled(pressProgress, pressTarget)
        && isSettled(selectedProgress, selectedTarget))
        stopTimer();
}

void ArtworkChoiceButton::paintButton(juce::Graphics& graphics, const bool highlighted, const bool down)
{
    const auto* skin = dynamic_cast<const LoopSurgeonLookAndFeel*>(&getLookAndFeel());
    if (skin == nullptr) return;
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const auto selected = getToggleState();
    const auto alpha = isEnabled() ? 1.0f : 0.35f;
    graphics.setColour(juce::Colour(selected ? LoopSurgeonTheme::selected : LoopSurgeonTheme::inset));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    graphics.setColour(juce::Colour(selected ? sulfur : LoopSurgeonTheme::line).withAlpha(alpha));
    graphics.drawRoundedRectangle(bounds, 5.0f, highlighted || down ? 1.5f : 1.0f);
    graphics.setColour(juce::Colour(selected ? sulfur : ivory).withAlpha(alpha));
    graphics.setFont(skin->getDisplayFont(16.0f));
    graphics.drawText(stateLabel, bounds, juce::Justification::centred);
}

void IllustratedStripControl::ensureAnimationRunning()
{
    if (!isTimerRunning())
    {
        lastFrameMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
}

juce::Rectangle<float> IllustratedStripControl::trackBounds() const
{
    const auto extra = pathKind == LoopSurgeonUi::StripPathKind::extra;
    const auto left = extra ? 126.0f : 240.0f;
    return { left, getHeight() * 0.5f - 2.0f, juce::jmax(20.0f, getWidth() - left - 8.0f), 4.0f };
}

juce::Point<float> IllustratedStripControl::pointOnTrack(const float normalised) const
{
    // One geometry drives the actual bitmap, hit mapping and every handle position.
    const auto rail = trackBounds();
    const auto inset = rail.getHeight() * 0.5f;
    return { juce::jmap(juce::jlimit(0.0f, 1.0f, normalised),
                        rail.getX() + inset, rail.getRight() - inset),
             rail.getCentreY() };
}

juce::Point<float> IllustratedStripControl::tangentOnTrack(const float normalised) const
{
    juce::ignoreUnused(normalised);
    return { 1.0f, 0.0f };
}

void IllustratedStripControl::updateValueFromPosition(juce::Point<float> position)
{
    const auto left = pointOnTrack(0.0f).x;
    const auto right = pointOnTrack(1.0f).x;
    const auto bestT = juce::jlimit(0.0f, 1.0f,
        (position.x - left) / juce::jmax(1.0f, right - left));
    setValue(proportionOfLengthToValue(bestT), juce::sendNotificationSync);
}

void IllustratedStripControl::mouseEnter(const juce::MouseEvent& event)
{
    hoverTarget = 1.0f;
    ensureAnimationRunning();
    juce::Slider::mouseEnter(event);
}

void IllustratedStripControl::mouseExit(const juce::MouseEvent& event)
{
    hoverTarget = 0.0f;
    pressTarget = 0.0f;
    ensureAnimationRunning();
    juce::Slider::mouseExit(event);
}

void IllustratedStripControl::mouseDown(const juce::MouseEvent& event)
{
    pressTarget = 1.0f;
    ensureAnimationRunning();
    setSliderSnapsToMousePosition(false);
    juce::Slider::mouseDown(event);
    updateValueFromPosition(event.position);
}

void IllustratedStripControl::mouseDrag(const juce::MouseEvent& event)
{
    updateValueFromPosition(event.position);
}

void IllustratedStripControl::mouseUp(const juce::MouseEvent& event)
{
    juce::Slider::mouseUp(event);
    pressTarget = 0.0f;
    ensureAnimationRunning();
}

void IllustratedStripControl::timerCallback()
{
    const auto delta = deltaSecondsSince(lastFrameMs);
    hoverProgress = LoopSurgeonUi::advanceTransition(
        hoverProgress, hoverTarget, delta, 0.20f);
    pressProgress = LoopSurgeonUi::advanceTransition(
        pressProgress, pressTarget, delta, pressTarget > pressProgress ? 0.12f : 0.18f);
    repaint();
    if (isSettled(hoverProgress, hoverTarget)
        && isSettled(pressProgress, pressTarget))
        stopTimer();
}

juce::String IllustratedStripControl::getFixedValueText()
{
    if (getName() == "MOTION")
        return juce::StringArray { "FLOW", "DRIFT", "FRACTURE" }
            [juce::jlimit(0, 2, juce::roundToInt(getValue()))];
    if (getName() == "EXTRA MIX")
        return "EXTRA MIX  " + juce::String(getValue() * 100.0, 1) + "%";
    return juce::String(getValue() * 100.0, 1) + "%";
}

juce::String IllustratedStripControl::getTextFromValue(const double value) const
{
    if (getName() == "MOTION")
    {
        const auto index = juce::jlimit(0, 2, juce::roundToInt(value));
        return "MOTION  |  " + juce::StringArray { "FLOW", "DRIFT", "FRACTURE" }[index];
    }
    if (getName() == "EXTRA MIX")
        return "CHARACTER MIX  |  " + juce::String(value * 100.0, 1) + "%";
    return "JOIN POSITION  |  " + juce::String(value * 100.0, 1) + "%";
}

void IllustratedStripControl::paint(juce::Graphics& graphics)
{
    const auto* skin = dynamic_cast<const LoopSurgeonLookAndFeel*>(&getLookAndFeel());
    if (skin == nullptr) return;
    const auto extra = pathKind == LoopSurgeonUi::StripPathKind::extra;
    const auto alpha = isEnabled() ? 1.0f : 0.4f;
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(dimIvory).withAlpha(alpha));
    graphics.setFont(skin->getDisplayFont(14.0f));
    graphics.drawText(extra ? "MIX" : getName(), bounds.withWidth(extra ? 46.0f : 136.0f),
                      juce::Justification::centredLeft);
    graphics.setColour(juce::Colour(ivory).withAlpha(alpha));
    graphics.setFont(skin->getHandFont(18.0f));
    graphics.drawText(extra ? juce::String(getValue() * 100.0, 0) + "%" : getFixedValueText(),
        bounds.withX(extra ? 48.0f : 144.0f).withWidth(extra ? 65.0f : 88.0f),
        juce::Justification::centredLeft);
    const auto track = trackBounds();
    const auto point = pointOnTrack(static_cast<float>(valueToProportionOfLength(getValue())));
    graphics.setColour(juce::Colour(LoopSurgeonTheme::line).withAlpha(alpha));
    graphics.fillRoundedRectangle(track, 2.0f);
    graphics.setColour(juce::Colour(sulfur).withAlpha(alpha));
    graphics.fillRoundedRectangle(track.withWidth(juce::jmax(0.0f, point.x - track.getX())), 2.0f);
    graphics.setColour(juce::Colour(ivory).withAlpha(alpha));
    graphics.fillRoundedRectangle(point.x - 4.0f, point.y - 10.0f, 8.0f, 20.0f, 3.0f);
}
