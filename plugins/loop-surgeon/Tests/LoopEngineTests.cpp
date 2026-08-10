≠rá^—f•ñÿ¶{}Ïy 'v√Æ∂õ≠#include "LoopAnalyzer.h"
#include "LoopEngine.h"
#include "RenderQuality.h"
#include "TextureSynthesizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
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
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        if (engine.getState() == LoopEngine::State::ready)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

std::vector<float> blockRms(const juce::AudioBuffer<float>& audio, const int blockSamples)
{
    std::vector<float> levels;
    for (int start = 0; start + blockSamples <= audio.getNumSamples();
         start += blockSamples)
    {
        auto energy = 0.0;
        for (int sample = 0; sample < blockSamples; ++sample)
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            {
                const auto value = audio.getSample(channel, start + sample);
                energy += static_cast<double>(value) * value;
            }
        levels.push_back(std::sqrt(static_cast<float>(
            energy / static_cast<double>(blockSamples * audio.getNumChannels()))));
    }
    return levels;
}

float percentile(std::vector<float> values, const float proportion)
{
    if (values.empty())
        return 0.0f;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>(juce::roundToInt(
        proportion * static_cast<float>(values.size() - 1u)));
    return values[index];
}

float coefficientOfVariation(const std::vector<float>& values)
{
    if (values.empty())
        return 0.0f;
    auto mean = 0.0;
    auto squareTotal = 0.0;
    for (const auto value : values)
    {
        mean += value;
        squareTotal += static_cast<double>(value) * value;
    }
    mean /= static_cast<double>(values.size());
    const auto variance = juce::jmax(
        0.0, squareTotal / static_cast<double>(values.size()) - mean * mean);
    return static_cast<float>(std::sqrt(variance) / juce::jmax(1.0e-7, mean));
}

std::vector<float> spectralCentroids(const juce::AudioBuffer<float>& audio,
                                     const double sampleRate,
                                     const int blockSamples)
{
    std::vector<float> centroids;
    for (int start = 0; start + blockSamples <= audio.getNumSamples();
         start += blockSamples)
    {
        auto weighted = 0.0;
        auto total = 0.0;
        for (int bin = 1; bin <= blockSamples / 2; ++bin)
        {
            auto real = 0.0;
            auto imaginary = 0.0;
            for (int sample = 0; sample < blockSamples; ++sample)
            {
                auto mono = 0.0f;
                for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                    mono += audio.getSample(channel, start + sample);
                mono /= static_cast<float>(audio.getNumChannels());
                const auto hann = 0.5 - 0.5 * std::cos(
                    juce::MathConstants<double>::twoPi
                    * static_cast<double>(sample)
                    / static_cast<double>(blockSamples - 1));
                const auto phase = juce::MathConstants<double>::twoPi
                                   * static_cast<double>(bin * sample)
                                   / static_cast<double>(blockSamples);
                real += mono * hann * std::cos(phase);
                imaginary -= mono * hann * std::sin(phase);
            }
            const auto power = real * real + imaginary * imaginary;
            const auto frequency = static_cast<double>(bin) * sampleRate
                                   / static_cast<double>(blockSamples);
            weighted += frequency * power;
            total += power;
        }
        centroids.push_back(static_cast<float>(weighted / juce::jmax(1.0e-12, total)));
    }
    return centroids;
}

float strongestAutocorrelation(const std::vector<float>& values,
                               const int minimumLag,
                               const int maximumLag)
{
    if (values.size() < 3u)
        return 0.0f;
    const auto mean = std::accumulate(values.begin(), values.end(), 0.0)
                      / static_cast<double>(values.size());
    auto strongest = -1.0f;
    for (int lag = minimumLag;
         lag <= maximumLag && lag < static_cast<int>(values.size()) / 2; ++lag)
    {
        auto numerator = 0.0;
        auto leftEnergy = 0.0;
        auto rightEnergy = 0.0;
        for (int index = 0; index + lag < static_cast<int>(values.size()); ++index)
        {
            const auto left = static_cast<double>(values[static_cast<size_t>(index)]) - mean;
            const auto right = static_cast<double>(
                values[static_cast<size_t>(index + lag)]) - mean;
            numerator += left * right;
            leftEnergy += left * left;
            rightEnergy += right * right;
        }
        strongest = juce::jmax(
            strongest,
            static_cast<float>(numerator / std::sqrt(
                juce::jmax(1.0e-12, leftEnergy * rightEnergy))));
    }
    return strongest;
}

float meanNearestSourceCorrelation(const juce::AudioBuffer<float>& source,
                                   const juce::AudioBuffer<float>& output,
                                   const int frameSamples)
{
    auto total = 0.0f;
    auto compared = 0;
    for (int outputStart = 0;
         outputStart + frameSamples <= output.getNumSamples();
         outputStart += frameSamples)
    {
        const auto outputRms = output.getRMSLevel(0, outputStart, frameSamples);
        if (outputRms < 1.0e-6f)
            continue;
        auto nearest = -1.0f;
        for (int sourceStart = 0;
             sourceStart + frameSamples <= source.getNumSamples();
             sourceStart += frameSamples / 2)
        {
            const auto sourceRms = source.getRMSLevel(0, sourceStart, frameSamples);
            if (sourceRms < 1.0e-6f)
                continue;
            auto dot = 0.0;
            auto sourceEnergy = 0.0;
            auto outputEnergy = 0.0;
            for (int sample = 0; sample < frameSamples; ++sample)
            {
                const auto sourceValue = source.getSample(0, sourceStart + sample);
                const auto outputValue = output.getSample(0, outputStart + sample);
                dot += static_cast<double>(sourceValue) * outputValue;
                sourceEnergy += static_cast<double>(sourceValue) * sourceValue;
                outputEnergy += static_cast<double>(outputValue) * outputValue;
            }
            nearest = juce::jmax(nearest, static_cast<float>(
                std::abs(dot) / std::sqrt(
                    juce::jmax(1.0e-12, sourceEnergy * outputEnergy))));
        }
        total += juce::jmax(0.0f, nearest);
        ++compared;
    }
    return total / static_cast<float>(juce::jmax(1, compared));
}
}

int main()
{
    bool passed = true;

    constexpr auto automaticSampleRate = 2000.0;
    constexpr auto truePeriod = 800;
    juce::AudioBuffer<float> repeatedMaterial(2, truePeriod * 6);
    for (int sample = 0; sample < repeatedMaterial.getNumSamples(); ++sample)
    {
        const auto position = sample % truePeriod;
        const auto envelope = 0.35f + 0.55f * std::sin(
            juce::MathConstants<float>::twoPi * static_cast<float>(position) / truePeriod);
        const auto carrier = std::sin(juce::MathConstants<float>::twoPi
                                      * 13.0f * static_cast<float>(position) / truePeriod);
        repeatedMaterial.setSample(0, sample, envelope * carrier);
        repeatedMaterial.setSample(1, sample, 0.8f * envelope * std::sin(
            juce::MathConstants<float>::twoPi * 13.0f * static_cast<float>(position) / truePeriod + 0.1f));
    }
    const auto automatic = LoopAnalyzer::analyzeSource(repeatedMaterial, automaticSampleRate,
                                                        500, 1200, 3);
    passed &= expect(!automatic.candidates.empty(),
                     "automatic source analysis should produce candidates");
    if (!automatic.candidates.empty())
    {
        const auto detectedLength = automatic.candidates.front().endSample
                                    - automatic.candidates.front().startSample;
        passed &= expect(std::abs(detectedLength - truePeriod) < 100,
                         "automatic source analysis should recover the repeated period");
        passed &= expect(automatic.candidates.front().transient >= 0.0f
                             && automatic.candidates.front().transient <= 100.0f,
                         "transient seam score should be normalized");
    }
    const auto overlapAware = LoopAnalyzer::analyzeSource(repeatedMaterial, automaticSampleRate,
                                                           500, 1200, 3, 50);
    passed &= expect(!overlapAware.candidates.empty(),
                     "overlap-aware analysis should produce candidates");
    if (!overlapAware.candidates.empty())
    {
        const auto selectedRepair = overlapAware.candidates.front().repairOverlapSamples;
        const auto renderedLength = overlapAware.candidates.front().endSample
                                    - overlapAware.candidates.front().startSample - selectedRepair;
        passed &= expect(selectedRepair > 0 && selectedRepair <= 50,
                         "automatic analysis should choose a repair window within the user maximum");
        passed &= expect(std::abs(renderedLength - truePeriod) < 100,
                         "crossfade overlap must not shorten the intended loop period");
        passed &= expect(overlapAware.candidates.front().repair >= 0.0f
                             && overlapAware.candidates.front().repair <= 100.0f,
                         "post-repair seam score should be normalized");
    }
    const auto exactRange = LoopAnalyzer::evaluateFixedRange(repeatedMaterial,
                                                              automaticSampleRate, 0, truePeriod);
    const auto brokenRange = LoopAnalyzer::evaluateFixedRange(repeatedMaterial,
                                                               automaticSampleRate, 0, truePeriod - 137);
    passed &= expect(exactRange.waveform > brokenRange.waveform,
                     "direct boundary score should prefer the sample-continuous period");
    const auto repairedExact = LoopAnalyzer::evaluateFixedRange(
        repeatedMaterial, automaticSampleRate, 0, truePeriod + 50, 50);
    const auto repairedBroken = LoopAnalyzer::evaluateFixedRange(
        repeatedMaterial, automaticSampleRate, 0, truePeriod - 137 + 50, 50);
    passed &= expect(repairedExact.repair > repairedBroken.repair,
                     "post-render repair scoring should reject a misleading bad seam");

    const auto rotateReport = LoopAnalyzer::analyzeRotateRepair(
        repeatedMaterial, automaticSampleRate, 3, 120);
    passed &= expect(!rotateReport.candidates.empty(),
                     "Rotate & Repair should propose full-selection loop starts");
    if (!rotateReport.candidates.empty())
    {
        const auto& rotate = rotateReport.candidates.front();
        const auto rotated = LoopAnalyzer::renderRotateRepair(repeatedMaterial, rotate);
        passed &= expect(rotate.startSample == 0
                             && rotate.endSample == repeatedMaterial.getNumSamples(),
                         "Rotate & Repair must keep the full selected source range");
        passed &= expect(rotate.rotationSample > rotate.startSample
                             && rotate.rotationSample < rotate.endSample,
                         "Rotate & Repair should choose an internal natural loop start");
        passed &= expect(rotated.getNumSamples()
                             == repeatedMaterial.getNumSamples() - rotate.repairOverlapSamples,
                         "only the internal seam overlap may shorten a repaired long loop");
        passed &= expect(rotated.getNumSamples() > repeatedMaterial.getNumSamples() * 9 / 10,
                         "Rotate & Repair must never collapse a long source into a short period");
        passed &= expect(RenderQuality::estimateCircularTruePeak(rotated)
                             <= juce::Decibels::decibelsToGain(-0.85f),
                         "repaired loop must retain true-peak headroom");
    }

    constexpr auto exactRepairOutputSamples = 4000;
    const auto exactRepairReport = LoopAnalyzer::analyzeRotateRepairExact(
        repeatedMaterial, automaticSampleRate, exactRepairOutputSamples, 3, 120);
    passed &= expect(!exactRepairReport.candidates.empty(),
                     "R&R should find an exact-duration window inside the source selection");
    if (!exactRepairReport.candidates.empty())
    {
        const auto exactRepair = LoopAnalyzer::renderRotateRepair(
            repeatedMaterial, exactRepairReport.candidates.front());
        passed &= expect(exactRepair.getNumSamples() == exactRepairOutputSamples,
                         "R&R Final Length must be sample-exact after seam overlap");
    }
    const auto impossibleRepairReport = LoopAnalyzer::analyzeRotateRepairExact(
        repeatedMaterial, automaticSampleRate,
        repeatedMaterial.getNumSamples() + 1, 3, 120);
    passed &= expect(impossibleRepairReport.candidates.empty(),
                     "R&R must reject a Final Length longer than Source In/Out");

    juce::AudioBuffer<float> silenceTrap(1, truePeriod * 6);
    silenceTrap.clear();
    for (int sample = truePeriod; sample < silenceTrap.getNumSamples(); ++sample)
    {
        const auto position = sample % truePeriod;
        const auto tone = 0.5f * std::sin(
            juce::MathConstants<float>::twoPi * 9.0f * static_cast<float>(position)
            / truePeriod);
        const auto texture = 0.18f * std::sin(
            juce::MathConstants<float>::twoPi * 23.0f * static_cast<float>(position)
            / truePeriod + 0.3f);
        silenceTrap.setSample(0, sample, tone + texture);
    }
    const auto silenceTrapReport = LoopAnalyzer::analyzeSource(
        silenceTrap, automaticSampleRate, 650, 1150, 3, 50);
    passed &= expect(!silenceTrapReport.candidates.empty(),
                     "quality corpus with a silent prefix should still produce an active loop");
    if (!silenceTrapReport.candidates.empty())
        passed &= expect(silenceTrapReport.candidates.front().startSample >= truePeriod / 2,
                         "silence activity penalty should prevent a perfect-score silent loop");

    juce::AudioBuffer<float> windOneShot(2, 16000);
    uint32_t noiseState = 0x1234567u;
    auto previousNoise = 0.0f;
    for (int sample = 0; sample < windOneShot.getNumSamples(); +ﬂæˆ∂âûÀk∫wµÁeÈî†(ÄÄÄÄÄÄÄÅ¡ÖÕÕ	Â=πïM°Ω–∞ÅÖ’—ΩµÖ—•çMÖµ¡±ïIÖ—î∞Å¡ÖÕÕ	ÂMï——•πùÃ§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅÕΩ’…çïïπ—…Ω•ëÃÄÙÅÕ¡ïç—…Ö±ïπ—…Ω•ëÃ†(ÄÄÄÄÄÄÄÅ¡ÖÕÕ	Â=πïM°Ω–∞ÅÖ’—ΩµÖ—•çMÖµ¡±ïIÖ—î∞Ä»‘ÿ§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅΩ’—¡’—ïπ—…Ω•ëÃÄÙÅÕ¡ïç—…Ö±ïπ—…Ω•ëÃ†(ÄÄÄÄÄÄÄÅ¡ÖÕÕ	ÂQï·—’…îπÖ’ë•º∞ÅÖ’—ΩµÖ—•çMÖµ¡±ïIÖ—î∞Ä»‘ÿ§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°çΩïôô•ç•ïπ—=ôYÖ…•Ö—•Ω∏°Ω’—¡’—ïπ—…Ω•ëÃ§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÅçΩïôô•ç•ïπ—=ôYÖ…•Ö—•Ω∏°ÕΩ’…çïïπ—…Ω•ëÃ§Ä®Ä¿∏‘·ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÕ—Ö—•ΩπÖ…‰ÅÕÂπ—°ïÕ•ÃÅµ’Õ–Å…ïµΩŸîÅ—°îÅΩπîµÕ°Ω–Å¡ÖÕÃµâ‰Å—…Ö©ïç—Ω…‰à§Ï(ÄÄÄÅÕ—êËÈŸïç—Ω»Òô±ΩÖ–¯ÅΩ’—¡’—ïπ—…Ω•ë5ΩŸïµïπ–Ï(ÄÄÄÅΩ’—¡’—ïπ—…Ω•ë5ΩŸïµïπ–π…ïÕï…Ÿî°Ω’—¡’—ïπ—…Ω•ëÃπÕ•Èî†§Ä¥Ä≈‘§Ï(ÄÄÄÅôΩ»Ä°Õ•Èï}–Åô…ÖµîÄÙÄƒÏÅô…ÖµîÄÅΩ’—¡’—ïπ—…Ω•ëÃπÕ•Èî†§ÏÄ¨≠ô…Öµî§(ÄÄÄÄÄÄÄÅΩ’—¡’—ïπ—…Ω•ë5ΩŸïµïπ–π¡’Õ°}âÖç¨†(ÄÄÄÄÄÄÄÄÄÄÄÅΩ’—¡’—ïπ—…Ω•ëÕmô…ÖµïtÄ¥ÅΩ’—¡’—ïπ—…Ω•ëÕmô…ÖµîÄ¥Ä≈’t§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅ¡ÖÕÕ	ÂIï¡ïÖ–ÄÙÅÕ—…ΩπùïÕ—’—ΩçΩ……ï±Ö—•Ω∏†(ÄÄÄÄÄÄÄÅΩ’—¡’—ïπ—…Ω•ë5ΩŸïµïπ–∞Ä–∞Ä»–§Ï(ÄÄÄÅ•òÄ°¡ÖÕÕ	ÂIï¡ïÖ–Ä¯ÙÄ¿∏ÃŸò§(ÄÄÄÄÄÄÄÅÕ—êËÈçï…»ÄÄâ¡ÖÕÃµâ‰Åçïπ—…Ω•êÅ…ï¡ï—•—•Ω∏ÙàÄÅ¡ÖÕÕ	ÂIï¡ïÖ–ÄÄùq∏úÏ(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°¡ÖÕÕ	ÂIï¡ïÖ–ÄÄ¿∏ÃŸò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâΩ’—¡’–ÅÕ¡ïç—…’¥Åµ’Õ–ÅπΩ–Å…ï¡ïÖ–ÅÑÅ°•ëëï∏Å¡ÖÕÃµâ‰ÅçÂç±îà§Ï((ÄÄÄÅ1ΩΩ¡πù•πîÅ—ï·—’…ïπù•πîÏ(ÄÄÄÅ—ï·—’…ïπù•πîπ¡…ï¡Ö…î°Ö’—ΩµÖ—•çMÖµ¡±ïIÖ—î∞Äÿ–∞Ä»§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï—ïπï…Ö—•Ωπ5Ωëî°1ΩΩ¡πù•πîËÈïπï…Ö—•Ωπ5ΩëîËÈ—ï·—’…ï1ΩΩ¿§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï—Qï·—’…ï’…Ö—•ΩπMïçΩπëÃ†ÿ∏¡ò§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï—Qï·—’…ïYÖ…•Ö—•Ω∏†¿∏·ò§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï—Qï·—’…ï±Ö——ï∏†¿∏·ò§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï—Qï·—’…ïMΩ’…çï5Ö—ç††¿∏Âò§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕ’âµ•—MΩ’…çî°›•πë=πïM°Ω–∞Äâ›•πêµΩπîµÕ°Ω–π›Öÿà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°—ï·—’…ïπù•πîπùï—M—Ö—î†§ÄÙÙÅ1ΩΩ¡πù•πîËÈM—Ö—îËÈÕΩ’…çïIïÖë‰∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâô•±îÅ•µ¡Ω…–ÅÕ°Ω’±êÅ›Ö•–ÅôΩ»Å—°îÅ’Õï»Å—ºÅç°ΩΩÕîÅ…ÖπùîÅÖπêÅµΩëîà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°—ï·—’…ïπù•πîπ…ïÖπÖ±ÂÈïMΩ’…çïIÖπùî†¿∏¡ò∞Äƒ∏¡ò§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâïπï…Ö—îÅÕ°Ω’±êÅÕ—Ö…–ÅQï·—’…îÅ1ΩΩ¿Åô…Ω¥Å—°îÅÕï±ïç—ïêÅ…Öπùîà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°›Ö•—Ω…IïÖë‰°—ï·—’…ïπù•πî§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâïŸΩ±Ÿ•πúÅ—ï·—’…îÅµΩëîÅÕ°Ω’±êÅô•π•Õ†Å•∏Å—°îÅâÖç≠ù…Ω’πêà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°—ï·—’…ïπù•πîπùï—1ÖÕ—UÕïëïπï…Ö—•Ωπ5Ωëî†§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙÙÅ1ΩΩ¡πù•πîËÈïπï…Ö—•Ωπ5ΩëîËÈ—ï·—’…ï1ΩΩ¿∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâï·¡±•ç•–Å—ï·—’…îÅÕï±ïç—•Ω∏ÅÕ°Ω’±êÅâîÅ…ï—Ö•πïêà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°—ï·—’…ïπù•πîπùï—Öπë•ëÖ—ïΩ’π–†§ÄÙÙÄ»∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ—ï·—’…îÅµΩëîÅÕ°Ω’±êÅ…ï—Ö•∏Å—›ºÅÖ±—ï…πÖ—•ŸïÃÅ›•—°Ω’–Å—…•¡±•πúÅµïµΩ…‰Å’Õîà§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅïπù•πïQï·—’…îÄÙÅ—ï·—’…ïπù•πîπç…ïÖ—ïIïπëï…ïë1ΩΩ¿†§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πïQï·—’…îπùï—9’µMÖµ¡±ïÃ†§ÄÙÙÄƒ»¿¿¿∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâïπù•πîÅÕ°Ω’±êÅ¡’â±•Õ†Å—°îÅ…ï≈’ïÕ—ïêÅùïπï…Ö—ïêÅ—ï·—’…îÅ±ïπù—†à§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï±ïç—Öπë•ëÖ—î†ƒ§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅÕïçΩπëQï·—’…îÄÙÅ—ï·—’…ïπù•πîπç…ïÖ—ïIïπëï…ïë1ΩΩ¿†§Ï(ÄÄÄÅÖ’—ºÅçÖπë•ëÖ—ï•ôôï…ïπçîÄÙÄ¿∏¿Ï(ÄÄÄÅôΩ»Ä°•π–ÅÕÖµ¡±îÄÙÄ¿ÏÅÕÖµ¡±îÄÅÕïçΩπëQï·—’…îπùï—9’µMÖµ¡±ïÃ†§ÏÄ¨≠ÕÖµ¡±î§(ÄÄÄÄÄÄÄÅçÖπë•ëÖ—ï•ôôï…ïπçîÄ¨ÙÅÕ—êËÈÖâÃ°ïπù•πïQï·—’…îπùï—MÖµ¡±î†¿∞ÅÕÖµ¡±î§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¥ÅÕïçΩπëQï·—’…îπùï—MÖµ¡±î†¿∞ÅÕÖµ¡±î§§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°çÖπë•ëÖ—ï•ôôï…ïπçîÄ¯Äƒ∏¿∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÕï±ïç—•πúÅÖπΩ—°ï»Å—ï·—’…îÅçÖπë•ëÖ—îÅÕ°Ω’±êÅÕ›Ö¿Å•∏Åë•ôôï…ïπ–ÅÖ’ë•ºà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°—ï·—’…ïπù•πîπùï—Öπë•ëÖ—ïïÕç…•¡—•Ω∏†¿§πçΩπ—Ö•πÃ†àÿ∏¿ÅÃà§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄòòÅ—ï·—’…ïπù•πîπùï—Öπë•ëÖ—ïïÕç…•¡—•Ω∏†ƒ§πçΩπ—Ö•πÃ†àÿ∏¿ÅÃà§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÖç—•ŸîÅÖπêÅ•πÖç—•ŸîÅµïµΩ…‰µÕ›Ö¡¡ïêÅŸÖ…•Öπ—ÃÅÕ°Ω’±êÅ…ï—Ö•∏ÅŸÖ±•êÅëïÕç…•¡—•ΩπÃà§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï±ïç—Öπë•ëÖ—î†¿§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅ…ïÕ—Ω…ïë•…Õ—Qï·—’…îÄÙÅ—ï·—’…ïπù•πîπç…ïÖ—ïIïπëï…ïë1ΩΩ¿†§Ï(ÄÄÄÅÖ’—ºÅ…ïÕ—Ω…ïëÖπë•ëÖ—ï……Ω»ÄÙÄ¿∏¿Ï(ÄÄÄÅôΩ»Ä°•π–ÅÕÖµ¡±îÄÙÄ¿ÏÅÕÖµ¡±îÄÅ…ïÕ—Ω…ïë•…Õ—Qï·—’…îπùï—9’µMÖµ¡±ïÃ†§ÏÄ¨≠ÕÖµ¡±î§(ÄÄÄÄÄÄÄÅ…ïÕ—Ω…ïëÖπë•ëÖ—ï……Ω»Ä¨ÙÅÕ—êËÈÖâÃ°ïπù•πïQï·—’…îπùï—MÖµ¡±î†¿∞ÅÕÖµ¡±î§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¥Å…ïÕ—Ω…ïë•…Õ—Qï·—’…îπùï—MÖµ¡±î†¿∞ÅÕÖµ¡±î§§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÕ—Ω…ïëÖπë•ëÖ—ï……Ω»ÄÄƒ∏¡î¥ÿ∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâçÖπë•ëÖ—îÅÕ›•—ç°•πúÅÕ°Ω’±êÅ¡…ïÕï…ŸîÅïÖ…±•ï»ÅÖ’ë•ºÅ›•—°Ω’–Åë’¡±•çÖ—îÅâ’ôôï…Ãà§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï—A…ïŸ•ï›5Ωëî°1ΩΩ¡πù•πîËÈA…ïŸ•ï›5ΩëîËÈ±ΩΩ¿§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπÕï—A…ïŸ•ï›A±ÖÂ•πú°—…’î§Ï(ÄÄÄÅ©’çîËÈ’ë•Ω	’ôôï»Òô±ΩÖ–¯Å—ï·—’…ïA…ïŸ•ï‹†»∞Äÿ–§Ï(ÄÄÄÅ—ï·—’…ïA…ïŸ•ï‹πç±ïÖ»†§Ï(ÄÄÄÅ—ï·—’…ïπù•πîπ¡…ΩçïÕÃ°—ï·—’…ïA…ïŸ•ï‹∞Äƒ∏¡ò§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°—ï·—’…ïA…ïŸ•ï‹πùï—5Öùπ•—’ëî†¿∞Ä¿∞Äÿ–§Ä¯Ä¿∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâïπï…Ö—ïêÅÖ’ë•—•Ω∏ÅÕ°Ω’±êÅÕ—Ö…–Å¡±ÖÂâÖç¨Å•µµïë•Ö—ï±‰à§Ï((ÄÄÄÅ1ΩΩ¡πù•πîÅ…Öπùïπù•πîÏ(ÄÄÄÅ…Öπùïπù•πîπ¡…ï¡Ö…î°Ö’—ΩµÖ—•çMÖµ¡±ïIÖ—î∞Äÿ–∞Ä»§Ï(ÄÄÄÅ…Öπùïπù•πîπÕï—ïπï…Ö—•Ωπ5Ωëî°1ΩΩ¡πù•πîËÈïπï…Ö—•Ωπ5ΩëîËÈ…Ω—Ö—ïIï¡Ö•»§Ï(ÄÄÄÅ…Öπùïπù•πîπÕ’âµ•—MΩ’…çî°…ï¡ïÖ—ïë5Ö—ï…•Ö∞∞Äâ…ï¡ïÖ—ïêµµÖ—ï…•Ö∞π›Öÿà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…Öπùïπù•πîπùï—M—Ö—î†§ÄÙÙÅ1ΩΩ¡πù•πîËÈM—Ö—îËÈÕΩ’…çïIïÖë‰∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ±ΩÖëïêÅÖµâ•ïπçîÅÕ°Ω’±êÅ›Ö•–ÅôΩ»ÅÖ∏Åï·¡±•ç•–Åïπï…Ö—îÅÖç—•Ω∏à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…Öπùïπù•πîπ…ïÖπÖ±ÂÈïMΩ’…çïIÖπùî†¿∏¡ò∞Äƒ∏¡ò§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâïπï…Ö—îÅÕ°Ω’±êÅÕ—Ö…–ÅIΩ—Ö—îÄòÅIï¡Ö•»Åô…Ω¥Å—°îÅÕï±ïç—ïêÅ…Öπùîà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°›Ö•—Ω…IïÖë‰°…Öπùïπù•πî§∞Äâ•µ¡Ω…—ïêÅÕΩ’…çîÅÕ°Ω’±êÅâîÅÖπÖ±ÂÕïêÅ•∏ÅâÖç≠ù…Ω’πêà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…Öπùïπù•πîπùï—Öπë•ëÖ—ïΩ’π–†§Ä¯Ä¿∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ•µ¡Ω…—ïêÅÕΩ’…çîÅÕ°Ω’±êÅï·¡ΩÕîÅÖ±—ï…πÖ—îÅçÖπë•ëÖ—ïÃà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…Öπùïπù•πîπùï—]ÖŸïôΩ…µA…ïŸ•ï‹†§πÕ•Èî†§ÄÙÙÄÃ»¿∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ•µ¡Ω…—ïêÅÕΩ’…çîÅÕ°Ω’±êÅ¡’â±•Õ†ÅÑÅâΩ’πëïêÅ›ÖŸïôΩ…¥Å¡…ïŸ•ï‹à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…Öπùïπù•πîπ…ïÖπÖ±ÂÈïMΩ’…çïIÖπùî†¿∏Õò∞Ä¿∏Âò§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÕΩ’…çîÅ%∏Ω=’–ÅÕï±ïç—•Ω∏ÅÕ°Ω’±êÅ≈’ï’îÅ…ïÖπÖ±ÂÕ•Ãà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°›Ö•—Ω…IïÖë‰°…Öπùïπù•πî§∞ÄâÕï±ïç—ïêÅÕΩ’…çîÅ…ÖπùîÅÕ°Ω’±êÅô•π•Õ†ÅÖπÖ±ÂÕ•Ãà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…Öπùïπù•πîπùï—IΩ—Ö—•ΩπA…Ω¡Ω…—•Ω∏†§Ä¯ÙÄ¿∏Õò(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄòòÅ…Öπùïπù•πîπùï—IΩ—Ö—•ΩπA…Ω¡Ω…—•Ω∏†§ÄÙÄ¿∏Âò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÖ’—ΩµÖ—•åÅ±ΩΩ¿ÅÕ—Ö…–Åµ’Õ–Å…ïµÖ•∏Å•πÕ•ëîÅ’Õï»ÅMΩ’…çîÅ%∏Ω=’–ÅÕï±ïç—•Ω∏à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…Öπùïπù•πîπÕï—5Öπ’Ö±IΩ—Ö—•ΩπAΩ•π–†¿∏—ò§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâë…ÖùùÖâ±îÅ1ΩΩ¿ÅM—Ö…–ÅÕ°Ω’±êÅ¡’â±•Õ†ÅÑÅµÖπ’Ö±±‰Å…Ω—Ö—ïêÅ±ΩΩ¿à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°Õ—êËÈÖâÃ°…Öπùïπù•πîπùï—IΩ—Ö—•ΩπA…Ω¡Ω…—•Ω∏†§Ä¥Ä¿∏—ò§ÄÄ¿∏¿¿…ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâµÖπ’Ö∞Å1ΩΩ¿ÅM—Ö…–ÅÕ°Ω’±êÅ…ï—Ö•∏Å—°îÅ…ï≈’ïÕ—ïêÅ…Ω—Ö—•Ω∏Å¡Ω•π–à§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅµÖπ’Ö±Iïπëï…ïêÄÙÅ…Öπùïπù•πîπç…ïÖ—ïIïπëï…ïë1ΩΩ¿†§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°µÖπ’Ö±Iïπëï…ïêπùï—9’µMÖµ¡±ïÃ†§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¯Å©’çîËÈ…Ω’πëQΩ%π–†¿∏‘‘Ä®Å…ï¡ïÖ—ïë5Ö—ï…•Ö∞πùï—9’µMÖµ¡±ïÃ†§§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâIΩ—Ö—îÄòÅIï¡Ö•»Åµ’Õ–Å¡…ïÕï…ŸîÅ—°îÅÕï±ïç—ïêÅ±ΩπúµôΩ…¥ÅµÖ—ï…•Ö∞à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…Öπùïπù•πîπùï—M•ùπÖ±MπÖ¡Õ°Ω–†§πŸÖ±•ê∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ…ïÖë‰ÅΩ’—¡’–ÅÕ°Ω’±êÅ¡’â±•Õ†Å…ïÖ∞ÅÕ¡ïç—…’¥∞Å¡°ÖÕîÅÖπêÅçΩ……ï±Ö—•Ω∏ÅÖπÖ±ÂÕ•Ãà§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅ…ÖπùïM—Ö—îÄÙÅ…Öπùïπù•πîπç…ïÖ—ï1ΩΩ¡M—Ö—î†§Ï(ÄÄÄÅ1ΩΩ¡πù•πîÅ…ïÕ—Ω…ïëIÖπùîÏ(ÄÄÄÅ…ïÕ—Ω…ïëIÖπùîπ¡…ï¡Ö…î°Ö’—ΩµÖ—•çMÖµ¡±ïIÖ—î∞Äÿ–∞Ä»§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÕ—Ω…ïëIÖπùîπ…ïÕ—Ω…ï1ΩΩ¡M—Ö—î†(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÅ…ÖπùïM—Ö—îπùï—Ö—Ñ†§∞Å…ÖπùïM—Ö—îπùï—M•Èî†§§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÕï±ïç—ïêÅÕΩ’…çîÅ…ÖπùîÅÖπêÅ…ï¡Ö•…ïêÅ…ïÕ’±–ÅÕ°Ω’±êÅ…ïÕ—Ω…îà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°Õ—êËÈÖâÃ°…ïÕ—Ω…ïëIÖπùîπùï—πÖ±ÂÕ•ÕIÖπùïM—Ö…—A…Ω¡Ω…—•Ω∏†§Ä¥Ä¿∏Õò§ÄÄ¿∏¿¿…ò(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄòòÅÕ—êËÈÖâÃ°…ïÕ—Ω…ïëIÖπùîπùï—πÖ±ÂÕ•ÕIÖπùïπëA…Ω¡Ω…—•Ω∏†§Ä¥Ä¿∏Âò§ÄÄ¿∏¿¿…ò(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄòòÅÕ—êËÈÖâÃ°…ïÕ—Ω…ïëIÖπùîπùï—IΩ—Ö—•ΩπA…Ω¡Ω…—•Ω∏†§Ä¥Ä¿∏—ò§ÄÄ¿∏¿¿…ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ¡…Ω©ïç–Å…ïçÖ±∞ÅÕ°Ω’±êÅ…ïÕ—Ω…îÅMΩ’…çîÅ%∏Ω=’–ÅÖπêÅ1ΩΩ¿ÅM—Ö…–Å¡ΩÕ•—•ΩπÃà§Ï(ÄÄÄÅ…Öπùïπù•πîπÕï—A…ïŸ•ï›5Ωëî°1ΩΩ¡πù•πîËÈA…ïŸ•ï›5ΩëîËÈΩ…•ù•πÖ∞§Ï(ÄÄÄÅ…Öπùïπù•πîπÕï—A…ïŸ•ï›A±ÖÂ•πú°—…’î§Ï(ÄÄÄÅ©’çîËÈ’ë•Ω	’ôôï»Òô±ΩÖ–¯ÅΩ…•ù•πÖ±A…ïŸ•ï‹†»∞Äÿ–§Ï(ÄÄÄÅΩ…•ù•πÖ±A…ïŸ•ï‹πç±ïÖ»†§Ï(ÄÄÄÅ…Öπùïπù•πîπ¡…ΩçïÕÃ°Ω…•ù•πÖ±A…ïŸ•ï‹∞Äƒ∏¡ò§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°Ω…•ù•πÖ±A…ïŸ•ï‹πùï—5Öùπ•—’ëî†¿∞Ä¿∞Äÿ–§Ä¯Ä¿∏¡ò(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄòòÅΩ…•ù•πÖ±A…ïŸ•ï‹πùï—5Öùπ•—’ëî†ƒ∞Ä¿∞Äÿ–§Ä¯Ä¿∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ=…•ù•πÖ∞ÅΩÅµΩëîÅÕ°Ω’±êÅÖ’ë•—•Ω∏Å•µ¡Ω…—ïêÅÕ—ï…ïºÅÕΩ’…çîà§Ï((ÄÄÄÅ©’çîËÈ’ë•Ω	’ôôï»Òô±ΩÖ–¯Å…ïÖ±•Õ—•çπÖ±ÂÕ•Ã†»∞Äÿ»–¿¿§Ï(ÄÄÄÅôΩ»Ä°•π–ÅÕÖµ¡±îÄÙÄ¿ÏÅÕÖµ¡±îÄÅ…ïÖ±•Õ—•çπÖ±ÂÕ•Ãπùï—9’µMÖµ¡±ïÃ†§ÏÄ¨≠ÕÖµ¡±î§(ÄÄÄÅÏ(ÄÄÄÄÄÄÄÅçΩπÕ–ÅÖ’—ºÅ¡°ÖÕîÄÙÅ©’çîËÈ5Ö—°ΩπÕ—Öπ—ÃÒô±ΩÖ–¯ËÈ—›ΩA§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ®ÅÕ—Ö—•ç}çÖÕ–Òô±ΩÖ–¯°ÕÖµ¡±îÄîÄƒ»¿¿¿§ÄºÄƒ»¿¿¿∏¡òÏ(ÄÄÄÄÄÄÄÅ…ïÖ±•Õ—•çπÖ±ÂÕ•ÃπÕï—MÖµ¡±î†¿∞ÅÕÖµ¡±î∞Ä¿∏›òÄ®ÅÕ—êËÈÕ•∏°¡°ÖÕî§§Ï(ÄÄÄÄÄÄÄÅ…ïÖ±•Õ—•çπÖ±ÂÕ•ÃπÕï—MÖµ¡±î†ƒ∞ÅÕÖµ¡±î∞Ä¿∏ŸòÄ®ÅÕ—êËÈÕ•∏°¡°ÖÕîÄ¨Ä¿∏ƒ…ò§§Ï(ÄÄÄÅÙ(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅÖπÖ±ÂÕ•ÕM—Ö…–ÄÙÅÕ—êËÈç°…ΩπºËÈÕ—ïÖëÂ}ç±Ωç¨ËÈπΩ‹†§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅ…ïÖ±•Õ—•çIïÕ’±–ÄÙÅ1ΩΩ¡πÖ±ÂÈï»ËÈô•πë	ïÕ—1ΩΩ¿°…ïÖ±•Õ—•çπÖ±ÂÕ•Ã∞Ä–‡¿¿¿∏¿∞Ä–‡¿¿¿∞Ä‹»¿¿§Ï(ÄÄÄÅçΩπÕ–ÅÖ’—ºÅÖπÖ±ÂÕ•Õ’…Ö—•Ω∏ÄÙÅÕ—êËÈç°…ΩπºËÈë’…Ö—•Ωπ}çÖÕ–ÒÕ—êËÈç°…ΩπºËÈµ•±±•ÕïçΩπëÃ¯†(ÄÄÄÄÄÄÄÅÕ—êËÈç°…ΩπºËÈÕ—ïÖëÂ}ç±Ωç¨ËÈπΩ‹†§Ä¥ÅÖπÖ±ÂÕ•ÕM—Ö…–§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÖ±•Õ—•çIïÕ’±–πïπëMÖµ¡±îÄ¯Å…ïÖ±•Õ—•çIïÕ’±–πÕ—Ö…—MÖµ¡±î∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄà–‡Å≠!ËÅÖπÖ±ÂÕ•ÃÅÕ°Ω’±êÅÕï±ïç–ÅŸÖ±•êÅâΩ’πëÖ…•ïÃà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÖ±•Õ—•çIïÕ’±–πÕ¡ïç—…’¥Ä¯ÙÄ¿∏¡òÄòòÅ…ïÖ±•Õ—•çIïÕ’±–πÕ¡ïç—…’¥ÄÙÄƒ¿¿∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄà–‡Å≠!ËÅÕ¡ïç—…Ö∞ÅÖπÖ±ÂÕ•ÃÅÕ°Ω’±êÅâîÅπΩ…µÖ±•Èïêà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ÖπÖ±ÂÕ•Õ’…Ö—•Ω∏ÄÅÕ—êËÈç°…ΩπºËÈÕïçΩπëÃ†‘§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâΩπîµÕïçΩπêÅâÖç≠ù…Ω’πêÅÖπÖ±ÂÕ•ÃÅÕ°Ω’±êÅ…ïµÖ•∏ÅâΩ’πëïêà§Ï((ÄÄÄÅçΩπÕ—ï·¡»ÅÖ’—ºÅ—ïÕ—MÖµ¡±ïIÖ—îÄÙÄƒ¿¿¿∏¿Ï(ÄÄÄÅ1ΩΩ¡πù•πîÅïπù•πîÏ(ÄÄÄÅïπù•πîπ¡…ï¡Ö…î°—ïÕ—MÖµ¡±ïIÖ—î∞Äÿ–∞Ä»§Ï(ÄÄÄÅïπù•πîπÕï—1ΩΩ¡1ïπù—°MïçΩπëÃ†¿∏—ò§Ï(ÄÄÄÅïπù•πîπÕï—…ΩÕÕôÖëï5•±±•ÕïçΩπëÃ†‘∏¡ò§Ï(ÄÄÄÅïπù•πîπâïù•πÖ¡—’…î†§Ï((ÄÄÄÅ©’çîËÈ’ë•Ω	’ôôï»Òô±ΩÖ–¯ÅçÖ¡—’…î†»∞Ä–¿¿§Ï(ÄÄÄÅôΩ»Ä°•π–ÅÕÖµ¡±îÄÙÄ¿ÏÅÕÖµ¡±îÄÅçÖ¡—’…îπùï—9’µMÖµ¡±ïÃ†§ÏÄ¨≠ÕÖµ¡±î§(ÄÄÄÅÏ(ÄÄÄÄÄÄÄÅçΩπÕ–ÅÖ’—ºÅ¡°ÖÕîÄÙÅ©’çîËÈ5Ö—°ΩπÕ—Öπ—ÃÒô±ΩÖ–¯ËÈ—›ΩA§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ®ÅÕ—Ö—•ç}çÖÕ–Òô±ΩÖ–¯°ÕÖµ¡±îÄîÄƒ¿¿§ÄºÄƒ¿¿∏¡òÏ(ÄÄÄÄÄÄÄÅçÖ¡—’…îπÕï—MÖµ¡±î†¿∞ÅÕÖµ¡±î∞ÅÕ—êËÈÕ•∏°¡°ÖÕî§§Ï(ÄÄÄÄÄÄÄÅçÖ¡—’…îπÕï—MÖµ¡±î†ƒ∞ÅÕÖµ¡±î∞Ä¿∏·òÄ®ÅÕ—êËÈÕ•∏°¡°ÖÕîÄ¨Ä¿∏¿·ò§§Ï(ÄÄÄÅÙ((ÄÄÄÅïπù•πîπ¡…ΩçïÕÃ°çÖ¡—’…î∞Äƒ∏¡ò§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πîπùï—M—Ö—î†§ÄÙÙÅ1ΩΩ¡πù•πîËÈM—Ö—îËÈÖπÖ±ÂÕ•πú(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÅÒÅïπù•πîπùï—M—Ö—î†§ÄÙÙÅ1ΩΩ¡πù•πîËÈM—Ö—îËÈ…ïÖë‰∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâçÖ¡—’…îÅÕ°Ω’±êÅ—…ÖπÕ•—•Ω∏Å—ºÅâÖç≠ù…Ω’πêÅÖπÖ±ÂÕ•Ãà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°›Ö•—Ω…IïÖë‰°ïπù•πî§∞ÄâÖπÖ±ÂÕ•ÃÅÕ°Ω’±êÅ¡’â±•Õ†ÅÑÅ±ΩΩ¿à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πîπùï—Ö¡—’…ïëMÖµ¡±ïΩ’π–†§Ä¯Ä¿∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÖπÖ±ÂÕ•ÃÅÕ°Ω’±êÅÕï±ïç–ÅÑÅπΩ∏µïµ¡—‰Å±ΩΩ¿à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πîπùï—Ö¡—’…ïA…Ωù…ïÕÃ†§ÄÙÙÄƒ∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâçÖ¡—’…îÅ¡…Ωù…ïÕÃÅÕ°Ω’±êÅçΩµ¡±ï—îà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πîπùï—MïÖµE’Ö±•—‰†§Ä¯ÙÄ¿∏¡òÄòòÅïπù•πîπùï—MïÖµE’Ö±•—‰†§ÄÙÄƒ¿¿∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâΩŸï…Ö±∞ÅÕïÖ¥Å≈’Ö±•—‰ÅÕ°Ω’±êÅâîÅπΩ…µÖ±•Èïêà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πîπùï—M¡ïç—…’µMçΩ…î†§Ä¯ÙÄ¿∏¡òÄòòÅïπù•πîπùï—M¡ïç—…’µMçΩ…î†§ÄÙÄƒ¿¿∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÕ¡ïç—…’¥ÅÕçΩ…îÅÕ°Ω’±êÅâîÅπΩ…µÖ±•Èïêà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πîπùï—A°ÖÕïMçΩ…î†§Ä¯ÙÄ¿∏¡òÄòòÅïπù•πîπùï—A°ÖÕïMçΩ…î†§ÄÙÄƒ¿¿∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ¡°ÖÕîÅÕçΩ…îÅÕ°Ω’±êÅâîÅπΩ…µÖ±•Èïêà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πîπùï—M—ï…ïΩMçΩ…î†§Ä¯ÙÄ¿∏¡òÄòòÅïπù•πîπùï—M—ï…ïΩMçΩ…î†§ÄÙÄƒ¿¿∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÕ—ï…ïºÅÕçΩ…îÅÕ°Ω’±êÅâîÅπΩ…µÖ±•Èïêà§Ï((ÄÄÄÅçΩπÕ–ÅÖ’—ºÅ…ïπëï…ïêÄÙÅïπù•πîπç…ïÖ—ïIïπëï…ïë1ΩΩ¿†§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïπëï…ïêπùï—9’µMÖµ¡±ïÃ†§Ä¯Ä¿∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâÕïÖ¥Å…ï¡Ö•»ÅÕ°Ω’±êÅ¡…Ωë’çîÅï·¡Ω…—Öâ±îÅ±ΩΩ¿ÅÖ’ë•ºà§Ï(ÄÄÄÅ•òÄ°…ïπëï…ïêπùï—9’µMÖµ¡±ïÃ†§Ä¯Äƒ§(ÄÄÄÅÏ(ÄÄÄÄÄÄÄÅçΩπÕ–ÅÖ’—ºÅâΩ’πëÖ…Â)’µ¿ÄÙÅÕ—êËÈÖâÃ°…ïπëï…ïêπùï—MÖµ¡±î†¿∞Å…ïπëï…ïêπùï—9’µMÖµ¡±ïÃ†§Ä¥Äƒ§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¥Å…ïπëï…ïêπùï—MÖµ¡±î†¿∞Ä¿§§Ï(ÄÄÄÄÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°âΩ’πëÖ…Â)’µ¿ÄÄ¿∏»’ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ…ïπëï…ïêÅ±ΩΩ¿ÅâΩ’πëÖ…‰ÅÕ°Ω’±êÅπΩ–ÅçΩπ—Ö•∏ÅÖ∏ÅΩâŸ•Ω’ÃÅ©’µ¿à§Ï(ÄÄÄÅÙ((ÄÄÄÅ©’çîËÈ’ë•Ω	’ôôï»Òô±ΩÖ–¯ÅÕ—Ω¡¡ïëA±ÖÂâÖç¨†»∞Äÿ–§Ï(ÄÄÄÅÕ—Ω¡¡ïëA±ÖÂâÖç¨πç±ïÖ»†§Ï(ÄÄÄÅïπù•πîπ¡…ΩçïÕÃ°Õ—Ω¡¡ïëA±ÖÂâÖç¨∞Äƒ∏¡ò§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°Õ—Ω¡¡ïëA±ÖÂâÖç¨πùï—5Öùπ•—’ëî†¿∞Ä¿∞Äÿ–§ÄÙÙÄ¿∏¡ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ¡…ïŸ•ï‹ÅÕ°Ω’±êÅ…ïµÖ•∏ÅÕ—Ω¡¡ïêÅ’π—•∞Å—°îÅ’Õï»Åï·¡±•ç•—±‰ÅÕ—Ö…—ÃÅ•–à§Ï((ÄÄÄÅïπù•πîπÕï—A…ïŸ•ï›A±ÖÂ•πú°—…’î§Ï(ÄÄÄÅ©’çîËÈ’ë•Ω	’ôôï»Òô±ΩÖ–¯Å¡±ÖÂâÖç¨†»∞Äÿ–§Ï(ÄÄÄÅ¡±ÖÂâÖç¨πç±ïÖ»†§Ï(ÄÄÄÅïπù•πîπ¡…ΩçïÕÃ°¡±ÖÂâÖç¨∞Äƒ∏¡ò§Ï(ÄÄÄÅâΩΩ∞ÅçΩπ—Ö•πÕM•ùπÖ∞ÄÙÅôÖ±ÕîÏ(ÄÄÄÅôΩ»Ä°•π–ÅÕÖµ¡±îÄÙÄ¿ÏÅÕÖµ¡±îÄÅ¡±ÖÂâÖç¨πùï—9’µMÖµ¡±ïÃ†§ÏÄ¨≠ÕÖµ¡±î§(ÄÄÄÅÏ(ÄÄÄÄÄÄÄÅçΩπÕ–ÅÖ’—ºÅŸÖ±’îÄÙÅ¡±ÖÂâÖç¨πùï—MÖµ¡±î†¿∞ÅÕÖµ¡±î§Ï(ÄÄÄÄÄÄÄÅçΩπ—Ö•πÕM•ùπÖ∞ÄÙÅçΩπ—Ö•πÕM•ùπÖ∞ÅÒÅÕ—êËÈÖâÃ°ŸÖ±’î§Ä¯Äƒ∏¡î¥’òÏ(ÄÄÄÄÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°Õ—êËÈ•Õô•π•—î°ŸÖ±’î§∞Äâ¡±ÖÂâÖç¨Åµ’Õ–ÅçΩπ—Ö•∏Åô•π•—îÅÕÖµ¡±ïÃà§Ï(ÄÄÄÅÙ(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°çΩπ—Ö•πÕM•ùπÖ∞∞Äâ¡±ÖÂâÖç¨ÅÕ°Ω’±êÅïµ•–Å—°îÅÕï±ïç—ïêÅ±ΩΩ¿à§Ï(ÄÄÄÅ•òÄ°…ïπëï…ïêπùï—9’µMÖµ¡±ïÃ†§Ä¯ÙÅ¡±ÖÂâÖç¨πùï—9’µMÖµ¡±ïÃ†§§(ÄÄÄÅÏ(ÄÄÄÄÄÄÄÅëΩ’â±îÅ¡…ïŸ•ï›·¡Ω…—……Ω»ÄÙÄ¿∏¿Ï(ÄÄÄÄÄÄÄÅôΩ»Ä°•π–Åç°Öππï∞ÄÙÄ¿ÏÅç°Öππï∞ÄÅ¡±ÖÂâÖç¨πùï—9’µ°Öππï±Ã†§ÏÄ¨≠ç°Öππï∞§(ÄÄÄÄÄÄÄÅÏ(ÄÄÄÄÄÄÄÄÄÄÄÅôΩ»Ä°•π–ÅÕÖµ¡±îÄÙÄ¿ÏÅÕÖµ¡±îÄÅ¡±ÖÂâÖç¨πùï—9’µMÖµ¡±ïÃ†§ÏÄ¨≠ÕÖµ¡±î§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÅ¡…ïŸ•ï›·¡Ω…—……Ω»Ä¨ÙÅÕ—êËÈÖâÃ°¡±ÖÂâÖç¨πùï—MÖµ¡±î°ç°Öππï∞∞ÅÕÖµ¡±î§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¥Å…ïπëï…ïêπùï—MÖµ¡±î°ç°Öππï∞∞ÅÕÖµ¡±î§§Ï(ÄÄÄÄÄÄÄÅÙ(ÄÄÄÄÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°¡…ïŸ•ï›·¡Ω…—……Ω»ÄÄƒ∏¡î¥‘∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ¡…ïŸ•ï‹ÅÕ°Ω’±êÅÕ—Ö…–ÅΩ∏Å—°îÅï·Öç–ÅÕÖµîÅÕÖµ¡±ïÃÅÖÃÅï·¡Ω…—ïêÅÖ’ë•ºà§Ï(ÄÄÄÅÙ(ÄÄÄÅ•òÄ†ÖçΩπ—Ö•πÕM•ùπÖ∞§(ÄÄÄÅÏ(ÄÄÄÄÄÄÄÅçΩπÕ–ÅÖ’—ºÅë•ÖùπΩÕ—•ç1ΩΩ¿ÄÙÅïπù•πîπç…ïÖ—ïIïπëï…ïë1ΩΩ¿†§Ï(ÄÄÄÄÄÄÄÅÕ—êËÈçï…»ÄÄâ¡±ÖÂâÖç¨Åë•ÖùπΩÕ—•åËÅÕÖµ¡±ïÃÙàÄÅïπù•πîπùï—Ö¡—’…ïëMÖµ¡±ïΩ’π–†§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄàÅÕïÖ¥ÙàÄÅïπù•πîπùï—MïÖµE’Ö±•—‰†§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄàÅ…ïπëï…ïêÙàÄÄ°ë•ÖùπΩÕ—•ç1ΩΩ¿πùï—9’µMÖµ¡±ïÃ†§Ä¯Ä¿(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¸Åë•ÖùπΩÕ—•ç1ΩΩ¿πùï—5Öùπ•—’ëî†¿∞Ä¿∞Åë•ÖùπΩÕ—•ç1ΩΩ¿πùï—9’µMÖµ¡±ïÃ†§§ÄËÄ¥ƒ∏¡ò§(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄùq∏úÏ(ÄÄÄÅÙ((ÄÄÄÅïπù•πîπÕï—A…ïŸ•ï›A±ÖÂ•πú°ôÖ±Õî§Ï(ÄÄÄÅ©’çîËÈ’ë•Ω	’ôôï»Òô±ΩÖ–¯Åë…Âô—ï…M—Ω¿†»∞Äÿ–§Ï(ÄÄÄÅë…Âô—ï…M—Ω¿πç±ïÖ»†§Ï(ÄÄÄÅë…Âô—ï…M—Ω¿πÖëëMÖµ¡±î†¿∞Ä‹∞Ä¿∏»’ò§Ï(ÄÄÄÅë…Âô—ï…M—Ω¿πÖëëMÖµ¡±î†ƒ∞Äƒƒ∞Ä¥¿∏…ò§Ï(ÄÄÄÅïπù•πîπ¡…ΩçïÕÃ°ë…Âô—ï…M—Ω¿∞Äƒ∏¡ò§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°Õ—êËÈÖâÃ°ë…Âô—ï…M—Ω¿πùï—MÖµ¡±î†¿∞Ä‹§Ä¥Ä¿∏»’ò§ÄÄƒ∏¡î¥›ò(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄòòÅÕ—êËÈÖâÃ°ë…Âô—ï…M—Ω¿πùï—MÖµ¡±î†ƒ∞Äƒƒ§Ä¨Ä¿∏…ò§ÄÄƒ∏¡î¥›ò∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâM—Ω¿ÅÕ°Ω’±êÅ•µµïë•Ö—ï±‰Å…ïÕ—Ω…îÅ’π—Ω’ç°ïêÅ\Å•π¡’–à§Ï((ÄÄÄÅçΩπÕ–ÅÖ’—ºÅÕÖŸïëM—Ö—îÄÙÅïπù•πîπç…ïÖ—ï1ΩΩ¡M—Ö—î†§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–†ÖÕÖŸïëM—Ö—îπ•Õµ¡—‰†§∞ÄâçÖ¡—’…ïêÅ±ΩΩ¿ÅÕ°Ω’±êÅÕï…•Ö±•Èîà§Ï((ÄÄÄÅ1ΩΩ¡πù•πîÅ…ïÕ—Ω…ïêÏ(ÄÄÄÅ…ïÕ—Ω…ïêπ¡…ï¡Ö…î°—ïÕ—MÖµ¡±ïIÖ—î∞Äÿ–∞Ä»§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÕ—Ω…ïêπ…ïÕ—Ω…ï1ΩΩ¡M—Ö—î°ÕÖŸïëM—Ö—îπùï—Ö—Ñ†§∞ÅÕÖŸïëM—Ö—îπùï—M•Èî†§§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâçÖ¡—’…ïêÅ±ΩΩ¿ÅÕ°Ω’±êÅ…ïÕ—Ω…îà§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÕ—Ω…ïêπùï—M—Ö—î†§ÄÙÙÅ1ΩΩ¡πù•πîËÈM—Ö—îËÈ…ïÖë‰∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ…ïÕ—Ω…ïêÅ±ΩΩ¿ÅÕ°Ω’±êÅâîÅ•µµïë•Ö—ï±‰Å…ïÖë‰à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÕ—Ω…ïêπùï—Ö¡—’…ïëMÖµ¡±ïΩ’π–†§ÄÙÙÅïπù•πîπùï—Ö¡—’…ïëMÖµ¡±ïΩ’π–†§∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ…ïÕ—Ω…ïêÅ±ΩΩ¿Å±ïπù—†ÅÕ°Ω’±êÅµÖ—ç†à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÕ—Ω…ïêπùï—MΩ’…çï9Öµî†§ÄÙÙÄâ\ÅÖ¡—’…îà(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄòòÅ…ïÕ—Ω…ïêπùï—]ÖŸïôΩ…µA…ïŸ•ï‹†§πÕ•Èî†§ÄÙÙÄÃ»¿∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ\Å¡…Ω©ïç–Å…ïçÖ±∞ÅÕ°Ω’±êÅ…ïÕ—Ω…îÅ—°îÅÕΩ’…çîÅÖπêÅïë•—Öâ±îÅ…ÖπùîÅçΩπ—ï·–à§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°…ïÕ—Ω…ïêπùï—M•ùπÖ±MπÖ¡Õ°Ω–†§πŸÖ±•ê∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ\Å¡…Ω©ïç–Å…ïçÖ±∞ÅÕ°Ω’±êÅ…ïÕ—Ω…îÅÕ¡ïç—…’¥∞Å¡°ÖÕîÅÖπêÅçΩ……ï±Ö—•Ω∏ÅÖπÖ±ÂÕ•Ãà§Ï((ÄÄÄÅïπù•πîπç±ïÖ»†§Ï(ÄÄÄÅ¡±ÖÂâÖç¨πç±ïÖ»†§Ï(ÄÄÄÅïπù•πîπ¡…ΩçïÕÃ°¡±ÖÂâÖç¨∞Äƒ∏¡ò§Ï(ÄÄÄÅ¡ÖÕÕïêÄòÙÅï·¡ïç–°ïπù•πîπùï—M—Ö—î†§ÄÙÙÅ1ΩΩ¡πù•πîËÈM—Ö—îËÈÕΩ’…çïIïÖë‰∞(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄâ±ïÖ»ÅIïÕ’±–ÅÕ°Ω’±êÅ…ï—Ö•∏Å—°îÅÕΩ’…çîÅôΩ»ÅÖπΩ—°ï»Åùïπï…Ö—•Ω∏à§Ï((ÄÄÄÅ•òÄ†Ö¡ÖÕÕïê§(ÄÄÄÄÄÄÄÅ…ï—’…∏ÄƒÏ((ÄÄÄÅÕ—êËÈçΩ’–ÄÄâ1ΩΩ¡M’…ùïΩ∏Åïπù•πîÅ—ïÕ—ÃÅ¡ÖÕÕïêÏÄ–‡Å≠!ËÅÖπÖ±ÂÕ•ÃÅ—ΩΩ¨Äà(ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÅÖπÖ±ÂÕ•Õ’…Ö—•Ω∏πçΩ’π–†§ÄÄàÅµÕq∏àÏ(ÄÄÄÅ…ï—’…∏Ä¿Ï)Ù