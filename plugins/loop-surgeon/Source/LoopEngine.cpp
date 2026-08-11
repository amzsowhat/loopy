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
                ? juce::String("Spectral Drift") : juce::String("Organism");
        return "Texture " + juce::String(index + 1) + "  |  " + structure
               + "  |  " + juce::String(seconds, 1)
               + " s  |  QC " + (variant.passedQualityGate ? "PASS " : "CHECK ")
               + juce::String(variant.qualityScore, 0);
    }
    if (!juce::isPositiveAndBelow(index, static_cast<int>(sourceCandidates.size())))
        return {};
    const auto& candidate = sourceCandidates[static_cast<size_t>(index)];
    const auto repairSamples = candidate.repairOverlapSamples;
    const auto seconds = static_cast<double>(candidate.endSample - candidate.startSample - repairSamples)
                         / sampleRate;
    return juce::String(candidate.rotationSample >= 0 ? "Repair " : "Candidate ")
           + juce::String(index + 1) + "  |  " + juce::String(seconds, 2)
           + " s  |  QC " + (candidate.overall >= 68.0f && candidate.repair >= 58.0f
                                ? "PASS " : "CHECK ")
           + juce::String(candidate.overall, 0);
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
    auto textureTransition = 0.0f;
    auto textureClosure = 0.0f;
    auto textureSpectrum = 0.0f;
    auto textureLoudness = 0.0f;
    auto texturePhase = 0.0f;
    auto texturePosition = 0.0f;
    auto textureDiversity = 0.0f;
    auto textureTransient = 0.0f;
    auto textureStability = 0.0f;
    auto textureRepeatSafety = 0.0f;
    auto textureTruePeak = -100.0f;
    auto textureQuality = 0.0f;
    auto textureGatePassed = false;
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
            textureTransition = texture.transitionQuality;
            textureClosure = texture.closureQuality;
            textureSpectrum = texture.spectrumPreservation;
            textureLoudness = texture.loudnessPreservation;
            texturePhase = texture.phasePreservation;
            texturePosition = texture.positionPreservation;
            textureDiversity = texture.diversity;
            textureTransient = texture.transientPreservation;
            textureStability = texture.macroStability;
            textureRepeatSafety = texture.repeatSafety;
            textureTruePeak = texture.truePeakDbtp;
            textureQuality = texture.qualityScore;
            textureGatePassed = texture.passedQualityGate;
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
    const auto nextSnapshot = RenderQuality::analyseSourceAndOutput(
        snapshotSource, snapshotOutput, sampleRate);
    {
        const std::scoped_lock snapshotLock(signalSnapshotMutex);
        signalSnapshot = nextSnapshot;
    }
    const auto selectedTruePeak = selectedAudio.getNumSamples() > 0
        ? juce::Decibels::gainToDecibels(juce::jmax(
            1.0e-9f, RenderQuality::estimateCircularTruePeak(selectedAudio)))
        : -100.0f;
    const auto selectedGatePassed = selectedAudio.getNumSamples() > 0
        && selectedTruePeak <= -0.85f
        && result.overall >= 68.0f && result.repair >= 58.0f;
    if (selectedTexture)
    {
        capturedSampleCount.store(textureSamples);
        effectiveCrossfadeSamples.store(0);
        selectedRotationSample.store(-1);
        playbackPosition = 0;
        seamQuality.store(textureClosure);
        waveformScore.store(textureTransition);
        levelScore.store(textureLoudness);
        slopeScore.store(textureTransient);
        spectrumScore.store(textureSpectrum);
        phaseScore.store(texturePhase);
        stereoScore.store(texturePosition);
        transientScore.store(textureTransient);
        periodicityScore.store(textureStability);
        repairScore.store(textureTransition);
        repeatSafetyScore.store(textureRepeatSafety);
        truePeakDbtp.store(textureTruePeak);
        renderQualityScore.store(textureQuality);
        qualityGatePassed.store(textureGatePassed);
        lowConfidence.store(!textureGatePassed
                            || textureClosure < 62.0f
                            || textureDiversity < 58.0f
                            || textureStability < 58.0f);
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
    qualityGatePassed.store(selectedGatePassed);
    seamQuality.store(result.overall);
    lowConfidence.store(!qualityGatePassed.load(std::memory_order_relaxed));
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
    const auto useLinearFade = phaseScore.load() >= 75.0f;
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

RenderQuality::SignalSnapshot LoopEngine::getSignalSnapshot() const
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
    resetScores();
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
    const auto useLinearFade = phaseScore.load(std::memory_order_relaxed) >= 75.0f;
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

void LoopEngine::analysisLoop()
{
    for (;;)
    {
        std::unique_lock waitLock(analysisWaitMutex);
        analysisCondition.wait(waitLock, [this]
        {
            return stopRequested.load(std::memory_order_acquire)
                   || analysisPending.load(std::memory_order_acquire);
        });
        waitLock.unlock();

        if (stopRequested.load(std::memory_order_acquire))
            return;

        if (!analysisPending.exchange(false, std::memory_order_acq_rel))
            continue;

        const auto requestGeneration = generation.load(std::memory_order_acquire);
        analysisProgress.store(0.03f, std::memory_order_relaxed);
        const auto imported = importedAnalysisPending.exchange(false, std::memory_order_acq_rel);
        juce::AudioBuffer<float> importedSource;
        juce::String importedName;
        int importedOffset = 0;
        int importedFullSourceSamples = 0;
        bool importedReplacesCurrentSource = true;
        int importedRepairOverlap = 0;
        {
            const std::scoped_lock lock(sourceDataMutex);
            importedSource = std::move(pendingSourceBuffer);
            importedName = std::move(pendingSourceName);
            importedOffset = pendingSourceOffset;
            importedFullSourceSamples = pendingFullSourceSamples;
            importedReplacesCurrentSource = pendingReplacesCurrentSource;
        }

        LoopAnalysisResult result;
        LoopAnalysisReport importedReport;
        std::vector<TextureSynthesisResult> generatedTextures;
        juce::AudioBuffer<float> capturedSourceView;
        std::vector<float> replacementWaveform;
        bool resultLowConfidence = false;
        const auto selectedMode = generationMode.load(std::memory_order_relaxed);
        const auto useTexture = selectedMode == GenerationMode::textureLoop;
        const auto* analysisSource = &importedSource;
        if (imported)
        {
            analysisSource = &importedSource;
        }
        else
        {
            capturedSourceView.setDataToReferTo(captureBuffer.getArrayOfWritePointers(),
                                                captureBuffer.getNumChannels(),
                                                captureSampleCount);
            importedSource = capturedSourceView;
            importedName = "DAW Capture";
            importedOffset = 0;
            importedFullSourceSamples = importedSource.getNumSamples();
            importedReplacesCurrentSource = true;
            analysisSource = &importedSource;
        }

        if (importedReplacesCurrentSource)
            replacementWaveform = buildWaveformPreview(importedSource);

        if (!useTexture)
        {
            importedRepairOverlap = juce::jlimit(0, importedSource.getNumSamples() / 8,
                juce::jmin(importedSource.getNumSamples() / 4,
                    juce::roundToInt(sampleRate
                        * crossfadeMilliseconds.load(std::memory_order_relaxed) * 0.001)));
            const auto requestedSeconds = repairDurationSeconds.load(
                std::memory_order_relaxed);
            const auto requestedSamples = requestedSeconds > 0.0f
                ? juce::roundToInt(sampleRate * requestedSeconds) : 0;
            importedReport = requestedSamples > 0
                ? LoopAnalyzer::analyzeRotateRepairExact(
                    importedSource, sampleRate, requestedSamples, 3,
                    importedRepairOverlap)
                : LoopAnalyzer::analyzeRotateRepair(
                    importedSource, sampleRate, 3, importedRepairOverlap);
            analysisProgress.store(0.82f, std::memory_order_relaxed);
            if (!importedReport.candidates.empty())
                result = importedReport.candidates.front();
            resultLowConfidence = importedReport.lowConfidence;
        }
        else
        {
            TextureSynthesisSettings settings;
            settings.durationSeconds = textureDurationSeconds.load(
                std::memory_order_relaxed);
            settings.variation = textureVariation.load(std::memory_order_relaxed);
            settings.flatten = textureFlatten.load(std::memory_order_relaxed);
            settings.sourceMatch = textureSourceMatch.load(std::memory_order_relaxed);
            settings.structure = textureStructure.load(std::memory_order_relaxed);
            const auto baseSeed = textureSeed.load(std::memory_order_relaxed);
            generatedTextures.reserve(2);
            constexpr uint32_t maximumAttempts = 6;
            for (uint32_t attempt = 0; attempt < maximumAttempts; ++attempt)
            {
                if (requestGeneration != generation.load(std::memory_order_acquire))
                    break;
                settings.seed = baseSeed + attempt * 0x85ebca6bu;
                auto candidate = TextureSynthesizer::synthesize(
                    importedSource, sampleRate, settings);
                if (candidate.audio.getNumSamples() == 0)
                    continue;
                generatedTextures.push_back(std::move(candidate));
                std::sort(generatedTextures.begin(), generatedTextures.end(),
                    [] (const auto& left, const auto& right)
                    {
                        if (left.passedQualityGate != right.passedQualityGate)
                            return left.passedQualityGate > right.passedQualityGate;
                        return left.qualityScore > right.qualityScore;
                    });
                if (generatedTextures.size() > 2u)
                    generatedTextures.pop_back();
                if (generatedTextures.size() == 2u
                    && std::all_of(generatedTextures.begin(), generatedTextures.end(),
                        [] (const auto& texture) { return texture.passedQualityGate; }))
                    break;
                analysisProgress.store(
                    0.08f + 0.78f * static_cast<float>(attempt + 1u)
                                / static_cast<float>(maximumAttempts),
                    std::memory_order_relaxed);
            }
        }
        if (requestGeneration != generation.load(std::memory_order_acquire)
            || state.load(std::memory_order_acquire) != State::analysing)
            continue;

        const auto sourceSampleCount = analysisSource->getNumSamples();
        const auto textureFailed = useTexture
            && (generatedTextures.empty()
                || generatedTextures.front().audio.getNumSamples() == 0);
        const auto loopFailed = !useTexture && importedReport.candidates.empty();
        if (textureFailed || loopFailed)
        {
            const std::scoped_lock lock(sourceDataMutex);
            if (importedReplacesCurrentSource)
            {
                currentSourceBuffer = std::move(importedSource);
                waveformPreview = std::move(replacementWaveform);
                sourceRevision.fetch_add(1, std::memory_order_release);
            }
            currentSourceName = importedName;
            sourceCandidates.clear();
            textureVariants.clear();
            candidateRevision.fetch_add(1, std::memory_order_release);
            capturedSampleCount.store(0);
            selectedStartSample.store(importedOffset);
            selectedEndSample.store(importedOffset + sourceSampleCount);
            selectedRotationSample.store(-1);
            selectedSourceSamples.store(importedFullSourceSamples);
            analysisRangeStartSample.store(importedOffset);
            analysisRangeEndSample.store(importedOffset + sourceSampleCount);
            sourcePlaybackPosition = importedOffset;
            analysisProgress.store(1.0f, std::memory_order_relaxed);
            lowConfidence.store(true);
            state.store(State::failed, std::memory_order_release);
            continue;
        }

        if (useTexture)
        {
            const auto nextSnapshot = RenderQuality::analyseSourceAndOutput(
                *analysisSource, generatedTextures.front().audio, sampleRate);
            const auto primarySamples = generatedTextures.front().audio.getNumSamples();
            const auto primaryTransition = generatedTextures.front().transitionQuality;
            const auto primaryClosure = generatedTextures.front().closureQuality;
            const auto primarySpectrum = generatedTextures.front().spectrumPreservation;
            const auto primaryLoudness = generatedTextures.front().loudnessPreservation;
            const auto primaryPhase = generatedTextures.front().phasePreservation;
            const auto primaryPosition = generatedTextures.front().positionPreservation;
            const auto primaryDiversity = generatedTextures.front().diversity;
            const auto primaryTransient = generatedTextures.front().transientPreservation;
            const auto primaryStability = generatedTextures.front().macroStability;
            const auto primaryRepeatSafety = generatedTextures.front().repeatSafety;
            const auto primaryTruePeak = generatedTextures.front().truePeakDbtp;
            const auto primaryQuality = generatedTextures.front().qualityScore;
            const auto primaryGatePassed = generatedTextures.front().passedQualityGate;
            {
                const std::scoped_lock lock(sourceDataMutex);
                currentSourceName = importedName;
                if (importedReplacesCurrentSource)
                {
                    currentSourceBuffer = std::move(importedSource);
                    waveformPreview = std::move(replacementWaveform);
                }
                sourceCandidates.clear();
                textureVariants = std::move(generatedTextures);
                {
                    const std::scoped_lock loopLock(loopDataMutex);
                    std::swap(loopBuffer, textureVariants.front().audio);
                }
                activeTextureVariant.store(0, std::memory_order_relaxed);
                candidateRevision.fetch_add(1, std::memory_order_release);
                if (importedReplacesCurrentSource)
                    sourceRevision.fetch_add(1, std::memory_order_release);
            }
            capturedSampleCount.store(primarySamples);
            effectiveCrossfadeSamples.store(0);
            playbackPosition = 0;
            previewMode.store(PreviewMode::loop);
            waveformScore.store(primaryTransition);
            levelScore.store(primaryLoudness);
            slopeScore.store(primaryTransient);
            spectrumScore.store(primarySpectrum);
            phaseScore.store(primaryPhase);
            stereoScore.store(primaryPosition);
            transientScore.store(primaryTransient);
            periodicityScore.store(primaryStability);
            repairScore.store(primaryTransition);
            repeatSafetyScore.store(primaryRepeatSafety);
            truePeakDbtp.store(primaryTruePeak);
            renderQualityScore.store(primaryQuality);
            qualityGatePassed.store(primaryGatePassed);
            seamQuality.store(primaryClosure);
            lowConfidence.store(!primaryGatePassed
                                || primaryClosure < 62.0f
                                || primaryDiversity < 58.0f
                                || primaryStability < 58.0f);
            selectedStartSample.store(importedOffset);
            selectedEndSample.store(importedOffset + sourceSampleCount);
            selectedRotationSample.store(-1);
            selectedSourceSamples.store(importedFullSourceSamples);
            analysisRangeStartSample.store(importedOffset);
            analysisRangeEndSample.store(importedOffset + sourceSampleCount);
            sourcePlaybackPosition = importedOffset;
            lastUsedGenerationMode.store(GenerationMode::textureLoop);
            {
                const std::scoped_lock snapshotLock(signalSnapshotMutex);
                signalSnapshot = nextSnapshot;
            }
            state.store(State::ready, std::memory_order_release);
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
