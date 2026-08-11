#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

enum class SpectralOrbitCurve
{
    sweep,
    fold,
    barberpole
};

struct SpectralOrbitSettings
{
    float requestedDurationSeconds = 24.0f;
    float maximumDelayRatio = 0.72f;
    float feedback = 0.78f;
    float diffusionFrames = 1.4f;
    int feedbackLaps = 5;
    SpectralOrbitCurve curve = SpectralOrbitCurve::sweep;
};

struct SpectralOrbitResult
{
    juce::AudioBuffer<float> audio;
    double actualDurationSeconds = 0.0;
    float truePeakDbtp = -100.0f;
    float boundarySampleDelta = 0.0f;
    float boundarySlopeDelta = 0.0f;
    bool containedNonFiniteInput = false;
};

// Offline research renderer. It is deliberately not connected to the plug-in UI or audio thread.
// Complex source spectra travel through a closed time ring according to one explicit frequency to
// delay curve. No source clip is tiled and no FFT-bin phase is randomized.
class SpectralOrbitPrototype
{
public:
    [[nodiscard]] static SpectralOrbitResult render(
        const juce::AudioBuffer<float>& source,
        double sampleRate,
        const SpectralOrbitSettings& settings);
};

