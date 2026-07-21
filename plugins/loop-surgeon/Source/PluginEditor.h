#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

class LoopSurgeonAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit LoopSurgeonAudioProcessorEditor(LoopSurgeonAudioProcessor&);
    ~LoopSurgeonAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void configureSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);

    LoopSurgeonAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label descriptionLabel;
    juce::Label statusLabel;
    juce::TextButton captureButton { "Capture Input" };
    juce::TextButton clearButton { "Clear Loop" };
    juce::Slider loopLengthSlider;
    juce::Slider crossfadeSlider;
    juce::Slider mixSlider;
    juce::Label loopLengthLabel;
    juce::Label crossfadeLabel;
    juce::Label mixLabel;
    std::unique_ptr<SliderAttachment> loopLengthAttachment;
    std::unique_ptr<SliderAttachment> crossfadeAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessorEditor)
};

