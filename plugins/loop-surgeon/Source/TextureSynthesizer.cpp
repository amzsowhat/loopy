#include "TextureSynthesizer.h"

#include "SignalDiagnostics.h"
#include "TextureMaterialModel.h"

TextureSynthesisResult TextureSynthesizer::synthesize(
    const juce::AudioBuffer<float>& source, const double sampleRate,
    TextureSynthesisSettings settings)
{
    TextureSynthesisResult result;
    if (source.getNumChannels() <= 0 || source.getNumSamples() < 32 || sampleRate <= 0.0)
        return result;

    const auto targetSamples = juce::jmax(
        32, juce::roundToInt(sampleRate * settings.durationSeconds));
    auto rendered = TextureMaterialModel::render(
        source, sampleRate, targetSamples, settings);
    result.audio = std::move(rendered.audio);
    result.analysisFrameStarts = std::move(rendered.analysisFrameStarts);
    result.usedStructure = settings.structure;
    if (result.audio.getNumSamples() == 0)
        return result;

    result.containsOnlyFiniteSamples = SignalDiagnostics::repairNonFiniteAndRemoveDc(result.audio);
    result.truePeakDbtp = SignalDiagnostics::applyCircularTruePeakCeiling(
        result.audio, -1.0f);
    return result;
}

