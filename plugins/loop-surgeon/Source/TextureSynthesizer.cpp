#include "TextureSynthesizer.h"

#include "SignalDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
struct SourceRegion
{
    int start = 0;
    float activityPenalty = 0.0f;
    float stationarityPenalty = 0.0f;
    int lastUsedStep = std::numeric_limits<int>::min() / 2;
};

uint32_t nextRandom(uint32_t& state) noexcept
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float randomUnit(uint32_t& state) noexcept
{
    return static_cast<float>(nextRandom(state) & 0x00ffffffu)
           / static_cast<float>(0x01000000u);
}

float regionRms(const juce::AudioBuffer<float>& audio, const int start, const int length)
{
    if (length <= 0 || audio.getNumChannels() <= 0)
        return 0.0f;

    double energy = 0.0;
    auto count = 0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto* samples = audio.getReadPointer(channel, start);
        for (int sample = 0; sample < length; ++sample)
        {
            const auto value = static_cast<double>(samples[sample]);
            energy += value * value;
            ++count;
        }
    }
    return static_cast<float>(std::sqrt(energy / static_cast<double>(juce::jmax(1, count))));
}

float channelRms(const juce::AudioBuffer<float>& audio, const int channel)
{
    if (!juce::isPositiveAndBelow(channel, audio.getNumChannels())
        || audio.getNumSamples() <= 0)
        return 0.0f;
    double energy = 0.0;
    const auto* samples = audio.getReadPointer(channel);
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto value = static_cast<double>(samples[sample]);
        energy += value * value;
    }
    return static_cast<float>(std::sqrt(
        energy / static_cast<double>(audio.getNumSamples())));
}

float median(std::vector<float> values)
{
    if (values.empty())
        return 0.0f;
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2u);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

juce::AudioBuffer<float> removeMacroEnvelope(const juce::AudioBuffer<float>& source,
                                              const double sampleRate,
                                              const float amount)
{
    juce::AudioBuffer<float> conditioned(source);
    const auto samples = source.getNumSamples();
    if (samples < 32 || amount <= 0.0f)
        return conditioned;

    const auto window = juce::jlimit(32, samples,
        juce::roundToInt(sampleRate * 0.080));
    const auto hop = juce::jmax(16, window / 4);
    const auto frames = juce::jmax(2, 1 + (samples - 1) / hop);
    std::vector<float> levels(static_cast<size_t>(frames));
    auto maximumLevel = 0.0f;
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto centre = juce::jmin(samples - 1, frame * hop);
        const auto start = juce::jlimit(0, juce::jmax(0, samples - window),
                                        centre - window / 2);
        levels[static_cast<size_t>(frame)] = regionRms(source, start, window);
        maximumLevel = juce::jmax(maximumLevel, levels[static_cast<size_t>(frame)]);
    }

    std::vector<float> activeLevels;
    activeLevels.reserve(levels.size());
    for (const auto level : levels)
        if (level >= maximumLevel * 0.04f)
            activeLevels.push_back(level);
    const auto target = juce::jmax(1.0e-7f,
        activeLevels.empty() ? median(levels) : median(activeLevels));

    std::vector<float> gains(levels.size(), 1.0f);
    for (size_t frame = 0; frame < levels.size(); ++frame)
    {
        const auto protectedLevel = juce::jmax(levels[frame], target * 0.12f);
        const auto requested = std::pow(target / protectedLevel, amount);
        gains[frame] = juce::jlimit(0.40f, 3.5f, requested);
    }
    for (int pass = 0; pass < 3; ++pass)
    {
        auto previous = gains.front();
        for (size_t frame = 1; frame < gains.size(); ++frame)
        {
            gains[frame] = 0.72f * previous + 0.28f * gains[frame];
            previous = gains[frame];
        }
        previous = gains.back();
        for (size_t frame = gains.size() - 1; frame-- > 0;)
        {
            gains[frame] = 0.72f * previous + 0.28f * gains[frame];
            previous = gains[frame];
        }
    }

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto framePosition = static_cast<float>(sample) / static_cast<float>(hop);
        const auto first = juce::jlimit(0, frames - 1,
                                        static_cast<int>(std::floor(framePosition)));
        const auto second = juce::jmin(frames - 1, first + 1);
        const auto fraction = framePosition - static_cast<float>(first);
        const auto gain = gains[static_cast<size_t>(first)]
                          + fraction * (gains[static_cast<size_t>(second)]
                                        - gains[static_cast<size_t>(first)]);
        for (int channel = 0; channel < conditioned.getNumChannels(); ++channel)
            conditioned.setSample(channel, sample,
                                  conditioned.getSample(channel, sample) * gain);
    }
    return conditioned;
}

float transitionPenalty(const juce::AudioBuffer<float>& audio,
                        const int previousStart, const int nextStart,
                        const int grainSamples, const int comparisonSamples)
{
    double dot = 0.0;
    double previousEnergy = 0.0;
    double nextEnergy = 0.0;
    double derivativeMismatch = 0.0;
    double derivativeScale = 0.0;
    const auto previousTail = previousStart + grainSamples - comparisonSamples;
    const auto points = juce::jlimit(2, 48, comparisonSamples);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        auto previousValue = 0.0;
        auto nextValue = 0.0;
        for (int point = 0; point < points; ++point)
        {
            const auto sample = point * (comparisonSamples - 1) / (points - 1);
            const auto previous = static_cast<double>(
                audio.getSample(channel, previousTail + sample));
            const auto next = static_cast<double>(audio.getSample(channel, nextStart + sample));
            dot += previous * next;
            previousEnergy += previous * previous;
            nextEnergy += next * next;
            if (point > 0)
            {
                const auto previousDelta = previous - previousValue;
                const auto nextDelta = next - nextValue;
                derivativeMismatch += std::abs(previousDelta - nextDelta);
                derivativeScale += 0.5 * (std::abs(previousDelta) + std::abs(nextDelta));
            }
            previousValue = previous;
            nextValue = next;
        }
    }
    const auto denominator = std::sqrt(previousEnergy * nextEnergy);
    const auto correlation = denominator > 1.0e-12
        ? juce::jlimit(-1.0, 1.0, dot / denominator) : 0.0;
    const auto correlationPenalty = static_cast<float>(0.5 * (1.0 - correlation));
    const auto levelPenalty = static_cast<float>(
        std::abs(std::sqrt(previousEnergy) - std::sqrt(nextEnergy))
        / juce::jmax(1.0e-9, 0.5 * (std::sqrt(previousEnergy) + std::sqrt(nextEnergy))));
    const auto slopePenalty = static_cast<float>(
        derivativeMismatch / juce::jmax(1.0e-9, derivativeScale));
    return 0.58f * correlationPenalty
           + 0.24f * juce::jmin(2.0f, levelPenalty)
           + 0.18f * juce::jmin(2.0f, slopePenalty);
}

float styleGrainSeconds(const TextureStructure structure, const float variation)
{
    switch (structure)
    {
        case TextureStructure::automatic:
            return 0.70f - 0.28f * variation;
        case TextureStructure::continuous:
            return 0.48f - 0.22f * variation;
        case TextureStructure::particles:
            return 0.27f - 0.13f * variation;
    }
    return 0.45f;
}

void balanceChannelEnergy(juce::AudioBuffer<float>& audio, const float amount)
{
    if (audio.getNumChannels() < 2 || amount <= 0.0f)
        return;
    const auto left = channelRms(audio, 0);
    const auto right = channelRms(audio, 1);
    const auto target = 0.5f * (left + right);
    if (left > 1.0e-7f)
        audio.applyGain(0, 0, audio.getNumSamples(),
            std::pow(juce::jlimit(0.72f, 1.38f, target / left), amount));
    if (right > 1.0e-7f)
        audio.applyGain(1, 0, audio.getNumSamples(),
            std::pow(juce::jlimit(0.72f, 1.38f, target / right), amount));
}
}

TextureSynthesisResult TextureSynthesizer::synthesize(
    const juce::AudioBuffer<float>& source, const double sampleRate,
    TextureSynthesisSettings settings)
{
    TextureSynthesisResult result;
    if (source.getNumChannels() <= 0 || source.getNumSamples() < 32 || sampleRate <= 0.0)
        return result;

    const auto channels = juce::jlimit(1, 2, source.getNumChannels());
    const auto targetSamples = juce::jmax(
        32, juce::roundToInt(sampleRate * settings.durationSeconds));
    const auto stability = juce::jlimit(0.0f, 1.0f, settings.flatten);
    const auto transform = juce::jlimit(0.0f, 1.0f, settings.sourceMatch);
    const auto variation = juce::jlimit(0.0f, 1.0f, settings.variation);
    const auto envelopeAmount = stability * (0.30f + 0.70f * transform);
    auto conditioned = removeMacroEnvelope(source, sampleRate, envelopeAmount);

    const auto maximumGrain = juce::jmax(32,
        juce::jmin(conditioned.getNumSamples(), juce::jmax(32, targetSamples / 2)));
    const auto requestedGrain = juce::roundToInt(
        sampleRate * styleGrainSeconds(settings.structure, variation));
    const auto grainSamples = juce::jlimit(32, maximumGrain, requestedGrain);
    const auto overlapRatio = 0.60f + 0.18f * stability;
    const auto outputHop = juce::jmax(8,
        juce::roundToInt(static_cast<float>(grainSamples) * (1.0f - overlapRatio)));
    const auto maximumStart = conditioned.getNumSamples() - grainSamples;
    const auto sourceStep = juce::jmax(
        1, juce::jmax(grainSamples / 8, maximumStart / 255));
    const auto referenceRms = juce::jmax(1.0e-7f,
        regionRms(conditioned, 0, conditioned.getNumSamples()));

    std::vector<SourceRegion> regions;
    for (int start = 0;; start = juce::jmin(maximumStart, start + sourceStep))
    {
        const auto localRms = regionRms(conditioned, start, grainSamples);
        const auto firstRms = regionRms(conditioned, start, grainSamples / 2);
        const auto secondRms = regionRms(
            conditioned, start + grainSamples / 2, grainSamples - grainSamples / 2);
        const auto stationarity = std::abs(firstRms - secondRms)
                                  / juce::jmax(1.0e-7f, 0.5f * (firstRms + secondRms));
        const auto activity = std::abs(std::log(juce::jmax(1.0e-7f, localRms)
                                                / referenceRms));
        regions.push_back({ start, activity, stationarity });
        if (start == maximumStart)
            break;
    }
    if (regions.empty())
        return result;

    std::vector<float> window(static_cast<size_t>(grainSamples));
    for (int sample = 0; sample < grainSamples; ++sample)
    {
        const auto phase = juce::MathConstants<float>::twoPi
                           * static_cast<float>(sample)
                           / static_cast<float>(grainSamples);
        window[static_cast<size_t>(sample)] = std::sqrt(
            juce::jmax(0.0f, 0.5f - 0.5f * std::cos(phase)));
    }

    result.audio.setSize(channels, targetSamples, false, true, false);
    std::vector<float> normalization(static_cast<size_t>(targetSamples), 0.0f);
    auto randomState = settings.seed == 0 ? 1u : settings.seed;
    const auto comparisonSamples = juce::jlimit(
        8, juce::jmax(8, grainSamples / 4),
        juce::roundToInt(sampleRate * 0.020));
    const auto regionSpan = static_cast<float>(juce::jmax(1, maximumStart));
    const auto refractorySteps = juce::jlimit(3, 14,
        4 + juce::roundToInt(8.0f * transform));
    auto previousRegion = 0;
    auto stepIndex = 0;

    for (int outputStart = 0; outputStart < targetSamples;
         outputStart += outputHop, ++stepIndex)
    {
        auto bestRegion = 0;
        auto bestPenalty = std::numeric_limits<float>::max();
        const auto expectedStart = stepIndex == 0 ? 0
            : (regions[static_cast<size_t>(previousRegion)].start + outputHop)
                % juce::jmax(1, maximumStart + 1);
        for (int index = 0; index < static_cast<int>(regions.size()); ++index)
        {
            auto& region = regions[static_cast<size_t>(index)];
            const auto sourceDistance = std::abs(region.start - expectedStart) / regionSpan;
            const auto basePenalty = 0.34f * region.activityPenalty
                                     + 0.38f * stability * region.stationarityPenalty;
            const auto continuityPenalty = stepIndex == 0 ? 0.0f
                : transitionPenalty(conditioned,
                    regions[static_cast<size_t>(previousRegion)].start,
                    region.start, grainSamples, comparisonSamples);
            const auto age = stepIndex - region.lastUsedStep;
            const auto reusePenalty = age < refractorySteps
                ? 3.0f * static_cast<float>(refractorySteps - age)
                    / static_cast<float>(refractorySteps)
                : 0.0f;
            const auto jitter = (randomUnit(randomState) - 0.5f)
                                * variation * 0.10f;
            const auto penalty = basePenalty
                + (0.68f + 0.20f * transform) * continuityPenalty
                + (stepIndex == 0 ? 0.0f
                                  : (1.0f - transform) * 0.42f * sourceDistance)
                + reusePenalty + jitter;
            if (penalty < bestPenalty)
            {
                bestPenalty = penalty;
                bestRegion = index;
            }
        }

        auto& chosen = regions[static_cast<size_t>(bestRegion)];
        chosen.lastUsedStep = stepIndex;
        previousRegion = bestRegion;
        result.analysisFrameStarts.push_back(chosen.start);
        for (int sample = 0; sample < grainSamples; ++sample)
        {
            const auto outputSample = (outputStart + sample) % targetSamples;
            const auto gain = window[static_cast<size_t>(sample)];
            normalization[static_cast<size_t>(outputSample)] += gain;
            for (int channel = 0; channel < channels; ++channel)
                result.audio.addSample(channel, outputSample,
                    conditioned.getSample(channel, chosen.start + sample) * gain);
        }
    }

    for (int sample = 0; sample < targetSamples; ++sample)
    {
        const auto normalizer = juce::jmax(
            1.0e-6f, normalization[static_cast<size_t>(sample)]);
        for (int channel = 0; channel < channels; ++channel)
            result.audio.setSample(channel, sample,
                result.audio.getSample(channel, sample) / normalizer);
    }

    balanceChannelEnergy(result.audio, 0.45f * stability * transform);
    const auto sourceRms = SignalDiagnostics::calculateRms(source);
    const auto outputRms = SignalDiagnostics::calculateRms(result.audio);
    if (sourceRms > 1.0e-7f && outputRms > 1.0e-7f)
        result.audio.applyGain(juce::jlimit(0.5f, 2.0f, sourceRms / outputRms));

    result.usedStructure = settings.structure;
    result.containsOnlyFiniteSamples = SignalDiagnostics::repairNonFiniteAndRemoveDc(result.audio);
    result.truePeakDbtp = SignalDiagnostics::applyCircularTruePeakCeiling(
        result.audio, -1.0f);
    return result;
}
