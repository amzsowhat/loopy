#pragma once

#include "TextureSynthesizer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace TextureMaterialModel
{
struct RenderedMaterial
{
    juce::AudioBuffer<float> audio;
    std::vector<int> analysisFrameStarts;
};

// Offline-only analysis and resynthesis. Source samples are analysed into a spectral material
// model and are never copied, reversed, tiled, or scheduled onto the output timeline.
[[nodiscard]] RenderedMaterial render(const juce::AudioBuffer<float>& source,
                                      double sampleRate,
                                      int targetSamples,
                                      const TextureSynthesisSettings& settings);
}
