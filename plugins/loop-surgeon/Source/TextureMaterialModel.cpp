#include "TextureMaterialModel.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace
{
constexpr float minimumMagnitude = 1.0e-7f;

class DeterministicRandom
{
public:
    explicit DeterministicRandom(const uint32_t seed) : state(seed == 0u ? 1u : seed) {}

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

float wrapPhase(float phase) noexcept
{
    while (phase > juce::MathConstants<float>::pi)
        phase -= juce::MathConstants<float>::twoPi;
    while (phase < -juce::MathConstants<float>::pi)
        phase += juce::MathConstants<float>::twoPi;
    return phase;
}

float percentile(std::vector<float> values, const float proportion)
{
    if (values.empty())
        return 0.0f;
    const auto index = static_cast<size_t>(juce::roundToInt(
        juce::jlimit(0.0f, 1.0f, proportion)
        * static_cast<float>(values.size() - 1u)));
    std::nth_element(values.begin(), values.begin() + static_cast<ptrdiff_t>(index),
                     values.end());
    return values[index];
}

float readComponent(const juce::AudioBuffer<float>& source,
                    const int component,
                    const int sample) noexcept
{
    if (sample < 0 || sample >= source.getNumSamples())
        return 0.0f;
    if (source.getNumChannels() < 2)
        return source.getSample(0, sample);
    constexpr auto inverseRootTwo = 0.7071067811865475f;
    const auto left = source.getSample(0, sample);
    const auto right = source.getSample(1, sample);
    return component == 0 ? (left + right) * inverseRootTwo
                          : (left - right) * inverseRootTwo;
}

struct SpectralFrame
{
    int sourceStart = 0;
    float level = 0.0f;
    std::vector<std::vector<float>> logMagnitude;
    std::vector<std::vector<float>> phaseDeviation;
};

struct Model
{
    int fftOrder = 10;
    int fftSize = 1024;
    int hop = 256;
    int bins = 513;
    int components = 1;
    float sourceRms = 0.0f;
    float sideToMid = 0.0f;
    float leftGain = 1.0f;
    float rightGain = 1.0f;
    std::vector<SpectralFrame> frames;
    std::vector<std::vector<float>> meanLogMagnitude;
    std::vector<std::vector<float>> spectralShape;
};

int selectFftOrder(const int sourceSamples)
{
    if (sourceSamples < 640)
        return 8;
    if (sourceSamples < 1500)
        return 9;
    return 10;
}

float frameLevel(const juce::AudioBuffer<float>& source,
                 const int centre,
                 const int fftSize)
{
    auto energy = 0.0;
    auto count = 0;
    const auto first = centre - fftSize / 2;
    for (int offset = 0; offset < fftSize; ++offset)
    {
        const auto sample = first + offset;
        if (sample < 0 || sample >= source.getNumSamples())
            continue;
        for (int channel = 0; channel < source.getNumChannels(); ++channel)
        {
            const auto value = source.getSample(channel, sample);
            energy += static_cast<double>(value) * value;
            ++count;
        }
    }
    return std::sqrt(static_cast<float>(energy / static_cast<double>(juce::jmax(1, count))));
}

Model analyse(const juce::AudioBuffer<float>& source)
{
    Model model;
    model.fftOrder = selectFftOrder(source.getNumSamples());
    model.fftSize = 1 << model.fftOrder;
    model.hop = model.fftSize / 4;
    model.bins = model.fftSize / 2 + 1;
    model.components = source.getNumChannels() >= 2 ? 2 : 1;
    model.meanLogMagnitude.assign(
        static_cast<size_t>(model.components),
        std::vector<float>(static_cast<size_t>(model.bins), 0.0f));
    model.spectralShape = model.meanLogMagnitude;

    if (source.getNumChannels() >= 2)
    {
        auto midEnergy = 0.0;
        auto sideEnergy = 0.0;
        auto leftEnergy = 0.0;
        auto rightEnergy = 0.0;
        for (int sample = 0; sample < source.getNumSamples(); ++sample)
        {
            const auto left = static_cast<double>(source.getSample(0, sample));
            const auto right = static_cast<double>(source.getSample(1, sample));
            const auto mid = (left + right) * 0.7071067811865475;
            const auto side = (left - right) * 0.7071067811865475;
            midEnergy += mid * mid;
            sideEnergy += side * side;
            leftEnergy += left * left;
            rightEnergy += right * right;
        }
        model.sideToMid = static_cast<float>(std::sqrt(
            sideEnergy / juce::jmax(1.0e-12, midEnergy)));
        const auto channelTotal = juce::jmax(1.0e-12, leftEnergy + rightEnergy);
        model.leftGain = static_cast<float>(std::sqrt(2.0 * leftEnergy / channelTotal));
        model.rightGain = static_cast<float>(std::sqrt(2.0 * rightEnergy / channelTotal));
    }

    std::vector<int> centres;
    std::vector<float> levels;
    for (int centre = 0; centre < source.getNumSamples(); centre += model.hop)
    {
        centres.push_back(centre);
        levels.push_back(frameLevel(source, centre, model.fftSize));
    }
    if (centres.empty())
    {
        centres.push_back(0);
        levels.push_back(0.0f);
    }
    const auto highLevel = juce::jmax(1.0e-8f, percentile(levels, 0.90f));
    const auto activeThreshold = highLevel * 0.075f;
    std::vector<float> activeLevels;
    for (const auto value : levels)
        if (value >= activeThreshold)
            activeLevels.push_back(value);
    model.sourceRms = juce::jmax(1.0e-6f, percentile(activeLevels, 0.50f));

    juce::dsp::FFT fft(model.fftOrder);
    std::vector<std::vector<float>> previousPhase(
        static_cast<size_t>(model.components),
        std::vector<float>(static_cast<size_t>(model.bins), 0.0f));
    auto havePrevious = false;
    for (size_t frameIndex = 0; frameIndex < centres.size(); ++frameIndex)
    {
        const auto centre = centres[frameIndex];
        const auto level = levels[frameIndex];
        std::vector<std::vector<float>> frameMagnitude(
            static_cast<size_t>(model.components),
            std::vector<float>(static_cast<size_t>(model.bins), minimumMagnitude));
        std::vector<std::vector<float>> framePhase(
            static_cast<size_t>(model.components),
            std::vector<float>(static_cast<size_t>(model.bins), 0.0f));

        for (int component = 0; component < model.components; ++component)
        {
            std::vector<float> data(static_cast<size_t>(2 * model.fftSize), 0.0f);
            const auto first = centre - model.fftSize / 2;
            for (int offset = 0; offset < model.fftSize; ++offset)
            {
                const auto window = std::sqrt(0.5f - 0.5f * std::cos(
                    juce::MathConstants<float>::twoPi * static_cast<float>(offset)
                    / static_cast<float>(model.fftSize - 1)));
                data[static_cast<size_t>(offset)]
                    = readComponent(source, component, first + offset) * window;
            }
            fft.performRealOnlyForwardTransform(data.data(), true);
            for (int bin = 0; bin < model.bins; ++bin)
            {
                const auto real = data[static_cast<size_t>(2 * bin)];
                const auto imaginary = data[static_cast<size_t>(2 * bin + 1)];
                frameMagnitude[static_cast<size_t>(component)][static_cast<size_t>(bin)]
                    = juce::jmax(minimumMagnitude, std::hypot(real, imaginary));
                framePhase[static_cast<size_t>(component)][static_cast<size_t>(bin)]
                    = std::atan2(imaginary, real);
            }
        }

        if (level >= activeThreshold)
        {
            SpectralFrame frame;
            frame.sourceStart = centre;
            frame.level = level;
            frame.logMagnitude = frameMagnitude;
            frame.phaseDeviation.assign(
                static_cast<size_t>(model.components),
                std::vector<float>(static_cast<size_t>(model.bins), 0.0f));
            for (int component = 0; component < model.components; ++component)
            {
                auto magnitudeEnergy = 0.0;
                for (int bin = 1; bin < model.bins; ++bin)
                {
                    const auto magnitude = frameMagnitude[static_cast<size_t>(component)]
                                                       [static_cast<size_t>(bin)];
                    magnitudeEnergy += static_cast<double>(magnitude) * magnitude;
                }
                const auto magnitudeScale = std::sqrt(magnitudeEnergy
                    / static_cast<double>(juce::jmax(1, model.bins - 1)));
                for (int bin = 0; bin < model.bins; ++bin)
                {
                    const auto magnitude = frameMagnitude[static_cast<size_t>(component)]
                                                       [static_cast<size_t>(bin)];
                    frame.logMagnitude[static_cast<size_t>(component)]
                                      [static_cast<size_t>(bin)]
                        = std::log(juce::jmax(minimumMagnitude,
                            magnitude / static_cast<float>(
                                juce::jmax(1.0e-7, magnitudeScale))));
                    if (havePrevious)
                    {
                        const auto expected = juce::MathConstants<float>::twoPi
                            * static_cast<float>(bin * model.hop)
                            / static_cast<float>(model.fftSize);
                        frame.phaseDeviation[static_cast<size_t>(component)]
                                            [static_cast<size_t>(bin)]
                            = wrapPhase(framePhase[static_cast<size_t>(component)]
                                                 [static_cast<size_t>(bin)]
                                - previousPhase[static_cast<size_t>(component)]
                                               [static_cast<size_t>(bin)]
                                - expected);
                    }
                }
            }
            model.frames.push_back(std::move(frame));
        }
        previousPhase = std::move(framePhase);
        havePrevious = true;
    }

    if (model.frames.empty())
    {
        SpectralFrame fallback;
        fallback.sourceStart = 0;
        fallback.level = model.sourceRms;
        fallback.logMagnitude.assign(
            static_cast<size_t>(model.components),
            std::vector<float>(static_cast<size_t>(model.bins), std::log(minimumMagnitude)));
        fallback.phaseDeviation.assign(
            static_cast<size_t>(model.components),
            std::vector<float>(static_cast<size_t>(model.bins), 0.0f));
        model.frames.push_back(std::move(fallback));
    }

    for (const auto& frame : model.frames)
        for (int component = 0; component < model.components; ++component)
            for (int bin = 0; bin < model.bins; ++bin)
                model.meanLogMagnitude[static_cast<size_t>(component)]
                                      [static_cast<size_t>(bin)]
                    += frame.logMagnitude[static_cast<size_t>(component)]
                                         [static_cast<size_t>(bin)];
    const auto inverseFrames = 1.0f / static_cast<float>(model.frames.size());
    for (auto& component : model.meanLogMagnitude)
        for (auto& value : component)
            value *= inverseFrames;

    // A broad log-frequency neighbourhood defines the persistent material colour. Deviations
    // from it are resonant detail and are deliberately available to the style transform.
    for (int component = 0; component < model.components; ++component)
        for (int bin = 0; bin < model.bins; ++bin)
        {
            const auto radius = juce::jmax(2, bin / 18);
            auto total = 0.0f;
            auto count = 0;
            for (int neighbour = juce::jmax(0, bin - radius);
                 neighbour <= juce::jmin(model.bins - 1, bin + radius);
                 ++neighbour)
            {
                total += model.meanLogMagnitude[static_cast<size_t>(component)]
                                               [static_cast<size_t>(neighbour)];
                ++count;
            }
            model.spectralShape[static_cast<size_t>(component)]
                               [static_cast<size_t>(bin)]
                = total / static_cast<float>(juce::jmax(1, count));
        }
    return model;
}

struct Orbit
{
    static constexpr int maximumPoints = 19;
    std::array<float, maximumPoints> points {};
    int pointCount = 11;
    int anchorOffset = 0;
};

float circularPosition(const Orbit& orbit, const float progress)
{
    const auto position = progress * static_cast<float>(orbit.pointCount);
    const auto base = static_cast<int>(std::floor(position));
    const auto fraction = position - std::floor(position);
    const auto at = [&orbit] (const int index)
    {
        auto wrapped = index % orbit.pointCount;
        if (wrapped < 0)
            wrapped += orbit.pointCount;
        return orbit.points[static_cast<size_t>(wrapped)];
    };
    const auto previous = at(base - 1);
    const auto current = at(base);
    const auto next = at(base + 1);
    const auto following = at(base + 2);
    const auto a = -0.5f * previous + 1.5f * current
                   - 1.5f * next + 0.5f * following;
    const auto b = previous - 2.5f * current + 2.0f * next - 0.5f * following;
    const auto c = -0.5f * previous + 0.5f * next;
    return juce::jlimit(0.0f, 1.0f,
        ((a * fraction + b) * fraction + c) * fraction + current);
}

Orbit createOrbit(DeterministicRandom& random,
                  const int pointCount,
                  const int anchorCount)
{
    Orbit result;
    result.pointCount = juce::jlimit(5, Orbit::maximumPoints, pointCount);
    for (int point = 0; point < result.pointCount; ++point)
        result.points[static_cast<size_t>(point)] = random.unit();
    for (int pass = 0; pass < 2; ++pass)
    {
        auto smoothed = result.points;
        for (int point = 0; point < result.pointCount; ++point)
        {
            const auto previous = (point + result.pointCount - 1) % result.pointCount;
            const auto next = (point + 1) % result.pointCount;
            smoothed[static_cast<size_t>(point)]
                = 0.22f * result.points[static_cast<size_t>(previous)]
                  + 0.56f * result.points[static_cast<size_t>(point)]
                  + 0.22f * result.points[static_cast<size_t>(next)];
        }
        result.points = smoothed;
    }
    result.anchorOffset = anchorCount > 0
        ? static_cast<int>(random.next() % static_cast<uint32_t>(anchorCount)) : 0;
    return result;
}

float interpolateFrameValue(const Model& model,
                            const std::vector<int>& atlas,
                            const Orbit& orbit,
                            const float progress,
                            const int component,
                            const int bin,
                            const bool phaseDeviation)
{
    if (atlas.empty())
        return 0.0f;
    const auto position = circularPosition(orbit, progress)
                          * static_cast<float>(atlas.size());
    const auto base = static_cast<int>(std::floor(position));
    const auto fraction = position - std::floor(position);
    const auto wrap = [&atlas] (const int index)
    {
        auto wrapped = index % static_cast<int>(atlas.size());
        if (wrapped < 0)
            wrapped += static_cast<int>(atlas.size());
        return atlas[static_cast<size_t>(wrapped)];
    };
    const auto first = wrap(base + orbit.anchorOffset);
    const auto second = wrap(base + 1 + orbit.anchorOffset);
    const auto& firstData = phaseDeviation
        ? model.frames[static_cast<size_t>(first)].phaseDeviation
        : model.frames[static_cast<size_t>(first)].logMagnitude;
    const auto& secondData = phaseDeviation
        ? model.frames[static_cast<size_t>(second)].phaseDeviation
        : model.frames[static_cast<size_t>(second)].logMagnitude;
    const auto firstValue = firstData[static_cast<size_t>(component)]
                                     [static_cast<size_t>(bin)];
    const auto secondValue = secondData[static_cast<size_t>(component)]
                                       [static_cast<size_t>(bin)];
    if (!phaseDeviation)
        return firstValue + fraction * (secondValue - firstValue);
    return wrapPhase(firstValue + fraction * wrapPhase(secondValue - firstValue));
}

TextureMaterialModel::RenderedMaterial synthesize(const Model& model,
                                                   const juce::AudioBuffer<float>& source,
                                                   const double sampleRate,
                                                   const int targetSamples,
                                                   const TextureSynthesisSettings& settings)
{
    TextureMaterialModel::RenderedMaterial result;
    const auto channels = juce::jlimit(1, 2, source.getNumChannels());
    result.audio.setSize(channels, targetSamples);
    result.audio.clear();
    for (const auto& frame : model.frames)
        result.analysisFrameStarts.push_back(frame.sourceStart);
    if (targetSamples <= 0)
        return result;

    DeterministicRandom random(settings.seed ^ 0xa53c9e17u);
    std::vector<int> atlas(model.frames.size());
    std::iota(atlas.begin(), atlas.end(), 0);
    for (size_t index = atlas.size(); index > 1u; --index)
        std::swap(atlas[index - 1u],
                  atlas[static_cast<size_t>(random.next() % static_cast<uint32_t>(index))]);

    // These are user-selected generative styles. They alter the geometry of one common model;
    // none of them classify or special-case the source material.
    const auto style = settings.structure;
    const auto groupCount = style == TextureStructure::continuous ? 7
        : style == TextureStructure::particles ? 23 : 13;
    std::vector<Orbit> orbits(static_cast<size_t>(groupCount));
    for (int group = 0; group < groupCount; ++group)
    {
        const auto curvePoints = style == TextureStructure::continuous ? 7
            : style == TextureStructure::particles ? 19 : 13;
        orbits[static_cast<size_t>(group)] = createOrbit(
            random, curvePoints, static_cast<int>(atlas.size()));
    }
    const auto globalOrbit = createOrbit(
        random, 11, static_cast<int>(atlas.size()));

    const auto closureSamples = juce::jlimit(
        model.fftSize,
        juce::jmax(model.fftSize, targetSamples / 5),
        juce::roundToInt(sampleRate * (0.18 + 0.24 * settings.flatten)));
    const auto preRoll = model.fftSize * 2;
    const auto synthesisSamples = preRoll + targetSamples + closureSamples + model.fftSize;
    const auto frameCount = 1 + (synthesisSamples - model.fftSize) / model.hop;
    juce::AudioBuffer<float> componentOla(model.components, synthesisSamples);
    componentOla.clear();
    std::vector<float> olaWeight(static_cast<size_t>(synthesisSamples), 0.0f);
    std::vector<std::vector<float>> phase(
        static_cast<size_t>(model.components),
        std::vector<float>(static_cast<size_t>(model.bins), 0.0f));
    for (auto& component : phase)
        for (auto& value : component)
            value = juce::MathConstants<float>::twoPi * random.unit();

    std::vector<float> window(static_cast<size_t>(model.fftSize), 0.0f);
    for (int sample = 0; sample < model.fftSize; ++sample)
        window[static_cast<size_t>(sample)] = std::sqrt(
            0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                * static_cast<float>(sample) / static_cast<float>(model.fftSize - 1)));

    juce::dsp::FFT fft(model.fftOrder);
    const auto targetFrames = juce::jmax(1.0f,
        static_cast<float>(targetSamples) / static_cast<float>(model.hop));
    const auto transform = juce::jlimit(0.0f, 1.0f, settings.sourceMatch);
    const auto stability = juce::jlimit(0.0f, 1.0f, settings.flatten);
    const auto variation = juce::jlimit(0.0f, 1.0f, settings.variation);
    const auto independentWeight = style == TextureStructure::continuous
        ? 0.24f : style == TextureStructure::particles ? 0.74f : 0.48f;
    const auto motionDepth = 0.20f + 0.62f * variation * (1.0f - 0.48f * stability);
    const auto phaseMemory = 0.22f + 0.58f * (1.0f - transform);
    const auto resonanceEmphasis = 1.0f + transform
        * (style == TextureStructure::particles ? 0.92f : 0.58f);

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        const auto frameStart = frameIndex * model.hop;
        auto relativeFrame = static_cast<float>(frameStart - preRoll)
                             / static_cast<float>(model.hop);
        relativeFrame = std::fmod(relativeFrame, targetFrames);
        if (relativeFrame < 0.0f)
            relativeFrame += targetFrames;
        const auto progress = relativeFrame / targetFrames;

        for (int component = 0; component < model.components; ++component)
        {
            std::vector<float> spectrum(static_cast<size_t>(2 * model.fftSize), 0.0f);
            for (int bin = 0; bin < model.bins; ++bin)
            {
                const auto group = juce::jlimit(0, groupCount - 1,
                    bin * groupCount / juce::jmax(1, model.bins));
                const auto& orbit = orbits[static_cast<size_t>(group)];
                const auto global = interpolateFrameValue(
                    model, atlas, globalOrbit, progress, component, bin, false);
                const auto local = interpolateFrameValue(
                    model, atlas, orbit, progress, component, bin, false);
                const auto mean = model.meanLogMagnitude[static_cast<size_t>(component)]
                                                        [static_cast<size_t>(bin)];
                const auto shape = model.spectralShape[static_cast<size_t>(component)]
                                                      [static_cast<size_t>(bin)];
                const auto sourceDetail = mean - shape;
                const auto mutation = transform * variation
                    * (0.15f + 0.16f * (style == TextureStructure::particles))
                    * (2.0f * circularPosition(orbit, progress) - 1.0f);
                auto logMagnitude = mean
                    + motionDepth * ((1.0f - independentWeight) * (global - mean)
                                     + independentWeight * (local - mean))
                    + (resonanceEmphasis - 1.0f) * sourceDetail + mutation;
                logMagnitude = juce::jlimit(-14.0f, 8.0f, logMagnitude);
                const auto magnitude = std::exp(logMagnitude);

                const auto sourcePhaseDeviation = interpolateFrameValue(
                    model, atlas, orbit, progress, component, bin, true);
                const auto expected = juce::MathConstants<float>::twoPi
                    * static_cast<float>(bin * model.hop)
                    / static_cast<float>(model.fftSize);
                const auto spectralDrift = transform * variation
                    * 0.055f * (2.0f * circularPosition(
                        orbits[static_cast<size_t>((group + 3) % groupCount)],
                        progress) - 1.0f);
                auto& binPhase = phase[static_cast<size_t>(component)]
                                      [static_cast<size_t>(bin)];
                binPhase += expected + phaseMemory * sourcePhaseDeviation + spectralDrift;
                binPhase = std::fmod(binPhase, juce::MathConstants<float>::twoPi);
                spectrum[static_cast<size_t>(2 * bin)] = magnitude * std::cos(binPhase);
                spectrum[static_cast<size_t>(2 * bin + 1)] = magnitude * std::sin(binPhase);
            }
            spectrum[1] = 0.0f;
            spectrum[static_cast<size_t>(2 * (model.bins - 1) + 1)] = 0.0f;
            fft.performRealOnlyInverseTransform(spectrum.data());
            for (int sample = 0; sample < model.fftSize; ++sample)
            {
                const auto position = frameStart + sample;
                if (position >= synthesisSamples)
                    break;
                const auto windowValue = window[static_cast<size_t>(sample)];
                componentOla.addSample(component, position,
                    spectrum[static_cast<size_t>(sample)] * windowValue);
                if (component == 0)
                    olaWeight[static_cast<size_t>(position)] += windowValue * windowValue;
            }
        }
    }

    juce::AudioBuffer<float> extended(channels, targetSamples + closureSamples);
    extended.clear();
    constexpr auto inverseRootTwo = 0.7071067811865475f;
    for (int sample = 0; sample < extended.getNumSamples(); ++sample)
    {
        const auto position = preRoll + sample;
        const auto normalization = 1.0f / juce::jmax(
            1.0e-6f, olaWeight[static_cast<size_t>(position)]);
        const auto mid = componentOla.getSample(0, position) * normalization;
        const auto side = model.components > 1
            ? componentOla.getSample(1, position) * normalization * model.sideToMid : 0.0f;
        if (channels == 1)
            extended.setSample(0, sample, mid);
        else
        {
            extended.setSample(0, sample,
                (mid + side) * inverseRootTwo * model.leftGain);
            extended.setSample(1, sample,
                (mid - side) * inverseRootTwo * model.rightGain);
        }
    }

    result.audio.makeCopyOf(extended, true);
    result.audio.setSize(channels, targetSamples, true, false, true);
    for (int sample = 0; sample < closureSamples; ++sample)
    {
        const auto progress = static_cast<float>(sample)
                              / static_cast<float>(juce::jmax(1, closureSamples - 1));
        const auto continuationGain = std::cos(
            0.5f * juce::MathConstants<float>::pi * progress);
        const auto beginningGain = std::sin(
            0.5f * juce::MathConstants<float>::pi * progress);
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto continuation = extended.getSample(channel, targetSamples + sample);
            const auto beginning = result.audio.getSample(channel, sample);
            result.audio.setSample(channel, sample,
                continuationGain * continuation + beginningGain * beginning);
        }
    }

    // Slow movement is a closed trajectory spanning the complete requested loop. It cannot form
    // a source-length recurrence because its only exact period is the output duration.
    const auto macroDepthDb = (1.0f - stability) * (1.2f + 2.8f * variation);
    for (int sample = 0; sample < targetSamples; ++sample)
    {
        const auto progress = static_cast<float>(sample)
                              / static_cast<float>(juce::jmax(1, targetSamples));
        const auto movement = 0.68f * std::sin(
            juce::MathConstants<float>::twoPi * progress
            + juce::MathConstants<float>::twoPi * globalOrbit.points[0])
            + 0.32f * std::sin(juce::MathConstants<float>::twoPi
                * 3.0f * progress + 0.37f
                + juce::MathConstants<float>::twoPi * globalOrbit.points[1]);
        const auto gain = juce::Decibels::decibelsToGain(macroDepthDb * movement);
        for (int channel = 0; channel < channels; ++channel)
            result.audio.setSample(channel, sample,
                result.audio.getSample(channel, sample) * gain);
    }

    auto outputEnergy = 0.0;
    auto outputCount = 0;
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < targetSamples; ++sample)
        {
            const auto value = result.audio.getSample(channel, sample);
            outputEnergy += static_cast<double>(value) * value;
            ++outputCount;
        }
    const auto outputRms = std::sqrt(static_cast<float>(outputEnergy
        / static_cast<double>(juce::jmax(1, outputCount))));
    result.audio.applyGain(model.sourceRms / juce::jmax(1.0e-7f, outputRms));
    return result;
}
}

namespace TextureMaterialModel
{
RenderedMaterial render(const juce::AudioBuffer<float>& source,
                        const double sampleRate,
                        const int targetSamples,
                        const TextureSynthesisSettings& settings)
{
    if (source.getNumChannels() == 0 || source.getNumSamples() < 64
        || sampleRate <= 0.0 || targetSamples <= 0)
        return {};
    const auto model = analyse(source);
    return synthesize(model, source, sampleRate, targetSamples, settings);
}
}
