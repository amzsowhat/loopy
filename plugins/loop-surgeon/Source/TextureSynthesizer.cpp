#include "TextureSynthesizer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
constexpr int spectrumBands = 8;

struct BoundaryFeature
{
    std::array<float, spectrumBands> spectrum {};
    float rms = 0.0f;
    float derivative = 0.0f;
    float stereoCorrelation = 1.0f;
};

struct Grain
{
    int start = 0;
    BoundaryFeature head;
    BoundaryFeature tail;
};

struct GrainEnvelopeStats
{
    float mean = 0.0f;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float coefficientOfVariation = 0.0f;
    float activeFraction = 0.0f;
};

struct PreparedSource
{
    juce::AudioBuffer<float> carrier;
    std::vector<float> slowEnvelope;
    float activeThreshold = 0.0f;
};

class DeterministicRandom
{
public:
    explicit DeterministicRandom(uint32_t initial) : state(initial != 0 ? initial : 1u) {}

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
        return static_cast<float>(next() & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    int below(const int limit) noexcept
    {
        return limit > 1 ? static_cast<int>(next() % static_cast<uint32_t>(limit)) : 0;
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

BoundaryFeature analyseBoundary(const juce::AudioBuffer<float>& source,
                                const int firstSample,
                                const int sampleCount)
{
    BoundaryFeature feature;
    const auto count = juce::jmax(16, sampleCount);
    const auto start = juce::jlimit(0, juce::jmax(0, source.getNumSamples() - count), firstSample);
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

    feature.rms = std::sqrt(static_cast<float>(energy / juce::jmax(1, analysed)));
    feature.derivative = std::sqrt(
        static_cast<float>(differenceEnergy / juce::jmax(1, analysed)));
    if (source.getNumChannels() > 1)
        feature.stereoCorrelation = static_cast<float>(
            stereoDot / std::sqrt(juce::jmax(1.0e-12, stereoLeft * stereoRight)));

    constexpr int transformSize = 128;
    std::array<float, transformSize> window {};
    for (int index = 0; index < transformSize; ++index)
    {
        const auto sourceOffset = index * juce::jmax(1, count - 1) / (transformSize - 1);
        const auto position = juce::jmin(source.getNumSamples() - 1, start + sourceOffset);
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
    const auto spectrumTotal = std::accumulate(feature.spectrum.begin(),
                                               feature.spectrum.end(), 1.0e-6f);
    for (auto& value : feature.spectrum)
        value /= spectrumTotal;
    return feature;
}

float featureDistance(const BoundaryFeature& tail, const BoundaryFeature& head)
{
    auto spectrumDistance = 0.0f;
    for (int band = 0; band < spectrumBands; ++band)
        spectrumDistance += std::abs(tail.spectrum[static_cast<size_t>(band)]
                                     - head.spectrum[static_cast<size_t>(band)]);
    const auto levelDistance = std::abs(tail.rms - head.rms)
                               / juce::jmax(0.01f, tail.rms + head.rms);
    const auto derivativeDistance = std::abs(tail.derivative - head.derivative)
                                    / juce::jmax(0.01f, tail.derivative + head.derivative);
    const auto stereoDistance = 0.5f * std::abs(tail.stereoCorrelation
                                                - head.stereoCorrelation);
    return 0.52f * spectrumDistance + 0.23f * levelDistance
           + 0.17f * derivativeDistance + 0.08f * stereoDistance;
}

float similarityScore(const float distance)
{
    return 100.0f * std::exp(-2.6f * juce::jmax(0.0f, distance));
}

BoundaryFeature analyseWhole(const juce::AudioBuffer<float>& audio)
{
    return analyseBoundary(audio, 0, audio.getNumSamples());
}

double samplePower(const juce::AudioBuffer<float>& audio, const int sample)
{
    auto power = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto value = audio.getSample(channel, sample);
        power += static_cast<double>(value) * value;
    }
    return power / static_cast<double>(juce::jmax(1, audio.getNumChannels()));
}

std::vector<float> calculateSlowEnvelope(const juce::AudioBuffer<float>& audio,
                                         const double sampleRate,
                                         const float windowSeconds,
                                         const bool circular)
{
    const auto sampleCount = audio.getNumSamples();
    std::vector<float> envelope(static_cast<size_t>(sampleCount), 0.0f);
    if (sampleCount == 0)
        return envelope;

    const auto window = juce::jlimit(
        1, sampleCount, juce::roundToInt(sampleRate * windowSeconds));
    const auto before = window / 2;
    const auto after = window - before - 1;

    if (circular)
    {
        const auto wrap = [sampleCount] (int sample)
        {
            sample %= sampleCount;
            return sample < 0 ? sample + sampleCount : sample;
        };
        auto sum = 0.0;
        for (int offset = -before; offset <= after; ++offset)
            sum += samplePower(audio, wrap(offset));

        for (int sample = 0; sample < sampleCount; ++sample)
        {
            envelope[static_cast<size_t>(sample)] = std::sqrt(
                static_cast<float>(juce::jmax(0.0, sum)
                                   / static_cast<double>(window)));
            sum -= samplePower(audio, wrap(sample - before));
            sum += samplePower(audio, wrap(sample + after + 1));
        }
        return envelope;
    }

    auto left = 0;
    auto right = juce::jmin(sampleCount - 1, after);
    auto sum = 0.0;
    for (int sample = left; sample <= right; ++sample)
        sum += samplePower(audio, sample);

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const auto wantedLeft = juce::jmax(0, sample - before);
        const auto wantedRight = juce::jmin(sampleCount - 1, sample + after);
        while (left < wantedLeft)
            sum -= samplePower(audio, left++);
        while (right < wantedRight)
            sum += samplePower(audio, ++right);
        envelope[static_cast<size_t>(sample)] = std::sqrt(
            static_cast<float>(juce::jmax(0.0, sum)
                               / static_cast<double>(right - left + 1)));
    }
    return envelope;
}

float sampledPercentile(const std::vector<float>& values, const float proportion)
{
    if (values.empty())
        return 0.0f;

    std::vector<float> samples;
    const auto stride = juce::jmax<size_t>(1, values.size() / 8192u);
    samples.reserve(values.size() / stride + 1u);
    for (size_t index = 0; index < values.size(); index += stride)
        samples.push_back(values[index]);
    const auto position = static_cast<size_t>(juce::roundToInt(
        juce::jlimit(0.0f, 1.0f, proportion)
        * static_cast<float>(samples.size() - 1u)));
    std::nth_element(samples.begin(), samples.begin() + static_cast<ptrdiff_t>(position),
                     samples.end());
    return samples[position];
}

PreparedSource prepareStationaryCarrier(const juce::AudioBuffer<float>& source,
                                         const double sampleRate)
{
    PreparedSource prepared;
    prepared.slowEnvelope = calculateSlowEnvelope(source, sampleRate, 0.16f, false);
    const auto highLevel = sampledPercentile(prepared.slowEnvelope, 0.98f);
    prepared.activeThreshold = juce::jmax(1.0e-5f, highLevel * 0.11f);

    std::vector<float> activeLevels;
    const auto stride = juce::jmax<size_t>(1, prepared.slowEnvelope.size() / 8192u);
    for (size_t index = 0; index < prepared.slowEnvelope.size(); index += stride)
        if (prepared.slowEnvelope[index] >= prepared.activeThreshold)
            activeLevels.push_back(prepared.slowEnvelope[index]);
    const auto targetLevel = juce::jmax(
        prepared.activeThreshold, sampledPercentile(activeLevels, 0.62f));

    // Convert the detected one-shot macro envelope into a slow, shared channel gain.
    // A common gain preserves the source's inter-channel image.
    for (auto& level : prepared.slowEnvelope)
    {
        const auto denominator = juce::jmax(level, prepared.activeThreshold);
        level = juce::jlimit(0.38f, 3.2f, targetLevel / denominator);
    }

    const auto smoothing = std::exp(
        -1.0f / static_cast<float>(juce::jmax(1.0, sampleRate * 0.075)));
    auto state = prepared.slowEnvelope.front();
    for (auto& gain : prepared.slowEnvelope)
    {
        state = smoothing * state + (1.0f - smoothing) * gain;
        gain = state;
    }
    state = prepared.slowEnvelope.back();
    for (auto iterator = prepared.slowEnvelope.rbegin();
         iterator != prepared.slowEnvelope.rend(); ++iterator)
    {
        state = smoothing * state + (1.0f - smoothing) * *iterator;
        *iterator = state;
    }

    prepared.carrier.setSize(source.getNumChannels(), source.getNumSamples(),
                             false, true, false);
    for (int channel = 0; channel < source.getNumChannels(); ++channel)
        for (int sample = 0; sample < source.getNumSamples(); ++sample)
            prepared.carrier.setSample(
                channel, sample,
                source.getSample(channel, sample)
                    * prepared.slowEnvelope[static_cast<size_t>(sample)]);

    // The original envelope is still needed for stable-body selection, so restore it.
    prepared.slowEnvelope = calculateSlowEnvelope(source, sampleRate, 0.16f, false);
    return prepared;
}

GrainEnvelopeStats analyseGrainEnvelope(const std::vector<float>& envelope,
                                        const int start,
                                        const int sampleCount,
                                        const float activeThreshold)
{
    GrainEnvelopeStats stats;
    stats.minimum = std::numeric_limits<float>::max();
    constexpr int observations = 64;
    auto squareTotal = 0.0f;
    auto active = 0;
    for (int observation = 0; observation < observations; ++observation)
    {
        const auto offset = observation * juce::jmax(0, sampleCount - 1)
                            / juce::jmax(1, observations - 1);
        const auto level = envelope[static_cast<size_t>(start + offset)];
        stats.mean += level;
        squareTotal += level * level;
        stats.minimum = juce::jmin(stats.minimum, level);
        stats.maximum = juce::jmax(stats.maximum, level);
        active += level >= activeThreshold ? 1 : 0;
    }
    stats.mean /= static_cast<float>(observations);
    const auto variance = juce::jmax(
        0.0f, squareTotal / static_cast<float>(observations) - stats.mean * stats.mean);
    stats.coefficientOfVariation = std::sqrt(variance) / juce::jmax(1.0e-6f, stats.mean);
    stats.activeFraction = static_cast<float>(active) / static_cast<float>(observations);
    return stats;
}

void smoothCircular(std::vector<float>& values, const int requestedWindow)
{
    if (values.empty())
        return;
    const auto size = static_cast<int>(values.size());
    const auto window = juce::jlimit(1, size, requestedWindow);
    const auto before = window / 2;
    const auto after = window - before - 1;
    const auto wrap = [size] (int position)
    {
        position %= size;
        return position < 0 ? position + size : position;
    };
    std::vector<float> smoothed(values.size(), 0.0f);
    auto sum = 0.0;
    for (int offset = -before; offset <= after; ++offset)
        sum += values[static_cast<size_t>(wrap(offset))];
    for (int sample = 0; sample < size; ++sample)
    {
        smoothed[static_cast<size_t>(sample)] = static_cast<float>(
            sum / static_cast<double>(window));
        sum -= values[static_cast<size_t>(wrap(sample - before))];
        sum += values[static_cast<size_t>(wrap(sample + after + 1))];
    }
    values.swap(smoothed);
}

float stabiliseCircularMacroEnvelope(juce::AudioBuffer<float>& audio,
                                     const double sampleRate)
{
    auto envelope = calculateSlowEnvelope(audio, sampleRate, 0.36f, true);
    const auto target = juce::jmax(1.0e-6f, sampledPercentile(envelope, 0.50f));
    for (auto& gain : envelope)
    {
        const auto ratio = target / juce::jmax(target * 0.28f, gain);
        gain = juce::jlimit(0.55f, 1.80f, std::pow(ratio, 0.86f));
    }
    smoothCircular(envelope, juce::roundToInt(sampleRate * 0.11));
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample(channel, sample,
                            audio.getSample(channel, sample)
                                * envelope[static_cast<size_t>(sample)]);

    const auto corrected = calculateSlowEnvelope(audio, sampleRate, 0.36f, true);
    const auto low = juce::jmax(1.0e-7f, sampledPercentile(corrected, 0.10f));
    const auto high = juce::jmax(low, sampledPercentile(corrected, 0.90f));
    const auto rangeDecibels = 20.0f * std::log10(high / low);
    auto mean = 0.0;
    auto squareTotal = 0.0;
    const auto stride = juce::jmax<size_t>(1, corrected.size() / 8192u);
    auto count = 0;
    for (size_t index = 0; index < corrected.size(); index += stride)
    {
        mean += corrected[index];
        squareTotal += static_cast<double>(corrected[index]) * corrected[index];
        ++count;
    }
    mean /= static_cast<double>(juce::jmax(1, count));
    const auto variance = juce::jmax(
        0.0, squareTotal / static_cast<double>(juce::jmax(1, count)) - mean * mean);
    const auto coefficientOfVariation = static_cast<float>(
        std::sqrt(variance) / juce::jmax(1.0e-7, mean));
    return 100.0f * std::exp(
        -0.11f * rangeDecibels - 1.8f * coefficientOfVariation);
}
}

TextureSynthesisResult TextureSynthesizer::synthesize(
    const juce::AudioBuffer<float>& source,
    const double sampleRate,
    TextureSynthesisSettings settings)
{
    TextureSynthesisResult result;
    if (source.getNumChannels() < 1 || source.getNumSamples() < 128 || sampleRate <= 0.0)
        return result;

    settings.durationSeconds = juce::jlimit(4.0f, 60.0f, settings.durationSeconds);
    settings.variation = juce::jlimit(0.0f, 1.0f, settings.variation);
    DeterministicRandom random(settings.seed);

    const auto sourceSamples = source.getNumSamples();
    const auto sourceSeconds = static_cast<float>(sourceSamples / sampleRate);
    auto prepared = prepareStationaryCarrier(source, sampleRate);
    const auto& synthesisSource = prepared.carrier;
    const auto desiredGrainSeconds = juce::jlimit(
        0.42f, 1.35f, 0.50f + sourceSeconds * 0.08f);
    const auto grainSamples = juce::jlimit(
        128, juce::jmax(128, sourceSamples - 1),
        juce::jmin(juce::roundToInt(sampleRate * desiredGrainSeconds),
                   juce::roundToInt(sourceSamples * 0.72f)));
    const auto overlapRatio = 0.38f + 0.14f * settings.variation;
    const auto overlapSamples = juce::jlimit(
        32, grainSamples - 32, juce::roundToInt(grainSamples * overlapRatio));
    const auto outputHop = juce::jmax(32, grainSamples - overlapSamples);
    const auto featureSamples = juce::jlimit(
        64, grainSamples / 2, juce::roundToInt(sampleRate * 0.22));
    const auto availableStart = juce::jmax(0, sourceSamples - grainSamples);
    const auto candidateHop = juce::jmax(
        1, juce::jmax(juce::roundToInt(sampleRate * 0.075), availableStart / 95));

    struct CandidateSite
    {
        int start = 0;
        GrainEnvelopeStats envelope;
    };
    std::vector<CandidateSite> sites;
    for (int start = 0; start <= availableStart; start += candidateHop)
        sites.push_back({ start, analyseGrainEnvelope(
            prepared.slowEnvelope, start, grainSamples, prepared.activeThreshold) });
    if (sites.empty() || sites.back().start != availableStart)
        sites.push_back({ availableStart, analyseGrainEnvelope(
            prepared.slowEnvelope, availableStart, grainSamples,
            prepared.activeThreshold) });

    std::vector<Grain> grains;
    const auto collectGrains = [&] (const bool relaxed)
    {
        grains.clear();
        for (const auto& site : sites)
        {
            const auto levelRatio = site.envelope.maximum
                / juce::jmax(1.0e-6f, site.envelope.minimum);
            const auto acceptable = relaxed
                ? (site.envelope.activeFraction >= 0.72f
                   && levelRatio <= 3.0f
                   && site.envelope.coefficientOfVariation <= 0.34f)
                : (site.envelope.activeFraction >= 0.90f
                   && site.envelope.mean >= prepared.activeThreshold * 1.12f
                   && levelRatio <= 1.85f
                   && site.envelope.coefficientOfVariation <= 0.20f);
            if (!acceptable)
                continue;
            Grain grain;
            grain.start = site.start;
            grain.head = analyseBoundary(synthesisSource, site.start, featureSamples);
            grain.tail = analyseBoundary(
                synthesisSource, site.start + grainSamples - featureSamples,
                featureSamples);
            grains.push_back(grain);
        }
    };
    collectGrains(false);
    if (grains.size() < 6u)
        collectGrains(true);
    if (grains.empty())
    {
        for (const auto& site : sites)
        {
            Grain grain;
            grain.start = site.start;
            grain.head = analyseBoundary(synthesisSource, site.start, featureSamples);
            grain.tail = analyseBoundary(
                synthesisSource, site.start + grainSamples - featureSamples,
                featureSamples);
            grains.push_back(grain);
        }
    }

    const auto targetSamples = juce::jmax(
        grainSamples, juce::roundToInt(sampleRate * settings.durationSeconds));
    const auto grainCount = juce::jmax(2, (targetSamples + outputHop - 1) / outputHop + 1);
    std::vector<int> usage(grains.size(), 0);
    std::vector<int> selected;
    selected.reserve(static_cast<size_t>(grainCount));
    auto current = random.below(static_cast<int>(grains.size()));
    selected.push_back(current);
    ++usage[static_cast<size_t>(current)];

    auto transitionDistanceTotal = 0.0f;
    for (int outputGrain = 1; outputGrain < grainCount; ++outputGrain)
    {
        struct RankedChoice { int index = 0; float cost = 0.0f; };
        std::vector<RankedChoice> choices;
        choices.reserve(grains.size());
        for (int candidate = 0; candidate < static_cast<int>(grains.size()); ++candidate)
        {
            auto cost = featureDistance(grains[static_cast<size_t>(current)].tail,
                                        grains[static_cast<size_t>(candidate)].head);
            const auto sourceDistance = std::abs(grains[static_cast<size_t>(candidate)].start
                                                 - grains[static_cast<size_t>(current)].start);
            const auto proximity = 1.0f - juce::jlimit(
                0.0f, 1.0f, static_cast<float>(sourceDistance)
                            / static_cast<float>(juce::jmax(1, grainSamples)));
            cost += settings.variation * 0.42f * proximity;
            cost += settings.variation * 0.16f
                    * static_cast<float>(usage[static_cast<size_t>(candidate)]);
            const auto recentCount = juce::jmin(5, static_cast<int>(selected.size()));
            for (int recent = 0; recent < recentCount; ++recent)
            {
                if (selected[selected.size() - 1u - static_cast<size_t>(recent)] == candidate)
                    cost += settings.variation * (0.72f - 0.09f * static_cast<float>(recent));
            }
            if (outputGrain + 2 >= grainCount)
                cost += 0.55f * featureDistance(
                    grains[static_cast<size_t>(candidate)].tail,
                    grains[static_cast<size_t>(selected.front())].head);
            choices.push_back({ candidate, cost });
        }
        std::sort(choices.begin(), choices.end(),
                  [] (const auto& left, const auto& right) { return left.cost < right.cost; });
        const auto topCount = juce::jlimit(
            1, static_cast<int>(choices.size()),
            1 + juce::roundToInt(settings.variation * 7.0f));
        auto totalWeight = 0.0f;
        for (int rank = 0; rank < topCount; ++rank)
            totalWeight += std::exp(-0.72f * static_cast<float>(rank));
        auto draw = random.unit() * totalWeight;
        auto chosenRank = 0;
        for (; chosenRank + 1 < topCount; ++chosenRank)
        {
            draw -= std::exp(-0.72f * static_cast<float>(chosenRank));
            if (draw <= 0.0f)
                break;
        }
        const auto choice = choices[static_cast<size_t>(chosenRank)];
        transitionDistanceTotal += featureDistance(
            grains[static_cast<size_t>(current)].tail,
            grains[static_cast<size_t>(choice.index)].head);
        current = choice.index;
        selected.push_back(current);
        ++usage[static_cast<size_t>(current)];
    }

    result.audio.setSize(source.getNumChannels(), targetSamples, false, true, false);
    std::vector<float> normalisation(static_cast<size_t>(targetSamples), 0.0f);
    result.sourceGrainStarts.reserve(selected.size());
    for (int sequence = 0; sequence < static_cast<int>(selected.size()); ++sequence)
    {
        const auto baseStart = grains[static_cast<size_t>(
            selected[static_cast<size_t>(sequence)])].start;
        const auto jitterLimit = juce::jmin(candidateHop / 2,
                                           juce::jmax(0, availableStart - baseStart));
        const auto signedJitter = jitterLimit > 0
            ? random.below(2 * jitterLimit + 1) - jitterLimit : 0;
        const auto sourceStart = juce::jlimit(0, availableStart, baseStart + signedJitter);
        result.sourceGrainStarts.push_back(sourceStart);
        const auto gain = 0.975f + 0.05f * random.unit();
        const auto outputStart = sequence * outputHop;

        for (int offset = 0; offset < grainSamples; ++offset)
        {
            auto window = 1.0f;
            if (offset < overlapSamples)
            {
                const auto phase = static_cast<float>(offset + 1)
                                   / static_cast<float>(overlapSamples + 1);
                const auto sine = std::sin(phase * juce::MathConstants<float>::halfPi);
                window = sine;
            }
            else if (offset >= grainSamples - overlapSamples)
            {
                const auto phase = static_cast<float>(grainSamples - offset)
                                   / static_cast<float>(overlapSamples + 1);
                const auto sine = std::sin(phase * juce::MathConstants<float>::halfPi);
                window = sine;
            }
            const auto outputPosition = (outputStart + offset) % targetSamples;
            normalisation[static_cast<size_t>(outputPosition)] += window * window;
            for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
                result.audio.addSample(channel, outputPosition,
                                       gain * window * synthesisSource.getSample(
                                           channel, sourceStart + offset));
        }
    }

    for (int sample = 0; sample < targetSamples; ++sample)
    {
        const auto scale = 1.0f / std::sqrt(juce::jmax(
            1.0e-10f, normalisation[static_cast<size_t>(sample)]));
        for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
            result.audio.setSample(channel, sample,
                                   result.audio.getSample(channel, sample) * scale);
    }

    result.macroStability = stabiliseCircularMacroEnvelope(result.audio, sampleRate);

    auto outputPeak = 0.0f;
    for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
        outputPeak = juce::jmax(outputPeak,
                               result.audio.getMagnitude(channel, 0, targetSamples));
    if (outputPeak > 0.98f)
        result.audio.applyGain(0.98f / outputPeak);

    const auto closureDistance = featureDistance(
        grains[static_cast<size_t>(selected.back())].tail,
        grains[static_cast<size_t>(selected.front())].head);
    result.transitionQuality = similarityScore(
        transitionDistanceTotal / static_cast<float>(juce::jmax(1, grainCount - 1)));
    const auto renderedTail = analyseBoundary(
        result.audio, targetSamples - featureSamples, featureSamples);
    const auto renderedHead = analyseBoundary(result.audio, 0, featureSamples);
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
        -3.0f * boundaryJump / juce::jmax(0.02f, boundaryReference));
    result.closureQuality = 0.45f * similarityScore(closureDistance)
                            + 0.35f * similarityScore(
                                featureDistance(renderedTail, renderedHead))
                            + 0.20f * jumpQuality;

    const auto sourceFeature = analyseWhole(source);
    const auto outputFeature = analyseWhole(result.audio);
    result.spectrumPreservation = similarityScore(featureDistance(sourceFeature, outputFeature));
    result.stereoPreservation = 100.0f * std::exp(
        -2.2f * std::abs(sourceFeature.stereoCorrelation - outputFeature.stereoCorrelation));
    result.transientPreservation = 100.0f * std::exp(
        -2.2f * std::abs(sourceFeature.derivative - outputFeature.derivative)
        / juce::jmax(0.01f, sourceFeature.derivative + outputFeature.derivative));

    auto usedCount = 0;
    for (const auto count : usage)
        usedCount += count > 0 ? 1 : 0;
    auto positionalMotion = 0.0f;
    for (size_t index = 1; index < result.sourceGrainStarts.size(); ++index)
        positionalMotion += juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(std::abs(result.sourceGrainStarts[index]
                                        - result.sourceGrainStarts[index - 1]))
            / static_cast<float>(juce::jmax(1, grainSamples)));
    positionalMotion /= static_cast<float>(
        juce::jmax<size_t>(1, result.sourceGrainStarts.size() - 1));
    const auto coverage = static_cast<float>(usedCount)
                          / static_cast<float>(juce::jmax(
                              1, juce::jmin(static_cast<int>(grains.size()), grainCount)));
    result.diversity = 100.0f * juce::jlimit(
        0.0f, 1.0f, 0.58f * coverage + 0.42f * positionalMotion);
    return result;
}
