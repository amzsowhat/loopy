#include "LoopAnalyzer.h"
#include "LoopEngine.h"
#include "TextureSynthesizer.h"

#include <chrono>
#include <array>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
bool expect(const bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool waitForReady(LoopEngine& engine)
{
    for (int attempt = 0; attempt < 500; ++attempt)
    {
        if (engine.getState() == LoopEngine::State::ready)
            return true;
        if (engine.getState() == LoopEngine::State::failed)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

double localLevelVariation(const juce::AudioBuffer<float>& audio,
                           const int window,
                           const int hop)
{
    std::vector<double> levels;
    for (int start = 0; start + window <= audio.getNumSamples(); start += hop)
    {
        double energy = 0.0;
        auto count = 0;
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < window; ++sample)
            {
                const auto value = static_cast<double>(audio.getSample(channel, start + sample));
                energy += value * value;
                ++count;
            }
        levels.push_back(std::sqrt(energy / static_cast<double>(juce::jmax(1, count))));
    }
    if (levels.empty())
        return 0.0;
    auto mean = 0.0;
    for (const auto level : levels)
        mean += level;
    mean /= static_cast<double>(levels.size());
    auto variance = 0.0;
    for (const auto level : levels)
        variance += (level - mean) * (level - mean);
    return std::sqrt(variance / static_cast<double>(levels.size()))
           / juce::jmax(1.0e-9, mean);
}

bool buffersEqual(const juce::AudioBuffer<float>& first,
                  const juce::AudioBuffer<float>& second)
{
    if (first.getNumChannels() != second.getNumChannels()
        || first.getNumSamples() != second.getNumSamples())
        return false;
    for (int channel = 0; channel < first.getNumChannels(); ++channel)
        for (int sample = 0; sample < first.getNumSamples(); ++sample)
            if (first.getSample(channel, sample) != second.getSample(channel, sample))
                return false;
    return true;
}

double averageDifference(const juce::AudioBuffer<float>& first,
                         const juce::AudioBuffer<float>& second)
{
    if (first.getNumChannels() != second.getNumChannels()
        || first.getNumSamples() != second.getNumSamples())
        return 0.0;
    double difference = 0.0;
    auto count = 0;
    for (int channel = 0; channel < first.getNumChannels(); ++channel)
        for (int sample = 0; sample < first.getNumSamples(); ++sample)
        {
            difference += std::abs(static_cast<double>(first.getSample(channel, sample)
                                                       - second.getSample(channel, sample)));
            ++count;
        }
    return difference / static_cast<double>(juce::jmax(1, count));
}
}

int main()
{
    bool passed = true;
    constexpr auto sampleRate = 4000.0;
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
    passed &= expect(!automatic.candidates.empty(),
                     "R&R analysis should find candidates");

    const auto exact = LoopAnalyzer::analyzeRotateRepairExact(
        repeated, sampleRate, 4000, 3, 160);
    passed &= expect(!exact.candidates.empty(),
                     "R&R should find an exact-duration repair");
    if (!exact.candidates.empty())
        passed &= expect(LoopAnalyzer::renderRotateRepair(
            repeated, exact.candidates.front()).getNumSamples() == 4000,
            "R&R final length must be sample-exact");

    LoopEngine engine;
    engine.prepare(sampleRate, 64, 2);
    engine.setGenerationMode(LoopEngine::GenerationMode::rotateRepair);
    engine.submitSource(repeated, "periodic-probe.wav");
    passed &= expect(engine.reanalyzeSourceRange(0.1f, 0.9f),
                     "R&R should accept Source In/Out");
    passed &= expect(waitForReady(engine),
                     "R&R analysis should complete");
    passed &= expect(engine.getCandidateCount() > 0,
                     "R&R should expose seam candidates");
    passed &= expect(engine.setManualRotationPoint(0.4f),
                     "R&R should accept a manual loop start");

    const auto state = engine.createLoopState();
    LoopEngine restored;
    restored.prepare(sampleRate, 64, 2);
    passed &= expect(restored.restoreLoopState(state.getData(), state.getSize()),
                     "saved DAW state should restore source and output");
    passed &= expect(restored.getSignalSnapshot().valid,
                     "restored output should include signal diagnostics");

    TextureSynthesisSettings textureSettings;
    textureSettings.durationSeconds = 4.0f;
    textureSettings.variation = 0.61f;
    textureSettings.flatten = 0.74f;
    textureSettings.sourceMatch = 0.82f;
    textureSettings.structure = TextureStructure::automatic;
    textureSettings.seed = 0x12345678u;
    const auto textureA = TextureSynthesizer::synthesize(
        repeated, sampleRate, textureSettings);
    const auto textureB = TextureSynthesizer::synthesize(
        repeated, sampleRate, textureSettings);
    passed &= expect(textureA.audio.getNumSamples() == 16000,
                     "Texture output length must be sample-exact");
    passed &= expect(textureA.containsOnlyFiniteSamples,
                     "Texture output must contain finite samples");
    passed &= expect(textureA.analysisFrameStarts.size() > 2u,
                     "Texture construction should traverse multiple source regions");
    auto deterministic = textureA.audio.getNumChannels() == textureB.audio.getNumChannels()
                         && textureA.audio.getNumSamples() == textureB.audio.getNumSamples();
    for (int channel = 0; deterministic && channel < textureA.audio.getNumChannels(); ++channel)
        for (int sample = 0; deterministic && sample < textureA.audio.getNumSamples(); ++sample)
            deterministic = textureA.audio.getSample(channel, sample)
                            == textureB.audio.getSample(channel, sample);
    passed &= expect(deterministic,
                     "Texture construction must be deterministic for a stored seed");

    juce::AudioBuffer<float> steadyTone(2, 20000);
    for (int sample = 0; sample < steadyTone.getNumSamples(); ++sample)
    {
        const auto phase = juce::MathConstants<float>::twoPi * 173.0f
                           * static_cast<float>(sample) / static_cast<float>(sampleRate);
        steadyTone.setSample(0, sample, 0.58f * std::sin(phase));
        steadyTone.setSample(1, sample, 0.54f * std::sin(phase + 0.06f));
    }
    auto naturalSettings = textureSettings;
    naturalSettings.flatten = 0.0f;
    naturalSettings.dynamicsCrush = 0.0f;
    naturalSettings.character = TextureCharacter::off;
    const auto naturalTone = TextureSynthesizer::synthesize(
        steadyTone, sampleRate, naturalSettings);
    passed &= expect(localLevelVariation(naturalTone.audio, 320, 80) < 0.08,
                     "Natural overlap should not impose strong periodic beating on a steady tone");

    auto characterOff = textureSettings;
    characterOff.character = TextureCharacter::off;
    characterOff.characterAmount = 1.0f;
    const auto offResult = TextureSynthesizer::synthesize(
        repeated, sampleRate, characterOff);
    auto zeroCharacter = characterOff;
    zeroCharacter.character = TextureCharacter::fray;
    zeroCharacter.characterAmount = 0.0f;
    const auto zeroResult = TextureSynthesizer::synthesize(
        repeated, sampleRate, zeroCharacter);
    passed &= expect(buffersEqual(offResult.audio, zeroResult.audio),
                     "Extra Off and zero amount must be exact audio bypasses");

    for (const auto character : std::array<TextureCharacter, 3> {
             TextureCharacter::patina, TextureCharacter::bloom, TextureCharacter::fray })
    {
        auto characterSettings = characterOff;
        characterSettings.character = character;
        characterSettings.characterAmount = 1.0f;
        const auto characterResult = TextureSynthesizer::synthesize(
            repeated, sampleRate, characterSettings);
        passed &= expect(characterResult.containsOnlyFiniteSamples
                             && characterResult.audio.getNumSamples()
                                    == offResult.audio.getNumSamples(),
                         "Every Extra style must preserve exact length and numeric safety");
        passed &= expect(averageDifference(offResult.audio, characterResult.audio) > 1.0e-4,
                         "Every Extra style must create an audible-scale signal change");
    }

    juce::AudioBuffer<float> dynamicsProbe(2, 16000);
    for (int sample = 0; sample < dynamicsProbe.getNumSamples(); ++sample)
    {
        const auto time = static_cast<float>(sample) / static_cast<float>(sampleRate);
        const auto envelope = 0.18f + 0.82f * std::pow(
            0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * 2.7f * time), 2.0f);
        const auto carrier = 0.58f * std::sin(
            juce::MathConstants<float>::twoPi * 173.0f * time)
            + 0.24f * std::sin(juce::MathConstants<float>::twoPi * 311.0f * time);
        dynamicsProbe.setSample(0, sample, envelope * carrier);
        dynamicsProbe.setSample(1, sample, envelope * 0.94f * carrier);
    }
    auto uncrushedSettings = textureSettings;
    uncrushedSettings.flatten = 0.0f;
    uncrushedSettings.dynamicsCrush = 0.0f;
    const auto uncrushed = TextureSynthesizer::synthesize(
        dynamicsProbe, sampleRate, uncrushedSettings);
    auto crushedSettings = uncrushedSettings;
    crushedSettings.dynamicsCrush = 1.0f;
    const auto crushed = TextureSynthesizer::synthesize(
        dynamicsProbe, sampleRate, crushedSettings);
    passed &= expect(crushed.audio.getNumSamples() == uncrushed.audio.getNumSamples()
                         && crushed.containsOnlyFiniteSamples,
                     "Dynamic Crush must preserve exact length and numeric safety");
    passed &= expect(localLevelVariation(crushed.audio, 320, 80)
                         < localLevelVariation(uncrushed.audio, 320, 80),
                     "Dynamic Crush should reduce linked local-envelope variation");

    LoopEngine textureEngine;
    textureEngine.prepare(sampleRate, 64, 2);
    textureEngine.setGenerationMode(LoopEngine::GenerationMode::textureLoop);
    textureEngine.setTextureDurationSeconds(4.0f);
    textureEngine.setTextureDynamicsCrush(0.8f);
    textureEngine.setTextureCharacter(TextureCharacter::bloom);
    textureEngine.setTextureCharacterAmount(0.45f);
    textureEngine.submitSource(repeated, "source-probe.wav");
    passed &= expect(textureEngine.reanalyzeSourceRange(0.1f, 0.9f),
                     "Texture mode should accept Source In/Out");
    passed &= expect(waitForReady(textureEngine),
                     "Texture generation should complete inside LoopEngine");
    passed &= expect(textureEngine.getLastUsedGenerationMode()
                         == LoopEngine::GenerationMode::textureLoop,
                      "Texture mode should remain active after generation");
    passed &= expect(textureEngine.getCandidateDescription(0).contains("Bloom"),
                     "LoopEngine should apply and identify the selected Extra style");
    passed &= expect(textureEngine.createRenderedLoop().getNumSamples() == 16000,
                     "Texture mode should expose the requested loop to the DAW");

    if (!passed)
        return 1;
    std::cout << "Loop Surgeon tests passed\n";
    return 0;
}
