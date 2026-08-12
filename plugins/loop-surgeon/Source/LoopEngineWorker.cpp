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
        const auto last = juce::jmax(first + 1, (bin + 1) * source.getNumSamples() / previewBins);
        for (int channel = 0; channel < source.getNumChannels(); ++channel)
            preview[static_cast<size_t>(bin)] = juce::jmax(
                preview[static_cast<size_t>(bin)],
                source.getMagnitude(channel, first, last - first));
    }
    return preview;
}
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
        }
        else
        {
            TextureSynthesisSettings settings;
            settings.durationSeconds = textureDurationSeconds.load(
                std::memory_order_relaxed);
            settings.variation = textureVariation.load(std::memory_order_relaxed);
            settings.flatten = textureFlatten.load(std::memory_order_relaxed);
            settings.dynamicsCrush = textureDynamicsCrush.load(std::memory_order_relaxed);
            settings.sourceMatch = textureSourceMatch.load(std::memory_order_relaxed);
            settings.structure = textureStructure.load(std::memory_order_relaxed);
            const auto baseSeed = textureSeed.load(std::memory_order_relaxed);
            generatedTextures.reserve(1);
            constexpr uint32_t maximumAttempts = 1;
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
                if (generatedTextures.size() == 1u)
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
            state.store(State::failed, std::memory_order_release);
            continue;
        }

        if (useTexture)
        {
            const auto nextSnapshot = SignalDiagnostics::analyseSourceAndOutput(
                *analysisSource, generatedTextures.front().audio, sampleRate);
            const auto primarySamples = generatedTextures.front().audio.getNumSamples();
            const auto primaryTruePeak = generatedTextures.front().truePeakDbtp;
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
            truePeakDbtp.store(primaryTruePeak);
            preferLinearRepairFade.store(false);
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
                1.0e-9f, SignalDiagnostics::estimateCircularTruePeak(selectedAudio)))
            : -100.0f;
        const auto nextSnapshot = result.rotationSample >= 0
            ? SignalDiagnostics::analyseSourceAndOutput(
                *analysisSource, selectedAudio, sampleRate)
            : SignalDiagnostics::SignalSnapshot {};
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
        truePeakDbtp.store(selectedTruePeak);
        preferLinearRepairFade.store(result.preferLinearRepairFade);
        lastUsedGenerationMode.store(GenerationMode::rotateRepair);
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

void LoopEngine::resetDiagnostics() noexcept
{
    truePeakDbtp.store(-100.0f);
    preferLinearRepairFade.store(false);
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
        compressed.writeInt(preferLinearRepairFade.load() ? 1 : 0);
        compressed.writeFloat(truePeakDbtp.load());
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

    auto savedPreferLinearFade = false;
    if (savedVersion >= 6)
    {
        savedPreferLinearFade = decompressed.readInt() != 0;
        static_cast<void>(decompressed.readFloat());
    }
    else
    {
        static_cast<void>(decompressed.readFloat());
        static_cast<void>(decompressed.readFloat());
        static_cast<void>(decompressed.readFloat());
        static_cast<void>(decompressed.readFloat());
        const auto legacyPhaseSimilarity = decompressed.readFloat();
        static_cast<void>(decompressed.readFloat());
        savedPreferLinearFade = legacyPhaseSimilarity >= 75.0f;
        if (savedVersion >= 3)
        {
            static_cast<void>(decompressed.readFloat());
            static_cast<void>(decompressed.readFloat());
            static_cast<void>(decompressed.readFloat());
            static_cast<void>(decompressed.readInt());
        }
    }
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
        juce::ignoreUnused(SignalDiagnostics::repairNonFiniteAndRemoveDc(loopBuffer));
        restoredTruePeak = SignalDiagnostics::applyCircularTruePeakCeiling(loopBuffer, -1.0f);
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
    truePeakDbtp.store(restoredTruePeak);
    preferLinearRepairFade.store(savedPreferLinearFade);
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
    SignalDiagnostics::SignalSnapshot restoredSnapshot;
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
        restoredSnapshot = SignalDiagnostics::analyseSourceAndOutput(
            snapshotSource, snapshotOutput, sampleRate);
    }
    {
        const std::scoped_lock lock(signalSnapshotMutex);
        signalSnapshot = restoredSnapshot;
    }
    captureProgress.store(1.0f);
    analysisProgress.store(1.0f, std::memory_order_relaxed);
    previewPlaying.store(false, std::memory_order_release);
    state.store(State::ready, std::memory_order_release);
    return true;
}
