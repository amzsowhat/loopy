#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "LoopAnalyzer.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class LoopEngine
{
public:
    enum class State
    {
        empty,
        armed,
        capturing,
        analysing,
        ready
    };

    LoopEngine() = default;
    ~LoopEngine();

    void prepare(double newSampleRate, int maximumBlockSize, int channelCount);
    void releaseResources();

    void setLoopLengthSeconds(float seconds) noexcept;
    void setCrossfadeMilliseconds(float milliseconds) noexcept;

    void beginCapture(int startDelaySamples = 0) noexcept;
    void clear() noexcept;
    void process(juce::AudioBuffer<float>& buffer, float wetMix) noexcept;

    [[nodiscard]] State getState() const noexcept;
    [[nodiscard]] float getCaptureProgress() const noexcept;
    [[nodiscard]] float getSeamQuality() const noexcept;
    [[nodiscard]] float getLevelScore() const noexcept;
    [[nodiscard]] float getSlopeScore() const noexcept;
    [[nodiscard]] float getSpectrumScore() const noexcept;
    [[nodiscard]] float getPhaseScore() const noexcept;
    [[nodiscard]] float getStereoScore() const noexcept;
    [[nodiscard]] int getCapturedSampleCount() const noexcept;

    [[nodiscard]] juce::MemoryBlock createLoopState() const;
    bool restoreLoopState(const void* data, size_t size);

private:
    void applyPendingRequests() noexcept;
    void finishCapture() noexcept;
    [[nodiscard]] float readLoopSample(int channel) const noexcept;
    void advancePlaybackPosition() noexcept;
    void startAnalysisThread();
    void stopAnalysisThread();
    void analysisLoop();
    void resetScores() noexcept;

    static constexpr float maximumLoopSeconds = 16.0f;
    static constexpr float searchRadiusSeconds = 0.15f;
    static constexpr int stateMagic = 0x4c535032;
    static constexpr int stateVersion = 1;

    juce::AudioBuffer<float> captureBuffer;
    juce::AudioBuffer<float> loopBuffer;
    mutable std::mutex loopDataMutex;
    double sampleRate = 44100.0;
    float loopLengthSeconds = 4.0f;
    float crossfadeMilliseconds = 25.0f;
    int captureWritePosition = 0;
    int scheduledCaptureDelay = 0;
    int captureSampleCount = 0;
    int requestedLoopSamples = 0;
    int searchRadiusSamples = 0;
    int playbackPosition = 0;
    std::atomic<int> capturedSampleCount { 0 };
    std::atomic<int> activeAudioReaders { 0 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;

    std::thread analysisThread;
    std::mutex analysisWaitMutex;
    std::condition_variable analysisCondition;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> analysisPending { false };
    std::atomic<uint64_t> generation { 0 };

    std::atomic<bool> captureRequested { false };
    std::atomic<int> requestedStartDelay { 0 };
    std::atomic<bool> clearRequested { false };
    std::atomic<State> state { State::empty };
    std::atomic<float> captureProgress { 0.0f };
    std::atomic<float> seamQuality { 0.0f };
    std::atomic<float> levelScore { 0.0f };
    std::atomic<float> slopeScore { 0.0f };
    std::atomic<float> spectrumScore { 0.0f };
    std::atomic<float> phaseScore { 0.0f };
    std::atomic<float> stereoScore { 0.0f };
};
