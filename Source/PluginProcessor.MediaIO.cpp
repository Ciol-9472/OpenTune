#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include "Utils/AppLogger.h"

namespace OpenTune {

namespace {

constexpr double kExportSampleRateHz = 44100.0;
constexpr int kExportNumChannels = 1;
constexpr int kExportMasterNumChannels = 2;
constexpr int kExportBitsPerSample = static_cast<int>(sizeof(float) * 8);

inline float computeFadeGain(int64_t sampleInClip, int64_t clipLen, int64_t fadeInSamples, int64_t fadeOutSamples)
{
    float fade = 1.0f;
    if (fadeInSamples > 1 && sampleInClip < fadeInSamples) {
        fade *= static_cast<float>(sampleInClip) / static_cast<float>(fadeInSamples - 1);
    }
    if (fadeOutSamples > 1 && (clipLen - 1 - sampleInClip) < fadeOutSamples) {
        fade *= static_cast<float>(clipLen - 1 - sampleInClip) / static_cast<float>(fadeOutSamples - 1);
    }
    return fade;
}

template <typename ClipType>
void renderClipForExport(const ClipType& clip,
                         float trackGain,
                         int64_t clipStartInOutput,
                         juce::AudioBuffer<float>& out,
                         int64_t totalLen)
{
    constexpr double kExportSr = TimeCoordinate::kRenderSampleRate;

    const int64_t clipLen = clip.audioBuffer->getNumSamples();
    if (clipLen <= 0 || clipStartInOutput >= totalLen) {
        return;
    }

    const float clipGain = clip.gain;
    const float baseGain = trackGain * clipGain;
    const int64_t fadeInSamples =
        (clip.fadeInDuration > 0.0) ? TimeCoordinate::secondsToSamples(clip.fadeInDuration, kExportSr) : 0;
    const int64_t fadeOutSamples =
        (clip.fadeOutDuration > 0.0) ? TimeCoordinate::secondsToSamples(clip.fadeOutDuration, kExportSr) : 0;

    for (int ch = 0; ch < out.getNumChannels(); ++ch) {
        const float* src = clip.audioBuffer->getReadPointer(std::min(ch, clip.audioBuffer->getNumChannels() - 1));
        float* dst = out.getWritePointer(ch);
        for (int64_t i = 0; i < clipLen; ++i) {
            const int64_t dstIndex = clipStartInOutput + i;
            if (dstIndex < 0 || dstIndex >= totalLen) {
                continue;
            }
            const float fade = computeFadeGain(i, clipLen, fadeInSamples, fadeOutSamples);
            dst[static_cast<size_t>(dstIndex)] += src[static_cast<size_t>(i)] * baseGain * fade;
        }
    }

    if (!clip.renderCache) {
        return;
    }

    const auto publishedChunks = clip.renderCache->getPublishedChunks();
    if (publishedChunks.empty()) {
        return;
    }

    const double clipDurationSeconds = TimeCoordinate::samplesToSeconds(clipLen, kExportSr);
    for (const auto& chunk : publishedChunks) {
        if (chunk.startSeconds >= clipDurationSeconds || chunk.endSeconds <= 0.0) {
            continue;
        }
        if (!chunk.audio || chunk.audio->empty()) {
            continue;
        }

        const int64_t chunkStartSample = TimeCoordinate::secondsToSamples(chunk.startSeconds, kExportSr);
        const int64_t chunkEndSample = TimeCoordinate::secondsToSamples(chunk.endSeconds, kExportSr);
        const int64_t relStart = std::max<int64_t>(0, chunkStartSample);
        const int64_t relEnd = std::min<int64_t>(clipLen, chunkEndSample);
        if (relEnd <= relStart) {
            continue;
        }

        const int num = static_cast<int>(relEnd - relStart);
        const int64_t audioOffset = relStart - chunkStartSample;
        for (int ch = 0; ch < out.getNumChannels(); ++ch) {
            float* dst = out.getWritePointer(ch);
            const float* src = clip.audioBuffer->getReadPointer(std::min(ch, clip.audioBuffer->getNumChannels() - 1));
            for (int i = 0; i < num; ++i) {
                const int64_t clipIndex = relStart + i;
                const int64_t dstIndex = clipStartInOutput + clipIndex;
                if (dstIndex < 0 || dstIndex >= totalLen) {
                    continue;
                }

                const size_t audioIdx = static_cast<size_t>(audioOffset + i);
                if (audioIdx >= chunk.audio->size()) {
                    break;
                }

                const float fade = computeFadeGain(clipIndex, clipLen, fadeInSamples, fadeOutSamples);
                const float dryVal = src[static_cast<size_t>(clipIndex)] * baseGain * fade;
                const float renderedVal = (*chunk.audio)[audioIdx] * baseGain * fade;
                dst[static_cast<size_t>(dstIndex)] = dst[static_cast<size_t>(dstIndex)] - dryVal + renderedVal;
            }
        }
    }
}

} // namespace

bool OpenTuneAudioProcessor::exportClipAudio(int trackId, int clipIndex, const juce::File& file)
{
    lastExportError_.clear();

    if (trackId < 0 || trackId >= MAX_TRACKS) {
        lastExportError_ = "无效的轨道ID: " + juce::String(trackId);
        return false;
    }

    const juce::ScopedReadLock tracksReadLock(tracksLock_);
    auto& track = tracks_[static_cast<size_t>(trackId)];
    if (clipIndex < 0 || clipIndex >= static_cast<int>(track.clips.size())) {
        lastExportError_ = "无效的Clip索引: " + juce::String(clipIndex);
        return false;
    }

    auto& clip = track.clips[static_cast<size_t>(clipIndex)];
    const int64_t clipLen = clip.audioBuffer->getNumSamples();
    if (clipLen <= 0) {
        lastExportError_ = "Clip音频长度为零";
        return false;
    }

    juce::AudioBuffer<float> out(kExportNumChannels, static_cast<int>(clipLen));
    out.clear();
    renderClipForExport(clip, track.volume, 0, out, clipLen);

    auto outFile = file;
    if (!outFile.hasFileExtension(".wav")) {
        outFile = outFile.withFileExtension(".wav");
    }
    outFile.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (!stream) {
        lastExportError_ = "无法创建输出文件";
        return false;
    }

    std::unique_ptr<juce::OutputStream> outStream(stream.release());
    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate(kExportSampleRateHz)
                       .withNumChannels(out.getNumChannels())
                       .withBitsPerSample(kExportBitsPerSample);

    auto writer = wav.createWriterFor(outStream, options);
    if (!writer) {
        lastExportError_ = "无法创建WAV写入器";
        return false;
    }

    return writer->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
}

bool OpenTuneAudioProcessor::exportTrackAudio(int trackId, const juce::File& file)
{
    lastExportError_.clear();

    if (trackId < 0 || trackId >= MAX_TRACKS) {
        lastExportError_ = "无效的轨道ID: " + juce::String(trackId);
        return false;
    }

    const juce::ScopedReadLock tracksReadLock(tracksLock_);
    auto& track = tracks_[static_cast<size_t>(trackId)];
    if (track.clips.empty()) {
        lastExportError_ = "轨道 " + juce::String(trackId + 1) + " 没有音频片段";
        return false;
    }

    constexpr double kExportSr = TimeCoordinate::kRenderSampleRate;
    int64_t totalLen = 0;
    for (const auto& clip : track.clips) {
        const int64_t clipStart = TimeCoordinate::secondsToSamples(clip.startSeconds, kExportSr);
        const int64_t clipEnd = clipStart + clip.audioBuffer->getNumSamples();
        totalLen = std::max(totalLen, clipEnd);
    }
    if (totalLen <= 0) {
        lastExportError_ = "音频总长度为零或无效";
        return false;
    }

    juce::AudioBuffer<float> out(kExportNumChannels, static_cast<int>(totalLen));
    out.clear();

    for (const auto& clip : track.clips) {
        const int64_t clipStart = TimeCoordinate::secondsToSamples(clip.startSeconds, kExportSr);
        renderClipForExport(clip, track.volume, clipStart, out, totalLen);
    }

    auto outFile = file;
    if (!outFile.hasFileExtension(".wav")) {
        outFile = outFile.withFileExtension(".wav");
    }
    outFile.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (!stream) {
        return false;
    }

    std::unique_ptr<juce::OutputStream> outStream(stream.release());
    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate(kExportSampleRateHz)
                       .withNumChannels(out.getNumChannels())
                       .withBitsPerSample(kExportBitsPerSample);

    auto writer = wav.createWriterFor(outStream, options);
    if (!writer) {
        return false;
    }

    return writer->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
}

bool OpenTuneAudioProcessor::exportMasterMixAudio(const juce::File& file)
{
    const juce::ScopedReadLock tracksReadLock(tracksLock_);

    constexpr double kExportSr = TimeCoordinate::kRenderSampleRate;
    int64_t totalLen = 0;
    for (int trackId = 0; trackId < static_cast<int>(tracks_.size()); ++trackId) {
        const auto& track = tracks_[static_cast<size_t>(trackId)];
        for (const auto& clip : track.clips) {
            const int64_t clipStart = TimeCoordinate::secondsToSamples(clip.startSeconds, kExportSr);
            const int64_t clipEnd = clipStart + clip.audioBuffer->getNumSamples();
            totalLen = std::max(totalLen, clipEnd);
        }
    }
    if (totalLen <= 0) {
        return false;
    }

    juce::AudioBuffer<float> mix(kExportMasterNumChannels, static_cast<int>(totalLen));
    mix.clear();

    bool anySolo = false;
    for (const auto& t : tracks_) {
        if (t.isSolo) {
            anySolo = true;
            break;
        }
    }

    for (int trackId = 0; trackId < static_cast<int>(tracks_.size()); ++trackId) {
        auto& track = tracks_[static_cast<size_t>(trackId)];
        if (anySolo) {
            if (!track.isSolo) {
                continue;
            }
        } else if (track.isMuted) {
            continue;
        }

        if (track.clips.empty()) {
            continue;
        }

        for (const auto& clip : track.clips) {
            const int64_t clipStart = TimeCoordinate::secondsToSamples(clip.startSeconds, kExportSr);
            renderClipForExport(clip, track.volume, clipStart, mix, totalLen);
        }
    }

    auto outFile = file;
    if (!outFile.hasFileExtension(".wav")) {
        outFile = outFile.withFileExtension(".wav");
    }
    outFile.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (!stream) {
        return false;
    }

    std::unique_ptr<juce::OutputStream> outStream(stream.release());
    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate(kExportSampleRateHz)
                       .withNumChannels(mix.getNumChannels())
                       .withBitsPerSample(kExportBitsPerSample);

    auto writer = wav.createWriterFor(outStream, options);
    if (!writer) {
        return false;
    }

    return writer->writeFromAudioSampleBuffer(mix, 0, mix.getNumSamples());
}

void OpenTuneAudioProcessor::setPlaying(bool playing)
{
    if (playing) {
        playStartPosition_.store(positionAtomic_->load(std::memory_order_relaxed));
        isFadingOut_.store(false);
        isPlaying_.store(true);
        useDrySignalFallback_.store(false);
        isBuffering_.store(false);
        AppLogger::log("Playback: start");
    } else if (isPlaying_.load()) {
        isFadingOut_.store(true);
        fadeOutSampleCount_.store(0);
        AppLogger::log("Playback: fade-out started");
    }
}

void OpenTuneAudioProcessor::setLoopEnabled(bool enabled)
{
    loopEnabled_.store(enabled);
}

void OpenTuneAudioProcessor::setPosition(double seconds)
{
    positionAtomic_->store(seconds, std::memory_order_relaxed);
}

double OpenTuneAudioProcessor::getPosition() const
{
    return positionAtomic_->load(std::memory_order_relaxed);
}

void OpenTuneAudioProcessor::setBpm(double bpm)
{
    bpm_ = bpm;

#if !JucePlugin_Build_Standalone
    hostBpm_.store(bpm);
#endif
}

void OpenTuneAudioProcessor::setZoomLevel(double zoom)
{
    zoomLevel_ = zoom;
}

bool OpenTuneAudioProcessor::prepareImportClip(int trackId,
                                               juce::AudioBuffer<float>&& inBuffer,
                                               double inSampleRate,
                                               const juce::String& clipName,
                                               PreparedImportClip& out)
{
    if (trackId < 0 || trackId >= MAX_TRACKS) {
        AppLogger::log("Import rejected: invalid trackId=" + juce::String(trackId));
        return false;
    }
    if (inBuffer.getNumSamples() <= 0) {
        AppLogger::log("Import rejected: empty audio buffer (zero samples) for '" + clipName + "'");
        return false;
    }
    if (inBuffer.getNumChannels() <= 0) {
        AppLogger::log("Import rejected: invalid channel count=" + juce::String(inBuffer.getNumChannels()) + " for '" + clipName + "'");
        return false;
    }
    if (inSampleRate <= 0.0) {
        AppLogger::log("Import rejected: invalid sample rate=" + juce::String(inSampleRate, 2) + " for '" + clipName + "'");
        return false;
    }

    PerfTimer perfTimer("prepareImportClip");

    out.trackId = trackId;
    out.clipName = clipName;

    const double targetSampleRate = TimeCoordinate::kRenderSampleRate;
    if (std::abs(inSampleRate - targetSampleRate) > 1.0) {
        const int numChannels = inBuffer.getNumChannels();
        const int originalLen = inBuffer.getNumSamples();
        const double sourceDurationSeconds = TimeCoordinate::samplesToSeconds(originalLen, inSampleRate);
        const int newLen = juce::jmax(
            1,
            static_cast<int>(TimeCoordinate::secondsToSamples(sourceDurationSeconds, targetSampleRate)));

        out.hostRateBuffer.setSize(numChannels, newLen);
        for (int ch = 0; ch < numChannels; ++ch) {
            auto resampledData = resamplingManager_->upsampleForHost(
                inBuffer.getReadPointer(ch),
                originalLen,
                static_cast<int>(inSampleRate),
                static_cast<int>(targetSampleRate));
            const int toCopy = juce::jmin(newLen, static_cast<int>(resampledData.size()));
            out.hostRateBuffer.copyFrom(ch, 0, resampledData.data(), toCopy);
        }
    } else {
        out.hostRateBuffer = std::move(inBuffer);
    }

    out.silentGaps.clear();
    return true;
}

bool OpenTuneAudioProcessor::loadAudioFileToHostRateBuffer(const juce::File& file, juce::AudioBuffer<float>& outHostRate)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) {
        return false;
    }

    const int numChannels = static_cast<int>(reader->numChannels);
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    if (numChannels <= 0 || numSamples <= 0) {
        return false;
    }

    juce::AudioBuffer<float> inBuffer(numChannels, numSamples);
    reader->read(&inBuffer, 0, numSamples, 0, true, true);
    const double inSampleRate = reader->sampleRate;
    if (inSampleRate <= 0.0) {
        return false;
    }

    const double targetSampleRate = TimeCoordinate::kRenderSampleRate;
    if (std::abs(inSampleRate - targetSampleRate) > 1.0) {
        const int originalLen = inBuffer.getNumSamples();
        const double sourceDurationSeconds = TimeCoordinate::samplesToSeconds(originalLen, inSampleRate);
        const int newLen =
            juce::jmax(1, static_cast<int>(TimeCoordinate::secondsToSamples(sourceDurationSeconds, targetSampleRate)));

        outHostRate.setSize(numChannels, newLen);
        for (int ch = 0; ch < numChannels; ++ch) {
            auto resampledData = resamplingManager_->upsampleForHost(
                inBuffer.getReadPointer(ch),
                originalLen,
                static_cast<int>(inSampleRate),
                static_cast<int>(targetSampleRate));
            const int toCopy = juce::jmin(newLen, static_cast<int>(resampledData.size()));
            outHostRate.copyFrom(ch, 0, resampledData.data(), toCopy);
        }
    } else {
        outHostRate = std::move(inBuffer);
    }

    return true;
}

bool OpenTuneAudioProcessor::commitPreparedImportClip(PreparedImportClip&& prepared)
{
    if (prepared.trackId < 0 || prepared.trackId >= MAX_TRACKS) {
        return false;
    }

    PerfTimer perfTimer("commitPreparedImportClip");

    const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
    TrackState::AudioClip clip;
    clip.clipId = nextClipId_.fetch_add(1);
    clip.audioBuffer = std::make_shared<const juce::AudioBuffer<float>>(std::move(prepared.hostRateBuffer));
    clip.sourceAudioAbsolutePath = prepared.sourceAudioAbsolutePath;
    clip.startSeconds = 0.0;
    clip.gain = 1.0f;
    clip.name = prepared.clipName;
    clip.colour = juce::Colour::fromHSV(prepared.trackId * 0.3f, 0.6f, 0.8f, 1.0f);
    clip.originalF0State = OriginalF0State::NotRequested;
    clip.silentGaps = std::move(prepared.silentGaps);
    clip.renderCache = std::make_shared<RenderCache>();

    const double deviceSampleRate = currentSampleRate_.load(std::memory_order_relaxed);
    resampleDrySignal(clip, deviceSampleRate);

    {
        const double clipDurSec = TimeCoordinate::samplesToSeconds(clip.audioBuffer->getNumSamples(),
                                                                    TimeCoordinate::kRenderSampleRate);
        auto doc = std::make_shared<SingingEditDocument>(*clip.getSingingEditDocument());
        doc->ensureDefaultFullClipSegment(clipDurSec);
        SingingEditCommitOptions insOpts;
        insOpts.bumpGeneration = false;
        insOpts.clearPendingRefine = true;
        insOpts.bumpDocumentRevision = false;
        insOpts.bumpEditVersion = false;
        if (!commitValidatedSingingEditOntoClip(clip, std::move(doc), clipDurSec, std::move(insOpts))) {
            return false;
        }
    }

    tracks_[prepared.trackId].clips.push_back(std::move(clip));
    return true;
}

bool OpenTuneAudioProcessor::prepareDeferredClipPostProcess(int trackId,
                                                            uint64_t clipId,
                                                            OpenTuneAudioProcessor::PreparedClipPostProcess& out) const
{
    if (trackId < 0 || trackId >= MAX_TRACKS || clipId == 0) {
        return false;
    }

    juce::AudioBuffer<float> clipAudio;
    {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        auto it = std::find_if(clips.begin(), clips.end(), [clipId](const TrackState::AudioClip& c) {
            return c.clipId == clipId;
        });
        if (it == clips.end()) {
            return false;
        }
        if (it->audioBuffer->getNumSamples() <= 0 || it->audioBuffer->getNumChannels() <= 0) {
            return false;
        }
        clipAudio.makeCopyOf(*it->audioBuffer);
    }

    out = OpenTuneAudioProcessor::PreparedClipPostProcess{};
    out.trackId = trackId;
    out.clipId = clipId;

    {
        PerfTimer perfSilent("deferredImportPostProcess_silentGapDetection");
        out.silentGaps = SilentGapDetector::detectAllGapsAdaptive(clipAudio);
    }

    return true;
}

bool OpenTuneAudioProcessor::commitDeferredClipPostProcess(int trackId,
                                                           uint64_t clipId,
                                                           OpenTuneAudioProcessor::PreparedClipPostProcess&& prepared)
{
    if (trackId < 0 || trackId >= MAX_TRACKS || clipId == 0) {
        return false;
    }

    const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
    auto& clips = tracks_[trackId].clips;
    auto it = std::find_if(clips.begin(), clips.end(), [clipId](const TrackState::AudioClip& c) {
        return c.clipId == clipId;
    });
    if (it == clips.end()) {
        return false;
    }

    it->silentGaps = std::move(prepared.silentGaps);
    return true;
}

} // namespace OpenTune
