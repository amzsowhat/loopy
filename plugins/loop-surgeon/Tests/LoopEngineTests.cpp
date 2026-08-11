#include "LoopAnalyzer.h"
#include "LoopEngine.h"
#include "RenderQuality.h"
#include "TextureSynthesizer.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
bool expect(const bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

bool waitForReady(LoopEngine& engine)
{
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        if (engine.getState() == LoopEngine::State::ready)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

std::vector<float> blockRms(const juce::AudioBuffer<float>& audio,
                            const int blockSamples)
{
    std::vector<float> result;
    for (int start = 0; start + blockSamples <= audio.getNumSamples();
         start += blockSamples)
    {
        auto energy = 0.0;
        auto count = 0;
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < blockSamples; ++sample)
            {
                const auto value = audio.getSample(channel, start + sample);
                energy += static_cast<double>(value) * value;
                ++count;
            }
        result.push_back(std::sqrt(static_cast<float>(
            energy / static_cast<double>(juce::jmax(1, count)))));
    }
    return result;
}

float coefficientOfVariation(const std::vector<float>& values)
{
    if (values.empty())
        return 0.0f;
    auto total = 0.0;
    auto square = 0.0;
    for (const auto value : values)
    {
        total += value;
        square += static_cast<double>(value) * value;
    }
    const auto mean = total / static_cast<double>(values.size());
    const auto variance = juce::jmax(
        0.0, square / static_cast<double>(values.size()) - mean * mean);
    return static_cast<float>(std::sqrt(variance) / juce::jmax(1.0e-8, mean));
}

juce::AudioBuffer<float> makeAnalysisProbe(const int channels,
                                           const int samples,
                                           const double sampleRate,
                                           const uint32_t seed,
                                           const float coherentAmount,
                                           const float irregularAmount,
                                           const float trajectoryAmount,
                                           const float stereoWidth)
{
    juce::AudioBuffer<float> result(channels, samples);
    result.clear();
    auto state = seed == 0u ? 1u : seed;
    auto colouredA = 0.0f;
    auto colouredB = 0.0f;
    auto phaseA = 0.0;
    auto phaseB = 0.0;
    for (int sample = 0; sample < samples; ++sample)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        const auto random = static_cast<float>(state & 0xffffu) / 32768.0f - 1.0f;
        const auto progress = static_cast<float>(sample)
                              / static_cast<float>(juce::jmax(1, samples - 1));
        const auto trajectory = 1.0f + trajectoryAmount
            * (0.62f * std::sin(juce::MathConstants<float>::pi * progress)
               + 0.28f * std::sin(juce::MathConstants<float>::twoPi
                                  * 1.37f * progress + 0.3f));
        const auto frequencyA = 53.0f * trajectory;
        const auto frequencyB = 137.0f / juce::jmax(0.35f, trajectory);
        phaseA += juce::MathConstants<double>::twoPi * frequencyA / sampleRate;
        phaseB += juce::MathConstants<double>::twoPi * frequencyB / sampleRate;
        colouredA = 0.91f * colouredA + 0.09f * random;
        colouredB = 0.72f * colouredB + 0.28f * (0.63f * random
            + 0.37f * std::sin(0.017f * static_cast<float>(sample)));
        const auto sparseGate = 0.35f + 0.65f * std::pow(
            std::abs(std::sin(0.0037f * static_cast<float>(sample)
                              + 0.7f * std::sin(0.0011f * sample))), 3.0f);
        const auto coherent = 0.67f * static_cast<float>(std::sin(phaseA))
                              + 0.29f * static_cast<float>(std::sin(phaseB + 0.4));
        const auto irregular = (0.78f * colouredA + 0.22f * colouredB) * sparseGate;
        const auto macro = 0.16f + 0.84f * std::pow(
            juce::jmax(0.0f, std::sin(juce::MathConstants<float>::pi * progress)), 0.7f);
        const auto mid = macro * (coherentAmount * coherent
                                  + irregularAmount * irregular);
        const auto side = stereoWidth * macro
            * (0.55f * irregular - 0.18f * coherent
               + 0.08f * std::sin(0.029f * static_cast<float>(sample)));
        if (channels == 1)
            result.setSample(0, sample, mid);
        else
        {
            result.setSample(0, sample, 0.70710678f * (mid + side));
            result.setSample(1, sample, 0.70710678f * (mid - side));
        }
    }
    return result;
}

float normalizedCorrelation(const juce::AudioBuffer<float>& left,
                            const int leftStart,
                            const juce::AudioBuffer<float>& right,
                            const int rightStart,
                            const int samples)
{
    auto dot = 0.0;
    auto leftEnergy = 0.0;
    auto rightEnergy = 0.0;
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto leftValue = left.getSample(0, leftStart + sample);
        const auto rightValue = right.getSample(0, rightStart + sample);
        dot += static_cast<double>(leftValue) * rightValue;
        leftEnergy += static_cast<double>(leftValue) * leftValue;
        rightEnergy += static_cast<double>(rightValue) * rightValue;
    }
    return static_cast<float>(std::abs(dot)
        / std::sqrt(juce::jmax(1.0e-12, leftEnergy * rightEnergy)));
}

float strongestSourceWindowCopy(const juce::AudioBuffer<float>& source,
                                const juce::AudioBuffer<float>& output)
{
    const auto window = juce::jlimit(96, source.getNumSamples(),
        juce::jmin(source.getNumSamples() / 2, 640));
    auto strongest = 0.0f;
    for (int sourceStart = 0; sourceStart + window <= source.getNumSamples();
         sourceStart += juce::jmax(1, window / 4))
        for (int outputStart = 0; outputStart + window <= output.getNumSamples();
             outputStart += juce::jmax(1, window / 3))
            strongest = juce::jmax(strongest, normalizedCorrelation(
                source, sourceStart, output, outputStart, window));
    return strongest;
}

float recurrenceAtLag(const juce::AudioBuffer<float>& audio,
                      const int lag,
                      const int block)
{
    if (lag <= 0 || lag + block > audio.getNumSamples())
        return 0.0f;
    auto total = 0.0f;
    auto count = 0;
    for (int start = 0; start + lag + block <= audio.getNumSamples(); start += block)
    {
        total += normalizedCorrelation(audio, start, audio, start + lag, block);
        ++count;
    }
    return total / static_cast<float>(juce::jmax(1, count));
}
}

int main()
{
    bool passed = true;
    constexpr auto sampleRate = 4000.0;

    // R&R remains an independent utility path and must preserve exact requested duration.
    constexpr auto period = 1200;
    juce::AudioBuffer<float> repeated(2, period * 6);
    for (int sample = 0; sample < repeated.getNumSamples(); ++sample)
    {
        const auto phase = juce::MathConstants<float>::twoPi
                           * static_cast<float>(sample % period)
                           / static_cast<float>(period);
        repeated.setSample(0, sample, 0.55f * std::sin(7.0f * phase)
                                          + 0.18f * std::sin(19.0f * phase));
        repeated.setSample(1, sample, 0.48f * std::sin(7.0f * phase + 0.08f)
                                          + 0.20f * std::sin(19.0f * phase - 0.11f));
    }
    const auto automatic = LoopAnalyzer::analyzeSource(
        repeated, sampleRate, 800, 1600, 3, 80);
    passed &= expect(!automatic.candidates.empty(), "R&R analysis should find candidates");
    const auto exact = LoopAnalyzer::analyzeRotateRepairExact(
        repeated, sampleRate, 4000, 3, 160);
    passed &= expect(!exact.candidates.empty(), "R&R should find an exact-duration repair");
    if (!exact.candidates.empty())
        passed &= expect(LoopAnalyzer::renderRotateRepair(
            repeated, exact.candidates.front()).getNumSamples() == 4000,
            "R&R final length must be sample-exact");

    // The texture corpus spans orthogonal signal properties. No case selects a different engine;
    // all settings use the same source-agnostic analysis and resynthesis contract.
    struct ProbeParameters
    {
        float coherent;
        float irregular;
        float trajectory;
        float width;
        uint32_t seed;
    };
    const ProbeParameters probes[] {
        { 0.85f, 0.15f, 0.10f, 0.10f, 0x10203u },
        { 0.55f, 0.45f, 0.55f, 0.75f, 0x20304u },
        { 0.20f, 0.80f, 0.25f, 0.42f, 0x30405u },
        { 0.48f, 0.52f, 0.90f, 0.95f, 0x40506u }
    };

    for (const auto& probe : probes)
    {
        const auto source = makeAnalysisProbe(
            2, 3600, sampleRate, probe.seed, probe.coherent,
            probe.irregular, probe.trajectory, probe.width);
        TextureSynthesisSettings settings;
        settings.durationSeconds = 6.0f;
        settings.variation = 0.73f;
        settings.flatten = 0.78f;
        settings.sourceMatch = 0.82f;
        settings.structure = TextureStructure::automatic;
        settings.seed = probe.seed ^ 0x5a17b33fu;
        const auto first = TextureSynthesizer::synthesize(source, sampleRate, settings);
        const auto repeatedRender = TextureSynthesizer::synthesize(
            source, sampleRate, settings);
        passed &= expect(first.audio.getNumSamples() == 24000,
                         "texture output length should match the requested loop");
        passed &= expect(first.analysisFrameStarts.size() >= 2u,
                         "texture must analyse multiple source states");
        passed &= expect(first.containsOnlyFiniteSamples,
                         "texture output must contain only finite samples");
        passed &= expect(first.truePeakDbtp <= -0.85f,
                         "texture output must keep true-peak headroom");
        if (first.closureQuality < 45.0f)
            std::cerr << "closure probe=" << probe.seed
                      << " score=" << first.closureQuality << '\n';
        passed &= expect(first.closureQuality >= 45.0f,
                         "generated state trajectory must close as a loop");
        passed &= expect(first.repeatSafety >= 40.0f,
                         "generated output must pass the recurrence guard");
        passed &= expect(first.materialIdentity >= 25.0f,
                         "resynthesis must retain a measurable material fingerprint");
        passed &= expect(first.noiseCollapseSafety >= 50.0f,
                         "resynthesis must not flatten into generic noise");
        auto deterministicError = 0.0;
        for (int sample = 0; sample < first.audio.getNumSamples(); ++sample)
            deterministicError += std::abs(first.audio.getSample(0, sample)
                - repeatedRender.audio.getSample(0, sample));
        passed &= expect(deterministicError < 1.0e-6,
                         "same seed must be sample-exact for DAW recall");
        passed &= expect(strongestSourceWindowCopy(source, first.audio) < 0.995f,
                         "no source window may be copied onto the output timeline");
        passed &= expect(recurrenceAtLag(
            first.audio, source.getNumSamples(), 480) < 0.82f,
            "output must not recur at the complete source duration");
    }

    const auto styleSource = makeAnalysisProbe(
        2, 4200, sampleRate, 0x9911u, 0.58f, 0.42f, 0.64f, 0.68f);
    TextureSynthesisSettings styleSettings;
    styleSettings.durationSeconds = 5.0f;
    styleSettings.seed = 0x13579u;
    std::vector<juce::AudioBuffer<float>> styles;
    for (const auto style : { TextureStructure::automatic,
                              TextureStructure::continuous,
                              TextureStructure::particles })
    {
        styleSettings.structure = style;
        const auto rendered = TextureSynthesizer::synthesize(
            styleSource, sampleRate, styleSettings);
        styles.push_back(rendered.audio);
        passed &= expect(rendered.usedStructure == style,
                         "style selection must be explicit and stable");
    }
    for (size_t style = 1; style < styles.size(); ++style)
    {
        auto difference = 0.0;
        for (int sample = 0; sample < styles[style].getNumSamples(); ++sample)
            difference += std::abs(styles[0].getSample(0, sample)
                                   - styles[style].getSample(0, sample));
        passed &= expect(difference > 5.0,
                         "each style must create a materially different algorithmic result");
    }

    TextureSynthesisSettings stableSettings;
    stableSettings.durationSeconds = 6.0f;
    stableSettings.flatten = 0.94f;
    const auto stable = TextureSynthesizer::synthesize(
        styleSource, sampleRate, stableSettings);
    auto movingSettings = stableSettings;
    movingSettings.flatten = 0.05f;
    const auto moving = TextureSynthesizer::synthesize(
        styleSource, sampleRate, movingSettings);
    passed &= expect(coefficientOfVariation(blockRms(stable.audio, 400))
                         < coefficientOfVariation(blockRms(moving.audio, 400)),
                     "Stability must control macro movement without changing engines");

    LoopEngine textureEngine;
    textureEngine.prepare(sampleRate, 64, 2);
    textureEngine.setGenerationMode(LoopEngine::GenerationMode::textureLoop);
    textureEngine.setTextureDurationSeconds(5.0f);
    textureEngine.setTextureVariation(0.74f);
    textureEngine.setTextureFlatten(0.80f);
    textureEngine.setTextureSourceMatch(0.86f);
    textureEngine.setTextureStructure(TextureStructure::automatic);
    textureEngine.submitSource(styleSource, "analysis-probe.wav");
    passed &= expect(textureEngine.reanalyzeSourceRange(0.0f, 1.0f),
                     "Generate should start Texture from Source In/Out");
    passed &= expect(waitForReady(textureEngine),
                     "offline texture generation should complete");
    passed &= expect(textureEngine.getCandidateCount() == 2,
                     "texture generation should retain two seeds for exploration");
    const auto textureLoop = textureEngine.createRenderedLoop();
    passed &= expect(textureLoop.getNumSamples() == 20000,
                     "engine should publish the exact texture duration");
    textureEngine.setPreviewMode(LoopEngine::PreviewMode::loop);
    textureEngine.setPreviewPlaying(true);
    juce::AudioBuffer<float> preview(2, 64);
    preview.clear();
    textureEngine.process(preview, 1.0f);
    passed &= expect(preview.getMagnitude(0, 0, 64) > 0.0f,
                     "Generated audition should produce audio immediately");

    LoopEngine rangeEngine;
    rangeEngine.prepare(sampleRate, 64, 2);
    rangeEngine.setGenerationMode(LoopEngine::GenerationMode::rotateRepair);
    rangeEngine.submitSource(repeated, "periodic-probe.wav");
    passed &= expect(rangeEngine.reanalyzeSourceRange(0.1f, 0.9f),
                     "R&R should accept a user Source In/Out range");
    passed &= expect(waitForReady(rangeEngine),
                     "R&R range analysis should complete");
    passed &= expect(rangeEngine.getCandidateCount() > 0,
                     "R&R should expose candidate seams");
    passed &= expect(rangeEngine.setManualRotationPoint(0.4f),
                     "R&R should accept a manual loop start");
    const auto state = rangeEngine.createLoopState();
    LoopEngine restored;
    restored.prepare(sampleRate, 64, 2);
    passed &= expect(restored.restoreLoopState(state.getData(), state.getSize()),
                     "saved DAW state should restore source and output");
    passed &= expect(restored.getSignalSnapshot().valid,
                     "restored output should include signal analysis");

    if (!passed)
        return 1;
    std::cout << "Loop Surgeon tests passed\n";
    return 0;
}
