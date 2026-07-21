#include "LoopEngine.h"

#include <algorithm>
#include <cmath>

void LoopEngine::prepare(const double newSampleRate,
                         const int maximumBlockSize,
                         const int channelCount)
{
    juce::ignoreUnused(maximumBlockSize);
    sampleRate = juce::jmax(1.0, newSampleRate);
    const auto maximumSamples = juce::roundToInt(sampleRate * maximumLoopSeconds);
    loopBuffer.setSize(juce::jmax(1, channelCount), maximumSamples, false, true, false);
    loopBuffer.clear();
    captureWritePosition = 0;
    playbackPosition = 0;
    capturedSampleCount = 0;
    state.store(State::empty);
    captureProgress.store(0.0f);
    seamQuality.store(0.0f);
}

void LoopEngine::releaseResources()
{
    loopBuffer.setSize(0, 0);
    capturedSampleCount = 0;
    state.store(State::empty);
}

void LoopEngine::setLoopLengthSeconds(const float seconds) noexcept
{
    loopLengthSeconds = juce::jlimit(0.01f, maximumLoopSeconds, seconds);
}

void LoopEngine::setCrossfadeMilliseconds(const float milliseconds) noexcept
{
    crossfadeMilliseconds = juce::jlimit(0.0f, 250.0f, milliseconds);
}

void LoopEngine::beginCapture() noexcept
{
    captureRequested.store(true, std::memory_order_release);
}

void LoopEngine::clear() noexcept
{
    clearRequested.store(true, std::memory_order_release);
}

void LoopEngine::process(juce::AudioBuffer<float>& buffer, const float wetMix) noexcept
{
    applyPendingRequests();

    if (loopBuffer.getNumSamples() == 0)
        return;

    const auto mix = juce::jlimit(0.0f, 1.0f, wetMix);
    const auto channelCount = juce::jmin(buffer.getNumChannels(), loopBuffer.getNumChannels());

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto currentState = state.load(std::memory_order_relaxed);

        if (currentState == State::capturing)
        {
            for (int channel = 0; channel < channelCount; ++channel)
                loopBuffer.setSample(channel, captureWritePosition, buffer.getSample(channel, sample));

            ++captureWritePosition;
            captureProgress.store(static_cast<float>(captureWritePosition)
                                      / static_cast<float>(juce::jmax(1, capturedSampleCount)),
                                  std::memory_order_relaxed);

            if (captureWritePosition >= capturedSampleCount)
                finishCapture();

            continue;
        }

        if (currentState != State::ready || capturedSampleCount <= 0)
            continue;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto dry = buffer.getSample(channel, sample);
            const auto wet = readLoopSample(channel);
            buffer.setSample(channel, sample, dry + mix * (wet - dry));
        }

        advancePlaybackPosition();
    }
}

LoopEngine::State LoopEngine::getState() const noexcept
{
    return state.load(std::memory_order_acquire);
}

float LoopEngine::getCaptureProgress() const noexcept
{
    return captureProgress.load(std::memory_order_relaxed);
}

float LoopEngine::getSeamQuality() const noexcept
{
    return seamQuality.load(std::memory_order_relaxed);
}

int LoopEngine::getCapturedSampleCount() const noexcept
{
    return capturedSampleCount;
}

void LoopEngine::applyPendingRequests() noexcept
{
    if (clearRequested.exchange(false, std::memory_order_acq_rel))
    {
        loopBuffer.clear();
        captureWritePosition = 0;
        playbackPosition = 0;
        capturedSampleCount = 0;
        captureProgress.store(0.0f);
        seamQuality.store(0.0f);
        state.store(State::empty, std::memory_order_release);
    }

    if (captureRequested.exchange(false, std::memory_order_acq_rel))
    {
        loopBuffer.clear();
        capturedSampleCount = juce::jlimit(1,
                                           loopBuffer.getNumSamples(),
                                           juce::roundToInt(sampleRate * loopLengthSeconds));
        captureWritePosition = 0;
        playbackPosition = 0;
        captureProgress.store(0.0f);
        seamQuality.store(0.0f);
        state.store(State::capturing, std::memory_order_release);
    }
}

void LoopEngine::finishCapture() noexcept
{
    captureWritePosition = capturedSampleCount;
    playbackPosition = 0;
    captureProgress.store(1.0f);
    updateSeamQuality();
    state.store(State::ready, std::memory_order_release);
}

float LoopEngine::readLoopSample(const int channel) const noexcept
{
    const auto maximumCrossfade = juce::jmax(0, capturedSampleCount / 2);
    const auto crossfadeSamples = juce::jlimit(
        0,
        maximumCrossfade,
        juce::roundToInt(sampleRate * crossfadeMilliseconds * 0.001));

    if (crossfadeSamples == 0 || playbackPosition < capturedSampleCount - crossfadeSamples)
        return loopBuffer.getSample(channel, playbackPosition);

    const auto crossfadeIndex = playbackPosition - (capturedSampleCount - crossfadeSamples);
    const auto alpha = static_cast<float>(crossfadeIndex + 1)
                       / static_cast<float>(crossfadeSamples);
    const auto tail = loopBuffer.getSample(channel, playbackPosition);
    const auto head = loopBuffer.getSample(channel, crossfadeIndex);
    return tail + alpha * (head - tail);
}

void LoopEngine::advancePlaybackPosition() noexcept
{
    ++playbackPosition;

    if (playbackPosition < capturedSampleCount)
        return;

    const auto crossfadeSamples = juce::jlimit(
        0,
        capturedSampleCount / 2,
        juce::roundToInt(sampleRate * crossfadeMilliseconds * 0.001));
    playbackPosition = crossfadeSamples;
}

void LoopEngine::updateSeamQuality() noexcept
{
    if (capturedSampleCount < 2 || loopBuffer.getNumChannels() == 0)
    {
        seamQuality.store(0.0f);
        return;
    }

    const auto requestedWindow = juce::roundToInt(sampleRate * crossfadeMilliseconds * 0.001);
    const auto window = juce::jlimit(1, capturedSampleCount / 2, juce::jmax(1, requestedWindow));
    double differenceEnergy = 0.0;
    double signalEnergy = 0.0;
    int valueCount = 0;

    for (int channel = 0; channel < loopBuffer.getNumChannels(); ++channel)
    {
        for (int index = 0; index < window; ++index)
        {
            const auto head = static_cast<double>(loopBuffer.getSample(channel, index));
            const auto tail = static_cast<double>(
                loopBuffer.getSample(channel, capturedSampleCount - window + index));
            const auto difference = tail - head;
            differenceEnergy += difference * difference;
            signalEnergy += 0.5 * (head * head + tail * tail);
            ++valueCount;
        }
    }

    const auto count = static_cast<double>(juce::jmax(1, valueCount));
    const auto differenceRms = std::sqrt(differenceEnergy / count);
    const auto signalRms = std::sqrt(signalEnergy / count);
    const auto relativeError = differenceRms / juce::jmax(1.0e-9, signalRms);
    const auto quality = 100.0 * (1.0 - juce::jlimit(0.0, 1.0, relativeError));
    seamQuality.store(static_cast<float>(quality), std::memory_order_relaxed);
}

