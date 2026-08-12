#include "PluginEditor.h"

#include "BinaryData.h"

#include <utility>

namespace
{
constexpr auto ink = 0xff191416;
constexpr auto paper = 0xffe7ddbf;
constexpr auto paperLight = 0xfff2e9cf;
constexpr auto screenMint = 0xffb9c8ac;
constexpr auto acidYellow = 0xffe4c72b;
constexpr auto fadedCoral = 0xffd96d5e;
constexpr auto dustyViolet = 0xff76608f;
constexpr auto softInk = 0xff544b47;

juce::Path makeWonkyPanel(const juce::Rectangle<float> bounds)
{
    const auto x = bounds.getX();
    const auto y = bounds.getY();
    const auto r = bounds.getRight();
    const auto b = bounds.getBottom();
    const auto n = juce::jmin(5.0f, bounds.getHeight() * 0.12f);
    juce::Path path;
    path.startNewSubPath(x + n, y + 1.0f);
    path.lineTo(r - n * 0.6f, y);
    path.quadraticTo(r, y + n * 0.7f, r - 1.0f, y + n * 1.5f);
    path.lineTo(r, b - n);
    path.quadraticTo(r - n * 0.5f, b, r - n * 1.4f, b - 1.0f);
    path.lineTo(x + n * 0.7f, b);
    path.quadraticTo(x, b - n * 0.6f, x + 1.0f, b - n * 1.5f);
    path.lineTo(x, y + n);
    path.quadraticTo(x + n * 0.4f, y, x + n, y + 1.0f);
    path.closeSubPath();
    return path;
}
}

LoopSurgeonLookAndFeel::LoopSurgeonLookAndFeel()
{
    machineSkin = juce::ImageCache::getFromMemory(
        LoopSurgeonAssets::loopmachineskin_png,
        LoopSurgeonAssets::loopmachineskin_pngSize);
    knobImage = juce::ImageCache::getFromMemory(
        LoopSurgeonAssets::loopknob_png,
        LoopSurgeonAssets::loopknob_pngSize);
    generateButtonImage = juce::ImageCache::getFromMemory(
        LoopSurgeonAssets::generatebutton_png,
        LoopSurgeonAssets::generatebutton_pngSize);
    displayTypeface = juce::Typeface::createSystemTypefaceFor(
        LoopSurgeonAssets::BowlbyOneSCRegular_ttf,
        LoopSurgeonAssets::BowlbyOneSCRegular_ttfSize);
    handTypeface = juce::Typeface::createSystemTypefaceFor(
        LoopSurgeonAssets::SpecialEliteRegular_ttf,
        LoopSurgeonAssets::SpecialEliteRegular_ttfSize);

    setColour(juce::Label::textColourId, juce::Colour(ink));
    setColour(juce::TextButton::textColourOffId, juce::Colour(ink));
    setColour(juce::TextButton::textColourOnId, juce::Colour(ink));
    setColour(juce::TextButton::buttonColourId, juce::Colour(paper));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(fadedCoral));
    setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::textColourId, juce::Colour(ink));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(ink));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(ink));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(paperLight));
    setColour(juce::PopupMenu::textColourId, juce::Colour(ink));
    setColour(juce::PopupMenu::highlightedBackgroundColourId,
              juce::Colour(acidYellow).withAlpha(0.72f));
}

void LoopSurgeonLookAndFeel::drawButtonBackground(
    juce::Graphics& graphics, juce::Button& button, const juce::Colour& baseColour,
    const bool highlighted, const bool down)
{
    juce::Graphics::ScopedSaveState state(graphics);
    auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
    if (down)
        bounds.translate(0.0f, 2.0f);

    if (button.getComponentID() == "primaryAction")
    {
        graphics.setOpacity(button.isEnabled() ? 1.0f : 0.42f);
        graphics.drawImage(generateButtonImage, bounds,
                           juce::RectanglePlacement::centred);
        if (highlighted && button.isEnabled())
        {
            graphics.setColour(juce::Colours::white.withAlpha(0.12f));
            graphics.fillEllipse(bounds.reduced(bounds.getWidth() * 0.23f));
        }
        return;
    }

    auto colour = button.getToggleState()
        ? juce::Colour(fadedCoral)
        : baseColour.interpolatedWith(juce::Colour(paperLight), 0.64f);
    if (highlighted)
        colour = colour.interpolatedWith(juce::Colour(acidYellow), 0.18f);
    const auto path = makeWonkyPanel(bounds);
    juce::ColourGradient fill(colour.brighter(0.08f), bounds.getX(), bounds.getY(),
                              colour.darker(0.10f), bounds.getRight(),
                              bounds.getBottom(), false);
    graphics.setGradientFill(fill);
    graphics.setOpacity(button.isEnabled() ? 1.0f : 0.38f);
    graphics.fillPath(path);
    graphics.setColour(juce::Colour(ink));
    graphics.strokePath(path, juce::PathStrokeType(1.7f));
    graphics.setColour(juce::Colour(softInk).withAlpha(0.38f));
    graphics.strokePath(makeWonkyPanel(bounds.reduced(3.0f)),
                        juce::PathStrokeType(0.8f));
}

void LoopSurgeonLookAndFeel::drawButtonText(
    juce::Graphics& graphics, juce::TextButton& button,
    const bool, const bool down)
{
    juce::Graphics::ScopedSaveState state(graphics);
    if (down)
        graphics.addTransform(juce::AffineTransform::translation(0.0f, 2.0f));
    graphics.setColour(juce::Colour(ink).withMultipliedAlpha(
        button.isEnabled() ? 1.0f : 0.38f));
    graphics.setFont(getTextButtonFont(button, button.getHeight()));
    graphics.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(7, 4),
                            juce::Justification::centred, 2, 0.82f);
}

void LoopSurgeonLookAndFeel::drawComboBox(
    juce::Graphics& graphics, const int width, const int height, const bool down,
    const int, const int, const int, const int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.5f, down ? 2.0f : 0.5f,
                                         static_cast<float>(width) - 1.0f,
                                         static_cast<float>(height) - 2.0f);
    const auto path = makeWonkyPanel(bounds);
    juce::ColourGradient fill(juce::Colour(paperLight).withAlpha(0.92f),
                              bounds.getX(), bounds.getY(),
                              juce::Colour(screenMint).withAlpha(0.72f),
                              bounds.getRight(), bounds.getBottom(), false);
    graphics.setGradientFill(fill);
    graphics.fillPath(path);
    graphics.setColour(juce::Colour(ink));
    graphics.strokePath(path, juce::PathStrokeType(box.isEnabled() ? 1.7f : 0.8f));

    const auto arrowX = bounds.getRight() - 16.0f;
    const auto arrowY = bounds.getCentreY();
    juce::Path arrow;
    arrow.startNewSubPath(arrowX - 5.0f, arrowY - 2.0f);
    arrow.lineTo(arrowX, arrowY + 3.0f);
    arrow.lineTo(arrowX + 5.0f, arrowY - 2.0f);
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
    const auto diameter = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.90f;
    auto knobBounds = juce::Rectangle<float>(diameter, diameter)
                          .withCentre(bounds.getCentre());
    const auto angle = rotaryStartAngle
        + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto radius = diameter * 0.53f;

    graphics.setColour(juce::Colour(ink).withAlpha(slider.isEnabled() ? 0.78f : 0.30f));
    for (int tick = 0; tick < 11; ++tick)
    {
        const auto tickAngle = rotaryStartAngle
            + (rotaryEndAngle - rotaryStartAngle) * static_cast<float>(tick) / 10.0f;
        const auto inner = bounds.getCentre()
            + juce::Point<float>(std::sin(tickAngle), -std::cos(tickAngle))
                  * (radius - 3.5f);
        const auto outer = bounds.getCentre()
            + juce::Point<float>(std::sin(tickAngle), -std::cos(tickAngle))
                  * (radius + (tick % 5 == 0 ? 4.0f : 1.5f));
        graphics.drawLine({ inner, outer }, tick % 5 == 0 ? 1.8f : 1.0f);
    }

    juce::Graphics::ScopedSaveState state(graphics);
    graphics.setOpacity(slider.isEnabled() ? 1.0f : 0.34f);
    graphics.addTransform(juce::AffineTransform::rotation(
        angle, knobBounds.getCentreX(), knobBounds.getCentreY()));
    graphics.drawImage(knobImage, knobBounds, juce::RectanglePlacement::centred);
}

void LoopSurgeonLookAndFeel::drawPopupMenuBackground(
    juce::Graphics& graphics, const int width, const int height)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
        static_cast<float>(width), static_cast<float>(height));
    juce::ColourGradient fill(juce::Colour(paperLight), 0.0f, 0.0f,
                              juce::Colour(screenMint).interpolatedWith(
                                  juce::Colour(paper), 0.62f),
                              bounds.getRight(), bounds.getBottom(), false);
    graphics.setGradientFill(fill);
    graphics.fillRect(bounds);
    graphics.setColour(juce::Colour(softInk).withAlpha(0.12f));
    for (int y = 5; y < height; y += 7)
        graphics.drawHorizontalLine(y, 0.0f, static_cast<float>(width));
    graphics.setColour(juce::Colour(ink));
    graphics.drawRect(bounds.reduced(0.5f), 1.0f);
}

void LoopSurgeonLookAndFeel::drawPopupMenuItem(
    juce::Graphics& graphics, const juce::Rectangle<int>& area,
    const bool separator, const bool active, const bool highlighted,
    const bool ticked, const bool hasSubMenu, const juce::String& text,
    const juce::String& shortcutText, const juce::Drawable* icon,
    const juce::Colour* textColour)
{
    juce::ignoreUnused(icon);
    if (separator)
    {
        graphics.setColour(juce::Colour(ink).withAlpha(0.32f));
        const auto centreY = static_cast<float>(area.getCentreY());
        graphics.drawLine(area.getX() + 8.0f, centreY,
                          area.getRight() - 8.0f, centreY, 1.0f);
        return;
    }

    auto item = area.toFloat().reduced(2.0f, 1.5f);
    if (highlighted && active)
    {
        graphics.setColour(juce::Colour(acidYellow).withAlpha(0.78f));
        graphics.fillPath(makeWonkyPanel(item));
        graphics.setColour(juce::Colour(ink).withAlpha(0.55f));
        graphics.strokePath(makeWonkyPanel(item), juce::PathStrokeType(1.0f));
    }

    auto textArea = area.reduced(12, 0);
    if (ticked)
    {
        juce::Path tick;
        const auto cx = static_cast<float>(textArea.getX() + 5);
        const auto cy = static_cast<float>(textArea.getCentreY());
        tick.startNewSubPath(cx - 5.0f, cy);
        tick.lineTo(cx - 1.0f, cy + 5.0f);
        tick.lineTo(cx + 7.0f, cy - 7.0f);
        graphics.setColour(juce::Colour(fadedCoral));
        graphics.strokePath(tick, juce::PathStrokeType(2.4f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    textArea.removeFromLeft(20);
    graphics.setFont(getPopupMenuFont());
    graphics.setColour((textColour != nullptr ? *textColour : juce::Colour(ink))
                           .withMultipliedAlpha(active ? 1.0f : 0.36f));
    auto shortcutArea = textArea.removeFromRight(shortcutText.isEmpty() ? 0 : 72);
    graphics.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);
    if (shortcutText.isNotEmpty())
        graphics.drawFittedText(shortcutText, shortcutArea,
                                juce::Justification::centredRight, 1);
    if (hasSubMenu)
    {
        const auto x = static_cast<float>(area.getRight() - 9);
        const auto y = static_cast<float>(area.getCentreY());
        juce::Path arrow;
        arrow.startNewSubPath(x - 3.0f, y - 4.0f);
        arrow.lineTo(x + 2.0f, y);
        arrow.lineTo(x - 3.0f, y + 4.0f);
        graphics.strokePath(arrow, juce::PathStrokeType(1.6f));
    }
}

juce::Label* LoopSurgeonLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setColour(juce::Label::textColourId, juce::Colour(ink));
    label->setColour(juce::Label::backgroundColourId,
                     juce::Colours::transparentBlack);
    label->setColour(juce::Label::outlineColourId,
                     juce::Colours::transparentBlack);
    label->setColour(juce::TextEditor::textColourId, juce::Colour(ink));
    label->setColour(juce::TextEditor::backgroundColourId,
                     juce::Colour(paperLight).withAlpha(0.94f));
    label->setColour(juce::TextEditor::outlineColourId, juce::Colour(ink));
    label->setJustificationType(juce::Justification::centred);
    return label;
}

void LoopSurgeonLookAndFeel::drawLabel(
    juce::Graphics& graphics, juce::Label& label)
{
    if (dynamic_cast<juce::Slider*>(label.getParentComponent()) == nullptr)
    {
        juce::LookAndFeel_V4::drawLabel(graphics, label);
        return;
    }

    const auto bounds = label.getLocalBounds().toFloat().reduced(1.0f);
    const auto path = makeWonkyPanel(bounds);
    graphics.setColour(juce::Colour(paperLight).withMultipliedAlpha(
        label.isEnabled() ? 0.90f : 0.48f));
    graphics.fillPath(path);
    graphics.setColour(juce::Colour(ink).withMultipliedAlpha(
        label.isEnabled() ? 0.88f : 0.38f));
    graphics.strokePath(path, juce::PathStrokeType(0.8f));
    if (!label.isBeingEdited())
    {
        graphics.setFont(getHandFont(12.0f));
        graphics.drawFittedText(label.getText(), label.getLocalBounds().reduced(3, 0),
                                juce::Justification::centred, 1, 0.72f);
    }
}

juce::Font LoopSurgeonLookAndFeel::getTextButtonFont(juce::TextButton& button,
                                                     const int buttonHeight)
{
    if (button.getComponentID() == "primaryAction")
        return getDisplayFont(juce::jlimit(13.0f, 17.0f,
            static_cast<float>(buttonHeight) * 0.15f));
    return getHandFont(juce::jlimit(12.0f, 15.0f,
        static_cast<float>(buttonHeight) * 0.42f));
}

juce::Font LoopSurgeonLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return getHandFont(14.5f);
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

    for (int division = 1; division < 8; ++division)
    {
        const auto x = bounds.getX() + bounds.getWidth() * static_cast<float>(division) / 8.0f;
        graphics.setColour(juce::Colour(ink).withAlpha(division == 4 ? 0.22f : 0.11f));
        graphics.drawVerticalLine(juce::roundToInt(x), bounds.getY() + 24.0f,
                                  bounds.getBottom() - 24.0f);
    }
    graphics.setColour(juce::Colour(ink).withAlpha(0.22f));
    graphics.drawHorizontalLine(juce::roundToInt(bounds.getCentreY()),
                                bounds.getX(), bounds.getRight());

    if (peaks.empty())
    {
        graphics.setColour(juce::Colour(softInk).withAlpha(0.78f));
        graphics.setFont(juce::FontOptions(15.0f));
        graphics.drawText("DROP OR LOAD AUDIO",
                          getLocalBounds().reduced(24), juce::Justification::centred);
        return;
    }

    const auto sourceX = bounds.getX() + bounds.getWidth() * sourceIn;
    const auto sourceRight = bounds.getX() + bounds.getWidth() * sourceOut;
    juce::ColourGradient selection(juce::Colour(acidYellow).withAlpha(0.12f),
                                   sourceX, bounds.getY(),
                                   juce::Colour(fadedCoral).withAlpha(0.10f),
                                   sourceRight, bounds.getBottom(), false);
    graphics.setGradientFill(selection);
    graphics.fillRect(juce::Rectangle<float>(sourceX, bounds.getY(),
                                              sourceRight - sourceX,
                                              bounds.getHeight()));
    graphics.setColour(juce::Colour(ink).withAlpha(0.16f));
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
    graphics.setColour(juce::Colour(ink).withAlpha(0.86f));
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
        const auto tagPath = makeWonkyPanel(tag);
        graphics.setColour(juce::Colour(paperLight).withAlpha(0.84f));
        graphics.fillPath(tagPath);
        graphics.setColour(juce::Colour(ink));
        graphics.strokePath(tagPath, juce::PathStrokeType(1.0f));
        graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        graphics.drawText(label, tag, juce::Justification::centred);
    };

    drawMarker(sourceIn, "SOURCE IN", juce::Colour(dustyViolet));
    drawMarker(sourceOut, "SOURCE OUT", juce::Colour(dustyViolet));
    if (rotation >= 0.0f)
        drawMarker(rotation, "LOOP START", juce::Colour(fadedCoral));
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
    if (!snapshot.valid)
        return;

    auto content = bounds.reduced(10.0f, 7.0f);
    auto spectrumArea = content.removeFromLeft(content.getWidth() * 0.62f);
    content.removeFromLeft(10.0f);
    auto phaseArea = content.removeFromLeft(juce::jmin(content.getWidth() * 0.48f,
                                                       content.getHeight()));
    content.removeFromLeft(10.0f);
    auto meterArea = content;

    graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    graphics.setColour(juce::Colour(softInk));
    graphics.drawText("SPECTRUM  SOURCE / LOOP", spectrumArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    for (int line = 1; line < 4; ++line)
    {
        const auto y = spectrumArea.getY() + spectrumArea.getHeight() * line / 4.0f;
        graphics.setColour(juce::Colour(ink).withAlpha(0.16f));
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
    drawSpectrum(snapshot.sourceSpectrum, juce::Colour(dustyViolet).withAlpha(0.76f), 1.0f);
    drawSpectrum(snapshot.outputSpectrum, juce::Colour(fadedCoral), 1.6f);

    graphics.setColour(juce::Colour(softInk));
    graphics.drawText("PHASE", phaseArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    const auto phaseSquare = phaseArea.withSizeKeepingCentre(
        juce::jmin(phaseArea.getWidth(), phaseArea.getHeight()),
        juce::jmin(phaseArea.getWidth(), phaseArea.getHeight()));
    graphics.setColour(juce::Colour(ink).withAlpha(0.65f));
    graphics.drawRect(phaseSquare, 1.0f);
    graphics.setColour(juce::Colour(ink).withAlpha(0.24f));
    graphics.drawLine(phaseSquare.getCentreX(), phaseSquare.getY(),
                      phaseSquare.getCentreX(), phaseSquare.getBottom());
    graphics.drawLine(phaseSquare.getX(), phaseSquare.getCentreY(),
                      phaseSquare.getRight(), phaseSquare.getCentreY());
    graphics.setColour(juce::Colour(dustyViolet).withAlpha(0.68f));
    for (const auto& point : snapshot.phasePoints)
    {
        const auto side = 0.5f * (point[0] - point[1]);
        const auto mid = 0.5f * (point[0] + point[1]);
        const auto x = phaseSquare.getCentreX() + side * phaseSquare.getWidth() * 0.45f;
        const auto y = phaseSquare.getCentreY() - mid * phaseSquare.getHeight() * 0.45f;
        graphics.fillEllipse(x - 0.75f, y - 0.75f, 1.5f, 1.5f);
    }

    graphics.setColour(juce::Colour(softInk));
    graphics.drawText("CORRELATION", meterArea.removeFromTop(12.0f),
                      juce::Justification::centredLeft);
    auto correlationTrack = meterArea.removeFromTop(9.0f).reduced(0.0f, 2.0f);
    graphics.setColour(juce::Colour(ink).withAlpha(0.24f));
    graphics.fillRoundedRectangle(correlationTrack, 2.5f);
    const auto correlation = juce::jlimit(-1.0f, 1.0f, snapshot.outputCorrelation);
    const auto markerX = correlationTrack.getX()
        + 0.5f * (correlation + 1.0f) * correlationTrack.getWidth();
    graphics.setColour(correlation < 0.0f ? juce::Colour(fadedCoral)
                                          : juce::Colour(acidYellow));
    graphics.fillRoundedRectangle(markerX - 2.0f, correlationTrack.getY() - 2.0f,
                                  4.0f, correlationTrack.getHeight() + 4.0f, 2.0f);
    graphics.setColour(juce::Colour(ink));
    graphics.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    graphics.drawText(juce::String(correlation, 2), meterArea.removeFromTop(18.0f),
                      juce::Justification::centredLeft);
    const auto imbalance = snapshot.outputImbalanceDb;
    const auto side = imbalance > 0.25f ? "L " : imbalance < -0.25f ? "R " : "C ";
    graphics.setColour(juce::Colour(softInk));
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
