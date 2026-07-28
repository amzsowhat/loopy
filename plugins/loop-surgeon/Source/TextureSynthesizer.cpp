#include "TextureSynthesizer.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    const auto desiredGrainSeconds = juce::jlimit(
        0.45f, 1.8f, 0.55f + sourceSeconds * 0.14f);
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

    std::vector<Grain> grains;
    for (int start = 0; start <= availableStart; start += candidateHop)
    {
        Grain grain;
        grain.start = start;
        grain.head = analyseBoundary(source, start, featureSamples);
        grain.tail = analyseBoundary(source, start + grainSamples - featureSamples,
                                     featureSamples);
        grains.push_back(grain);
    }
    if (grains.empty() || grains.back().start != availableStart)
    {
        Grain grain;
        grain.start = availableStart;
        grain.head = analyseBoundary(source, availableStart, featureSamples);
        grain.tail = analyseBoundary(source, availableStart + grainSamples - featureSamples,
                                     featureSamples);
        grains.push_back(grain);
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
                                       gain * window * source.getSample(
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
