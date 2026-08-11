#include "TextureSynthesizer.h"

#include "RenderQuality.h"
#include "TextureMaterialModel.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace
{
constexpr int descriptorBands = 8;

struct FrameDescriptor
{
    std::array<float, descriptorBands> bands {};
    float rms = 0.0f;
    float derivative = 0.0f;
};

float readMono(const juce::AudioBuffer<float>& audio, const int sample) noexcept
{
    auto value = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        value += audio.getSample(channel, sample);
    return value / static_cast<float>(juce::jmax(1, audio.getNumChannels()));
}

FrameDescriptor analyseFrame(const juce::AudioBuffer<float>& audio,
                             const int start,
                             const int count)
{
    FrameDescriptor result;
    if (count < 8 || audio.getNumSamples() == 0)
        return result;
    auto energy = 0.0;
    auto differenceEnergy = 0.0;
    auto previous = readMono(audio, juce::jlimit(0, audio.getNumSamples() - 1, start));
    for (int offset = 0; offset < count; ++offset)
    {
        const auto position = juce::jlimit(0, audio.getNumSamples() - 1, start + offset);
        const auto value = readMono(audio, position);
        energy += static_cast<double>(value) * value;
        const auto difference = value - previous;
        differenceEnergy += static_cast<double>(difference) * difference;
        previous = value;
    }
    result.rms = std::sqrt(static_cast<float>(energy / static_cast<double>(count)));
    result.derivative = std::sqrt(
        static_cast<float>(differenceEnergy / static_cast<double>(count)));

    constexpr int transformSize = 128;
    for (int band = 0; band < descriptorBands; ++band)
    {
        const auto bin = 1 + band * (transformSize / 2 - 2) / (descriptorBands - 1);
        auto real = 0.0;
        auto imaginary = 0.0;
        for (int index = 0; index < transformSize; ++index)
        {
            const auto sourceOffset = index * juce::jmax(1, count - 1)
                                      / (transformSize - 1);
            const auto position = juce::jlimit(
                0, audio.getNumSamples() - 1, start + sourceOffset);
            const auto window = 0.5 - 0.5 * std::cos(
                juce::MathConstants<double>::twoPi * index
                / static_cast<double>(transformSize - 1));
            const auto phase = juce::MathConstants<double>::twoPi * bin * index
                               / static_cast<double>(transformSize);
            const auto sample = readMono(audio, position) * window;
            real += sample * std::cos(phase);
            imaginary -= sample * std::sin(phase);
        }
        result.bands[static_cast<size_t>(band)] = std::log1p(
            static_cast<float>(std::hypot(real, imaginary)));
    }
    const auto bandTotal = std::accumulate(
        result.bands.begin(), result.bands.end(), 1.0e-8f);
    for (auto& band : result.bands)
        band /= bandTotal;
    return result;
}

std::vector<FrameDescriptor> descriptorSequence(const juce::AudioBuffer<float>& audio,
                                                const double sampleRate)
{
    std::vector<FrameDescriptor> result;
    const auto frameSamples = juce::jlimit(
        64, juce::jmax(64, audio.getNumSamples() / 3),
        juce::roundToInt(sampleRate * 0.050));
    const auto hop = juce::jmax(16, frameSamples / 2);
    for (int start = 0; start + frameSamples <= audio.getNumSamples(); start += hop)
        result.push_back(analyseFrame(audio, start, frameSamples));
    return result;
}

float descriptorDistance(const FrameDescriptor& left,
                         const FrameDescriptor& right)
{
    auto spectrum = 0.0f;
    for (int band = 0; band < descriptorBands; ++band)
        spectrum += std::abs(left.bands[static_cast<size_t>(band)]
                             - right.bands[static_cast<size_t>(band)]);
    const auto level = std::abs(left.rms - right.rms)
                       / juce::jmax(1.0e-6f, left.rms + right.rms);
    const auto derivative = std::abs(left.derivative - right.derivative)
                            / juce::jmax(1.0e-6f,
                                left.derivative + right.derivative);
    return 0.58f * spectrum + 0.24f * level + 0.18f * derivative;
}

float calculateClosureQuality(const juce::AudioBuffer<float>& audio,
                              const double sampleRate)
{
    if (audio.getNumSamples() < 8)
        return 0.0f;
    const auto window = juce::jlimit(
        8, audio.getNumSamples() / 2,
        juce::roundToInt(sampleRate * 0.035));
    const auto head = analyseFrame(audio, 0, window);
    const auto tail = analyseFrame(audio, audio.getNumSamples() - window, window);
    const auto featureScore = 100.0f * std::exp(-2.8f * descriptorDistance(head, tail));
    auto jump = 0.0f;
    auto slopeJump = 0.0f;
    auto differenceEnergy = 0.0;
    auto curvatureEnergy = 0.0;
    auto differenceCount = 0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto last = audio.getSample(channel, audio.getNumSamples() - 1);
        const auto first = audio.getSample(channel, 0);
        const auto previous = audio.getSample(channel, audio.getNumSamples() - 2);
        const auto second = audio.getSample(channel, 1);
        jump += std::abs(first - last);
        slopeJump += std::abs((second - first) - (last - previous));
        auto older = audio.getSample(channel, 0);
        auto oldDifference = 0.0f;
        for (int sample = 1; sample < audio.getNumSamples(); ++sample)
        {
            const auto current = audio.getSample(channel, sample);
            const auto difference = current - older;
            differenceEnergy += static_cast<double>(difference) * difference;
            if (sample > 1)
            {
                const auto curvature = difference - oldDifference;
                curvatureEnergy += static_cast<double>(curvature) * curvature;
            }
            oldDifference = difference;
            older = current;
            ++differenceCount;
        }
    }
    jump /= static_cast<float>(juce::jmax(1, audio.getNumChannels()));
    slopeJump /= static_cast<float>(juce::jmax(1, audio.getNumChannels()));
    const auto typicalJump = std::sqrt(static_cast<float>(differenceEnergy
        / static_cast<double>(juce::jmax(1, differenceCount))));
    const auto typicalSlopeJump = std::sqrt(static_cast<float>(curvatureEnergy
        / static_cast<double>(juce::jmax(1, differenceCount
            - audio.getNumChannels()))));
    const auto abnormalJump = juce::jmax(
        0.0f, jump / juce::jmax(1.0e-7f, 3.0f * typicalJump) - 1.0f);
    const auto abnormalSlope = juce::jmax(
        0.0f, slopeJump / juce::jmax(1.0e-7f, 3.0f * typicalSlopeJump) - 1.0f);
    const auto sampleScore = 100.0f * std::exp(
        -1.8f * abnormalJump - 0.9f * abnormalSlope);
    // An evolving loop may legitimately enter and leave the seam with different local spectra.
    // Sample and slope continuity dominate; the short-window feature match is a secondary guard.
    return juce::jlimit(0.0f, 100.0f, 0.78f * sampleScore + 0.22f * featureScore);
}

float calculateMacroStability(const std::vector<FrameDescriptor>& frames)
{
    if (frames.empty())
        return 0.0f;
    auto mean = 0.0;
    auto square = 0.0;
    for (const auto& frame : frames)
    {
        mean += frame.rms;
        square += static_cast<double>(frame.rms) * frame.rms;
    }
    mean /= static_cast<double>(frames.size());
    const auto variance = juce::jmax(
        0.0, square / static_cast<double>(frames.size()) - mean * mean);
    const auto coefficient = std::sqrt(variance) / juce::jmax(1.0e-8, mean);
    return 100.0f * std::exp(-1.75f * static_cast<float>(coefficient));
}

float calculateDiversity(const std::vector<FrameDescriptor>& frames)
{
    if (frames.size() < 2u)
        return 0.0f;
    auto total = 0.0f;
    for (size_t index = 1; index < frames.size(); ++index)
        total += descriptorDistance(frames[index - 1u], frames[index]);
    const auto mean = total / static_cast<float>(frames.size() - 1u);
    return 100.0f * (1.0f - std::exp(-4.0f * mean));
}

float calculateRepeatSafety(const std::vector<FrameDescriptor>& frames)
{
    if (frames.size() < 12u)
        return 100.0f;
    const auto maximumLag = static_cast<int>(frames.size()) / 2;
    std::vector<float> lagSimilarity;
    lagSimilarity.reserve(static_cast<size_t>(maximumLag - 2));
    for (int lag = 3; lag <= maximumLag; ++lag)
    {
        auto similarity = 0.0f;
        auto count = 0;
        for (int index = 0; index + lag < static_cast<int>(frames.size()); ++index)
        {
            similarity += std::exp(-5.0f * descriptorDistance(
                frames[static_cast<size_t>(index)],
                frames[static_cast<size_t>(index + lag)]));
            ++count;
        }
        if (count > 0)
            lagSimilarity.push_back(similarity / static_cast<float>(count));
    }
    if (lagSimilarity.empty())
        return 100.0f;
    const auto baseline = std::accumulate(
        lagSimilarity.begin(), lagSimilarity.end(), 0.0f)
        / static_cast<float>(lagSimilarity.size());
    const auto strongest = *std::max_element(
        lagSimilarity.begin(), lagSimilarity.end());
    // A stable texture is similar at every lag and is not a repeating event. Recurrence is the
    // excess similarity of one particular lag above the all-lag baseline.
    const auto recurrence = juce::jlimit(0.0f, 1.0f,
        (strongest - baseline) / juce::jmax(0.08f, 1.0f - baseline));
    return 100.0f * (1.0f - juce::jlimit(0.0f, 1.0f, recurrence));
}

float calculateSpectrumPreservation(const RenderQuality::SignalSnapshot& snapshot)
{
    if (!snapshot.valid)
        return 0.0f;
    auto distance = 0.0f;
    for (size_t band = 0; band < snapshot.sourceSpectrum.size(); ++band)
        distance += std::abs(snapshot.sourceSpectrum[band]
                             - snapshot.outputSpectrum[band]);
    distance /= static_cast<float>(snapshot.sourceSpectrum.size());
    return 100.0f * std::exp(-3.2f * distance);
}

float calculateSpectralFlatness(const juce::AudioBuffer<float>& audio)
{
    if (audio.getNumSamples() < 128)
        return 0.0f;
    constexpr int order = 9;
    constexpr int fftSize = 1 << order;
    constexpr int bins = fftSize / 2 + 1;
    juce::dsp::FFT fft(order);
    const auto frames = 8;
    auto flatness = 0.0;
    for (int frame = 0; frame < frames; ++frame)
    {
        std::vector<float> data(static_cast<size_t>(2 * fftSize), 0.0f);
        const auto maximumStart = juce::jmax(0, audio.getNumSamples() - fftSize);
        const auto start = frame * maximumStart / juce::jmax(1, frames - 1);
        for (int sample = 0; sample < fftSize; ++sample)
        {
            const auto window = 0.5f - 0.5f * std::cos(
                juce::MathConstants<float>::twoPi * static_cast<float>(sample)
                / static_cast<float>(fftSize - 1));
            const auto position = juce::jmin(audio.getNumSamples() - 1, start + sample);
            data[static_cast<size_t>(sample)] = readMono(audio, position) * window;
        }
        fft.performRealOnlyForwardTransform(data.data(), true);
        auto logTotal = 0.0;
        auto linearTotal = 0.0;
        for (int bin = 1; bin < bins; ++bin)
        {
            const auto magnitude = juce::jmax(1.0e-12f,
                std::hypot(data[static_cast<size_t>(2 * bin)],
                           data[static_cast<size_t>(2 * bin + 1)]));
            logTotal += std::log(magnitude);
            linearTotal += magnitude;
        }
        const auto geometric = std::exp(logTotal / static_cast<double>(bins - 1));
        const auto arithmetic = linearTotal / static_cast<double>(bins - 1);
        flatness += geometric / juce::jmax(1.0e-12, arithmetic);
    }
    return static_cast<float>(flatness / static_cast<double>(frames));
}

float calculateNearestSpectralIdentity(const juce::AudioBuffer<float>& source,
                                       const juce::AudioBuffer<float>& output,
                                       const double sampleRate)
{
    const auto sourceFrames = descriptorSequence(source, sampleRate);
    const auto outputFrames = descriptorSequence(output, sampleRate);
    if (sourceFrames.empty() || outputFrames.empty())
        return 0.0f;
    const auto stride = juce::jmax<size_t>(1u, outputFrames.size() / 48u);
    auto total = 0.0f;
    auto compared = 0;
    for (size_t outputIndex = 0; outputIndex < outputFrames.size(); outputIndex += stride)
    {
        auto nearest = 1.0e9f;
        for (const auto& sourceFrame : sourceFrames)
            nearest = juce::jmin(nearest,
                descriptorDistance(sourceFrame, outputFrames[outputIndex]));
        total += std::exp(-3.4f * nearest);
        ++compared;
    }
    return 100.0f * total / static_cast<float>(juce::jmax(1, compared));
}
}

TextureSynthesisResult TextureSynthesizer::synthesize(
    const juce::AudioBuffer<float>& source,
    const double sampleRate,
    TextureSynthesisSettings settings)
{
    TextureSynthesisResult result;
    if (source.getNumChannels() == 0 || source.getNumSamples() < 64 || sampleRate <= 0.0)
        return result;

    settings.durationSeconds = juce::jlimit(0.25f, 60.0f, settings.durationSeconds);
    settings.variation = juce::jlimit(0.0f, 1.0f, settings.variation);
    settings.flatten = juce::jlimit(0.0f, 1.0f, settings.flatten);
    settings.sourceMatch = juce::jlimit(0.0f, 1.0f, settings.sourceMatch);
    const auto targetSamples = juce::jmax(
        64, juce::roundToInt(sampleRate * settings.durationSeconds));

    auto rendered = TextureMaterialModel::render(
        source, sampleRate, targetSamples, settings);
    result.audio = std::move(rendered.audio);
    result.analysisFrameStarts = std::move(rendered.analysisFrameStarts);
    if (result.audio.getNumSamples() == 0)
        return result;

    result.containsOnlyFiniteSamples = RenderQuality::repairNonFiniteAndRemoveDc(result.audio);
    const auto sourceLoudness = RenderQuality::estimateIntegratedLoudnessDb(source, sampleRate);
    const auto outputLoudness = RenderQuality::estimateIntegratedLoudnessDb(result.audio, sampleRate);
    if (sourceLoudness > -90.0f && outputLoudness > -90.0f)
        result.audio.applyGain(juce::Decibels::decibelsToGain(
            juce::jlimit(-9.0f, 9.0f, sourceLoudness - outputLoudness)));
    result.truePeakDbtp = RenderQuality::applyCircularTruePeakCeiling(result.audio, -1.0f);

    const auto snapshot = RenderQuality::analyseSourceAndOutput(source, result.audio, sampleRate);
    const auto outputDescriptors = descriptorSequence(result.audio, sampleRate);
    result.closureQuality = calculateClosureQuality(result.audio, sampleRate);
    result.transitionQuality = calculateMacroStability(outputDescriptors);
    result.macroStability = result.transitionQuality;
    result.diversity = calculateDiversity(outputDescriptors);
    result.repeatSafety = calculateRepeatSafety(outputDescriptors);
    result.spectrumPreservation = calculateSpectrumPreservation(snapshot);
    result.localFrameIdentity = calculateNearestSpectralIdentity(source, result.audio, sampleRate);
    result.materialIdentity = 0.58f * result.spectrumPreservation
                              + 0.42f * result.localFrameIdentity;
    result.sourceSpectralFlatness = calculateSpectralFlatness(source);
    result.outputSpectralFlatness = calculateSpectralFlatness(result.audio);
    const auto excessFlatness = juce::jmax(
        0.0f, result.outputSpectralFlatness - result.sourceSpectralFlatness - 0.035f);
    result.noiseCollapseSafety = 100.0f * std::exp(-5.5f * excessFlatness);
    result.stereoPreservation = snapshot.valid ? 100.0f * std::exp(
        -1.8f * std::abs(snapshot.outputCorrelation - snapshot.sourceCorrelation)) : 0.0f;
    result.phasePreservation = result.stereoPreservation;
    result.positionPreservation = snapshot.valid ? 100.0f * std::exp(
        -0.20f * std::abs(snapshot.outputImbalanceDb - snapshot.sourceImbalanceDb)) : 0.0f;
    result.loudnessPreservation = 100.0f * std::exp(-0.16f * std::abs(
        RenderQuality::estimateIntegratedLoudnessDb(result.audio, sampleRate)
        - sourceLoudness));
    result.transientPreservation = 100.0f - settings.flatten * 75.0f;
    result.usedStructure = settings.structure;
    result.structureConfidence = 100.0f;

    result.qualityScore = 0.16f * result.closureQuality
                          + 0.17f * result.repeatSafety
                          + 0.15f * result.materialIdentity
                          + 0.13f * result.noiseCollapseSafety
                          + 0.12f * result.macroStability
                          + 0.10f * result.loudnessPreservation
                          + 0.09f * result.phasePreservation
                          + 0.08f * result.positionPreservation;
    result.passedQualityGate = result.containsOnlyFiniteSamples
        && result.truePeakDbtp <= -0.85f
        && result.closureQuality >= 48.0f
        && result.repeatSafety >= 45.0f
        && result.materialIdentity >= 28.0f
        && result.noiseCollapseSafety >= 55.0f
        && result.macroStability >= 45.0f;
    return result;
}
