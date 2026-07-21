#include "LoopEngine.h"

#include <cmath>

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
    crossfadeMilliseconds = juce::jlimit(0.0f, 250.0f, milliseconds);
}

void LoopEngine::beginCapture(const int startDelaySamples) noexcept
{
    requestedStartDelay.store(juce::jmax(0, startDelaySamples), std::memory_order_relaxed);
    captureRequested.store(true, std::memory_order_release);
}

void LoopEngine::clear() noexcept
{
    clearRequested.store(true, std::memory_order_release);
}

void LoopEngine::process(juce::AudioBuffer<float>& buffer, const float wetMix) noexcept
{
    applyPendingRequests();
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

        if (!hasReadLease || currentState != State::ready
            || capturedSampleCount.load(std::memory_order_relaxed) <= 0)
        {
            mixSmoother.skip(1);
            continue;
        }

        const auto channelCount = juce::jmin(buffer.getNumChannels(), loopBuffer.getNumChannels());
        const auto mix = mixSmoother.getNextValue();
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto dry = buffer.getSample(channel, sample);
            const auto wet = readLoopSample(channel);
            buffer.setSample(channel, sample, dry + mix * (wet - dry));
        }

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
float LoopEngine::getLevelScore() const noexcept { return levelScore.load(); }
float LoopEngine::getSlopeScore() const noexcept { return slopeScore.load(); }
float LoopEngine::getSpectrumScore() const noexcept { return spectrumScore.load(); }
float LoopEngine::getPhaseScore() const noexcept { return phaseScore.load(); }
float LoopEngine::getStereoScore() const noexcept { return stereoScore.load(); }
int LoopEngine::getCapturedSampleCount() const noexcept { return capturedSampleCount.load(); }

void LoopEngine::applyPendingRequests() noexcept
{
    if (clearRequested.exchange(false, std::memory_order_acq_rel))
    {
        generation.fetch_add(1, std::memory_order_acq_rel);
        captureWritePosition = 0;
        playbackPosition = 0;
        capturedSampleCount.store(0);
        captureProgress.store(0.0f);
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
        0, maximumCrossfade, juce::roundToInt(sampleRate * crossfadeMilliseconds * 0.001));

    if (crossfadeSamples == 0 || playbackPosition < loopSamples - crossfadeSamples)
        return loopBuffer.getSample(channel, playbackPosition);

    const auto crossfadeIndex = playbackPosition - (loopSamples - crossfadeSamples);
    const auto alpha = static_cast<float>(crossfadeIndex + 1)
                       / static_cast<float>(crossfadeSamples);
    const auto tail = loopBuffer.getSample(channel, playbackPosition);
    const auto head = loopBuffer.getSample(channel, crossfadeIndex);
    return tail + alpha * (head - tail);
}

void LoopEngine::advancePlaybackPosition() noexcept
{
    const auto loopSamples = capturedSampleCount.load(std::memory_order_relaxed);
    ++playbackPosition;
    if (playbackPosition < loopSamples)
        return;

    const auto crossfadeSamples = juce::jlimit(
        0, loopSamples / 2, juce::roundToInt(sampleRate * crossfadeMilliseconds * 0.001));
    playbackPosition = crossfadeSamples;
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
        const auto result = LoopAnalyzer::findBestLoop(captureBuffer,
                                                       sampleRate,
                                                       requestedLoopSamples,
                                                       searchRadiusSamples);
        if (requestGeneration != generation.load(std::memory_order_acquire)
            || state.load(std::memory_order_acquire) != State::analysing)
            continue;

        const auto selectedSamples = juce::jmax(1, result.endSample - result.startSample);
        {
            const std::scoped_lock lock(loopDataMutex);
            loopBuffer.setSize(captureBuffer.getNumChannels(), selectedSamples,
                               false, false, false);
            for (int channel = 0; channel < loopBuffer.getNumChannels(); ++channel)
                loopBuffer.copyFrom(channel, 0, captureBuffer, channel,
                                    result.startSample, selectedSamples);
        }

        playbackPosition = 0;
        capturedSampleCount.store(selectedSamples);
        levelScore.store(result.level);
        slopeScore.store(result.slope);
        spectrumScore.store(result.spectrum);
        phaseScore.store(result.phase);
        stereoScore.store(result.stereo);
        seamQuality.store(result.overall);
        state.store(State::ready, std::memory_order_release);
    }
}

void LoopEngine::resetScores() noexcept
{
    seamQuality.store(0.0f);
    levelScore.store(0.0f);
    slopeScore.store(0.0f);
    spectrumScore.store(0.0f);
    phaseScore.store(0.0f);
    stereoScore.store(0.0f);
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
    if (decompressed.readInt() != stateMagic || decompressed.readInt() != stateVersion)
        return false;

    const auto savedSampleRate = decompressed.readDouble();
    const auto channels = decompressed.readInt();
    const auto samples = decompressed.readInt();
    if (savedSampleRate <= 0.0 || channels < 1 || channels > 2 || samples < 1
        || samples > juce::roundToInt(savedSampleRate * maximumLoopSeconds))
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
                                           juce::roundToInt(sampleRate * maximumLoopSeconds),
                                           juce::roundToInt(static_cast<double>(samples)
                                                            * sampleRate / savedSampleRate));
    {
        const std::scoped_lock lock(loopDataMutex);
        loopBuffer.setSize(channels, targetSamples, false, false, false);
        for (int channel = 0; channel < channels; ++channel)
        {
            for (int index = 0; index < targetSamples; ++index)
            {
                const auto sourcePosition = static_cast<double>(index) * savedSampleRate / sampleRate;
                const auto lower = juce::jlimit(0, samples - 1, static_cast<int>(sourcePosition));
                const auto upper = juce::jmin(samples - 1, lower + 1);
                const auto fraction = static_cast<float>(sourcePosition - static_cast<double>(lower));
                const auto value = restored.getSample(channel, lower)
                                   + fraction * (restored.getSample(channel, upper)
                                                 - restored.getSample(channel, lower));
                loopBuffer.setSample(channel, index, value);
            }
        }
    }

    playbackPosition = 0;
    capturedSampleCount.store(targetSamples);
    seamQuality.store(savedOverall);
    levelScore.store(savedLevel);
    slopeScore.store(savedSlope);
    spectrumScore.store(savedSpectrum);
    phaseScore.store(savedPhase);
    stereoScore.store(savedStereo);
    captureProgress.store(1.0f);
    state.store(State::ready, std::memory_order_release);
    return true;
}
