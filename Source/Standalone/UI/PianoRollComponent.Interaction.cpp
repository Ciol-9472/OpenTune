#include "PianoRollComponent.h"
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
    const int newOffset = juce::jmax(0, offset);
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

double PianoRollComponent::readPlayheadTime() const
{
    if (auto source = positionSource_.lock())
        return source->load(std::memory_order_relaxed);

    return 0.0;
}

void PianoRollComponent::updateAutoScroll()
{
    if (!isPlaying_.load(std::memory_order_relaxed))
        return;

    const double playheadTime = readPlayheadTime();

    if (scrollMode_ == ScrollMode::Page)
    {
        double pixelsPerSecond = 100.0 * zoomLevel_;
        int playheadVisualX = static_cast<int>(playheadTime * pixelsPerSecond) - scrollOffset_ + pianoKeyWidth_;

        if (playheadVisualX >= getWidth())
        {
            int visibleW = getWidth() - pianoKeyWidth_;
            setScrollOffset(scrollOffset_ + visibleW);
            smoothScrollCurrent_ = static_cast<float>(scrollOffset_);
        }
        else if (playheadVisualX < pianoKeyWidth_)
        {
            int absX = static_cast<int>(playheadTime * pixelsPerSecond);

            int visibleW = getWidth() - pianoKeyWidth_;
            int pageIndex = absX / visibleW;
            int newScroll = pageIndex * visibleW;

            setScrollOffset(newScroll);
            smoothScrollCurrent_ = static_cast<float>(newScroll);
        }
    }
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

    if (scrollMode_ == ScrollMode::Continuous)
    {
        const double pixelsPerSecond = 100.0 * zoomLevel_;
        const float playheadAbsX = static_cast<float>(playheadTime * pixelsPerSecond);

        const float viewCenter = pianoKeyWidth_ + (getWidth() - pianoKeyWidth_) / 2.0f;
        float targetScroll = playheadAbsX + pianoKeyWidth_ - viewCenter;
        if (targetScroll < 0.0f)
            targetScroll = 0.0f;

        const bool isEditingNow = interactionState_.drawing.isDrawingF0
            || interactionState_.drawing.isDrawingNote
            || interactionState_.noteDrag.isDraggingNotes
            || interactionState_.noteResize.isResizing
            || interactionState_.isPanning;
        if (snapNextScroll_)
        {
            smoothScrollCurrent_ = targetScroll;
            snapNextScroll_ = false;
        }

        if (isEditingNow)
        {
            smoothScrollCurrent_ = targetScroll;
        }
        else
        {
            const float diff = targetScroll - smoothScrollCurrent_;
            if (std::abs(diff) < 1.0f)
                smoothScrollCurrent_ = targetScroll;
            else
                smoothScrollCurrent_ += diff * 0.2f;
        }

        const int newScrollInt = static_cast<int>(std::llround(smoothScrollCurrent_));
        if (newScrollInt != scrollOffset_)
            setScrollOffset(newScrollInt);
        return;
    }

    if (scrollMode_ == ScrollMode::Page)
    {
        double pixelsPerSecond = 100.0 * zoomLevel_;
        int playheadVisualX = static_cast<int>(playheadTime * pixelsPerSecond) - scrollOffset_ + pianoKeyWidth_;

        if (playheadVisualX >= getWidth())
        {
            int visibleW = getWidth() - pianoKeyWidth_;
            setScrollOffset(scrollOffset_ + visibleW);
            smoothScrollCurrent_ = static_cast<float>(scrollOffset_);
        }
        else if (playheadVisualX < pianoKeyWidth_)
        {
            int absX = static_cast<int>(playheadTime * pixelsPerSecond);

            int visibleW = getWidth() - pianoKeyWidth_;
            int pageIndex = absX / visibleW;
            int newScroll = pageIndex * visibleW;

            setScrollOffset(newScroll);
            smoothScrollCurrent_ = static_cast<float>(newScroll);
        }
    }
}

void PianoRollComponent::setZoomLevel(double zoom)
{
    zoomLevel_ = juce::jlimit(0.02, 10.0, zoom);
    timeConverter_.setZoom(zoomLevel_);
    playheadOverlay_.setZoomLevel(zoomLevel_);
    updateScrollBars();
    requestInteractiveRepaint();
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
    menu.addItem(UiText::pianoRollToolSelect(), [applyTool] { applyTool(ToolId::Select); });
    menu.addItem(UiText::pianoRollToolDrawNote(), [applyTool] { applyTool(ToolId::DrawNote); });
    menu.addItem(UiText::pianoRollToolLineAnchor(), [applyTool] { applyTool(ToolId::LineAnchor); });
    menu.addItem(UiText::pianoRollToolHandDraw(), [applyTool] { applyTool(ToolId::HandDraw); });
    menu.addItem(UiText::pianoRollToolSplitNote(), [applyTool] { applyTool(ToolId::SplitNote); });
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
        float maxScroll = getTotalHeight() - getHeight();
        verticalScrollOffset_ = juce::jlimit(0.0f, std::max(0.0f, maxScroll), newScrollY);
        requestInteractiveRepaint();
        return;
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
    pixelsPerSemitone_ = juce::jlimit(5.0f, 120.0f, pixelsPerSemitone_);
    userHasManuallyZoomed_ = true;

    float targetY = (maxMidi_ - mouseMidi) * pixelsPerSemitone_;
    verticalScrollOffset_ = targetY - static_cast<float>(e.y);

    float totalHeight = getTotalHeight();
    float visibleHeight = static_cast<float>(getHeight() - rulerHeight_ - 15);
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
}

void PianoRollComponent::handleVerticalScrollWheel(float deltaY)
{
    const auto& settings = ZoomSensitivityConfig::getSettings();
    float scrollDelta = deltaY * settings.scrollSpeed;
    verticalScrollOffset_ -= scrollDelta;
    float totalHeight = getTotalHeight();
    float visibleHeight = static_cast<float>(getHeight() - rulerHeight_ - 15);
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
    newZoom = std::max(0.02, std::min(10.0, newZoom));

    int mouseX = e.x - pianoKeyWidth_;
    double mouseTime = timeConverter_.pixelToTime(mouseX);

    setZoomLevel(newZoom);
    userHasManuallyZoomed_ = true;

    setScrollOffset(0);
    int absolutePixel = timeConverter_.timeToPixel(mouseTime);
    int newScrollOffset = absolutePixel - mouseX;
    setScrollOffset(newScrollOffset);
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    float deltaX = wheel.deltaX;
    float deltaY = wheel.deltaY;

#if JUCE_MAC
    if (e.mods.isShiftDown() && deltaY == 0.0f && deltaX != 0.0f)
    {
        deltaY = deltaX;
        deltaX = 0.0f;
    }
#endif

    if (deltaY == 0.0f && deltaX == 0.0f)
        return;

    const bool ctrl = e.mods.isCtrlDown() || e.mods.isCommandDown();
    const bool shift = e.mods.isShiftDown();

    if (ctrl && shift)
    {
        handleHorizontalZoomWheel(e, deltaY);
        handleVerticalZoomWheel(e, deltaY);
    }
    else if (shift)
    {
        handleVerticalZoomWheel(e, deltaY);
    }
    else if (ctrl)
    {
        handleHorizontalZoomWheel(e, deltaY);
    }
    else if (e.mods.isAltDown())
    {
        handleHorizontalScrollWheel(deltaX, deltaY);
    }
    else
    {
        handleVerticalScrollWheel(deltaY);
    }
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key)
{
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
    }
    else if (scrollBar == &verticalScrollBar_)
    {
        verticalScrollOffset_ = static_cast<float>(newRangeStart);
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Normal);
    }
}

void PianoRollComponent::updateScrollBars()
{
    double maxTime = 0.0;
    if (audioBuffer_)
    {
        maxTime = static_cast<double>(audioBuffer_->getNumSamples()) / PianoRollComponent::kAudioSampleRate;
    }
    else
    {
        const auto& notes = getCurrentClipNotes();
        for (const auto& note : notes)
        {
            if (note.endTime > maxTime)
                maxTime = note.endTime;
        }
    }

    maxTime = std::max(maxTime, 10.0);
    maxTime += 5.0;

    double pixelsPerSecond = 100.0 * zoomLevel_;
    int totalContentWidth = static_cast<int>(maxTime * pixelsPerSecond);
    int visibleWidth = getWidth() - pianoKeyWidth_ - 15;
    visibleWidth = juce::jmax(1, visibleWidth);

    horizontalScrollBar_.setRangeLimits(0.0, totalContentWidth + visibleWidth);
    horizontalScrollBar_.setCurrentRange(scrollOffset_, visibleWidth);

    float totalHeight = getTotalHeight();
    int visibleHeight = getHeight() - rulerHeight_ - 15;
    visibleHeight = juce::jmax(1, visibleHeight);

    verticalScrollBar_.setRangeLimits(0.0, totalHeight);
    verticalScrollBar_.setCurrentRange(verticalScrollOffset_, visibleHeight);
}

} // namespace OpenTune
