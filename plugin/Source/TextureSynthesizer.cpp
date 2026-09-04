#include "TextureSynthesizer.h"

#include "SignalDiagnostics.h"
#include "TextureCharacter.h"

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

    const auto sourceSeconds = static_cast<double>(samples) / sampleRate;
    const auto windowSeconds = juce::jlimit(0.14, 0.65, sourceSeconds * 0.22);
    const auto window = juce::jlimit(32, samples,
        juce::roundToInt(sampleRate * windowSeconds));
    const auto hop = juce::jmax(16, window / 6);
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
        gains[frame] = juce::jlimit(0.55f, 2.2f, requested);
    }
    for (int pass = 0; pass < 4; ++pass)
    {
        auto previous = gains.front();
        for (size_t frame = 1; frame < gains.size(); ++frame)
        {
            gains[frame] = 0.82f * previous + 0.18f * gains[frame];
            previous = gains[frame];
        }
        previous = gains.back();
        for (size_t frame = gains.size() - 1; frame-- > 0;)
        {
            gains[frame] = 0.82f * previous + 0.18f * gains[frame];
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

float alignedOverlapPenalty(const juce::AudioBuffer<float>& audio,
                            const int previousStart, const int nextStart,
                            const int previousAdvance, const int overlapSamples)
{
    double dot = 0.0;
    double previousEnergy = 0.0;
    double nextEnergy = 0.0;
    double derivativeMismatch = 0.0;
    double derivativeScale = 0.0;
    const auto overlap = juce::jmax(2, overlapSamples);
    const auto previousOverlap = previousStart + previousAdvance;
    const auto points = juce::jlimit(12, 128, overlap);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        auto previousValue = 0.0;
        auto nextValue = 0.0;
        for (int point = 0; point < points; ++point)
        {
            const auto sample = point * (overlap - 1) / (points - 1);
            const auto previous = static_cast<double>(
                audio.getSample(channel, previousOverlap + sample));
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
    return 0.72f * correlationPenalty
           + 0.18f * juce::jmin(2.0f, levelPenalty)
           + 0.10f * juce::jmin(2.0f, slopePenalty);
}

float styleGrainSeconds(const TextureStructure structure, const float variation)
{
    switch (structure)
    {
        case TextureStructure::automatic:
            return 0.95f - 0.42f * variation;
        case TextureStructure::continuous:
            return 0.74f - 0.30f * variation;
        case TextureStructure::particles:
            return 0.46f - 0.20f * variation;
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

void flattenCircularEnvelope(juce::AudioBuffer<float>& audio,
                             const double sampleRate,
                             const float amount,
                             const double windowSeconds)
{
    const auto samples = audio.getNumSamples();
    const auto channels = audio.getNumChannels();
    if (samples < 32 || channels <= 0 || amount <= 0.0f)
        return;

    const auto window = juce::jlimit(16, samples,
        juce::roundToInt(sampleRate * windowSeconds));
    const auto hop = juce::jmax(8, window / 5);
    const auto frames = juce::jmax(2, (samples + hop - 1) / hop);
    std::vector<float> levels(static_cast<size_t>(frames), 0.0f);
    auto maximumLevel = 0.0f;

    const auto wrap = [samples] (int index) noexcept
    {
        index %= samples;
        return index < 0 ? index + samples : index;
    };
    const auto linkedPower = [&audio, channels] (const int index) noexcept
    {
        double power = 0.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto value = static_cast<double>(audio.getSample(channel, index));
            power += value * value;
        }
        return power / static_cast<double>(channels);
    };

    auto windowStart = wrap(-window / 2);
    auto energy = 0.0;
    for (int offset = 0; offset < window; ++offset)
        energy += linkedPower(wrap(windowStart + offset));

    for (int frame = 0; frame < frames; ++frame)
    {
        if (frame > 0)
        {
            const auto advance = juce::jmin(hop, samples - (frame - 1) * hop);
            for (int offset = 0; offset < advance; ++offset)
            {
                energy -= linkedPower(windowStart);
                energy += linkedPower(wrap(windowStart + window));
                windowStart = wrap(windowStart + 1);
            }
        }
        const auto level = static_cast<float>(std::sqrt(
            juce::jmax(0.0, energy) / static_cast<double>(window)));
        levels[static_cast<size_t>(frame)] = level;
        maximumLevel = juce::jmax(maximumLevel, level);
    }

    std::vector<float> activeLevels;
    activeLevels.reserve(levels.size());
    for (const auto level : levels)
        if (level >= maximumLevel * 0.025f)
            activeLevels.push_back(level);
    const auto target = juce::jmax(1.0e-7f,
        activeLevels.empty() ? median(levels) : median(activeLevels));

    std::vector<float> gains(levels.size(), 1.0f);
    for (size_t frame = 0; frame < levels.size(); ++frame)
    {
        const auto protectedLevel = juce::jmax(levels[frame], target * 0.14f);
        const auto requested = std::pow(target / protectedLevel, amount);
        gains[frame] = juce::jlimit(0.42f, 2.4f, requested);
    }

    std::vector<float> smoothed(gains.size(), 1.0f);
    for (int pass = 0; pass < 3; ++pass)
    {
        for (size_t frame = 0; frame < gains.size(); ++frame)
        {
            const auto previous = (frame + gains.size() - 1u) % gains.size();
            const auto next = (frame + 1u) % gains.size();
            smoothed[frame] = 0.25f * gains[previous]
                              + 0.50f * gains[frame]
                              + 0.25f * gains[next];
        }
        gains.swap(smoothed);
    }

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto first = juce::jmin(frames - 1, sample / hop);
        const auto second = (first + 1) % frames;
        const auto frameStart = first * hop;
        const auto span = first == frames - 1 ? samples - frameStart : hop;
        const auto fraction = static_cast<float>(sample - frameStart)
                              / static_cast<float>(juce::jmax(1, span));
        const auto gain = gains[static_cast<size_t>(first)]
                          + fraction * (gains[static_cast<size_t>(second)]
                                        - gains[static_cast<size_t>(first)]);
        for (int channel = 0; channel < channels; ++channel)
            audio.setSample(channel, sample, audio.getSample(channel, sample) * gain);
    }
}

void applyDynamicsCrush(juce::AudioBuffer<float>& audio,
                        const double sampleRate,
                        const float amount)
{
    const auto crush = juce::jlimit(0.0f, 1.0f, amount);
    if (crush <= 0.0f)
        return;

    // Two linked, circular time scales reduce short attack/release pulses without changing
    // stereo image or introducing a privileged loop boundary.
    flattenCircularEnvelope(audio, sampleRate, 0.72f * crush, 0.085);
    flattenCircularEnvelope(audio, sampleRate, 0.46f * crush, 0.310);
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
    const auto requestedOverlap = juce::jlimit(8, grainSamples / 3,
        juce::roundToInt(static_cast<float>(grainSamples)
                         * (0.12f + 0.10f * stability)));
    const auto requestedAdvance = juce::jmax(8, grainSamples - requestedOverlap);
    const auto segmentCount = juce::jmax(2,
        (targetSamples + requestedAdvance - 1) / requestedAdvance);
    std::vector<int> outputStarts(static_cast<size_t>(segmentCount));
    for (int segment = 0; segment < segmentCount; ++segment)
        outputStarts[static_cast<size_t>(segment)] = juce::roundToInt(
            static_cast<double>(segment) * static_cast<double>(targetSamples)
            / static_cast<double>(segmentCount));
    const auto maximumStart = conditioned.getNumSamples() - grainSamples;
    const auto sourceStep = juce::jmax(
        1, juce::jmax(grainSamples / 12, maximumStart / 383));
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

    result.audio.setSize(channels, targetSamples, false, true, false);
    std::vector<float> normalization(static_cast<size_t>(targetSamples), 0.0f);
    auto randomState = settings.seed == 0 ? 1u : settings.seed;
    const auto regionSpan = static_cast<float>(juce::jmax(1, maximumStart));
    const auto refractorySteps = juce::jlimit(3, 14,
        4 + juce::roundToInt(8.0f * transform));
    auto previousStart = 0;
    auto firstStart = 0;

    for (int stepIndex = 0; stepIndex < segmentCount; ++stepIndex)
    {
        const auto outputStart = outputStarts[static_cast<size_t>(stepIndex)];
        const auto previousOutput = stepIndex == 0
            ? outputStarts.back() - targetSamples
            : outputStarts[static_cast<size_t>(stepIndex - 1)];
        const auto nextOutput = stepIndex + 1 == segmentCount
            ? targetSamples : outputStarts[static_cast<size_t>(stepIndex + 1)];
        const auto previousAdvance = outputStart - previousOutput;
        const auto nextAdvance = nextOutput - outputStart;
        const auto overlapFromPrevious = grainSamples - previousAdvance;
        const auto overlapToNext = grainSamples - nextAdvance;
        auto bestRegion = 0;
        auto bestPenalty = std::numeric_limits<float>::max();
        const auto expectedStart = stepIndex == 0 ? 0
            : (previousStart + previousAdvance)
                % juce::jmax(1, maximumStart + 1);
        for (int index = 0; index < static_cast<int>(regions.size()); ++index)
        {
            auto& region = regions[static_cast<size_t>(index)];
            const auto directDistance = std::abs(region.start - expectedStart);
            const auto sourceDistance = static_cast<float>(juce::jmin(
                directDistance, juce::jmax(0, maximumStart + 1 - directDistance)))
                / regionSpan;
            const auto basePenalty = 0.34f * region.activityPenalty
                                     + 0.38f * stability * region.stationarityPenalty;
            const auto continuityPenalty = stepIndex == 0 ? 0.0f
                : alignedOverlapPenalty(conditioned, previousStart, region.start,
                                        previousAdvance, overlapFromPrevious);
            const auto closurePenalty = stepIndex + 1 == segmentCount && stepIndex > 0
                ? alignedOverlapPenalty(conditioned, region.start, firstStart,
                                        nextAdvance, overlapToNext)
                : 0.0f;
            const auto age = stepIndex - region.lastUsedStep;
            const auto reusePenalty = age < refractorySteps
                ? 3.0f * static_cast<float>(refractorySteps - age)
                    / static_cast<float>(refractorySteps)
                : 0.0f;
            const auto jitter = (randomUnit(randomState) - 0.5f)
                                * variation * 0.10f;
            const auto penalty = basePenalty
                + (0.82f + 0.24f * transform) * continuityPenalty
                + 0.88f * closurePenalty
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
        auto chosenStart = chosen.start;
        if (stepIndex > 0 && sourceStep > 1)
        {
            const auto radius = juce::jmax(1, sourceStep / 2);
            const auto refinementStep = juce::jmax(1, radius / 48);
            auto refinedPenalty = std::numeric_limits<float>::max();
            const auto firstCandidate = juce::jmax(0, chosen.start - radius);
            const auto lastCandidate = juce::jmin(maximumStart, chosen.start + radius);
            for (int candidate = firstCandidate; candidate <= lastCandidate;
                 candidate += refinementStep)
            {
                auto penalty = alignedOverlapPenalty(
                    conditioned, previousStart, candidate,
                    previousAdvance, overlapFromPrevious);
                if (stepIndex + 1 == segmentCount)
                    penalty += 0.82f * alignedOverlapPenalty(
                        conditioned, candidate, firstStart,
                        nextAdvance, overlapToNext);
                penalty += 0.04f * static_cast<float>(std::abs(candidate - chosen.start))
                           / static_cast<float>(radius);
                if (penalty < refinedPenalty)
                {
                    refinedPenalty = penalty;
                    chosenStart = candidate;
                }
            }
        }
        chosen.lastUsedStep = stepIndex;
        previousStart = chosenStart;
        if (stepIndex == 0)
            firstStart = chosenStart;
        result.analysisFrameStarts.push_back(chosenStart);
        for (int sample = 0; sample < grainSamples; ++sample)
        {
            const auto outputSample = (outputStart + sample) % targetSamples;
            auto gain = 1.0f;
            if (sample < overlapFromPrevious)
            {
                const auto phase = juce::MathConstants<float>::pi
                    * (static_cast<float>(sample) + 0.5f)
                    / static_cast<float>(overlapFromPrevious);
                gain *= 0.5f - 0.5f * std::cos(phase);
            }
            if (sample >= grainSamples - overlapToNext)
            {
                const auto offset = sample - (grainSamples - overlapToNext);
                const auto phase = juce::MathConstants<float>::pi
                    * (static_cast<float>(offset) + 0.5f)
                    / static_cast<float>(overlapToNext);
                gain *= 0.5f + 0.5f * std::cos(phase);
            }
            normalization[static_cast<size_t>(outputSample)] += gain;
            for (int channel = 0; channel < channels; ++channel)
                result.audio.addSample(channel, outputSample,
                    conditioned.getSample(channel, chosenStart + sample) * gain);
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

    applyDynamicsCrush(result.audio, sampleRate, settings.dynamicsCrush);
    TextureCharacterProcessor::apply(result.audio, sampleRate,
                                     settings.character, settings.characterAmount,
                                     settings.seed ^ 0xa53c9e1du);
    balanceChannelEnergy(result.audio, 0.45f * stability * transform);
    const auto sourceRms = SignalDiagnostics::calculateRms(source);
    const auto outputRms = SignalDiagnostics::calculateRms(result.audio);
    if (sourceRms > 1.0e-7f && outputRms > 1.0e-7f)
        result.audio.applyGain(juce::jlimit(0.5f, 2.0f, sourceRms / outputRms));

    result.usedStructure = settings.structure;
    result.usedCharacter = settings.character;
    result.containsOnlyFiniteSamples = SignalDiagnostics::repairNonFiniteAndRemoveDc(result.audio);
    result.truePeakDbtp = SignalDiagnostics::applyCircularTruePeakCeiling(
        result.audio, -1.0f);
    return result;
}
