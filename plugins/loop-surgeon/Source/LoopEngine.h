#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "LoopAnalyzer.h"
#include "RenderQuality.h"
#include "TextureSynthesizer.h"

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
        sourceReady,
        armed,
        capturing,
        analysing,
        ready,
        failed
    };

    enum class PreviewMode { original, loop };
    enum class GenerationMode { rotateRepair = 0, textureLoop = 1 };

    LoopEngine() = default;
    ~LoopEngine();

    void prepare(double newSampleRate, int maximumBlockSize, int channelCount);
    void releaseResources();

    void setLoopLengthSeconds(float seconds) noexcept;
    void setCrossfadeMilliseconds(float milliseconds) noexcept;
    void setRepairDurationSeconds(float seconds) noexcept;
    void setTextureDurationSeconds(float seconds) noexcept;
    void setTextureVariation(float amount) noexcept;
    void setTextureFlatten(float amount) noexcept;
    void setTextureSourceMatch(float amount) noexcept;
    void setGenerationMode(GenerationMode mode) noexcept { generationMode.store(mode); }
    [[nodiscard]] GenerationMode getGenerationMode() const noexcept
    {
        return generationMode.load();
    }
    [[nodiscard]] GenerationMode getLastUsedGenerationMode() const noexcept
    {
        return lastUsedGenerationMode.load();
    }
    bool regenerateTexture(float startProportion, float endProportion);
    void setPreviewMode(PreviewMode mode) noexcept { previewMode.store(mode); }
    [[nodiscard]] PreviewMode getPreviewMode() const noexcept { return previewMode.load(); }
    void setPreviewPlaying(bool shouldPlay) noexcept;
    [[nodiscard]] bool isPreviewPlaying() const noexcept { return previewPlaying.load(); }

    void beginCapture(int startDelaySamples = 0) noexcept;
    void submitSource(juce::AudioBuffer<float> source, juce::String sourceName);
    bool reanalyzeSourceRange(float startProportion, float endProportion);
    bool setManualRotationPoint(float proportion);
    void clear();
    void process(juce::AudioBuffer<float>& buffer, float wetMix) noexcept;

    [[nodiscard]] State getState() const noexcept;
    [[nodiscard]] float getCaptureProgress() const noexcept;
    [[nodiscard]] float getAnalysisProgress() const noexcept
    {
        return analysisProgress.load(std::memory_order_relaxed);
    }
    [[nodiscard]] float getSeamQuality() const noexcept;
    [[nodiscard]] float getWaveformScore() const noexcept;
    [[nodiscard]] float getLevelScore() const noexcept;
    [[nodiscard]] float getSlopeScore() const noexcept;
    [[nodiscard]] float getSpectrumScore() const noexcept;
    [[nodiscard]] float getPhaseScore() const noexcept;
    [[nodiscard]] float getStereoScore() const noexcept;
    [[nodiscard]] float getTransientScore() const noexcept;
    [[nodiscard]] float getPeriodicityScore() const noexcept;
    [[nodiscard]] float getRepairScore() const noexcept;
    [[nodiscard]] float getRepeatSafetyScore() const noexcept;
    [[nodiscard]] float getTruePeakDbtp() const noexcept;
    [[nodiscard]] float getRenderQualityScore() const noexcept;
    [[nodiscard]] bool hasPassedQualityGate() const noexcept;
    [[nodiscard]] bool isLowConfidence() const noexcept;
    [[nodiscard]] int getCapturedSampleCount() const noexcept;
    [[nodiscard]] juce::String getSourceName() const;
    [[nodiscard]] double getSourceDurationSeconds() const;
    [[nodiscard]] double getRenderedDurationSeconds() const noexcept
    {
        return static_cast<double>(capturedSampleCount.load()) / sampleRate;
    }
    [[nodiscard]] int getCandidateCount() const;
    [[nodiscard]] uint64_t getCandidateRevision() const noexcept;
    [[nodiscard]] uint64_t getSourceRevision() const noexcept;
    [[nodiscard]] juce::String getCandidateDescription(int index) const;
    void selectCandidate(int index);
    [[nodiscard]] std::vector<float> getWaveformPreview() const;
    [[nodiscard]] float getRotationProportion() const noexcept;
    [[nodiscard]] float getAnalysisRangeStartProportion() const noexcept;
    [[nodiscard]] float getAnalysisRangeEndProportion() const noexcept;
    [[nodiscard]] juce::AudioBuffer<float> createRenderedLoop() const;
    [[nodiscard]] RenderQuality::SignalSnapshot getSignalSnapshot() const;

    [[nodiscard]] juce::MemoryBlock createLoopState() const;
    bool restoreLoopState(const void* data, size_t size);

private:
    void applyPendingRequests() noexcept;
    void finishCapture() noexcept;
    [[nodiscard]] float readLoopSample(int channel) const noexcept;
    void advancePlaybackPosition() noexcept;
    [[nodiscard]] float readSourceSample(int channel) const noexcept;
    void advanceSourcePlaybackPosition() noexcept;
    void startAnalysisThread();
    void stopAnalysisThread();
    void analysisLoop();
    void resetScores() noexcept;

    static constexpr float maximumLoopSeconds = 16.0f;
    static constexpr float maximumTextureSeconds = 60.0f;
    static constexpr float searchRadiusSeconds = 0.15f;
    static constexpr int stateMagic = 0x4c535032;
    static constexpr int stateVersion = 5;

    juce::AudioBuffer<float> captureBuffer;
    juce::AudioBuffer<float> loopBuffer;
    juce::AudioBuffer<float> pendingSourceBuffer;
    juce::AudioBuffer<float> currentSourceBuffer;
    mutable std::mutex sourceDataMutex;
    juce::String pendingSourceName;
    juce::String currentSourceName;
    std::vector<LoopAnalysisResult> sourceCandidates;
    std::vector<TextureSynthesisResult> textureVariants;
    std::vector<float> waveformPreview;
    int pendingSourceOffset = 0;
    int pendingFullSourceSamples = 0;
    bool pendingReplacesCurrentSource = true;
    mutable std::mutex loopDataMutex;
    mutable std::mutex signalSnapshotMutex;
    RenderQuality::SignalSnapshot signalSnapshot;
    double sampleRate = 44100.0;
    float loopLengthSeconds = 4.0f;
    std::atomic<float> crossfadeMilliseconds { 25.0f };
    // Zero follows the complete Source In/Out selection.
    std::atomic<float> repairDurationSeconds { 0.0f };
    std::atomic<float> textureDurationSeconds { 24.0f };
    std::atomic<float> textureVariation { 0.72f };
    std::atomic<float> textureFlatten { 0.72f };
    std::atomic<float> textureSourceMatch { 0.85f };
    std::atomic<uint32_t> textureSeed { 0x4c535501u };
    std::atomic<int> effectiveCrossfadeSamples { 0 };
    int captureWritePosition = 0;
    int scheduledCaptureDelay = 0;
    int captureSampleCount = 0;
    int requestedLoopSamples = 0;
    int playbackPosition = 0;
    int sourcePlaybackPosition = 0;
    std::atomic<int> capturedSampleCount { 0 };
    std::atomic<int> activeAudioReaders { 0 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;

    std::thread analysisThread;
    std::mutex analysisWaitMutex;
    std::condition_variable analysisCondition;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> analysisPending { false };
    std::atomic<bool> importedAnalysisPending { false };
    std::atomic<uint64_t> generation { 0 };

    std::atomic<bool> captureRequested { false };
    std::atomic<int> requestedStartDelay { 0 };
    std::atomic<State> state { State::empty };
    std::atomic<float> captureProgress { 0.0f };
    std::atomic<float> analysisProgress { 0.0f };
    std::atomic<float> seamQuality { 0.0f };
    std::atomic<float> waveformScore { 0.0f };
    std::atomic<float> levelScore { 0.0f };
    std::atomic<float> slopeScore { 0.0f };
    std::atomic<float> spectrumScore { 0.0f };
    std::atomic<float> phaseScore { 0.0f };
    std::atomic<float> stereoScore { 0.0f };
    std::atomic<float> transientScore { 0.0f };
    std::atomic<float> periodicityScore { 0.0f };
    std::atomic<float> repairScore { 0.0f };
    std::atomic<float> repeatSafetyScore { 0.0f };
    std::atomic<float> truePeakDbtp { -100.0f };
    std::atomic<float> renderQualityScore { 0.0f };
    std::atomic<bool> qualityGatePassed { false };
    std::atomic<bool> lowConfidence { false };
    std::atomic<int> selectedStartSample { 0 };
    std::atomic<int> selectedEndSample { 0 };
    std::atomic<int> selectedRotationSample { -1 };
    std::atomic<int> selectedSourceSamples { 0 };
    std::atomic<int> analysisRangeStartSample { 0 };
    std::atomic<int> analysisRangeEndSample { 0 };
    std::atomic<PreviewMode> previewMode { PreviewMode::loop };
    std::atomic<GenerationMode> generationMode { GenerationMode::rotateRepair };
    std::atomic<GenerationMode> lastUsedGenerationMode { GenerationMode::rotateRepair };
    std::atomic<bool> previewPlaying { false };
    std::atomic<bool> previewRestartRequested { false };
    std::atomic<uint64_t> candidateRevision { 0 };
    std::atomic<uint64_t> sourceRevision { 0 };
    std::atomic<int> activeTextureVariant { -1 };
};
