#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include "Utils/AppLogger.h"

namespace OpenTune {

namespace {

template <typename ClipT>
void computeClipSilentGaps(ClipT& clip)
{
    clip.silentGaps.clear();

    if (!clip.audioBuffer) {
        return;
    }
    const int64_t clipLen = static_cast<int64_t>(clip.audioBuffer->getNumSamples());
    if (clipLen <= 0) {
        return;
    }

    clip.silentGaps = SilentGapDetector::detectAllGapsAdaptive(*clip.audioBuffer);
}

} // namespace

void OpenTuneAudioProcessor::setActiveTrack(int trackId)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        activeTrackId_ = trackId;
    }
}

void OpenTuneAudioProcessor::setTrackMuted(int trackId, bool muted)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        tracks_[trackId].isMuted = muted;
    }
}

bool OpenTuneAudioProcessor::isTrackMuted(int trackId) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        return tracks_[trackId].isMuted;
    }
    return false;
}

void OpenTuneAudioProcessor::setTrackSolo(int trackId, bool solo)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        tracks_[trackId].isSolo = solo;
        anyTrackSoloed_ = false;
        for (const auto& t : tracks_) {
            if (t.isSolo) {
                anyTrackSoloed_ = true;
                break;
            }
        }
    }
}

bool OpenTuneAudioProcessor::isTrackSolo(int trackId) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        return tracks_[trackId].isSolo;
    }
    return false;
}

void OpenTuneAudioProcessor::setTrackVolume(int trackId, float volume)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        tracks_[trackId].volume = volume;
    }
}

float OpenTuneAudioProcessor::getTrackVolume(int trackId) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        return tracks_[trackId].volume;
    }
    return 1.0f;
}

float OpenTuneAudioProcessor::getTrackRMS(int trackId) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        return tracks_[trackId].currentRMS.load();
    }
    return -100.0f;
}

void OpenTuneAudioProcessor::setTrackHeight(int height)
{
    trackHeight_ = height;
}

int OpenTuneAudioProcessor::getNumClips(int trackId) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        return static_cast<int>(tracks_[trackId].clips.size());
    }
    return 0;
}

bool OpenTuneAudioProcessor::hasAnyClipOnAnyTrack() const
{
    const juce::ScopedReadLock tracksReadLock(tracksLock_);
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        if (!tracks_[static_cast<size_t>(i)].clips.empty())
            return true;
    }
    return false;
}

int OpenTuneAudioProcessor::getSelectedClip(int trackId) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        return tracks_[trackId].selectedClipIndex;
    }
    return -1;
}

void OpenTuneAudioProcessor::setSelectedClip(int trackId, int clipIndex)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        tracks_[trackId].selectedClipIndex = clipIndex;
    }
}

std::shared_ptr<const juce::AudioBuffer<float>> OpenTuneAudioProcessor::getClipAudioBuffer(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].audioBuffer;
        }
    }
    return nullptr;
}

uint64_t OpenTuneAudioProcessor::getClipId(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].clipId;
        }
    }
    return 0;
}

int OpenTuneAudioProcessor::findClipIndexById(int trackId, uint64_t clipId) const
{
    if (clipId == 0) {
        return -1;
    }
    if (trackId < 0 || trackId >= MAX_TRACKS) {
        return -1;
    }

    const juce::ScopedReadLock tracksReadLock(tracksLock_);
    const auto& clips = tracks_[trackId].clips;
    for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
        if (clips[static_cast<size_t>(i)].clipId == clipId) {
            return i;
        }
    }
    return -1;
}

double OpenTuneAudioProcessor::getClipStartSeconds(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].startSeconds;
        }
    }
    return 0.0;
}

juce::String OpenTuneAudioProcessor::getClipName(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].name;
        }
    }
    return {};
}

float OpenTuneAudioProcessor::getClipGain(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].gain;
        }
    }
    return 1.0f;
}

void OpenTuneAudioProcessor::setClipStartSeconds(int trackId, int clipIndex, double startSeconds)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            clips[clipIndex].startSeconds = std::max(0.0, startSeconds);
        }
    }
}

void OpenTuneAudioProcessor::setClipGain(int trackId, int clipIndex, float gain)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            clips[clipIndex].gain = std::max(0.0f, gain);
        }
    }
}

bool OpenTuneAudioProcessor::splitClipAtSeconds(int trackId, int clipIndex, double splitSeconds)
{
    if (trackId < 0 || trackId >= MAX_TRACKS) {
        AppLogger::log("Split rejected: invalid trackId=" + juce::String(trackId));
        return false;
    }

    const juce::ScopedWriteLock tracksWriteLock(tracksLock_);

    auto& clips = tracks_[trackId].clips;
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size())) {
        AppLogger::log("Split rejected: invalid clipIndex=" + juce::String(clipIndex) + " for trackId=" + juce::String(trackId));
        return false;
    }

    auto& originalClip = clips[clipIndex];
    const double clipStartSeconds = originalClip.startSeconds;
    const double splitPointSeconds = splitSeconds - clipStartSeconds;

    constexpr double kStoredSampleRate = AudioConstants::StoredAudioSampleRate;
    const int64_t splitPointInClip = static_cast<int64_t>(splitPointSeconds * kStoredSampleRate);
    const int64_t totalSamples = originalClip.audioBuffer->getNumSamples();

    const int64_t minLen = static_cast<int64_t>(0.1 * kStoredSampleRate);
    if (splitPointInClip < minLen || splitPointInClip > totalSamples - minLen) {
        AppLogger::log("Split rejected: split point out of valid range. splitPoint="
                       + juce::String(static_cast<double>(splitPointInClip / kStoredSampleRate), 3)
                       + "s, clipLen=" + juce::String(static_cast<double>(totalSamples / kStoredSampleRate), 3) + "s");
        return false;
    }

    TrackState::AudioClip newClip;
    newClip.clipId = nextClipId_.fetch_add(1);
    newClip.name = originalClip.name;
    newClip.colour = originalClip.colour;
    newClip.gain = originalClip.gain;
    newClip.startSeconds = splitSeconds;
    newClip.fadeInDuration = 0.25;
    newClip.fadeOutDuration = originalClip.fadeOutDuration;
    newClip.sourceAudioAbsolutePath.clear();
    originalClip.sourceAudioAbsolutePath.clear();

    const int64_t newLength = totalSamples - splitPointInClip;
    const int numCh = originalClip.audioBuffer->getNumChannels();
    auto newRightBuffer = std::make_shared<juce::AudioBuffer<float>>(numCh, static_cast<int>(newLength));
    for (int ch = 0; ch < numCh; ++ch) {
        newRightBuffer->copyFrom(
            ch, 0, *originalClip.audioBuffer, ch, static_cast<int>(splitPointInClip), static_cast<int>(newLength));
    }
    newClip.audioBuffer = newRightBuffer;

    auto newLeftBuffer = std::make_shared<juce::AudioBuffer<float>>(numCh, static_cast<int>(splitPointInClip));
    for (int ch = 0; ch < numCh; ++ch) {
        newLeftBuffer->copyFrom(ch, 0, *originalClip.audioBuffer, ch, 0, static_cast<int>(splitPointInClip));
    }
    originalClip.audioBuffer = newLeftBuffer;
    originalClip.fadeOutDuration = 0.25;

    computeClipSilentGaps(originalClip);
    computeClipSilentGaps(newClip);

    clips.insert(clips.begin() + clipIndex + 1, std::move(newClip));
    tracks_[trackId].selectedClipIndex = clipIndex + 1;

    return true;
}

void OpenTuneAudioProcessor::setClipGainById(int trackId, uint64_t clipId, float gain)
{
    if (trackId < 0 || trackId >= MAX_TRACKS || clipId == 0) {
        return;
    }

    const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
    auto& clips = tracks_[trackId].clips;
    for (auto& clip : clips) {
        if (clip.clipId == clipId) {
            clip.gain = std::max(0.0f, gain);
            return;
        }
    }
}

bool OpenTuneAudioProcessor::mergeSplitClips(int trackId, uint64_t originalClipId, uint64_t newClipId, int targetClipIndex)
{
    if (trackId < 0 || trackId >= MAX_TRACKS) {
        AppLogger::log("Merge rejected: invalid trackId=" + juce::String(trackId));
        return false;
    }

    const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
    auto& clips = tracks_[trackId].clips;

    int originalIndex = -1;
    int newIndex = -1;
    for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
        if (clips[i].clipId == originalClipId) {
            originalIndex = i;
        }
        if (clips[i].clipId == newClipId) {
            newIndex = i;
        }
    }

    if (originalIndex < 0 || newIndex < 0) {
        AppLogger::log("Merge rejected: clip not found. originalClipId="
                       + juce::String(static_cast<juce::int64>(originalClipId))
                       + " newClipId=" + juce::String(static_cast<juce::int64>(newClipId)));
        return false;
    }

    auto& originalClip = clips[originalIndex];
    auto& newClip = clips[newIndex];

    const int originalSamples = originalClip.audioBuffer->getNumSamples();
    const int newSamples = newClip.audioBuffer->getNumSamples();
    const int channels = std::max(originalClip.audioBuffer->getNumChannels(), newClip.audioBuffer->getNumChannels());

    auto mergedBuffer = std::make_shared<juce::AudioBuffer<float>>(channels, originalSamples + newSamples);
    for (int ch = 0; ch < originalClip.audioBuffer->getNumChannels(); ++ch) {
        mergedBuffer->copyFrom(ch, 0, *originalClip.audioBuffer, ch, 0, originalSamples);
    }
    for (int ch = 0; ch < newClip.audioBuffer->getNumChannels(); ++ch) {
        mergedBuffer->copyFrom(ch, originalSamples, *newClip.audioBuffer, ch, 0, newSamples);
    }
    originalClip.audioBuffer = mergedBuffer;
    originalClip.sourceAudioAbsolutePath.clear();
    originalClip.fadeOutDuration = newClip.fadeOutDuration;

    computeClipSilentGaps(originalClip);
    clips.erase(clips.begin() + newIndex);

    if (targetClipIndex >= 0 && targetClipIndex < static_cast<int>(clips.size())) {
        tracks_[trackId].selectedClipIndex = targetClipIndex;
    }

    return true;
}

bool OpenTuneAudioProcessor::deleteClip(int trackId, int clipIndex)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            clips.erase(clips.begin() + clipIndex);
            if (tracks_[trackId].selectedClipIndex >= static_cast<int>(clips.size())) {
                tracks_[trackId].selectedClipIndex = std::max(0, static_cast<int>(clips.size()) - 1);
            }
            return true;
        }
    }
    return false;
}

bool OpenTuneAudioProcessor::getClipSnapshot(int trackId, uint64_t clipId, ClipSnapshot& out) const
{
    if (trackId < 0 || trackId >= MAX_TRACKS || clipId == 0) {
        return false;
    }

    const juce::ScopedReadLock tracksReadLock(tracksLock_);
    const auto& clips = tracks_[trackId].clips;
    auto it = std::find_if(clips.begin(), clips.end(), [clipId](const TrackState::AudioClip& c) {
        return c.clipId == clipId;
    });
    if (it == clips.end()) {
        return false;
    }

    copyClipToSnapshot(*it, out);
    return true;
}

double OpenTuneAudioProcessor::getClipStartSecondsById(int trackId, uint64_t clipId) const
{
    if (trackId < 0 || trackId >= MAX_TRACKS || clipId == 0) {
        return 0.0;
    }

    const juce::ScopedReadLock tracksReadLock(tracksLock_);
    const auto& clips = tracks_[trackId].clips;
    auto it = std::find_if(clips.begin(), clips.end(), [clipId](const TrackState::AudioClip& c) {
        return c.clipId == clipId;
    });
    return (it != clips.end()) ? it->startSeconds : 0.0;
}

bool OpenTuneAudioProcessor::setClipStartSecondsById(int trackId, uint64_t clipId, double startSeconds)
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
    it->startSeconds = std::max(0.0, startSeconds);
    return true;
}

bool OpenTuneAudioProcessor::deleteClipById(int trackId, uint64_t clipId, ClipSnapshot* deletedOut, int* deletedIndexOut)
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

    const int clipIndex = static_cast<int>(std::distance(clips.begin(), it));
    if (deletedOut != nullptr) {
        copyClipToSnapshot(*it, *deletedOut);
    }
    if (deletedIndexOut != nullptr) {
        *deletedIndexOut = clipIndex;
    }

    clips.erase(clips.begin() + clipIndex);
    if (tracks_[trackId].selectedClipIndex >= static_cast<int>(clips.size())) {
        tracks_[trackId].selectedClipIndex = std::max(0, static_cast<int>(clips.size()) - 1);
    }
    return true;
}

bool OpenTuneAudioProcessor::insertClipSnapshot(int trackId, int insertIndex, const ClipSnapshot& snap, uint64_t forcedClipId)
{
    if (trackId < 0 || trackId >= MAX_TRACKS) {
        AppLogger::log("InsertClip rejected: invalid trackId=" + juce::String(trackId));
        return false;
    }
    if (!snap.audioBuffer || snap.audioBuffer->getNumSamples() <= 0) {
        AppLogger::log("InsertClip rejected: empty audio buffer (zero samples) for clipId="
                       + (forcedClipId != 0 ? juce::String(static_cast<juce::int64>(forcedClipId)) : "new"));
        return false;
    }
    if (snap.audioBuffer->getNumChannels() <= 0) {
        AppLogger::log("InsertClip rejected: invalid channel count=" + juce::String(snap.audioBuffer->getNumChannels()));
        return false;
    }

    const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
    auto& clips = tracks_[trackId].clips;
    insertIndex = juce::jlimit(0, static_cast<int>(clips.size()), insertIndex);

    TrackState::AudioClip clip;
    const uint64_t clipId = (forcedClipId != 0) ? forcedClipId : nextClipId_.fetch_add(1);
    copySnapshotToClip(snap, clip, clipId);
    computeClipSilentGaps(clip);

    clips.insert(clips.begin() + insertIndex, std::move(clip));
    if (tracks_[trackId].selectedClipIndex >= insertIndex) {
        tracks_[trackId].selectedClipIndex += 1;
    }
    return true;
}

bool OpenTuneAudioProcessor::hasTrackAudio(int trackId) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        return !tracks_[trackId].clips.empty();
    }
    return false;
}

bool OpenTuneAudioProcessor::moveClipToTrack(int sourceTrackId, int targetTrackId, uint64_t clipId, double newStartSeconds)
{
    if (sourceTrackId < 0 || sourceTrackId >= MAX_TRACKS || targetTrackId < 0 || targetTrackId >= MAX_TRACKS) {
        return false;
    }
    if (sourceTrackId == targetTrackId) {
        return false;
    }

    const juce::ScopedWriteLock tracksWriteLock(tracksLock_);

    auto& sourceClips = tracks_[sourceTrackId].clips;
    auto& targetClips = tracks_[targetTrackId].clips;
    auto it = std::find_if(sourceClips.begin(), sourceClips.end(), [clipId](const TrackState::AudioClip& c) {
        return c.clipId == clipId;
    });
    if (it == sourceClips.end()) {
        return false;
    }

    TrackState::AudioClip movedClip = std::move(*it);
    movedClip.startSeconds = std::max(0.0, newStartSeconds);
    movedClip.colour = juce::Colour::fromHSV(targetTrackId * 0.3f, 0.6f, 0.8f, 1.0f);
    sourceClips.erase(it);
    targetClips.push_back(std::move(movedClip));
    tracks_[targetTrackId].selectedClipIndex = static_cast<int>(targetClips.size()) - 1;
    return true;
}

std::shared_ptr<PitchCurve> OpenTuneAudioProcessor::getClipPitchCurve(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].pitchCurve;
        }
    }
    return nullptr;
}

void OpenTuneAudioProcessor::setClipPitchCurve(int trackId, int clipIndex, std::shared_ptr<PitchCurve> curve)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            clips[clipIndex].pitchCurve = curve;
            clips[clipIndex].originalF0State =
                (curve && !curve->getSnapshot()->getOriginalF0().empty()) ? OriginalF0State::Ready
                                                                          : OriginalF0State::NotRequested;
        }
    }
}

OriginalF0State OpenTuneAudioProcessor::getClipOriginalF0State(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].originalF0State;
        }
    }
    return OriginalF0State::NotRequested;
}

void OpenTuneAudioProcessor::setClipOriginalF0State(int trackId, int clipIndex, OriginalF0State state)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            clips[clipIndex].originalF0State = state;
        }
    }
}

bool OpenTuneAudioProcessor::setClipOriginalF0StateById(int trackId, uint64_t clipId, OriginalF0State state)
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
    it->originalF0State = state;
    return true;
}

DetectedKey OpenTuneAudioProcessor::getClipDetectedKey(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].detectedKey;
        }
    }
    return DetectedKey{};
}

void OpenTuneAudioProcessor::setClipDetectedKey(int trackId, int clipIndex, const DetectedKey& key)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            clips[clipIndex].detectedKey = key;
        }
    }
}

std::vector<Note> OpenTuneAudioProcessor::getClipNotes(int trackId, int clipIndex) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].notes;
        }
    }
    return {};
}

std::vector<Note>& OpenTuneAudioProcessor::getClipNotesRef(int trackId, int clipIndex)
{
    static std::vector<Note> empty;
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            return clips[clipIndex].notes;
        }
    }
    return empty;
}

int OpenTuneAudioProcessor::getClipIndexById(int trackId, uint64_t clipId) const
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedReadLock tracksReadLock(tracksLock_);
        const auto& clips = tracks_[trackId].clips;
        for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
            if (clips[i].clipId == clipId) {
                return i;
            }
        }
    }
    return -1;
}

void OpenTuneAudioProcessor::setClipNotes(int trackId, int clipIndex, const std::vector<Note>& notes)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            clips[clipIndex].notes = notes;
        }
    }
}

bool OpenTuneAudioProcessor::setClipNotesById(int trackId, uint64_t clipId, const std::vector<Note>& notes)
{
    if (trackId >= 0 && trackId < MAX_TRACKS) {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        auto& clips = tracks_[trackId].clips;
        for (auto& clip : clips) {
            if (clip.clipId == clipId) {
                clip.notes = notes;
                return true;
            }
        }
    }
    return false;
}

SilentGapDetector::DetectionConfig OpenTuneAudioProcessor::getSilentGapDetectionConfig() const
{
    return SilentGapDetector::getConfig();
}

void OpenTuneAudioProcessor::setSilentGapDetectionConfig(const SilentGapDetector::DetectionConfig& config)
{
    SilentGapDetector::setConfig(config);
}

void OpenTuneAudioProcessor::resampleDrySignal(TrackState::AudioClip& clip, double deviceSampleRate)
{
    constexpr double kStoredAudioSampleRate = TimeCoordinate::kRenderSampleRate;

    if (clip.audioBuffer->getNumSamples() <= 0) {
        clip.drySignalBuffer_.setSize(0, 0);
        return;
    }

    if (std::abs(kStoredAudioSampleRate - deviceSampleRate) < 1.0) {
        clip.drySignalBuffer_.makeCopyOf(*clip.audioBuffer);
        return;
    }

    const int numChannels = clip.audioBuffer->getNumChannels();
    const int srcSamples = clip.audioBuffer->getNumSamples();
    const double sourceDurationSeconds = TimeCoordinate::samplesToSeconds(srcSamples, kStoredAudioSampleRate);
    const int newLen =
        juce::jmax(1, static_cast<int>(TimeCoordinate::secondsToSamples(sourceDurationSeconds, deviceSampleRate)));

    clip.drySignalBuffer_.setSize(numChannels, newLen, false, true, true);
    for (int ch = 0; ch < numChannels; ++ch) {
        auto resampled = resamplingManager_->upsampleForHost(
            clip.audioBuffer->getReadPointer(ch),
            srcSamples,
            static_cast<int>(kStoredAudioSampleRate),
            static_cast<int>(deviceSampleRate));
        const int toCopy = juce::jmin(newLen, static_cast<int>(resampled.size()));
        clip.drySignalBuffer_.copyFrom(ch, 0, resampled.data(), toCopy);
    }
}

} // namespace OpenTune
