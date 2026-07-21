#include "LoopAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr auto pi = 3.14159265358979323846;

struct Candidate
{
    LoopAnalysisResult result;
    float cheapScore = 0.0f;
};

float clampScore(const double value)
{
    return static_cast<float>(juce::jlimit(0.0, 100.0, value));
}

double sampleRms(const juce::AudioBuffer<float>& audio, const int start, const int length)
{
    double energy = 0.0;
    int count = 0;

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto* samples = audio.getReadPointer(channel, start);
        for (int index = 0; index < length; ++index)
        {
            const auto value = static_cast<double>(samples[index]);
            energy += value * value;
            ++count;
        }
    }

    return std::sqrt(energy / static_cast<double>(juce::jmax(1, count)));
}

float calculateLevelScore(const juce::AudioBuffer<float>& audio,
                          const int start,
                          const int end,
                          const int window)
{
    const auto headRms = sampleRms(audio, start, window);
    const auto tailRms = sampleRms(audio, end - window, window);
    const auto scale = juce::jmax(1.0e-7, 0.5 * (headRms + tailRms));
    return clampScore(100.0 * std::exp(-2.5 * std::abs(headRms - tailRms) / scale));
}

float calculateSlopeScore(const juce::AudioBuffer<float>& audio,
                          const int start,
                          const int end)
{
    double error = 0.0;
    double scale = 0.0;

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto headSlope = static_cast<double>(audio.getSample(channel, start + 1)
                                                   - audio.getSample(channel, start));
        const auto tailSlope = static_cast<double>(audio.getSample(channel, end - 1)
                                                   - audio.getSample(channel, end - 2));
        error += std::abs(headSlope - tailSlope);
        scale += 0.5 * (std::abs(headSlope) + std::abs(tailSlope));
    }

    return clampScore(100.0 * std::exp(-2.0 * error / juce::jmax(1.0e-7, scale)));
}

float calculatePhaseScore(const juce::AudioBuffer<float>& audio,
                          const int start,
                          const int end,
                          const int window)
{
    double dot = 0.0;
    double headEnergy = 0.0;
    double tailEnergy = 0.0;

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {…9264 tokens truncated…alueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void configureSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);

    LoopSurgeonAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label descriptionLabel;
    juce::Label statusLabel;
    juce::TextButton captureButton { "Capture Input" };
    juce::TextButton clearButton { "Clear Loop" };
    juce::ToggleButton syncButton { "Sync capture to next bar" };
    juce::ComboBox barsBox;
    juce::Label barsLabel;
    juce::Slider loopLengthSlider;
    juce::Slider crossfadeSlider;
    juce::Slider mixSlider;
    juce::Label loopLengthLabel;
    juce::Label crossfadeLabel;
    juce::Label mixLabel;
    std::unique_ptr<SliderAttachment> loopLengthAttachment;
    std::unique_ptr<SliderAttachment> crossfadeAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<ComboBoxAttachment> barsAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopSurgeonAudioProcessorEditor)
};
