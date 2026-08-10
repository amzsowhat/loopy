#include "TextureSynthesizer.h"

#include "RenderQuality.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace
{
constexpr int spectrumBands = 8;
constexpr int driftBands = 10;
constexpr int driftLayers = 4;
constexpr int maximumAnalysisFrames = 256;

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

    float gaussian() noexcept
    {
        if (hasSpare)
        {
            hasSpare = false;
            return spare;
        }
        const auto first = juce::jmax(1.0e-7f, unit());
        const auto second = unit();
        const auto radius = std::sqrt(-2.0f * std::log(first));
        const auto phase = juce::MathConstants<float>::twoPi * second;
        spare = radius * std::sin(phase);
        hasSpare = true;
        return radius * std::cos(phase);
    }

private:
    uint32_t state;
    float spare = 0.0f;
    bool hasSpare = false;
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
        const auto…2084 tokens truncated…     const auto analysed = analyseBoundary(audio, start, window);
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
    for (int component = 0; component < componentCount; ++component)
    {
        auto& spectra = analysisSpectra[static_cast<size_t>(component)];
        spectra.reserve(selectedFrames.size());
        for (const auto frame : selectedFrames)
            spectra.push_back(analyseFrameSpectrum(
                source, component, frameStarts[static_cast<size_t>(frame)],
                fftSize, fft));
        models[static_cast<size_t>(component)] = buildStationaryModel(spectra);
    }
    result.analysisFrameStarts.reserve(selectedFrames.size());
    for (const auto frame : selectedFrames)
        result.analysisFrameStarts.push_back(
            frameStarts[static_cast<size_t>(frame)]);

    const auto targetSamples = juce::jmax(
        fftSize, juce::roundToInt(sampleRate * settings.durationSeconds));
    juce::AudioBuffer<float> components(componentCount, targetSamples);
    components.clear();
    std::vector<float> normalisation(static_cast<size_t>(targetSamples), 0.0f);
    std::vector<float> transform(static_cast<size_t>(2 * fftSize), 0.0f);
    const auto outputFrameCount = juce::jmax(1, (targetSamples + hop - 1) / hop);
    std::array<std::array<float, driftLayers>, driftBands> driftPhases {};
    std::array<std::array<int, driftLayers>, driftBands> driftCycles {};
    constexpr std::array<float, driftLayers> driftWeights { 0.52f, 0.30f, 0.13f, 0.05f };
    for (int band = 0; band < driftBands; ++band)
        for (int layer = 0; layer < driftLayers; ++layer)
        {
            driftPhases[static_cast<size_t>(band)][static_cast<size_t>(layer)]
                = juce::MathConstants<float>::twoPi * random.unit();
            const auto maximumCycle = juce::jmax(1, 2 + layer * layer * 2);
            driftCycles[static_cast<size_t>(band)][static_cast<size_t>(layer)]
                = 1 + static_cast<int>(random.next()
                    % static_cast<uint32_t>(maximumCycle));
        }

    auto outputFrame = 0;
    for (int outputStart = 0; outputStart < targetSamples;
         outputStart += hop, ++outputFrame)
    {
        std::array<float, driftBands> driftState {};
        const auto circularPosition = static_cast<float>(outputFrame)
                                      / static_cast<float>(outputFrameCount);
        for (int band = 0; band < driftBands; ++band)
            for (int layer = 0; layer < driftLayers; ++layer)
                driftState[static_cast<size_t>(band)] +=
                    (0.18f + (0.32f + 1.08f * settings.variation)
                                  * (0.35f + 0.65f * (1.0f - settings.flatten)))
                    * driftWeights[static_cast<size_t>(layer)]
                    * std::sin(juce::MathConstants<float>::twoPi
                                   * static_cast<float>(driftCycles[static_cast<size_t>(band)]
                                                                  [static_cast<size_t>(layer)])
                                   * circularPosition
                               + driftPhases[static_cast<size_t>(band)]
                                            [static_cast<size_t>(layer)]);

        for (int component = 0; component < componentCount; ++component)
        {
            std::fill(transform.begin(), transform.end(), 0.0f);
            const auto& model = models[static_cast<size_t>(component)];
            for (int bin = 1; bin <= fftSize / 2; ++bin)
            {
                const auto driftDecibels = driftForBin(
                    driftState, bin, fftSize, sampleRate);
                const auto gain = std::pow(10.0f, driftDecibels / 20.0f);
                const auto magnitude = model[static_cast<size_t>(bin)] * gain;
                if (bin == fftSize / 2)
                {
                    transform[static_cast<size_t>(2 * bin)]
                        = magnitude * random.gaussian();
                }
                else
                {
                    transform[static_cast<size_t>(2 * bin)]
                        = 0.70710678f * magnitude * random.gaussian();
                    transform[static_cast<size_t>(2 * bin + 1)]
                        = 0.70710678f * magnitude * random.gaussian();
                }
            }
            fft.performRealOnlyInverseTransform(transform.data());
            for (int sample = 0; sample < fftSize; ++sample)
            {
                const auto window = std::sin(
                    juce::MathConstants<float>::pi
                    * (static_cast<float>(sample) + 0.5f)
                    / static_cast<float>(fftSize));
                const auto position = (outputStart + sample) % targetSamples;
                components.addSample(
                    component, position,
                    transform[static_cast<size_t>(sample)] * window);
                if (component == 0)
                    normalisation[static_cast<size_t>(position)] += window * window;
            }
        }
    }
    for (int sample = 0; sample < targetSamples; ++sample)
    {
        const auto scale = 1.0f / std::sqrt(juce::jmax(
            1.0e-12f, normalisation[static_cast<size_t>(sample)]));
        for (int component = 0; component < componentCount; ++component)
            components.setSample(component, sample,
                                 components.getSample(component, sample) * scale);
    }

    constexpr auto inverseRootTwo = 0.7071067811865475f;
    for (int sample = 0; sample < targetSamples; ++sample)
    {
        if (source.getNumChannels() < 2)
            continue;
        const auto mid = components.getSample(0, sample);
        const auto side = components.getSample(1, sample);
        components.setSample(0, sample, (mid + side) * inverseRootTwo);
        components.setSample(1, sample, (mid - side) * inverseRootTwo);
    }
    result.audio = std::move(components);

    applyCircularMacroMovement(result.audio, activeRangeDb, settings.flatten, random);
    result.containsOnlyFiniteSamples = RenderQuality::repairNonFiniteAndRemoveDc(
        result.audio);

    const auto sourceLoudness = RenderQuality::estimateIntegratedLoudnessDb(
        activeReference, sampleRate);
    const auto rawLoudness = RenderQuality::estimateIntegratedLoudnessDb(
        result.audio, sampleRate);
    const auto rawCorrelation = RenderQuality::calculateStereoCorrelation(result.audio);
    const auto rawImbalance = RenderQuality::calculateStereoLevelImbalanceDb(result.audio);
    const auto expectedCorrelation = rawCorrelation + settings.sourceMatch
        * (sourceSpatial.correlation - rawCorrelation);
    const auto expectedImbalance = rawImbalance + settings.sourceMatch
        * (sourceSpatial.imbalanceDb - rawImbalance);
    matchStereoImage(result.audio, sourceSpatial, settings.sourceMatch);

    const auto matchedGainDb = settings.sourceMatch * juce::jlimit(
        -18.0f, 18.0f, sourceLoudness - rawLoudness);
    result.audio.applyGain(juce::Decibels::decibelsToGain(matchedGainDb));
    const auto expectedLoudness = rawLoudness + matchedGainDb;
    result.truePeakDbtp = RenderQuality::applyCircularTruePeakCeiling(result.audio, -1.0f);

    const auto featureSamples = juce::jlimit(
        64, targetSamples / 2, juce::roundToInt(sampleRate * 0.22));
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
    const auto jumpQuality = 100.0f * std::exp(
        -2.2f * boundaryJump / juce::jmax(0.02f, boundaryReference));
    result.closureQuality = 0.72f * similarityScore(
                                fullFeatureDistance(tail, head), 2.1f)
                            + 0.28f * jumpQuality;
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
    result.spectrumPreservation = compareSpectralModels(
        models[0], buildStationaryModel(outputSpectra), fftSize, sampleRate);
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
    result.transientPreservation = 100.0f * std::exp(
        -1.6f * std::abs(sourceFeature.derivative - outputFeature.derivative)
        / juce::jmax(0.01f,
                     sourceFeature.derivative + outputFeature.derivative));
    result.macroStability = calculateMacroStability(result.audio, sampleRate);
    const auto repeatRisk = calculateRepeatRisk(result.audio, sampleRate);
    result.repeatSafety = 100.0f * (1.0f - repeatRisk);
    result.diversity = result.repeatSafety;
    result.qualityScore = 0.20f * result.closureQuality
                          + 0.08f * result.transitionQuality
                          + 0.18f * result.spectrumPreservation
                          + 0.12f * result.loudnessPreservation
                          + 0.10f * result.phasePreservation
                          + 0.08f * result.positionPreservation
                          + 0.12f * result.macroStability
                          + 0.12f * result.repeatSafety;
    const auto requiredStability = 45.0f + 22.0f * settings.flatten;
    result.passedQualityGate = result.containsOnlyFiniteSamples
                               && result.truePeakDbtp <= -0.85f
                               && result.closureQuality >= 62.0f
                               && result.spectrumPreservation >= 42.0f
                               && result.loudnessPreservation >= 70.0f
                               && result.phasePreservation >= 62.0f
                               && result.positionPreservation >= 62.0f
                               && result.macroStability >= requiredStability
                               && result.repeatSafety >= 58.0f;
    return result;
}
