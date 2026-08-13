#include "PluginEditor.h"

#include "BinaryData.h"

#include <cmath>
#include <utility>

namespace
{
constexpr auto black = 0xff101014;
constexpr auto charcoal = 0xff222128;
constexpr auto sulfur = 0xfff0ee63;
constexpr auto mint = 0xff57e0b5;
constexpr auto coral = 0xffff625f;
constexpr auto violet = 0xff8367e8;
constexpr auto ivory = 0xfff5f0de;
constexpr auto dimIvory = 0xffa8a3a0;

juce::Colour colourForButton(const juce::Button& button,
                             const juce::Colour fallback)
{
    const auto id = button.getComponentID();
    if (id == "primaryAction")
        return juce::Colour(coral);
    if (id == "transport")
        return button.getToggleState() ? juce::Colour(coral) : juce::Colour(mint);
    if (id == "segmented")
        return button.getToggleState() ? juce::Colour(sulfur) : juce::Colour(charcoal);
    if (id == "headerAction" || id == "textAction")
        return juce::Colours::transparentBlack;
    if (id == "delivery")
        return juce::Colour(charcoal);
    return fallback;
}
}

LoopSurgeonLookAndFeel::LoopSurgeonLookAndFeel()
{
    fieldImage = juce::ImageCache::getFromMemory(
        LoopSurgeonAssets::spliceribbonfunctional_png,
        LoopSurgeonAssets::spliceribbonfunctional_pngSize);
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
    juce::Graphics& graphics, juce::Button& button, const juce::Colour& baseColour,
    const bool highlighted, const bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto id = button.getComponentID();
    auto colour = colourForButton(button, baseColour);
    if (id == "headerAction" || id == "textAction")
    {
        if (highlighted && button.isEnabled())
        {
            graphics.setColour(juce::Colour(ivory).withAlpha(0.08f));
            graphics.fillRoundedRectangle(bounds, 5.0f);
        }
        if (id == "headerAction")
        {
            graphics.setColour(juce::Colour(ivory).withAlpha(0.22f));
            graphics.drawLine(bounds.getX(), bounds.getBottom(), bounds.getRight(),
                              bounds.getBottom(), 1.0f);
        }
        return;
    }

    if (id == "primaryAction")
    {
        const auto square = bounds.withSizeKeepingCentre(
            juce::jmin(bounds.getWidth(), bounds.getHeight()),
            juce::jmin(bounds.getWidth(), bounds.getHeight()));
        graphics.setColour(juce::Colour(coral).withAlpha(
            button.isEnabled() ? (down ? 0.76f : 0.92f) : 0.28f));
        graphics.drawEllipse(square.reduced(square.getWidth() * 0.21f), 4.0f);
        if (highlighted && button.isEnabled())
        {
            graphics.setColour(juce::Colour(sulfur).withAlpha(0.22f));
            graphics.fillEllipse(square.reduced(square.getWidth() * 0.25f));
        }
        return;
    }

    if (id == "tailAction")
    {
        if (highlighted && button.isEnabled())
        {
            graphics.setColour(juce::Colour(sulfur).withAlpha(0.78f));
            graphics.drawEllipse(bounds.reduced(5.0f, 1.0f), 2.0f);
        }
        return;
    }

    if (highlighted && button.isEnabled())
        colour = colour.brighter(0.08f);
    if (down)
        colour = colour.darker(0.12f);
    juce::Path patch;
    patch.startNewSubPath(bounds.getX() + 5.0f, bounds.getY());
    patch.lineTo(bounds.getRight() - 2.0f, bounds.getY() + 2.0f);
    patch.lineTo(bounds.getRight(), bounds.getBottom() - 7.0f);
    patch.lineTo(bounds.getRight() - 8.0f, bounds.getBottom());
    patch.lineTo(bounds.getX() + 2.0f, bounds.getBottom() - 2.0f);
    patch.lineTo(bounds.getX(), bounds.getY() + 8.0f);
    patch.closeSubPath();
    graphics.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 0.94f : 0.30f));
    graphics.fillPath(patch);
    graphics.setColour(juce::Colour(black).withMultipliedAlpha(
        button.isEnabled() ? 0.92f : 0.25f));
    graphics.strokePath(patch, juce::PathStrokeType(1.4f));
}

void LoopSurgeonLookAndFeel::drawButtonText(
    juce::Graphics& graphics, juce::TextButton& button, const bool, const bool down)
{
    const auto id = button.getComponentID();
    const auto darkText = id == "segmented" && button.getToggleState();
    graphics.setColour(juce::Colour(darkText ? black : ivory).withMultipliedAlpha(
        button.isEnabled() ? 1.0f : 0.34f));
    graphics.setFont(getTextButtonFont(button, button.getHeight()));
    auto area = button.getLocalBounds().reduced(8, 3);
    if (down)
        area.translate(0, 1);
    graphics.drawFittedText(button.getButtonText(), area,
                            juce::Justification::centred, 1, 0.75f);
}

void LoopSurgeonLookAndFeel::drawComboBox(
    juce::Graphics& graphics, const int width, const int height, const bool down,
    const int, const int, const int, const int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.5f, down ? 1.5f : 0.5f,
                                         static_cast<float>(width) - 1.0f,
                                         static_cast<float>(height) - 1.0f);
    juce::Path tab;
    tab.startNewSubPath(bounds.getX() + 5.0f, bounds.getY() + 2.0f);
    tab.lineTo(bounds.getRight() - 15.0f, bounds.getY());
    tab.lineTo(bounds.getRight(), bounds.getCentreY());
    tab.lineTo(bounds.getRight() - 12.0f, bounds.getBottom());
    tab.lineTo(bounds.getX(), bounds.getBottom() - 2.0f);
    tab.closeSubPath();
    graphics.setColour(juce::Colour(black).withMultipliedAlpha(
        box.isEnabled() ? 0.90f : 0.38f));
    graphics.fillPath(tab);
    graphics.setColour(juce::Colour(coral).withMultipliedAlpha(
        box.isEnabled() ? 1.0f : 0.28f));
    graphics.fillRect(bounds.getX(), bounds.getY() + 5.0f, 4.0f,
                      bounds.getHeight() - 10.0f);

    const auto centre = juce::Point<float>(bounds.getRight() - 15.0f,
                                           bounds.getCentreY());
    juce::Path arrow;
    arrow.startNewSubPath(centre.x - 5.0f, centre.y - 2.0f);
    arrow.lineTo(centre.x, centre.y + 3.0f);
    arrow.lineTo(centre.x + 5.0f, centre.y - 2.0f);
    graphics.setColour(juce::Colour(mint).withMultipliedAlpha(
        box.isEnabled() ? 1.0f : 0.28f));
    graphics.strokePath(arrow, juce::PathStrokeType(2.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LoopSurgeonLookAndFeel::drawRotarySlider(
    juce::Graphics& graphics, const int x, const int y, const int width,
    const int height, const float sliderPosition, const float rotaryStartAngle,
    const float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
        static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto diameter = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 2.0f;
    const auto frame = juce::Rectangle<float>(diameter, diameter)
                           .withCentre(bounds.getCentre());
    const auto centre = frame.getCentre();
    const auto angle = rotaryStartAngle
        + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto alpha = slider.isEnabled() ? 1.0f : 0.28f;

    const auto cap = frame.reduced(diameter * 0.20f);
    graphics.setColour(juce::Colour(black).withAlpha(0.74f * alpha));
    graphics.fillEllipse(cap.expanded(3.0f));
    graphics.setColour(juce::Colour(violet).withAlpha(alpha));
    graphics.fillEllipse(cap);
    graphics.setColour(juce::Colour(ivory).withAlpha(0.82f * alpha));
    graphics.drawEllipse(cap, 2.0f);

    const auto ring = cap.expanded(diameter * 0.055f);
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, ring.getWidth() * 0.5f,
                      ring.getHeight() * 0.5f, 0.0f,
                      rotaryStartAngle, angle, true);
    graphics.setColour(juce::Colour(sulfur).withAlpha(alpha));
    graphics.strokePath(arc, juce::PathStrokeType(4.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto markerStart = centre
        + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (diameter * 0.05f);
    const auto markerEnd = centre
        + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (diameter * 0.15f);
    graphics.setColour(juce::Colour(ivory).withAlpha(alpha));
    graphics.drawLine({ markerStart, markerEnd }, 3.0f);
}

void LoopSurgeonLookAndFeel::drawLinearSlider(
    juce::Graphics& graphics, const int x, const int y, const int width,
    const int height, const float sliderPosition, const float,
    const float, const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height,
                                               sliderPosition, 0.0f, 0.0f,
                                               style, slider);
        return;
    }
    const auto alpha = slider.isEnabled() ? 1.0f : 0.28f;
    const auto centreY = static_cast<float>(y + height / 2);
    const auto startX = static_cast<float>(x + 4);
    const auto endX = static_cast<float>(x + width - 4);
    graphics.setColour(juce::Colour(ivory).withAlpha(0.16f * alpha));
    graphics.drawLine(startX, centreY, endX, centreY, 5.0f);
    graphics.setColour(juce::Colour(mint).withAlpha(alpha));
    graphics.drawLine(startX, centreY, sliderPosition, centreY, 5.0f);
    graphics.setColour(juce::Colour(sulfur).withAlpha(alpha));
    graphics.fillEllipse(sliderPosition - 7.0f, centreY - 7.0f, 14.0f, 14.0f);
    graphics.setColour(juce::Colour(black).withAlpha(alpha));
    graphics.drawEllipse(sliderPosition - 7.0f, centreY - 7.0f, 14.0f, 14.0f, 1.5f);
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
    if (dynamic_cast<juce::Slider*>(label.getParentComponent()) == nullptr)
    {
        juce::LookAndFeel_V4::drawLabel(graphics, label);
        return;
    }
    if (!label.isBeingEdited())
    {
        const auto bounds = label.getLocalBounds().toFloat().reduced(5.0f, 1.0f);
        graphics.setColour(juce::Colour(ivory).withMultipliedAlpha(
            label.isEnabled() ? 0.82f : 0.28f));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(juce::Colour(black).withMultipliedAlpha(
            label.isEnabled() ? 0.96f : 0.34f));
        graphics.setFont(getHandFont(12.0f));
        graphics.drawFittedText(label.getText(), label.getLocalBounds().reduced(2, 0),
                                juce::Justification::centred, 1, 0.75f);
    }
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
    bounds = bounds.reduced(14.0f, 12.0f);

    if (peaks.empty())
    {
        graphics.setColour(juce::Colour(ivory).withAlpha(0.82f));
        graphics.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        graphics.drawText("DROP OR LOAD AUDIO", getLocalBounds().reduced(36),
                          juce::Justification::centred);
        return;
    }

    for (int division = 1; division < 8; ++division)
    {
        const auto x = bounds.getX() + bounds.getWidth()
            * static_cast<float>(division) / 8.0f;
        graphics.setColour(juce::Colour(black).withAlpha(division == 4 ? 0.22f : 0.09f));
        graphics.drawVerticalLine(juce::roundToInt(x), bounds.getY() + 18.0f,
                                  bounds.getBottom() - 18.0f);
    }
    graphics.setColour(juce::Colour(black).withAlpha(0.18f));
    graphics.drawHorizontalLine(juce::roundToInt(bounds.getCentreY()),
                                bounds.getX(), bounds.getRight());

    const auto sourceX = bounds.getX() + bounds.getWidth() * sourceIn;
    const auto sourceRight = bounds.getX() + bounds.getWidth() * sourceOut;
    graphics.setColour(juce::Colour(sulfur).withAlpha(0.18f));
    graphics.fillRect(juce::Rectangle<float>(sourceX, bounds.getY(),
                                              sourceRight - sourceX,
                                              bounds.getHeight()));
    graphics.setColour(juce::Colour(black).withAlpha(0.22f));
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
    graphics.setColour(juce::Colour(ivory).withAlpha(0.88f));
    graphics.strokePath(waveform, juce::PathStrokeType(1.0f));

    const auto drawMarker = [&] (const float proportion, const juce::String& label,
                                 const juce::Colour colour)
    {
        const auto x = bounds.getX() + bounds.getWidth() * proportion;
        graphics.setColour(colour);
        graphics.drawLine(x, bounds.getY(), x, bounds.getBottom(), 2.0f);
        juce::Path triangle;
        triangle.addTriangle(x - 5.0f, bounds.getY(), x + 5.0f, bounds.getY(),
                             x, bounds.getY() + 8.0f);
        graphics.fillPath(triangle);
        const auto labelWidth = label.length() > 9 ? 80.0f : 68.0f;
        const auto labelX = juce::jlimit(bounds.getX() + 3.0f,
            bounds.getRight() - labelWidth - 3.0f, x - labelWidth * 0.5f);
        juce::Rectangle<float> tag(labelX, bounds.getY() + 7.0f,
                                   labelWidth, 18.0f);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(tag, 3.0f);
        graphics.setColour(juce::Colour(black));
        graphics.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        graphics.drawText(label, tag, juce::Justification::centred);
    };

    drawMarker(sourceIn, "SOURCE IN", juce::Colour(mint));
    drawMarker(sourceOut, "SOURCE OUT", juce::Colour(mint));
    if (rotation >= 0.0f)
        drawMarker(rotation, "LOOP START", juce::Colour(coral));
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
        dragTarget = inDistance <= outDistance ? DragTarget::sourceIn
                                               : DragTarget::sourceOut;
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

RenderDragButton::RenderDragButton()
    : juce::TextButton("DRAG TO DAW")
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
