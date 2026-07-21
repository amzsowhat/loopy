#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

class LoopEngine
{
public:
    enum class State
    {
        empty,
        capturing,
        ready
    };

    void prepare(double newSampleRate, int maximumBlockSize, int channelCount);
    void releaseResources();

    void setLoopLengthSeconds(float seconds) noexcept;
    void setCrossfadeMilliseconds(float milliseconds) noexcept;

    void beginCapture() noexcept;
    void clear() noexcept;
    void process(juce::AudioBuffer<float>& buffer, float wetMix) noexcept;

    [[nodiscard]] State getState() const noexcept;
    [[nodiscard]] float getCaptureProgress() const noexcept;
    [[nodiscard]] float getSeamQuality() const noexcept;
    [[nodiscard]] int getCapturedSampleCount() const noexcept;

private:
    void applyPendingRequests() noexcept;
    void finishCapture() noexcept;
    [[nodiscard]] float readLoopSample(int channel) const noexcept;
    void advancePlaybackPosition() noexcept;
    void updateSeamQuality() noexcept;

    static constexpr float maximumLoopSeconds = 16.0f;

    juce::AudioBuffer<float> loopBuffer;
    double sampleRate = 44100.0;
    float loopLengthSeconds = 4.0f;
    float crossfadeMilliseconds = 25.0f;
    int captureWritePosition = 0;
    int playbackPosition = 0;
    int capturedSampleCount = 0;

    std::atomic<bool> captureRequested { false };
    std::atomic<bool> clearRequested { false };
    std::atomic<State> state { State::empty };
    std::atomic<float> captureProgress { 0.0f };
    std::atomic<float> seamQuality { 0.0f };
};

