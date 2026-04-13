#include "ArrangementViewComponent.h"
#include "FrameScheduler.h"
#include "../Utils/ZoomSensitivityConfig.h"
#include "../../Utils/KeyShortcutConfig.h"

#include <algorithm>
#include <cmath>

namespace OpenTune {

void ArrangementViewComponent::timerCallback()
{
    onHeartbeatTick();
}

void ArrangementViewComponent::onHeartbeatTick()
{
    if (!isShowing())
        return;

    const bool playingNow = processor_.isPlaying();

    playheadOverlay_.setPlaying(playingNow);

    bool progressed = false;
    if (inferenceActive_)
    {
        waveformBuildTickCounter_ = (waveformBuildTickCounter_ + 1) % 8;
        if (waveformBuildTickCounter_ == 0)
            progressed = buildWaveformCaches(0.15);
    }
    else if (playingNow)
    {
        waveformBuildTickCounter_ = (waveformBuildTickCounter_ + 1) % 3;
        if (waveformBuildTickCounter_ == 0)
            progressed = buildWaveformCaches(0.25);
    }
    else
    {
        waveformBuildTickCounter_ = 0;
        progressed = buildWaveformCaches(0.75);
    }

    if (!playingNow)
    {
        if (progressed)
            FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Background);
        return;
    }
}

void ArrangementViewComponent::updateAutoScroll()
{
    // Page auto-scroll mode has been removed; playback uses continuous follow logic in onScrollVBlankCallback.
}

void ArrangementViewComponent::onScrollVBlankCallback(double timestampSec)
{
    juce::ignoreUnused(timestampSec);

    if (!isShowing() || !isPlaying_.load(std::memory_order_relaxed))
        return;

    const double playheadTime = readPlayheadTime();
    playheadOverlay_.setPlayheadSeconds(playheadTime);

    const double pixelsPerSecond = 100.0 * zoomLevel_;
    const float playheadAbsX = static_cast<float>(playheadTime * pixelsPerSecond);

    float targetScroll = playheadAbsX + 8.0f - (getWidth() / 2.0f);
    if (targetScroll < 0.0f)
        targetScroll = 0.0f;

    const float diff = targetScroll - smoothScrollCurrent_;
    if (std::abs(diff) < 1.0f)
    {
        smoothScrollCurrent_ = targetScroll;
    }
    else
    {
        smoothScrollCurrent_ += diff * 0.1f;
    }

    const int newScrollInt = static_cast<int>(std::llround(smoothScrollCurrent_));
    if (newScrollInt != scrollOffset_)
    {
        setScrollOffset(newScrollInt);
        notifyVisibleStartTimeChanged();
    }

    playheadOverlay_.setScrollOffset(static_cast<double>(smoothScrollCurrent_));
}

double ArrangementViewComponent::readPlayheadTime() const
{
    if (auto source = positionSource_.lock())
        return source->load(std::memory_order_relaxed);

    return processor_.getPosition();
}

void ArrangementViewComponent::mouseMove(const juce::MouseEvent& e)
{
    lastMousePos_ = e.getPosition();

    if (juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey))
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    if (e.y <= rulerHeight_)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    auto hit = hitTestClip(e.getPosition());
    if (hit.trackId >= 0)
    {
        if (hit.isTopEdge)
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void ArrangementViewComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    lastMousePos_ = e.getPosition();

    if (e.mods.isPopupMenu()) {
        const double splitRefSeconds = xToTime(e.x);
        processor_.setPosition(splitRefSeconds);
        playheadOverlay_.setPlayheadSeconds(splitRefSeconds);
        playheadOverlay_.repaint();
        listeners_.call([splitRefSeconds](Listener& l) { l.playheadPositionChangeRequested(splitRefSeconds); });

        auto hit = hitTestClip(e.getPosition());
        if (hit.trackId >= 0 && hit.clipIndex >= 0) {
            listeners_.call([&](Listener& l) {
                l.arrangementClipContextMenu(hit.trackId, hit.clipIndex, e.getScreenPosition());
            });
        }
        return;
    }

    if (juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey))
    {
        isPanning_ = true;
        lastMousePos_ = e.getPosition();
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    double sr = processor_.getSampleRate();
    if (sr <= 0.0)
        sr = 44100.0;

    double newPosSeconds = xToTime(e.x);
    processor_.setPosition(newPosSeconds);
    playheadOverlay_.setPlayheadSeconds(newPosSeconds);
    playheadOverlay_.repaint();
    listeners_.call([newPosSeconds](Listener& l) { l.playheadPositionChangeRequested(newPosSeconds); });
    repaint();

    if (e.y <= rulerHeight_)
    {
        isDraggingPlayhead_ = true;
        dragStartPos_ = e.getPosition();
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
        return;
    }

    auto hit = hitTestClip(e.getPosition());
    if (hit.trackId < 0)
    {
        if (e.y > rulerHeight_)
        {
            const int tr = getTrackIndexAtPoint(e.getPosition());
            if (tr >= 0)
            {
                processor_.setActiveTrack(tr);
                const int n = processor_.getNumClips(tr);
                int sel = processor_.getSelectedClip(tr);
                if (n <= 0) {
                    sel = -1;
                } else {
                    sel = juce::jlimit(0, n - 1, sel);
                }
                processor_.setSelectedClip(tr, sel);
                selectedTrack_ = tr;
                selectedClip_ = sel;
                if (sel >= 0 && sel < n) {
                    selectedClipId_ = processor_.getClipId(tr, sel);
                } else {
                    selectedClipId_ = 0;
                }
                if (!e.mods.isCtrlDown() && !e.mods.isShiftDown()) {
                    clearClipSelection();
                }
                listeners_.call([this](Listener& l) { l.clipSelectionChanged(selectedTrack_, selectedClip_); });
                repaint();
                return;
            }
        }

        if (!e.mods.isCtrlDown() && !e.mods.isShiftDown())
            clearClipSelection();

        repaint();
        return;
    }

    uint64_t hitClipId = processor_.getClipId(hit.trackId, hit.clipIndex);

    if (e.mods.isCtrlDown() && !e.mods.isShiftDown())
    {
        toggleClipSelection(hit.trackId, hitClipId, hit.clipIndex);
        selectedTrack_ = hit.trackId;
        selectedClip_ = hit.clipIndex;
        selectedClipId_ = hitClipId;
        processor_.setActiveTrack(selectedTrack_);
        processor_.setSelectedClip(selectedTrack_, selectedClip_);
        listeners_.call([this](Listener& l) { l.clipSelectionChanged(selectedTrack_, selectedClip_); });
        repaint();
        return;
    }

    if (e.mods.isShiftDown() && hasShiftAnchor_)
    {
        ClipSelectionKey toKey{ hit.trackId, hitClipId };
        selectClipsInRange(shiftAnchor_, toKey);
        selectedTrack_ = hit.trackId;
        selectedClip_ = hit.clipIndex;
        selectedClipId_ = hitClipId;
        processor_.setActiveTrack(selectedTrack_);
        processor_.setSelectedClip(selectedTrack_, selectedClip_);
        listeners_.call([this](Listener& l) { l.clipSelectionChanged(selectedTrack_, selectedClip_); });
        repaint();
        return;
    }

    if (!e.mods.isCtrlDown() && !e.mods.isShiftDown())
    {
        if (!isClipSelected(hit.trackId, hitClipId))
            clearClipSelection();

        shiftAnchor_ = ClipSelectionKey{ hit.trackId, hitClipId };
        hasShiftAnchor_ = true;
    }

    selectedTrack_ = hit.trackId;
    selectedClip_ = hit.clipIndex;
    selectedClipId_ = hitClipId;

    processor_.setActiveTrack(selectedTrack_);
    processor_.setSelectedClip(selectedTrack_, selectedClip_);

    if (selectedClips_.empty())
    {
        selectedClips_.insert(ClipSelectionKey{ selectedTrack_, selectedClipId_ });
    }
    else if (!isClipSelected(selectedTrack_, selectedClipId_))
    {
        if (!e.mods.isCtrlDown())
        {
            clearClipSelection();
            selectedClips_.insert(ClipSelectionKey{ selectedTrack_, selectedClipId_ });
        }
    }

    listeners_.call([this](Listener& l) { l.clipSelectionChanged(selectedTrack_, selectedClip_); });

    dragStartPos_ = e.getPosition();
    dragStartClipSeconds_ = processor_.getClipStartSeconds(selectedTrack_, selectedClip_);
    dragStartClipGain_ = processor_.getClipGain(selectedTrack_, selectedClip_);
    dragStartClipId_ = selectedClipId_;
    dragStartTrackId_ = selectedTrack_;

    isAdjustingGain_ = hit.isTopEdge;
    isDraggingClip_ = !isAdjustingGain_;

    multiDragStartStates_.clear();
    if (isDraggingClip_ && selectedClips_.size() > 1)
    {
        for (const auto& sel : selectedClips_)
        {
            double startSec = processor_.getClipStartSecondsById(sel.trackId, sel.clipId);
            multiDragStartStates_.push_back({ sel.trackId, sel.clipId, startSec });
        }
    }

    repaint();
}

void ArrangementViewComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (isPanning_)
    {
        auto delta = e.getPosition() - lastMousePos_;

        setScrollOffset(scrollOffset_ - delta.x);

        verticalScrollOffset_ -= delta.y;

        double maxScroll = verticalScrollBar_.getRangeLimit().getEnd() - verticalScrollBar_.getCurrentRangeSize();
        verticalScrollOffset_ = juce::jlimit(0, static_cast<int>(maxScroll), verticalScrollOffset_);
        verticalScrollBar_.setCurrentRangeStart(verticalScrollOffset_);

        listeners_.call([this](Listener& l) { l.verticalScrollChanged(verticalScrollOffset_); });

        lastMousePos_ = e.getPosition();
        notifyVisibleStartTimeChanged();
        repaint();
        return;
    }

    double sr = processor_.getSampleRate();
    if (sr <= 0.0)
        sr = 44100.0;

    if (isDraggingPlayhead_)
    {
        const int timelineRight = juce::jmax(0, getWidth() - kScrollbarBreadth_);
        bool scrolled = false;

        if (e.x < 0)
        {
            setScrollOffset(scrollOffset_ + e.x);
            scrolled = true;
        }
        else if (e.x > timelineRight)
        {
            setScrollOffset(scrollOffset_ + (e.x - timelineRight));
            scrolled = true;
        }

        const int cursorX = juce::jlimit(0, timelineRight, e.x);
        double newPosSeconds = xToTime(cursorX);
        processor_.setPosition(newPosSeconds);
        playheadOverlay_.setPlayheadSeconds(newPosSeconds);
        playheadOverlay_.repaint();
        listeners_.call([newPosSeconds](Listener& l) { l.playheadPositionChangeRequested(newPosSeconds); });
        if (scrolled)
            notifyVisibleStartTimeChanged();
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
        return;
    }

    if (selectedTrack_ < 0 || selectedTrack_ >= OpenTuneAudioProcessor::MAX_TRACKS)
        return;

    int clipIndex = selectedClip_;
    if (selectedClipId_ != 0)
        clipIndex = processor_.findClipIndexById(selectedTrack_, selectedClipId_);

    if (clipIndex < 0 || clipIndex >= processor_.getNumClips(selectedTrack_))
        return;

    selectedClip_ = clipIndex;

    auto delta = e.getPosition() - dragStartPos_;
    if (isDraggingClip_)
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);

        double startT = xToTime(dragStartPos_.x);
        double currentT = xToTime(e.x);
        double deltaSeconds = currentT - startT;

        if (selectedClips_.size() > 1 && !multiDragStartStates_.empty())
        {
            for (const auto& state : multiDragStartStates_)
            {
                double newStart = state.startSeconds + deltaSeconds;
                if (newStart < 0.0)
                    newStart = 0.0;

                processor_.setClipStartSecondsById(state.trackId, state.clipId, newStart);
            }
        }
        else
        {
            if (selectedClipId_ != 0)
                processor_.setClipStartSecondsById(selectedTrack_, selectedClipId_, dragStartClipSeconds_ + deltaSeconds);
            else
                processor_.setClipStartSeconds(selectedTrack_, selectedClip_, dragStartClipSeconds_ + deltaSeconds);
        }

        listeners_.call([this](Listener& l) { l.clipTimingChanged(selectedTrack_, selectedClip_); });

        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
    }
    else if (isAdjustingGain_)
    {
        double factor = std::pow(10.0, (-static_cast<double>(delta.y)) / 200.0);
        processor_.setClipGain(selectedTrack_, selectedClip_, static_cast<float>(dragStartClipGain_ * factor));

        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
    }
}

void ArrangementViewComponent::mouseUp(const juce::MouseEvent& e)
{
    if (isDraggingClip_)
    {
        auto delta = e.getPosition() - dragStartPos_;
        const float dragThreshold = 5.0f;
        bool isDraggedSignificantly = delta.getDistanceFromOrigin() > dragThreshold;

        int adjustedY = e.y + verticalScrollOffset_;
        int mouseTrackId = (adjustedY - rulerHeight_) / processor_.getTrackHeight();
        mouseTrackId = juce::jlimit(0, OpenTuneAudioProcessor::MAX_TRACKS - 1, mouseTrackId);

        if (selectedClips_.size() > 1 && !multiDragStartStates_.empty())
        {
            if (isDraggedSignificantly)
            {
                int firstClipTrackId = multiDragStartStates_.front().trackId;
                bool isCrossTrack = (mouseTrackId != firstClipTrackId);

                if (isCrossTrack)
                {
                    std::vector<DragStartState> sortedStates = multiDragStartStates_;
                    std::sort(sortedStates.begin(), sortedStates.end(), [](const auto& a, const auto& b) {
                        return a.startSeconds > b.startSeconds;
                    });

                    for (const auto& state : sortedStates)
                    {
                        double currentStart = processor_.getClipStartSecondsById(state.trackId, state.clipId);
                        if (processor_.moveClipToTrack(state.trackId, mouseTrackId, state.clipId, currentStart))
                        {
                            processor_.getUndoManager().addAction(std::make_unique<ClipCrossTrackMoveAction>(
                                processor_, state.trackId, mouseTrackId, state.clipId, state.startSeconds, currentStart));
                        }
                    }

                    clearClipSelection();
                    selectedTrack_ = mouseTrackId;
                    selectedClip_ = processor_.getSelectedClip(mouseTrackId);
                    if (selectedClip_ >= 0 && selectedClip_ < processor_.getNumClips(mouseTrackId))
                        selectedClipId_ = processor_.getClipId(mouseTrackId, selectedClip_);

                    processor_.setActiveTrack(selectedTrack_);

                    for (int i = 0; i < processor_.getNumClips(mouseTrackId); ++i)
                    {
                        uint64_t clipId = processor_.getClipId(mouseTrackId, i);
                        for (const auto& state : multiDragStartStates_)
                        {
                            if (state.clipId == clipId)
                            {
                                selectedClips_.insert(ClipSelectionKey{ mouseTrackId, clipId });
                                break;
                            }
                        }
                    }

                    listeners_.call([this](Listener& l) { l.clipSelectionChanged(selectedTrack_, selectedClip_); });
                }
                else
                {
                    for (const auto& state : multiDragStartStates_)
                    {
                        double currentStart = processor_.getClipStartSecondsById(state.trackId, state.clipId);
                        if (std::abs(currentStart - state.startSeconds) > 1e-9)
                        {
                            processor_.getUndoManager().addAction(std::make_unique<ClipMoveAction>(
                                processor_, state.trackId, state.clipId, state.startSeconds, currentStart));
                        }
                    }
                }
            }
        }
        else if (dragStartClipId_ != 0)
        {
            double currentStart = processor_.getClipStartSecondsById(dragStartTrackId_, dragStartClipId_);

            if (isDraggedSignificantly && mouseTrackId != dragStartTrackId_)
            {
                if (processor_.moveClipToTrack(dragStartTrackId_, mouseTrackId, dragStartClipId_, currentStart))
                {
                    processor_.getUndoManager().addAction(std::make_unique<ClipCrossTrackMoveAction>(
                        processor_, dragStartTrackId_, mouseTrackId, dragStartClipId_, dragStartClipSeconds_, currentStart));
                    selectedTrack_ = mouseTrackId;
                    selectedClip_ = processor_.getSelectedClip(mouseTrackId);
                    selectedClipId_ = dragStartClipId_;
                    processor_.setActiveTrack(selectedTrack_);
                    listeners_.call([this](Listener& l) { l.clipSelectionChanged(selectedTrack_, selectedClip_); });
                }
            }
            else if (std::abs(currentStart - dragStartClipSeconds_) > 1e-9)
            {
                processor_.getUndoManager().addAction(std::make_unique<ClipMoveAction>(
                    processor_, selectedTrack_, dragStartClipId_, dragStartClipSeconds_, currentStart));
            }
        }
    }

    if (isAdjustingGain_ && dragStartClipId_ != 0)
    {
        float currentGain = processor_.getClipGain(selectedTrack_, selectedClip_);
        if (std::abs(currentGain - dragStartClipGain_) > 0.001f)
        {
            processor_.getUndoManager().addAction(std::make_unique<ClipGainChangeAction>(
                processor_, selectedTrack_, dragStartClipId_, dragStartClipGain_, currentGain));
        }
    }

    isDraggingClip_ = false;
    isAdjustingGain_ = false;
    isDraggingPlayhead_ = false;
    isPanning_ = false;
    dragStartTrackId_ = -1;
    multiDragStartStates_.clear();
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ArrangementViewComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto hit = hitTestClip(e.getPosition());
    if (hit.trackId >= 0 && hit.clipIndex >= 0)
        listeners_.call([&](Listener& l) { l.clipDoubleClicked(hit.trackId, hit.clipIndex); });
}

void ArrangementViewComponent::applyWheelHorizontalPan(float deltaX, float deltaY)
{
    const auto& settings = ZoomSensitivityConfig::getSettings();
    float scrollDelta = (deltaX != 0.0f ? deltaX : deltaY);
    if (scrollDelta == 0.0f)
        return;

    setScrollOffset(scrollOffset_ - static_cast<int>(scrollDelta * settings.scrollSpeed * 10.0f));
    notifyVisibleStartTimeChanged();
}

void ArrangementViewComponent::applyWheelTrackHeightChange(float deltaY)
{
    if (deltaY == 0.0f)
        return;

    const auto& settings = ZoomSensitivityConfig::getSettings();
    int currentHeight = processor_.getTrackHeight();
    int change = static_cast<int>(deltaY * settings.verticalZoomFactor * 150);
    change = (change == 0) ? ((deltaY > 0) ? 10 : -10) : change;

    int newHeight = juce::jlimit(70, 300, currentHeight + change);

    if (newHeight != currentHeight)
    {
        processor_.setTrackHeight(newHeight);
        updateScrollBars();
        listeners_.call([newHeight](Listener& l) { l.trackHeightChanged(newHeight); });
        repaint();
    }
}

void ArrangementViewComponent::applyWheelTimelineZoom(float deltaY, int anchorContentX)
{
    if (deltaY == 0.0f)
        return;

    const auto& settings = ZoomSensitivityConfig::getSettings();
    double zoomFactor = 1.0 + deltaY * settings.horizontalZoomFactor * 1.7;
    zoomFactor = juce::jlimit(0.5, 1.5, zoomFactor);
    double newZoom = juce::jlimit(getMinimumHorizontalZoomLevel(), 10.0, zoomLevel_ * zoomFactor);

    if (std::abs(newZoom - zoomLevel_) <= 0.001)
        return;

    double timeAtMouse = xToTime(anchorContentX);

    setZoomLevel(newZoom);
    userHasManuallyZoomed_ = true;

    double pps = 100.0 * newZoom;
    int newOffset = static_cast<int>(timeAtMouse * pps) + 8 - anchorContentX;
    setScrollOffset(newOffset);

    if (onUserTimelineZoomChanged) {
        onUserTimelineZoomChanged(newZoom);
    }

    notifyVisibleStartTimeChanged();

    FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Normal);
}

void ArrangementViewComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto& settings = ZoomSensitivityConfig::getSettings();
    const bool ctrl = e.mods.isCtrlDown() || e.mods.isCommandDown();
    const bool alt = e.mods.isAltDown();
    const bool shift = e.mods.isShiftDown();

    if (alt && ctrl)
    {
        if (wheel.deltaY != 0.0f)
        {
            applyWheelTimelineZoom(wheel.deltaY, e.x);
            applyWheelTrackHeightChange(wheel.deltaY);
        }
        return;
    }

    if (shift)
    {
        if (wheel.deltaX != 0.0f || wheel.deltaY != 0.0f)
            applyWheelHorizontalPan(wheel.deltaX, wheel.deltaY);
        return;
    }

    if (alt)
    {
        if (wheel.deltaY != 0.0f)
            applyWheelTrackHeightChange(wheel.deltaY);
        return;
    }

    if (ctrl)
    {
        if (wheel.deltaY != 0.0f)
            applyWheelTimelineZoom(wheel.deltaY, e.x);
        return;
    }

    if (wheel.deltaY != 0.0f)
    {
        int scrollDelta = static_cast<int>(wheel.deltaY * settings.scrollSpeed);
        int newOffset = verticalScrollOffset_ - scrollDelta;
        setVerticalScrollOffset(newOffset);
        listeners_.call([this](Listener& l) { l.verticalScrollChanged(verticalScrollOffset_); });
    }

    if (wheel.deltaX != 0.0f)
    {
        setScrollOffset(scrollOffset_ - static_cast<int>(wheel.deltaX * settings.scrollSpeed * 5.0f));
        notifyVisibleStartTimeChanged();
    }
}

bool ArrangementViewComponent::keyPressed(const juce::KeyPress& key)
{
    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::PlayPause, key))
    {
        processor_.setPlaying(!processor_.isPlaying());
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::Cut, key))
    {
        cutSelectedClips();
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::Copy, key))
    {
        copySelectedClips();
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::Paste, key))
    {
        pasteClips();
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::SelectAll, key))
    {
        selectAllClipsInTrack(selectedTrack_);
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::Delete, key))
    {
        deleteSelectedClips();
        return true;
    }

    if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
    {
        if (selectedTrack_ < 0 || selectedTrack_ >= OpenTuneAudioProcessor::MAX_TRACKS)
            return true;

        if (selectedClip_ < 0 || selectedClip_ >= processor_.getNumClips(selectedTrack_))
            return true;

        int originalClipIndex = selectedClip_;
        uint64_t originalClipId = processor_.getClipId(selectedTrack_, selectedClip_);
        double originalDuration = 0.0;
        std::shared_ptr<const juce::AudioBuffer<float>> clipBuffer =
            processor_.getClipAudioBuffer(selectedTrack_, selectedClip_);
        if (clipBuffer)
        {
            originalDuration = static_cast<double>(clipBuffer->getNumSamples())
                / OpenTuneAudioProcessor::getStoredAudioSampleRate();
        }

        double splitSeconds = processor_.getPosition();
        const auto selectedClipBounds = getClipBounds(selectedTrack_, selectedClip_);
        if (!selectedClipBounds.isEmpty() && selectedClipBounds.contains(lastMousePos_))
        {
            splitSeconds = xToTime(lastMousePos_.x);
        }

        const double clipStart = processor_.getClipStartSeconds(selectedTrack_, selectedClip_);
        const double clipEnd = clipStart + originalDuration;
        splitSeconds = juce::jlimit(clipStart, clipEnd, splitSeconds);

        if (processor_.splitClipAtSeconds(selectedTrack_, selectedClip_, splitSeconds))
        {
            int newClipIndex = processor_.getSelectedClip(selectedTrack_);
            uint64_t newClipId = 0;
            if (newClipIndex >= 0 && newClipIndex < processor_.getNumClips(selectedTrack_))
                newClipId = processor_.getClipId(selectedTrack_, newClipIndex);

            ClipSplitAction::SplitResult result;
            result.newClipId = newClipId;
            result.splitSeconds = splitSeconds;
            result.originalClipDuration = originalDuration;

            processor_.getUndoManager().addAction(std::make_unique<ClipSplitAction>(
                processor_, selectedTrack_, originalClipId, result, originalClipIndex, newClipIndex));

            selectedClip_ = newClipIndex;
            if (selectedClip_ >= 0 && selectedClip_ < processor_.getNumClips(selectedTrack_))
                selectedClipId_ = processor_.getClipId(selectedTrack_, selectedClip_);
            else
                selectedClipId_ = 0;

            listeners_.call([this](Listener& l) { l.clipTimingChanged(selectedTrack_, selectedClip_); });
            repaint();
        }
        return true;
    }

    return false;
}

} // namespace OpenTune
