#include "SpectralOrbitPrototype.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

namespace
{
SpectralOrbitCurve parseCurve(const juce::String& name)
{
    if (name.equalsIgnoreCase("fold"))
        return SpectralOrbitCurve::fold;
    if (name.equalsIgnoreCase("barberpole"))
        return SpectralOrbitCurve::barberpole;
    return SpectralOrbitCurve::sweep;
}
}

int main(const int argumentCount, char* arguments[])
{
    if (argumentCount < 4)
    {
        std::cerr << "Usage: LoopSurgeonSpectralOrbitLab <input> <output> "
                     "<sweep|fold|barberpole> [duration] [max-delay] [feedback] "
                     "[diffusion] [laps]\n";
        return 2;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const juce::File input(juce::String::fromUTF8(arguments[1]));
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(input));
    if (reader == nullptr || reader->lengthInSamples < 32)
        return 3;

    const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const auto samples = static_cast<int>(juce::jmin<int64_t>(
        reader->lengthInSamples,
        static_cast<int64_t>(std::llround(reader->sampleRate * 60.0))));
    juce::AudioBuffer<float> source(channels, samples);
    if (!reader->read(&source, 0, samples, 0, true, true))
        return 4;

    SpectralOrbitSettings settings;
    settings.curve = parseCurve(juce::String::fromUTF8(arguments[3]));
    if (argumentCount > 4)
        settings.requestedDurationSeconds = juce::String::fromUTF8(arguments[4]).getFloatValue();
    if (argumentCount > 5)
        settings.maximumDelayRatio = juce::String::fromUTF8(arguments[5]).getFloatValue();
    if (argumentCount > 6)
        settings.feedback = juce::String::fromUTF8(arguments[6]).getFloatValue();
    if (argumentCount > 7)
        settings.diffusionFrames = juce::String::fromUTF8(arguments[7]).getFloatValue();
    if (argumentCount > 8)
        settings.feedbackLaps = juce::String::fromUTF8(arguments[8]).getIntValue();

    const auto result = SpectralOrbitPrototype::render(source, reader->sampleRate, settings);
    if (result.audio.getNumSamples() == 0)
        return 5;

    const juce::File output(juce::String::fromUTF8(arguments[2]));
    if (output.existsAsFile() && !output.deleteFile())
        return 6;
    std::unique_ptr<juce::OutputStream> stream = output.createOutputStream();
    if (stream == nullptr)
        return 7;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream,
        juce::AudioFormatWriterOptions {}
            .withSampleRate(reader->sampleRate)
            .withNumChannels(result.audio.getNumChannels())
            .withBitsPerSample(24));
    if (writer == nullptr)
        return 8;
    stream.release();
    if (!writer->writeFromAudioSampleBuffer(
            result.audio, 0, result.audio.getNumSamples()))
        return 9;

    std::cout << "samples=" << result.audio.getNumSamples()
              << " duration=" << result.actualDurationSeconds
              << " truePeakDbtp=" << result.truePeakDbtp
              << " boundarySampleDelta=" << result.boundarySampleDelta
              << " boundarySlopeDelta=" << result.boundarySlopeDelta
              << " repairedNonFinite=" << (result.containedNonFiniteInput ? 1 : 0)
              << '\n';
    return 0;
}

