#include "PluginEditor.h"

LoopSurgeonAudioProcessorEditor::LoopSurgeonAudioProcessorEditor(
    LoopSurgeonAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner)
{
    titleLabel.setText("LOOP SURGEON", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff2f2f2));
    addAndMakeVisible(titleLabel);

    descriptionLabel.setText("Capture a region, audition the seam, and keep the DAW playing.",
                             juce::dontSendNotification);
    descriptionLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaeb7c2));
    addAndMakeVisible(descriptionLabel);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7fe0b2));
    addAndMakeVisible(statusLabel);

    captureButton.onClick = [this] { processor.beginCapture(); };
    captureButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e7d63));
    addAndMakeVisible(captureButton);

    clearButton.onClick = [this] { processor.clearLoop(); };
    clearButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a5260));
    addAndMakeVisible(clearButton);

    configureSlider(loopLengthSlider, loopLengthLabel, "Capture Length");
    configureSlider(crossfadeSlider, crossfadeLabel, "Seam Crossfade");
    configureSlider(mixSlider, mixLabel, "Loop Mix");

    auto& state = processor.getParameterState();
    loopLengthAttachment = std::make_unique<SliderAttachment>(state, "loopLength", loopLengthSlider);
    crossfadeAttachment = std::make_unique<SliderAttachment>(state, "crossfadeMs", crossfadeSlider);
    mixAttachment = std::make_unique<SliderAttachment>(state, "mix", mixSlider);

    setResizable(true, true);
    setResizeLimits(560, 320, 900, 600);
    setSize(680, 390);
    startTimerHz(12);
}

LoopSurgeonAudioProcessorEditor::~LoopSurgeonAudioProcessorEditor()
{
    stopTimer();
}

void LoopSurgeonAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff15191f));
    graphics.setColour(juce::Colour(0xff252b34));
    graphics.fillRoundedRectangle(getLocalBounds().toFloat().reduced(18.0f).withTrimmedTop(88.0f),
                                  12.0f);
}

void LoopSurgeonAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    titleLabel.setBounds(area.removeFromTop(38));
    descriptionLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(18);

    auto controls = area.removeFromTop(160).reduced(16, 12);
    const auto columnWidth = controls.getWidth() / 3;

    auto placeControl = [&] (juce::Slider& slider, juce::Label& label)
    {
        auto column = controls.removeFromLeft(columnWidth).reduced(8, 0);
        label.setBounds(column.removeFromTop(24));
        slider.setBounds(column);
    };

    placeControl(loopLengthSlider, loopLengthLabel);
    placeControl(crossfadeSlider, crossfadeLabel);
    placeControl(mixSlider, mixLabel);

    area.removeFromTop(14);
    auto buttons = area.removeFromTop(44);
    captureButton.setBounds(buttons.removeFromLeft(170));
    buttons.removeFromLeft(12);
    clearButton.setBounds(buttons.removeFromLeft(130));
    buttons.removeFromLeft(18);
    statusLabel.setBounds(buttons);
}

void LoopSurgeonAudioProcessorEditor::timerCallback()
{
    switch (processor.getLoopState())
    {
        case LoopEngine::State::empty:
            statusLabel.setText("Ready for input", juce::dontSendNotification);
            break;
        case LoopEngine::State::capturing:
            statusLabel.setText("Capturing "
                                    + juce::String(juce::roundToInt(processor.getCaptureProgress() * 100.0f))
                                    + "%",
                                juce::dontSendNotification);
            break;
        case LoopEngine::State::ready:
            statusLabel.setText("Loop ready  |  seam quality "
                                    + juce::String(processor.getSeamQuality(), 0) + "/100",
                                juce::dontSendNotification);
            break;
    }
}

void LoopSurgeonAudioProcessorEditor::configureSlider(juce::Slider& slider,
                                                       juce::Label& label,
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

