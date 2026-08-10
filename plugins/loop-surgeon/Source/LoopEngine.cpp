#include "LoopEngine.h"

#include "RenderQuality.h"

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
    resetScores();
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
    resetScores();
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
    resetScores();
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
        result.periodicity = 100.0f;
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
        1.0e-9f, RenderQuality::estimateCircularTruePeak(selectedAudio)));
    const auto nextSnapshot = RenderQuality::analyseSourceAndOutput(
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
    waveformScore.store(result.waveform);
    levelScore.store(result.level);
    slopeScore.store(result.slope);
    spectrumScore.store(result.spectrum);
    phaseScore.store(result.phase);
    stereoScore.store(result.stereo);
    transientScore.store(result.transient);
    periodicityScore.store(result.periodicity);
    repairScore.store(result.repair);
    repeatSafetyScore.store(result.periodicity);
    truePeakDbtp.store(selectedTruePeak);
    renderQualityScore.store(result.overall);
    const auto passedGate = selectedTruePeak <= -0.85f
                            && result.overall >= 68.0f && result.repair >= 58.0f;
    qualityGatePassed.store(passedGate);
    seamQuality.store(result.overall);
    lowConfidence.store(!passedGate);
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
    resetScores();
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

            state.store(State::capturing, std::memory_order_release);
            currentState = State::capturing;
        }

        if (currentState == State::capturing)
        {
            const auto channelCount = juce::jmin(buffer.getNumChannels(), captureBuffer.getNumChannels());
            for (int channel = 0; channel < channelCount; ++channel)
                captureBuffer.setSample(channel, captureWritePosition, buffer.getSample(channel, sample));

            ++captureWritePosition;
            captureProgress.store(static_cast<float>(captureWritePosition)
                                      / static_cast<float>(juce::jmax(1, captureSampleCount)),
                                  std::memory_order_relaxed);

            if (captureWritePosition >= captureSampleCount)
                finishCapture();

            mixSmoother.skip(1);
            continue;
        }

        if (!previewPlaying.load(std::memory_order_relaxed)
            || !hasReadLease || currentState != State::ready
            || capturedSampleCount.load(std::memory_order_relaxed) <= 0)
        {
            mixSmoother.skip(1);
            continue;
        }

        const auto auditionOriginal = previewMode.load(std::memory_order_relaxed) == PreviewMode::original
                                      && currentSourceBuffer.getNumSamples() > 0;
        const auto sourceChannels = auditionOriginal ? currentSourceBuffer.getNumChannels()
                                                     : loopBuffer.getNumChannels();
        const auto mix = mixSmoother.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto dry = buffer.getSample(channel, sample);
            const auto sourceChannel = juce::jlimit(0, juce::jmax(0, sourceChannels - 1), channel);
            const auto wet = auditionOriginal ? readSourceSample(sourceChannel)
                                              : readLoopSample(sourceChannel);
            buffer.setSample(channel, sample, dry + mix * (wet - dry));
        }

        if (auditionOriginal)
            advanceSourcePlaybackPosition();
        else
            advancePlaybackPosition();
    }

    if (hasReadLease)
        activeAudioReaders.fetch_sub(1, std::memory_order_release);
}

LoopEngine::State LoopEngine::getState() const noexcept
{
    return state.load(std::memory_order_acquire);
}

float LoopEngine::getCaptureProgress() const noexcept { return captureProgress.load(); }
float LoopEngine::getSeamQuality() const noexcept { return seamQuality.load(); }
float LoopEngine::getWaveformScore() const noexcept { return waveformScore.load(); }
float LoopEngine::getLevelScore() const noexcept { return levelScore.load(); }
float LoopEngine::getSlopeScore() const noexcept { return slopeScore.load(); }
float LoopEngine::getSpectrumScore() const noexcept { return spectrumScore.load(); }
float LoopEngine::getPhaseScore() const noexcept { return phaseScore.load(); }
float LoopEngine::getStereoScore() const noexcept { return stereoScore.load(); }
float LoopEngine::getTransientScore() const noexcept { return transientScore.load(); }
float LoopEngine::getPeriodicityScore() const noexcept { return periodicityScore.load(); }
float LoopEngine::getRepairScore() const noexcept { return repairScore.load(); }
float LoopEngine::getRepeatSafetyScore() const noexcept { return repeatSafetyScore.load(); }
float LoopEngine::getTruePeakDbtp() const noexcept { return truePeakDbtp.load(); }
float LoopEngine::getRenderQualityScore() const noexcept { return renderQualityScore.load(); }
bool LoopEngine::hasPassedQualityGate() const noexcept { return qualityGatePassed.load(); }
bool LoopEngine::isLowConfidence() const noexcept { return lowConfidence.load(); }
int LoopEngine::getCapturedSampleCount() const noexcept { return capturedSamp…7452 tokens truncated…:ready, std::memory_order_release);
            analysisProgress.store(1.0f, std::memory_order_relaxed);
            continue;
        }

        auto selectedAudio = result.rotationSample >= 0
            ? LoopAnalyzer::renderRotateRepair(*analysisSource, result)
            : juce::AudioBuffer<float> {};
        const auto selectedTruePeak = selectedAudio.getNumSamples() > 0
            ? juce::Decibels::gainToDecibels(juce::jmax(
                1.0e-9f, RenderQuality::estimateCircularTruePeak(selectedAudio)))
            : -100.0f;
        const auto nextSnapshot = result.rotationSample >= 0
            ? RenderQuality::analyseSourceAndOutput(
                *analysisSource, selectedAudio, sampleRate)
            : RenderQuality::SignalSnapshot {};
        const auto selectedSamples = result.rotationSample >= 0
            ? selectedAudio.getNumSamples()
            : juce::jmax(1, result.endSample - result.startSample);
        {
            const std::scoped_lock lock(loopDataMutex);
            if (result.rotationSample >= 0)
            {
                loopBuffer = std::move(selectedAudio);
            }
            else
            {
                loopBuffer.setSize(analysisSource->getNumChannels(), selectedSamples,
                                   false, false, false);
                for (int channel = 0; channel < loopBuffer.getNumChannels(); ++channel)
                    loopBuffer.copyFrom(channel, 0, *analysisSource, channel,
                                        result.startSample, selectedSamples);
            }
        }

        {
            const std::scoped_lock lock(sourceDataMutex);
            currentSourceName = importedName;
            if (importedReplacesCurrentSource)
            {
                currentSourceBuffer = std::move(importedSource);
                waveformPreview = std::move(replacementWaveform);
            }
            sourceCandidates = importedReport.candidates;
            textureVariants.clear();
            for (auto& candidate : sourceCandidates)
            {
                candidate.startSample += importedOffset;
                candidate.endSample += importedOffset;
                if (candidate.rotationSample >= 0)
                    candidate.rotationSample += importedOffset;
            }
            candidateRevision.fetch_add(1, std::memory_order_release);
            if (importedReplacesCurrentSource)
                sourceRevision.fetch_add(1, std::memory_order_release);
        }

        if (nextSnapshot.valid)
        {
            const std::scoped_lock snapshotLock(signalSnapshotMutex);
            signalSnapshot = nextSnapshot;
        }

        capturedSampleCount.store(selectedSamples);
        effectiveCrossfadeSamples.store(
            result.rotationSample >= 0 ? 0 : result.repairOverlapSamples);
        playbackPosition = effectiveCrossfadeSamples.load(std::memory_order_relaxed);
        waveformScore.store(result.waveform);
        levelScore.store(result.level);
        slopeScore.store(result.slope);
        spectrumScore.store(result.spectrum);
        phaseScore.store(result.phase);
        stereoScore.store(result.stereo);
        transientScore.store(result.transient);
        periodicityScore.store(result.periodicity);
        repairScore.store(result.repair);
        repeatSafetyScore.store(result.periodicity);
        truePeakDbtp.store(selectedTruePeak);
        renderQualityScore.store(result.overall);
        qualityGatePassed.store(selectedSamples > 0
            && (result.rotationSample < 0 || selectedTruePeak <= -0.85f)
            && result.overall >= 68.0f && result.repair >= 58.0f);
        lowConfidence.store(resultLowConfidence
                            || !qualityGatePassed.load(std::memory_order_relaxed));
        lastUsedGenerationMode.store(GenerationMode::rotateRepair);
        seamQuality.store(result.overall);
        selectedStartSample.store(result.startSample + importedOffset);
        selectedEndSample.store(result.endSample
                                 - (result.rotationSample < 0
                                        ? result.repairOverlapSamples : 0)
                                 + importedOffset);
        selectedRotationSample.store(result.rotationSample >= 0
            ? result.rotationSample + importedOffset : -1);
        selectedSourceSamples.store(importedFullSourceSamples);
        analysisRangeStartSample.store(importedOffset);
        analysisRangeEndSample.store(importedOffset + sourceSampleCount);
        sourcePlaybackPosition = importedOffset;
        state.store(State::ready, std::memory_order_release);
        analysisProgress.store(1.0f, std::memory_order_relaxed);
    }
}

void LoopEngine::resetScores() noexcept
{
    seamQuality.store(0.0f);
    waveformScore.store(0.0f);
    levelScore.store(0.0f);
    slopeScore.store(0.0f);
    spectrumScore.store(0.0f);
    phaseScore.store(0.0f);
    stereoScore.store(0.0f);
    transientScore.store(0.0f);
    periodicityScore.store(0.0f);
    repairScore.store(0.0f);
    repeatSafetyScore.store(0.0f);
    truePeakDbtp.store(-100.0f);
    renderQualityScore.store(0.0f);
    qualityGatePassed.store(false);
    lowConfidence.store(false);
}

juce::MemoryBlock LoopEngine::createLoopState() const
{
    juce::MemoryBlock result;
    const std::scoped_lock lock(sourceDataMutex, loopDataMutex);
    if (loopBuffer.getNumChannels() == 0 || loopBuffer.getNumSamples() == 0)
        return result;

    juce::MemoryOutputStream destination(result, false);
    {
        juce::GZIPCompressorOutputStream compressed(&destination, 6, false);
        compressed.writeInt(stateMagic);
        compressed.writeInt(stateVersion);
        compressed.writeDouble(sampleRate);
        compressed.writeInt(static_cast<int>(lastUsedGenerationMode.load()));
        compressed.writeInt(loopBuffer.getNumChannels());
        compressed.writeInt(loopBuffer.getNumSamples());
        compressed.writeFloat(seamQuality.load());
        compressed.writeFloat(levelScore.load());
        compressed.writeFloat(slopeScore.load());
        compressed.writeFloat(spectrumScore.load());
        compressed.writeFloat(phaseScore.load());
        compressed.writeFloat(stereoScore.load());
        compressed.writeFloat(repeatSafetyScore.load());
        compressed.writeFloat(truePeakDbtp.load());
        compressed.writeFloat(renderQualityScore.load());
        compressed.writeInt(qualityGatePassed.load() ? 1 : 0);
        for (int channel = 0; channel < loopBuffer.getNumChannels(); ++channel)
            compressed.write(loopBuffer.getReadPointer(channel),
                             static_cast<size_t>(loopBuffer.getNumSamples()) * sizeof(float));
        compressed.writeString(currentSourceName);
        compressed.writeInt(currentSourceBuffer.getNumChannels());
        compressed.writeInt(currentSourceBuffer.getNumSamples());
        compressed.writeInt(selectedStartSample.load());
        compressed.writeInt(selectedEndSample.load());
        compressed.writeInt(selectedRotationSample.load());
        compressed.writeInt(selectedSourceSamples.load());
        compressed.writeInt(analysisRangeStartSample.load());
        compressed.writeInt(analysisRangeEndSample.load());
        for (int channel = 0; channel < currentSourceBuffer.getNumChannels(); ++channel)
            compressed.write(currentSourceBuffer.getReadPointer(channel),
                             static_cast<size_t>(currentSourceBuffer.getNumSamples())
                                 * sizeof(float));
    }
    return result;
}

bool LoopEngine::restoreLoopState(const void* data, const size_t size)
{
    if (data == nullptr || size == 0)
        return false;

    juce::MemoryInputStream source(data, size, false);
    juce::GZIPDecompressorInputStream decompressed(&source, false);
    if (decompressed.readInt() != stateMagic)
        return false;

    const auto savedVersion = decompressed.readInt();
    if (savedVersion < 1 || savedVersion > stateVersion)
        return false;
    const auto savedSampleRate = decompressed.readDouble();
    auto savedMode = GenerationMode::rotateRepair;
    if (savedVersion >= 2)
    {
        const auto storedMode = decompressed.readInt();
        savedMode = savedVersion <= 3
            ? (storedMode == 1 ? GenerationMode::textureLoop
                               : GenerationMode::rotateRepair)
            : (storedMode == static_cast<int>(GenerationMode::textureLoop)
                   ? GenerationMode::textureLoop : GenerationMode::rotateRepair);
    }
    const auto channels = decompressed.readInt();
    const auto samples = decompressed.readInt();
    if (savedSampleRate <= 0.0 || channels < 1 || channels > 2 || samples < 1
        || samples > juce::roundToInt(savedSampleRate * maximumTextureSeconds))
        return false;

    const auto savedOverall = decompressed.readFloat();
    const auto savedLevel = decompressed.readFloat();
    const auto savedSlope = decompressed.readFloat();
    const auto savedSpectrum = decompressed.readFloat();
    const auto savedPhase = decompressed.readFloat();
    const auto savedStereo = decompressed.readFloat();
    const auto savedRepeatSafety = savedVersion >= 3 ? decompressed.readFloat() : 0.0f;
    const auto savedTruePeak = savedVersion >= 3 ? decompressed.readFloat() : -100.0f;
    const auto savedQuality = savedVersion >= 3 ? decompressed.readFloat() : savedOverall;
    const auto savedGatePassed = savedVersion >= 3
        ? decompressed.readInt() != 0 : savedOverall >= 68.0f;
    juce::AudioBuffer<float> restored(channels, samples);
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto bytes = static_cast<int>(static_cast<size_t>(samples) * sizeof(float));
        if (decompressed.read(restored.getWritePointer(channel), bytes) != bytes)
            return false;
    }

    juce::String restoredSourceName;
    juce::AudioBuffer<float> restoredSource;
    auto savedSelectedStart = 0;
    auto savedSelectedEnd = 0;
    auto savedRotation = -1;
    auto savedSelectedSourceSamples = 0;
    auto savedRangeStart = 0;
    auto savedRangeEnd = 0;
    if (savedVersion >= 5)
    {
        restoredSourceName = decompressed.readString();
        const auto sourceChannels = decompressed.readInt();
        const auto sourceSamples = decompressed.readInt();
        savedSelectedStart = decompressed.readInt();
        savedSelectedEnd = decompressed.readInt();
        savedRotation = decompressed.readInt();
        savedSelectedSourceSamples = decompressed.readInt();
        savedRangeStart = decompressed.readInt();
        savedRangeEnd = decompressed.readInt();
        if (sourceChannels < 0 || sourceChannels > 2 || sourceSamples < 0
            || sourceSamples > juce::roundToInt(savedSampleRate * maximumTextureSeconds)
            || (sourceSamples > 0 && sourceChannels == 0))
            return false;
        restoredSource.setSize(sourceChannels, sourceSamples, false, false, false);
        for (int channel = 0; channel < sourceChannels; ++channel)
        {
            const auto bytes = static_cast<int>(
                static_cast<size_t>(sourceSamples) * sizeof(float));
            if (decompressed.read(restoredSource.getWritePointer(channel), bytes) != bytes)
                return false;
        }
    }

    generation.fetch_add(1, std::memory_order_acq_rel);
    state.store(State::analysing, std::memory_order_release);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    const auto targetSamples = juce::jlimit(1,
                                           juce::roundToInt(sampleRate * maximumTextureSeconds),
                                           juce::roundToInt(static_cast<double>(samples)
                                                            * sampleRate / savedSampleRate));
    juce::AudioBuffer<float> restoredSourceAtHostRate;
    if (restoredSource.getNumSamples() > 0)
    {
        const auto targetSourceSamples = juce::jlimit(
            1, juce::roundToInt(sampleRate * maximumTextureSeconds),
            juce::roundToInt(static_cast<double>(restoredSource.getNumSamples())
                             * sampleRate / savedSampleRate));
        restoredSourceAtHostRate.setSize(restoredSource.getNumChannels(),
                                         targetSourceSamples, false, false, false);
        const auto speedRatio = savedSampleRate / sampleRate;
        for (int channel = 0; channel < restoredSource.getNumChannels(); ++channel)
        {
            juce::WindowedSincInterpolator interpolator;
            interpolator.process(speedRatio, restoredSource.getReadPointer(channel),
                                 restoredSourceAtHostRate.getWritePointer(channel),
                                 targetSourceSamples,
                                 restoredSource.getNumSamples(), 0);
        }
    }
    auto restoredFinite = true;
    auto restoredTruePeak = -100.0f;
    {
        const std::scoped_lock lock(loopDataMutex);
        loopBuffer.setSize(channels, targetSamples, false, false, false);
        const auto speedRatio = savedSampleRate / sampleRate;
        for (int channel = 0; channel < channels; ++channel)
        {
            juce::WindowedSincInterpolator interpolator;
            interpolator.process(speedRatio, restored.getReadPointer(channel),
                                 loopBuffer.getWritePointer(channel), targetSamples,
                                 samples, 0);
        }
        restoredFinite = RenderQuality::repairNonFiniteAndRemoveDc(loopBuffer);
        restoredTruePeak = RenderQuality::applyCircularTruePeakCeiling(loopBuffer, -1.0f);
    }

    const auto sourceScale = static_cast<double>(sampleRate) / savedSampleRate;
    if (restoredSourceAtHostRate.getNumSamples() > 0)
    {
        const std::scoped_lock lock(sourceDataMutex);
        currentSourceBuffer = std::move(restoredSourceAtHostRate);
        currentSourceName = restoredSourceName;
        waveformPreview = buildWaveformPreview(currentSourceBuffer);
        sourceCandidates.clear();
        textureVariants.clear();
        sourceRevision.fetch_add(1, std::memory_order_release);
        candidateRevision.fetch_add(1, std::memory_order_release);
    }
    else
    {
        const std::scoped_lock lock(sourceDataMutex);
        currentSourceBuffer.setSize(0, 0);
        currentSourceName.clear();
        waveformPreview.clear();
        sourceCandidates.clear();
        textureVariants.clear();
        sourceRevision.fetch_add(1, std::memory_order_release);
        candidateRevision.fetch_add(1, std::memory_order_release);
    }

    capturedSampleCount.store(targetSamples);
    effectiveCrossfadeSamples.store(savedVersion <= 3
            && savedMode == GenerationMode::rotateRepair
        ? juce::jlimit(0, targetSamples / 3,
            juce::roundToInt(sampleRate
                * crossfadeMilliseconds.load(std::memory_order_relaxed) * 0.001))
        : 0);
    playbackPosition = effectiveCrossfadeSamples.load(std::memory_order_relaxed);
    seamQuality.store(savedOverall);
    levelScore.store(savedLevel);
    slopeScore.store(savedSlope);
    spectrumScore.store(savedSpectrum);
    phaseScore.store(savedPhase);
    stereoScore.store(savedStereo);
    repairScore.store(savedOverall);
    repeatSafetyScore.store(savedRepeatSafety);
    truePeakDbtp.store(restoredTruePeak);
    renderQualityScore.store(savedQuality);
    const auto restoredGatePassed = savedGatePassed && restoredFinite
                                    && restoredTruePeak <= -0.85f;
    qualityGatePassed.store(restoredGatePassed);
    lowConfidence.store(!restoredGatePassed);
    lastUsedGenerationMode.store(savedMode);
    const auto scaleSample = [sourceScale] (const int sample)
    {
        return sample < 0 ? -1 : juce::roundToInt(sample * sourceScale);
    };
    const auto restoredSourceSamples = currentSourceBuffer.getNumSamples();
    if (savedVersion >= 5 && restoredSourceSamples > 0)
    {
        selectedSourceSamples.store(juce::jlimit(
            1, restoredSourceSamples,
            juce::jmax(1, scaleSample(savedSelectedSourceSamples))));
        selectedStartSample.store(juce::jlimit(
            0, restoredSourceSamples - 1, scaleSample(savedSelectedStart)));
        selectedEndSample.store(juce::jlimit(
            selectedStartSample.load() + 1, restoredSourceSamples,
            scaleSample(savedSelectedEnd)));
        const auto restoredSelectionSamples = selectedEndSample.load()
                                              - selectedStartSample.load();
        selectedRotationSample.store(savedRotation < 0 || restoredSelectionSamples < 3
            ? -1 : juce::jlimit(
                selectedStartSample.load() + 1, selectedEndSample.load() - 1,
                scaleSample(savedRotation)));
        analysisRangeStartSample.store(juce::jlimit(
            0, restoredSourceSamples - 1, scaleSample(savedRangeStart)));
        analysisRangeEndSample.store(juce::jlimit(
            analysisRangeStartSample.load() + 1, restoredSourceSamples,
            scaleSample(savedRangeEnd)));
        sourcePlaybackPosition = analysisRangeStartSample.load();
    }
    else
    {
        selectedSourceSamples.store(0);
        selectedStartSample.store(0);
        selectedEndSample.store(0);
        selectedRotationSample.store(-1);
        analysisRangeStartSample.store(0);
        analysisRangeEndSample.store(0);
        sourcePlaybackPosition = 0;
    }
    activeTextureVariant.store(-1, std::memory_order_relaxed);
    RenderQuality::SignalSnapshot restoredSnapshot;
    if (restoredSourceSamples > 0)
    {
        juce::AudioBuffer<float> snapshotSource;
        juce::AudioBuffer<float> snapshotOutput;
        {
            const std::scoped_lock lock(sourceDataMutex);
            const auto rangeStart = analysisRangeStartSample.load();
            const auto rangeEnd = analysisRangeEndSample.load();
            snapshotSource.setSize(currentSourceBuffer.getNumChannels(),
                                   rangeEnd - rangeStart, false, false, false);
            for (int channel = 0; channel < snapshotSource.getNumChannels(); ++channel)
                snapshotSource.copyFrom(channel, 0, currentSourceBuffer, channel,
                                        rangeStart, rangeEnd - rangeStart);
        }
        {
            const std::scoped_lock lock(loopDataMutex);
            snapshotOutput = loopBuffer;
        }
        restoredSnapshot = RenderQuality::analyseSourceAndOutput(
            snapshotSource, snapshotOutput, sampleRate);
    }
    {
        const std::scoped_lock lock(signalSnapshotMutex);
        signalSnapshot = restoredSnapshot;
    }
    juce::ignoreUnused(savedTruePeak);
    captureProgress.store(1.0f);
    analysisProgress.store(1.0f, std::memory_order_relaxed);
    previewPlaying.store(false, std::memory_order_release);
    state.store(State::ready, std::memory_order_release);
    return true;
}
