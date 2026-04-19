#include "../../PluginProcessor.h"
#include "PianoRollComponent.h"
#include "TopBarComponent.h"
#include "../../Utils/KeyShortcutConfig.h"
#include "../Utils/ZoomSensitivityConfig.h"
#include "FrameScheduler.h"
#include "UiText.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kRightToolMenuLongPressMs = 200;

} // namespace

namespace OpenTune {

void PianoRollComponent::requestInteractiveRepaint()
{
    requestInteractiveRepaint(getLocalBounds());
}

void PianoRollComponent::requestInteractiveRepaint(const juce::Rectangle<int>& dirtyArea)
{
    auto clippedDirtyArea = dirtyArea;
    if (clippedDirtyArea.isEmpty())
        clippedDirtyArea = getLocalBounds();
    else
        clippedDirtyArea = clippedDirtyArea.getIntersection(getLocalBounds());

    if (clippedDirtyArea.isEmpty())
        return;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    constexpr double minIntervalMs = 1000.0 / 60.0;
    if ((nowMs - lastInteractiveRepaintMs_) >= minIntervalMs)
    {
        lastInteractiveRepaintMs_ = nowMs;
        pendingInteractiveRepaint_ = false;
        hasPendingInteractiveDirtyArea_ = false;
        pendingInteractiveDirtyArea_ = {};
        FrameScheduler::instance().requestInvalidate(*this, clippedDirtyArea, FrameScheduler::Priority::Interactive);
        return;
    }

    if (hasPendingInteractiveDirtyArea_)
        pendingInteractiveDirtyArea_ = pendingInteractiveDirtyArea_.getUnion(clippedDirtyArea);
    else
    {
        pendingInteractiveDirtyArea_ = clippedDirtyArea;
        hasPendingInteractiveDirtyArea_ = true;
    }

    pendingInteractiveRepaint_ = true;
}

void PianoRollComponent::setScrollOffset(int offset)
{
    const int visibleWidth = getTimelineVisibleWidth();
    const int totalContentWidth = static_cast<int>(std::ceil(getTimelineSpanSeconds() * 100.0 * zoomLevel_));
    const int maxScrollOffset = juce::jmax(0, totalContentWidth - visibleWidth);
    const int newOffset = juce::jlimit(0, maxScrollOffset, offset);
    if (newOffset == scrollOffset_)
        return;

    const int oldOffset = scrollOffset_;
    scrollOffset_ = newOffset;
    timeConverter_.setScrollOffset(scrollOffset_);
    horizontalScrollBar_.setCurrentRangeStart(scrollOffset_);
    playheadOverlay_.setScrollOffset(static_cast<double>(scrollOffset_));

    const int scrollDelta = scrollOffset_ - oldOffset;
    const int contentWidth = getWidth() - pianoKeyWidth_;
    if (contentWidth > 0 && std::abs(scrollDelta) < contentWidth)
    {
        juce::Rectangle<int> dirtyArea(pianoKeyWidth_, 0, contentWidth, getHeight());
        FrameScheduler::instance().requestInvalidate(*this, dirtyArea, FrameScheduler::Priority::Interactive);
    }
    else
    {
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
    }
}

double PianoRollComponent::getVisibleStartTimeSeconds() const
{
    return static_cast<double>(scrollOffset_) / (100.0 * zoomLevel_) + trackOffsetSeconds_ - alignmentOffsetSeconds_;
}

double PianoRollComponent::getVisibleDurationSeconds() const
{
    const int visibleWidth = getTimelineVisibleWidth();
    const double pixelsPerSecond = juce::jmax(1.0e-6, 100.0 * zoomLevel_);
    return static_cast<double>(visibleWidth) / pixelsPerSecond;
}

void PianoRollComponent::setVisibleStartTimeSeconds(double timeSeconds)
{
    const double relativeStart = juce::jmax(0.0, timeSeconds - trackOffsetSeconds_ + alignmentOffsetSeconds_);
    const int newOffset = static_cast<int>(std::llround(relativeStart * 100.0 * zoomLevel_));
    setScrollOffset(newOffset);
}

void PianoRollComponent::syncPlayheadPosition(double timeSeconds)
{
    playheadOverlay_.setPlayheadSeconds(timeSeconds);

    if (!isPlaying_.load(std::memory_order_relaxed))
    {
        playheadOverlay_.repaint();
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
    }
}

double PianoRollComponent::readPlayheadTime() const
{
    if (auto source = positionSource_.lock())
        return source->load(std::memory_order_relaxed);

    return 0.0;
}

void PianoRollComponent::updateAutoScroll()
{
    // Playback horizontal follow: page-flip at viewport edges (see onScrollVBlankCallback).
}

void PianoRollComponent::timerCallback()
{
    if (isShowing() && isRendering_)
        repaint();
}

void PianoRollComponent::onHeartbeatTick()
{
    consumeCompletedCorrectionResults();

    if (!isShowing())
        return;

    timeConverter_.setContext(bpm_, timeSigNum_, timeSigDenom_);
    timeConverter_.setZoom(zoomLevel_);
    timeConverter_.setScrollOffset(scrollOffset_);

    if (pendingInteractiveRepaint_)
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        constexpr double minIntervalMs = 1000.0 / 60.0;
        if (!isPlaying_.load(std::memory_order_relaxed) || (nowMs - lastInteractiveRepaintMs_) >= minIntervalMs)
        {
            lastInteractiveRepaintMs_ = nowMs;
            pendingInteractiveRepaint_ = false;
            if (hasPendingInteractiveDirtyArea_)
            {
                auto dirtyArea = pendingInteractiveDirtyArea_.getIntersection(getLocalBounds());
                hasPendingInteractiveDirtyArea_ = false;
                pendingInteractiveDirtyArea_ = {};
                if (!dirtyArea.isEmpty())
                    FrameScheduler::instance().requestInvalidate(*this, dirtyArea, FrameScheduler::Priority::Interactive);
                else
                    FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
            }
            else
            {
                FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
            }
        }
    }

    if (isRendering_)
        repaint();

    if (!waveformMipmap_.isComplete() && showWaveform_)
    {
        if (inferenceActive_)
        {
            waveformBuildTickCounter_ = (waveformBuildTickCounter_ + 1) % 8;
            if (waveformBuildTickCounter_ == 0 && waveformMipmap_.buildIncremental(1.0))
                repaint();
        }
        else
        {
            waveformBuildTickCounter_ = 0;
            if (waveformMipmap_.buildIncremental(5.0))
                repaint();
        }
    }
}

void PianoRollComponent::onScrollVBlankCallback(double timestampSec)
{
    juce::ignoreUnused(timestampSec);

    if (!isShowing() || !isPlaying_.load(std::memory_order_relaxed))
        return;

    const double playheadTime = readPlayheadTime();
    playheadOverlay_.setPlayheadSeconds(playheadTime);

    const double relativePlayheadTime = juce::jmax(0.0, playheadTime - trackOffsetSeconds_ + alignmentOffsetSeconds_);
    const double pixelsPerSecond = 100.0 * zoomLevel_;
    const float playheadAbsX = static_cast<float>(relativePlayheadTime * pixelsPerSecond);

    const int visibleWidth = getTimelineVisibleWidth();
    const float vw = static_cast<float>(juce::jmax(1, visibleWidth));
    const float pk = static_cast<float>(pianoKeyWidth_);
    const float margin = juce::jmax(48.0f, vw * 0.12f);
    const float playheadX = playheadAbsX - smoothScrollCurrent_ + pk;

    const int totalContentWidth = static_cast<int>(std::ceil(getTimelineSpanSeconds() * 100.0 * zoomLevel_));
    const int maxScroll = juce::jmax(0, totalContentWidth - visibleWidth);

    const float viewCenter = pk + (getWidth() - pianoKeyWidth_) / 2.0f;
    float centeringScroll = playheadAbsX + pianoKeyWidth_ - viewCenter;
    if (centeringScroll < 0.0f)
        centeringScroll = 0.0f;

    const bool isEditingNow = interactionState_.drawing.isDrawingF0
        || interactionState_.drawing.isDrawingNote
        || interactionState_.noteDrag.isDraggingNotes
        || interactionState_.noteResize.isResizing
        || interactionState_.isPanning;
    if (snapNextScroll_)
    {
        float snapScroll = smoothScrollCurrent_;
        if (playheadX > pk + vw - margin)
            snapScroll = playheadAbsX - (vw - margin);
        else if (playheadX < pk + margin)
            snapScroll = playheadAbsX - margin;
        else
            snapScroll = juce::jlimit(0.0f, static_cast<float>(maxScroll), centeringScroll);
        smoothScrollCurrent_ = juce::jlimit(0.0f, static_cast<float>(maxScroll), snapScroll);
        snapNextScroll_ = false;
    }

    if (isEditingNow)
    {
        smoothScrollCurrent_ = juce::jlimit(0.0f, static_cast<float>(maxScroll), centeringScroll);
    }
    else
    {
        float newSmooth = smoothScrollCurrent_;
        if (playheadX > pk + vw - margin)
            newSmooth = playheadAbsX - (vw - margin);
        else if (playheadX < pk + margin)
            newSmooth = playheadAbsX - margin;
        smoothScrollCurrent_ = juce::jlimit(0.0f, static_cast<float>(maxScroll), newSmooth);
    }

    const int newScrollInt = static_cast<int>(std::llround(smoothScrollCurrent_));
    if (newScrollInt != scrollOffset_)
    {
        setScrollOffset(newScrollInt);
        notifyVisibleStartTimeChanged();
    }
}

void PianoRollComponent::setZoomLevel(double zoom)
{
    zoomLevel_ = juce::jlimit(getMinimumHorizontalZoomLevel(), 10.0, zoom);
    timeConverter_.setZoom(zoomLevel_);
    playheadOverlay_.setZoomLevel(zoomLevel_);
    updateScrollBars();
    requestInteractiveRepaint();
}

void PianoRollComponent::setVerticalZoom(float pixelsPerSemitone)
{
    const float minZoom = getMinimumVerticalZoom();
    pixelsPerSemitone_ = juce::jlimit(minZoom, 120.0f, pixelsPerSemitone);

    const float totalHeight = getTotalHeight();
    const float visibleHeight = static_cast<float>(getNoteGridViewportHeight());
    const float maxScroll = juce::jmax(0.0f, totalHeight - visibleHeight);
    verticalScrollOffset_ = juce::jlimit(0.0f, maxScroll, verticalScrollOffset_);

    updateScrollBars();
    requestInteractiveRepaint();
}

void PianoRollComponent::setVerticalScrollOffset(float offset)
{
    const float totalHeight = getTotalHeight();
    const float visibleHeight = static_cast<float>(getNoteGridViewportHeight());
    const float maxScroll = juce::jmax(0.0f, totalHeight - visibleHeight);
    verticalScrollOffset_ = juce::jlimit(0.0f, maxScroll, offset);

    verticalScrollBar_.setCurrentRangeStart(verticalScrollOffset_);
    FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Normal);
}

void PianoRollComponent::restoreZoomState(double horizontalZoom, float verticalZoom)
{
    setZoomLevel(horizontalZoom);
    setVerticalZoom(verticalZoom);
    verticalScrollOffset_ = 0.0f;
    userHasManuallyZoomed_ = true;
    updateScrollBars();
    requestInteractiveRepaint();
}

void PianoRollComponent::notifyVisibleStartTimeChanged()
{
    if (onVisibleStartTimeChanged)
        onVisibleStartTimeChanged(getVisibleStartTimeSeconds());
}

double PianoRollComponent::getTimelineSpanSeconds() const
{
    double maxTime = 0.0;

    if (audioBuffer_)
    {
        maxTime = static_cast<double>(audioBuffer_->getNumSamples()) / PianoRollComponent::kAudioSampleRate;
    }
    else
    {
        const auto notes = getCurrentClipNotesCopy();
        for (const auto& note : notes)
        {
            if (note.endTime > maxTime)
                maxTime = note.endTime;
        }
    }

    return std::max(maxTime, 1.0);
}

double PianoRollComponent::getMinimumHorizontalZoomLevel() const
{
    const double spanSeconds = getTimelineSpanSeconds();
    if (spanSeconds <= 0.0)
        return 0.02;

    const int visibleWidth = getTimelineVisibleWidth();
    const double fitZoom = static_cast<double>(visibleWidth) / (spanSeconds * 100.0);
    return juce::jlimit(0.02, 10.0, fitZoom);
}

int PianoRollComponent::getTimelineVisibleWidth() const noexcept
{
    return juce::jmax(1, horizontalScrollBar_.getWidth());
}

void PianoRollComponent::applyScrollBarThumbResize(double thumbStartNormalized, double thumbEndNormalized)
{
    const int visibleWidth = getTimelineVisibleWidth();
    const double normalizedSpan = thumbEndNormalized - thumbStartNormalized;
    const double timelineSpanSeconds = getTimelineSpanSeconds();

    if (timelineSpanSeconds <= 0.0 || normalizedSpan <= 1.0e-6)
        return;

    const double oldZoom = zoomLevel_;
    const int oldScrollOffset = scrollOffset_;
    const double playheadTime = readPlayheadTime();
    const double relativePlayheadTime = juce::jmax(0.0, playheadTime - trackOffsetSeconds_ + alignmentOffsetSeconds_);
    const double oldPlayheadPixel = relativePlayheadTime * (100.0 * oldZoom);
    const double oldViewportPlayheadX = oldPlayheadPixel - static_cast<double>(oldScrollOffset) + static_cast<double>(pianoKeyWidth_);

    const double newTotalRange = static_cast<double>(visibleWidth) / normalizedSpan;
    const double newZoom = juce::jlimit(
        getMinimumHorizontalZoomLevel(),
        10.0,
        newTotalRange / (timelineSpanSeconds * 100.0));

    if (!std::isfinite(newZoom))
        return;

    setZoomLevel(newZoom);
    userHasManuallyZoomed_ = true;

    int newScrollOffset = static_cast<int>(std::llround(thumbStartNormalized * newTotalRange));
    if (std::isfinite(oldViewportPlayheadX))
    {
        const double newPlayheadPixel = relativePlayheadTime * (100.0 * newZoom);
        const double anchoredScroll = newPlayheadPixel + static_cast<double>(pianoKeyWidth_) - oldViewportPlayheadX;
        if (std::isfinite(anchoredScroll))
            newScrollOffset = static_cast<int>(std::llround(anchoredScroll));
    }
    setScrollOffset(newScrollOffset);
    snapNextScroll_ = true;

    if (onUserTimelineZoomChanged)
        onUserTimelineZoomChanged(newZoom);

    notifyVisibleStartTimeChanged();
}

void PianoRollComponent::applyVerticalScrollBarThumbResize(double thumbStartNormalized, double thumbEndNormalized)
{
    const float visibleHeight = static_cast<float>(getNoteGridViewportHeight());
    const double normalizedSpan = thumbEndNormalized - thumbStartNormalized;
    const float midiRange = maxMidi_ - minMidi_;

    if (visibleHeight <= 0.0f || midiRange <= 0.0f || normalizedSpan <= 1.0e-6)
        return;

    const double newTotalHeight = static_cast<double>(visibleHeight) / normalizedSpan;
    const float minZoom = getMinimumVerticalZoom();
    const float newVerticalZoom = juce::jlimit(minZoom, 120.0f, static_cast<float>(newTotalHeight) / midiRange);

    setVerticalZoom(newVerticalZoom);
    userHasManuallyZoomed_ = true;

    const float updatedVisibleHeight = static_cast<float>(getNoteGridViewportHeight());
    const float updatedTotalHeight = getTotalHeight();
    const float updatedRangeLimit = juce::jmax(updatedTotalHeight, updatedVisibleHeight);
    const float newOffset = static_cast<float>(thumbStartNormalized * static_cast<double>(updatedRangeLimit));
    setVerticalScrollOffset(newOffset);
}

void PianoRollComponent::setCurrentTool(ToolId tool)
{
    if (tool != ToolId::LineAnchor && currentTool_ == ToolId::LineAnchor)
    {
        if (interactionState_.drawing.isPlacingAnchors)
        {
            if (undoSupport_ && undoSupport_->isTransactionActive())
                undoSupport_->commitTransaction();
            interactionState_.drawing.isPlacingAnchors = false;
            interactionState_.drawing.pendingAnchors.clear();
        }
        interactionState_.drawing.anchorEdit.clear();
        anchorFitOverlay_.setVisible(false);
        anchorFitting_.store(false, std::memory_order_release);
    }

    if (tool != ToolId::HandDraw && currentTool_ == ToolId::HandDraw)
    {
        interactionState_.handDrawPendingDrag = false;
        if (interactionState_.drawing.isDrawingF0 && undoSupport_ != nullptr && undoSupport_->isTransactionActive())
            undoSupport_->commitTransaction();
        interactionState_.drawing.clearF0Drawing();
    }

    const bool changed = (tool != currentTool_);

    if (changed)
    {
        auto& notes = getCurrentClipNotes();
        for (auto& n : notes)
            n.selected = false;
        interactionState_.selection.clearF0Selection();
    }

    currentTool_ = tool;
    anchorFitSerial_++;
    if (toolHandler_)
    {
        toolHandler_->setTool(tool);
        if (tool == ToolId::LineAnchor)
        {
            anchorFitting_.store(true, std::memory_order_release);
            anchorFitOverlay_.setMessageText(juce::String::fromUTF8("\u6b63\u5728\u62df\u5408\u951a\u70b9..."));
            anchorFitOverlay_.setVisible(true);
            anchorFitOverlay_.toFront(false);
            const uint32_t serial = anchorFitSerial_;
            juce::MessageManager::callAsync([this, serial]() {
                if (anchorFitSerial_ != serial)
                    return;
                toolHandler_->loadAnchorsFromCurve();
                anchorFitting_.store(false, std::memory_order_release);
                anchorFitOverlay_.setVisible(false);
                repaint();
            });
        }
    }

    switch (tool)
    {
        case ToolId::Select:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
        case ToolId::DrawNote:
        case ToolId::LineAnchor:
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            break;
        case ToolId::SplitNote:
            setMouseCursor(juce::MouseCursor::IBeamCursor);
            break;
        case ToolId::Vibrato:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
        case ToolId::HandDraw:
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            break;
        case ToolId::AutoTune:
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            break;
    }

    if (changed)
    {
        const int toolId = static_cast<int>(tool);
        listeners_.call([toolId](Listener& l) { l.toolChanged(toolId); });
    }

    updateToolButtonStates();
    repaint();
}

bool PianoRollComponent::selectToolByContextMenuCommand(int commandId)
{
    switch (commandId)
    {
        case kContextMenuCommandSelect:
            setCurrentTool(ToolId::Select);
            return true;
        case kContextMenuCommandDrawNote:
            setCurrentTool(ToolId::DrawNote);
            return true;
        case kContextMenuCommandHandDraw:
            setCurrentTool(ToolId::HandDraw);
            return true;
        case kContextMenuCommandSplitNote:
            setCurrentTool(ToolId::SplitNote);
            return true;
        default:
            return false;
    }
}

void PianoRollComponent::beginRightToolMenuLongPress(juce::Point<int> anchorPos)
{
    juce::ignoreUnused(anchorPos);
    ++rightToolMenuPressSerial_;
    const uint32_t serial = rightToolMenuPressSerial_;
    rightToolMenuPhase_ = RightToolMenuPhase::WaitingLongPress;

    juce::Component::SafePointer<PianoRollComponent> safeThis(this);
    juce::Timer::callAfterDelay(kRightToolMenuLongPressMs, [safeThis, serial]() {
        if (safeThis != nullptr)
            safeThis->onRightToolMenuLongPressFired(serial);
    });
}

void PianoRollComponent::onRightToolMenuLongPressFired(uint32_t serial)
{
    if (serial != rightToolMenuPressSerial_)
        return;
    if (rightToolMenuPhase_ != RightToolMenuPhase::WaitingLongPress)
        return;

    rightToolMenuPhase_ = RightToolMenuPhase::None;
    showToolContextPopupMenu();
}

void PianoRollComponent::showToolContextPopupMenu()
{
    auto applyTool = [this](ToolId tool) {
        if (externalToolSelectionHandler_)
            externalToolSelectionHandler_(static_cast<int>(tool));
        else
            setCurrentTool(tool);
    };

    juce::PopupMenu menu;
    menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcutAction(
        UiText::pianoRollToolSelect(), KeyShortcutConfig::ShortcutId::ToolSelect,
        [applyTool] { applyTool(ToolId::Select); }));
    menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcutAction(
        UiText::pianoRollToolDrawNote(), KeyShortcutConfig::ShortcutId::ToolDrawNote,
        [applyTool] { applyTool(ToolId::DrawNote); }));
    menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcutAction(
        UiText::pianoRollToolLineAnchor(), KeyShortcutConfig::ShortcutId::ToolLineAnchor,
        [applyTool] { applyTool(ToolId::LineAnchor); }));
    menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcutAction(
        UiText::pianoRollToolHandDraw(), KeyShortcutConfig::ShortcutId::ToolHandDraw,
        [applyTool] { applyTool(ToolId::HandDraw); }));
    menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcutAction(
        UiText::pianoRollToolVibrato(), KeyShortcutConfig::ShortcutId::ToolVibrato,
        [applyTool] { applyTool(ToolId::Vibrato); }));
    menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcutAction(
        UiText::pianoRollToolSplitNote(), KeyShortcutConfig::ShortcutId::ToolSplitNote,
        [applyTool] { applyTool(ToolId::SplitNote); }));
    menu.showMenuAsync(juce::PopupMenu::Options());
}

bool PianoRollComponent::handleRightToolMenuMouseUp(const juce::MouseEvent& e)
{
    if (rightToolMenuPhase_ != RightToolMenuPhase::WaitingLongPress)
        return false;

    if (!e.mods.isPopupMenu())
        return false;

    ++rightToolMenuPressSerial_;
    rightToolMenuPhase_ = RightToolMenuPhase::None;
    showToolContextPopupMenu();
    return true;
}

void PianoRollComponent::mouseMove(const juce::MouseEvent& e)
{
    toolHandler_->mouseMove(e);
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    if (isAutoTuneProcessing())
        return;

    if (juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey))
    {
        interactionState_.isPanning = true;
        interactionState_.dragStartPos = e.getPosition();
        dragStartScrollOffset_ = scrollOffset_;
        dragStartVerticalScrollOffset_ = verticalScrollOffset_;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    toolHandler_->mouseDown(e);
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (isAutoTuneProcessing())
        return;

    if (interactionState_.isPanning)
    {
        int deltaX = e.x - interactionState_.dragStartPos.x;
        int deltaY = e.y - interactionState_.dragStartPos.y;
        int newScrollX = dragStartScrollOffset_ - deltaX;
        setScrollOffset(newScrollX);
        float newScrollY = dragStartVerticalScrollOffset_ - static_cast<float>(deltaY);
        const float vh = static_cast<float>(getNoteGridViewportHeight());
        float maxScroll = getTotalHeight() - vh;
        verticalScrollOffset_ = juce::jlimit(0.0f, std::max(0.0f, maxScroll), newScrollY);
        notifyVisibleStartTimeChanged();
        requestInteractiveRepaint();
        return;
    }

    if (toolHandler_ && toolHandler_->isDraggingTimelinePlayhead())
    {
        const int leftBoundary = pianoKeyWidth_;
        const int rightBoundary = leftBoundary + getTimelineVisibleWidth();
        bool scrolled = false;

        if (e.x < leftBoundary)
        {
            setScrollOffset(scrollOffset_ - (leftBoundary - e.x));
            scrolled = true;
        }
        else if (e.x > rightBoundary)
        {
            setScrollOffset(scrollOffset_ + (e.x - rightBoundary));
            scrolled = true;
        }

        if (scrolled)
            notifyVisibleStartTimeChanged();
    }

    toolHandler_->mouseDrag(e);
    requestInteractiveRepaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent& e)
{
    if (isAutoTuneProcessing())
        return;

    if (interactionState_.isPanning)
    {
        interactionState_.isPanning = false;
        setCurrentTool(currentTool_);
        grabKeyboardFocus();
        return;
    }

    if (handleRightToolMenuMouseUp(e))
    {
        grabKeyboardFocus();
        return;
    }

    toolHandler_->mouseUp(e);
    grabKeyboardFocus();
}

void PianoRollComponent::handleVerticalZoomWheel(const juce::MouseEvent& e, float deltaY)
{
    const auto& settings = ZoomSensitivityConfig::getSettings();
    float zoomFactor = 1.0f + (deltaY * settings.verticalZoomFactor);
    float mouseMidi = yToMidi(static_cast<float>(e.y));

    pixelsPerSemitone_ *= zoomFactor;
    pixelsPerSemitone_ = juce::jlimit(getMinimumVerticalZoom(), 120.0f, pixelsPerSemitone_);
    userHasManuallyZoomed_ = true;

    float targetY = (maxMidi_ - mouseMidi) * pixelsPerSemitone_;
    verticalScrollOffset_ = targetY - static_cast<float>(e.y);

    float totalHeight = getTotalHeight();
    float visibleHeight = static_cast<float>(getNoteGridViewportHeight());
    float maxScroll = totalHeight - visibleHeight;
    if (maxScroll > 0.0f)
        verticalScrollOffset_ = juce::jlimit(0.0f, maxScroll, verticalScrollOffset_);
    else
        verticalScrollOffset_ = 0.0f;

    updateScrollBars();
    repaint();
}

void PianoRollComponent::handleHorizontalScrollWheel(float deltaX, float deltaY)
{
    const auto& settings = ZoomSensitivityConfig::getSettings();
    float scrollDelta = (deltaX != 0 ? deltaX : deltaY);
    int pixelDelta = static_cast<int>(scrollDelta * settings.scrollSpeed);
    setScrollOffset(scrollOffset_ - pixelDelta);
    notifyVisibleStartTimeChanged();
}

void PianoRollComponent::handleVerticalScrollWheel(float deltaY)
{
    const auto& settings = ZoomSensitivityConfig::getSettings();
    float scrollDelta = deltaY * settings.scrollSpeed;
    verticalScrollOffset_ -= scrollDelta;
    float totalHeight = getTotalHeight();
    float visibleHeight = static_cast<float>(getNoteGridViewportHeight());
    float maxScroll = totalHeight - visibleHeight;
    if (maxScroll > 0.0f)
        verticalScrollOffset_ = juce::jlimit(0.0f, maxScroll, verticalScrollOffset_);
    else
        verticalScrollOffset_ = 0.0f;

    updateScrollBars();
    repaint();
}

void PianoRollComponent::handleHorizontalZoomWheel(const juce::MouseEvent& e, float deltaY)
{
    const auto& settings = ZoomSensitivityConfig::getSettings();
    double zoomFactor = 1.0 + deltaY * settings.horizontalZoomFactor;
    double newZoom = zoomLevel_ * zoomFactor;
    newZoom = juce::jlimit(getMinimumHorizontalZoomLevel(), 10.0, newZoom);

    int mouseX = e.x - pianoKeyWidth_;
    double mouseTime = timeConverter_.pixelToTime(mouseX);

    setZoomLevel(newZoom);
    userHasManuallyZoomed_ = true;

    setScrollOffset(0);
    int absolutePixel = timeConverter_.timeToPixel(mouseTime);
    int newScrollOffset = absolutePixel - mouseX;
    setScrollOffset(newScrollOffset);
    snapNextScroll_ = true;

    if (onUserTimelineZoomChanged) {
        onUserTimelineZoomChanged(zoomLevel_);
    }

    notifyVisibleStartTimeChanged();
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    float deltaX = wheel.deltaX;
    float deltaY = wheel.deltaY;

    if (deltaY == 0.0f && deltaX == 0.0f)
        return;

    const bool ctrl = e.mods.isCtrlDown() || e.mods.isCommandDown();
    const bool shift = e.mods.isShiftDown();
    const bool alt = e.mods.isAltDown();

    // Alt+Ctrl：横向+纵向同时缩放（全局缩放）
    if (alt && ctrl)
    {
        handleHorizontalZoomWheel(e, deltaY);
        handleVerticalZoomWheel(e, deltaY);
    }
    else if (shift)
    {
        handleHorizontalScrollWheel(deltaX, deltaY);
    }
    else if (ctrl)
    {
        handleHorizontalZoomWheel(e, deltaY);
    }
    else if (alt)
    {
        handleVerticalZoomWheel(e, deltaY);
    }
    else
    {
        handleVerticalScrollWheel(deltaY);
    }
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key)
{
    if (auto* top = findParentComponentOfClass<TopBarComponent>())
    {
        auto* parentEditor = findParentComponentOfClass<juce::AudioProcessorEditor>();
        if (top->getMenuBar().tryHandleTopLevelMenuMnemonic(key, top->getTransportBar(), *top, parentEditor))
            return true;
    }

    if (isAutoTuneProcessing() || isAnchorFitting())
        return false;

    return toolHandler_->keyPressed(key);
}

void PianoRollComponent::visibilityChanged()
{
    if (isShowing() && isVisible())
    {
        juce::Component::SafePointer<PianoRollComponent> safeThis(this);
        juce::Timer::callAfterDelay(10, [safeThis]() {
            if (safeThis != nullptr && safeThis->isShowing())
                safeThis->grabKeyboardFocus();
        });
    }
}

void PianoRollComponent::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart)
{
    if (scrollBar == &horizontalScrollBar_)
    {
        setScrollOffset(static_cast<int>(newRangeStart));
        smoothScrollCurrent_ = static_cast<float>(newRangeStart);
        notifyVisibleStartTimeChanged();
    }
    else if (scrollBar == &verticalScrollBar_)
    {
        setVerticalScrollOffset(static_cast<float>(newRangeStart));
    }
}

void PianoRollComponent::updateScrollBars()
{
    pixelsPerSemitone_ = juce::jmax(pixelsPerSemitone_, getMinimumVerticalZoom());

    const double maxTime = getTimelineSpanSeconds();

    double pixelsPerSecond = 100.0 * zoomLevel_;
    int totalContentWidth = static_cast<int>(maxTime * pixelsPerSecond);
    int visibleWidth = getTimelineVisibleWidth();
    const int maxRange = juce::jmax(totalContentWidth, visibleWidth);
    scrollOffset_ = juce::jlimit(0, juce::jmax(0, maxRange - visibleWidth), scrollOffset_);

    horizontalScrollBar_.setRangeLimits(0.0, static_cast<double>(maxRange));
    horizontalScrollBar_.setCurrentRange(scrollOffset_, visibleWidth);

    float totalHeight = getTotalHeight();
    int visibleHeight = getNoteGridViewportHeight();
    verticalScrollOffset_ = juce::jlimit(0.0f,
                                         juce::jmax(0.0f, totalHeight - static_cast<float>(visibleHeight)),
                                         verticalScrollOffset_);

    verticalScrollBar_.setRangeLimits(0.0, static_cast<double>(juce::jmax(totalHeight, static_cast<float>(visibleHeight))));
    verticalScrollBar_.setCurrentRange(verticalScrollOffset_, visibleHeight);
}

} // namespace OpenTune
