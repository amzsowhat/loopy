­r‡^Ñf¥–Ø¦{}lyÊ'vÃ®¶›­#include "LoopEngine.h"

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
        activeAud×n;òÚ$z{-®éÜj×‡&W7VÇBç&÷FF–öå6×ÆRÂ ¢ò&W7VÇBç&W—$÷fW&Æ6×ÆW2¢¢²–×÷'FVDöfg6WB“°¢6VÆV7FVE&÷FF–öå6×ÆRç7F÷&R‡&W7VÇBç&÷FF–öå6×ÆRãÒ ¢ò&W7VÇBç&÷FF–öå6×ÆR²–×÷'FVDöfg6WB¢Ó“°¢6VÆV7FVE6÷W&6U6×ÆW2ç7F÷&R†–×÷'FVDgVÆÅ6÷W&6U6×ÆW2“°¢æÇ—6—5&ævU7F'E6×ÆRç7F÷&R†–×÷'FVDöfg6WB“°¢æÇ—6—5&ævTVæE6×ÆRç7F÷&R†–×÷'FVDöfg6WB²6÷W&6U6×ÆT6÷VçB“°¢6÷W&6UÆ–&6µ÷6—F–öâÒ–×÷'FVDöfg6WC°¢7FFRç7F÷&R…7FFS£§&VG’Â7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆV6R“°¢æÇ—6—5&öw&W72ç7F÷&RƒãbÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆ†VB“°¢Ğ§Ğ §fö–BÆö÷Væv–æS£§&W6WE66÷&W2‚’æöW†6W@§°¢6VÕVÆ—G’ç7F÷&Rƒãb“°¢vfVf÷&Õ66÷&Rç7F÷&Rƒãb“°¢ÆWfVÅ66÷&Rç7F÷&Rƒãb“°¢6Æ÷U66÷&Rç7F÷&Rƒãb“°¢7V7G'VÕ66÷&Rç7F÷&Rƒãb“°¢†6U66÷&Rç7F÷&Rƒãb“°¢7FW&Võ66÷&Rç7F÷&Rƒãb“°¢G&ç6–VçE66÷&Rç7F÷&Rƒãb“°¢W&–öF–6—G•66÷&Rç7F÷&Rƒãb“°¢&W—%66÷&Rç7F÷&Rƒãb“°¢&WVE6fWG•66÷&Rç7F÷&Rƒãb“°¢G'VUV´F'Gç7F÷&R‚Óãb“°¢&VæFW%VÆ—G•66÷&Rç7F÷&Rƒãb“°¢VÆ—G”vFU76VBç7F÷&R†fÇ6R“°¢Æ÷t6öæf–FVæ6Rç7F÷&R†fÇ6R“°§Ğ ¦§V6S£¤ÖVÖ÷'”&Æö6²Æö÷Væv–æS£¦7&VFTÆö÷7FFR‚’6öç7@§°¢§V6S£¤ÖVÖ÷'”&Æö6²&W7VÇC°¢6öç7B7FC£§66÷VEöÆö6²Æö6²‡6÷W&6TFF×WFW‚ÂÆö÷FF×WFW‚“°¢–b†Æö÷'VffW"ævWDçVÔ6†ææVÇ2‚’ÓÒÇÂÆö÷'VffW"ævWDçVÕ6×ÆW2‚’ÓÒ¢&WGW&â&W7VÇC° ¢§V6S£¤ÖVÖ÷'”÷WGWE7G&VÒFW7F–æF–öâ‡&W7VÇBÂfÇ6R“°¢°¢§V6S£¤u¤•6ö×&W76÷$÷WGWE7G&VÒ6ö×&W76VB‚fFW7F–æF–öâÂbÂfÇ6R“°¢6ö×&W76VBçw&—FT–çB‡7FFTÖv–2“°¢6ö×&W76VBçw&—FT–çB‡7FFUfW'6–öâ“°¢6ö×&W76VBçw&—FTF÷V&ÆR‡6×ÆU&FR“°¢6ö×&W76VBçw&—FT–çB‡7FF–5ö67CÆ–çCâ†Æ7EW6VDvVæW&F–öäÖöFRæÆöB‚’’“°¢6ö×&W76VBçw&—FT–çB†Æö÷'VffW"ævWDçVÔ6†ææVÇ2‚’“°¢6ö×&W76VBçw&—FT–çB†Æö÷'VffW"ævWDçVÕ6×ÆW2‚’“°¢6ö×&W76VBçw&—FTfÆöB‡6VÕVÆ—G’æÆöB‚’“°¢6ö×&W76VBçw&—FTfÆöB†ÆWfVÅ66÷&RæÆöB‚’“°¢6ö×&W76VBçw&—FTfÆöB‡6Æ÷U66÷&RæÆöB‚’“°¢6ö×&W76VBçw&—FTfÆöB‡7V7G'VÕ66÷&RæÆöB‚’“°¢6ö×&W76VBçw&—FTfÆöB‡†6U66÷&RæÆöB‚’“°¢6ö×&W76VBçw&—FTfÆöB‡7FW&Võ66÷&RæÆöB‚’“°¢6ö×&W76VBçw&—FTfÆöB‡&WVE6fWG•66÷&RæÆöB‚’“°¢6ö×&W76VBçw&—FTfÆöB‡G'VUV´F'GæÆöB‚’“°¢6ö×&W76VBçw&—FTfÆöB‡&VæFW%VÆ—G•66÷&RæÆöB‚’“°¢6ö×&W76VBçw&—FT–çB‡VÆ—G”vFU76VBæÆöB‚’ò¢“°¢f÷"†–çB6†ææVÂÒ²6†ææVÂÂÆö÷'VffW"ævWDçVÔ6†ææVÇ2‚“²²¶6†ææVÂ¢6ö×&W76VBçw&—FR†Æö÷'VffW"ævWE&VEö–çFW"†6†ææVÂ’À¢7FF–5ö67CÇ6—¦U÷Câ†Æö÷'VffW"ævWDçVÕ6×ÆW2‚’’¢6—¦Vöb†fÆöB’“°¢6ö×&W76VBçw&—FU7G&–ær†7W'&VçE6÷W&6TæÖR“°¢6ö×&W76VBçw&—FT–çB†7W'&VçE6÷W&6T'VffW"ævWDçVÔ6†ææVÇ2‚’“°¢6ö×&W76VBçw&—FT–çB†7W'&VçE6÷W&6T'VffW"ævWDçVÕ6×ÆW2‚’“°¢6ö×&W76VBçw&—FT–çB‡6VÆV7FVE7F'E6×ÆRæÆöB‚’“°¢6ö×&W76VBçw&—FT–çB‡6VÆV7FVDVæE6×ÆRæÆöB‚’“°¢6ö×&W76VBçw&—FT–çB‡6VÆV7FVE&÷FF–öå6×ÆRæÆöB‚’“°¢6ö×&W76VBçw&—FT–çB‡6VÆV7FVE6÷W&6U6×ÆW2æÆöB‚’“°¢6ö×&W76VBçw&—FT–çB†æÇ—6—5&ævU7F'E6×ÆRæÆöB‚’“°¢6ö×&W76VBçw&—FT–çB†æÇ—6—5&ævTVæE6×ÆRæÆöB‚’“°¢f÷"†–çB6†ææVÂÒ²6†ææVÂÂ7W'&VçE6÷W&6T'VffW"ævWDçVÔ6†ææVÇ2‚“²²¶6†ææVÂ¢6ö×&W76VBçw&—FR†7W'&VçE6÷W&6T'VffW"ævWE&VEö–çFW"†6†ææVÂ’À¢7FF–5ö67CÇ6—¦U÷Câ†7W'&VçE6÷W&6T'VffW"ævWDçVÕ6×ÆW2‚’¢¢6—¦Vöb†fÆöB’“°¢Ğ¢&WGW&â&W7VÇC°§Ğ ¦&ööÂÆö÷Væv–æS£§&W7F÷&TÆö÷7FFR†6öç7Bfö–B¢FFÂ6öç7B6—¦U÷B6—¦R§°¢–b†FFÓÒçVÆÇG"ÇÂ6—¦RÓÒ¢&WGW&âfÇ6S° ¢§V6S£¤ÖVÖ÷'”–çWE7G&VÒ6÷W&6R†FFÂ6—¦RÂfÇ6R“°¢§V6S£¤u¤•FV6ö×&W76÷$–çWE7G&VÒFV6ö×&W76VB‚g6÷W&6RÂfÇ6R“°¢–b†FV6ö×&W76VBç&VD–çB‚’Ò7FFTÖv–2¢&WGW&âfÇ6S° ¢6öç7BWFò6fVEfW'6–öâÒFV6ö×&W76VBç&VD–çB‚“°¢–b‡6fVEfW'6–öâÂÇÂ6fVEfW'6–öââ7FFUfW'6–öâ¢&WGW&âfÇ6S°¢6öç7BWFò6fVE6×ÆU&FRÒFV6ö×&W76VBç&VDF÷V&ÆR‚“°¢WFò6fVDÖöFRÒvVæW&F–öäÖöFS£§&÷FFU&W—#°¢–b‡6fVEfW'6–öâãÒ"¢°¢6öç7BWFò7F÷&VDÖöFRÒFV6ö×&W76VBç&VD–çB‚“°¢6fVDÖöFRÒ6fVEfW'6–öâÃÒ0¢ò‡7F÷&VDÖöFRÓÒòvVæW&F–öäÖöFS£§FW‡GW&TÆö÷ ¢¢vVæW&F–öäÖöFS£§&÷FFU&W—"¢¢‡7F÷&VDÖöFRÓÒ7FF–5ö67CÆ–çCâ„vVæW&F–öäÖöFS£§FW‡GW&TÆö÷¢òvVæW&F–öäÖöFS£§FW‡GW&TÆö÷¢vVæW&F–öäÖöFS£§&÷FFU&W—"“°¢Ğ¢6öç7BWFò6†ææVÇ2ÒFV6ö×&W76VBç&VD–çB‚“°¢6öç7BWFò6×ÆW2ÒFV6ö×&W76VBç&VD–çB‚“°¢–b‡6fVE6×ÆU&FRÃÒãÇÂ6†ææVÇ2ÂÇÂ6†ææVÇ2â"ÇÂ6×ÆW2Â¢ÇÂ6×ÆW2â§V6S£§&÷VæEFô–çB‡6fVE6×ÆU&FR¢Ö†–×VÕFW‡GW&U6V6öæG2’¢&WGW&âfÇ6S° ¢6öç7BWFò6fVD÷fW&ÆÂÒFV6ö×&W76VBç&VDfÆöB‚“°¢6öç7BWFò6fVDÆWfVÂÒFV6ö×&W76VBç&VDfÆöB‚“°¢6öç7BWFò6fVE6Æ÷RÒFV6ö×&W76VBç&VDfÆöB‚“°¢6öç7BWFò6fVE7V7G'VÒÒFV6ö×&W76VBç&VDfÆöB‚“°¢6öç7BWFò6fVE†6RÒFV6ö×&W76VBç&VDfÆöB‚“°¢6öç7BWFò6fVE7FW&VòÒFV6ö×&W76VBç&VDfÆöB‚“°¢6öç7BWFò6fVE&WVE6fWG’Ò6fVEfW'6–öâãÒ2òFV6ö×&W76VBç&VDfÆöB‚’¢ãc°¢6öç7BWFò6fVEG'VUV²Ò6fVEfW'6–öâãÒ2òFV6ö×&W76VBç&VDfÆöB‚’¢Óãc°¢6öç7BWFò6fVEVÆ—G’Ò6fVEfW'6–öâãÒ2òFV6ö×&W76VBç&VDfÆöB‚’¢6fVD÷fW&ÆÃ°¢6öç7BWFò6fVDvFU76VBÒ6fVEfW'6–öâãÒ0¢òFV6ö×&W76VBç&VD–çB‚’Ò¢6fVD÷fW&ÆÂãÒc‚ãc°¢§V6S£¤VF–ô'VffW#ÆfÆöCâ&W7F÷&VB†6†ææVÇ2Â6×ÆW2“°¢f÷"†–çB6†ææVÂÒ²6†ææVÂÂ6†ææVÇ3²²¶6†ææVÂ¢°¢6öç7BWFò'—FW2Ò7FF–5ö67CÆ–çCâ‡7FF–5ö67CÇ6—¦U÷Câ‡6×ÆW2’¢6—¦Vöb†fÆöB’“°¢–b†FV6ö×&W76VBç&VB‡&W7F÷&VBævWEw&—FUö–çFW"†6†ææVÂ’Â'—FW2’Ò'—FW2¢&WGW&âfÇ6S°¢Ğ ¢§V6S£¥7G&–ær&W7F÷&VE6÷W&6TæÖS°¢§V6S£¤VF–ô'VffW#ÆfÆöCâ&W7F÷&VE6÷W&6S°¢WFò6fVE6VÆV7FVE7F'BÒ°¢WFò6fVE6VÆV7FVDVæBÒ°¢WFò6fVE&÷FF–öâÒÓ°¢WFò6fVE6VÆV7FVE6÷W&6U6×ÆW2Ò°¢WFò6fVE&ævU7F'BÒ°¢WFò6fVE&ævTVæBÒ°¢–b‡6fVEfW'6–öâãÒR¢°¢&W7F÷&VE6÷W&6TæÖRÒFV6ö×&W76VBç&VE7G&–ær‚“°¢6öç7BWFò6÷W&6T6†ææVÇ2ÒFV6ö×&W76VBç&VD–çB‚“°¢6öç7BWFò6÷W&6U6×ÆW2ÒFV6ö×&W76VBç&VD–çB‚“°¢6fVE6VÆV7FVE7F'BÒFV6ö×&W76VBç&VD–çB‚“°¢6fVE6VÆV7FVDVæBÒFV6ö×&W76VBç&VD–çB‚“°¢6fVE&÷FF–öâÒFV6ö×&W76VBç&VD–çB‚“°¢6fVE6VÆV7FVE6÷W&6U6×ÆW2ÒFV6ö×&W76VBç&VD–çB‚“°¢6fVE&ævU7F'BÒFV6ö×&W76VBç&VD–çB‚“°¢6fVE&ævTVæBÒFV6ö×&W76VBç&VD–çB‚“°¢–b‡6÷W&6T6†ææVÇ2ÂÇÂ6÷W&6T6†ææVÇ2â"ÇÂ6÷W&6U6×ÆW2Â ¢ÇÂ6÷W&6U6×ÆW2â§V6S£§&÷VæEFô–çB‡6fVE6×ÆU&FR¢Ö†–×VÕFW‡GW&U6V6öæG2¢ÇÂ‡6÷W&6U6×ÆW2âbb6÷W&6T6†ææVÇ2ÓÒ’¢&WGW&âfÇ6S°¢&W7F÷&VE6÷W&6Rç6WE6—¦R‡6÷W&6T6†ææVÇ2Â6÷W&6U6×ÆW2ÂfÇ6RÂfÇ6RÂfÇ6R“°¢f÷"†–çB6†ææVÂÒ²6†ææVÂÂ6÷W&6T6†ææVÇ3²²¶6†ææVÂ¢°¢6öç7BWFò'—FW2Ò7FF–5ö67CÆ–çCâ€¢7FF–5ö67CÇ6—¦U÷Câ‡6÷W&6U6×ÆW2’¢6—¦Vöb†fÆöB’“°¢–b†FV6ö×&W76VBç&VB‡&W7F÷&VE6÷W&6RævWEw&—FUö–çFW"†6†ææVÂ’Â'—FW2’Ò'—FW2¢&WGW&âfÇ6S°¢Ğ¢Ğ ¢vVæW&F–öâæfWF6…öFBƒÂ7FC£¦ÖVÖ÷'•ö÷&FW%ö7÷&VÂ“°¢7FFRç7F÷&R…7FFS£¦æÇ—6–ærÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆV6R“°¢v†–ÆR†7F—fTVF–õ&VFW'2æÆöB‡7FC£¦ÖVÖ÷'•ö÷&FW%ö7V—&R’Ò¢7FC£§F†—5÷F‡&VC£§––VÆB‚“°¢6öç7BWFòF&vWE6×ÆW2Ò§V6S£¦¦Æ–Ö—BƒÀ¢§V6S£§&÷VæEFô–çB‡6×ÆU&FR¢Ö†–×VÕFW‡GW&U6V6öæG2’À¢§V6S£§&÷VæEFô–çB‡7FF–5ö67CÆF÷V&ÆSâ‡6×ÆW2¢¢6×ÆU&FRò6fVE6×ÆU&FR’“°¢§V6S£¤VF–ô'VffW#ÆfÆöCâ&W7F÷&VE6÷W&6TD†÷7E&FS°¢–b‡&W7F÷&VE6÷W&6RævWDçVÕ6×ÆW2‚’â¢°¢6öç7BWFòF&vWE6÷W&6U6×ÆW2Ò§V6S£¦¦Æ–Ö—B€¢Â§V6S£§&÷VæEFô–çB‡6×ÆU&FR¢Ö†–×VÕFW‡GW&U6V6öæG2’À¢§V6S£§&÷VæEFô–çB‡7FF–5ö67CÆF÷V&ÆSâ‡&W7F÷&VE6÷W&6RævWDçVÕ6×ÆW2‚’¢¢6×ÆU&FRò6fVE6×ÆU&FR’“°¢&W7F÷&VE6÷W&6TD†÷7E&FRç6WE6—¦R‡&W7F÷&VE6÷W&6RævWDçVÔ6†ææVÇ2‚’À¢F&vWE6÷W&6U6×ÆW2ÂfÇ6RÂfÇ6RÂfÇ6R“°¢6öç7BWFò7VVE&F–òÒ6fVE6×ÆU&FRò6×ÆU&FS°¢f÷"†–çB6†ææVÂÒ²6†ææVÂÂ&W7F÷&VE6÷W&6RævWDçVÔ6†ææVÇ2‚“²²¶6†ææVÂ¢°¢§V6S£¥v–æF÷vVE6–æ4–çFW'öÆF÷"–çFW'öÆF÷#°¢–çFW'öÆF÷"ç&ö6W72‡7VVE&F–òÂ&W7F÷&VE6÷W&6RævWE&VEö–çFW"†6†ææVÂ’À¢&W7F÷&VE6÷W&6TD†÷7E&FRævWEw&—FUö–çFW"†6†ææVÂ’À¢F&vWE6÷W&6U6×ÆW2À¢&W7F÷&VE6÷W&6RævWDçVÕ6×ÆW2‚’Â“°¢Ğ¢Ğ¢WFò&W7F÷&VDf–æ—FRÒG'VS°¢WFò&W7F÷&VEG'VUV²ÒÓãc°¢°¢6öç7B7FC£§66÷VEöÆö6²Æö6²†Æö÷FF×WFW‚“°¢Æö÷'VffW"ç6WE6—¦R†6†ææVÇ2ÂF&vWE6×ÆW2ÂfÇ6RÂfÇ6RÂfÇ6R“°¢6öç7BWFò7VVE&F–òÒ6fVE6×ÆU&FRò6×ÆU&FS°¢f÷"†–çB6†ææVÂÒ²6†ææVÂÂ6†ææVÇ3²²¶6†ææVÂ¢°¢§V6S£¥v–æF÷vVE6–æ4–çFW'öÆF÷"–çFW'öÆF÷#°¢–çFW'öÆF÷"ç&ö6W72‡7VVE&F–òÂ&W7F÷&VBævWE&VEö–çFW"†6†ææVÂ’À¢Æö÷'VffW"ævWEw&—FUö–çFW"†6†ææVÂ’ÂF&vWE6×ÆW2À¢6×ÆW2Â“°¢Ğ¢&W7F÷&VDf–æ—FRÒ&VæFW%VÆ—G“£§&W—$æöäf–æ—FTæE&VÖ÷fTF2†Æö÷'VffW"“°¢&W7F÷&VEG'VUV²Ò&VæFW%VÆ—G“£¦Ç”6—&7VÆ%G'VUV´6V–Æ–ær†Æö÷'VffW"ÂÓãb“°¢Ğ ¢6öç7BWFò6÷W&6U66ÆRÒ7FF–5ö67CÆF÷V&ÆSâ‡6×ÆU&FR’ò6fVE6×ÆU&FS°¢–b‡&W7F÷&VE6÷W&6TD†÷7E&FRævWDçVÕ6×ÆW2‚’â¢°¢6öç7B7FC£§66÷VEöÆö6²Æö6²‡6÷W&6TFF×WFW‚“°¢7W'&VçE6÷W&6T'VffW"Ò7FC£¦Ö÷fR‡&W7F÷&VE6÷W&6TD†÷7E&FR“°¢7W'&VçE6÷W&6TæÖRÒ&W7F÷&VE6÷W&6TæÖS°¢vfVf÷&Õ&Wf–WrÒ'V–ÆEvfVf÷&Õ&Wf–Wr†7W'&VçE6÷W&6T'VffW"“°¢6÷W&6T6æF–FFW2æ6ÆV"‚“°¢FW‡GW&Uf&–çG2æ6ÆV"‚“°¢6÷W&6U&Wf—6–öâæfWF6…öFBƒÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆV6R“°¢6æF–FFU&Wf—6–öâæfWF6…öFBƒÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆV6R“°¢Ğ¢VÇ6P¢°¢6öç7B7FC£§66÷VEöÆö6²Æö6²‡6÷W&6TFF×WFW‚“°¢7W'&VçE6÷W&6T'VffW"ç6WE6—¦RƒÂ“°¢7W'&VçE6÷W&6TæÖRæ6ÆV"‚“°¢vfVf÷&Õ&Wf–Wræ6ÆV"‚“°¢6÷W&6T6æF–FFW2æ6ÆV"‚“°¢FW‡GW&Uf&–çG2æ6ÆV"‚“°¢6÷W&6U&Wf—6–öâæfWF6…öFBƒÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆV6R“°¢6æF–FFU&Wf—6–öâæfWF6…öFBƒÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆV6R“°¢Ğ ¢6GW&VE6×ÆT6÷VçBç7F÷&R‡F&vWE6×ÆW2“°¢VffV7F—fT7&÷76fFU6×ÆW2ç7F÷&R‡6fVEfW'6–öâÃÒ0¢bb6fVDÖöFRÓÒvVæW&F–öäÖöFS£§&÷FFU&W— ¢ò§V6S£¦¦Æ–Ö—BƒÂF&vWE6×ÆW2ò2À¢§V6S£§&÷VæEFô–çB‡6×ÆU&FP¢¢7&÷76fFTÖ–ÆÆ—6V6öæG2æÆöB‡7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆ†VB’¢ã’¢¢“°¢Æ–&6µ÷6—F–öâÒVffV7F—fT7&÷76fFU6×ÆW2æÆöB‡7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆ†VB“°¢6VÕVÆ—G’ç7F÷&R‡6fVD÷fW&ÆÂ“°¢ÆWfVÅ66÷&Rç7F÷&R‡6fVDÆWfVÂ“°¢6Æ÷U66÷&Rç7F÷&R‡6fVE6Æ÷R“°¢7V7G'VÕ66÷&Rç7F÷&R‡6fVE7V7G'VÒ“°¢†6U66÷&Rç7F÷&R‡6fVE†6R“°¢7FW&Võ66÷&Rç7F÷&R‡6fVE7FW&Vò“°¢&W—%66÷&Rç7F÷&R‡6fVD÷fW&ÆÂ“°¢&WVE6fWG•66÷&Rç7F÷&R‡6fVE&WVE6fWG’“°¢G'VUV´F'Gç7F÷&R‡&W7F÷&VEG'VUV²“°¢&VæFW%VÆ—G•66÷&Rç7F÷&R‡6fVEVÆ—G’“°¢6öç7BWFò&W7F÷&VDvFU76VBÒ6fVDvFU76VBbb&W7F÷&VDf–æ—FP¢bb&W7F÷&VEG'VUV²ÃÒÓãƒVc°¢VÆ—G”vFU76VBç7F÷&R‡&W7F÷&VDvFU76VB“°¢Æ÷t6öæf–FVæ6Rç7F÷&R‚&W7F÷&VDvFU76VB“°¢Æ7EW6VDvVæW&F–öäÖöFRç7F÷&R‡6fVDÖöFR“°¢6öç7BWFò66ÆU6×ÆRÒ·6÷W&6U66ÆUÒ†6öç7B–çB6×ÆR¢°¢&WGW&â6×ÆRÂòÓ¢§V6S£§&÷VæEFô–çB‡6×ÆR¢6÷W&6U66ÆR“°¢Ó°¢6öç7BWFò&W7F÷&VE6÷W&6U6×ÆW2Ò7W'&VçE6÷W&6T'VffW"ævWDçVÕ6×ÆW2‚“°¢–b‡6fVEfW'6–öâãÒRbb&W7F÷&VE6÷W&6U6×ÆW2â¢°¢6VÆV7FVE6÷W&6U6×ÆW2ç7F÷&R†§V6S£¦¦Æ–Ö—B€¢Â&W7F÷&VE6÷W&6U6×ÆW2À¢§V6S£¦¦Ö‚ƒÂ66ÆU6×ÆR‡6fVE6VÆV7FVE6÷W&6U6×ÆW2’’’“°¢6VÆV7FVE7F'E6×ÆRç7F÷&R†§V6S£¦¦Æ–Ö—B€¢Â&W7F÷&VE6÷W&6U6×ÆW2ÒÂ66ÆU6×ÆR‡6fVE6VÆV7FVE7F'B’’“°¢6VÆV7FVDVæE6×ÆRç7F÷&R†§V6S£¦¦Æ–Ö—B€¢6VÆV7FVE7F'E6×ÆRæÆöB‚’²Â&W7F÷&VE6÷W&6U6×ÆW2À¢66ÆU6×ÆR‡6fVE6VÆV7FVDVæB’’“°¢6öç7BWFò&W7F÷&VE6VÆV7F–öå6×ÆW2Ò6VÆV7FVDVæE6×ÆRæÆöB‚¢Ò6VÆV7FVE7F'E6×ÆRæÆöB‚“°¢6VÆV7FVE&÷FF–öå6×ÆRç7F÷&R‡6fVE&÷FF–öâÂÇÂ&W7F÷&VE6VÆV7F–öå6×ÆW2Â0¢òÓ¢§V6S£¦¦Æ–Ö—B€¢6VÆV7FVE7F'E6×ÆRæÆöB‚’²Â6VÆV7FVDVæE6×ÆRæÆöB‚’ÒÀ¢66ÆU6×ÆR‡6fVE&÷FF–öâ’’“°¢æÇ—6—5&ævU7F'E6×ÆRç7F÷&R†§V6S£¦¦Æ–Ö—B€¢Â&W7F÷&VE6÷W&6U6×ÆW2ÒÂ66ÆU6×ÆR‡6fVE&ævU7F'B’’“°¢æÇ—6—5&ævTVæE6×ÆRç7F÷&R†§V6S£¦¦Æ–Ö—B€¢æÇ—6—5&ævU7F'E6×ÆRæÆöB‚’²Â&W7F÷&VE6÷W&6U6×ÆW2À¢66ÆU6×ÆR‡6fVE&ævTVæB’’“°¢6÷W&6UÆ–&6µ÷6—F–öâÒæÇ—6—5&ævU7F'E6×ÆRæÆöB‚“°¢Ğ¢VÇ6P¢°¢6VÆV7FVE6÷W&6U6×ÆW2ç7F÷&Rƒ“°¢6VÆV7FVE7F'E6×ÆRç7F÷&Rƒ“°¢6VÆV7FVDVæE6×ÆRç7F÷&Rƒ“°¢6VÆV7FVE&÷FF–öå6×ÆRç7F÷&R‚Ó“°¢æÇ—6—5&ævU7F'E6×ÆRç7F÷&Rƒ“°¢æÇ—6—5&ævTVæE6×ÆRç7F÷&Rƒ“°¢6÷W&6UÆ–&6µ÷6—F–öâÒ°¢Ğ¢7F—fUFW‡GW&Uf&–çBç7F÷&R‚ÓÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆ†VB“°¢&VæFW%VÆ—G“£¥6–væÅ6æ6†÷B&W7F÷&VE6æ6†÷C°¢–b‡&W7F÷&VE6÷W&6U6×ÆW2â¢°¢§V6S£¤VF–ô'VffW#ÆfÆöCâ6æ6†÷E6÷W&6S°¢§V6S£¤VF–ô'VffW#ÆfÆöCâ6æ6†÷D÷WGWC°¢°¢6öç7B7FC£§66÷VEöÆö6²Æö6²‡6÷W&6TFF×WFW‚“°¢6öç7BWFò&ævU7F'BÒæÇ—6—5&ævU7F'E6×ÆRæÆöB‚“°¢6öç7BWFò&ævTVæBÒæÇ—6—5&ævTVæE6×ÆRæÆöB‚“°¢6æ6†÷E6÷W&6Rç6WE6—¦R†7W'&VçE6÷W&6T'VffW"ævWDçVÔ6†ææVÇ2‚’À¢&ævTVæBÒ&ævU7F'BÂfÇ6RÂfÇ6RÂfÇ6R“°¢f÷"†–çB6†ææVÂÒ²6†ææVÂÂ6æ6†÷E6÷W&6RævWDçVÔ6†ææVÇ2‚“²²¶6†ææVÂ¢6æ6†÷E6÷W&6Ræ6÷”g&öÒ†6†ææVÂÂÂ7W'&VçE6÷W&6T'VffW"Â6†ææVÂÀ¢&ævU7F'BÂ&ævTVæBÒ&ævU7F'B“°¢Ğ¢°¢6öç7B7FC£§66÷VEöÆö6²Æö6²†Æö÷FF×WFW‚“°¢6æ6†÷D÷WGWBÒÆö÷'VffW#°¢Ğ¢&W7F÷&VE6æ6†÷BÒ&VæFW%VÆ—G“£¦æÇ—6U6÷W&6TæD÷WGWB€¢6æ6†÷E6÷W&6RÂ6æ6†÷D÷WGWBÂ6×ÆU&FR“°¢Ğ¢°¢6öç7B7FC£§66÷VEöÆö6²Æö6²‡6–væÅ6æ6†÷D×WFW‚“°¢6–væÅ6æ6†÷BÒ&W7F÷&VE6æ6†÷C°¢Ğ¢§V6S£¦–væ÷&UVçW6VB‡6fVEG'VUV²“°¢6GW&U&öw&W72ç7F÷&Rƒãb“°¢æÇ—6—5&öw&W72ç7F÷&RƒãbÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆ†VB“°¢&Wf–WuÆ––ærç7F÷&R†fÇ6RÂ7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆV6R“°¢7FFRç7F÷&R…7FFS£§&VG’Â7FC£¦ÖVÖ÷'•ö÷&FW%÷&VÆV6R“°¢&WGW&âG'VS°§Ğ