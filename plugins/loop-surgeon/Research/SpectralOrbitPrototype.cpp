#include "SpectralOrbitPrototype.h"

#include "SignalDiagnostics.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <complex>
#include <vector>

namespace
{
using Complex = std::complex<float>;

struct TransformLayout
{
    int order = 11;
    int size = 2048;
    int hop = 512;
    int bins = 1025;
};

TransformLayout chooseLayout(const double sampleRate)
{
    const auto order = sampleRate >= 32000.0 ? 11 : 10;
    const auto size = 1 << order;
    return { order, size, size / 4, size / 2 + 1 };
}

float curveValue(const SpectralOrbitCurve curve, const float normalizedFrequency)
{
    const auto logFrequency = std::log1p(63.0f * normalizedFrequency) / std::log(64.0f);
    switch (curve)
    {
        case SpectralOrbitCurve::sweep:
        {
            const auto smooth = logFrequency * logFrequency * (3.0f - 2.0f * logFrequency);
            return 0.06f + 0.88f * smooth;
        }
        case SpectralOrbitCurve::fold:
        {
            const auto phase = std::fmod(2.5f * logFrequency + 0.18f, 1.0f);
            return 0.08f + 0.84f * (1.0f - std::abs(2.0f * phase - 1.0f));
        }
        case SpectralOrbitCurve::barberpole:
            return 0.04f + 0.92f * std::fmod(3.0f * logFrequency + 0.12f, 1.0f);
    }
    return logFrequency;
}

size_t stateIndex(const int channel, const int frame, const int bin,
                  const int frameCount, const int bins)
{
    return (static_cast<size_t>(channel) * static_cast<size_t>(frameCount)
            + static_cast<size_t>(frame)) * static_cast<size_t>(bins)
           + static_cast<size_t>(bin);
}

void depositCoefficient(std::vector<Complex>& destination,
                        const Complex coefficient,
                        const int channel,
                        const int sourceFrame,
                        const int bin,
                        const float delayFrames,
                        const float diffusionFrames,
                        const int frameCount,
                        const TransformLayout layout)
{
    const auto centreDelay = juce::jmax(0.0f, delayFrames);
    const auto radius = juce::jlimit(0, 8, juce::roundToInt(
        std::ceil(2.25f * juce::jmax(0.0f, diffusionFrames))));
    auto weightSum = 0.0f;
    for (int offset = -radius; offset <= radius; ++offset)
    {
        const auto distance = static_cast<float>(offset) / juce::jmax(0.35f, diffusionFrames);
        weightSum += std::exp(-0.5f * distance * distance);
    }
    weightSum = juce::jmax(1.0e-8f, weightSum);

    const auto integerCentre = juce::roundToInt(centreDelay);
    for (int offset = -radius; offset <= radius; ++offset)
    {
        const auto actualDelay = juce::jmax(0, integerCentre + offset);
        const auto destinationFrame = (sourceFrame + actualDelay) % frameCount;
        const auto distance = static_cast<float>(offset) / juce::jmax(0.35f, diffusionFrames);
        const auto weight = std::exp(-0.5f * distance * distance) / weightSum;
        const auto delayedSamples = static_cast<float>(actualDelay * layout.hop);
        const auto phase = -juce::MathConstants<float>::twoPi
                           * static_cast<float>(bin) * delayedSamples
                           / static_cast<float>(layout.size);
        const Complex shift(std::cos(phase), std::sin(phase));
        destination[stateIndex(channel, destinationFrame, bin,
                               frameCount, layout.bins)] += coefficient * shift * weight;
    }
}

std::vector<float> makeSqrtHann(const int size)
{
    std::vector<float> window(static_cast<size_t>(size));
    for (int sample = 0; sample < size; ++sample)
    {
        const auto phase = juce::MathConstants<float>::twoPi
                           * static_cast<float>(sample)
                           / static_cast<float>(size);
        window[static_cast<size_t>(sample)] = std::sqrt(
            juce::jmax(0.0f, 0.5f - 0.5f * std::cos(phase)));
    }
    return window;
}

void measureBoundary(SpectralOrbitResult& result)
{
    if (result.audio.getNumSamples() < 3)
        return;
    auto sampleDelta = 0.0f;
    auto slopeDelta = 0.0f;
    const auto end = result.audio.getNumSamples() - 1;
    for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
    {
        const auto head = result.audio.getSample(channel, 0);
        const auto next = result.audio.getSample(channel, 1);
        const auto tail = result.audio.getSample(channel, end);
        const auto previous = result.audio.getSample(channel, end - 1);
        sampleDelta = juce::jmax(sampleDelta, std::abs(head - tail));
        slopeDelta = juce::jmax(slopeDelta, std::abs((next - head) - (tail - previous)));
    }
    result.boundarySampleDelta = sampleDelta;
    result.boundarySlopeDelta = slopeDelta;
}
}

SpectralOrbitResult SpectralOrbitPrototype::render(
    const juce::AudioBuffer<float>& source, const double sampleRate,
    const SpectralOrbitSettings& settings)
{
    SpectralOrbitResult result;
    if (source.getNumChannels() <= 0 || source.getNumSamples() < 32 || sampleRate <= 0.0)
        return result;

    const auto layout = chooseLayout(sampleRate);
    const auto channels = juce::jlimit(1, 2, source.getNumChannels());
    const auto requestedSamples = juce::jmax(
        layout.size, juce::roundToInt(sampleRate * settings.requestedDurationSeconds));
    const auto outputFrames = juce::jmax(
        4, juce::roundToInt(static_cast<double>(requestedSamples) / layout.hop));
    const auto outputSamples = outputFrames * layout.hop;
    const auto sourceFrames = juce::jmax(
        1, 1 + juce::roundToInt(std::ceil(
            static_cast<double>(source.getNumSamples() - 1) / layout.hop)));
    const auto window = makeSqrtHann(layout.size);
    juce::dsp::FFT fft(layout.order);

    const auto stateSize = static_cast<size_t>(channels)
                           * static_cast<size_t>(outputFrames)
                           * static_cast<size_t>(layout.bins);
    std::vector<Complex> current(stateSize);
    std::vector<Complex> next(stateSize);
    std::vector<Complex> accumulated(stateSize);
    std::vector<float> fftData(static_cast<size_t>(2 * layout.size));

    const auto maximumDelayFrames = juce::jlimit(
        1.0f, static_cast<float>(outputFrames - 1),
        settings.maximumDelayRatio * static_cast<float>(outputFrames));

    for (int channel = 0; channel < channels; ++channel)
    {
        for (int frame = 0; frame < sourceFrames; ++frame)
        {
            std::fill(fftData.begin(), fftData.end(), 0.0f);
            const auto start = frame * layout.hop;
            for (int sample = 0; sample < layout.size; ++sample)
            {
                const auto sourceSample = start + sample;
                if (sourceSample < source.getNumSamples())
                    fftData[static_cast<size_t>(sample)] =
                        source.getSample(channel, sourceSample)
                        * window[static_cast<size_t>(sample)];
            }
            fft.performRealOnlyForwardTransform(fftData.data());
            const auto ringFrame = frame % outputFrames;
            for (int bin = 0; bin < layout.bins; ++bin)
            {
                const auto normalizedFrequency = static_cast<float>(bin)
                                                 / static_cast<float>(layout.bins - 1);
                const auto delay = curveValue(settings.curve, normalizedFrequency)
                                   * maximumDelayFrames;
                const Complex coefficient(fftData[static_cast<size_t>(2 * bin)],
                                          fftData[static_cast<size_t>(2 * bin + 1)]);
                depositCoefficient(current, coefficient, channel, ringFrame, bin,
                                   delay, settings.diffusionFrames,
                                   outputFrames, layout);
            }
        }
    }

    const auto feedback = juce::jlimit(0.0f, 0.94f, settings.feedback);
    const auto laps = juce::jlimit(0, 12, settings.feedbackLaps);
    auto accumulationWeight = 1.0f;
    accumulated = current;
    auto totalWeight = 1.0f;
    for (int lap = 0; lap < laps; ++lap)
    {
        std::fill(next.begin(), next.end(), Complex {});
        for (int channel = 0; channel < channels; ++channel)
            for (int frame = 0; frame < outputFrames; ++frame)
                for (int bin = 0; bin < layout.bins; ++bin)
                {
                    const auto coefficient = current[stateIndex(
                        channel, frame, bin, outputFrames, layout.bins)];
                    if (std::norm(coefficient) < 1.0e-18f)
                        continue;
                    const auto normalizedFrequency = static_cast<float>(bin)
                                                     / static_cast<float>(layout.bins - 1);
                    const auto base = curveValue(settings.curve, normalizedFrequency);
                    const auto orbit = std::fmod(
                        base + 0.071f * static_cast<float>(lap + 1), 1.0f);
                    const auto delay = (0.18f + 0.82f * orbit) * maximumDelayFrames;
                    depositCoefficient(next, coefficient * feedback,
                                       channel, frame, bin, delay,
                                       settings.diffusionFrames * (1.0f + 0.12f * lap),
                                       outputFrames, layout);
                }
        current.swap(next);
        accumulationWeight *= 0.92f;
        totalWeight += accumulationWeight;
        for (size_t index = 0; index < accumulated.size(); ++index)
            accumulated[index] += current[index] * accumulationWeight;
    }
    for (auto& coefficient : accumulated)
        coefficient /= totalWeight;

    result.audio.setSize(channels, outputSamples, false, true, false);
    std::vector<float> normalization(static_cast<size_t>(outputSamples), 0.0f);
    for (int frame = 0; frame < outputFrames; ++frame)
    {
        const auto frameStart = frame * layout.hop;
        for (int sample = 0; sample < layout.size; ++sample)
        {
            const auto outputSample = (frameStart + sample) % outputSamples;
            const auto weight = window[static_cast<size_t>(sample)];
            normalization[static_cast<size_t>(outputSample)] += weight * weight;
        }
        for (int channel = 0; channel < channels; ++channel)
        {
            std::fill(fftData.begin(), fftData.end(), 0.0f);
            for (int bin = 0; bin < layout.bins; ++bin)
            {
                const auto coefficient = accumulated[stateIndex(
                    channel, frame, bin, outputFrames, layout.bins)];
                fftData[static_cast<size_t>(2 * bin)] = coefficient.real();
                fftData[static_cast<size_t>(2 * bin + 1)] = coefficient.imag();
            }
            fftData[1] = 0.0f;
            fftData[static_cast<size_t>(2 * (layout.bins - 1) + 1)] = 0.0f;
            fft.performRealOnlyInverseTransform(fftData.data());
            for (int sample = 0; sample < layout.size; ++sample)
            {
                const auto outputSample = (frameStart + sample) % outputSamples;
                result.audio.addSample(channel, outputSample,
                    fftData[static_cast<size_t>(sample)]
                    * window[static_cast<size_t>(sample)]);
            }
        }
    }
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < outputSamples; ++sample)
            result.audio.setSample(channel, sample,
                result.audio.getSample(channel, sample)
                / juce::jmax(1.0e-6f, normalization[static_cast<size_t>(sample)]));

    result.containedNonFiniteInput =
        !SignalDiagnostics::repairNonFiniteAndRemoveDc(result.audio);
    const auto sourceRms = SignalDiagnostics::calculateRms(source);
    const auto outputRms = SignalDiagnostics::calculateRms(result.audio);
    if (sourceRms > 1.0e-7f && outputRms > 1.0e-7f)
        result.audio.applyGain(juce::jlimit(
            0.25f, juce::Decibels::decibelsToGain(24.0f), sourceRms / outputRms));
    result.truePeakDbtp = SignalDiagnostics::applyCircularTruePeakCeiling(
        result.audio, -1.0f);
    result.actualDurationSeconds = static_cast<double>(outputSamples) / sampleRate;
    measureBoundary(result);
    return result;
}

