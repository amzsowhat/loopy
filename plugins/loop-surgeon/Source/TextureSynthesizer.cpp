#include "TextureSynthesizer.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace
{
constexpr int spectrumBands = 8;
constexpr int driftBands = 10;
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
        model[bin] = std::exp(0.75f * raw[bin] + 0.25f * smoothed);
    }
    model.front() = 0.0f;
    return model;
}

float driftForBin(const std::array<float, driftBands>& states,
                  const int bin,
                  const int fftSize,
                  const double sampleRate)
{
    constexpr std::array<float, driftBands> centres {
        40.0f, 80.0f, 160.0f, 315.0f, 630.0f,
        1250.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f
    };
    const auto frequency = static_cast<float>(
        static_cast<double>(bin) * sampleRate / static_cast<double>(fftSize));
    if (frequency <= centres.front())
        return states.front();
    for (int index = 1; index < driftBands; ++index)
    {
        if (frequency <= centres[static_cast<size_t>(index)])
        {
            const auto low = std::log2(centres[static_cast<size_t>(index - 1)]);
            const auto high = std::log2(centres[static_cast<size_t>(index)]);
            const auto position = juce::jlimit(
                0.0f, 1.0f, (std::log2(frequency) - low) / (high - low));
            return states[static_cast<size_t>(index - 1)]
                   + position * (states[static_cast<size_t>(index)]
                                 - states[static_cast<size_t>(index - 1)]);
        }
    }
    return states.back();
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
    result.sourceGrainStarts.reserve(selectedFrames.size());
    for (const auto frame : selectedFrames)
        result.sourceGrainStarts.push_back(
            frameStarts[static_cast<size_t>(frame)]);

    const auto targetSamples = juce::jmax(
        fftSize, juce::roundToInt(sampleRate * settings.durationSeconds));
    juce::AudioBuffer<float> components(componentCount, targetSamples);
    components.clear();
    std::vector<float> normalisation(static_cast<size_t>(targetSamples), 0.0f);
    std::vector<float> transform(static_cast<size_t>(2 * fftSize), 0.0f);
    std::array<float, driftBands> driftState {};
    const auto driftCoefficient = std::exp(
        -static_cast<float>(hop) / static_cast<float>(sampleRate * 0.85));
    const auto driftDrive = (0.12f + 0.62f * settings.variation)
                            * std::sqrt(juce::jmax(
                                0.0f, 1.0f - driftCoefficient * driftCoefficient));

    for (int outputStart = 0; outputStart < targetSamples; outputStart += hop)
    {
        for (auto& state : driftState)
            state = driftCoefficient * state + driftDrive * random.gaussian();

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

    result.audio.setSize(source.getNumChannels(), targetSamples, false, true, false);
    constexpr auto inverseRootTwo = 0.7071067811865475f;
    for (int sample = 0; sample < targetSamples; ++sample)
    {
        if (source.getNumChannels() < 2)
        {
            result.audio.setSample(0, sample, components.getSample(0, sample));
            continue;
        }
        const auto mid = components.getSample(0, sample);
        const auto side = components.getSample(1, sample);
        result.audio.setSample(0, sample, (mid + side) * inverseRootTwo);
        result.audio.setSample(1, sample, (mid - side) * inverseRootTwo);
    }

    std::vector<float> activeLevels;
    for (const auto frame : selectedFrames)
        activeLevels.push_back(levels[static_cast<size_t>(frame)]);
    const auto targetRms = sampledPercentile(activeLevels, 0.60f);
    auto outputEnergy = 0.0;
    for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < targetSamples; ++sample)
        {
            const auto value = result.audio.getSample(channel, sample);
            outputEnergy += static_cast<double>(value) * value;
        }
    const auto outputRms = std::sqrt(static_cast<float>(
        outputEnergy / static_cast<double>(
            targetSamples * result.audio.getNumChannels())));
    result.audio.applyGain(targetRms / juce::jmax(1.0e-8f, outputRms));
    auto peak = 0.0f;
    for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
        peak = juce::jmax(
            peak, result.audio.getMagnitude(channel, 0, targetSamples));
    if (peak > 0.98f)
        result.audio.applyGain(0.98f / peak);

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
    result.stereoPreservation = 100.0f * std::exp(
        -2.2f * std::abs(sourceFeature.stereoCorrelation
                         - outputFeature.stereoCorrelation));
    result.transientPreservation = 100.0f * std::exp(
        -1.6f * std::abs(sourceFeature.derivative - outputFeature.derivative)
        / juce::jmax(0.01f,
                     sourceFeature.derivative + outputFeature.derivative));
    result.macroStability = calculateMacroStability(result.audio, sampleRate);
    const auto frameCoverage = juce::jlimit(
        0.0f, 1.0f, static_cast<float>(selectedFrames.size()) / 24.0f);
    result.diversity = 100.0f * juce::jlimit(
        0.0f, 1.0f, 0.65f + 0.20f * frameCoverage
                          + 0.15f * settings.variation);
    return result;
}
