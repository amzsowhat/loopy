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

void LoopEngine::setTextureDynamicsCrush(const float amount) noexcept
{
    textureDynamicsCrush.store(juce::jlimit(0.0f, 1.0f, amount),
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
        const auto rangeStart = juce::jlimit(0, total - 32, selectedStartSample.load());
        const auto rangeEnd = juce::jlimit(rangeStart + 32, total, selectedEndSample.load());
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
        result.targetOutputSamples = juce::jmax(
            result.endSample - result.startSample - result.repairOverlapSamples,
            capturedSampleCount.load(std::memory_order_relaxed));
        result.repetitionCount = juce::jmax(
            1, juce::roundToInt(static_cast<double>(result.targetOutputSamples)
                                / juce::jmax(1, result.endSample - result.startSample
                                                   - result.repairOverlapSamples)));
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
int LoopEngine::getCapturedSampleCount() const noexcept { return capturedSampleCount.load(); }

juce::String LoopEngine::getSourceName() const
{
    const std::scoped_lock lock(sourceDataMutex);
    return currentSourceName;
}

double LoopEngine::getSourceDurationSeconds() const
{
    const std::scoped_lock lock(sourceDataMutex);
    return static_cast<double>(currentSourceBuffer.getNumSamples()) / sampleRate;
}

int LoopEngine::getCandidateCount() const
{
    const std::scoped_lock lock(sourceDataMutex);
    if (!textureVariants.empty())
        return static_cast<int>(textureVariants.size());
    return static_cast<int>(sourceCandidates.size());
}

uint64_t LoopEngine::getCandidateRevision() const noexcept { return candidateRevision.load(); }
uint64_t LoopEngine::getSourceRevision() const noexcept { return sourceRevision.load(); }

juce::String LoopEngine::getCandidateDescription(const int index) const
{
    const std::scoped_lock lock(sourceDataMutex);
    if (juce::isPositiveAndBelow(index, static_cast<int>(textureVariants.size())))
    {
        const auto& variant = textureVariants[static_cast<size_t>(index)];
        const auto samples = index == activeTextureVariant.load(std::memory_order_relaxed)
            ? capturedSampleCount.load(std::memory_order_relaxed)
            : variant.audio.getNumSamples();
        const auto seconds = static_cast<double>(samples) / sampleRate;
        const auto structure = variant.usedStructure == TextureStructure::particles
            ? juce::String("Fracture")
            : variant.usedStructure == TextureStructure::continuous
                ? juce::String("Drift") : juce::String("Flow");
        const auto character = variant.usedCharacter == TextureCharacter::patina
            ? juce::String("Patina")
            : variant.usedCharacter == TextureCharacter::bloom
                ? juce::String("Bloom")
                : variant.usedCharacter == TextureCharacter::fray
                    ? juce::String("Fray") : juce::String("Natural");
        return "Texture " + juce::String(index + 1) + "  |  " + structure
               + "  |  " + character
               + "  |  " + juce::String(seconds, 1) + " s";
    }
    if (!juce::isPositiveAndBelow(index, static_cast<int>(sourceCandidates.size())))
        return {};
    const auto& candidate = sourceCandidates[static_cast<size_t>(index)];
    const auto repairSamples = candidate.repairOverlapSamples;
    const auto outputSamples = candidate.targetOutputSamples > 0
        ? candidate.targetOutputSamples
        : candidate.endSample - candidate.startSample - repairSamples;
    const auto seconds = static_cast<double>(outputSamples) / sampleRate;
    return juce::String(candidate.rotationSample >= 0 ? "Loop " : "Candidate ")
           + juce::String(index + 1) + "  |  " + juce::String(seconds, 2) + " s";
}

void LoopEngine::selectCandidate(const int index)
{
    generation.fetch_add(1, std::memory_order_acq_rel);
    state.store(State::analysing, std::memory_order_release);
    analysisProgress.store(0.0f, std::memory_order_relaxed);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();

    LoopAnalysisResult result;
    auto selectedTexture = false;
    auto textureSamples = 0;
    auto textureTruePeak = -100.0f;
    juce::AudioBuffer<float> selectedAudio;
    juce::AudioBuffer<float> snapshotSource;
    juce::AudioBuffer<float> snapshotOutput;
    {
        const std::scoped_lock sourceLock(sourceDataMutex);
        const auto snapshotStart = juce::jlimit(
            0, juce::jmax(0, currentSourceBuffer.getNumSamples() - 1),
            analysisRangeStartSample.load(std::memory_order_relaxed));
        const auto snapshotEnd = juce::jlimit(
            snapshotStart + 1, currentSourceBuffer.getNumSamples(),
            analysisRangeEndSample.load(std::memory_order_relaxed));
        snapshotSource.setSize(currentSourceBuffer.getNumChannels(),
                               snapshotEnd - snapshotStart, false, false, false);
        for (int channel = 0; channel < snapshotSource.getNumChannels(); ++channel)
            snapshotSource.copyFrom(channel, 0, currentSourceBuffer, channel,
                                    snapshotStart, snapshotEnd - snapshotStart);
        if (juce::isPositiveAndBelow(index, static_cast<int>(textureVariants.size())))
        {
            selectedTexture = true;
            auto& texture = textureVariants[static_cast<size_t>(index)];
            textureTruePeak = texture.truePeakDbtp;
            const auto previous = activeTextureVariant.load(std::memory_order_relaxed);
            if (index != previous)
            {
                const std::scoped_lock loopLock(loopDataMutex);
                std::swap(loopBuffer, texture.audio);
                if (juce::isPositiveAndBelow(
                        previous, static_cast<int>(textureVariants.size())))
                    std::swap(textureVariants[static_cast<size_t>(previous)].audio,
                              texture.audio);
                textureSamples = loopBuffer.getNumSamples();
                activeTextureVariant.store(index, std::memory_order_relaxed);
            }
            else
            {
                textureSamples = capturedSampleCount.load(std::memory_order_relaxed);
            }
        }
        else
        {
            if (!juce::isPositiveAndBelow(index, static_cast<int>(sourceCandidates.size())))
            {
                state.store(State::ready, std::memory_order_release);
                return;
            }
            result = sourceCandidates[static_cast<size_t>(index)];
            if (result.rotationSample >= 0)
            {
                selectedAudio = LoopAnalyzer::renderRotateRepair(
                    currentSourceBuffer, result);
            }
            else
            {
                const auto samples = result.endSample - result.startSample;
                selectedAudio.setSize(currentSourceBuffer.getNumChannels(), samples,
                                      false, false, false);
                for (int channel = 0; channel < selectedAudio.getNumChannels(); ++channel)
                    selectedAudio.copyFrom(channel, 0, currentSourceBuffer, channel,
                                           result.startSample, samples);
            }
        }
        if (selectedTexture)
        {
            const std::scoped_lock loopLock(loopDataMutex);
            snapshotOutput = loopBuffer;
        }
        else
        {
            snapshotOutput = selectedAudio;
        }
    }
    const auto nextSnapshot = SignalDiagnostics::analyseSourceAndOutput(
        snapshotSource, snapshotOutput, sampleRate);
    {
        const std::scoped_lock snapshotLock(signalSnapshotMutex);
        signalSnapshot = nextSnapshot;
    }
    const auto selectedTruePeak = selectedAudio.getNumSamples() > 0
        ? juce::Decibels::gainToDecibels(juce::jmax(
            1.0e-9f, SignalDiagnostics::estimateCircularTruePeak(selectedAudio)))
        : -100.0f;
    if (selectedTexture)
    {
        capturedSampleCount.store(textureSamples);
        effectiveCrossfadeSamples.store(0);
        selectedRotationSample.store(-1);
        playbackPosition = 0;
        truePeakDbtp.store(textureTruePeak);
        preferLinearRepairFade.store(false);
        lastUsedGenerationMode.store(GenerationMode::textureLoop);
        previewMode.store(PreviewMode::loop);
        previewRestartRequested.store(true, std::memory_order_release);
        state.store(State::ready, std::memory_order_release);
        analysisProgress.store(1.0f, std::memory_order_relaxed);
        return;
    }

    {
        const std::scoped_lock loopLock(loopDataMutex);
        loopBuffer = std::move(selectedAudio);
    }
    capturedSampleCount.store(loopBuffer.getNumSamples());
    selectedStartSample.store(result.startSample);
    selectedRotationSample.store(result.rotationSample);
    const auto repairSamples = result.repairOverlapSamples;
    effectiveCrossfadeSamples.store(result.rotationSample >= 0 ? 0 : repairSamples);
    playbackPosition = effectiveCrossfadeSamples.load(std::memory_order_relaxed);
    selectedEndSample.store(result.rotationSample >= 0
                                ? result.endSample : result.endSample - repairSamples);
    truePeakDbtp.store(selectedTruePeak);
    preferLinearRepairFade.store(result.preferLinearRepairFade);
    lastUsedGenerationMode.store(GenerationMode::rotateRepair);
    state.store(State::ready, std::memory_order_release);
    analysisProgress.store(1.0f, std::memory_order_relaxed);
}

std::vector<float> LoopEngine::getWaveformPreview() const
{
    const std::scoped_lock lock(sourceDataMutex);
    return waveformPreview;
}

float LoopEngine::getRotationProportion() const noexcept
{
    return static_cast<float>(selectedRotationSample.load())
           / static_cast<float>(juce::jmax(1, selectedSourceSamples.load()));
}

float LoopEngine::getAnalysisRangeStartProportion() const noexcept
{
    return juce::jlimit(0.0f, 1.0f,
        static_cast<float>(analysisRangeStartSample.load())
            / static_cast<float>(juce::jmax(1, selectedSourceSamples.load())));
}

float LoopEngine::getAnalysisRangeEndProportion() const noexcept
{
    return juce::jlimit(0.0f, 1.0f,
        static_cast<float>(analysisRangeEndSample.load())
            / static_cast<float>(juce::jmax(1, selectedSourceSamples.load())));
}

juce::AudioBuffer<float> LoopEngine::createRenderedLoop() const
{
    const std::scoped_lock lock(loopDataMutex);
    const auto samples = loopBuffer.getNumSamples();
    const auto channels = loopBuffer.getNumChannels();
    if (samples < 2 || channels < 1)
        return {};

    const auto fade = juce::jlimit(0, samples / 3,
                                   effectiveCrossfadeSamples.load(std::memory_order_relaxed));
    if (fade == 0)
        return loopBuffer;

    juce::AudioBuffer<float> rendered(channels, samples - fade);
    const auto middle = samples - 2 * fade;
    const auto useLinearFade = preferLinearRepairFade.load(std::memory_order_relaxed);
    for (int channel = 0; channel < channels; ++channel)
    {
        if (middle > 0)
            rendered.copyFrom(channel, 0, loopBuffer, channel, fade, middle);
        for (int index = 0; index < fade; ++index)
        {
            const auto phase = static_cast<float>(index + 1) / static_cast<float>(fade + 1);
            const auto tailGain = useLinearFade ? 1.0f - phase
                                                : std::cos(phase * juce::MathConstants<float>::halfPi);
            const auto headGain = useLinearFade ? phase
                                                : std::sin(phase * juce::MathConstants<float>::halfPi);
            const auto tail = loopBuffer.getSample(channel, samples - fade + index);
            const auto head = loopBuffer.getSample(channel, index);
            rendered.setSample(channel, middle + index,
                               tailGain * tail + headGain * head);
        }
    }
    return rendered;
}

SignalDiagnostics::SignalSnapshot LoopEngine::getSignalSnapshot() const
{
    const std::scoped_lock lock(signalSnapshotMutex);
    return signalSnapshot;
}

void LoopEngine::applyPendingRequests() noexcept
{
    if (!captureRequested.exchange(false, std::memory_order_acq_rel)
        || state.load(std::memory_order_acquire) == State::analysing
        || captureBuffer.getNumSamples() == 0)
        return;

    generation.fetch_add(1, std::memory_order_acq_rel);
    requestedLoopSamples = juce::jlimit(1,
                                       captureBuffer.getNumSamples(),
                                       juce::roundToInt(sampleRate * loopLengthSeconds));
    captureSampleCount = juce::jmin(captureBuffer.getNumSamples(), requestedLoopSamples);
    captureWritePosition = 0;
    scheduledCaptureDelay = requestedStartDelay.load(std::memory_order_relaxed);
    captureProgress.store(0.0f);
    resetDiagnostics();
    state.store(scheduledCaptureDelay > 0 ? State::armed : State::capturing,
                std::memory_order_release);
}

void LoopEngine::finishCapture() noexcept
{
    captureWritePosition = captureSampleCount;
    captureProgress.store(1.0f);
    state.store(State::analysing, std::memory_order_release);
    analysisPending.store(true, std::memory_order_release);
    analysisCondition.notify_one();
}

float LoopEngine::readLoopSample(const int channel) const noexcept
{
    const auto loopSamples = capturedSampleCount.load(std::memory_order_relaxed);
    const auto maximumCrossfade = juce::jmax(0, loopSamples / 2);
    const auto crossfadeSamples = juce::jlimit(
        0, maximumCrossfade, effectiveCrossfadeSamples.load(std::memory_order_relaxed));

    if (crossfadeSamples == 0 || playbackPosition < loopSamples - crossfadeSamples)
        return loopBuffer.getSample(channel, playbackPosition);

    const auto crossfadeIndex = playbackPosition - (loopSamples - crossfadeSamples);
    const auto alpha = static_cast<float>(crossfadeIndex + 1)
                       / static_cast<float>(crossfadeSamples + 1);
    const auto useLinearFade = preferLinearRepairFade.load(std::memory_order_relaxed);
    const auto tailGain = useLinearFade ? 1.0f - alpha
                                        : std::cos(alpha * juce::MathConstants<float>::halfPi);
    const auto headGain = useLinearFade ? alpha
                                        : std::sin(alpha * juce::MathConstants<float>::halfPi);
    const auto tail = loopBuffer.getSample(channel, playbackPosition);
    const auto head = loopBuffer.getSample(channel, crossfadeIndex);
    return tailGain * tail + headGain * head;
}

void LoopEngine::advancePlaybackPosition() noexcept
{
    const auto loopSamples = capturedSampleCount.load(std::memory_order_relaxed);
    ++playbackPosition;
    if (playbackPosition < loopSamples)
        return;

    const auto crossfadeSamples = juce::jlimit(
        0, loopSamples / 2, effectiveCrossfadeSamples.load(std::memory_order_relaxed));
    playbackPosition = crossfadeSamples;
}

float LoopEngine::readSourceSample(const int channel) const noexcept
{
    return currentSourceBuffer.getSample(channel, sourcePlaybackPosition);
}

void LoopEngine::advanceSourcePlaybackPosition() noexcept
{
    const auto start = analysisRangeStartSample.load(std::memory_order_relaxed);
    const auto end = analysisRangeEndSample.load(std::memory_order_relaxed);
    ++sourcePlaybackPosition;
    if (sourcePlaybackPosition >= end || sourcePlaybackPosition < start)
        sourcePlaybackPosition = start;
}

void LoopEngine::startAnalysisThread()
{
    stopRequested.store(false);
    analysisPending.store(false);
    analysisThread = std::thread([this] { analysisLoop(); });
}

void LoopEngine::stopAnalysisThread()
{
    stopRequested.store(true, std::memory_order_release);
    analysisCondition.notify_all();
    if (analysisThread.joinable())
        analysisThread.join();
    analysisPending.store(false);
}
