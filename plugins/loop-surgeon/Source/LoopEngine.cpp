#include "LoopEngine.h"

#include <cmath>
#include <utility>

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
    resetScores();
    mixSmoother.reset(sampleRate, 0.02);
    mixSmoother.setCurrentAndTargetValue(1.0f);
    effectiveCrossfadeSamples.store(0);
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
        constexpr auto previewBins = 320;
        waveformPreview.assign(previewBins, 0.0f);
        for (int bin = 0; bin < previewBins; ++bin)
        {
            const auto first = bin * source.getNumSamples() / previewBins;
            const auto last = juce::jmax(first + 1, (bin + 1) * source.getNumSamples() / previewBins);
            float peak = 0.0f;
            for (int channel = 0; channel < source.getNumChannels(); ++channel)
                peak = juce::jmax(peak, source.getMagnitude(channel, first, last - first));
            waveformPreview[static_cast<size_t>(bin)] = peak;
        }
        pendingSourceBuffer = std::move(source);
        pendingSourceName = std::move(sourceName);
        pendingSourceOffset = 0;
        pendingFullSourceSamples = pendingSourceBuffer.getNumSamples();
        pendingReplacesCurrentSource = true;
    }
    resetScores();
    captureProgress.store(1.0f);
    importedAnalysisPending.store(true, std::memory_order_release);
    analysisPending.store(true, std::memory_order_release);
    analysisCondition.notify_one();
}

bool LoopEngine::reanalyzeSourceRange(const float startProportion, const float endProportion)
{
    generation.fetch_add(1, std::memory_order_acq_rel);
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
        const auto total = currentSourceBuffer.getNumSamples();
        const auto start = juce::jlimit(0, juce::jmax(0, total - 32),
                                        juce::roundToInt(startProportion * total));
        const auto end = juce::jlimit(start + 32, total,
                                      juce::roundToInt(endProportion * total));
        if (total < 32 || end - start < 32)
        {
            state.store(currentSourceBuffer.getNumSamples() > 0 ? State::ready : State::empty,
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

bool LoopEngine::setManualLoopRange(const float startProportion, const float endProportion)
{
    generation.fetch_add(1, std::memory_order_acq_rel);
    state.store(State::analysing, std::memory_order_release);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();

    LoopAnalysisResult result;
    juce::AudioBuffer<float> selectedAudio;
    int displayedEnd = 0;
    int manualRepairSamples = 0;
    {
        const std::scoped_lock sourceLock(sourceDataMutex);
        const auto total = currentSourceBuffer.getNumSamples();
        if (total < 32)
        {
            state.store(State::empty, std::memory_order_release);
            return false;
        }
        const auto rangeStart = juce::jlimit(0, total - 32, analysisRangeStartSample.load());
        const auto rangeEnd = juce::jlimit(rangeStart + 32, total, analysisRangeEndSample.load());
        const auto start = juce::jlimit(rangeStart, rangeEnd - 32,
                                        juce::roundToInt(startProportion * total));
        displayedEnd = juce::jlimit(start + 32, rangeEnd,
                                     juce::roundToInt(endProportion * total));
        const auto requestedRepair = juce::roundToInt(
            sampleRate * crossfadeMilliseconds.load(std::memory_order_relaxed) * 0.001);
        manualRepairSamples = juce::jmin(requestedRepair, (displayedEnd - start) / 2,
                                         rangeEnd - displayedEnd);
        const auto rawEnd = displayedEnd + manualRepairSamples;
        result = LoopAnalyzer::evaluateFixedRange(currentSourceBuffer, sampleRate, start, rawEnd,
                                                  manualRepairSamples);
        selectedAudio.setSize(currentSourceBuffer.getNumChannels(), rawEnd - start,
                              false, false, false);
        for (int channel = 0; channel < selectedAudio.getNumChannels(); ++channel)
            selectedAudio.copyFrom(channel, 0, currentSourceBuffer, channel, start, rawEnd - start);
    }
    {
        const std::scoped_lock loopLock(loopDataMutex);
        loopBuffer = std::move(selectedAudio);
    }
    capturedSampleCount.store(loopBuffer.getNumSamples());
    effectiveCrossfadeSamples.store(manualRepairSamples);
    playbackPosition = manualRepairSamples;
    selectedStartSample.store(result.startSample);
    selectedEndSample.store(displayedEnd);
    waveformScore.store(result.waveform);
    levelScore.store(result.level);
    slopeScore.store(result.slope);
    spectrumScore.store(result.spectrum);
    phaseScore.store(result.phase);
    stereoScore.store(result.stereo);
    transientScore.store(result.transient);
    periodicityScore.store(result.periodicity);
    repairScore.store(result.repair);
    seamQuality.store(result.overall);
    lowConfidence.store(result.overall < 62.0f);
    state.store(State::ready, std::memory_order_release);
    return true;
}

void LoopEngine::clear() noexcept
{
    clearRequested.store(true, std::memory_order_release);
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
        return "Texture " + juce::String(index + 1) + "  |  " + juce::String(seconds, 1)
               + " s  |  stability " + juce::String(variant.macroStability, 0);
    }
    if (!juce::isPositiveAndBelow(index, static_cast<int>(sourceCandidates.size())))
        return {};
    const auto& candidate = sourceCandidates[static_cast<size_t>(index)];
    const auto repairSamples = candidate.repairOverlapSamples;
    const auto seconds = static_cast<double>(candidate.endSample - candidate.startSample - repairSamples)
                         / sampleRate;
    return "Candidate " + juce::String(index + 1) + "  |  " + juce::String(seconds, 2)
           + " s  |  score " + juce::String(candidate.overall, 0);
}

void LoopEngine::selectCandidate(const int index)
{
    generation.fetch_add(1, std::memory_order_acq_rel);
    state.store(State::analysing, std::memory_order_release);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();

    LoopAnalysisResult result;
    auto selectedTexture = false;
    auto textureSamples = 0;
    auto textureTransition = 0.0f;
    auto textureClosure = 0.0f;
    auto textureSpectrum = 0.0f;
    auto textureStereo = 0.0f;
    auto textureDiversity = 0.0f;
    auto textureTransient = 0.0f;
    auto textureStability = 0.0f;
    juce::AudioBuffer<float> selectedAudio;
    {
        const std::scoped_lock sourceLock(sourceDataMutex);
        if (juce::isPositiveAndBelow(index, static_cast<int>(textureVariants.size())))
        {
            selectedTexture = true;
            auto& texture = textureVariants[static_cast<size_t>(index)];
            textureTransition = texture.transitionQuality;
            textureClosure = texture.closureQuality;
            textureSpectrum = texture.spectrumPreservation;
            textureStereo = texture.stereoPreservation;
            textureDiversity = texture.diversity;
            textureTransient = texture.transientPreservation;
            textureStability = texture.macroStability;
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
        capturedSampleCount.store(textureSamples);
        effectiveCrossfadeSamples.store(0);
        playbackPosition = 0;
        seamQuality.store(textureClosure);
        waveformScore.store(textureTransition);
        levelScore.store(textureTransition);
        slopeScore.store(textureTransient);
        spectrumScore.store(textureSpectrum);
        phaseScore.store(textureClosure);
        stereoScore.store(textureStereo);
        transientScore.store(textureTransient);
        periodicityScore.store(textureStability);
        repairScore.store(textureTransition);
        lowConfidence.store(textureClosure < 55.0f
                            || textureDiversity < 30.0f
                            || textureStability < 62.0f);
        lastUsedGenerationMode.store(GenerationMode::evolvingTexture);
        previewMode.store(PreviewMode::loop);
        previewRestartRequested.store(true, std::memory_order_release);
        state.store(State::ready, std::memory_order_release);
        return;
    }

    {
        const std::scoped_lock loopLock(loopDataMutex);
        loopBuffer = std::move(selectedAudio);
    }
    capturedSampleCount.store(result.endSample - result.startSample);
    selectedStartSample.store(result.startSample);
    const auto repairSamples = result.repairOverlapSamples;
    effectiveCrossfadeSamples.store(repairSamples);
    playbackPosition = repairSamples;
    selectedEndSample.store(result.endSample - repairSamples);
    waveformScore.store(result.waveform);
    levelScore.store(result.level);
    slopeScore.store(result.slope);
    spectrumScore.store(result.spectrum);
    phaseScore.store(result.phase);
    stereoScore.store(result.stereo);
    transientScore.store(result.transient);
    periodicityScore.store(result.periodicity);
    repairScore.store(result.repair);
    seamQuality.store(result.overall);
    lowConfidence.store(result.overall < 62.0f || result.repair < 55.0f);
    lastUsedGenerationMode.store(GenerationMode::seamLoop);
    state.store(State::ready, std::memory_order_release);
}

std::vector<float> LoopEngine::getWaveformPreview() const
{
    const std::scoped_lock lock(sourceDataMutex);
    return waveformPreview;
}

float LoopEngine::getLoopStartProportion() const noexcept
{
    return static_cast<float>(selectedStartSample.load())
           / static_cast<float>(juce::jmax(1, selectedSourceSamples.load()));
}

float LoopEngine::getLoopEndProportion() const noexcept
{
    return static_cast<float>(selectedEndSample.load())
           / static_cast<float>(juce::jmax(1, selectedSourceSamples.load()));
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

void LoopEngine::applyPendingRequests() noexcept
{
    if (clearRequested.exchange(false, std::memory_order_acq_rel))
    {
        generation.fetch_add(1, std::memory_order_acq_rel);
        captureWritePosition = 0;
        playbackPosition = 0;
        sourcePlaybackPosition = 0;
        capturedSampleCount.store(0);
        captureProgress.store(0.0f);
        effectiveCrossfadeSamples.store(0, std::memory_order_relaxed);
        previewPlaying.store(false, std::memory_order_release);
        resetScores();
        state.store(State::empty, std::memory_order_release);
    }

    if (!captureRequested.exchange(false, std::memory_order_acq_rel)
        || state.load(std::memory_order_acquire) == State::analysing
        || captureBuffer.getNumSamples() == 0)
        return;

    generation.fetch_add(1, std::memory_order_acq_rel);
    requestedLoopSamples = juce::jlimit(1,
                                       captureBuffer.getNumSamples(),
                                       juce::roundToInt(sampleRate * loopLengthSeconds));
    searchRadiusSamples = juce::jmin(juce::roundToInt(sampleRate * searchRadiusSeconds),
                                    (captureBuffer.getNumSamples() - requestedLoopSamples) / 2);
    captureSampleCount = juce::jmin(captureBuffer.getNumSamples(),
                                    requestedLoopSamples + 2 * searchRadiusSamples);
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
        const auto imported = importedAnalysisPending.exchange(false, std::memory_order_acq_rel);
        juce::AudioBuffer<float> importedSource;
        juce::String importedName;
        int importedOffset = 0;
        int importedFullSourceSamples = 0;
        bool importedReplacesCurrentSource = true;
        int importedRepairOverlap = 0;
        if (imported)
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
        bool resultLowConfidence = false;
        auto selectedMode = generationMode.load(std::memory_order_relaxed);
        auto useTexture = imported && selectedMode == GenerationMode::evolvingTexture;
        const auto* analysisSource = &captureBuffer;
        if (imported)
        {
            analysisSource = &importedSource;
            if (selectedMode != GenerationMode::evolvingTexture)
            {
                const auto minimum = juce::jmin(importedSource.getNumSamples() / 2,
                                                 juce::roundToInt(sampleRate * 0.25));
                const auto maximum = juce::jmin(importedSource.getNumSamples() - 1,
                                                 juce::roundToInt(sampleRate * maximumLoopSeconds));
                importedRepairOverlap = juce::jlimit(0, minimum / 2,
                    juce::jmin(importedSource.getNumSamples() / 4,
                        juce::roundToInt(sampleRate
                            * crossfadeMilliseconds.load(std::memory_order_relaxed) * 0.001)));
                importedReport = LoopAnalyzer::analyzeSource(
                    importedSource, sampleRate, minimum, maximum, 3, importedRepairOverlap);
                if (!importedReport.candidates.empty())
                    result = importedReport.candidates.front();
                resultLowConfidence = importedReport.lowConfidence;
            }

            if (selectedMode == GenerationMode::automatic)
            {
                const auto confidentlyPeriodic = !importedReport.candidates.empty()
                    && !importedReport.lowConfidence
                    && result.periodicity >= 72.0f
                    && result.transient >= 58.0f
                    && result.overall >= 68.0f;
                useTexture = !confidentlyPeriodic;
                selectedMode = useTexture ? GenerationMode::evolvingTexture
                                          : GenerationMode::seamLoop;
            }

            if (useTexture)
            {
                TextureSynthesisSettings settings;
                settings.durationSeconds = textureDurationSeconds.load(
                    std::memory_order_relaxed);
                settings.variation = textureVariation.load(std::memory_order_relaxed);
                const auto baseSeed = textureSeed.load(std::memory_order_relaxed);
                generatedTextures.reserve(3);
                for (uint32_t variant = 0; variant < 3; ++variant)
                {
                    settings.seed = baseSeed + variant * 0x85ebca6bu;
                    generatedTextures.push_back(TextureSynthesizer::synthesize(
                        importedSource, sampleRate, settings));
                }
            }
        }
        else
        {
            selectedMode = GenerationMode::seamLoop;
            capturedSourceView.setDataToReferTo(captureBuffer.getArrayOfWritePointers(),
                                                captureBuffer.getNumChannels(),
                                                captureSampleCount);
            analysisSource = &capturedSourceView;
            result = LoopAnalyzer::findBestLoop(capturedSourceView, sampleRate,
                                                requestedLoopSamples, searchRadiusSamples);
        }
        if (requestGeneration != generation.load(std::memory_order_acquire)
            || state.load(std::memory_order_acquire) != State::analysing)
            continue;

        const auto textureFailed = useTexture
            && (generatedTextures.empty()
                || generatedTextures.front().audio.getNumSamples() == 0);
        const auto loopFailed = imported && !useTexture && importedReport.candidates.empty();
        if (imported && (textureFailed || loopFailed))
        {
            const std::scoped_lock lock(sourceDataMutex);
            if (importedReplacesCurrentSource)
                currentSourceBuffer = std::move(importedSource);
            currentSourceName = importedName;
            sourceCandidates.clear();
            textureVariants.clear();
            candidateRevision.fetch_add(1, std::memory_order_release);
            if (importedReplacesCurrentSource)
                sourceRevision.fetch_add(1, std::memory_order_release);
            capturedSampleCount.store(0);
            lowConfidence.store(true);
            state.store(State::failed, std::memory_order_release);
            continue;
        }

        const auto sourceSampleCount = analysisSource->getNumSamples();
        if (useTexture)
        {
            const auto primarySamples = generatedTextures.front().audio.getNumSamples();
            const auto primaryTransition = generatedTextures.front().transitionQuality;
            const auto primaryClosure = generatedTextures.front().closureQuality;
            const auto primarySpectrum = generatedTextures.front().spectrumPreservation;
            const auto primaryStereo = generatedTextures.front().stereoPreservation;
            const auto primaryDiversity = generatedTextures.front().diversity;
            const auto primaryTransient = generatedTextures.front().transientPreservation;
            const auto primaryStability = generatedTextures.front().macroStability;
            {
                const std::scoped_lock lock(sourceDataMutex);
                currentSourceName = importedName;
                if (importedReplacesCurrentSource)
                    currentSourceBuffer = std::move(importedSource);
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
            levelScore.store(primaryTransition);
            slopeScore.store(primaryTransient);
            spectrumScore.store(primarySpectrum);
            phaseScore.store(primaryClosure);
            stereoScore.store(primaryStereo);
            transientScore.store(primaryTransient);
            periodicityScore.store(primaryStability);
            repairScore.store(primaryTransition);
            seamQuality.store(primaryClosure);
            lowConfidence.store(primaryClosure < 55.0f
                                || primaryDiversity < 30.0f
                                || primaryStability < 62.0f);
            selectedStartSample.store(importedOffset);
            selectedEndSample.store(importedOffset + sourceSampleCount);
            selectedSourceSamples.store(importedFullSourceSamples);
            analysisRangeStartSample.store(importedOffset);
            analysisRangeEndSample.store(importedOffset + sourceSampleCount);
            sourcePlaybackPosition = importedOffset;
            lastUsedGenerationMode.store(GenerationMode::evolvingTexture);
            state.store(State::ready, std::memory_order_release);
            continue;
        }

        const auto selectedSamples = juce::jmax(1, result.endSample - result.startSample);
        {
            const std::scoped_lock lock(loopDataMutex);
            loopBuffer.setSize(analysisSource->getNumChannels(), selectedSamples,
                               false, false, false);
            for (int channel = 0; channel < loopBuffer.getNumChannels(); ++channel)
                loopBuffer.copyFrom(channel, 0, *analysisSource, channel,
                                    result.startSample, selectedSamples);
        }

        if (imported)
        {
            const std::scoped_lock lock(sourceDataMutex);
            currentSourceName = importedName;
            if (importedReplacesCurrentSource)
                currentSourceBuffer = std::move(importedSource);
            sourceCandidates = importedReport.candidates;
            textureVariants.clear();
            for (auto& candidate : sourceCandidates)
            {
                candidate.startSample += importedOffset;
                candidate.endSample += importedOffset;
            }
            candidateRevision.fetch_add(1, std::memory_order_release);
            if (importedReplacesCurrentSource)
                sourceRevision.fetch_add(1, std::memory_order_release);
        }

        capturedSampleCount.store(selectedSamples);
        effectiveCrossfadeSamples.store(imported ? result.repairOverlapSamples
            : juce::jlimit(0, selectedSamples / 3,
                juce::roundToInt(sampleRate
                    * crossfadeMilliseconds.load(std::memory_order_relaxed) * 0.001)));
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
        lowConfidence.store(resultLowConfidence);
        lastUsedGenerationMode.store(GenerationMode::seamLoop);
        seamQuality.store(result.overall);
        selectedStartSample.store(result.startSample + (imported ? importedOffset : 0));
        selectedEndSample.store(result.endSample - (imported ? result.repairOverlapSamples : 0)
                                 + (imported ? importedOffset : 0));
        selectedSourceSamples.store(imported ? importedFullSourceSamples : sourceSampleCount);
        if (imported)
        {
            analysisRangeStartSample.store(importedOffset);
            analysisRangeEndSample.store(importedOffset + sourceSampleCount);
            sourcePlaybackPosition = importedOffset;
        }
        state.store(State::ready, std::memory_order_release);
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
    lowConfidence.store(false);
}

juce::MemoryBlock LoopEngine::createLoopState() const
{
    juce::MemoryBlock result;
    const std::scoped_lock lock(loopDataMutex);
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
        for (int channel = 0; channel < loopBuffer.getNumChannels(); ++channel)
            compressed.write(loopBuffer.getReadPointer(channel),
                             static_cast<size_t>(loopBuffer.getNumSamples()) * sizeof(float));
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
    const auto savedMode = savedVersion >= 2
        ? static_cast<GenerationMode>(decompressed.readInt())
        : GenerationMode::seamLoop;
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
    juce::AudioBuffer<float> restored(channels, samples);
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto bytes = static_cast<int>(static_cast<size_t>(samples) * sizeof(float));
        if (decompressed.read(restored.getWritePointer(channel), bytes) != bytes)
            return false;
    }

    generation.fetch_add(1, std::memory_order_acq_rel);
    state.store(State::analysing, std::memory_order_release);
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    const auto targetSamples = juce::jlimit(1,
                                           juce::roundToInt(sampleRate * maximumTextureSeconds),
                                           juce::roundToInt(static_cast<double>(samples)
                                                            * sampleRate / savedSampleRate));
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
    }

    capturedSampleCount.store(targetSamples);
    effectiveCrossfadeSamples.store(savedMode == GenerationMode::evolvingTexture ? 0
        : juce::jlimit(0, targetSamples / 3,
            juce::roundToInt(sampleRate
                * crossfadeMilliseconds.load(std::memory_order_relaxed) * 0.001)));
    playbackPosition = effectiveCrossfadeSamples.load(std::memory_order_relaxed);
    seamQuality.store(savedOverall);
    levelScore.store(savedLevel);
    slopeScore.store(savedSlope);
    spectrumScore.store(savedSpectrum);
    phaseScore.store(savedPhase);
    stereoScore.store(savedStereo);
    repairScore.store(savedOverall);
    lastUsedGenerationMode.store(savedMode);
    captureProgress.store(1.0f);
    previewPlaying.store(false, std::memory_order_release);
    state.store(State::ready, std::memory_order_release);
    return true;
}
