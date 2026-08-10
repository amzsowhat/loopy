­r‡^Ñf¥–Ø¦{}ìyÊ'vÃ®¶›­#include "PluginEditor.h"

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
    graphics.drawLine(phaseSquare.getCentreç5¶‰žËkºwµçy•I½Ü¹É•µ½Ù•É½µI¥¡Ð ÄÈÀ¤¤ì(€€€É…¹•I½Ü¹É•µ½Ù•É½µI¥¡Ð à¤ì(€€€…¹…±åé•I…¹•	ÕÑÑ½¸¹Í•Ñ	½Õ¹‘Ì¡É…¹•I½Ü¹É•µ½Ù•É½µI¥¡Ð ÄÜØ¤¤ì(€€€É…¹•I½Ü¹É•µ½Ù•É½µI¥¡Ð ÄÈ¤ì(€€€É…¹•1…‰•°¹Í•Ñ	½Õ¹‘Ì¡É…¹•I½Ü¤ì((€€€…ÕÑ¼…Õ‘¥Ñ¥½¸€ô…Õ‘¥Ñ¥½¹…É¹É•‘Õ• ÄÐ¤ì(€€€…Õ‘¥Ñ¥½¸¹É•µ½Ù•É½µQ½À Èà¤ì(€€€…¹‘¥‘…Ñ•	½à¹Í•Ñ	½Õ¹‘Ì¡…Õ‘¥Ñ¥½¸¹É•µ½Ù•É½µQ½À ÌÐ¤¤ì(€€€…Õ‘¥Ñ¥½¸¹É•µ½Ù•É½µQ½À à¤ì(€€€…ÕÑ¼ÑÉ…¹ÍÁ½ÉÐ€ô…Õ‘¥Ñ¥½¸¹É•µ½Ù•É½µQ½À Ìà¤ì(€€€ÁÉ•Ù¥•ÝQÉ…¹ÍÁ½ÉÑ	ÕÑÑ½¸¹Í•Ñ	½Õ¹‘Ì¡ÑÉ…¹ÍÁ½ÉÐ¹É•µ½Ù•É½µ1•™Ð ÄÄØ¤¤ì(€€€ÑÉ…¹ÍÁ½ÉÐ¹É•µ½Ù•É½µ1•™Ð à¤ì(€€€½É¥¥¹…±AÉ•Ù¥•Ý	ÕÑÑ½¸¹Í•Ñ	½Õ¹‘Ì¡ÑÉ…¹ÍÁ½ÉÐ¹É•µ½Ù•É½µ1•™Ð àØ¤¤ì(€€€ÑÉ…¹ÍÁ½ÉÐ¹É•µ½Ù•É½µ1•™Ð Ø¤ì(€€€±½½ÁAÉ•Ù¥•Ý	ÕÑÑ½¸¹Í•Ñ	½Õ¹‘Ì¡ÑÉ…¹ÍÁ½ÉÐ¹É•µ½Ù•É½µ1•™Ð ÄÀÐ¤¤ì(€€€…Õ‘¥Ñ¥½¸¹É•µ½Ù•É½µQ½À ÄÀ¤ì(€€€…ÕÑ¼…Õ‘¥Ñ¥½¹5¥à€ô…Õ‘¥Ñ¥½¸¹É•µ½Ù•É½µQ½À ÐÀ¤ì(€€€µ¥á1…‰•°¹Í•Ñ	½Õ¹‘Ì¡…Õ‘¥Ñ¥½¹5¥à¹É•µ½Ù•É½µ1•™Ð ÄÄÈ¤¤ì(€€€µ¥áM±¥‘•È¹Í•Ñ	½Õ¹‘Ì¡…Õ‘¥Ñ¥½¹5¥à¤ì(€€€…Õ‘¥Ñ¥½¸¹É•µ½Ù•É½µQ½À à¤ì(€€€É••¹•É…Ñ•	ÕÑÑ½¸¹Í•Ñ	½Õ¹‘Ì¡…Õ‘¥Ñ¥½¸¹É•µ½Ù•É½µQ½À ÌÐ¤¹É•µ½Ù•É½µ1•™Ð ÄÔÀ¤¤ì((€€€…ÕÑ¼™¥¹¥Í €ô™¥¹¥Í¡…É¹É•‘Õ• ÄÐ¤ì(€€€™¥¹¥Í ¹É•µ½Ù•É½µQ½À Èà¤ì(€€€…ÕÑ¼µ½‘•I½Ü€ô™¥¹¥Í ¹É•µ½Ù•É½µQ½À ÌÐ¤ì(€€€µ½‘•1…‰•°¹Í•Ñ	½Õ¹‘Ì¡µ½‘•I½Ü¹É•µ½Ù•É½µ1•™Ð ÄÄÈ¤¤ì(€€€•¹•É…Ñ¥½¹5½‘•	½à¹Í•Ñ	½Õ¹‘Ì¡µ½‘•I½Ü¤ì(€€€™¥¹¥Í ¹É•µ½Ù•É½µQ½À Ü¤ì(€€€…ÕÑ¼‘ÕÉ…Ñ¥½¹I½Ü€ô™¥¹¥Í ¹É•µ½Ù•É½µQ½À ÌØ¤ì(€€€‘ÕÉ…Ñ¥½¹1…‰•°¹Í•Ñ	½Õ¹‘Ì¡‘ÕÉ…Ñ¥½¹I½Ü¹É•µ½Ù•É½µ1•™Ð ÄÄÈ¤¤ì(€€€‘ÕÉ…Ñ¥½¹M±¥‘•È¹Í•Ñ	½Õ¹‘Ì¡‘ÕÉ…Ñ¥½¹I½Ü¤ì(€€€É•Á…¥ÉÕÉ…Ñ¥½¹1…‰•°¹Í•Ñ	½Õ¹‘Ì¡‘ÕÉ…Ñ¥½¹1…‰•°¹•Ñ	½Õ¹‘Ì ¤¤ì(€€€É•Á…¥ÉÕÉ…Ñ¥½¹M±¥‘•È¹Í•Ñ	½Õ¹‘Ì¡‘ÕÉ…Ñ¥½¹M±¥‘•È¹•Ñ	½Õ¹‘Ì ¤¤ì(€€€…ÕÑ¼Í•…µI½Ü€ô‘ÕÉ…Ñ¥½¹1…‰•°¹•Ñ	½Õ¹‘Ì ¤¹•ÑU¹¥½¸¡‘ÕÉ…Ñ¥½¹M±¥‘•È¹•Ñ	½Õ¹‘Ì ¤¤ì(€€€É½ÍÍ™…‘•1…‰•°¹Í•Ñ	½Õ¹‘Ì¡Í•…µI½Ü¹É•µ½Ù•É½µ1•™Ð ÄÄÈ¤¤ì(€€€É½ÍÍ™…‘•M±¥‘•È¹Í•Ñ	½Õ¹‘Ì¡Í•…µI½Ü¤ì(€€€™¥¹¥Í ¹É•µ½Ù•É½µQ½À Ô¤ì(€€€…ÕÑ¼™±…ÑÑ•¹I½Ü€ô™¥¹¥Í ¹É•µ½Ù•É½µQ½À ÌØ¤ì(€€€™±…ÑÑ•¹1…‰•°¹Í•Ñ	½Õ¹‘Ì¡™±…ÑÑ•¹I½Ü¹É•µ½Ù•É½µ1•™Ð ÄÄÈ¤¤ì(€€€™±…ÑÑ•¹M±¥‘•È¹Í•Ñ	½Õ¹‘Ì¡™±…ÑÑ•¹I½Ü¤ì(€€€™¥¹¥Í ¹É•µ½Ù•É½µQ½À Ô¤ì(€€€…ÕÑ¼µ…Ñ¡I½Ü€ô™¥¹¥Í ¹É•µ½Ù•É½µQ½À ÌØ¤ì(€€€Í½ÕÉ•5…Ñ¡1…‰•°¹Í•Ñ	½Õ¹‘Ì¡µ…Ñ¡I½Ü¹É•µ½Ù•É½µ1•™Ð ÄÄÈ¤¤ì(€€€Í½ÕÉ•5…Ñ¡M±¥‘•È¹Í•Ñ	½Õ¹‘Ì¡µ…Ñ¡I½Ü¤ì((€€€…ÕÑ¼™½½Ñ•È€ô™½½Ñ•ÉÉ•„ì(€€€•áÁ½ÉÑ	ÕÑÑ½¸¹Í•Ñ	½Õ¹‘Ì¡™½½Ñ•È¹É•µ½Ù•É½µI¥¡Ð ÄÐÀ¤¹É•‘Õ• À°€Ü¤¤ì(€€€™½½Ñ•È¹É•µ½Ù•É½µI¥¡Ð à¤ì(€€€‘É…Q½…Ý	ÕÑÑ½¸¹Í•Ñ	½Õ¹‘Ì¡™½½Ñ•È¹É•µ½Ù•É½µI¥¡Ð ÄàÐ¤¹É•‘Õ• À°€Ü¤¤ì(€€€™½½Ñ•È¹É•µ½Ù•É½µI¥¡Ð ÄÐ¤ì(€€€ÍÑ…ÑÕÍ1…‰•°¹Í•Ñ	½Õ¹‘Ì¡™½½Ñ•È¹É•µ½Ù•É½µQ½À ÈÔ¤¤ì(€€€ÅÕ…±¥ÑåY¥•Ü¹Í•Ñ	½Õ¹‘Ì¡™½½Ñ•È¹É•µ½Ù•É½µ	½ÑÑ½´ ÈÜ¤¤ì(€€€™½½Ñ•È¹É•µ½Ù•É½µQ½À Ì¤ì(€€€™½½Ñ•È¹É•µ½Ù•É½µ	½ÑÑ½´ Ì¤ì(€€€Í¥¹…±¹…±åÍ¥ÍY¥•Ü¹Í•Ñ	½Õ¹‘Ì¡™½½Ñ•È¤ì)ô()‰½½°1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½Èèé¥Í%¹Ñ•É•ÍÑ•‘%¹¥±•É…œ (€€€½¹ÍÐ©Õ”èéMÑÉ¥¹ÉÉ…ä˜™¥±•Ì¤)ì(€€€É•ÑÕÉ¸™¥±•Ì¹Í¥é” ¤€ôô€Ä(€€€€€€€€€€€˜˜©Õ”èé¥±”¡™¥±•ÍlÁt¤¹¡…Í¥±•áÑ•¹Í¥½¸ ‰Ý…Øí…¥˜í…¥™˜í™±…Œí½œˆ¤ì)ô()Ù½¥1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½Èèé™¥±•ÍÉ½ÁÁ• (€€€½¹ÍÐ©Õ”èéMÑÉ¥¹ÉÉ…ä˜™¥±•Ì°¥¹Ð°¥¹Ð¤)ì(€€€¥˜€¡™¥±•Ì¹Í¥é” ¤€ôô€Ä¤(€€€€€€€¥µÁ½ÉÑ¥±”¡©Õ”èé¥±”¡™¥±•ÍlÁt¤¤ì)ô()Ù½¥1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½Èèé¥µÁ½ÉÑ¥±”¡½¹ÍÐ©Õ”èé¥±”˜™¥±”¤)ì(€€€ÁÉ½•ÍÍ½È¹Í•ÑAÉ•Ù¥•ÝA±…å¥¹œ¡™…±Í”¤ì(€€€±…ÍÑ5•ÍÍ…”€ôÁÉ½•ÍÍ½È¹¥µÁ½ÉÑÕ‘¥½¥±”¡™¥±”¤ì(€€€¥˜€¡±…ÍÑ5•ÍÍ…”¹¥ÍµÁÑä ¤¤(€€€€€€€±…ÍÑ5•ÍÍ…”€ô€‰M½ÕÉ”±½…‘•ˆì(€€€ÕÁ‘…Ñ•AÉ¥µ…ÉåÑ¥½¸ ¤ì)ô()Ù½¥1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½Èèé¡½½Í•%µÁ½ÉÑ¥±” ¤)ì(€€€™¥±•¡½½Í•È€ôÍÑèéµ…­•}Õ¹¥ÅÕ”ñ©Õ”èé¥±•¡½½Í•Èø (€€€€€€€€‰¡½½Í”Í½ÕÉ”…Õ‘¥¼ˆ°©Õ”èé¥±”íô°€ˆ¨¹Ý…Øì¨¹…¥˜ì¨¹…¥™˜ì¨¹™±…Œì¨¹½œˆ¤ì(€€€™¥±•¡½½Í•È´ù±…Õ¹¡Íå¹Œ¡©Õ”èé¥±•	É½ÝÍ•É½µÁ½¹•¹Ðèé½Á•¹5½‘”(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ð©Õ”èé¥±•	É½ÝÍ•É½µÁ½¹•¹Ðèé…¹M•±•Ñ¥±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€mÑ¡¥Ít€¡½¹ÍÐ©Õ”èé¥±•¡½½Í•È˜¡½½Í•È¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€ì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ…ÕÑ¼™¥±”€ô¡½½Í•È¹•ÑI•ÍÕ±Ð ¤ì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¥˜€¡™¥±”¹•á¥ÍÑÍÍ¥±” ¤¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¥µÁ½ÉÑ¥±”¡™¥±”¤ì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€ô¤ì)ô()Ù½¥1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½Èèé¡½½Í•áÁ½ÉÑ¥±” ¤)ì(€€€™¥±•¡½½Í•È€ôÍÑèéµ…­•}Õ¹¥ÅÕ”ñ©Õ”èé¥±•¡½½Í•Èø (€€€€€€€€‰áÁ½ÉÐ•¹•É…Ñ•…Õ‘¥¼ˆ°(€€€€€€€©Õ”èé¥±”èé•ÑMÁ•¥…±1½…Ñ¥½¸¡©Õ”èé¥±”èéÕÍ•É½Õµ•¹ÑÍ¥É•Ñ½Éä¤(€€€€€€€€€€€€¹•Ñ¡¥±‘¥±” ‰1½½ÀMÕÉ•½¸¹Ý…Øˆ¤°(€€€€€€€€ˆ¨¹Ý…Øˆ¤ì(€€€™¥±•¡½½Í•È´ù±…Õ¹¡Íå¹Œ¡©Õ”èé¥±•	É½ÝÍ•É½µÁ½¹•¹ÐèéÍ…Ù•5½‘”(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ð©Õ”èé¥±•	É½ÝÍ•É½µÁ½¹•¹Ðèé…¹M•±•Ñ¥±•Ì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ð©Õ”èé¥±•	É½ÝÍ•É½µÁ½¹•¹ÐèéÝ…É¹‰½ÕÑ=Ù•ÉÝÉ¥Ñ¥¹œ°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€mÑ¡¥Ít€¡½¹ÍÐ©Õ”èé¥±•¡½½Í•È˜¡½½Í•È¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€ì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐ…ÕÑ¼™¥±”€ô¡½½Í•È¹•ÑI•ÍÕ±Ð ¤ì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¥˜€¡™¥±”€„ô©Õ”èé¥±”íô¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€±…ÍÑ5•ÍÍ…”€ôÁÉ½•ÍÍ½È¹•áÁ½ÉÑ1½½Á¥±”¡™¥±”¤ì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¥˜€¡±…ÍÑ5•ÍÍ…”¹¥ÍµÁÑä ¤¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€±…ÍÑ5•ÍÍ…”€ô€ˆÈÐµ‰¥Ð]X•áÁ½ÉÑ•ˆì(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ô(€€€€€€€€€€€€€€€€€€€€€€€€€€€€ô¤ì)ô()©Õ”èé¥±”1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½ÈèéÁÉ•Á…É•…ÝÉ…¥±” ¤)ì(€€€…ÕÑ¼É•¹‘•É¥É•Ñ½Éä€ô©Õ”èé¥±”èé•ÑMÁ•¥…±1½…Ñ¥½¸ (€€€€€€€©Õ”èé¥±”èéÕÍ•ÉÁÁ±¥…Ñ¥½¹…Ñ…¥É•Ñ½Éä¤(€€€€€€€€¹•Ñ¡¥±‘¥±” ‰M½Õ¹YMPAÉ½©•Ðˆ¤(€€€€€€€€¹•Ñ¡¥±‘¥±” ‰1½½ÀMÕÉ•½¸ˆ¤(€€€€€€€€¹•Ñ¡¥±‘¥±” ‰\I•¹‘•ÉÌˆ¤ì(€€€¥˜€¡É•¹‘•É¥É•Ñ½Éä¹É•…Ñ•¥É•Ñ½Éä ¤¹™…¥±• ¤¤(€€€€€€€É•ÑÕÉ¸íôì((€€€½¹ÍÐ…ÕÑ¼ÍÑ…µÀ€ô©Õ”èéQ¥µ”èé•ÑÕÉÉ•¹ÑQ¥µ” ¤¹™½Éµ…ÑÑ• ˆ•d•´•´• •4•Lˆ¤ì(€€€½¹ÍÐ…ÕÑ¼™¥±”€ôÉ•¹‘•É¥É•Ñ½Éä¹•Ñ9½¹•á¥ÍÑ•¹Ñ¡¥±‘¥±” (€€€€€€€€‰1½½ÀµMÕÉ•½¸´ˆ€¬ÍÑ…µÀ°€ˆ¹Ý…Øˆ°™…±Í”¤ì(€€€½¹ÍÐ…ÕÑ¼•ÉÉ½È€ôÁÉ½•ÍÍ½È¹•áÁ½ÉÑ1½½Á¥±”¡™¥±”¤ì(€€€¥˜€¡•ÉÉ½È¹¥Í9½ÑµÁÑä ¤¤(€€€ì(€€€€€€€±…ÍÑ5•ÍÍ…”€ô•ÉÉ½Èì(€€€€€€€É•ÑÕÉ¸íôì(€€€ô(€€€É•ÑÕÉ¸™¥±”ì)ô()Ù½¥1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½ÈèéÑ¥µ•É…±±‰…¬ ¤)ì(€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ”€ôÁÉ½•ÍÍ½È¹•ÑM½ÕÉ•9…µ” ¤ì(€€€Í½ÕÉ•1…‰•°¹Í•ÑQ•áÐ¡Í½ÕÉ”¹¥ÍµÁÑä ¤€ü€‰9¼Í½ÕÉ”±½…‘•ˆ€èÍ½ÕÉ”°(€€€€€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€½¹ÍÐ…ÕÑ¼…¹‘¥‘…Ñ•½Õ¹Ð€ôÁÉ½•ÍÍ½È¹•Ñ…¹‘¥‘…Ñ•½Õ¹Ð ¤ì(€€€½¹ÍÐ…ÕÑ¼…¹‘¥‘…Ñ•I•Ù¥Í¥½¸€ôÁÉ½•ÍÍ½È¹•Ñ…¹‘¥‘…Ñ•I•Ù¥Í¥½¸ ¤ì(€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ•I•Ù¥Í¥½¸€ôÁÉ½•ÍÍ½È¹•ÑM½ÕÉ•I•Ù¥Í¥½¸ ¤ì(€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ•¡…¹•€ôÍ½ÕÉ”€„ô‘¥ÍÁ±…å•‘M½ÕÉ”(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ñðÍ½ÕÉ•I•Ù¥Í¥½¸€„ô‘¥ÍÁ±…å•‘M½ÕÉ•I•Ù¥Í¥½¸ì(€€€¥˜€¡Í½ÕÉ•¡…¹•¤(€€€ì(€€€€€€€‘¥ÍÁ±…å•‘M½ÕÉ”€ôÍ½ÕÉ”ì(€€€€€€€‘¥ÍÁ±…å•‘M½ÕÉ•I•Ù¥Í¥½¸€ôÍ½ÕÉ•I•Ù¥Í¥½¸ì(€€€€€€€Ý…Ù•™½ÉµY¥•Ü¹Í•Ñ]…Ù•™½É´¡ÁÉ½•ÍÍ½È¹•Ñ]…Ù•™½ÉµAÉ•Ù¥•Ü ¤¤ì(€€€€€€€Ý…Ù•™½ÉµY¥•Ü¹Í•ÑM½ÕÉ•I…¹” (€€€€€€€€€€€ÁÉ½•ÍÍ½È¹•Ñ¹…±åÍ¥ÍI…¹•MÑ…ÉÑAÉ½Á½ÉÑ¥½¸ ¤°(€€€€€€€€€€€ÁÉ½•ÍÍ½È¹•Ñ¹…±åÍ¥ÍI…¹•¹‘AÉ½Á½ÉÑ¥½¸ ¤¤ì(€€€€€€€Ý…Ù•™½ÉµY¥•Ü¹Í•ÑI½Ñ…Ñ¥½¸ ´Ä¸Á˜¤ì(€€€ô(€€€¥˜€¡Í½ÕÉ•¡…¹•ñð…¹‘¥‘…Ñ•½Õ¹Ð€„ô‘¥ÍÁ±…å•‘…¹‘¥‘…Ñ•½Õ¹Ð(€€€€€€€ñð…¹‘¥‘…Ñ•I•Ù¥Í¥½¸€„ô‘¥ÍÁ±…å•‘…¹‘¥‘…Ñ•I•Ù¥Í¥½¸¤(€€€ì(€€€€€€€‘¥ÍÁ±…å•‘…¹‘¥‘…Ñ•½Õ¹Ð€ô…¹‘¥‘…Ñ•½Õ¹Ðì(€€€€€€€‘¥ÍÁ±…å•‘…¹‘¥‘…Ñ•I•Ù¥Í¥½¸€ô…¹‘¥‘…Ñ•I•Ù¥Í¥½¸ì(€€€€€€€…¹‘¥‘…Ñ•	½à¹±•…È¡©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€™½È€¡¥¹Ð¥¹‘•à€ô€Àì¥¹‘•à€ð…¹‘¥‘…Ñ•½Õ¹Ðì€¬­¥¹‘•à¤(€€€€€€€€€€€…¹‘¥‘…Ñ•	½à¹…‘‘%Ñ•´¡ÁÉ½•ÍÍ½È¹•Ñ…¹‘¥‘…Ñ••ÍÉ¥ÁÑ¥½¸¡¥¹‘•à¤°¥¹‘•à€¬€Ä¤ì(€€€€€€€¥˜€¡…¹‘¥‘…Ñ•½Õ¹Ð€ø€À¤(€€€€€€€€€€€…¹‘¥‘…Ñ•	½à¹Í•ÑM•±•Ñ•‘%Ñ•µ%¹‘•à À°©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€ô((€€€ÕÁ‘…Ñ•I…¹•1…‰•° ¤ì(€€€ÕÁ‘…Ñ•AÉ¥µ…ÉåÑ¥½¸ ¤ì((€€€½¹ÍÐ…ÕÑ¼ÍÑ…Ñ”€ôÁÉ½•ÍÍ½È¹•Ñ1½½ÁMÑ…Ñ” ¤ì(€€€½¹ÍÐ…ÕÑ¼É•…‘ä€ôÍÑ…Ñ”€ôô1½½Á¹¥¹”èéMÑ…Ñ”èéÉ•…‘äì(€€€½¹ÍÐ…ÕÑ¼Ñ•áÑÕÉ•I•ÍÕ±Ð€ôÉ•…‘ä(€€€€€€€€˜˜ÁÉ½•ÍÍ½È¹•Ñ1…ÍÑUÍ•‘•¹•É…Ñ¥½¹5½‘” ¤(€€€€€€€€€€€€€€€ôô1½½Á¹¥¹”èé•¹•É…Ñ¥½¹5½‘”èéÑ•áÑÕÉ•1½½Àì(€€€½¹ÍÐ…ÕÑ¼Í•±•Ñ•‘Q•áÑÕÉ•5½‘”€ô•¹•É…Ñ¥½¹5½‘•	½à¹•ÑM•±•Ñ•‘%Ñ•µ%¹‘•à ¤€ôô€Äì(€€€…¹‘¥‘…Ñ•	½à¹Í•ÑQ•áÑ]¡•¹9½Ñ¡¥¹M•±•Ñ•¡Í•±•Ñ•‘Q•áÑÕÉ•5½‘”(€€€€€€€€ü€‰Q•áÑÕÉ”Ù…É¥…Ñ¥½¹Ìˆ€è€‰I•Á…¥È½ÁÑ¥½¹Ìˆ¤ì(€€€ÅÕ…±¥ÑåY¥•Ü¹Í•ÑQ•áÑÕÉ•5½‘”¡É•…‘ä€üÑ•áÑÕÉ•I•ÍÕ±Ð€èÍ•±•Ñ•‘Q•áÑÕÉ•5½‘”¤ì(€€€¥˜€¡É•…‘ä€˜˜€…Ñ•áÑÕÉ•I•ÍÕ±Ð€˜˜€…Ý…Ù•™½ÉµY¥•Ü¹¥Í‘¥Ñ¥¹I½Ñ…Ñ¥½¸ ¤¤(€€€€€€€Ý…Ù•™½ÉµY¥•Ü¹Í•ÑI½Ñ…Ñ¥½¸¡ÁÉ½•ÍÍ½È¹•ÑI½Ñ…Ñ¥½¹AÉ½Á½ÉÑ¥½¸ ¤¤ì(€€€•±Í”¥˜€ …É•…‘äñðÑ•áÑÕÉ•I•ÍÕ±Ð¤(€€€€€€€Ý…Ù•™½ÉµY¥•Ü¹Í•ÑI½Ñ…Ñ¥½¸ ´Ä¸Á˜¤ì(€€€½¹ÍÐ…ÕÑ¼…ÁÁÉ½Ù•€ôÉ•…‘ä€˜˜ÁÉ½•ÍÍ½È¹¡…ÍA…ÍÍ•‘EÕ…±¥Ñå…Ñ” ¤ì(€€€Í¥¹…±¹…±åÍ¥ÍY¥•Ü¹Í•ÑM¹…ÁÍ¡½Ð¡É•…‘ä€üÁÉ½•ÍÍ½È¹•ÑM¥¹…±M¹…ÁÍ¡½Ð ¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€èI•¹‘•ÉEÕ…±¥ÑäèéM¥¹…±M¹…ÁÍ¡½Ðíô¤ì(€€€•áÁ½ÉÑ	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡…ÁÁÉ½Ù•¤ì(€€€‘É…Q½…Ý	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡…ÁÁÉ½Ù•¤ì(€€€…¹‘¥‘…Ñ•	½à¹Í•Ñ¹…‰±•¡É•…‘ä¤ì(€€€ÁÉ•Ù¥•ÝQÉ…¹ÍÁ½ÉÑ	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡É•…‘ä¤ì(€€€½É¥¥¹…±AÉ•Ù¥•Ý	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡É•…‘ä¤ì(€€€±½½ÁAÉ•Ù¥•Ý	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡É•…‘ä¤ì(€€€…¹…±åé•I…¹•	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡Í½ÕÉ”¹¥Í9½ÑµÁÑä ¤€˜˜ÍÑ…Ñ”€„ô1½½Á¹¥¹”èéMÑ…Ñ”èé…¹…±åÍ¥¹œ¤ì(€€€É•Í•ÑI…¹•	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡Í½ÕÉ”¹¥Í9½ÑµÁÑä ¤¤ì(€€€É••¹•É…Ñ•	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡É•…‘ä(€€€€€€€€˜˜ÁÉ½•ÍÍ½È¹•Ñ1…ÍÑUÍ•‘•¹•É…Ñ¥½¹5½‘” ¤(€€€€€€€€€€€€€€€ôô1½½Á¹¥¹”èé•¹•É…Ñ¥½¹5½‘”èéÑ•áÑÕÉ•1½½À¤ì(€€€É••¹•É…Ñ•	ÕÑÑ½¸¹Í•ÑY¥Í¥‰±”¡Í•±•Ñ•‘Q•áÑÕÉ•5½‘”¤ì(€€€±•…É	ÕÑÑ½¸¹Í•Ñ¹…‰±•¡Í½ÕÉ”¹¥Í9½ÑµÁÑä ¤ñðÉ•…‘ä¤ì((€€€½¹ÍÐ…ÕÑ¼‘¥É•Ñ5½‘•M•±•Ñ•€ô•¹•É…Ñ¥½¹5½‘•	½à¹•ÑM•±•Ñ•‘%Ñ•µ%¹‘•à ¤€ôô€Àì(€€€‘ÕÉ…Ñ¥½¹1…‰•°¹Í•ÑY¥Í¥‰±” …‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€‘ÕÉ…Ñ¥½¹M±¥‘•È¹Í•ÑY¥Í¥‰±” …‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€É•Á…¥ÉÕÉ…Ñ¥½¹1…‰•°¹Í•ÑY¥Í¥‰±”¡‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€É•Á…¥ÉÕÉ…Ñ¥½¹M±¥‘•È¹Í•ÑY¥Í¥‰±”¡‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€™±…ÑÑ•¹1…‰•°¹Í•ÑY¥Í¥‰±” …‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€™±…ÑÑ•¹M±¥‘•È¹Í•ÑY¥Í¥‰±” …‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€Í½ÕÉ•5…Ñ¡1…‰•°¹Í•ÑY¥Í¥‰±” …‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€Í½ÕÉ•5…Ñ¡M±¥‘•È¹Í•ÑY¥Í¥‰±” …‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€É½ÍÍ™…‘•1…‰•°¹Í•ÑY¥Í¥‰±”¡‘¥É•Ñ5½‘•M•±•Ñ•¤ì(€€€É½ÍÍ™…‘•M±¥‘•È¹Í•ÑY¥Í¥‰±”¡‘¥É•Ñ5½‘•M•±•Ñ•¤ì((€€€½¹ÍÐ…ÕÑ¼ÁÉ•Ù¥•Ý5½‘”€ôÁÉ½•ÍÍ½È¹•ÑAÉ•Ù¥•Ý5½‘” ¤ì(€€€½É¥¥¹…±AÉ•Ù¥•Ý	ÕÑÑ½¸¹Í•ÑQ½±•MÑ…Ñ”¡ÁÉ•Ù¥•Ý5½‘”€ôô1½½Á¹¥¹”èéAÉ•Ù¥•Ý5½‘”èé½É¥¥¹…°°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€±½½ÁAÉ•Ù¥•Ý	ÕÑÑ½¸¹Í•ÑQ½±•MÑ…Ñ”¡ÁÉ•Ù¥•Ý5½‘”€ôô1½½Á¹¥¹”èéAÉ•Ù¥•Ý5½‘”èé±½½À°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€½¹ÍÐ…ÕÑ¼ÁÉ•Ù¥•Ý¥¹œ€ôÁÉ½•ÍÍ½È¹¥ÍAÉ•Ù¥•ÝA±…å¥¹œ ¤ì(€€€ÁÉ•Ù¥•ÝQÉ…¹ÍÁ½ÉÑ	ÕÑÑ½¸¹Í•Ñ	ÕÑÑ½¹Q•áÐ¡ÁÉ•Ù¥•Ý¥¹œ€ü€‰MÑ½Àˆ€è€‰AÉ•Ù¥•Üˆ¤ì(€€€ÁÉ•Ù¥•ÝQÉ…¹ÍÁ½ÉÑ	ÕÑÑ½¸¹Í•Ñ½±½ÕÈ (€€€€€€€©Õ”èéQ•áÑ	ÕÑÑ½¸èé‰ÕÑÑ½¹½±½ÕÉ%°(€€€€€€€©Õ”èé½±½ÕÈ¡ÁÉ•Ù¥•Ý¥¹œ€ü€Áá™˜á„ÐäÐä€è€Áá™˜ÈàÝˆØÀ¤¤ì((€€€¥˜€¡±…ÍÑ5•ÍÍ…”¹¥Í9½ÑµÁÑä ¤¤(€€€ì(€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ¡±…ÍÑ5•ÍÍ…”°©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€±…ÍÑ5•ÍÍ…”¹±•…È ¤ì(€€€ô(€€€•±Í”(€€€ì(€€€€€€€ÍÝ¥Ñ €¡ÍÑ…Ñ”¤(€€€€€€€ì(€€€€€€€€€€€…Í”1½½Á¹¥¹”èéMÑ…Ñ”èé•µÁÑäè(€€€€€€€€€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ ‰9¼Í½ÕÉ”ˆ°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€€€€€€€€€‰É•…¬ì(€€€€€€€€€€€…Í”1½½Á¹¥¹”èéMÑ…Ñ”èéÍ½ÕÉ•I•…‘äè(€€€€€€€€€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ (€€€€€€€€€€€€€€€€€€€€‰M½ÕÉ”É•…‘ä€´Í•ÐÉ…¹”…¹µ½‘”°Ñ¡•¸•¹•É…Ñ”ˆ°(€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€€€€€€€€€‰É•…¬ì(€€€€€€€€€€€…Í”1½½Á¹¥¹”èéMÑ…Ñ”èé…Éµ•è(€€€€€€€€€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ ‰]…¥Ñ¥¹œ™½È\…ÁÑÕÉ”ˆ°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€€€€€€€€€‰É•…¬ì(€€€€€€€€€€€…Í”1½½Á¹¥¹”èéMÑ…Ñ”èé…ÁÑÕÉ¥¹œè(€€€€€€€€€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ (€€€€€€€€€€€€€€€€€€€€‰…ÁÑÕÉ¥¹œ€ˆ€¬©Õ”èéMÑÉ¥¹œ (€€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡ÁÉ½•ÍÍ½È¹•Ñ…ÁÑÕÉ•AÉ½É•ÍÌ ¤€¨€ÄÀÀ¸Á˜¤¤€¬€ˆ”ˆ°(€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€€€€€€€€€‰É•…¬ì(€€€€€€€€€€€…Í”1½½Á¹¥¹”èéMÑ…Ñ”èé…¹…±åÍ¥¹œè(€€€€€€€€€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ (€€€€€€€€€€€€€€€€€€€©Õ”èéMÑÉ¥¹œ ‰•¹•É…Ñ¥¹œ€€ˆ¤€¬©Õ”èéMÑÉ¥¹œ¡©Õ”èéÉ½Õ¹‘Q½%¹Ð (€€€€€€€€€€€€€€€€€€€€€€€ÁÉ½•ÍÍ½È¹•Ñ¹…±åÍ¥ÍAÉ½É•ÍÌ ¤€¨€ÄÀÀ¸Á˜¤¤€¬€ˆ”ˆ°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€€€€€€€€€‰É•…¬ì(€€€€€€€€€€€…Í”1½½Á¹¥¹”èéMÑ…Ñ”èéÉ•…‘äè(€€€€€€€€€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ (€€€€€€€€€€€€€€€€€€€ÁÉ½•ÍÍ½È¹¥Í1½Ý½¹™¥‘•¹” ¤(€€€€€€€€€€€€€€€€€€€€€€€€ü€‰1½½ÀÉ•…‘äƒŠP±½Ü½¹™¥‘•¹”è½µÁ…É”…¹‘¥‘…Ñ•Ì…É•™Õ±±äˆ(€€€€€€€€€€€€€€€€€€€€€€€€è€‰1½½ÀÉ•…‘äƒŠPÁÉ•ÍÌAÉ•Ù¥•Ü°Ñ¡•¸áÁ½ÉÐÝ¡•¸Í…Ñ¥Í™¥•ˆ°(€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€€€€€€€€€‰É•…¬ì(€€€€€€€€€€€…Í”1½½Á¹¥¹”èéMÑ…Ñ”èé™…¥±•è(€€€€€€€€€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ (€€€€€€€€€€€€€€€€€€€€‰9¼É•±¥…‰±”±½½À™½Õ¹ƒŠPÝ¥‘•¸Ñ¡”‰±Õ”É…¹”½ÈÑÉä‘¥™™•É•¹Ðµ…Ñ•É¥…°ˆ°(€€€€€€€€€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€€€€€€€€€€€€€‰É•…¬ì(€€€€€€€ô(€€€ô((€€€¥˜€¡ÍÑ…Ñ”€ôô1½½Á¹¥¹”èéMÑ…Ñ”èéÉ•…‘ä¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼Á…ÍÍ•‘…Ñ”€ôÁÉ½•ÍÍ½È¹¡…ÍA…ÍÍ•‘EÕ…±¥Ñå…Ñ” ¤ì(€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•Ñ½±½ÕÈ¡©Õ”èé1…‰•°èéÑ•áÑ½±½ÕÉ%°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èé½±½ÕÈ¡Á…ÍÍ•‘…Ñ”€ü±½½ÁÉ••¸€èÝ…É¹¥¹µ‰•È¤¤ì(€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•ÑQ•áÐ (€€€€€€€€€€€©Õ”èéMÑÉ¥¹œ¡Ñ•áÑÕÉ•I•ÍÕ±Ð€ü€‰Q•áÑÕÉ”1½½ÀÉ•…‘äˆ(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€è€‰I½Ñ…Ñ”€˜I•Á…¥È±½½ÀÉ•…‘äˆ¤(€€€€€€€€€€€€€€€€¬€¡Á…ÍÍ•‘…Ñ”(€€€€€€€€€€€€€€€€€€€€ü€ˆ€´EAML€ˆ€¬©Õ”èéMÑÉ¥¹œ¡ÁÉ½•ÍÍ½È¹•ÑI•¹‘•ÉEÕ…±¥ÑåM½É” ¤°€À¤(€€€€€€€€€€€€€€€€€€€€è€ˆ€´E	1=-èÉ••¹•É…Ñ”½ÈÉ•™¥¹”ˆ¤°(€€€€€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€ô(€€€•±Í”(€€€ì(€€€€€€€ÍÑ…ÑÕÍ1…‰•°¹Í•Ñ½±½ÕÈ¡©Õ”èé1…‰•°èéÑ•áÑ½±½ÕÉ%°©Õ”èé½±½ÕÈ¡±½½ÁÉ••¸¤¤ì(€€€ô((€€€ÅÕ…±¥ÑåY¥•Ü¹Í•ÑM½É•Ì (€€€€€€€ÁÉ½•ÍÍ½È¹•ÑM•…µEÕ…±¥Ñä ¤°(€€€€€€€Ñ•áÑÕÉ•I•ÍÕ±Ð€üÁÉ½•ÍÍ½È¹•Ñ1•Ù•±M½É” ¤€èÁÉ½•ÍÍ½È¹•ÑI•Á…¥ÉM½É” ¤°(€€€€€€€ÁÉ½•ÍÍ½È¹•ÑMÁ•ÑÉÕµM½É” ¤°ÁÉ½•ÍÍ½È¹•ÑA¡…Í•M½É” ¤°(€€€€€€€ÁÉ½•ÍÍ½È¹•ÑMÑ•É•½M½É” ¤°(€€€€€€€Ñ•áÑÕÉ•I•ÍÕ±Ð€üÁÉ½•ÍÍ½È¹•ÑI•Á•…ÑM…™•ÑåM½É” ¤(€€€€€€€€€€€€€€€€€€€€€€èÁÉ½•ÍÍ½È¹•ÑA•É¥½‘¥¥ÑåM½É” ¤¤ì(€€€É•Á…¥¹Ð ¤ì)ô()Ù½¥1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½ÈèéÕÁ‘…Ñ•I…¹•1…‰•° ¤)ì(€€€½¹ÍÐ…ÕÑ¼‘ÕÉ…Ñ¥½¸€ôÁÉ½•ÍÍ½È¹•ÑM½ÕÉ•ÕÉ…Ñ¥½¹M•½¹‘Ì ¤ì(€€€½¹ÍÐ…ÕÑ¼ÍÑ…ÉÐ€ô‘ÕÉ…Ñ¥½¸€¨Ý…Ù•™½ÉµY¥•Ü¹•ÑM½ÕÉ•%¸ ¤ì(€€€½¹ÍÐ…ÕÑ¼•¹€ô‘ÕÉ…Ñ¥½¸€¨Ý…Ù•™½ÉµY¥•Ü¹•ÑM½ÕÉ•=ÕÐ ¤ì(€€€½¹ÍÐ…ÕÑ¼É•Á…¥É•‘=ÕÑÁÕÐ€ôÁÉ½•ÍÍ½È¹•Ñ1½½ÁMÑ…Ñ” ¤€ôô1½½Á¹¥¹”èéMÑ…Ñ”èéÉ•…‘ä(€€€€€€€€˜˜ÁÉ½•ÍÍ½È¹•Ñ1…ÍÑUÍ•‘•¹•É…Ñ¥½¹5½‘” ¤€ôô1½½Á¹¥¹”èé•¹•É…Ñ¥½¹5½‘”èéÉ½Ñ…Ñ•I•Á…¥È(€€€€€€€€ü©Õ”èéMÑÉ¥¹œ ˆ€€€€=UQAUP€€ˆ¤(€€€€€€€€€€€€€€¬©Õ”èéMÑÉ¥¹œ¡ÁÉ½•ÍÍ½È¹•ÑI•¹‘•É•‘ÕÉ…Ñ¥½¹M•½¹‘Ì ¤°€È¤€¬€ˆÌˆ(€€€€€€€€è©Õ”èéMÑÉ¥¹œíôì(€€€É…¹•1…‰•°¹Í•ÑQ•áÐ (€€€€€€€€‰%8€€ˆ€¬©Õ”èéMÑÉ¥¹œ¡ÍÑ…ÉÐ°€È¤€¬€ˆÌ€€€€=UP€€ˆ€¬©Õ”èéMÑÉ¥¹œ¡•¹°€È¤(€€€€€€€€€€€€¬€ˆÌ€€€€M1Q€€ˆ€¬©Õ”èéMÑÉ¥¹œ¡©Õ”èé©µ…à À¸À°•¹€´ÍÑ…ÉÐ¤°€È¤€¬€ˆÌˆ(€€€€€€€€€€€€¬É•Á…¥É•‘=ÕÑÁÕÐ°(€€€€€€€©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì)ô()Ù½¥1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½ÈèéÕÁ‘…Ñ•AÉ¥µ…ÉåÑ¥½¸ ¤)ì(€€€½¹ÍÐ…ÕÑ¼Í•±•Ñ•‘5½‘”€ô•¹•É…Ñ¥½¹5½‘•	½à¹•ÑM•±•Ñ•‘%Ñ•µ%¹‘•à ¤ì(€€€…¹…±åé•I…¹•	ÕÑÑ½¸¹Í•Ñ	ÕÑÑ½¹Q•áÐ (€€€€€€€Í•±•Ñ•‘5½‘”€ôô€À€ü€‰I•Á…¥ÈM•±•Ñ•1½½Àˆ(€€€€€€€€€€€€€€€€€€€€€€€€€€è€‰•¹•É…Ñ”Q•áÑÕÉ”1½½Àˆ¤ì)ô()Ù½¥1½½ÁMÕÉ•½¹Õ‘¥½AÉ½•ÍÍ½É‘¥Ñ½Èèé½¹™¥ÕÉ•M±¥‘•È (€€€©Õ”èéM±¥‘•È˜Í±¥‘•È°©Õ”èé1…‰•°˜±…‰•°°½¹ÍÐ©Õ”èéMÑÉ¥¹œ˜Ñ•áÐ¤)ì(€€€Í±¥‘•È¹Í•ÑM±¥‘•ÉMÑå±”¡©Õ”èéM±¥‘•Èèé1¥¹•…É!½É¥é½¹Ñ…°¤ì(€€€Í±¥‘•È¹Í•ÑQ•áÑ	½áMÑå±”¡©Õ”èéM±¥‘•ÈèéQ•áÑ	½áI¥¡Ð°™…±Í”°€ÜØ°€ÈÔ¤ì(€€€Í±¥‘•È¹Í•Ñ½±½ÕÈ¡©Õ”èéM±¥‘•ÈèéÑÉ…­½±½ÕÉ%°©Õ”èé½±½ÕÈ Áá™˜ÉÍŒÑˆ¤¤ì(€€€Í±¥‘•È¹Í•Ñ½±½ÕÈ¡©Õ”èéM±¥‘•ÈèéÑ¡Õµ‰½±½ÕÉ%°©Õ”èé½±½ÕÈ¡±½½ÁÉ••¸¤¤ì(€€€…‘‘¹‘5…­•Y¥Í¥‰±”¡Í±¥‘•È¤ì(€€€±…‰•°¹Í•ÑQ•áÐ¡Ñ•áÐ°©Õ”èé‘½¹ÑM•¹‘9½Ñ¥™¥…Ñ¥½¸¤ì(€€€±…‰•°¹Í•Ñ½¹Ð¡©Õ”èé½¹Ñ=ÁÑ¥½¹Ì ÄÀ¸Õ˜°©Õ”èé½¹Ðèé‰½±¤¤ì(€€€±…‰•°¹Í•Ñ½±½ÕÈ¡©Õ”èé1…‰•°èéÑ•áÑ½±½ÕÉ%°©Õ”èé½±½ÕÈ¡Ñ•áÑ5ÕÑ•¤¤ì(€€€…‘‘¹‘5…­•Y¥Í¥‰±”¡±…‰•°¤ì)ô(