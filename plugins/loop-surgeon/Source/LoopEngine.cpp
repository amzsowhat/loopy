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
    effectiveCrossfadeSamples.store(0);
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
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    {
        const std::scoped_lock lock(sourceDataMutex);
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
    while (activeAudioReaders.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();

    {
        const std::scoped_lock lock(sourceDataMutex);
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
        result = LoopAnalyzer::evaluateFixedRange(currentSourceBuffer, sampleRate, start, rawEnd);
        selectedAudio.setSize(currentSourceBuffer.getNumChannels(), rawEnd - start,
                              false, false, false);
        for (int channel = 0; channel < selectedAudio.getNumChannels(); ++channel)
            selectedAudio.copyFrom(channel, 0, currentSourceBuffer, channel, start, rawEnd - start);
    }
    {
        const std::scoped_lock loopLock(loopDataMutex);
        loopBuffer = std::move(selectedAudio);
    }
    playbackPosition = 0;
    capturedSampleCount.store(loopBuffer.getNumSamples());
    effectiveCrossfadeSamples.store(manualRepairSamples);
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
    return static_cast<int>(sourceCandidates.size());
}

uint64_t LoopEngine::getCandidateRevision() const noexcept { return candidateRevision.load(); }
uint64_t LoopEngine::getSourceRevision() const noexcept { return sourceRevision.load(); }

juce::String LoopEngine::getCandidateDescription(const int index) const
{
    const std::scoped_lock lock(sourceDataMutex);
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
    juce::AudioBuffer<float> selectedAudio;
    {
        const std::scoped_lock sourceLock(sourceDataMutex);
        if (!juce::isPositiveAndBelow(index, static_cast<int>(sourceCandidates.size())))
        {
            state.store(State::ready, std::memory_order_release);
            return;
        }
        result = sourceCandidates[static_cast<size_t>(index)];
        const auto samples = result.endSample - result.startSample;
        selectedAudio.setSize(currentSourceBuffer.getNumChannels(), samples, false, false, false);
        for (int channel = 0; channel < selectedAudio.getNumChannels(); ++channel)
            selectedAudio.copyFrom(channel, 0, currentSourceBuffer, channel, result.startSample, samples);
    }
    {
        const std::scoped_lock loopLock(loopDataMutex);
        loopBuffer = std::move(selectedAudio);
    }
    playbackPosition = 0;
    capturedSampleCount.store(result.endSample - result.startSample);
    selectedStartSample.store(result.startSample);
    const auto repairSamples = result.repairOverlapSamples;
    effectiveCrossfadeSamples.store(repairSamples);
    selectedEndSample.store(r×u¶‰žËkºwµçQÕÉ¥¹œ°(€€€€€€€€€€€€€€€ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì)ô()Ù½¥1½½Á¹¥¹”èé™¥¹¥Í¡…ÁÑÕÉ” ¤¹½•á•ÁÐ)ì(€€€…ÁÑÕÉ•]É¥Ñ•A½Í¥Ñ¥½¸€ô…ÁÑÕÉ•M…µÁ±•½Õ¹Ðì(€€€…ÁÑÕÉ•AÉ½É•ÍÌ¹ÍÑ½É” Ä¸Á˜¤ì(€€€ÍÑ…Ñ”¹ÍÑ½É”¡MÑ…Ñ”èé…¹…±åÍ¥¹œ°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€…¹…±åÍ¥ÍA•¹‘¥¹œ¹ÍÑ½É”¡ÑÉÕ”°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€…¹…±åÍ¥Í½¹‘¥Ñ¥½¸¹¹½Ñ¥™å}½¹” ¤ì)ô()™±½…Ð1½½Á¹¥¹”èéÉ•…‘1½½ÁM…µÁ±”¡½¹ÍÐ¥¹Ð¡…¹¹•°¤½¹ÍÐ¹½•á•ÁÐ)ì(€€€½¹ÍÐ…ÕÑ¼±½½ÁM…µÁ±•Ì€ô…ÁÑÕÉ•‘M…µÁ±•½Õ¹Ð¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€½¹ÍÐ…ÕÑ¼µ…á¥µÕµÉ½ÍÍ™…‘”€ô©Õ”èé©µ…à À°±½½ÁM…µÁ±•Ì€¼€È¤ì(€€€½¹ÍÐ…ÕÑ¼É½ÍÍ™…‘•M…µÁ±•Ì€ô©Õ”èé©±¥µ¥Ð À°µ…á¥µÕµÉ½ÍÍ™…‘”°(€€€€€€€©Õ”èé©µ¥¸¡•™™•Ñ¥Ù•É½ÍÍ™…‘•M…µÁ±•Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤°(€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”(€€€€€€€€€€€€€€€€€€€€€€€¨É½ÍÍ™…‘•5¥±±¥Í•½¹‘Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤€¨€À¸ÀÀÄ¤¤¤ì((€€€¥˜€¡É½ÍÍ™…‘•M…µÁ±•Ì€ôô€ÀñðÁ±…å‰…­A½Í¥Ñ¥½¸€ð±½½ÁM…µÁ±•Ì€´É½ÍÍ™…‘•M…µÁ±•Ì¤(€€€€€€€É•ÑÕÉ¸±½½Á	Õ™™•È¹•ÑM…µÁ±”¡¡…¹¹•°°Á±…å‰…­A½Í¥Ñ¥½¸¤ì((€€€½¹ÍÐ…ÕÑ¼É½ÍÍ™…‘•%¹‘•à€ôÁ±…å‰…­A½Í¥Ñ¥½¸€´€¡±½½ÁM…µÁ±•Ì€´É½ÍÍ™…‘•M…µÁ±•Ì¤ì(€€€½¹ÍÐ…ÕÑ¼…±Á¡„€ôÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡É½ÍÍ™…‘•%¹‘•à€¬€Ä¤(€€€€€€€€€€€€€€€€€€€€€€€¼ÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡É½ÍÍ™…‘•M…µÁ±•Ì€¬€Ä¤ì(€€€½¹ÍÐ…ÕÑ¼ÕÍ•1¥¹•…É…‘”€ôÁ¡…Í•M½É”¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤€øô€ÜÔ¸Á˜ì(€€€½¹ÍÐ…ÕÑ¼Ñ…¥±…¥¸€ôÕÍ•1¥¹•…É…‘”€ü€Ä¸Á˜€´…±Á¡„(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€èÍÑèé½Ì¡…±Á¡„€¨©Õ”èé5…Ñ¡½¹ÍÑ…¹ÑÌñ™±½…Ðøèé¡…±™A¤¤ì(€€€½¹ÍÐ…ÕÑ¼¡•…‘…¥¸€ôÕÍ•1¥¹•…É…‘”€ü…±Á¡„(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€èÍÑèéÍ¥¸¡…±Á¡„€¨©Õ”èé5…Ñ¡½¹ÍÑ…¹ÑÌñ™±½…Ðøèé¡…±™A¤¤ì(€€€½¹ÍÐ…ÕÑ¼Ñ…¥°€ô±½½Á	Õ™™•È¹•ÑM…µÁ±”¡¡…¹¹•°°Á±…å‰…­A½Í¥Ñ¥½¸¤ì(€€€½¹ÍÐ…ÕÑ¼¡•…€ô±½½Á	Õ™™•È¹•ÑM…µÁ±”¡¡…¹¹•°°É½ÍÍ™…‘•%¹‘•à¤ì(€€€É•ÑÕÉ¸Ñ…¥±…¥¸€¨Ñ…¥°€¬¡•…‘…¥¸€¨¡•…ì)ô()Ù½¥1½½Á¹¥¹”èé…‘Ù…¹•A±…å‰…­A½Í¥Ñ¥½¸ ¤¹½•á•ÁÐ)ì(€€€½¹ÍÐ…ÕÑ¼±½½ÁM…µÁ±•Ì€ô…ÁÑÕÉ•‘M…µÁ±•½Õ¹Ð¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€€¬­Á±…å‰…­A½Í¥Ñ¥½¸ì(€€€¥˜€¡Á±…å‰…­A½Í¥Ñ¥½¸€ð±½½ÁM…µÁ±•Ì¤(€€€€€€€É•ÑÕÉ¸ì((€€€½¹ÍÐ…ÕÑ¼É½ÍÍ™…‘•M…µÁ±•Ì€ô©Õ”èé©±¥µ¥Ð À°±½½ÁM…µÁ±•Ì€¼€È°(€€€€€€€©Õ”èé©µ¥¸¡•™™•Ñ¥Ù•É½ÍÍ™…‘•M…µÁ±•Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤°(€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”(€€€€€€€€€€€€€€€€€€€€€€€¨É½ÍÍ™…‘•5¥±±¥Í•½¹‘Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤€¨€À¸ÀÀÄ¤¤¤ì(€€€Á±…å‰…­A½Í¥Ñ¥½¸€ôÉ½ÍÍ™…‘•M…µÁ±•Ìì)ô()™±½…Ð1½½Á¹¥¹”èéÉ•…‘M½ÕÉ•M…µÁ±”¡½¹ÍÐ¥¹Ð¡…¹¹•°¤½¹ÍÐ¹½•á•ÁÐ)ì(€€€É•ÑÕÉ¸ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È¹•ÑM…µÁ±”¡¡…¹¹•°°Í½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸¤ì)ô()Ù½¥1½½Á¹¥¹”èé…‘Ù…¹•M½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸ ¤¹½•á•ÁÐ)ì(€€€½¹ÍÐ…ÕÑ¼ÍÑ…ÉÐ€ô…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€½¹ÍÐ…ÕÑ¼•¹€ô…¹…±åÍ¥ÍI…¹•¹‘M…µÁ±”¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤ì(€€€€¬­Í½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸ì(€€€¥˜€¡Í½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸€øô•¹ñðÍ½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸€ðÍÑ…ÉÐ¤(€€€€€€€Í½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸€ôÍÑ…ÉÐì)ô()Ù½¥1½½Á¹¥¹”èéÍÑ…ÉÑ¹…±åÍ¥ÍQ¡É•… ¤)ì(€€€ÍÑ½ÁI•ÅÕ•ÍÑ•¹ÍÑ½É”¡™…±Í”¤ì(€€€…¹…±åÍ¥ÍA•¹‘¥¹œ¹ÍÑ½É”¡™…±Í”¤ì(€€€…¹…±åÍ¥ÍQ¡É•…€ôÍÑèéÑ¡É•…¡mÑ¡¥Ítì…¹…±åÍ¥Í1½½À ¤ìô¤ì)ô()Ù½¥1½½Á¹¥¹”èéÍÑ½Á¹…±åÍ¥ÍQ¡É•… ¤)ì(€€€ÍÑ½ÁI•ÅÕ•ÍÑ•¹ÍÑ½É”¡ÑÉÕ”°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€…¹…±åÍ¥Í½¹‘¥Ñ¥½¸¹¹½Ñ¥™å}…±° ¤ì(€€€¥˜€¡…¹…±åÍ¥ÍQ¡É•…¹©½¥¹…‰±” ¤¤(€€€€€€€…¹…±åÍ¥ÍQ¡É•…¹©½¥¸ ¤ì(€€€…¹…±åÍ¥ÍA•¹‘¥¹œ¹ÍÑ½É”¡™…±Í”¤ì)ô()Ù½¥1½½Á¹¥¹”èé…¹…±åÍ¥Í1½½À ¤)ì(€€€™½È€ ìì¤(€€€ì(€€€€€€€ÍÑèéÕ¹¥ÅÕ•}±½¬Ý…¥Ñ1½¬¡…¹…±åÍ¥Í]…¥Ñ5ÕÑ•à¤ì(€€€€€€€…¹…±åÍ¥Í½¹‘¥Ñ¥½¸¹Ý…¥Ð¡Ý…¥Ñ1½¬°mÑ¡¥Ít(€€€€€€€ì(€€€€€€€€€€€É•ÑÕÉ¸ÍÑ½ÁI•ÅÕ•ÍÑ•¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}…ÅÕ¥É”¤(€€€€€€€€€€€€€€€€€€ñð…¹…±åÍ¥ÍA•¹‘¥¹œ¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}…ÅÕ¥É”¤ì(€€€€€€€ô¤ì(€€€€€€€Ý…¥Ñ1½¬¹Õ¹±½¬ ¤ì((€€€€€€€¥˜€¡ÍÑ½ÁI•ÅÕ•ÍÑ•¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}…ÅÕ¥É”¤¤(€€€€€€€€€€€É•ÑÕÉ¸ì((€€€€€€€¥˜€ ……¹…±åÍ¥ÍA•¹‘¥¹œ¹•á¡…¹”¡™…±Í”°ÍÑèéµ•µ½Éå}½É‘•É}…Å}É•°¤¤(€€€€€€€€€€€½¹Ñ¥¹Õ”ì((€€€€€€€½¹ÍÐ…ÕÑ¼É•ÅÕ•ÍÑ•¹•É…Ñ¥½¸€ô•¹•É…Ñ¥½¸¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}…ÅÕ¥É”¤ì(€€€€€€€½¹ÍÐ…ÕÑ¼¥µÁ½ÉÑ•€ô¥µÁ½ÉÑ•‘¹…±åÍ¥ÍA•¹‘¥¹œ¹•á¡…¹”¡™…±Í”°ÍÑèéµ•µ½Éå}½É‘•É}…Å}É•°¤ì(€€€€€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø¥µÁ½ÉÑ•‘M½ÕÉ”ì(€€€€€€€©Õ”èéMÑÉ¥¹œ¥µÁ½ÉÑ•‘9…µ”ì(€€€€€€€¥¹Ð¥µÁ½ÉÑ•‘=™™Í•Ð€ô€Àì(€€€€€€€¥¹Ð¥µÁ½ÉÑ•‘Õ±±M½ÕÉ•M…µÁ±•Ì€ô€Àì(€€€€€€€‰½½°¥µÁ½ÉÑ•‘I•Á±…•ÍÕÉÉ•¹ÑM½ÕÉ”€ôÑÉÕ”ì(€€€€€€€¥¹Ð¥µÁ½ÉÑ•‘I•Á…¥É=Ù•É±…À€ô€Àì(€€€€€€€¥˜€¡¥µÁ½ÉÑ•¤(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡Í½ÕÉ•…Ñ…5ÕÑ•à¤ì(€€€€€€€€€€€¥µÁ½ÉÑ•‘M½ÕÉ”€ôÍÑèéµ½Ù”¡Á•¹‘¥¹M½ÕÉ•	Õ™™•È¤ì(€€€€€€€€€€€¥µÁ½ÉÑ•‘9…µ”€ôÍÑèéµ½Ù”¡Á•¹‘¥¹M½ÕÉ•9…µ”¤ì(€€€€€€€€€€€¥µÁ½ÉÑ•‘=™™Í•Ð€ôÁ•¹‘¥¹M½ÕÉ•=™™Í•Ðì(€€€€€€€€€€€¥µÁ½ÉÑ•‘Õ±±M½ÕÉ•M…µÁ±•Ì€ôÁ•¹‘¥¹Õ±±M½ÕÉ•M…µÁ±•Ìì(€€€€€€€€€€€¥µÁ½ÉÑ•‘I•Á±…•ÍÕÉÉ•¹ÑM½ÕÉ”€ôÁ•¹‘¥¹I•Á±…•ÍÕÉÉ•¹ÑM½ÕÉ”ì(€€€€€€€ô((€€€€€€€1½½Á¹…±åÍ¥ÍI•ÍÕ±ÐÉ•ÍÕ±Ðì(€€€€€€€1½½Á¹…±åÍ¥ÍI•Á½ÉÐ¥µÁ½ÉÑ•‘I•Á½ÉÐì(€€€€€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…Ðø…ÁÑÕÉ•‘M½ÕÉ•Y¥•Üì(€€€€€€€‰½½°É•ÍÕ±Ñ1½Ý½¹™¥‘•¹”€ô™…±Í”ì(€€€€€€€½¹ÍÐ…ÕÑ¼¨…¹…±åÍ¥ÍM½ÕÉ”€ô€™…ÁÑÕÉ•	Õ™™•Èì(€€€€€€€¥˜€¡¥µÁ½ÉÑ•¤(€€€€€€€ì(€€€€€€€€€€€…¹…±åÍ¥ÍM½ÕÉ”€ô€™¥µÁ½ÉÑ•‘M½ÕÉ”ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼µ¥¹¥µÕ´€ô©Õ”èé©µ¥¸¡¥µÁ½ÉÑ•‘M½ÕÉ”¹•Ñ9ÕµM…µÁ±•Ì ¤€¼€È°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨€À¸ÈÔ¤¤ì(€€€€€€€€€€€½¹ÍÐ…ÕÑ¼µ…á¥µÕ´€ô©Õ”èé©µ¥¸¡¥µÁ½ÉÑ•‘M½ÕÉ”¹•Ñ9ÕµM…µÁ±•Ì ¤€´€Ä°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨µ…á¥µÕµ1½½ÁM•½¹‘Ì¤¤ì(€€€€€€€€€€€¥µÁ½ÉÑ•‘I•Á…¥É=Ù•É±…À€ô©Õ”èé©±¥µ¥Ð À°µ¥¹¥µÕ´€¼€È°(€€€€€€€€€€€€€€€©Õ”èé©µ¥¸¡¥µÁ½ÉÑ•‘M½ÕÉ”¹•Ñ9ÕµM…µÁ±•Ì ¤€¼€Ð°(€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”(€€€€€€€€€€€€€€€€€€€€€€€€¨É½ÍÍ™…‘•5¥±±¥Í•½¹‘Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤€¨€À¸ÀÀÄ¤¤¤ì(€€€€€€€€€€€¥µÁ½ÉÑ•‘I•Á½ÉÐ€ô1½½Á¹…±åé•Èèé…¹…±åé•M½ÕÉ”¡¥µÁ½ÉÑ•‘M½ÕÉ”°Í…µÁ±•I…Ñ”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€µ¥¹¥µÕ´°µ…á¥µÕ´°€Ì°¥µÁ½ÉÑ•‘I•Á…¥É=Ù•É±…À¤ì(€€€€€€€€€€€¥˜€ …¥µÁ½ÉÑ•‘I•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹•µÁÑä ¤¤(€€€€€€€€€€€€€€€É•ÍÕ±Ð€ô¥µÁ½ÉÑ•‘I•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹™É½¹Ð ¤ì(€€€€€€€€€€€É•ÍÕ±Ñ1½Ý½¹™¥‘•¹”€ô¥µÁ½ÉÑ•‘I•Á½ÉÐ¹±½Ý½¹™¥‘•¹”ì(€€€€€€€ô(€€€€€€€•±Í”(€€€€€€€ì(€€€€€€€€€€€…ÁÑÕÉ•‘M½ÕÉ•Y¥•Ü¹Í•Ñ…Ñ…Q½I•™•ÉQ¼¡…ÁÑÕÉ•	Õ™™•È¹•ÑÉÉ…å=™]É¥Ñ•A½¥¹Ñ•ÉÌ ¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€…ÁÑÕÉ•	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€…ÁÑÕÉ•M…µÁ±•½Õ¹Ð¤ì(€€€€€€€€€€€…¹…±åÍ¥ÍM½ÕÉ”€ô€™…ÁÑÕÉ•‘M½ÕÉ•Y¥•Üì(€€€€€€€€€€€É•ÍÕ±Ð€ô1½½Á¹…±åé•Èèé™¥¹‘	•ÍÑ1½½À¡…ÁÑÕÉ•‘M½ÕÉ•Y¥•Ü°Í…µÁ±•I…Ñ”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É•ÅÕ•ÍÑ•‘1½½ÁM…µÁ±•Ì°Í•…É¡I…‘¥ÕÍM…µÁ±•Ì¤ì(€€€€€€€ô(€€€€€€€¥˜€¡É•ÅÕ•ÍÑ•¹•É…Ñ¥½¸€„ô•¹•É…Ñ¥½¸¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}…ÅÕ¥É”¤(€€€€€€€€€€€ñðÍÑ…Ñ”¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}…ÅÕ¥É”¤€„ôMÑ…Ñ”èé…¹…±åÍ¥¹œ¤(€€€€€€€€€€€½¹Ñ¥¹Õ”ì((€€€€€€€¥˜€¡¥µÁ½ÉÑ•€˜˜¥µÁ½ÉÑ•‘I•Á½ÉÐ¹…¹‘¥‘…Ñ•Ì¹•µÁÑä ¤¤(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡Í½ÕÉ•…Ñ…5ÕÑ•à¤ì(€€€€€€€€€€€¥˜€¡¥µÁ½ÉÑ•‘I•Á±…•ÍÕÉÉ•¹ÑM½ÕÉ”¤(€€€€€€€€€€€€€€€ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È€ôÍÑèéµ½Ù”¡¥µÁ½ÉÑ•‘M½ÕÉ”¤ì(€€€€€€€€€€€ÕÉÉ•¹ÑM½ÕÉ•9…µ”€ô¥µÁ½ÉÑ•‘9…µ”ì(€€€€€€€€€€€Í½ÕÉ•…¹‘¥‘…Ñ•Ì¹±•…È ¤ì(€€€€€€€€€€€…¹‘¥‘…Ñ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€€€€€¥˜€¡¥µÁ½ÉÑ•‘I•Á±…•ÍÕÉÉ•¹ÑM½ÕÉ”¤(€€€€€€€€€€€€€€€Í½ÕÉ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€€€€€…ÁÑÕÉ•‘M…µÁ±•½Õ¹Ð¹ÍÑ½É” À¤ì(€€€€€€€€€€€±½Ý½¹™¥‘•¹”¹ÍÑ½É”¡ÑÉÕ”¤ì(€€€€€€€€€€€ÍÑ…Ñ”¹ÍÑ½É”¡MÑ…Ñ”èé™…¥±•°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€€€€€½¹Ñ¥¹Õ”ì(€€€€€€€ô((€€€€€€€½¹ÍÐ…ÕÑ¼Í•±•Ñ•‘M…µÁ±•Ì€ô©Õ”èé©µ…à Ä°É•ÍÕ±Ð¹•¹‘M…µÁ±”€´É•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”¤ì(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡±½½Á…Ñ…5ÕÑ•à¤ì(€€€€€€€€€€€±½½Á	Õ™™•È¹Í•ÑM¥é”¡…¹…±åÍ¥ÍM½ÕÉ”´ù•Ñ9Õµ¡…¹¹•±Ì ¤°Í•±•Ñ•‘M…µÁ±•Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€™…±Í”°™…±Í”°™…±Í”¤ì(€€€€€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ð±½½Á	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤ì€¬­¡…¹¹•°¤(€€€€€€€€€€€€€€€±½½Á	Õ™™•È¹½ÁåÉ½´¡¡…¹¹•°°€À°€©…¹…±åÍ¥ÍM½ÕÉ”°¡…¹¹•°°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€É•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”°Í•±•Ñ•‘M…µÁ±•Ì¤ì(€€€€€€€ô((€€€€€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ•M…µÁ±•½Õ¹Ð€ô…¹…±åÍ¥ÍM½ÕÉ”´ù•Ñ9ÕµM…µÁ±•Ì ¤ì(€€€€€€€¥˜€¡¥µÁ½ÉÑ•¤(€€€€€€€ì(€€€€€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡Í½ÕÉ•…Ñ…5ÕÑ•à¤ì(€€€€€€€€€€€ÕÉÉ•¹ÑM½ÕÉ•9…µ”€ô¥µÁ½ÉÑ•‘9…µ”ì(€€€€€€€€€€€¥˜€¡¥µÁ½ÉÑ•‘I•Á±…•ÍÕÉÉ•¹ÑM½ÕÉ”¤(€€€€€€€€€€€€€€€ÕÉÉ•¹ÑM½ÕÉ•	Õ™™•È€ôÍÑèéµ½Ù”¡¥µÁ½ÉÑ•‘M½ÕÉ”¤ì(€€€€€€€€€€€Í½ÕÉ•…¹‘¥‘…Ñ•Ì€ô¥µÁ½ÉÑ•‘I•Á½ÉÐ¹…¹‘¥‘…Ñ•Ìì(€€€€€€€€€€€™½È€¡…ÕÑ¼˜…¹‘¥‘…Ñ”€èÍ½ÕÉ•…¹‘¥‘…Ñ•Ì¤(€€€€€€€€€€€ì(€€€€€€€€€€€€€€€…¹‘¥‘…Ñ”¹ÍÑ…ÉÑM…µÁ±”€¬ô¥µÁ½ÉÑ•‘=™™Í•Ðì(€€€€€€€€€€€€€€€…¹‘¥‘…Ñ”¹•¹‘M…µÁ±”€¬ô¥µÁ½ÉÑ•‘=™™Í•Ðì(€€€€€€€€€€€ô(€€€€€€€€€€€…¹‘¥‘…Ñ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€€€€€¥˜€¡¥µÁ½ÉÑ•‘I•Á±…•ÍÕÉÉ•¹ÑM½ÕÉ”¤(€€€€€€€€€€€€€€€Í½ÕÉ•I•Ù¥Í¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€€€€€ô((€€€€€€€Á±…å‰…­A½Í¥Ñ¥½¸€ô€Àì(€€€€€€€…ÁÑÕÉ•‘M…µÁ±•½Õ¹Ð¹ÍÑ½É”¡Í•±•Ñ•‘M…µÁ±•Ì¤ì(€€€€€€€•™™•Ñ¥Ù•É½ÍÍ™…‘•M…µÁ±•Ì¹ÍÑ½É”¡¥µÁ½ÉÑ•€ü¥µÁ½ÉÑ•‘I•Á…¥É=Ù•É±…À(€€€€€€€€€€€€è©Õ”èé©±¥µ¥Ð À°Í•±•Ñ•‘M…µÁ±•Ì€¼€Ì°(€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”(€€€€€€€€€€€€€€€€€€€€¨É½ÍÍ™…‘•5¥±±¥Í•½¹‘Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤€¨€À¸ÀÀÄ¤¤¤ì(€€€€€€€Ý…Ù•™½ÉµM½É”¹ÍÑ½É”¡É•ÍÕ±Ð¹Ý…Ù•™½É´¤ì(€€€€€€€±•Ù•±M½É”¹ÍÑ½É”¡É•ÍÕ±Ð¹±•Ù•°¤ì(€€€€€€€Í±½Á•M½É”¹ÍÑ½É”¡É•ÍÕ±Ð¹Í±½Á”¤ì(€€€€€€€ÍÁ•ÑÉÕµM½É”¹ÍÑ½É”¡É•ÍÕ±Ð¹ÍÁ•ÑÉÕ´¤ì(€€€€€€€Á¡…Í•M½É”¹ÍÑ½É”¡É•ÍÕ±Ð¹Á¡…Í”¤ì(€€€€€€€ÍÑ•É•½M½É”¹ÍÑ½É”¡É•ÍÕ±Ð¹ÍÑ•É•¼¤ì(€€€€€€€ÑÉ…¹Í¥•¹ÑM½É”¹ÍÑ½É”¡É•ÍÕ±Ð¹ÑÉ…¹Í¥•¹Ð¤ì(€€€€€€€Á•É¥½‘¥¥ÑåM½É”¹ÍÑ½É”¡É•ÍÕ±Ð¹Á•É¥½‘¥¥Ñä¤ì(€€€€€€€±½Ý½¹™¥‘•¹”¹ÍÑ½É”¡É•ÍÕ±Ñ1½Ý½¹™¥‘•¹”¤ì(€€€€€€€Í•…µEÕ…±¥Ñä¹ÍÑ½É”¡É•ÍÕ±Ð¹½Ù•É…±°¤ì(€€€€€€€Í•±•Ñ•‘MÑ…ÉÑM…µÁ±”¹ÍÑ½É”¡É•ÍÕ±Ð¹ÍÑ…ÉÑM…µÁ±”€¬€¡¥µÁ½ÉÑ•€ü¥µÁ½ÉÑ•‘=™™Í•Ð€è€À¤¤ì(€€€€€€€Í•±•Ñ•‘¹‘M…µÁ±”¹ÍÑ½É”¡É•ÍÕ±Ð¹•¹‘M…µÁ±”€´€¡¥µÁ½ÉÑ•€ü¥µÁ½ÉÑ•‘I•Á…¥É=Ù•É±…À€è€À¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¬€¡¥µÁ½ÉÑ•€ü¥µÁ½ÉÑ•‘=™™Í•Ð€è€À¤¤ì(€€€€€€€Í•±•Ñ•‘M½ÕÉ•M…µÁ±•Ì¹ÍÑ½É”¡¥µÁ½ÉÑ•€ü¥µÁ½ÉÑ•‘Õ±±M½ÕÉ•M…µÁ±•Ì€èÍ½ÕÉ•M…µÁ±•½Õ¹Ð¤ì(€€€€€€€¥˜€¡¥µÁ½ÉÑ•¤(€€€€€€€ì(€€€€€€€€€€€…¹…±åÍ¥ÍI…¹•MÑ…ÉÑM…µÁ±”¹ÍÑ½É”¡¥µÁ½ÉÑ•‘=™™Í•Ð¤ì(€€€€€€€€€€€…¹…±åÍ¥ÍI…¹•¹‘M…µÁ±”¹ÍÑ½É”¡¥µÁ½ÉÑ•‘=™™Í•Ð€¬Í½ÕÉ•M…µÁ±•½Õ¹Ð¤ì(€€€€€€€€€€€Í½ÕÉ•A±…å‰…­A½Í¥Ñ¥½¸€ô¥µÁ½ÉÑ•‘=™™Í•Ðì(€€€€€€€ô(€€€€€€€ÍÑ…Ñ”¹ÍÑ½É”¡MÑ…Ñ”èéÉ•…‘ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€ô)ô()Ù½¥1½½Á¹¥¹”èéÉ•Í•ÑM½É•Ì ¤¹½•á•ÁÐ)ì(€€€Í•…µEÕ…±¥Ñä¹ÍÑ½É” À¸Á˜¤ì(€€€Ý…Ù•™½ÉµM½É”¹ÍÑ½É” À¸Á˜¤ì(€€€±•Ù•±M½É”¹ÍÑ½É” À¸Á˜¤ì(€€€Í±½Á•M½É”¹ÍÑ½É” À¸Á˜¤ì(€€€ÍÁ•ÑÉÕµM½É”¹ÍÑ½É” À¸Á˜¤ì(€€€Á¡…Í•M½É”¹ÍÑ½É” À¸Á˜¤ì(€€€ÍÑ•É•½M½É”¹ÍÑ½É” À¸Á˜¤ì(€€€ÑÉ…¹Í¥•¹ÑM½É”¹ÍÑ½É” À¸Á˜¤ì(€€€Á•É¥½‘¥¥ÑåM½É”¹ÍÑ½É” À¸Á˜¤ì(€€€±½Ý½¹™¥‘•¹”¹ÍÑ½É”¡™…±Í”¤ì)ô()©Õ”èé5•µ½Éå	±½¬1½½Á¹¥¹”èéÉ•…Ñ•1½½ÁMÑ…Ñ” ¤½¹ÍÐ)ì(€€€©Õ”èé5•µ½Éå	±½¬É•ÍÕ±Ðì(€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡±½½Á…Ñ…5ÕÑ•à¤ì(€€€¥˜€¡±½½Á	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤€ôô€Àñð±½½Á	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤€ôô€À¤(€€€€€€€É•ÑÕÉ¸É•ÍÕ±Ðì((€€€©Õ”èé5•µ½Éå=ÕÑÁÕÑMÑÉ•…´‘•ÍÑ¥¹…Ñ¥½¸¡É•ÍÕ±Ð°™…±Í”¤ì(€€€ì(€€€€€€€©Õ”èéi%A½µÁÉ•ÍÍ½É=ÕÑÁÕÑMÑÉ•…´½µÁÉ•ÍÍ• ™‘•ÍÑ¥¹…Ñ¥½¸°€Ø°™…±Í”¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡ÍÑ…Ñ•5…¥Œ¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡ÍÑ…Ñ•Y•ÉÍ¥½¸¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•½Õ‰±”¡Í…µÁ±•I…Ñ”¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡±½½Á	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•%¹Ð¡±½½Á	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•±½…Ð¡Í•…µEÕ…±¥Ñä¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•±½…Ð¡±•Ù•±M½É”¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•±½…Ð¡Í±½Á•M½É”¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•±½…Ð¡ÍÁ•ÑÉÕµM½É”¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•±½…Ð¡Á¡…Í•M½É”¹±½… ¤¤ì(€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ•±½…Ð¡ÍÑ•É•½M½É”¹±½… ¤¤ì(€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ð±½½Á	Õ™™•È¹•Ñ9Õµ¡…¹¹•±Ì ¤ì€¬­¡…¹¹•°¤(€€€€€€€€€€€½µÁÉ•ÍÍ•¹ÝÉ¥Ñ”¡±½½Á	Õ™™•È¹•ÑI•…‘A½¥¹Ñ•È¡¡…¹¹•°¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€ÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡±½½Á	Õ™™•È¹•Ñ9ÕµM…µÁ±•Ì ¤¤€¨Í¥é•½˜¡™±½…Ð¤¤ì(€€€ô(€€€É•ÑÕÉ¸É•ÍÕ±Ðì)ô()‰½½°1½½Á¹¥¹”èéÉ•ÍÑ½É•1½½ÁMÑ…Ñ”¡½¹ÍÐÙ½¥¨‘…Ñ„°½¹ÍÐÍ¥é•}ÐÍ¥é”¤)ì(€€€¥˜€¡‘…Ñ„€ôô¹Õ±±ÁÑÈñðÍ¥é”€ôô€À¤(€€€€€€€É•ÑÕÉ¸™…±Í”ì((€€€©Õ”èé5•µ½Éå%¹ÁÕÑMÑÉ•…´Í½ÕÉ”¡‘…Ñ„°Í¥é”°™…±Í”¤ì(€€€©Õ”èéi%A•½µÁÉ•ÍÍ½É%¹ÁÕÑMÑÉ•…´‘•½µÁÉ•ÍÍ• ™Í½ÕÉ”°™…±Í”¤ì(€€€¥˜€¡‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤€„ôÍÑ…Ñ•5…¥Œñð‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤€„ôÍÑ…Ñ•Y•ÉÍ¥½¸¤(€€€€€€€É•ÑÕÉ¸™…±Í”ì((€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘M…µÁ±•I…Ñ”€ô‘•½µÁÉ•ÍÍ•¹É•…‘½Õ‰±” ¤ì(€€€½¹ÍÐ…ÕÑ¼¡…¹¹•±Ì€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€½¹ÍÐ…ÕÑ¼Í…µÁ±•Ì€ô‘•½µÁÉ•ÍÍ•¹É•…‘%¹Ð ¤ì(€€€¥˜€¡Í…Ù•‘M…µÁ±•I…Ñ”€ðô€À¸Àñð¡…¹¹•±Ì€ð€Äñð¡…¹¹•±Ì€ø€ÈñðÍ…µÁ±•Ì€ð€Ä(€€€€€€€ñðÍ…µÁ±•Ì€ø©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…Ù•‘M…µÁ±•I…Ñ”€¨µ…á¥µÕµ1½½ÁM•½¹‘Ì¤¤(€€€€€€€É•ÑÕÉ¸™…±Í”ì((€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘=Ù•É…±°€ô‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤ì(€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘1•Ù•°€ô‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤ì(€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘M±½Á”€ô‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤ì(€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘MÁ•ÑÉÕ´€ô‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤ì(€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘A¡…Í”€ô‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤ì(€€€½¹ÍÐ…ÕÑ¼Í…Ù•‘MÑ•É•¼€ô‘•½µÁÉ•ÍÍ•¹É•…‘±½…Ð ¤ì(€€€©Õ”èéÕ‘¥½	Õ™™•Èñ™±½…ÐøÉ•ÍÑ½É•¡¡…¹¹•±Ì°Í…µÁ±•Ì¤ì(€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ð¡…¹¹•±Ìì€¬­¡…¹¹•°¤(€€€ì(€€€€€€€½¹ÍÐ…ÕÑ¼‰åÑ•Ì€ôÍÑ…Ñ¥}…ÍÐñ¥¹Ðø¡ÍÑ…Ñ¥}…ÍÐñÍ¥é•}Ðø¡Í…µÁ±•Ì¤€¨Í¥é•½˜¡™±½…Ð¤¤ì(€€€€€€€¥˜€¡‘•½µÁÉ•ÍÍ•¹É•…¡É•ÍÑ½É•¹•Ñ]É¥Ñ•A½¥¹Ñ•È¡¡…¹¹•°¤°‰åÑ•Ì¤€„ô‰åÑ•Ì¤(€€€€€€€€€€€É•ÑÕÉ¸™…±Í”ì(€€€ô((€€€•¹•É…Ñ¥½¸¹™•Ñ¡}…‘ Ä°ÍÑèéµ•µ½Éå}½É‘•É}…Å}É•°¤ì(€€€ÍÑ…Ñ”¹ÍÑ½É”¡MÑ…Ñ”èé…¹…±åÍ¥¹œ°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€Ý¡¥±”€¡…Ñ¥Ù•Õ‘¥½I•…‘•ÉÌ¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}…ÅÕ¥É”¤€„ô€À¤(€€€€€€€ÍÑèéÑ¡¥Í}Ñ¡É•…èéå¥•± ¤ì(€€€½¹ÍÐ…ÕÑ¼Ñ…É•ÑM…µÁ±•Ì€ô©Õ”èé©±¥µ¥Ð Ä°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”€¨µ…á¥µÕµ1½½ÁM•½¹‘Ì¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡ÍÑ…Ñ¥}…ÍÐñ‘½Õ‰±”ø¡Í…µÁ±•Ì¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¨Í…µÁ±•I…Ñ”€¼Í…Ù•‘M…µÁ±•I…Ñ”¤¤ì(€€€ì(€€€€€€€½¹ÍÐÍÑèéÍ½Á•‘}±½¬±½¬¡±½½Á…Ñ…5ÕÑ•à¤ì(€€€€€€€±½½Á	Õ™™•È¹Í•ÑM¥é”¡¡…¹¹•±Ì°Ñ…É•ÑM…µÁ±•Ì°™…±Í”°™…±Í”°™…±Í”¤ì(€€€€€€€™½È€¡¥¹Ð¡…¹¹•°€ô€Àì¡…¹¹•°€ð¡…¹¹•±Ìì€¬­¡…¹¹•°¤(€€€€€€€ì(€€€€€€€€€€€™½È€¡¥¹Ð¥¹‘•à€ô€Àì¥¹‘•à€ðÑ…É•ÑM…µÁ±•Ìì€¬­¥¹‘•à¤(€€€€€€€€€€€ì(€€€€€€€€€€€€€€€½¹ÍÐ…ÕÑ¼Í½ÕÉ•A½Í¥Ñ¥½¸€ôÍÑ…Ñ¥}…ÍÐñ‘½Õ‰±”ø¡¥¹‘•à¤€¨Í…Ù•‘M…µÁ±•I…Ñ”€¼Í…µÁ±•I…Ñ”ì(€€€€€€€€€€€€€€€½¹ÍÐ…ÕÑ¼±½Ý•È€ô©Õ”èé©±¥µ¥Ð À°Í…µÁ±•Ì€´€Ä°ÍÑ…Ñ¥}…ÍÐñ¥¹Ðø¡Í½ÕÉ•A½Í¥Ñ¥½¸¤¤ì(€€€€€€€€€€€€€€€½¹ÍÐ…ÕÑ¼ÕÁÁ•È€ô©Õ”èé©µ¥¸¡Í…µÁ±•Ì€´€Ä°±½Ý•È€¬€Ä¤ì(€€€€€€€€€€€€€€€½¹ÍÐ…ÕÑ¼™É…Ñ¥½¸€ôÍÑ…Ñ¥}…ÍÐñ™±½…Ðø¡Í½ÕÉ•A½Í¥Ñ¥½¸€´ÍÑ…Ñ¥}…ÍÐñ‘½Õ‰±”ø¡±½Ý•È¤¤ì(€€€€€€€€€€€€€€€½¹ÍÐ…ÕÑ¼Ù…±Õ”€ôÉ•ÍÑ½É•¹•ÑM…µÁ±”¡¡…¹¹•°°±½Ý•È¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¬™É…Ñ¥½¸€¨€¡É•ÍÑ½É•¹•ÑM…µÁ±”¡¡…¹¹•°°ÕÁÁ•È¤(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€´É•ÍÑ½É•¹•ÑM…µÁ±”¡¡…¹¹•°°±½Ý•È¤¤ì(€€€€€€€€€€€€€€€±½½Á	Õ™™•È¹Í•ÑM…µÁ±”¡¡…¹¹•°°¥¹‘•à°Ù…±Õ”¤ì(€€€€€€€€€€€ô(€€€€€€€ô(€€€ô((€€€Á±…å‰…­A½Í¥Ñ¥½¸€ô€Àì(€€€…ÁÑÕÉ•‘M…µÁ±•½Õ¹Ð¹ÍÑ½É”¡Ñ…É•ÑM…µÁ±•Ì¤ì(€€€•™™•Ñ¥Ù•É½ÍÍ™…‘•M…µÁ±•Ì¹ÍÑ½É”¡©Õ”èé©±¥µ¥Ð À°Ñ…É•ÑM…µÁ±•Ì€¼€Ì°(€€€€€€€©Õ”èéÉ½Õ¹‘Q½%¹Ð¡Í…µÁ±•I…Ñ”(€€€€€€€€€€€€¨É½ÍÍ™…‘•5¥±±¥Í•½¹‘Ì¹±½…¡ÍÑèéµ•µ½Éå}½É‘•É}É•±…á•¤€¨€À¸ÀÀÄ¤¤¤ì(€€€Í•…µEÕ…±¥Ñä¹ÍÑ½É”¡Í…Ù•‘=Ù•É…±°¤ì(€€€±•Ù•±M½É”¹ÍÑ½É”¡Í…Ù•‘1•Ù•°¤ì(€€€Í±½Á•M½É”¹ÍÑ½É”¡Í…Ù•‘M±½Á”¤ì(€€€ÍÁ•ÑÉÕµM½É”¹ÍÑ½É”¡Í…Ù•‘MÁ•ÑÉÕ´¤ì(€€€Á¡…Í•M½É”¹ÍÑ½É”¡Í…Ù•‘A¡…Í”¤ì(€€€ÍÑ•É•½M½É”¹ÍÑ½É”¡Í…Ù•‘MÑ•É•¼¤ì(€€€…ÁÑÕÉ•AÉ½É•ÍÌ¹ÍÑ½É” Ä¸Á˜¤ì(€€€ÍÑ…Ñ”¹ÍÑ½É”¡MÑ…Ñ”èéÉ•…‘ä°ÍÑèéµ•µ½Éå}½É‘•É}É•±•…Í”¤ì(€€€É•ÑÕÉ¸ÑÉÕ”ì)ô