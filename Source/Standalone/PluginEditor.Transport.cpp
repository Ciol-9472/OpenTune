#include "PluginEditor.h"
#include "Utils/UndoAction.h"
#include <algorithm>
#include <cmath>

namespace OpenTune {

// ============================================================================
// TransportBarComponent::Listener Implementation
// ============================================================================

void OpenTuneAudioProcessorEditor::playRequested()
{
    processorRef_.setPlaying(true);
    transportBar_.setPlaying(true);
    pianoRoll_.setIsPlaying(true);  // Notify PianoRoll for auto-scroll
    arrangementView_.setIsPlaying(true);  // Notify ArrangementView for overlay sync
}

void OpenTuneAudioProcessorEditor::pauseRequested()
{
    processorRef_.setPlaying(false);
    transportBar_.setPlaying(false);
    pianoRoll_.setIsPlaying(false);  // Notify PianoRoll to stop auto-scroll
    arrangementView_.setIsPlaying(false);  // Notify ArrangementView to stop overlay updates
}

void OpenTuneAudioProcessorEditor::stopRequested()
{
    processorRef_.setPlaying(false);
    processorRef_.setPosition(0);
    transportBar_.setPlaying(false);
    pianoRoll_.setIsPlaying(false);  // Notify PianoRoll to stop auto-scroll
    arrangementView_.setIsPlaying(false);  // Notify ArrangementView to stop overlay updates
    playheadPositionChangeRequested(0.0);
}

void OpenTuneAudioProcessorEditor::loopToggled(bool enabled)
{
    processorRef_.setLoopEnabled(enabled);
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::bypassToggled(bool enabled)
{
    processorRef_.setBypassEnabled(enabled);
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::bpmChanged(double newBpm)
{
    processorRef_.setBpm(newBpm);
    pianoRoll_.setBpm(newBpm);  // Update piano roll to redraw time grid
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::scaleChanged(int rootNote, int scaleType)
{
    if (suppressScaleChangedCallback_) {
        return;
    }

    const int activeTrack = processorRef_.getActiveTrackId();
    const int activeClip = processorRef_.getSelectedClip(activeTrack);
    const uint64_t activeClipId = processorRef_.getClipId(activeTrack, activeClip);

    const int newRoot = juce::jlimit(0, 11, rootNote);
    const int newScaleType = juce::jlimit(1, 8, scaleType);

    const DetectedKey oldResolved = resolveScaleForClip(activeTrack, activeClip, nullptr);
    const int oldRootNote = static_cast<int>(oldResolved.root);
    const int oldScaleType = scaleToUiScaleType(oldResolved.scale);

    if (oldRootNote == newRoot && oldScaleType == newScaleType) {
        applyScaleToUi(newRoot, newScaleType);
        return;
    }

    const DetectedKey newKey = makeDetectedKeyFromUi(newRoot, newScaleType, 1.0f);

    if (activeClip >= 0) {
        processorRef_.setClipDetectedKey(activeTrack, activeClip, newKey);
    }
    applyScaleToUi(newRoot, newScaleType);

    DBG("ScaleSyncTrace: source=manual trackId=" + juce::String(activeTrack)
        + " clipIndex=" + juce::String(activeClip)
        + " clipId=" + juce::String(static_cast<juce::int64>(activeClipId))
        + " root=" + juce::String(newRoot)
        + " scale=" + juce::String(newScaleType));

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
    processorRef_.getUndoManager().addAction(
        std::make_unique<ClipScaleKeyChangeAction>(
            activeTrack,
            activeClipId,
            oldRootNote,
            oldScaleType,
            newRoot,
            newScaleType,
            [safeThis](int trackId, uint64_t clipId, int r, int s) {
                if (safeThis == nullptr) return;

                const int resolvedClipIndex = (clipId != 0)
                    ? safeThis->processorRef_.findClipIndexById(trackId, clipId)
                    : safeThis->processorRef_.getSelectedClip(trackId);

                const DetectedKey dk = OpenTuneAudioProcessorEditor::makeDetectedKeyFromUi(r, s, 1.0f);
                if (resolvedClipIndex >= 0) {
                    safeThis->processorRef_.setClipDetectedKey(trackId, resolvedClipIndex, dk);
                }

                const int activeTrackNow = safeThis->processorRef_.getActiveTrackId();
                const int activeClipNow = safeThis->processorRef_.getSelectedClip(activeTrackNow);
                const uint64_t activeClipIdNow = safeThis->processorRef_.getClipId(activeTrackNow, activeClipNow);
                const bool sameVisibleClip = (clipId != 0)
                    ? (activeTrackNow == trackId && activeClipIdNow == clipId)
                    : (activeTrackNow == trackId && activeClipNow == resolvedClipIndex);

                if (sameVisibleClip) {
                    safeThis->applyScaleToUi(r, s);
                }

                DBG("UndoTrace: ClipScaleKeyChangeAction trackId=" + juce::String(trackId)
                    + " clipId=" + juce::String(static_cast<juce::int64>(clipId))
                    + " root=" + juce::String(r)
                    + " scale=" + juce::String(s));
            })
    );
}

// ============================================================================
// TrackPanelComponent::Listener Implementation
// ============================================================================

void OpenTuneAudioProcessorEditor::trackSelected(int trackId)
{
    processorRef_.setActiveTrack(trackId);
    processorRef_.setSelectedClip(trackId, processorRef_.getSelectedClip(trackId));
    int clipIndex = processorRef_.getSelectedClip(trackId);
    syncPianoRollFromClipSelection(trackId, clipIndex);
    
    pianoRoll_.repaint();
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::trackMuteToggled(int trackId, bool muted)
{
    bool oldMuted = processorRef_.isTrackMuted(trackId);
    processorRef_.setTrackMuted(trackId, muted);
    
    // 鍒涘缓 Undo Action
    if (oldMuted != muted) {
        juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
        processorRef_.getUndoManager().addAction(
            std::make_unique<TrackMuteAction>(
                processorRef_, trackId, oldMuted, muted,
                [safeThis](int tid, bool m) {
                    if (safeThis) safeThis->trackPanel_.setTrackMuted(tid, m);
                }
            )
        );
    }
}

void OpenTuneAudioProcessorEditor::trackSoloToggled(int trackId, bool solo)
{
    bool oldSolo = processorRef_.isTrackSolo(trackId);
    processorRef_.setTrackSolo(trackId, solo);
    
    // 鍒涘缓 Undo Action
    if (oldSolo != solo) {
        juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
        processorRef_.getUndoManager().addAction(
            std::make_unique<TrackSoloAction>(
                processorRef_, trackId, oldSolo, solo,
                [safeThis](int tid, bool s) {
                    if (safeThis) safeThis->trackPanel_.setTrackSolo(tid, s);
                }
            )
        );
    }
}

void OpenTuneAudioProcessorEditor::trackVolumeChanged(int trackId, float volume)
{
    if (trackId < 0 || trackId >= OpenTuneAudioProcessor::MAX_TRACKS) return;
    
    float oldVolume = lastTrackVolumes_[static_cast<size_t>(trackId)];
    processorRef_.setTrackVolume(trackId, volume);
    lastTrackVolumes_[static_cast<size_t>(trackId)] = volume;
    
    // 鍙湁鍊煎彉鍖栬秴杩囬槇鍊兼椂鎵嶅垱寤?Undo Action锛堥伩鍏嶆嫋鍔ㄦ椂浜х敓杩囧 Action锛?
    // 浣跨敤 0.01 浣滀负闃堝€硷紝绾︾瓑浜?0.1dB
    if (std::abs(oldVolume - volume) > 0.01f) {
        juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
        processorRef_.getUndoManager().addAction(
            std::make_unique<TrackVolumeAction>(
                processorRef_, trackId, oldVolume, volume,
                [safeThis](int tid, float v) {
                    if (safeThis) {
                        safeThis->trackPanel_.setTrackVolume(tid, v);
                        safeThis->lastTrackVolumes_[static_cast<size_t>(tid)] = v;
                    }
                }
            )
        );
    }
}

// Y杞寸缉鏀惧悓姝ワ細褰揟rackPanel鎴朅rrangementView閫氳繃Ctrl+婊氳疆缂╂斁鏃讹紝鍚屾鍙︿竴涓粍浠?
void OpenTuneAudioProcessorEditor::arrangementHorizontalPanWheel(float deltaX, float deltaY)
{
    arrangementView_.applyWheelHorizontalPan(deltaX, deltaY);
}

void OpenTuneAudioProcessorEditor::arrangementTrackHeightWheel(float deltaY)
{
    arrangementView_.applyWheelTrackHeightChange(deltaY);
}

void OpenTuneAudioProcessorEditor::arrangementAltCtrlWheel(float deltaY)
{
    arrangementView_.applyWheelTrackHeightChange(deltaY);
    const int anchorX = juce::jmax(0, arrangementView_.getWidth() / 2);
    arrangementView_.applyWheelTimelineZoom(deltaY, anchorX);
}

void OpenTuneAudioProcessorEditor::trackInsertRequested(int trackId)
{
    const int visibleBefore = trackPanel_.getVisibleTrackCount();
    if (!processorRef_.insertEmptyTrackAt(trackId))
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Insert Track",
            "Cannot insert a new track because the last track already contains clips.");
        return;
    }
    const int visibleAfter = juce::jmin(OpenTuneAudioProcessor::MAX_TRACKS, visibleBefore + 1);
    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
    processorRef_.getUndoManager().addAction(
        std::make_unique<TrackInsertDeleteAction>(
            processorRef_,
            trackId,
            TrackInsertDeleteAction::Type::Insert,
            visibleBefore,
            visibleAfter,
            [safeThis](int count) {
                if (safeThis != nullptr) {
                    safeThis->trackPanel_.setVisibleTrackCount(count);
                }
            }));

    trackPanel_.setVisibleTrackCount(visibleAfter);
    trackPanel_.setActiveTrack(processorRef_.getActiveTrackId());
    trackPanel_.syncTrackNamesFromProcessor(processorRef_);
    for (int i = 0; i < OpenTuneAudioProcessor::MAX_TRACKS; ++i)
    {
        trackPanel_.setTrackMuted(i, processorRef_.isTrackMuted(i));
        trackPanel_.setTrackSolo(i, processorRef_.isTrackSolo(i));
        trackPanel_.setTrackVolume(i, processorRef_.getTrackVolume(i));
        lastTrackVolumes_[static_cast<size_t>(i)] = processorRef_.getTrackVolume(i);
    }
    arrangementView_.repaint();
    trackPanel_.repaint();
    const int activeTrack = processorRef_.getActiveTrackId();
    syncPianoRollFromClipSelection(activeTrack, processorRef_.getSelectedClip(activeTrack));
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::trackDeleteRequested(int trackId)
{
    if (trackPanel_.getVisibleTrackCount() <= 1)
        return;

    const int visibleBefore = trackPanel_.getVisibleTrackCount();
    if (!processorRef_.deleteTrackAt(trackId))
        return;
    const int visibleAfter = juce::jmax(1, visibleBefore - 1);
    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
    processorRef_.getUndoManager().addAction(
        std::make_unique<TrackInsertDeleteAction>(
            processorRef_,
            trackId,
            TrackInsertDeleteAction::Type::Delete,
            visibleBefore,
            visibleAfter,
            [safeThis](int count) {
                if (safeThis != nullptr) {
                    safeThis->trackPanel_.setVisibleTrackCount(count);
                }
            }));

    trackPanel_.setVisibleTrackCount(visibleAfter);
    trackPanel_.setActiveTrack(processorRef_.getActiveTrackId());
    trackPanel_.syncTrackNamesFromProcessor(processorRef_);
    for (int i = 0; i < OpenTuneAudioProcessor::MAX_TRACKS; ++i)
    {
        trackPanel_.setTrackMuted(i, processorRef_.isTrackMuted(i));
        trackPanel_.setTrackSolo(i, processorRef_.isTrackSolo(i));
        trackPanel_.setTrackVolume(i, processorRef_.getTrackVolume(i));
        lastTrackVolumes_[static_cast<size_t>(i)] = processorRef_.getTrackVolume(i);
    }
    arrangementView_.repaint();
    trackPanel_.repaint();
    const int activeTrack = processorRef_.getActiveTrackId();
    syncPianoRollFromClipSelection(activeTrack, processorRef_.getSelectedClip(activeTrack));
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::trackHeightChanged(int newHeight)
{
    // 鏇存柊processor涓殑杞ㄩ亾楂樺害
    processorRef_.setTrackHeight(newHeight);
    
    // 鍚屾TrackPanel锛堝鏋滀笉鏄敱瀹冭Е鍙戠殑锛?
    if (trackPanel_.getTrackHeight() != newHeight)
    {
        trackPanel_.setTrackHeight(newHeight);
    }
    
    // 鍒锋柊ArrangementView
    arrangementView_.repaint();
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::clipSelectionChanged(int trackId, int clipIndex)
{
    processorRef_.setActiveTrack(trackId);
    processorRef_.setSelectedClip(trackId, clipIndex);
    trackPanel_.setActiveTrack(trackId);

    if (clipIndex >= 0) {
        syncPianoRollFromClipSelection(trackId, clipIndex);
    }
}

void OpenTuneAudioProcessorEditor::timeDisplayModeChanged(TransportBarComponent::TimeDisplayMode mode)
{
    const bool useSeconds = (mode == TransportBarComponent::TimeDisplayMode::Time);
    arrangementView_.setTimeUnitSeconds(useSeconds);
    pianoRoll_.setTimeUnit(useSeconds
        ? PianoRollComponent::TimeUnit::Seconds
        : PianoRollComponent::TimeUnit::Bars);
}

bool OpenTuneAudioProcessorEditor::requestNextPendingOriginalF0ExtractionOnTrack(int trackId)
{
    if (trackId < 0 || trackId >= OpenTuneAudioProcessor::MAX_TRACKS)
        return false;

    struct PendingClip
    {
        int index = -1;
        double startSeconds = 0.0;
    };

    bool hasLiveActiveRequest = false;
    const int numClips = processorRef_.getNumClips(trackId);
    for (int idx = 0; idx < numClips; ++idx)
    {
        const uint64_t clipId = processorRef_.getClipId(trackId, idx);
        const uint64_t requestKey = F0ExtractionService::makeRequestKey(clipId, trackId, idx);
        if (!f0ExtractionService_.isActive(requestKey))
            continue;

        const auto state = processorRef_.getClipOriginalF0State(trackId, idx);
        if (state == OriginalF0State::NotRequested)
        {
            // This active request belongs to stale pre-split audio range; cancel and let fresh request be re-scheduled.
            f0ExtractionService_.cancel(requestKey);
            continue;
        }

        hasLiveActiveRequest = true;
    }

    if (hasLiveActiveRequest)
        return false;

    std::vector<PendingClip> pending;
    for (int idx = 0; idx < numClips; ++idx)
    {
        const auto state = processorRef_.getClipOriginalF0State(trackId, idx);
        if (state != OriginalF0State::NotRequested)
            continue;

        auto curve = processorRef_.getClipPitchCurve(trackId, idx);
        if (curve != nullptr)
        {
            auto snap = curve->getSnapshot();
            if (snap != nullptr && !snap->getOriginalF0().empty())
                continue;
        }

        pending.push_back({ idx, processorRef_.getClipStartSeconds(trackId, idx) });
    }

    if (pending.empty())
        return false;

    std::sort(pending.begin(), pending.end(), [](const PendingClip& a, const PendingClip& b) {
        if (std::abs(a.startSeconds - b.startSeconds) <= 1.0e-6)
            return a.index < b.index;
        return a.startSeconds < b.startSeconds;
    });

    requestOriginalF0ExtractionForImport(trackId, pending.front().index);
    return true;
}

void OpenTuneAudioProcessorEditor::clipTimingChanged(int trackId, int clipIndex)
{
    juce::ignoreUnused(clipIndex);

    // Always refresh PianoRoll from processor when the active track matches. Clip splits replace
    // audio buffers and PitchCurve shared_ptrs; a partial update (time offset only) leaves
    // currentCurve_ pointing at the wrong clip so HandDraw / LineAnchor see null or mismatched F0.
    if (processorRef_.getActiveTrackId() == trackId)
    {
        const int selectedClip = processorRef_.getSelectedClip(trackId);
        if (selectedClip >= 0) {
            syncPianoRollFromClipSelection(trackId, selectedClip);
        }
    }

    // Split/import aftermath: always schedule pending clips from left to right.
    requestNextPendingOriginalF0ExtractionOnTrack(trackId);

    arrangementView_.reconcileHorizontalScrollAfterEdit();
}

// Y杞存粴鍔ㄥ悓姝ワ細ArrangementView鎴朤rackPanel婊氬姩鏃堕€氱煡鍙︿竴涓粍浠惰窡闅?
void OpenTuneAudioProcessorEditor::verticalScrollChanged(int newOffset)
{
    // 鍚屾TrackPanel
    trackPanel_.setVerticalScrollOffset(newOffset);
    // 鍚屾ArrangementView
    arrangementView_.setVerticalScrollOffset(newOffset);
}

void OpenTuneAudioProcessorEditor::arrangementClipNameEdited(int trackId, int clipIndex)
{
    juce::ignoreUnused(trackId, clipIndex);
    markSessionNeedsSave();
}

} // namespace OpenTune
