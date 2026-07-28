#include "TextureSynthesizer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

int main(const int argumentCount, char** arguments)
{
    if (argumentCount < 3)
    {
        std::cerr << "Usage: LoopSurgeonTextureRender <input> <output> "
                     "[selection-seconds] [duration-seconds]\n";
        return 2;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const juce::File input(juce::String::fromUTF8(arguments[1]));
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(input));
    if (reader == nullptr)
    {
        std::cerr << "Could not read input\n";
        return 3;
    }

    const auto selectionSeconds = argumentCount > 3
        ? juce::String::fromUTF8(arguments[3]).getDoubleValue()
        : static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    const auto sourceSamples = static_cast<int>(juce::jlimit<int64_t>(
        128, reader->lengthInSamples,
        static_cast<int64_t>(std::llround(selectionSeconds * reader->sampleRate))));
    const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> decoded(channels, sourceSamples);
    if (!reader->read(&decoded, 0, sourceSamples, 0, true, true))
        return 4;

    constexpr auto outputSampleRate = 48000.0;
    const auto resampledSamples = juce::roundToInt(
        static_cast<double>(sourceSamples) * outputSampleRate / reader->sampleRate);
    juce::AudioBuffer<float> resampled(channels, resampledSamples);
    const auto ratio = reader->sampleRate / outputSampleRate;
    for (int channel = 0; channel < channels; ++channel)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.process(ratio, decoded.getReadPointer(channel),
                             resampled.getWritePointer(channel), resampledSamples,
                             sourceSamples, 0);
    }

    TextureSynthesisSettings settings;
    settings.durationSeconds = argumentCount > 4
        ? juce::String::fromUTF8(arguments[4]).getFloatValue() : 24.0f;
    settings.variation = 0.72f;
    settings.seed = 0x5a17b33fu;
    const auto result = TextureSynthesizer::synthesize(
        resampled, outputSampleRate, settings);
    if (result.audio.getNumSamples() == 0)
        return 5;

    const juce::File output(juce::String::fromUTF8(arguments[2]));
    output.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = output.createOutputStream();
    if (stream == nullptr)
        return 6;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions {}
            .withSampleRate(outputSampleRate)
            .withNumChannels(result.audio.getNumChannels())
            .withBitsPerSample(24));
    if (writer == nullptr
        || !writer->writeFromAudioSampleBuffer(
            result.audio, 0, result.audio.getNumSamples()))
        return 7;

    std::cout << "samples=" << result.audio.getNumSamples()
              << " closure=" << result.closureQuality
              << " stationary=" << result.transitionQuality
              << " spectrum=" << result.spectrumPreservation
              << " stereo=" << result.stereoPreservation
              << " stability=" << result.macroStability << '\n';
    return 0;
}
