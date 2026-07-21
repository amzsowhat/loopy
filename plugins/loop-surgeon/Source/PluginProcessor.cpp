#include "PluginProcessor.h"

#include "PluginEditor.h"

LoopSurgeonAudioProcessor::LoopSurgeonAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "LoopSurgeonState", createParameterLayout())
{
}

void LoopSurgeonAudioProcessor::prepareToPlay(const double newSampleRate,
                                               const int samplesPerBlock)
{
    loopEngine.prepare(newSampleRate,
                       samplesPerBlock,
                       juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels()));
}

void LoopSurgeonAudioProcessor::releaseResources()
{
    loopEngine.releaseResources();
}

bool LoopSurgeonAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void LoopSurgeonAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    loopEngine.setLoopLengthSeconds(parameters.getRawParameterValue("loopLength")->load());
    loopEngine.setCrossfadeMilliseconds(parameters.getRawParameterValue("crossfadeMs")->load());
    loopEngine.process(buffer, parameters.getRawParameterValue("mix")->load());
}

juce::AudioProcessorEditor* LoopSurgeonAudioProcessor::createEditor()
{
    return new LoopSurgeonAudioProcessorEditor(*this);
}

void LoopSurgeonAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream(destData, false);
    parameters.copyState().writeToStream(stream);
}

void LoopSurgeonAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    const auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
    if (state.isValid())
        parameters.replaceState(state);

    loopEngine.clear();
}

juce::AudioProcessorValueTreeState::ParameterLayout
LoopSurgeonAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "loopLength", 1 },
        "Capture Length",
        juce::NormalisableRange<float> { 0.25f, 16.0f, 0.01f, 0.45f },
        4.0f,
        juce::AudioParameterFloatAttributes().withLabel("s")));

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "crossfadeMs", 1 },
        "Crossfade",
        juce::NormalisableRange<float> { 1.0f, 250.0f, 0.1f, 0.5f },
        25.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "mix", 1 },
        "Loop Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (const float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    return { layout.begin(), layout.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoopSurgeonAudioProcessor();
}

