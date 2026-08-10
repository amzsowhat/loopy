#include "TextureSynthesizer.h"

#include "RenderQuality.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace
{
constexpr int spectrumBands = 8;
constexpr int maximumAnalysisFrames = 256;
constexpr int fingerprintBands = 96;

struct BoundaryFeature
{
    std::array<float, spectrumBands> spectrum {};
    float rms = 0.0f;
    float derivative = 0.0f;
    float stereoCorrelation = 1.0f;
};

class DeterministicRandom
{
public:
    explicit DeterministicRandom(const uint32_t initial)
        : state(initial != 0 ? initial : 1u)
    {
    }

    uint32_t next() noexcept
    {
        auto value = state;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        state = value;
        return value;
    }

    float unit() noexcept
    {
        return static_cast<float>(next() & 0x00ffffffu)
               / static_cast<float>(0x01000000u);
    }

private:
    uint32_t state;
};

float readMono(const juce::AudioBuffer<float>& audio, const int sample)
{
    auto value = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        value += audio.getSample(channel, sample);
    return value / static_cast<float>(juce::jmax(1, audio.getNumChannels()));
}

float readComponent(const juce::AudioBuffer<float>& audio,
                    const int component,
                    const int sample)
{
    if (audio.getNumChannels() < 2)
        return audio.getSample(0, sample);
    constexpr auto inverseRootTwo = 0.7071067811865475f;
    const auto left = audio.getSample(0, sample);
    const auto right = audio.getSample(1, sample);
    return component == 0 ? (left + right) * inverseRootTwo
                          : (left - right) * inverseRootTwo;
}

BoundaryFeature analyseBoundary(const juce::AudioBuffer<float>& source,
                                const int firstSample,
                                const int sampleCount)
{
    BoundaryFeature feature;
    const auto count = juce::jlimit(
        16, source.getNumSamples(), juce::jmax(16, sampleCount));
    const auto start = juce::jlimit(
        0, juce::jmax(0, source.getNumSamples() - count), firstSample);
    const auto stride = juce::jmax(1, count / 256);
    auto energy = 0.0;
    auto differenceEnergy = 0.0;
    auto stereoDot = 0.0;
    auto stereoLeft = 0.0;
    auto stereoRight = 0.0;
    auto previous = readMono(source, start);
    int analysed = 0;

    for (int offset = 0; offset < count; offset += stride)
    {
        const auto position = juce::jmin(source.getNumSamples() - 1, start + offset);
        const auto value = readMono(source, position);
        energy += static_cast<double>(value) * value;
        const auto difference = value - previous;
        differenceEnergy += static_cast<double>(difference) * difference;
        previous = value;
        if (source.getNumChannels() > 1)
        {
            const auto left = source.getSample(0, position);
            const auto right = source.getSample(1, position);
            stereoDot += static_cast<double>(left) * right;
            stereoLeft += static_cast<double>(left) * left;
            stereoRight += static_cast<double>(right) * right;
        }
        ++analysed;
    }

    feature.rms = std::sqrt(
        static_cast<float>(energy / static_cast<double>(juce::jmax(1, analysed))));
    feature.derivative = std::sqrt(
        static_cast<float>(differenceEnergy
                           / static_cast<double>(juce::jmax(1, analysed))));
    if (source.getNumChannels() > 1)
        feature.stereoCorrelation = static_cast<float>(
            stereoDot / std::sqrt(juce::jmax(1.0e-12, stereoLeft * stereoRight)));

    constexpr int transformSize = 128;
    std::array<float, transformSize> window {};
    for (int index = 0; index < transformSize; ++index)
    {
        const auto sourceOffset = index * juce::jmax(1, count - 1)
                                  / (transformSize - 1);
        const auto position = juce::jmin(source.getNumSamples() - 1,
                                         start + sourceOffset);
        const auto hann = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(index)
            / static_cast<float>(transformSize - 1));
        window[static_cast<size_t>(index)] = readMono(source, position) * hann;
    }
    for (int band = 0; band < spectrumBands; ++band)
    {
        const auto bin = 1 + band * 3;
        auto real = 0.0;
        auto imaginary = 0.0;
        for (int index = 0; index < transformSize; ++index)
        {
            const auto phase = juce::MathConstants<double>::twoPi
                               * static_cast<double>(bin * index)
                               / static_cast<double>(transformSize);
            real += window[static_cast<size_t>(index)] * std::cos(phase);
            imaginary -= window[static_cast<size_t>(index)] * std::sin(phase);
        }
        feature.spectrum[static_cast<size_t>(band)] = std::log1p(
            static_cast<float>(std::sqrt(real * real + imaginary * imaginary)));
    }
    const auto spectrumTotal = std::accumulate(
        feature.spectrum.begin(), feature.spectrum.end(), 1.0e-6f);
    for (auto& value : feature.spectrum)
        value /= spectrumTotal;
    return feature;
}

float spectrumDistance(const BoundaryFeature& left, const BoundaryFeature& right)
{
    auto distance = 0.0f;
    for (int band = 0; band < spectrumBands; ++band)
        distance += std::abs(left.spectrum[static_cast<size_t>(band)]
                             - right.spectrum[static_cast<size_t>(band)]);
    return distance;
}

float fullFeatureDistance(const BoundaryFeature& left, const BoundaryFeature& right)
{
    const auto level = std::abs(left.rms - right.rms)
                       / juce::jmax(0.01f, left.rms + right.rms);
    const auto derivative = std::abs(left.derivative - right.derivative)
                            / juce::jmax(0.01f,
                                        left.derivative + right.derivative);
    const auto stereo = 0.5f * std::abs(left.stereoCorrelation
                                        - right.stereoCorrelation);
    return 0.56f * spectrumDistance(left, right)
           + 0.20f * level + 0.16f * derivative + 0.08f * stereo;
}

float similarityScore(const float distance, const float sensitivity = 2.6f)
{
    return 100.0f * std::exp(
        -sensitivity * juce::jmax(0.0f, distance));
}

BoundaryFeature analyseWhole(const juce::AudioBuffer<float>& audio,
                             const double sampleRate)
{
    BoundaryFeature result;
    constexpr int observations = 12;
    const auto window = juce::jlimit(
        64, audio.getNumSamples(),
        juce::roundToInt(sampleRate * 0.24));
    for (int observation = 0; observation < observations; ++observation)
    {
        const auto maximumStart = juce::jmax(0, audio.getNumSamples() - window);
        const auto start = observation * maximumStart
                           / juce::jmax(1, observations - 1);
        const auto feature = analyseBoundary(audio, start, window);
        result.rms += feature.rms;
        result.derivative += feature.derivative;
        result.stereoCorrelation += feature.stereoCorrelation;
        for (int band = 0; band < spectrumBands; ++band)
            result.spectrum[static_cast<size_t>(band)]
                += feature.spectrum[static_cast<size_t>(band)];
    }
    result.rms /= static_cast<float>(observations);
    result.derivative /= static_cast<float>(observations);
    result.stereoCorrelation /= static_cast<float>(observations);
    for (auto& value : result.spectrum)
        value /= static_cast<float>(observations);
    return result;
}

float sampledPercentile(const std::vector<float>& values, const float proportion)
{
    if (values.empty())
        return 0.0f;
    auto copy = values;
    const auto position = static_cast<size_t>(juce::roundToInt(
        juce::jlimit(0.0f, 1.0f, proportion)
        * static_cast<float>(copy.size() - 1u)));
    std::nth_element(copy.begin(), copy.begin() + static_cast<ptrdiff_t>(position),
                     copy.end());
    return copy[position];
}

float frameRms(const juce::AudioBuffer<float>& source,
               const int start,
               const int sampleCount)
{
    auto energy = 0.0;
    for (int sample = 0; sample < sampleCount; ++sample)
        for (int channel = 0; channel < source.getNumChannels(); ++channel)
        {
            const auto value = source.getSample(channel, start + sample);
            energy += static_cast<double>(value) * value;
        }
    return std::sqrt(static_cast<float>(
        energy / static_cast<double>(
            sampleCount * juce::jmax(1, source.getNumChannels()))));
}

struct TextureGrain
{
    int start = 0;
    float rms = 0.0f;
    float envelopeRangeDb = 0.0f;
    BoundaryFeature head;
    BoundaryFeature tail;
};

float grainEnvelopeRangeDb(const juce::AudioBuffer<float>& source,
                           const int start,
                           const int sampleCount)
{
    constexpr int observations = 8;
    const auto blockSamples = juce::jmax(16, sampleCount / observations);
    std::vector<float> levels;
    levels.reserve(observations);
    for (int observation = 0; observation < observations; ++observation)
    {
        const auto offset = juce::jmin(
            sampleCount - blockSamples,
            observation * juce::jmax(0, sampleCount - blockSamples)
                / juce::jmax(1, observations - 1));
        levels.push_back(frameRms(source, start + offset, blockSamples));
    }
    const auto low = juce::jmax(1.0e-8f, sampledPercentile(levels, 0.15f));
    const auto high = juce::jmax(low, sampledPercentile(levels, 0.85f));
    return 20.0f * std::log10(high / low);
}

struct SpatialMeasurement
{
    float correlation = 1.0f;
    float imbalanceDb = 0.0f;
};

SpatialMeasurement analyseSelectedSpatial(
    const juce::AudioBuffer<float>& source,
    const std::vector<int>& frameStarts,
    const std::vector<int>& selectedFrames,
    const int frameSamples)
{
    SpatialMeasurement result;
    if (source.getNumChannels() < 2)
        return result;

    auto leftEnergy = 0.0;
    auto rightEnergy = 0.0;
    auto dot = 0.0;
    for (const auto frame : selectedFrames)
    {
        const auto start = frameStarts[static_cast<size_t>(frame)];
        for (int sample = 0; sample < frameSamples; ++sample)
        {
            const auto left = static_cast<double>(source.getSample(0, start + sample));
            const auto right = static_cast<double>(source.getSample(1, start + sample));
            leftEnergy += left * left;
            rightEnergy += right * right;
            dot += left * right;
        }
    }
    const auto denominator = std::sqrt(leftEnergy * rightEnergy);
    if (denominator > 1.0e-12)
        result.correlation = static_cast<float>(
            juce::jlimit(-1.0, 1.0, dot / denominator));
    result.imbalanceDb = 10.0f * std::log10(static_cast<float>(
        juce::jmax(1.0e-12, leftEnergy) / juce::jmax(1.0e-12, rightEnergy)));
    return result;
}

void applyCircularMacroMovement(juce::AudioBuffer<float>& audio,
                                const float sourceRangeDb,
                                const float flatten,
                                DeterministicRandom& random)
{
    const auto depthDb = juce::jlimit(0.0f, 9.0f, sourceRangeDb)
                         * (1.0f - flatten) * 0.48f;
    if (depthDb < 0.01f || audio.getNumSamples() == 0)
        return;

    constexpr std::array<float, 3> weights { 0.58f, 0.29f, 0.13f };
    std::array<float, weights.size()> phases {};
    std::array<int, weights.size()> cycles {};
    for (size_t layer = 0; layer < weights.size(); ++layer)
    {
        phases[layer] = juce::MathConstants<float>::twoPi * random.unit();
        cycles[layer] = 1 + static_cast<int>(random.next() % static_cast<uint32_t>(2 + layer * 2));
    }

    const auto gainAt = [&] (const int sample)
    {
        const auto position = static_cast<float>(sample)
                              / static_cast<float>(audio.getNumSamples());
        auto movement = 0.0f;
        for (size_t layer = 0; layer < weights.size(); ++layer)
            movement += weights[layer] * std::sin(
                juce::MathConstants<float>::twoPi
                    * static_cast<float>(cycles[layer]) * position
                + phases[layer]);
        return juce::Decibels::decibelsToGain(depthDb * movement);
    };
    auto squareGain = 0.0;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto gain = gainAt(sample);
        squareGain += static_cast<double>(gain) * gain;
    }
    const auto normalisation = 1.0f / std::sqrt(static_cast<float>(
        squareGain / static_cast<double>(audio.getNumSamples())));
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample(channel, sample, audio.getSample(channel, sample)
                * gainAt(sample) * normalisation);
}

void matchStereoImage(juce::AudioBuffer<float>& audio,
                      const SpatialMeasurement source,
                      const float amount)
{
    if (audio.getNumChannels() < 2 || audio.getNumSamples() == 0 || amount <= 0.0f)
        return;

    const auto originalRms = RenderQuality::calculateRms(audio);
    const auto currentCorrelation = RenderQuality::calculateStereoCorrelation(audio);
    const auto desiredCorrelation = juce::jlimit(
        -0.92f, 0.98f, currentCorrelation
            + amount * (source.correlation - currentCorrelation));

    auto midEnergy = 0.0;
    auto sideEnergy = 0.0;
    constexpr auto inverseRootTwo = 0.7071067811865475f;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto left = audio.getSample(0, sample);
        const auto right = audio.getSample(1, sample);
        const auto mid = (left + right) * inverseRootTwo;
        const auto side = (left - right) * inverseRootTwo;
        midEnergy += static_cast<double>(mid) * mid;
        sideEnergy += static_cast<double>(side) * side;
    }
    const auto currentRatio = std::sqrt(static_cast<float>(
        juce::jmax(1.0e-12, sideEnergy) / juce::jmax(1.0e-12, midEnergy)));
    const auto desiredRatio = std::sqrt(
        (1.0f - desiredCorrelation) / (1.0f + desiredCorrelation));
    const auto sideScale = juce::jlimit(0.25f, 4.0f,
        desiredRatio / juce::jmax(1.0e-6f, currentRatio));
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto left = audio.getSample(0, sample);
        const auto right = audio.getSample(1, sample);
        const auto mid = (left + right) * inverseRootTwo;
        const auto side = (left - right) * inverseRootTwo * sideScale;
        audio.setSample(0, sample, (mid + side) * inverseRootTwo);
        audio.setSample(1, sample, (mid - side) * inverseRootTwo);
    }

    const auto currentImbalance = RenderQuality::calculateStereoLevelImbalanceDb(audio);
    const auto desiredImbalance = currentImbalance
                                  + amount * (source.imbalanceDb - currentImbalance);
    const auto correctionDb = juce::jlimit(-12.0f, 12.0f,
                                            desiredImbalance - currentImbalance);
    audio.applyGain(0, 0, audio.getNumSamples(),
                    juce::Decibels::decibelsToGain(0.5f * correctionDb));
    audio.applyGain(1, 0, audio.getNumSamples(),
                    juce::Decibels::decibelsToGain(-0.5f * correctionDb));

    const auto adjustedRms = RenderQuality::calculateRms(audio);
    if (adjustedRms > 1.0e-9f)
        audio.applyGain(originalRms / adjustedRms);
}

std::vector<float> analyseFrameSpectrum(const juce::AudioBuffer<float>& source,
                                        const int component,
                                        const int start,
                                        const int fftSize,
                                        juce::dsp::FFT& fft)
{
    std::vector<float> transform(static_cast<size_t>(2 * fftSize), 0.0f);
    for (int sample = 0; sample < fftSize; ++sample)
    {
        const auto hann = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(sample)
            / static_cast<float>(fftSize - 1));
        transform[static_cast<size_t>(sample)]
            = readComponent(source, component, start + sample) * hann;
    }
    fft.performRealOnlyForwardTransform(transform.data(), true);
    std::vector<float> logMagnitude(static_cast<size_t>(fftSize / 2 + 1));
    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        const auto real = transform[static_cast<size_t>(2 * bin)];
        const auto imaginary = transform[static_cast<size_t>(2 * bin + 1)];
        logMagnitude[static_cast<size_t>(bin)] = std::log(
            juce::jmax(1.0e-9f, std::sqrt(real * real + imaginary * imaginary)));
    }
    return logMagnitude;
}

std::vector<float> buildStationaryModel(
    const std::vector<std::vector<float>>& spectra)
{
    if (spectra.empty())
        return {};
    const auto bins = spectra.front().size();
    std::vector<float> raw(bins, 0.0f);
    std::vector<float> values;
    values.reserve(spectra.size());
    for (size_t bin = 0; bin < bins; ++bin)
    {
        values.clear();
        for (const auto& spectrum : spectra)
            values.push_back(spectrum[bin]);
        const auto middle = values.begin()
                            + static_cast<ptrdiff_t>(values.size() / 2u);
        std::nth_element(values.begin(), middle, values.end());
        raw[bin] = *middle;
    }

    std::vector<double> prefix(bins + 1u, 0.0);
    for (size_t bin = 0; bin < bins; ++bin)
        prefix[bin + 1u] = prefix[bin] + raw[bin];

    std::vector<float> model(bins, 0.0f);
    for (size_t bin = 0; bin < bins; ++bin)
    {
        const auto radius = juce::jmax(
            2, juce::roundToInt(static_cast<float>(bin) * 0.025f));
        const auto first = bin > static_cast<size_t>(radius)
            ? bin - static_cast<size_t>(radius) : 0u;
        const auto last = juce::jmin(
            bins, bin + static_cast<size_t>(radius) + 1u);
        const auto smoothed = static_cast<float>(
            (prefix[last] - prefix[first])
            / static_cast<double>(last - first));
        const auto isolatedPeak = juce::jlimit(
            0.0f, 1.0f, (raw[bin] - smoothed - 0.18f) / 0.90f);
        const auto rawWeight = 0.68f - 0.30f * isolatedPeak;
        model[bin] = std::exp(rawWeight * raw[bin]
                              + (1.0f - rawWeight) * smoothed);
    }
    model.front() = 0.0f;
    return model;
}

std::vector<float> buildMaterialColourModel(
    const std::vector<std::vector<float>>& spectra)
{
    const auto stationary = buildStationaryModel(spectra);
    if (stationary.empty())
        return {};
    std::vector<float> colour(stationary.size(), 0.0f);
    for (size_t bin = 0; bin < stationary.size(); ++bin)
    {
        auto power = 0.0;
        for (const auto& spectrum : spectra)
        {
            const auto magnitude = std::exp(spectrum[bin]);
            power += static_cast<double>(magnitude) * magnitude;
        }
        const auto rmsMagnitude = std::sqrt(
            power / static_cast<double>(juce::jmax<size_t>(1u, spectra.size())));
        // The stationary median defines the continuous bed. A controlled amount of the frame-RMS
        // colour restores resonant bands that are brief but perceptually define the material.
        colour[bin] = std::exp(
            0.58f * std::log(juce::jmax(1.0e-9f, stationary[bin]))
            + 0.42f * std::log(juce::jmax(1.0e-9f,
                                         static_cast<float>(rmsMagnitude))));
    }
    colour.front() = 0.0f;
    return colour;
}

std::vector<float> buildTonalWeights(const std::vector<float>& model)
{
    std::vector<float> weights(model.size(), 0.0f);
    if (model.size() < 5u)
        return weights;

    for (size_t bin = 1; bin + 1u < model.size(); ++bin)
    {
        const auto first = bin > 5u ? bin - 5u : 1u;
        const auto last = juce::jmin(model.size() - 1u, bin + 5u);
        auto neighbourhood = 0.0f;
        auto count = 0;
        for (auto neighbour = first; neighbour <= last; ++neighbour)
        {
            if (neighbour == bin)
                continue;
            neighbourhood += model[neighbour];
            ++count;
        }
        const auto localMean = neighbourhood / static_cast<float>(juce::jmax(1, count));
        const auto prominence = model[bin] / juce::jmax(1.0e-9f, localMean);
        const auto peak = juce::jlimit(0.0f, 1.0f, (prominence - 1.05f) / 1.55f);
        // A small coherent floor prevents the stochastic layer from reducing every broadband
        // material to featureless shaped noise, while the cap avoids a bank-of-sines sound.
        const auto shapedPeak = peak * peak * (3.0f - 2.0f * peak);
        weights[bin] = 0.04f + 0.90f * shapedPeak;
    }
    return weights;
}

juce::AudioBuffer<float> synthesizeMaterialModel(
    const std::array<std::vector<std::vector<float>>, 2>& sourceSpectra,
    const std::array<std::vector<float>, 2>& models,
    const int outputChannels,
    const int fftOrder,
    const int targetSamples,
    const float variation,
    DeterministicRandom& random)
{
    const auto fftSize = 1 << fftOrder;
    const auto bins = fftSize / 2 + 1;
    const auto hop = fftSize / 4;
    const auto components = outputChannels > 1 ? 2 : 1;
    const auto frameCount = juce::jmax(4, (targetSamples + hop - 1) / hop);
    juce::AudioBuffer<float> componentAudio(components, targetSamples);
    componentAudio.clear();
    std::vector<float> normalisation(static_cast<size_t>(targetSamples), 0.0f);
    std::vector<float> window(static_cast<size_t>(fftSize), 0.0f);
    for (int sample = 0; sample < fftSize; ++sample)
        window[static_cast<size_t>(sample)] = std::sqrt(juce::jmax(
            0.0f, 0.5f - 0.5f * std::cos(
                juce::MathConstants<float>::twoPi * static_cast<float>(sample)
                / static_cast<float>(fftSize - 1))));

    juce::dsp::FFT transform(fftOrder);
    for (int component = 0; component < components; ++component)
    {
        const auto& model = models[static_cast<size_t>(component)];
        const auto& exemplars = sourceSpectra[static_cast<size_t>(component)];
        if (model.size() < static_cast<size_t>(bins) || exemplars.empty())
            continue;
        const auto tonalWeights = buildTonalWeights(model);
        std::vector<float> tonalPhase(static_cast<size_t>(bins), 0.0f);
        std::vector<float> packed(static_cast<size_t>(2 * fftSize), 0.0f);
        for (int bin = 0; bin < bins; ++bin)
            tonalPhase[static_cast<size_t>(bin)]
                = juce::MathConstants<float>::twoPi * random.unit();

        auto previousExemplar = static_cast<int>(
            random.next() % static_cast<uint32_t>(exemplars.size()));
        auto nextExemplar = previousExemplar;
        constexpr auto holdFrames = 8;
        for (int frame = 0; frame < frameCount; ++frame)
        {
            if (frame % holdFrames == 0)
            {
                previousExemplar = nextExemplar;
                nextExemplar = static_cast<int>(
                    random.next() % static_cast<uint32_t>(exemplars.size()));
            }
            const auto blend = static_cast<float>(frame % holdFrames)
                               / static_cast<float>(holdFrames);
            std::fill(packed.begin(), packed.end(), 0.0f);
            for (int bin = 1; bin < bins; ++bin)
            {
                const auto modelMagnitude = juce::jmax(
                    1.0e-9f, model[static_cast<size_t>(bin)]);
                const auto previousLog = exemplars[static_cast<size_t>(previousExemplar)]
                                                   [static_cast<size_t>(bin)];
                const auto nextLog = exemplars[static_cast<size_t>(nextExemplar)]
                                               [static_cast<size_t>(bin)];
                const auto exemplarLog = previousLog + blend * (nextLog - previousLog);
                const auto deviation = juce::jlimit(
                    -1.15f, 1.15f, exemplarLog - std::log(modelMagnitude));
                const auto movement = 0.05f + 0.20f * variation;
                const auto magnitude = modelMagnitude * std::exp(movement * deviation);
                const auto tonal = tonalWeights[static_cast<size_t>(bin)];
                const auto expectedAdvance = juce::MathConstants<float>::twoPi
                    * static_cast<float>(bin * hop) / static_cast<float>(fftSize);
                const auto drift = (random.unit() - 0.5f) * 0.012f * variation;
                tonalPhase[static_cast<size_t>(bin)] += expectedAdvance + drift;
                const auto noisePhase = juce::MathConstants<float>::twoPi * random.unit();
                const auto tonalMagnitude = magnitude * std::sqrt(tonal);
                const auto residualMagnitude = magnitude * std::sqrt(1.0f - tonal);
                packed[static_cast<size_t>(2 * bin)]
                    = tonalMagnitude * std::cos(tonalPhase[static_cast<size_t>(bin)])
                      + residualMagnitude * std::cos(noisePhase);
                packed[static_cast<size_t>(2 * bin + 1)]
                    = tonalMagnitude * std::sin(tonalPhase[static_cast<size_t>(bin)])
                      + residualMagnitude * std::sin(noisePhase);
            }
            packed[0] = 0.0f;
            packed[1] = 0.0f;
            packed[static_cast<size_t>(fftSize + 1)] = 0.0f;
            transform.performRealOnlyInverseTransform(packed.data());
            const auto outputStart = (frame * hop) % targetSamples;
            for (int sample = 0; sample < fftSize; ++sample)
            {
                const auto position = (outputStart + sample) % targetSamples;
                const auto gain = window[static_cast<size_t>(sample)];
                componentAudio.addSample(component, position,
                                         packed[static_cast<size_t>(sample)] * gain);
                if (component == 0)
                    normalisation[static_cast<size_t>(position)] += gain * gain;
            }
        }
    }

    for (int component = 0; component < components; ++component)
        for (int sample = 0; sample < targetSamples; ++sample)
            componentAudio.setSample(
                component, sample,
                componentAudio.getSample(component, sample)
                    / juce::jmax(1.0e-6f, normalisation[static_cast<size_t>(sample)]));

    juce::AudioBuffer<float> result(outputChannels, targetSamples);
    if (outputChannels < 2)
    {
        result.copyFrom(0, 0, componentAudio, 0, 0, targetSamples);
        return result;
    }
    constexpr auto inverseRootTwo = 0.7071067811865475f;
    for (int sample = 0; sample < targetSamples; ++sample)
    {
        const auto mid = componentAudio.getSample(0, sample);
        const auto side = componentAudio.getSample(1, sample);
        result.setSample(0, sample, (mid + side) * inverseRootTwo);
        result.setSample(1, sample, (mid - side) * inverseRootTwo);
    }
    return result;
}

void matchMaterialColour(juce::AudioBuffer<float>& audio,
                         const std::vector<float>& targetModel,
                         const int fftOrder,
                         const double sampleRate,
                         const float amount)
{
    const auto fftSize = 1 << fftOrder;
    const auto bins = fftSize / 2 + 1;
    if (audio.getNumSamples() < fftSize
        || targetModel.size() < static_cast<size_t>(bins) || amount <= 0.0f)
        return;

    juce::dsp::FFT transform(fftOrder);
    constexpr auto observations = 24;
    std::vector<std::vector<float>> spectra;
    spectra.reserve(observations);
    const auto maximumStart = audio.getNumSamples() - fftSize;
    for (int observation = 0; observation < observations; ++observation)
        spectra.push_back(analyseFrameSpectrum(
            audio, 0, observation * maximumStart / (observations - 1),
            fftSize, transform));
    const auto currentModel = buildStationaryModel(spectra);
    if (currentModel.size() < static_cast<size_t>(bins))
        return;

    std::vector<float> rawLogCorrection(static_cast<size_t>(bins), 0.0f);
    for (int bin = 1; bin < bins; ++bin)
        rawLogCorrection[static_cast<size_t>(bin)] = juce::jlimit(
            -1.04f, 1.04f,
            std::log(juce::jmax(1.0e-9f, targetModel[static_cast<size_t>(bin)]))
                - std::log(juce::jmax(1.0e-9f, currentModel[static_cast<size_t>(bin)])));

    std::vector<double> prefix(static_cast<size_t>(bins + 1), 0.0);
    for (int bin = 0; bin < bins; ++bin)
        prefix[static_cast<size_t>(bin + 1)]
            = prefix[static_cast<size_t>(bin)]
              + rawLogCorrection[static_cast<size_t>(bin)];
    std::vector<float> correction(static_cast<size_t>(bins), 1.0f);
    for (int bin = 1; bin < bins; ++bin)
    {
        const auto radius = juce::jmax(3, juce::roundToInt(bin * 0.035f));
        const auto first = juce::jmax(1, bin - radius);
        const auto last = juce::jmin(bins, bin + radius + 1);
        const auto smoothed = static_cast<float>(
            (prefix[static_cast<size_t>(last)]
             - prefix[static_cast<size_t>(first)])
            / static_cast<double>(last - first));
        correction[static_cast<size_t>(bin)] = std::exp(amount * smoothed);
    }

    const auto originalRms = RenderQuality::calculateRms(audio);
    const auto hop = fftSize / 4;
    const auto frames = juce::jmax(4, (audio.getNumSamples() + hop - 1) / hop);
    juce::AudioBuffer<float> balanced(audio.getNumChannels(), audio.getNumSamples());
    balanced.clear();
    std::vector<float> normalisation(static_cast<size_t>(audio.getNumSamples()), 0.0f);
    std::vector<float> packed(static_cast<size_t>(2 * fftSize), 0.0f);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        for (int frame = 0; frame < frames; ++frame)
        {
            const auto start = (frame * hop) % audio.getNumSamples();
            std::fill(packed.begin(), packed.end(), 0.0f);
            for (int sample = 0; sample < fftSize; ++sample)
            {
                const auto window = std::sqrt(juce::jmax(
                    0.0f, 0.5f - 0.5f * std::cos(
                        juce::MathConstants<float>::twoPi
                        * static_cast<float>(sample)
                        / static_cast<float>(fftSize - 1))));
                packed[static_cast<size_t>(sample)]
                    = audio.getSample(channel, (start + sample) % audio.getNumSamples())
                      * window;
            }
            transform.performRealOnlyForwardTransform(packed.data(), true);
            for (int bin = 1; bin < bins; ++bin)
            {
                const auto gain = correction[static_cast<size_t>(bin)];
                packed[static_cast<size_t>(2 * bin)] *= gain;
                packed[static_cast<size_t>(2 * bin + 1)] *= gain;
            }
            packed[0] = 0.0f;
            packed[1] = 0.0f;
            transform.performRealOnlyInverseTransform(packed.data());
            for (int sample = 0; sample < fftSize; ++sample)
            {
                const auto position = (start + sample) % audio.getNumSamples();
                const auto window = std::sqrt(juce::jmax(
                    0.0f, 0.5f - 0.5f * std::cos(
                        juce::MathConstants<float>::twoPi
                        * static_cast<float>(sample)
                        / static_cast<float>(fftSize - 1))));
                balanced.addSample(channel, position,
                                   packed[static_cast<size_t>(sample)] * window);
                if (channel == 0)
                    normalisation[static_cast<size_t>(position)] += window * window;
            }
        }
    }
    for (int channel = 0; channel < balanced.getNumChannels(); ++channel)
        for (int sample = 0; sample < balanced.getNumSamples(); ++sample)
            balanced.setSample(
                channel, sample,
                balanced.getSample(channel, sample)
                    / juce::jmax(1.0e-6f, normalisation[static_cast<size_t>(sample)]));
    const auto balancedRms = RenderQuality::calculateRms(balanced);
    if (balancedRms > 1.0e-9f && originalRms > 1.0e-9f)
        balanced.applyGain(originalRms / balancedRms);
    audio = std::move(balanced);
    juce::ignoreUnused(sampleRate);
}

juce::AudioBuffer<float> closeConstructedLoop(
    const juce::AudioBuffer<float>& construction,
    const double sampleRate,
    const int overlapSamples)
{
    const auto samples = construction.getNumSamples();
    const auto overlap = juce::jlimit(0, samples / 8, overlapSamples);
    if (construction.getNumChannels() == 0 || samples < 64 || overlap < 2)
        return construction;

    const auto guard = juce::jmax(
        overlap + 2, juce::roundToInt(sampleRate * 0.20));
    const auto firstCut = juce::jmin(samples / 2, guard);
    const auto lastCut = juce::jmax(firstCut, samples - guard);
    const auto scanStep = juce::jmax(1, juce::roundToInt(sampleRate * 0.005));
    const auto localWindow = juce::jmax(8, juce::roundToInt(sampleRate * 0.012));
    auto bestCut = samples / 2;
    auto bestScore = std::numeric_limits<float>::max();
    for (int cut = firstCut; cut <= lastCut; cut += scanStep)
    {
        auto derivative = 0.0f;
        auto energy = 0.0f;
        const auto start = juce::jmax(1, cut - localWindow);
        const auto end = juce::jmin(samples - 1, cut + localWindow);
        for (int sample = start; sample <= end; ++sample)
            for (int channel = 0; channel < construction.getNumChannels(); ++channel)
            {
                const auto value = construction.getSample(channel, sample);
                derivative += std::abs(
                    value - construction.getSample(channel, sample - 1));
                energy += value * value;
            }
        const auto count = static_cast<float>(juce::jmax(
            1, (end - start + 1) * construction.getNumChannels()));
        const auto score = derivative / count + 0.04f * std::sqrt(energy / count);
        if (score < bestScore)
        {
            bestScore = score;
            bestCut = cut;
        }
    }

    const auto renderedSamples = samples - overlap;
    juce::AudioBuffer<float> rendered(
        construction.getNumChannels(), renderedSamples);
    const auto tailLength = samples - bestCut;
    const auto prefixLength = tailLength - overlap;
    const auto suffixLength = bestCut - overlap;
    for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
    {
        if (prefixLength > 0)
            rendered.copyFrom(channel, 0, construction, channel,
                              bestCut, prefixLength);
        for (int sample = 0; sample < overlap; ++sample)
        {
            const auto position = static_cast<float>(sample + 1)
                                  / static_cast<float>(overlap + 1);
            const auto tailGain = std::cos(
                position * juce::MathConstants<float>::halfPi);
            const auto headGain = std::sin(
                position * juce::MathConstants<float>::halfPi);
            rendered.setSample(
                channel, prefixLength + sample,
                tailGain * construction.getSample(
                    channel, samples - overlap + sample)
                    + headGain * construction.getSample(channel, sample));
        }
        if (suffixLength > 0)
            rendered.copyFrom(channel, prefixLength + overlap,
                              construction, channel, overlap, suffixLength);
    }
    return rendered;
}

std::vector<float> calculateSlowEnvelope(const juce::AudioBuffer<float>& audio,
                                         const double sampleRate,
                                         const float windowSeconds)
{
    const auto sampleCount = audio.getNumSamples();
    std::vector<float> power(static_cast<size_t>(sampleCount), 0.0f);
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        auto value = 0.0f;
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto current = audio.getSample(channel, sample);
            value += current * current;
        }
        power[static_cast<size_t>(sample)]
            = value / static_cast<float>(juce::jmax(1, audio.getNumChannels()));
    }

    const auto window = juce::jlimit(
        1, sampleCount, juce::roundToInt(sampleRate * windowSeconds));
    const auto before = window / 2;
    const auto after = window - before - 1;
    const auto wrap = [sampleCount] (int sample)
    {
        sample %= sampleCount;
        return sample < 0 ? sample + sampleCount : sample;
    };
    auto sum = 0.0;
    for (int offset = -before; offset <= after; ++offset)
        sum += power[static_cast<size_t>(wrap(offset))];
    std::vector<float> envelope(static_cast<size_t>(sampleCount), 0.0f);
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        envelope[static_cast<size_t>(sample)] = std::sqrt(
            static_cast<float>(juce::jmax(0.0, sum)
                               / static_cast<double>(window)));
        sum -= power[static_cast<size_t>(wrap(sample - before))];
        sum += power[static_cast<size_t>(wrap(sample + after + 1))];
    }
    return envelope;
}

void flattenCircularEnvelope(juce::AudioBuffer<float>& audio,
                              const double sampleRate,
                              const float amount)
{
    if (amount <= 0.0f || audio.getNumSamples() == 0)
        return;

    const auto envelope = calculateSlowEnvelope(audio, sampleRate, 0.22f);
    const auto target = juce::jmax(1.0e-7f, sampledPercentile(envelope, 0.50f));
    const auto floor = target * 0.16f;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto measured = juce::jmax(
            floor, envelope[static_cast<size_t>(sample)]);
        const auto requested = std::pow(target / measured, amount);
        const auto gain = juce::jlimit(
            juce::Decibels::decibelsToGain(-12.0f),
            juce::Decibels::decibelsToGain(10.0f), requested);
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            audio.setSample(channel, sample,
                            audio.getSample(channel, sample) * gain);
    }
}

float calculateMacroStability(const juce::AudioBuffer<float>& audio,
                              const double sampleRate)
{
    const auto envelope = calculateSlowEnvelope(audio, sampleRate, 0.36f);
    const auto low = juce::jmax(1.0e-7f, sampledPercentile(envelope, 0.10f));
    const auto high = juce::jmax(low, sampledPercentile(envelope, 0.90f));
    const auto rangeDecibels = 20.0f * std::log10(high / low);
    auto mean = 0.0;
    auto squareTotal = 0.0;
    for (const auto value : envelope)
    {
        mean += value;
        squareTotal += static_cast<double>(value) * value;
    }
    mean /= static_cast<double>(juce::jmax<size_t>(1u, envelope.size()));
    const auto variance = juce::jmax(
        0.0, squareTotal / static_cast<double>(
            juce::jmax<size_t>(1u, envelope.size())) - mean * mean);
    const auto coefficient = static_cast<float>(
        std::sqrt(variance) / juce::jmax(1.0e-7, mean));
    return 100.0f * std::exp(
        -0.06f * rangeDecibels - 1.1f * coefficient);
}

float calculateStationarity(const juce::AudioBuffer<float>& audio,
                            const double sampleRate)
{
    constexpr int observations = 16;
    const auto window = juce::jlimit(
        64, audio.getNumSamples(), juce::roundToInt(sampleRate * 0.20));
    auto previous = analyseBoundary(audio, 0, window);
    auto total = 0.0f;
    for (int observation = 1; observation < observations; ++observation)
    {
        const auto start = observation * juce::jmax(0, audio.getNumSamples() - window)
                           / (observations - 1);
        const auto current = analyseBoundary(audio, start, window);
        total += spectrumDistance(previous, current);
        previous = current;
    }
    return similarityScore(total / static_cast<float>(observations - 1), 0.85f);
}

float compareSpectralModels(const std::vector<float>& sourceModel,
                            const std::vector<float>& outputModel,
                            const int fftSize,
                            const double sampleRate)
{
    constexpr std::array<float, 13> edges {
        20.0f, 50.0f, 80.0f, 125.0f, 200.0f, 315.0f, 500.0f,
        800.0f, 1250.0f, 2500.0f, 5000.0f, 10000.0f, 24000.0f
    };
    std::array<double, edges.size() - 1u> sourceBands {};
    std::array<double, edges.size() - 1u> outputBands {};
    for (int bin = 1; bin <= fftSize / 2; ++bin)
    {
        const auto frequency = static_cast<float>(
            static_cast<double>(bin) * sampleRate / static_cast<double>(fftSize));
        for (size_t band = 0; band + 1u < edges.size(); ++band)
        {
            if (frequency >= edges[band]
                && frequency < juce::jmin(
                    edges[band + 1u], static_cast<float>(sampleRate * 0.5)))
            {
                sourceBands[band] += static_cast<double>(
                    sourceModel[static_cast<size_t>(bin)])
                    * sourceModel[static_cast<size_t>(bin)];
                outputBands[band] += static_cast<double>(
                    outputModel[static_cast<size_t>(bin)])
                    * outputModel[static_cast<size_t>(bin)];
                break;
            }
        }
    }
    const auto sourceTotal = std::accumulate(
        sourceBands.begin(), sourceBands.end(), 1.0e-20);
    const auto outputTotal = std::accumulate(
        outputBands.begin(), outputBands.end(), 1.0e-20);
    auto distance = 0.0f;
    for (size_t band = 0; band < sourceBands.size(); ++band)
        distance += std::abs(
            static_cast<float>(sourceBands[band] / sourceTotal)
            - static_cast<float>(outputBands[band] / outputTotal));
    return similarityScore(distance, 2.0f);
}

std::array<float, fingerprintBands> buildSpectralFingerprint(
    const std::vector<float>& logMagnitude)
{
    std::array<float, fingerprintBands> bands {};
    const auto bins = static_cast<int>(logMagnitude.size());
    for (int band = 0; band < fingerprintBands; ++band)
    {
        const auto firstNormalised = static_cast<float>(band)
                                     / static_cast<float>(fingerprintBands);
        const auto lastNormalised = static_cast<float>(band + 1)
                                    / static_cast<float>(fingerprintBands);
        const auto first = juce::jlimit(
            1, bins - 1, juce::roundToInt(
                std::exp(firstNormalised * std::log(static_cast<float>(bins))) - 1.0f));
        const auto last = juce::jlimit(
            first + 1, bins, juce::roundToInt(
                std::exp(lastNormalised * std::log(static_cast<float>(bins)))));
        auto total = 0.0f;
        for (int bin = first; bin < last; ++bin)
            total += logMagnitude[static_cast<size_t>(bin)];
        bands[static_cast<size_t>(band)] = total / static_cast<float>(last - first);
    }

    std::array<float, fingerprintBands> residual {};
    for (int band = 0; band < fingerprintBands; ++band)
    {
        const auto first = juce::jmax(0, band - 3);
        const auto last = juce::jmin(fingerprintBands - 1, band + 3);
        auto localMean = 0.0f;
        for (int neighbour = first; neighbour <= last; ++neighbour)
            localMean += bands[static_cast<size_t>(neighbour)];
        localMean /= static_cast<float>(last - first + 1);
        residual[static_cast<size_t>(band)]
            = bands[static_cast<size_t>(band)] - localMean;
    }

    const auto mean = std::accumulate(
        residual.begin(), residual.end(), 0.0f)
        / static_cast<float>(fingerprintBands);
    auto squareTotal = 0.0f;
    for (auto& value : residual)
    {
        value -= mean;
        squareTotal += value * value;
    }
    const auto scale = 1.0f / std::sqrt(juce::jmax(1.0e-12f, squareTotal));
    for (auto& value : residual)
        value *= scale;
    return residual;
}

float compareFrameIdentity(
    const std::vector<std::vector<float>>& sourceSpectra,
    const std::vector<std::vector<float>>& outputSpectra)
{
    if (sourceSpectra.empty() || outputSpectra.empty())
        return 0.0f;

    std::vector<std::array<float, fingerprintBands>> sourceFingerprints;
    sourceFingerprints.reserve(sourceSpectra.size());
    for (const auto& spectrum : sourceSpectra)
        sourceFingerprints.push_back(buildSpectralFingerprint(spectrum));

    std::vector<float> nearestScores;
    nearestScores.reserve(outputSpectra.size());
    for (const auto& spectrum : outputSpectra)
    {
        const auto output = buildSpectralFingerprint(spectrum);
        auto nearest = -1.0f;
        for (const auto& source : sourceFingerprints)
        {
            auto dot = 0.0f;
            for (int band = 0; band < fingerprintBands; ++band)
                dot += output[static_cast<size_t>(band)]
                       * source[static_cast<size_t>(band)];
            nearest = juce::jmax(nearest, dot);
        }
        nearestScores.push_back(nearest);
    }

    const auto similarity = sampledPercentile(nearestScores, 0.50f);
    return 100.0f * juce::jlimit(0.0f, 1.0f,
                                 (similarity - 0.45f) / 0.45f);
}

float calculateRepeatRisk(const juce::AudioBuffer<float>& audio,
                          const double sampleRate)
{
    const auto hop = juce::jmax(32, juce::roundToInt(sampleRate * 0.08));
    const auto window = juce::jmin(audio.getNumSamples(), 2 * hop);
    const auto frames = audio.getNumSamples() / hop;
    if (frames < 12 || window < 32)
        return 0.0f;

    constexpr auto dimensions = 4;
    std::vector<std::array<float, dimensions>> features(
        static_cast<size_t>(frames));
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto start = juce::jmin(
            audio.getNumSamples() - window, frame * hop);
        const auto analysed = analyseBoundary(audio, start, window);
        auto spectralCentre = 0.0f;
        for (int band = 0; band < spectrumBands; ++band)
            spectralCentre += static_cast<float>(band)
                              * analysed.spectrum[static_cast<size_t>(band)];
        features[static_cast<size_t>(frame)] = {
            std::log(juce::jmax(1.0e-8f, analysed.rms)),
            std::log(juce::jmax(1.0e-8f, analysed.derivative)),
            spectralCentre,
            analysed.stereoCorrelation
        };
    }

    for (int dimension = 0; dimension < dimensions; ++dimension)
    {
        auto mean = 0.0;
        for (const auto& feature : features)
            mean += feature[static_cast<size_t>(dimension)];
        mean /= static_cast<double>(features.size());
        auto variance = 0.0;
        for (const auto& feature : features)
        {
            const auto centred = feature[static_cast<size_t>(dimension)] - mean;
            variance += static_cast<double>(centred) * centred;
        }
        const auto scale = std::sqrt(
            variance / static_cast<double>(features.size())) + 1.0e-7;
        for (auto& feature : features)
            feature[static_cast<size_t>(dimension)] = static_cast<float>(
                (feature[static_cast<size_t>(dimension)] - mean) / scale);
    }

    const auto minimumLag = juce::jmax(
        3, juce::roundToInt(0.40 * sampleRate / static_cast<double>(hop)));
    const auto maximumLag = juce::jmin(frames / 2, frames - 12);
    auto strongest = 0.0f;
    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        auto dot = 0.0;
        auto firstEnergy = 0.0;
        auto secondEnergy = 0.0;
        for (int frame = 0; frame + lag < frames; ++frame)
            for (int dimension = 0; dimension < dimensions; ++dimension)
            {
                const auto first = features[static_cast<size_t>(frame)]
                                           [static_cast<size_t>(dimension)];
                const auto second = features[static_cast<size_t>(frame + lag)]
                                            [static_cast<size_t>(dimension)];
                dot += static_cast<double>(first) * second;
                firstEnergy += static_cast<double>(first) * first;
                secondEnergy += static_cast<double>(second) * second;
            }
        const auto denominator = std::sqrt(firstEnergy * secondEnergy);
        if (denominator > 1.0e-12)
            strongest = juce::jmax(
                strongest, static_cast<float>(dot / denominator));
    }
    return juce::jlimit(0.0f, 1.0f, strongest);
}
}

TextureSynthesisResult TextureSynthesizer::synthesize(
    const juce::AudioBuffer<float>& source,
    const double sampleRate,
    TextureSynthesisSettings settings)
{
    TextureSynthesisResult result;
    if (source.getNumChannels() < 1 || source.getNumSamples() < 128
        || sampleRate <= 0.0)
        return result;

    settings.durationSeconds = juce::jlimit(4.0f, 60.0f, settings.durationSeconds);
    settings.variation = juce::jlimit(0.0f, 1.0f, settings.variation);
    settings.flatten = juce::jlimit(0.0f, 1.0f, settings.flatten);
    settings.sourceMatch = juce::jlimit(0.0f, 1.0f, settings.sourceMatch);
    DeterministicRandom random(settings.seed);

    auto fftOrder = 12;
    while ((1 << fftOrder) > source.getNumSamples() && fftOrder > 8)
        --fftOrder;
    const auto fftSize = 1 << fftOrder;
    const auto hop = fftSize / 2;
    const auto maximumStart = juce::jmax(0, source.getNumSamples() - fftSize);
    const auto availableFrameCount = juce::jmax(1, maximumStart / hop + 1);
    const auto analysisFrameCount = juce::jmin(
        maximumAnalysisFrames, availableFrameCount);

    std::vector<int> frameStarts;
    std::vector<float> levels;
    frameStarts.reserve(static_cast<size_t>(analysisFrameCount));
    levels.reserve(static_cast<size_t>(analysisFrameCount));
    for (int frame = 0; frame < analysisFrameCount; ++frame)
    {
        const auto start = analysisFrameCount > 1
            ? frame * maximumStart / (analysisFrameCount - 1) : 0;
        frameStarts.push_back(start);
        levels.push_back(frameRms(source, start, fftSize));
    }
    const auto highLevel = sampledPercentile(levels, 0.90f);
    const auto activeThreshold = juce::jmax(1.0e-7f, highLevel * 0.12f);
    const auto excessiveLevel = sampledPercentile(levels, 0.98f) * 1.08f;
    std::vector<int> selectedFrames;
    for (int frame = 0; frame < analysisFrameCount; ++frame)
        if (levels[static_cast<size_t>(frame)] >= activeThreshold
            && levels[static_cast<size_t>(frame)] <= excessiveLevel)
            selectedFrames.push_back(frame);
    if (selectedFrames.size() < 4u)
    {
        selectedFrames.clear();
        for (int frame = 0; frame < analysisFrameCount; ++frame)
            if (levels[static_cast<size_t>(frame)] >= activeThreshold)
                selectedFrames.push_back(frame);
    }
    if (selectedFrames.empty())
        selectedFrames.push_back(static_cast<int>(
            std::distance(levels.begin(),
                          std::max_element(levels.begin(), levels.end()))));

    std::vector<float> activeLevels;
    activeLevels.reserve(selectedFrames.size());
    for (const auto frame : selectedFrames)
        activeLevels.push_back(levels[static_cast<size_t>(frame)]);
    const auto activeLow = juce::jmax(1.0e-8f, sampledPercentile(activeLevels, 0.10f));
    const auto activeHigh = juce::jmax(activeLow, sampledPercentile(activeLevels, 0.90f));
    const auto activeRangeDb = 20.0f * std::log10(activeHigh / activeLow);
    const auto sourceSpatial = analyseSelectedSpatial(
        source, frameStarts, selectedFrames, fftSize);
    const auto referenceFrameCount = juce::jmin(
        24, static_cast<int>(selectedFrames.size()));
    juce::AudioBuffer<float> activeReference(
        source.getNumChannels(), referenceFrameCount * fftSize);
    for (int reference = 0; reference < referenceFrameCount; ++reference)
    {
        const auto selectedIndex = referenceFrameCount > 1
            ? reference * (static_cast<int>(selectedFrames.size()) - 1)
                / (referenceFrameCount - 1)
            : 0;
        const auto sourceStart = frameStarts[static_cast<size_t>(
            selectedFrames[static_cast<size_t>(selectedIndex)])];
        for (int channel = 0; channel < activeReference.getNumChannels(); ++channel)
            activeReference.copyFrom(channel, reference * fftSize,
                                     source, channel, sourceStart, fftSize);
    }

    juce::dsp::FFT fft(fftOrder);
    const auto componentCount = source.getNumChannels() > 1 ? 2 : 1;
    std::array<std::vector<std::vector<float>>, 2> analysisSpectra;
    std::array<std::vector<float>, 2> models;
    std::array<std::vector<float>, 2> colourModels;
    for (int component = 0; component < componentCount; ++component)
    {
        auto& spectra = analysisSpectra[static_cast<size_t>(component)];
        spectra.reserve(selectedFrames.size());
        for (const auto frame : selectedFrames)
            spectra.push_back(analyseFrameSpectrum(
                source, component, frameStarts[static_cast<size_t>(frame)],
                fftSize, fft));
        models[static_cast<size_t>(component)] = buildStationaryModel(spectra);
        colourModels[static_cast<size_t>(component)] = buildMaterialColourModel(spectra);
    }
    result.analysisFrameStarts.reserve(selectedFrames.size());
    for (const auto frame : selectedFrames)
        result.analysisFrameStarts.push_back(
            frameStarts[static_cast<size_t>(frame)]);

    const auto targetSamples = juce::jmax(
        fftSize, juce::roundToInt(sampleRate * settings.durationSeconds));
    const auto closureOverlap = juce::jlimit(
        64, targetSamples / 8, juce::roundToInt(sampleRate * 0.12));
    const auto constructionSamples = targetSamples + closureOverlap;
    const auto desiredGrainSamples = juce::roundToInt(
        sampleRate * (0.82 - 0.30 * static_cast<double>(settings.variation)));
    const auto grainSamples = juce::jlimit(
        128, source.getNumSamples(), desiredGrainSamples);
    const auto requestedCrossfade = juce::jlimit(
        32, juce::jmax(32, grainSamples / 3),
        juce::roundToInt(sampleRate
            * (0.055 + 0.055 * static_cast<double>(settings.variation))));
    const auto nominalStep = juce::jmax(1, grainSamples - requestedCrossfade);
    const auto outputGrainCount = juce::jmax(
        2, (constructionSamples + nominalStep - 1) / nominalStep);
    const auto averageStep = juce::jmax(1, constructionSamples / outputGrainCount);
    const auto boundarySamples = juce::jlimit(
        32, grainSamples / 3,
        juce::roundToInt(sampleRate * 0.08));

    std::vector<TextureGrain> grains;
    grains.reserve(selectedFrames.size());
    const auto maximumGrainStart = source.getNumSamples() - grainSamples;
    for (const auto selectedFrame : selectedFrames)
    {
        const auto centre = frameStarts[static_cast<size_t>(selectedFrame)]
                            + fftSize / 2;
        const auto start = juce::jlimit(
            0, maximumGrainStart, centre - grainSamples / 2);
        if (!grains.empty() && grains.back().start == start)
            continue;

        TextureGrain grain;
        grain.start = start;
        grain.rms = frameRms(source, start, grainSamples);
        grain.envelopeRangeDb = grainEnvelopeRangeDb(
            source, start, grainSamples);
        grain.head = analyseBoundary(source, start, boundarySamples);
        grain.tail = analyseBoundary(
            source, start + grainSamples - boundarySamples, boundarySamples);
        grains.push_back(std::move(grain));
    }
    if (grains.empty())
    {
        TextureGrain grain;
        grain.rms = frameRms(source, 0, grainSamples);
        grain.envelopeRangeDb = grainEnvelopeRangeDb(source, 0, grainSamples);
        grain.head = analyseBoundary(source, 0, boundarySamples);
        grain.tail = analyseBoundary(
            source, grainSamples - boundarySamples, boundarySamples);
        grains.push_back(std::move(grain));
    }

    std::vector<float> envelopeRanges;
    envelopeRanges.reserve(grains.size());
    for (const auto& grain : grains)
        envelopeRanges.push_back(grain.envelopeRangeDb);
    const auto stabilityQuantile = 0.55f + 0.35f * (1.0f - settings.flatten);
    const auto maximumEnvelopeRange = sampledPercentile(
        envelopeRanges, stabilityQuantile);
    std::vector<int> stableGrains;
    std::vector<float> stableLevels;
    for (int grain = 0; grain < static_cast<int>(grains.size()); ++grain)
        if (grains[static_cast<size_t>(grain)].envelopeRangeDb
                <= maximumEnvelopeRange + 0.25f)
        {
            stableGrains.push_back(grain);
            stableLevels.push_back(grains[static_cast<size_t>(grain)].rms);
        }
    if (stableGrains.size() < juce::jmin<size_t>(4u, grains.size()))
    {
        stableGrains.clear();
        stableLevels.clear();
        for (int grain = 0; grain < static_cast<int>(grains.size()); ++grain)
        {
            stableGrains.push_back(grain);
            stableLevels.push_back(grains[static_cast<size_t>(grain)].rms);
        }
    }
    const auto targetGrainRms = juce::jmax(
        1.0e-8f, sampledPercentile(stableLevels, 0.50f));

    std::vector<int> outputPositions(static_cast<size_t>(outputGrainCount));
    const auto positionJitter = 0.075f * settings.variation
                                * static_cast<float>(averageStep);
    for (int grain = 0; grain < outputGrainCount; ++grain)
    {
        const auto circular = static_cast<float>(grain)
                              / static_cast<float>(outputGrainCount);
        const auto jitter = positionJitter
            * (0.68f * std::sin(juce::MathConstants<float>::twoPi * circular)
               + 0.32f * std::sin(
                   3.0f * juce::MathConstants<float>::twoPi * circular));
        outputPositions[static_cast<size_t>(grain)] = juce::jlimit(
            0, constructionSamples - 1,
            juce::roundToInt(circular * static_cast<float>(constructionSamples)
                             + jitter));
    }

    juce::AudioBuffer<float> assembled(source.getNumChannels(), constructionSamples);
    assembled.clear();
    std::vector<float> normalisation(static_cast<size_t>(constructionSamples), 0.0f);
    std::vector<int> recentGrains;
    std::vector<int> grainUseCounts(grains.size(), 0);
    auto previousGrain = -1;
    auto firstGrain = -1;
    for (int outputGrain = 0; outputGrain < outputGrainCount; ++outputGrain)
    {
        auto selected = stableGrains[static_cast<size_t>(
            random.next() % static_cast<uint32_t>(stableGrains.size()))];
        if (previousGrain >= 0 && stableGrains.size() > 1u)
        {
            auto bestScore = std::numeric_limits<float>::max();
            for (const auto candidateIndex : stableGrains)
            {
                const auto& previous = grains[static_cast<size_t>(previousGrain)];
                const auto& candidate = grains[static_cast<size_t>(candidateIndex)];
                auto score = 0.68f * fullFeatureDistance(
                    previous.tail, candidate.head);
                score += 0.12f * std::abs(std::log(
                    juce::jmax(1.0e-8f, previous.rms)
                    / juce::jmax(1.0e-8f, candidate.rms)));
                score += 0.06f * juce::jlimit(
                    0.0f, 1.0f, candidate.envelopeRangeDb / 18.0f)
                    * settings.flatten;
                if (std::abs(candidate.start - previous.start)
                        < grainSamples / 2)
                    score += 0.55f;
                if (std::abs(candidate.start
                             - (previous.start + averageStep))
                        < grainSamples / 3)
                    score += 0.30f * settings.flatten;
                if (std::find(recentGrains.begin(), recentGrains.end(),
                              candidateIndex) != recentGrains.end())
                    score += 0.48f;
                if (outputGrain == outputGrainCount - 1 && firstGrain >= 0)
                    score += 0.72f * fullFeatureDistance(
                        candidate.tail,
                        grains[static_cast<size_t>(firstGrain)].head);
                score += 0.08f * static_cast<float>(
                    grainUseCounts[static_cast<size_t>(candidateIndex)]);
                score += random.unit() * (0.07f + 0.28f * settings.variation);
                if (score < bestScore)
                {
                    bestScore = score;
                    selected = candidateIndex;
                }
            }
        }

        const auto& grain = grains[static_cast<size_t>(selected)];
        const auto gain = juce::jlimit(
            0.25f, 4.0f, std::pow(
                targetGrainRms / juce::jmax(1.0e-8f, grain.rms),
                0.90f * settings.flatten));
        const auto outputStart = outputPositions[static_cast<size_t>(outputGrain)];
        const auto previousPosition = outputGrain == 0
            ? outputPositions.back() - constructionSamples
            : outputPositions[static_cast<size_t>(outputGrain - 1)];
        const auto nextPosition = outputGrain + 1 == outputGrainCount
            ? constructionSamples
            : outputPositions[static_cast<size_t>(outputGrain + 1)];
        const auto fadeInSamples = juce::jlimit(
            1, grainSamples / 3,
            grainSamples - (outputStart - previousPosition));
        const auto fadeOutSamples = juce::jlimit(
            1, grainSamples / 3,
            grainSamples - (nextPosition - outputStart));
        for (int sample = 0; sample < grainSamples; ++sample)
        {
            auto window = 1.0f;
            if (sample < fadeInSamples)
            {
                const auto phase = juce::MathConstants<float>::halfPi
                    * (static_cast<float>(sample) + 0.5f)
                    / static_cast<float>(fadeInSamples);
                const auto sine = std::sin(phase);
                window = sine * sine;
            }
            else if (sample >= grainSamples - fadeOutSamples)
            {
                const auto offset = sample - (grainSamples - fadeOutSamples);
                const auto phase = juce::MathConstants<float>::halfPi
                    * (static_cast<float>(offset) + 0.5f)
                    / static_cast<float>(fadeOutSamples);
                const auto cosine = std::cos(phase);
                window = cosine * cosine;
            }
            const auto position = (outputStart + sample) % constructionSamples;
            for (int channel = 0; channel < assembled.getNumChannels(); ++channel)
                assembled.addSample(
                    channel, position,
                    source.getSample(channel, grain.start + sample) * gain * window);
            normalisation[static_cast<size_t>(position)] += window;
        }

        previousGrain = selected;
        if (firstGrain < 0)
            firstGrain = selected;
        ++grainUseCounts[static_cast<size_t>(selected)];
        recentGrains.push_back(selected);
        if (recentGrains.size() > 5u)
            recentGrains.erase(recentGrains.begin());
    }
    for (int sample = 0; sample < constructionSamples; ++sample)
    {
        const auto scale = 1.0f / juce::jmax(
            1.0e-6f, normalisation[static_cast<size_t>(sample)]);
        for (int channel = 0; channel < assembled.getNumChannels(); ++channel)
            assembled.setSample(channel, sample,
                                assembled.getSample(channel, sample) * scale);
    }

    auto reconstructed = synthesizeMaterialModel(
        analysisSpectra, models, source.getNumChannels(), fftOrder,
        constructionSamples, settings.variation, random);
    const auto exemplarRms = RenderQuality::calculateRms(assembled);
    const auto reconstructedRms = RenderQuality::calculateRms(reconstructed);
    if (reconstructedRms > 1.0e-9f && exemplarRms > 1.0e-9f)
        reconstructed.applyGain(exemplarRms / reconstructedRms);

    result.audio.setSize(source.getNumChannels(), constructionSamples);
    const auto rebuild = settings.sourceMatch;
    const auto exemplarGain = std::cos(
        rebuild * juce::MathConstants<float>::halfPi);
    const auto reconstructionGain = std::sin(
        rebuild * juce::MathConstants<float>::halfPi);
    for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < constructionSamples; ++sample)
            result.audio.setSample(
                channel, sample,
                exemplarGain * assembled.getSample(channel, sample)
                    + reconstructionGain * reconstructed.getSample(channel, sample));
    flattenCircularEnvelope(result.audio, sampleRate, settings.flatten);

    matchMaterialColour(result.audio, colourModels[0], fftOrder, sampleRate,
                        0.72f + 0.20f * settings.sourceMatch);

    applyCircularMacroMovement(result.audio, activeRangeDb, settings.flatten, random);
    result.audio = closeConstructedLoop(result.audio, sampleRate, closureOverlap);
    result.containsOnlyFiniteSamples = RenderQuality::repairNonFiniteAndRemoveDc(
        result.audio);

    const auto sourceLoudness = RenderQuality::estimateIntegratedLoudnessDb(
        activeReference, sampleRate);
    const auto rawLoudness = RenderQuality::estimateIntegratedLoudnessDb(
        result.audio, sampleRate);
    const auto rawCorrelation = RenderQuality::calculateStereoCorrelation(result.audio);
    const auto rawImbalance = RenderQuality::calculateStereoLevelImbalanceDb(result.audio);
    const auto spatialMatch = 0.35f + 0.45f * (1.0f - settings.sourceMatch);
    const auto expectedCorrelation = rawCorrelation + spatialMatch
        * (sourceSpatial.correlation - rawCorrelation);
    const auto expectedImbalance = rawImbalance + spatialMatch
        * (sourceSpatial.imbalanceDb - rawImbalance);
    matchStereoImage(result.audio, sourceSpatial, spatialMatch);

    const auto requestedGainDb = juce::jlimit(
        -18.0f, 18.0f, sourceLoudness - rawLoudness);
    const auto rawTruePeak = RenderQuality::estimateCircularTruePeak(result.audio);
    const auto availableHeadroomDb = -1.0f - juce::Decibels::gainToDecibels(
        juce::jmax(1.0e-9f, rawTruePeak));
    const auto matchedGainDb = juce::jmin(requestedGainDb, availableHeadroomDb);
    result.audio.applyGain(juce::Decibels::decibelsToGain(matchedGainDb));
    const auto expectedLoudness = rawLoudness + matchedGainDb;
    result.truePeakDbtp = RenderQuality::applyCircularTruePeakCeiling(result.audio, -1.0f);

    const auto featureSamples = juce::jlimit(
        64, targetSamples / 2, juce::roundToInt(sampleRate * 0.04));
    const auto head = analyseBoundary(result.audio, 0, featureSamples);
    const auto tail = analyseBoundary(
        result.audio, targetSamples - featureSamples, featureSamples);
    auto boundaryJump = 0.0f;
    auto boundaryReference = 0.0f;
    for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
    {
        boundaryJump = juce::jmax(
            boundaryJump,
            std::abs(result.audio.getSample(channel, targetSamples - 1)
                     - result.audio.getSample(channel, 0)));
        boundaryReference = juce::jmax(
            boundaryReference,
            result.audio.getRMSLevel(channel, 0, targetSamples));
    }
    juce::ignoreUnused(boundaryReference);
    const auto naturalStep = juce::jmax(
        1.0e-6f, 0.5f * (head.derivative + tail.derivative));
    const auto excessStep = juce::jmax(
        0.0f, boundaryJump / naturalStep - 1.35f);
    const auto jumpQuality = 100.0f * std::exp(-0.90f * excessStep);
    result.closureQuality = 0.42f * similarityScore(
                                fullFeatureDistance(tail, head), 2.1f)
                            + 0.58f * jumpQuality;
    result.transitionQuality = calculateStationarity(result.audio, sampleRate);

    const auto sourceFeature = analyseWhole(source, sampleRate);
    const auto outputFeature = analyseWhole(result.audio, sampleRate);
    std::vector<std::vector<float>> outputSpectra;
    constexpr int outputModelFrames = 24;
    outputSpectra.reserve(outputModelFrames);
    const auto outputMaximumStart = juce::jmax(0, targetSamples - fftSize);
    for (int frame = 0; frame < outputModelFrames; ++frame)
    {
        const auto start = frame * outputMaximumStart / (outputModelFrames - 1);
        outputSpectra.push_back(analyseFrameSpectrum(
            result.audio, 0, start, fftSize, fft));
    }
    const auto coarseSpectrum = compareSpectralModels(
        colourModels[0], buildMaterialColourModel(outputSpectra), fftSize, sampleRate);
    const auto sourceFrameIdentity = compareFrameIdentity(
        analysisSpectra[0], outputSpectra);
    // Rebuilt textures may intentionally abandon the source event timeline and exact frames.
    // Coarse material colour remains mandatory so a tonal or granular source cannot collapse to
    // generic white/pink noise.
    result.spectrumPreservation = 0.72f * coarseSpectrum
                                  + 0.28f * sourceFrameIdentity;
    const auto outputLoudness = RenderQuality::estimateIntegratedLoudnessDb(
        result.audio, sampleRate);
    const auto outputCorrelation = RenderQuality::calculateStereoCorrelation(result.audio);
    const auto outputImbalance = RenderQuality::calculateStereoLevelImbalanceDb(result.audio);
    const auto loudnessErrorDb = std::abs(outputLoudness - expectedLoudness);
    result.loudnessPreservation = 100.0f * std::exp(-0.30f * loudnessErrorDb);
    result.phasePreservation = 100.0f * std::exp(
        -3.2f * std::abs(outputCorrelation - expectedCorrelation));
    result.positionPreservation = result.audio.getNumChannels() < 2 ? 100.0f
        : 100.0f * std::exp(-0.24f * std::abs(outputImbalance - expectedImbalance));
    result.stereoPreservation = 0.58f * result.phasePreservation
                                + 0.42f * result.positionPreservation;
    const auto sourceTextureRate = sourceFeature.derivative
                                   / juce::jmax(1.0e-6f, sourceFeature.rms);
    const auto outputTextureRate = outputFeature.derivative
                                   / juce::jmax(1.0e-6f, outputFeature.rms);
    result.transientPreservation = 100.0f * std::exp(
        -0.72f * std::abs(std::log(
            juce::jmax(1.0e-4f, outputTextureRate)
            / juce::jmax(1.0e-4f, sourceTextureRate))));
    result.macroStability = calculateMacroStability(result.audio, sampleRate);
    const auto repeatRisk = calculateRepeatRisk(result.audio, sampleRate);
    result.repeatSafety = 100.0f * (1.0f - repeatRisk);
    result.diversity = result.repeatSafety;
    result.qualityScore = 0.16f * result.closureQuality
                          + 0.06f * result.transitionQuality
                          + 0.26f * result.spectrumPreservation
                          + 0.12f * result.loudnessPreservation
                          + 0.10f * result.phasePreservation
                          + 0.08f * result.positionPreservation
                          + 0.12f * result.macroStability
                          + 0.10f * result.repeatSafety;
    const auto requiredStability = 45.0f + 22.0f * settings.flatten;
    result.passedQualityGate = result.containsOnlyFiniteSamples
                               && result.truePeakDbtp <= -0.85f
                               && result.closureQuality >= 60.0f
                               && result.spectrumPreservation >= 64.0f
                               && result.loudnessPreservation >= 70.0f
                               && result.phasePreservation >= 62.0f
                               && result.positionPreservation >= 62.0f
                               && result.macroStability >= requiredStability
                               && result.repeatSafety >= 58.0f;
    return result;
}
