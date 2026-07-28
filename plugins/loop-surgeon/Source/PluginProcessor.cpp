#include "PluginProcessor.h"

#include "PluginEditor.h"

LoopSurgeonAudioProcessor::LoopSurgeonAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "LoopSurgeonState", createParameterLayout())
{
    formatManager.registerBasicFormats();
}

void LoopSurgeonAudioProcessor::prepareToPlay(const double newSampleRate,
                                               const int samplesPerBlock)
{
    currentSampleRate = newSampleRate;
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

    auto captureLength = parameters.getRawParameterValue("loopLength")->load();
    auto captureDelaySamples = 0;
    if (captureRequested.exchange(false, std::memory_order_acq_rel))
    {
        const auto syncToHost = parameters.getRawParameterValue("syncToHost")->load() >= 0.5f;
        if (syncToHost)
        {
            if (auto position = getPlayHead() != nullptr ? getPlayHead()->getPosition() : std::nullopt)
            {
                const auto bpm = position->getBpm().orFallback(120.0);
                const auto signature = position->getTimeSignature().orFallback(
                    juce::AudioPlayHead::TimeSignature { 4, 4 });
                const int barOptions[] { 1, 2, 4, 8 };
                const auto barIndex = juce::jlimit(0, 3,
                    juce::roundToInt(parameters.getRawParameterValue("bars")->load()));
                const auto quarterNotesPerBar = static_cast<double>(signature.numerator) * 4.0
                                                / static_cast<double>(signature.denominator);
                captureLength = static_cast<float>(barOptions[barIndex] * quarterNotesPerBar
                                                   * 60.0 / juce::jmax(1.0, bpm));

                if (const auto ppq = position->getPpqPosition())
                {
                    const auto currentBar = std::floor(*ppq / quarterNotesPerBar);
                    auto nextBarPpq = (currentBar + 1.0) * quarterNotesPerBar;
                    if (std::abs(*ppq - currentBar * quarterNotesPerBar) < 1.0e-6)
                        nextBarPpq = *ppq;
                    captureDelaySamples = juce::jmax(0, juce::roundToInt(
                        (nextBarPpq - *ppq) * 60.0 / juce::jmax(1.0, bpm) * currentSampleRate));
                }
            }
        }

        loopEngine.setLoopLengthSeconds(juce::jlimit(0.25f, 16.0f, captureLength));
        loopEngine.beginCapture(captureDelaySamples);
    }
    else
    {
        loopEngine.setLoopLengthSeconds(juce::jlimit(0.25f, 16.0f, captureLength));
    }
    loopEngine.setCrossfadeMilliseconds(parameters.getRawParameterValue("crossfadeMs")->load());
    loopEngine.process(buffer, parameters.getRawParameterValue("mix")->load());
}

void LoopSurgeonAudioProcessor::beginCapture() noexcept
{
    captureRequested.store(true, std::memory_order_release);
}

juce::String LoopSurgeonAudioProcessor::importAudioFile(const juce::File& file)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return "Unsupported or unreadable audio file";
    if (reader->lengthInSamples < 32 || reader->sampleRate <= 0.0)
        return "The audio file is too short";

    constexpr auto maximumSourceSeconds = 60.0;
    const auto wasTruncated = reader->lengthInSamples
                              > static_cast<int64_t>(std::llround(
                                  reader->sampleRate * maximumSourceSeconds));
    const auto sourceSamples = static_cast<int>(juce::jmin<int64_t>(
        reader->lengthInSamples, static_cast<int64_t>(std::llround(
                                     reader->sampleRate * maximumSourceSeconds))));
    const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> decoded(channels, sourceSamples);
    if (!reader->read(&decoded, 0, sourceSamples, 0, true, true))
        return "The audio file could not be decoded";

    const auto targetSamples = juce::jmax(32, juce::roundToInt(
        static_cast<double>(sourceSamples) * currentSampleRate / reader->sampleRate));
    juce::AudioBuffer<float> resampled(channels, targetSamples);
    const auto speedRatio = reader->sampleRate / currentSampleRate;
    for (int channel = 0; channel < channels; ++channel)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.process(speedRatio, decoded.getReadPointer(channel),
                             resampled.getWritePointer(channel), targetSamples,
                             sourceSamples, 0);
    }

    loopEngine.submitSource(std::move(resampled), file.getFileName());
    return wasTruncated ? "Only the first 60 seconds were imported" : juce::String {};
}

juce::String LoopSurgeonAudioProcessor::exportLoopFile(const juce::File& requestedFile) const
{
    auto audio = loopEngine.createRenderedLoop();
    if (audio.getNumSamples() == 0)
        return "No analysed loop is ready to export";

    auto file = requestedFile.hasFileExtension("wav")
                    ? requestedFile
                    : requestedFile.withFileExtension("wav");
    if (file.existsAsFile() && !file.deleteFile())
        return "The existing output file could not be replaced";
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return "The output file could not be created";

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate(currentSampleRate)
                             .withNumChannels(audio.getNumChannels())
                             .withBitsPerSample(24);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr)
        return "The WAV encoder could not be created";
    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
        return "Writing the WAV file failed";
    return {};
}

juce::AudioProcessorEditor* LoopSurgeonAudioProcessor::createEditor()
{
    return new LoopSurgeonAudioProcessorEditor(*this);
}

void LoopSurgeonAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    const auto loopState = loopEngine.createLoopState();
    state.setProperty("stateVersion", 1, nullptr);
    if (!loopState.isEmpty())
        state.setProperty("capturedLoop", juce::var(loopState), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}

void LoopSurgeonAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
    if (state.isValid())
    {
        if (const auto* loopData = state.getProperty("capturedLoop").getBinaryData())
            loopEngine.restoreLoopState(loopData->getData(), loopData->getSize());

        state.removeProperty("capturedLoop", nullptr);
        state.removeProperty("stateVersion", nullptr);
        parameters.replaceState(state);
    }
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

    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "syncToHost", 1 },
        "Sync Capture to Host",
        true));

    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "bars", 1 },
        "Loop Bars",
        juce::StringArray { "1 bar", "2 bars", "4 bars", "8 bars" },
        0));

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
