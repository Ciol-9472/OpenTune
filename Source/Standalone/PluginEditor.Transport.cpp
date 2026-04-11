#include "PluginEditor.h"
#include "Utils/UndoAction.h"
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
}

void OpenTuneAudioProcessorEditor::loopToggled(bool enabled)
{
    processorRef_.setLoopEnabled(enabled);
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

void OpenTuneAudioProcessorEditor::viewToggled(bool workspaceView)
{
    isWorkspaceView_ = workspaceView;
    arrangementView_.setVisible(isWorkspaceView_);
    pianoRoll_.setVisible(!isWorkspaceView_);
    
    // Explicitly grab focus for the active view to ensure keyboard shortcuts work immediately
    if (isWorkspaceView_)
        arrangementView_.grabKeyboardFocus();
    else
        pianoRoll_.grabKeyboardFocus();

    resized();
    repaint();

    // 寤惰繜璋冪敤鑷姩缂╂斁锛岀‘淇漴esized()瀹屾垚鍚庢墽琛?
    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
    juce::Timer::callAfterDelay(50, [safeThis, workspaceView]() {
        if (safeThis == nullptr) return;

        if (workspaceView) {
            // 鍒囨崲鍒癆rrangementView
            if (!safeThis->arrangementView_.hasUserManuallyZoomed()) {
                safeThis->arrangementView_.fitToContent();
            }
        } else {
            // 鍒囨崲鍒癙ianoRoll
            if (!safeThis->pianoRoll_.hasUserManuallyZoomed()) {
                safeThis->pianoRoll_.fitToScreen();
            }
        }
    });

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

    syncPianoRollFromClipSelection(trackId, clipIndex);
    markSessionNeedsSave();

    // 濡傛灉褰撳墠鍦≒ianoRoll瑙嗗浘锛屼笖鐢ㄦ埛娌℃湁鎵嬪姩缂╂斁杩囷紝鑷姩閫傞厤鏂癱lip
    if (!isWorkspaceView_) {
        juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
        juce::Timer::callAfterDelay(100, [safeThis]() {
            if (safeThis != nullptr && !safeThis->pianoRoll_.hasUserManuallyZoomed()) {
                safeThis->pianoRoll_.fitToScreen();
            }
        });
    }
}

void OpenTuneAudioProcessorEditor::clipTimingChanged(int trackId, int clipIndex)
{
    // Update PianoRoll if this clip is active
    if (processorRef_.getActiveTrackId() == trackId && processorRef_.getSelectedClip(trackId) == clipIndex)
    {
        pianoRoll_.setTrackTimeOffset(processorRef_.getClipStartSeconds(trackId, clipIndex));
    }
}

// Y杞存粴鍔ㄥ悓姝ワ細ArrangementView鎴朤rackPanel婊氬姩鏃堕€氱煡鍙︿竴涓粍浠惰窡闅?
void OpenTuneAudioProcessorEditor::verticalScrollChanged(int newOffset)
{
    // 鍚屾TrackPanel
    trackPanel_.setVerticalScrollOffset(newOffset);
    // 鍚屾ArrangementView
    arrangementView_.setVerticalScrollOffset(newOffset);
}

void OpenTuneAudioProcessorEditor::clipDoubleClicked(int trackId, int clipIndex)
{
    // 1. Switch to Piano Roll View
    if (isWorkspaceView_)
    {
        transportBar_.setWorkspaceView(false);
        viewToggled(false); // 浼氳Е鍙戣嚜鍔ㄧ缉鏀?
    }
    else
    {
        // 濡傛灉宸茬粡鍦≒ianoRoll瑙嗗浘锛屼篃闇€瑕佽皟鐢╢itToScreen
        juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
        juce::Timer::callAfterDelay(50, [safeThis]() {
            if (safeThis != nullptr && !safeThis->pianoRoll_.hasUserManuallyZoomed()) {
                safeThis->pianoRoll_.fitToScreen();
            }
        });
    }

    // 2. Select the clip
    clipSelectionChanged(trackId, clipIndex);

}

} // namespace OpenTune
