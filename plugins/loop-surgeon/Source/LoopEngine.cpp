#include "LoopEngine.h"

#include "SignalDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
std::vector<float> buildWaveformPreview(const juce::AudioBuffer<float>& source)
{
    constexpr auto previewBins = 320;
    std::vector<float> preview(previewBins, 0.0f);
    if (source.getNumChannels() == 0 || source.getNumSamples() == 0)
        return preview;
    for (int bin = 0; bin < previewBins; ++bin)
    {
        const auto first = bin * source.getNumSamples() / previewBins;
        const auto last = juce::jmax(
            first + 1, (bin + 1) * source.getNumSamples() / previewBins);
        for (int channel = 0; channel < source.getNumChannels(); ++channel)
            preview[static_cast<size_t>(bin)] = juce::jmax(
                preview[static_cast<size_t>(bin)],
                source.getMagnitude(channel, first, last - first));
    }
    return preview;
}
}

LoopEngine::~LoopEngine()
{
    stopAnalysisThread();
}

void LoopEngine::prepare(const double newSampleRate,
                         const int maximumBlockSize,
                         const int channelCount)
{
    juce::ignoreUnused(maximumBlockSize);
    stopAnalysisThread();

    sampleRate = juce::jmax(1.0, newSampleRate);
    const auto maximumCaptureSamples = juce::roundToInt(
        sampleRate * (maximumLoopSeconds + 2.0f * searchRadiusSeconds));
    captureBuffer.setSize(juce::jmax(1, channelCount), maximumCaptureSamples,
                          false, false, false);
    captureWritePosition = 0;
    playbackPosition = 0;
    capturedSampleCount.store(0);
    state.store(State::empty);
    captureProgress.store(0.0f);
    analysisProgress.store(0.0f);
    resetDiagnostics();
    mixSmoother.reset(sampleRate, 0.02);
    mixSmoother.setCurrentAndTargetValue(1.0f);
    effectiveCrossfadeSamples.store(0);
    selectedRotationSample.store(-1);
    {
        const std::scoped_lock lock(signalSnapshotMutex);
        signalSnapshot = {};
    }
    previewPlaying.store(false);
    previewRestartRequested.store(false);
    startAnalysisThread();
}

void LoopEngine::releaseResources()
{
    stopAnalysisThread();
    captureBuffer.setSize(0, 0);
    {
        const std::scoped_lock lock(loopDataMutex);
        loopBuffer.setSize(0, 0);
    }
    capturedSampleCount.store(0);
    state.store(State::empty);
}

void LoopEngine::setLoopLengthSeconds(const float seconds) noexcept
{
    loopLengthSeconds = juce::jlimit(0.01f, maximumLoopSeconds, seconds);
}

void LoopEngine::setCrossfadeMilliseconds(const float milliseconds) noexcept
{
    crossfadeMilliseconds.store(juce::jlimit(0.0f, 250.0f, milliseconds),
                                std::memory_order_relaxed);
}

void LoopEngine::setRepairDurationSeconds(const float seconds) noexcept
{
    repairDurationSeconds.store(juce::jlimit(0.0f, maximumTextureSeconds, seconds),
                                std::memory_order_relaxed);
}

void LoopEngine::setTextureDurationSeconds(const float seconds) noexcept
{
    textureDurationSeconds.store(
        juce::jlimit(4.0f, maximumTextureSeconds, seconds), std::memory_order_relaxed);
}

void LoopEngine::setTextureVariation(const float amount) noexcept
{
    textureVariation.store(juce::jlimit(0.0f, 1.0f, amount),
                           std::memory_order_relaxed);
}

void LoopEngine::setTextureFlatten(const float amount) noexcept
{
    textureFlatten.store(juce::jlimit(0.0f, 1.0f, amount),
                         std::memory_order_relaxed);
}

void LoopEngine::setTextureSourceMatch(const float amount) noexcept
{
    textureSourceMatch.store(juce::jlimit(0.0f, 1.0f, amount),
                             std::memory_order_relaxed);
}

bool LoopEngine::regenerateTexture(const float startProportion,
                                   const float endProportion)
{
    auto seed = textureSeed.load(std::memory_order_relaxed);
    seed += 0x9e3779b9u;
    if (seed == 0)
        seed = 1;
    textureSeed.store(seed, std::memory_order_relaxed);
    return reanalyzeSourceRange(startProportion, endProportion);
}

void LoopEngine::setPreviewPlaying(const bool shouldPlay) noexcept
{
    if (shouldPlay)
        previewRestartRequested.store(true, std::memory_order_release);
    previewPlaying.store(shouldPlay, std::memory_order_release);
}

void LoopEngine::beginCapture(const int startDelaySamples) noexcept
{
    requestedStartDelay.store(juce::jmax(0, startDelaySamples), std::memory_order_relaxed);
    captureRequested.store(true, std::memory_order_release);
}

void LoopEngine::submitSource(juce::AudioBuffer<float> source, juce::String sourceName)
{
    if (source.getNumChannels() < 1 || source.getNumSamples() < 32)
        return;

    generation.fetch_add(1, std::memory_order_acq_rel);
    analysisPending.store(false, std::memory_order_release);
    importedAnalysisPending.store(false, std::memory_order_release);
    state.store(State::analysing, std::memory_order_release);
    analysisProgress.store(0.0f, std::memory_order_relaxed);
    previewPlaying.store(false, std::memory_order_release);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    {
        const std::scoped_lock lock(loopDataMutex);
        loopBuffer.setSize(0, 0);
    }
    auto sourceSamples = 0;
    {
        const std::scoped_lock lock(sourceDataMutex);
        sourceCandidates.clear();
        textureVariants.clear();
        activeTextureVariant.store(-1, std::memory_order_relaxed);
        currentSourceBuffer = std::move(source);
        currentSourceName = std::move(sourceName);
        waveformPreview = buildWaveformPreview(currentSourceBuffer);
        sourceSamples = currentSourceBuffer.getNumSamples();
        pendingSourceBuffer.setSize(0, 0);
        pendingSourceName.clear();
        pendingSourceOffset = 0;
        pendingFullSourceSamples = 0;
        pendingReplacesCurrentSource = true;
    }
    {
        const std::scoped_lock snapshotLock(signalSnapshotMutex);
        signalSnapshot = {};
    }
    resetDiagnostics();
    capturedSampleCount.store(0);
    effectiveCrossfadeSamples.store(0);
    selectedStartSample.store(0);
    selectedEndSample.store(sourceSamples);
    selectedRotationSample.store(-1);
    selectedSourceSamples.store(sourceSamples);
    analysisRangeStartSample.store(0);
    analysisRangeEndSample.store(sourceSamples);
    sourcePlaybackPosition = 0;
    playbackPosition = 0;
    captureProgress.store(1.0f);
    candidateRevision.fetch_add(1, std::memory_order_release);
    sourceRevision.fetch_add(1, std::memory_order_release);
    state.store(State::sourceReady, std::memory_order_release);
}

bool LoopEngine::reanalyzeSourceRange(const float startProportion, const float endProportion)
{
    generation.fetch_add(1, std::memory_order_acq_rel);
    state.store(State::analysing, std::memory_order_release);
    analysisProgress.store(0.0f, std::memory_order_relaxed);
    previewPlaying.store(false, std::memory_order_release);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    {
        const std::scoped_lock lock(loopDataMutex);
        loopBuffer.setSize(0, 0);
    }

    {
        const std::scoped_lock lock(sourceDataMutex);
        sourceCandidates.clear();
        textureVariants.clear();
        activeTextureVariant.store(-1, std::memory_order_relaxed);
        const auto total = currentSourceBuffer.getNumSamples();
        const auto start = juce::jlimit(0, juce::jmax(0, total - 32),
                                        juce::roundToInt(startProportion * total));
        const auto end = juce::jlimit(start + 32, total,
                                      juce::roundToInt(endProportion * total));
        if (total < 32 || end - start < 32)
        {
            state.store(currentSourceBuffer.getNumSamples() > 0
                            ? State::sourceReady : State::empty,
                        std::memory_order_release);
            return false;
        }
        pendingSourceBuffer.setSize(currentSourceBuffer.getNumChannels(), end - start,
                                    false, false, false);
        for (int channel = 0; channel < currentSourceBuffer.getNumChannels(); ++channel)
            pendingSourceBuffer.copyFrom(channel, 0, currentSourceBuffer, channel, start, end - start);
        pendingSourceName = currentSourceName;
        pendingSourceOffset = start;
        pendingFullSourceSamples = total;
        pendingReplacesCurrentSource = false;
    }
    resetDiagnostics();
    importedAnalysisPending.store(true, std::memory_order_release);
    analysisPending.store(true, std::memory_order_release);
    analysisCondition.notify_one();
    return true;
}

bool LoopEngine::setManualRotationPoint(const float proportion)
{
    generation.fetch_add(1, std::memory_order_acq_rel);
    state.store(State::analysing, std::memory_order_release);
    analysisProgress.store(0.0f, std::memory_order_relaxed);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();

    LoopAnalysisResult result;
    juce::AudioBuffer<float> selectedAudio;
    juce::AudioBuffer<float> snapshotSource;
    {
        const std::scoped_lock sourceLock(sourceDataMutex);
        const auto total = currentSourceBuffer.getNumSamples();
        if (total < 32)
        {
            state.store(State::sourceReady, std::memory_order_release);
            return false;
        }
        const auto rangeStart = juce::jlimit(0, total - 32, analysisRangeStartSample.load());
        const auto rangeEnd = juce::jlimit(rangeStart + 32, total, analysisRangeEndSample.load());
        const auto rotation = juce::jlimit(
            rangeStart + 1, rangeEnd - 1,
            juce::roundToInt(juce::jlimit(0.0f, 1.0f, proportion) * total));
        const auto requestedRepair = juce::roundToInt(
            sampleRate * crossfadeMilliseconds.load(std::memory_order_relaxed) * 0.001);
        const auto repair = juce::jlimit(
            0, (rangeEnd - rangeStart) / 8, requestedRepair);
        result = LoopAnalyzer::evaluateFixedRange(
            currentSourceBuffer, sampleRate, rangeStart, rangeEnd, repair);
        result.rotationSample = rotation;
        selectedAudio = LoopAnalyzer::renderRotateRepair(currentSourceBuffer, result);
        snapshotSource.setSize(currentSourceBuffer.getNumChannels(),
                               rangeEnd - rangeStart, false, false, false);
        for (int channel = 0; channel < snapshotSource.getNumChannels(); ++channel)
            snapshotSource.copyFrom(channel, 0, currentSourceBuffer, channel,
                                    rangeStart, rangeEnd - rangeStart);
        sourceCandidates.clear();
        sourceCandidates.push_back(result);
        candidateRevision.fetch_add(1, std::memory_order_release);
    }
    if (selectedAudio.getNumSamples() == 0)
    {
        state.store(State::failed, std::memory_order_release);
        return false;
    }
    const auto selectedTruePeak = juce::Decibels::gainToDecibels(juce::jmax(
        1.0e-9f, SignalDiagnostics::estimateCircularTruePeak(selectedAudio)));
    const auto nextSnapshot = SignalDiagnostics::analyseSourceAndOutput(
        snapshotSource, selectedAudio, sampleRate);
    {
        const std::scoped_lock loopLock(loopDataMutex);
        loopBuffer = std::move(selectedAudio);
    }
    {
        const std::scoped_lock snapshotLock(signalSnapshotMutex);
        signalSnapshot = nextSnapshot;
    }
    capturedSampleCount.store(loopBuffer.getNumSamples());
    effectiveCrossfadeSamples.store(0);
    playbackPosition = 0;
    selectedStartSample.store(result.startSample);
    selectedEndSample.store(result.endSample);
    selectedRotationSample.store(result.rotationSample);
    truePeakDbtp.store(selectedTruePeak);
    preferLinearRepairFade.store(result.preferLinearRepairFade);
    lastUsedGenerationMode.store(GenerationMode::rotateRepair);
    previewMode.store(PreviewMode::loop);
    previewRestartRequested.store(true, std::memory_order_release);
    state.store(State::ready, std::memory_order_release);
    return true;
}

void LoopEngine::clear()
{
    generation.fetch_add(1, std::memory_order_acq_rel);
    analysisPending.store(false, std::memory_order_release);
    importedAnalysisPending.store(false, std::memory_order_release);
    state.store(State::analysing, std::memory_order_release);
    previewPlaying.store(false, std::memory_order_release);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    {
        const std::scoped_lock lock(loopDataMutex);
        loopBuffer.setSize(0, 0);
    }
    {
        const std::scoped_lock lock(sourceDataMutex);
        sourceCandidates.clear();
        textureVariants.clear();
        activeTextureVariant.store(-1, std::memory_order_relaxed);
        candidateRevision.fetch_add(1, std::memory_order_release);
    }
    {
        const std::scoped_lock lock(signalSnapshotMutex);
        signalSnapshot = {};
    }
    captureWritePosition = 0;
    playbackPosition = 0;
    sourcePlaybackPosition = analysisRangeStartSample.load(std::memory_order_relaxed);
    capturedSampleCount.store(0);
    captureProgress.store(0.0f);
    analysisProgress.store(0.0f, std::memory_order_relaxed);
    effectiveCrossfadeSamples.store(0, std::memory_order_relaxed);
    selectedRotationSample.store(-1, std::memory_order_relaxed);
    resetDiagnostics();
    state.store(selectedSourceSamples.load(std::memory_order_relaxed) > 0
                    ? State::sourceReady : State::empty,
                std::memory_order_release);
}

void LoopEngine::process(juce::AudioBuffer<float>& buffer, const float wetMix) noexcept
{
    applyPendingRequests();
    if (previewRestartRequested.exchange(false, std::memory_order_acq_rel))
    {
        playbackPosition = juce::jlimit(
            0, juce::jmax(0, capturedSampleCount.load(std::memory_order_relaxed) - 1),
            effectiveCrossfadeSamples.load(std::memory_order_relaxed));
        sourcePlaybackPosition = analysisRangeStartSample.load(std::memory_order_relaxed);
    }
    mixSmoother.setTargetValue(juce::jlimit(0.0f, 1.0f, wetMix));
    auto hasReadLease = false;
    if (state.load(std::memory_order_acquire) == State::ready)
    {
        activeAudioReaders.fetch_add(1, std::memory_order_acq_rel);
        hasReadLease = state.load(std::memory_order_acquire) == State::ready;
        if (!hasReadLease)
            activeAudioReaders.fetch_sub(1, std::memory_order_acq_rel);
    }

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        auto currentState = state.load(std::memory_order_acquire);

        if (currentState == State::armed)
        {
            if (scheduledCaptureDelay > 0)
            {
                --scheduledCaptureDelay;
                mixSmoother.skip(1);
                continue;
            }

            state.store(State::capturing, std÷Žt¶‰žËkºwµç@€€€€€€€€€€€€…¹‘¥‘…Ñ”¹ÍÑ…ÉÑM…µÁ±”€¬ô¥µÁ½ÉÑ•‘=™™Í•Ðì(€€€€€€€€€€€€€€€…¹‘¥‘…Ñ”¹•¹‘M…µÁ±”€¬ô¥µÁ½ÉÑ•‘=™™Í•Ðì(€€€€€€€€€€€€€€€¥˜€¡…¹‘¥‘…Ñ”¹É½Ñ…Ñ¥½¹M…µÁ±”€øô€À¤(€€€€€€€€€€€€€€€€€€€…¹‘¥‘…Ñ”¹É½Ñ…Ñ¥½¹M…µÁ±”€¬ô¥µÁ½ÉÑ•‘=™™Í•Ðì(€€€€€€€€€€€ô(€€€€€€€€€€€…¹‘¥‘…Ñ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€€€€€¥˜€¡¥µÁ½ÉÑ•‘I•Á±…•ÍÕÉÉ•¹ÑM½ÕÉ”¤(€€€€€€€€€€€€€€€Í½ÕÉ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€ô((€€€€€€€¥˜€¡¹•áÑM¹…ÁÍ¡½Ð¹Ù…±¥¤(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬Í¹…ÁÍ¡½Ñ1½¬¡Í¥¹…±M¹…ÁÍ¡½Ñ5ÕÑ•à¤ì(€€€€€€€€€€€Í¥¹…±M¹…ÁÍ¡½Ð€ô¹•áÑM¹…ÁÍ¡½Ðì(€€€€€€€ô((€€€€€€€…ÁÑÕÉ•‘M…µÁ±•½Õ¹Ð¹ÍÑ½É”¡Í•±•Ñ•‘M…µÁ±•Ì¤ì(€€€€€€€•™™•Ñ¥Ù•É½ÍÍ™…‘•M…µÁ±•Ì¹ÍÑ½É” (€€€€€€€€€€€É•ÍÕ±Ð¹É½Ñ…Ñ¥½¹M…µÁ±”€øô€À€ü€À€èÉ•ÍÕ±Ð¹É•Á…¥É=Ù•É±…ÁM…µÁ±•Ì¤ì(€€€€€€€Á±…å‰…­A½Í¥Ñ¥½¸€ô•™™•Ñ¥Ù•É½ÍÍ™…‘•M…µÁ±•Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€€€€€ÑÉÕ•A•…­‰ÑÀ¹ÍÑ½É”¡Í•±•Ñ•‘QÉÕ•A•…¬¤ì(€€€€€€€ÁÉ•™•É1¥¹•…ÉI•Á…¥É…‘”¹ÍÑ½É”¡É•ÍÕ±Ð¹ÁÉ•™•É1¥¹•…ÉI•Á…¥É…‘”¤ì(€€€€€€€±…ÍÑUÍ•‘•¹•É…Ñ¥½¹5½‘”¹ÍÑ½É”¡•¹•É…Ñ¥½¹5½‘”èéÉ½Ñ…Ñ•I•Á…¥È¤ì(€€€€€€€Í•±•Ñ•‘MÑ…ÉÑM…µÁ±”¹ÍÑ½É”¡É•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”€¬¥µÁ½ÉÑ•‘=™™Í•Ð¤ì(€€€€€€€Í•±•Ñ•‘¹‘M…µÁ±”¹ÍÑ½É”¡É•ÍÕ±Ð¹•¹‘M…µÁ±”(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€´€¡É•ÍÕ±Ð¹É½Ñ…Ñ¥½¹M…µÁ±”€ð€À(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€üÉ•ÍÕ±Ð¹É•Á…¥É=Ù•É±…ÁM…µÁ±•Ì€è€À¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¬¥µÁ½ÉÑ•‘=™™Í•Ð¤ì(€€€€€€€Í•±•Ñ•‘I½Ñ…Ñ¥½¹M…µÁ±”¹ÍÑ½É”¡É•ÍÕ±Ð¹É½Ñ…Ñ¥½¹M…µÁ±”€øô€À(€€€€€€€€€€€€üÉ•ÍÕ±Ð¹É½Ñ…Ñ¥½¹M…µÁ±”€¬¥µÁ½ÉÑ•‘=™™Í•Ð€è€´Ä¤ì(€€€€€€€Í•±•Ñ•‘M½ÕÉ•M…µÁ±•Ì¹ÍÑ½É”¡¥µÁ½ÉÑ•‘Õ±±M½ÕÉ•M…µÁ±•Ì¤ì(€€€€€€€…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹ÍÑ½É”¡¥µÁ½ÉÑ•‘=™™Í•Ð¤ì(€€€€€€€…¹…±åÍ¥ÍI…¹•¹‘M…µÁ±”¹ÍÑ½É”¡¥µÁ½ÉÑ•‘=™™Í•Ð€¬Í½ÕÉ•M…µÁ±•½Õ¹Ð¤ì(€€€€€€€Í½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸€ô¥µÁ½ÉÑ•‘=™™Í•Ðì(€€€€€€€ÍÑ…Ñ”¹ÍÑ½É”¡MÑ…Ñ”èéÉ•…‘ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€…¹…±åÍ¥ÍAÉ½É•ÍÌ¹ÍÑ½É” Ä¸Á˜°ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€ô)ô()Ù½¥1½½Á¹¥¹”èéÉ•Í•Ñ¥…¹½ÍÑ¥Ì ¤¹½•á•ÁÐ)ì(€€€ÑÉÕ•A•…­‰ÑÀ¹ÍÑ½É” ´ÄÀÀ¸Á˜¤ì(€€€ÁÉ•™•É1¥¹•…ÉI•Á…¥É…‘”¹ÍÑ½É”¡™…±Í”¤ì)ô()©Õ”èé5•µ½Éå	±½¬1½½Á¹¥¹”èéÉ•…Ñ•1½½ÁMÑ…Ñ” ¤½¹ÍÐ)ì(€€€©Õ”èé5•µ½Éå	±½¬É•ÍÕ±Ðì(€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡Í½ÕÉ•…Ñ…5ÕÑ•à°±½½Á…Ñ…5ÕÑ•à¤ì(€€€¥˜€¡±½½Á	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤€ôô€Àñð±½½Á	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤€ôô€À¤(€€€€€€€É•ÑÕÉ¸É•ÍÕ±Ðì((€€€©Õ”èé5•µ½Éå=ÕÑÁÕÑMÑÉ•…´‘•ÍÑ¥¹…Ñ¥½¸¡É•ÍÕ±Ð°™…±Í”¤ì(€€€ì(€€€€€€€©Õ”èéi%A½µÁÉ•ÍÍ½É=ÕÑÁÕÑMÑÉ•…´½µÁÉ•ÍÍ• ™‘•ÍÑ¥¹…Ñ¥½¸°€Ø°™…±Í”¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡ÍÑ…Ñ•5…¥Œ¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡ÍÑ…Ñ•Y•ÉÍ¥½¸¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•½Õ‰±”¡Í…µÁ±•I…Ñ”¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡ÍÑ…Ñ¥}…ÍÐñ¥¹Ðø¡±…ÍÑUÍ•‘•¹•É…Ñ¥½¹5½‘”¹±½… ¤¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡±½½Á	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡±½½Á	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡ÁÉ•™•É1¥¹•…ÉI•Á…¥É…‘”¹±½… ¤€ü€Ä€è€À¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•±½…Ð¡ÑÉÕ•A•…­‰ÑÀ¹±½… ¤¤ì(€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ð±½½Á	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤ì€¬­¡…¹¹•°¤(€€€€€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ”¡±½½Á	Õ™™•È¹•ÑI•…‘A½¥¹Ñ•È¡¡…¹¹•°¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€ÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡±½½Á	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤¤€¨Í¥é•½˜¡™±½…Ð¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•MÑÉ¥¹œ¡ÕÉÉ•¹ÑM½ÕÉ•9…µ”¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡Í•±•Ñ•‘MÑ…ÉÑM…µÁ±”¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡Í•±•Ñ•‘¹‘M…µÁ±”¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡Í•±•Ñ•‘I½Ñ…Ñ¥½¹M…µÁ±”¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡Í•±•Ñ•‘M½ÕÉ•M…µÁ±•Ì¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡…¹…±åÍ¥ÍI…¹•¹‘M…µÁ±”¹±½… ¤¤ì(€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ðÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤ì€¬­¡…¹¹•°¤(€€€€€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ”¡ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹•ÑI•…‘A½¥¹Ñ•È¡¡…¹¹•°¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€ÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¨Í¥é•½˜¡™±½…Ð¤¤ì(€€€ô(€€€É•ÑÕÉ¸É•ÍÕ±Ðì)ô()‰½½°1½½Á¹¥¹”èéÉ•ÍÑ½É•1½½ÁMÑ…Ñ”¡½¹ÍÐÙ½¥¨‘…Ñ„°½¹ÍÐÍ¥é•}ÐÍ¥é”¤)ì(€€€¥˜€¡‘…Ñ„€ôô¹Õ±±ÁÑÈñðÍ¥é”€ôô€À¤(€€€€€€€É•ÑÕÉ¸™…±Í”ì((€€€©Õ”èé5•µ½Éå%¹ÁÕÑMÑÉ•…´Í½ÕÉ”¡‘…Ñ„°Í¥é”°™…±Í”¤ì(€€€©Õ”èéi%A•½µÁÉ•ÍÍ½É%¹ÁÕÑMÑÉ•…´‘•½µÁÉ•ÍÍ• ™Í½ÕÉ”°™…±Í”¤ì(€€€¥˜€¡‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤€„ôÍÑ…Ñ•5…¥Œ¤(€€€€€€€É•ÑÕÉ¸™…±Í”ì((€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘Y•ÉÍ¥½¸€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€¥˜€¡Í…Ù•‘Y•ÉÍ¥½¸€ð€ÄñðÍ…Ù•‘Y•ÉÍ¥½¸€øÍÑ…Ñ•Y•ÉÍ¥½¸¤(€€€€€€€É•ÑÕÉ¸™…±Í”ì(€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘M…µÁ±•I…Ñ”€ô‘•½µÁÉ•ÍÍ•¹É•…‘½Õ‰±” ¤ì(€€€…ÕÑ¼Í…Ù•‘5½‘”€ô•¹•É…Ñ¥½¹5½‘”èéÉ½Ñ…Ñ•I•Á…¥Èì(€€€¥˜€¡Í…Ù•‘Y•ÉÍ¥½¸€øô€È¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼ÍÑ½É•‘5½‘”€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€Í…Ù•‘5½‘”€ôÍ…Ù•‘Y•ÉÍ¥½¸€ðô€Ì(€€€€€€€€€€€€ü€¡ÍÑ½É•‘5½‘”€ôô€Ä€ü•¹•É…Ñ¥½¹5½‘”èéÑ•áÑÕÉ•1½½À(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€è•¹•É…Ñ¥½¹5½‘”èéÉ½Ñ…Ñ•I•Á…¥È¤(€€€€€€€€€€€€è€¡ÍÑ½É•‘5½‘”€ôôÍÑ…Ñ¥}…ÍÐñ¥¹Ðø¡•¹•É…Ñ¥½¹5½‘”èéÑ•áÑÕÉ•1½½À¤(€€€€€€€€€€€€€€€€€€€ü•¹•É…Ñ¥½¹5½‘”èéÑ•áÑÕÉ•1½½À€è•¹•É…Ñ¥½¹5½‘”èéÉ½Ñ…Ñ•I•Á…¥È¤ì(€€€ô(€€€½¹ÍÐ…ÕÑ¼¡…¹¹•±Ì€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€½¹ÍÐ…ÕÑ¼Í…µÁ±•Ì€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€¥˜€¡Í…Ù•‘M…µÁ±•I…Ñ”€ðô€À¸Àñð¡…¹¹•±Ì€ð€Äñð¡…¹¹•±Ì€ø€ÈñðÍ…µÁ±•Ì€ð€Ä(€€€€€€€ñðÍ…µÁ±•Ì€ø©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…Ù•‘M…µÁ±•I…Ñ”€¨µ…á¥µÕµQ•áÑÕÉ•M•½¹‘Ì¤¤(€€€€€€€É•ÑÕÉ¸™…±Í”ì((€€€…ÕÑ¼Í…Ù•‘AÉ•™•É1¥¹•…É…‘”€ô™…±Í”ì(€€€¥˜€¡Í…Ù•‘Y•ÉÍ¥½¸€øô€Ø¤(€€€ì(€€€€€€€Í…Ù•‘AÉ•™•É1¥¹•…É…‘”€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤€„ô€Àì(€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€ô(€€€•±Í”(€€€ì(€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼±•…åA¡…Í•M¥µ¥±…É¥Ñä€ô‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤ì(€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€€€€€Í…Ù•‘AÉ•™•É1¥¹•…É…‘”€ô±•…åA¡…Í•M¥µ¥±…É¥Ñä€øô€ÜÔ¸Á˜ì(€€€€€€€¥˜€¡Í…Ù•‘Y•ÉÍ¥½¸€øô€Ì¤(€€€€€€€ì(€€€€€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€€€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€€€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤¤ì(€€€€€€€€€€€ÍÑ…Ñ¥}…ÍÐñÙ½¥ø¡‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤¤ì(€€€€€€€ô(€€€ô(€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…ÐøÉ•ÍÑ½É•¡¡…¹¹•±Ì°Í…µÁ±•Ì¤ì(€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ð¡…¹¹•±Ìì€¬­¡…¹¹•°¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼‰åÑ•Ì€ôÍÑ…Ñ¥}…ÍÐñ¥¹Ðø¡ÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡Í…µÁ±•Ì¤€¨Í¥é•½˜¡™±½…Ð¤¤ì(€€€€€€€¥˜€¡‘•½µÁÉ•ÍÍ•¹É•…¡É•ÍÑ½É•¹•Ñ]É¥Ñ•A½¥¹Ñ•È¡¡…¹¹•°¤°‰åÑ•Ì¤€„ô‰åÑ•Ì¤(€€€€€€€€€€€É•ÑÕÉ¸™…±Í”ì(€€€ô((€€€©Õ”èéMÑÉ¥¹œÉ•ÍÑ½É•‘M½ÕÉ•9…µ”ì(€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…ÐøÉ•ÍÑ½É•‘M½ÕÉ”ì(€€€…ÕÑ¼Í…Ù•‘M•±•Ñ•‘MÑ…ÉÐ€ô€Àì(€€€…ÕÑ¼Í…Ù•‘M•±•Ñ•‘¹€ô€Àì(€€€…ÕÑ¼Í…Ù•‘I½Ñ…Ñ¥½¸€ô€´Äì(€€€…ÕÑ¼Í…Ù•‘M•±•Ñ•‘M½ÕÉ•M…µÁ±•Ì€ô€Àì(€€€…ÕÑ¼Í…Ù•‘I…¹•MÑ…ÉÐ€ô€Àì(€€€…ÕÑ¼Í…Ù•‘I…¹•¹€ô€Àì(€€€¥˜€¡Í…Ù•‘Y•ÉÍ¥½¸€øô€Ô¤(€€€ì(€€€€€€€É•ÍÑ½É•‘M½ÕÉ•9…µ”€ô‘•½µÁÉ•ÍÍ•¹É•…‘MÑÉ¥¹œ ¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ•¡…¹¹•±Ì€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ•M…µÁ±•Ì€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€Í…Ù•‘M•±•Ñ•‘MÑ…ÉÐ€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€Í…Ù•‘M•±•Ñ•‘¹€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€Í…Ù•‘I½Ñ…Ñ¥½¸€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€Í…Ù•‘M•±•Ñ•‘M½ÕÉ•M…µÁ±•Ì€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€Í…Ù•‘I…¹•MÑ…ÉÐ€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€Í…Ù•‘I…¹•¹€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€€€€€¥˜€¡Í½ÕÉ•¡…¹¹•±Ì€ð€ÀñðÍ½ÕÉ•¡…¹¹•±Ì€ø€ÈñðÍ½ÕÉ•M…µÁ±•Ì€ð€À(€€€€€€€€€€€ñðÍ½ÕÉ•M…µÁ±•Ì€ø©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…Ù•‘M…µÁ±•I…Ñ”€¨µ…á¥µÕµQ•áÑÕÉ•M•½¹‘Ì¤(€€€€€€€€€€€ñð€¡Í½ÕÉ•M…µÁ±•Ì€ø€À€˜˜Í½ÕÉ•¡…¹¹•±Ì€ôô€À¤¤(€€€€€€€€€€€É•ÑÕÉ¸™…±Í”ì(€€€€€€€É•ÍÑ½É•‘M½ÕÉ”¹Í•ÑM¥é”¡Í½ÕÉ•¡…¹¹•±Ì°Í½ÕÉ•M…µÁ±•Ì°™…±Í”°™…±Í”°™…±Í”¤ì(€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ðÍ½ÕÉ•¡…¹¹•±Ìì€¬­¡…¹¹•°¤(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼‰åÑ•Ì€ôÍÑ…Ñ¥}…ÍÐñ¥¹Ðø (€€€€€€€€€€€€€€€ÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡Í½ÕÉ•M…µÁ±•Ì¤€¨Í¥é•½˜¡™±½…Ð¤¤ì(€€€€€€€€€€€¥˜€¡‘•½µÁÉ•ÍÍ•¹É•…¡É•ÍÑ½É•‘M½ÕÉ”¹•Ñ]É¥Ñ•A½¥¹Ñ•È¡¡…¹¹•°¤°‰åÑ•Ì¤€„ô‰åÑ•Ì¤(€€€€€€€€€€€€€€€É•ÑÕÉ¸™…±Í”ì(€€€€€€€ô(€€€ô((€€€•¹•É…Ñ¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}…Å}É•°¤ì(€€€ÍÑ…Ñ”¹ÍÑ½É”¡MÑ…Ñ”èé…¹…±åÍ¥¹œ°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€Ý¡¥±”€¡…Ñ¥Ù•Õ‘¥½I•…‘•ÉÌ¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}…ÅÕ¥É”¤€„ô€À¤(€€€€€€€ÍÑèéÑ¡¥Í}Ñ¡É•…èéå¥•± ¤ì(€€€½¹ÍÐ…ÕÑ¼Ñ…É•ÑM…µÁ±•Ì€ô©Õ”èé©±¥µ¥Ð Ä°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨µ…á¥µÕµQ•áÑÕÉ•M•½¹‘Ì¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡ÍÑ…Ñ¥}…ÍÐñ‘½Õ‰±”ø¡Í…µÁ±•Ì¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¨Í…µÁ±•I…Ñ”€¼Í…Ù•‘M…µÁ±•I…Ñ”¤¤ì(€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…ÐøÉ•ÍÑ½É•‘M½ÕÉ•Ñ!½ÍÑI…Ñ”ì(€€€¥˜€¡É•ÍÑ½É•‘M½ÕÉ”¹•Ñ9ÕµM…µÁ±•Ì ¤€ø€À¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼Ñ…É•ÑM½ÕÉ•M…µÁ±•Ì€ô©Õ”èé©±¥µ¥Ð (€€€€€€€€€€€€Ä°©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨µ…á¥µÕµQ•áÑÕÉ•M•½¹‘Ì¤°(€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡ÍÑ…Ñ¥}…ÍÐñ‘½Õ‰±”ø¡É•ÍÑ½É•‘M½ÕÉ”¹•Ñ9ÕµM…µÁ±•Ì ¤¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¨Í…µÁ±•I…Ñ”€¼Í…Ù•‘M…µÁ±•I…Ñ”¤¤ì(€€€€€€€É•ÍÑ½É•‘M½ÕÉ•Ñ!½ÍÑI…Ñ”¹Í•ÑM¥é”¡É•ÍÑ½É•‘M½ÕÉ”¹•Ñ9Õµ¡…¹¹•±Ì ¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€Ñ…É•ÑM½ÕÉ•M…µÁ±•Ì°™…±Í”°™…±Í”°™…±Í”¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼ÍÁ••‘I…Ñ¥¼€ôÍ…Ù•‘M…µÁ±•I…Ñ”€¼Í…µÁ±•I…Ñ”ì(€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ðÉ•ÍÑ½É•‘M½ÕÉ”¹•Ñ9Õµ¡…¹¹•±Ì ¤ì€¬­¡…¹¹•°¤(€€€€€€€ì(€€€€€€€€€€€©Õ”èé]¥¹‘½Ý•‘M¥¹%¹Ñ•ÉÁ½±…Ñ½È¥¹Ñ•ÉÁ½±…Ñ½Èì(€€€€€€€€€€€¥¹Ñ•ÉÁ½±…Ñ½È¹ÁÉ½•ÍÌ¡ÍÁ••‘I…Ñ¥¼°É•ÍÑ½É•‘M½ÕÉ”¹•ÑI•…‘A½¥¹Ñ•È¡¡…¹¹•°¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É•ÍÑ½É•‘M½ÕÉ•Ñ!½ÍÑI…Ñ”¹•Ñ]É¥Ñ•A½¥¹Ñ•È¡¡…¹¹•°¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€Ñ…É•ÑM½ÕÉ•M…µÁ±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É•ÍÑ½É•‘M½ÕÉ”¹•Ñ9ÕµM…µÁ±•Ì ¤°€À¤ì(€€€€€€€ô(€€€ô(€€€…ÕÑ¼É•ÍÑ½É•‘QÉÕ•A•…¬€ô€´ÄÀÀ¸Á˜ì(€€€ì(€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡±½½Á…Ñ…5ÕÑ•à¤ì(€€€€€€€±½½Á	Õ™™•È¹Í•ÑM¥é”¡¡…¹¹•±Ì°Ñ…É•ÑM…µÁ±•Ì°™…±Í”°™…±Í”°™…±Í”¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼ÍÁ••‘I…Ñ¥¼€ôÍ…Ù•‘M…µÁ±•I…Ñ”€¼Í…µÁ±•I…Ñ”ì(€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ð¡…¹¹•±Ìì€¬­¡…¹¹•°¤(€€€€€€€ì(€€€€€€€€€€€©Õ”èé]¥¹‘½Ý•‘M¥¹%¹Ñ•ÉÁ½±…Ñ½È¥¹Ñ•ÉÁ½±…Ñ½Èì(€€€€€€€€€€€¥¹Ñ•ÉÁ½±…Ñ½È¹ÁÉ½•ÍÌ¡ÍÁ••‘I…Ñ¥¼°É•ÍÑ½É•¹•ÑI•…‘A½¥¹Ñ•È¡¡…¹¹•°¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€±½½Á	Õ™™•È¹•Ñ]É¥Ñ•A½¥¹Ñ•È¡¡…¹¹•°¤°Ñ…É•ÑM…µÁ±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€Í…µÁ±•Ì°€À¤ì(€€€€€€€ô(€€€€€€€©Õ”èé¥¹½É•U¹ÕÍ•¡M¥¹…±¥…¹½ÍÑ¥ÌèéÉ•Á…¥É9½¹¥¹¥Ñ•¹‘I•µ½Ù•Œ¡±½½Á	Õ™™•È¤¤ì(€€€€€€€É•ÍÑ½É•‘QÉÕ•A•…¬€ôM¥¹…±¥…¹½ÍÑ¥Ìèé…ÁÁ±å¥ÉÕ±…ÉQÉÕ•A•…­•¥±¥¹œ¡±½½Á	Õ™™•È°€´Ä¸Á˜¤ì(€€€ô((€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ•M…±”€ôÍÑ…Ñ¥}…ÍÐñ‘½Õ‰±”ø¡Í…µÁ±•I…Ñ”¤€¼Í…Ù•‘M…µÁ±•I…Ñ”ì(€€€¥˜€¡É•ÍÑ½É•‘M½ÕÉ•Ñ!½ÍÑI…Ñ”¹•Ñ9ÕµM…µÁ±•Ì ¤€ø€À¤(€€€ì(€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡Í½ÕÉ•…Ñ…5ÕÑ•à¤ì(€€€€€€€ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È€ôÍÑèéµ½Ù”¡É•ÍÑ½É•‘M½ÕÉ•Ñ!½ÍÑI…Ñ”¤ì(€€€€€€€ÕÉÉ•¹ÑM½ÕÉ•9…µ”€ôÉ•ÍÑ½É•‘M½ÕÉ•9…µ”ì(€€€€€€€Ý…Ù•™½ÉµAÉ•Ù¥•Ü€ô‰Õ¥±‘]…Ù•™½ÉµAÉ•Ù¥•Ü¡ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¤ì(€€€€€€€Í½ÕÉ•…¹‘¥‘…Ñ•Ì¹±•…È ¤ì(€€€€€€€Ñ•áÑÕÉ•Y…É¥…¹ÑÌ¹±•…È ¤ì(€€€€€€€Í½ÕÉ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€…¹‘¥‘…Ñ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€ô(€€€•±Í”(€€€ì(€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡Í½ÕÉ•…Ñ…5ÕÑ•à¤ì(€€€€€€€ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹Í•ÑM¥é” À°€À¤ì(€€€€€€€ÕÉÉ•¹ÑM½ÕÉ•9…µ”¹±•…È ¤ì(€€€€€€€Ý…Ù•™½ÉµAÉ•Ù¥•Ü¹±•…È ¤ì(€€€€€€€Í½ÕÉ•…¹‘¥‘…Ñ•Ì¹±•…È ¤ì(€€€€€€€Ñ•áÑÕÉ•Y…É¥…¹ÑÌ¹±•…È ¤ì(€€€€€€€Í½ÕÉ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€…¹‘¥‘…Ñ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€ô((€€€…ÁÑÕÉ•‘M…µÁ±•½Õ¹Ð¹ÍÑ½É”¡Ñ…É•ÑM…µÁ±•Ì¤ì(€€€•™™•Ñ¥Ù•É½ÍÍ™…‘•M…µÁ±•Ì¹ÍÑ½É”¡Í…Ù•‘Y•ÉÍ¥½¸€ðô€Ì(€€€€€€€€€€€€˜˜Í…Ù•‘5½‘”€ôô•¹•É…Ñ¥½¹5½‘”èéÉ½Ñ…Ñ•I•Á…¥È(€€€€€€€€ü©Õ”èé©±¥µ¥Ð À°Ñ…É•ÑM…µÁ±•Ì€¼€Ì°(€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”(€€€€€€€€€€€€€€€€¨É½ÍÍ™…‘•5¥±±¥Í•½¹‘Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤€¨€À¸ÀÀÄ¤¤(€€€€€€€€è€À¤ì(€€€Á±…å‰…­A½Í¥Ñ¥½¸€ô•™™•Ñ¥Ù•É½ÍÍ™…‘•M…µÁ±•Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€ÑÉÕ•A•…­‰ÑÀ¹ÍÑ½É”¡É•ÍÑ½É•‘QÉÕ•A•…¬¤ì(€€€ÁÉ•™•É1¥¹•…ÉI•Á…¥É…‘”¹ÍÑ½É”¡Í…Ù•‘AÉ•™•É1¥¹•…É…‘”¤ì(€€€±…ÍÑUÍ•‘•¹•É…Ñ¥½¹5½‘”¹ÍÑ½É”¡Í…Ù•‘5½‘”¤ì(€€€½¹ÍÐ…ÕÑ¼Í…±•M…µÁ±”€ômÍ½ÕÉ•M…±•t€¡½¹ÍÐ¥¹ÐÍ…µÁ±”¤(€€€ì(€€€€€€€É•ÑÕÉ¸Í…µÁ±”€ð€À€ü€´Ä€è©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±”€¨Í½ÕÉ•M…±”¤ì(€€€ôì(€€€½¹ÍÐ…ÕÑ¼É•ÍÑ½É•‘M½ÕÉ•M…µÁ±•Ì€ôÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤ì(€€€¥˜€¡Í…Ù•‘Y•ÉÍ¥½¸€øô€Ô€˜˜É•ÍÑ½É•‘M½ÕÉ•M…µÁ±•Ì€ø€À¤(€€€ì(€€€€€€€Í•±•Ñ•‘M½ÕÉ•M…µÁ±•Ì¹ÍÑ½É”¡©Õ”èé©±¥µ¥Ð (€€€€€€€€€€€€Ä°É•ÍÑ½É•‘M½ÕÉ•M…µÁ±•Ì°(€€€€€€€€€€€©Õ”èé©µ…à Ä°Í…±•M…µÁ±”¡Í…Ù•‘M•±•Ñ•‘M½ÕÉ•M…µÁ±•Ì¤¤¤¤ì(€€€€€€€Í•±•Ñ•‘MÑ…ÉÑM…µÁ±”¹ÍÑ½É”¡©Õ”èé©±¥µ¥Ð (€€€€€€€€€€€€À°É•ÍÑ½É•‘M½ÕÉ•M…µÁ±•Ì€´€Ä°Í…±•M…µÁ±”¡Í…Ù•‘M•±•Ñ•‘MÑ…ÉÐ¤¤¤ì(€€€€€€€Í•±•Ñ•‘¹‘M…µÁ±”¹ÍÑ½É”¡©Õ”èé©±¥µ¥Ð (€€€€€€€€€€€Í•±•Ñ•‘MÑ…ÉÑM…µÁ±”¹±½… ¤€¬€Ä°É•ÍÑ½É•‘M½ÕÉ•M…µÁ±•Ì°(€€€€€€€€€€€Í…±•M…µÁ±”¡Í…Ù•‘M•±•Ñ•‘¹¤¤¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼É•ÍÑ½É•‘M•±•Ñ¥½¹M…µÁ±•Ì€ôÍ•±•Ñ•‘¹‘M…µÁ±”¹±½… ¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€´Í•±•Ñ•‘MÑ…ÉÑM…µÁ±”¹±½… ¤ì(€€€€€€€Í•±•Ñ•‘I½Ñ…Ñ¥½¹M…µÁ±”¹ÍÑ½É”¡Í…Ù•‘I½Ñ…Ñ¥½¸€ð€ÀñðÉ•ÍÑ½É•‘M•±•Ñ¥½¹M…µÁ±•Ì€ð€Ì(€€€€€€€€€€€€ü€´Ä€è©Õ”èé©±¥µ¥Ð (€€€€€€€€€€€€€€€Í•±•Ñ•‘MÑ…ÉÑM…µÁ±”¹±½… ¤€¬€Ä°Í•±•Ñ•‘¹‘M…µÁ±”¹±½… ¤€´€Ä°(€€€€€€€€€€€€€€€Í…±•M…µÁ±”¡Í…Ù•‘I½Ñ…Ñ¥½¸¤¤¤ì(€€€€€€€…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹ÍÑ½É”¡©Õ”èé©±¥µ¥Ð (€€€€€€€€€€€€À°É•ÍÑ½É•‘M½ÕÉ•M…µÁ±•Ì€´€Ä°Í…±•M…µÁ±”¡Í…Ù•‘I…¹•MÑ…ÉÐ¤¤¤ì(€€€€€€€…¹…±åÍ¥ÍI…¹•¹‘M…µÁ±”¹ÍÑ½É”¡©Õ”èé©±¥µ¥Ð (€€€€€€€€€€€…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹±½… ¤€¬€Ä°É•ÍÑ½É•‘M½ÕÉ•M…µÁ±•Ì°(€€€€€€€€€€€Í…±•M…µÁ±”¡Í…Ù•‘I…¹•¹¤¤¤ì(€€€€€€€Í½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸€ô…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹±½… ¤ì(€€€ô(€€€•±Í”(€€€ì(€€€€€€€Í•±•Ñ•‘M½ÕÉ•M…µÁ±•Ì¹ÍÑ½É” À¤ì(€€€€€€€Í•±•Ñ•‘MÑ…ÉÑM…µÁ±”¹ÍÑ½É” À¤ì(€€€€€€€Í•±•Ñ•‘¹‘M…µÁ±”¹ÍÑ½É” À¤ì(€€€€€€€Í•±•Ñ•‘I½Ñ…Ñ¥½¹M…µÁ±”¹ÍÑ½É” ´Ä¤ì(€€€€€€€…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹ÍÑ½É” À¤ì(€€€€€€€…¹…±åÍ¥ÍI…¹•¹‘M…µÁ±”¹ÍÑ½É” À¤ì(€€€€€€€Í½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸€ô€Àì(€€€ô(€€€…Ñ¥Ù•Q•áÑÕÉ•Y…É¥…¹Ð¹ÍÑ½É” ´Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€M¥¹…±¥…¹½ÍÑ¥ÌèéM¥¹…±M¹…ÁÍ¡½ÐÉ•ÍÑ½É•‘M¹…ÁÍ¡½Ðì(€€€¥˜€¡É•ÍÑ½É•‘M½ÕÉ•M…µÁ±•Ì€ø€À¤(€€€ì(€€€€€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…ÐøÍ¹…ÁÍ¡½ÑM½ÕÉ”ì(€€€€€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…ÐøÍ¹…ÁÍ¡½Ñ=ÕÑÁÕÐì(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡Í½ÕÉ•…Ñ…5ÕÑ•à¤ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼É…¹•MÑ…ÉÐ€ô…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹±½… ¤ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼É…¹•¹€ô…¹…±åÍ¥ÍI…¹•¹‘M…µÁ±”¹±½… ¤ì(€€€€€€€€€€€Í¹…ÁÍ¡½ÑM½ÕÉ”¹Í•ÑM¥é”¡ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É…¹•¹€´É…¹•MÑ…ÉÐ°™…±Í”°™…±Í”°™…±Í”¤ì(€€€€€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ðÍ¹…ÁÍ¡½ÑM½ÕÉ”¹•Ñ9Õµ¡…¹¹•±Ì ¤ì€¬­¡…¹¹•°¤(€€€€€€€€€€€€€€€Í¹…ÁÍ¡½ÑM½ÕÉ”¹½ÁåÉ½´¡¡…¹¹•°°€À°ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È°¡…¹¹•°°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É…¹•MÑ…ÉÐ°É…¹•¹€´É…¹•MÑ…ÉÐ¤ì(€€€€€€€ô(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡±½½Á…Ñ…5ÕÑ•à¤ì(€€€€€€€€€€€Í¹…ÁÍ¡½Ñ=ÕÑÁÕÐ€ô±½½Á	Õ™™•Èì(€€€€€€€ô(€€€€€€€É•ÍÑ½É•‘M¹…ÁÍ¡½Ð€ôM¥¹…±¥…¹½ÍÑ¥Ìèé…¹…±åÍ•M½ÕÉ•¹‘=ÕÑÁÕÐ (€€€€€€€€€€€Í¹…ÁÍ¡½ÑM½ÕÉ”°Í¹…ÁÍ¡½Ñ=ÕÑÁÕÐ°Í…µÁ±•I…Ñ”¤ì(€€€ô(€€€ì(€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡Í¥¹…±M¹…ÁÍ¡½Ñ5ÕÑ•à¤ì(€€€€€€€Í¥¹…±M¹…ÁÍ¡½Ð€ôÉ•ÍÑ½É•‘M¹…ÁÍ¡½Ðì(€€€ô(€€€…ÁÑÕÉ•AÉ½É•ÍÌ¹ÍÑ½É” Ä¸Á˜¤ì(€€€…¹…±åÍ¥ÍAÉ½É•ÍÌ¹ÍÑ½É” Ä¸Á˜°ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€ÁÉ•Ù¥•ÝA±…å¥¹œ¹ÍÑ½É”¡™…±Í”°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€ÍÑ…Ñ”¹ÍÑ½É”¡MÑ…Ñ”èéÉ•…‘ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€É•ÑÕÉ¸ÑÉÕ”ì)ô(