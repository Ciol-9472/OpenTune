#include "PianoRollToolHandler.h"
#include "PianoRollToolHandler.Shared.h"
#include "../../../Utils/AppLogger.h"
#include "../../../Utils/KeyShortcutConfig.h"

namespace OpenTune {

// ============================================================================
// PianoRollToolHandler - 钢琴卷帘工具处理器实现
// ============================================================================

PianoRollToolHandler::PianoRollToolHandler(Context context)
    : ctx_(std::move(context))
{
    AppLogger::debug("[PianoRollToolHandler] Created with default tool");
}

void PianoRollToolHandler::mouseMove(const juce::MouseEvent& e)
// 鼠标移动处理：更新光标形状（音符边缘调整、线锚点预览）
{
    if (currentTool_ == ToolId::LineAnchor) {
        ctx_.getState().drawing.currentMousePos = e.position;
        auto hit = hitTestAnchor(static_cast<float>(e.x), static_cast<float>(e.y));
        if (hit.groupIdx >= 0) {
            ctx_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        } else {
            ctx_.setMouseCursor(juce::MouseCursor::CrosshairCursor);
        }
        ctx_.requestRepaint();
        return;
    }

    if (currentTool_ == ToolId::Select || currentTool_ == ToolId::DrawNote) {
        const int edgeThreshold = 6;
        const double offsetSeconds = ctx_.getTrackOffsetSeconds();
        const float mousePitch = ctx_.yToFreq((float)e.y);
        const float mouseMidiVal = 69.0f + 12.0f * std::log2(mousePitch / 440.0f) - 0.5f;

        auto& notes = ctx_.getNotes();
        Note* edgeNote = nullptr;
        NoteResizeEdge pickedEdge = NoteResizeEdge::None;
        const bool edgeHit = pickBestNoteEdgeHit(
            notes,
            e.x,
            mouseMidiVal,
            edgeThreshold,
            offsetSeconds,
            [&](double t) { return ctx_.timeToX(t); },
            edgeNote,
            pickedEdge);

        if (edgeHit && edgeNote != nullptr) {
            (void)pickedEdge;
            ctx_.setMouseCursor(juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor));
            return;
        }

        bool bodyHit = false;
        for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
            const auto& note = *it;
            const int x1 = ctx_.timeToX(note.startTime + offsetSeconds);
            const int x2 = ctx_.timeToX(note.endTime + offsetSeconds);
            const float noteMidi = midiBandForNote(note);
            if (std::abs(mouseMidiVal - noteMidi) >= 1.0f)
                continue;
            if (e.x >= x1 && e.x <= x2) {
                ctx_.setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
                bodyHit = true;
                break;
            }
        }

        if (!bodyHit) {
            ctx_.setMouseCursor(juce::MouseCursor::NormalCursor);
        }
        return;
    }
}

void PianoRollToolHandler::mouseDown(const juce::MouseEvent& e)
{
    AppLogger::debug("[PianoRollToolHandler] mouseDown: pos=(" + juce::String(e.x) + "," + juce::String(e.y) 
        + "), tool=" + juce::String(static_cast<int>(currentTool_)));

    ctx_.grabKeyboardFocus();

    if (e.mods.isPopupMenu()) {
        if (currentTool_ == ToolId::LineAnchor) {
            auto& ae = ctx_.getState().drawing.anchorEdit;
            auto hit = hitTestAnchor(static_cast<float>(e.x), static_cast<float>(e.y));
            if (hit.groupIdx >= 0 && hit.pointIdx >= 0) {
                if (!ctx_.isTransactionActive())
                    ctx_.beginEditTransaction("Delete Anchor");
                auto& grp = ae.groups[hit.groupIdx];
                grp.removePoint(hit.pointIdx);
                if (grp.points.size() < 2) {
                    ae.groups.erase(ae.groups.begin() + hit.groupIdx);
                    ae.activeGroupIndex = -1;
                }
                regenerateAnchorsF0();
                commitAnchorEdit();
                ctx_.requestRepaint();
                return;
            }
            if (ae.mode == AnchorEditState::Mode::Placing) {
                for (auto& g : ae.groups) g.deselectAll();
                ae.mode = AnchorEditState::Mode::Idle;
                ae.activeGroupIndex = -1;
                ae.draggedAnchorIndex = -1;
                ctx_.getState().drawing.isPlacingAnchors = false;
                if (ctx_.isTransactionActive())
                    ctx_.commitEditTransaction();
                ctx_.requestRepaint();
            }
            return;
        }
        AppLogger::debug("[PianoRollToolHandler] mouseDown: begin right tool menu long-press");
        if (ctx_.beginRightToolMenuLongPress)
            ctx_.beginRightToolMenuLongPress(e.getPosition());
        return;
    }

    constexpr int inset = 12;
    constexpr int rulerHeight = 30;
    constexpr int timelineExtendedHitArea = 20;
    const int timelineBottomExtended = inset + rulerHeight + timelineExtendedHitArea;
    if (e.y < timelineBottomExtended && e.x > ctx_.getPianoKeyWidth()) {
        double clickedTime = ctx_.xToTime(e.x);
        if (clickedTime >= 0) {
            ctx_.notifyPlayheadChange(clickedTime);
        }
        return;
    }

    dragStartPos_ = e.getPosition();

    if (e.x > ctx_.getPianoKeyWidth()) {
        double clickedTime = ctx_.xToTime(e.x);
        if (clickedTime >= 0) {
            ctx_.notifyPlayheadChange(clickedTime);
        }
    }

    switch (currentTool_) {
        case ToolId::AutoTune:
            AppLogger::debug("[PianoRollToolHandler] mouseDown: handling AutoTune tool");
            handleAutoTuneTool(e);
            break;
        case ToolId::Select:
            AppLogger::debug("[PianoRollToolHandler] mouseDown: handling Select tool");
            handleSelectTool(e);
            break;
        case ToolId::DrawNote:
            AppLogger::debug("[PianoRollToolHandler] mouseDown: handling DrawNote tool");
            handleDrawNoteMouseDown(e);
            break;
        case ToolId::HandDraw:
            ctx_.getState().handDrawPendingDrag = true;
            AppLogger::debug("[PianoRollToolHandler] mouseDown: HandDraw tool pending drag");
            break;
        case ToolId::LineAnchor:
            AppLogger::debug("[PianoRollToolHandler] mouseDown: handling LineAnchor tool");
            handleLineAnchorMouseDown(e);
            break;
        case ToolId::SplitNote:
            AppLogger::debug("[PianoRollToolHandler] mouseDown: handling SplitNote tool");
            handleSplitNoteTool(e);
            break;
        default:
            AppLogger::warn("[PianoRollToolHandler] mouseDown: unknown tool " + juce::String(static_cast<int>(currentTool_)));
            break;
    }
}

void PianoRollToolHandler::mouseDrag(const juce::MouseEvent& e)
{
    AppLogger::debug("[PianoRollToolHandler] mouseDrag: pos=(" + juce::String(e.x) + "," + juce::String(e.y) 
        + "), tool=" + juce::String(static_cast<int>(currentTool_)));

    switch (currentTool_) {
        case ToolId::Select:
            handleSelectDrag(e);
            break;
        case ToolId::DrawNote:
            handleDrawNoteDrag(e);
            break;
        case ToolId::HandDraw:
            if (ctx_.getState().handDrawPendingDrag) {
                int dx = e.x - dragStartPos_.x;
                int dy = e.y - dragStartPos_.y;
                int threshold = ctx_.getDragThreshold();
                if (dx * dx + dy * dy > threshold * threshold) {
                    ctx_.getState().handDrawPendingDrag = false;
                    AppLogger::debug("[PianoRollToolHandler] mouseDrag: HandDraw threshold exceeded, starting curve draw");
                    handleDrawCurveTool(e);
                }
            } else if (ctx_.getState().drawing.isDrawingF0) {
                handleDrawCurveTool(e);
            }
            break;
        case ToolId::LineAnchor:
            handleLineAnchorMouseDrag(e);
            break;
        default:
            break;
    }

    ctx_.requestRepaint();
}

void PianoRollToolHandler::mouseUp(const juce::MouseEvent& e)
{
    AppLogger::debug("[PianoRollToolHandler] mouseUp: tool=" + juce::String(static_cast<int>(currentTool_)));

    switch (currentTool_) {
        case ToolId::Select:
            AppLogger::debug("[PianoRollToolHandler] mouseUp: handling Select tool");
            handleSelectUp(e);
            break;
        case ToolId::HandDraw:
            AppLogger::debug("[PianoRollToolHandler] mouseUp: handling HandDraw tool");
            handleDrawCurveUp(e);
            break;
        case ToolId::DrawNote:
            AppLogger::debug("[PianoRollToolHandler] mouseUp: handling DrawNote tool");
            handleDrawNoteUp(e);
            break;
        case ToolId::LineAnchor:
            AppLogger::debug("[PianoRollToolHandler] mouseUp: handling LineAnchor tool");
            handleLineAnchorMouseUp(e);
            break;
        default:
            ctx_.getState().noteDrag.draggedNote = nullptr;
            break;
    }

    ctx_.requestRepaint();
}

void PianoRollToolHandler::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(e, wheel);
}

bool PianoRollToolHandler::keyPressed(const juce::KeyPress& key)
{
    AppLogger::debug("[PianoRollToolHandler] keyPressed: keyCode=" + juce::String(key.getKeyCode()));

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::SelectAll, key)) {
        if (currentTool_ == ToolId::LineAnchor) {
            auto& ae = ctx_.getState().drawing.anchorEdit;
            for (auto& g : ae.groups)
                for (auto& pt : g.points)
                    pt.selected = true;
            ctx_.requestRepaint();
            return true;
        }

        AppLogger::debug("[PianoRollToolHandler] keyPressed: select all");
        auto& notes = ctx_.getNotes();
        if (notes.empty()) {
            return true;
        }
        
        ctx_.selectAllNotes();
        updateF0SelectionFromNotes();
        ctx_.requestRepaint();
        return true;
    }

    if (!key.getModifiers().isAnyModifierKeyDown()) {
        if (key.getTextCharacter() == '1') {
            AppLogger::debug("[PianoRollToolHandler] keyPressed: switching to Select tool");
            ctx_.setCurrentTool(ToolId::Select);
            return true;
        }

        if (key.getTextCharacter() == '2') {
            AppLogger::debug("[PianoRollToolHandler] keyPressed: switching to DrawNote tool");
            ctx_.setCurrentTool(ToolId::DrawNote);
            return true;
        }

        if (key.getTextCharacter() == '3') {
            AppLogger::debug("[PianoRollToolHandler] keyPressed: switching to LineAnchor tool");
            ctx_.setCurrentTool(ToolId::LineAnchor);
            return true;
        }

        if (key.getTextCharacter() == '4') {
            AppLogger::debug("[PianoRollToolHandler] keyPressed: switching to HandDraw tool");
            ctx_.setCurrentTool(ToolId::HandDraw);
            return true;
        }

        if (key.getTextCharacter() == '5') {
            AppLogger::debug("[PianoRollToolHandler] keyPressed: switching to SplitNote tool");
            ctx_.setCurrentTool(ToolId::SplitNote);
            return true;
        }

        if (key.getTextCharacter() == '6') {
            AppLogger::debug("[PianoRollToolHandler] keyPressed: AutoTune requested");
            ctx_.notifyAutoTuneRequested();
            return true;
        }
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::PlayPause, key)) {
        AppLogger::debug("[PianoRollToolHandler] keyPressed: play/pause toggle");
        ctx_.notifyPlayPauseToggle();
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::Stop, key)) {
        AppLogger::debug("[PianoRollToolHandler] keyPressed: stop playback");
        ctx_.notifyStopPlayback();
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::Delete, key)) {
        AppLogger::debug("[PianoRollToolHandler] keyPressed: delete key pressed");

        if (currentTool_ == ToolId::LineAnchor) {
            auto& ae = ctx_.getState().drawing.anchorEdit;
            bool anySelected = false;
            for (const auto& g : ae.groups)
                if (g.hasSelected()) { anySelected = true; break; }
            if (anySelected) {
                if (!ctx_.isTransactionActive())
                    ctx_.beginEditTransaction("Delete Anchor");
                for (auto& g : ae.groups) g.deleteSelected();
                ae.groups.erase(
                    std::remove_if(ae.groups.begin(), ae.groups.end(),
                        [](const AnchorGroup& g) { return g.points.size() < 2; }),
                    ae.groups.end());
                ae.activeGroupIndex = -1;
                regenerateAnchorsF0();
                commitAnchorEdit();
                ctx_.requestRepaint();
                return true;
            }
        }

        handleDeleteKey();
        ctx_.requestRepaint();
        return true;
    }

    if (key == juce::KeyPress::escapeKey) {
        if (currentTool_ == ToolId::LineAnchor) {
            auto& ae = ctx_.getState().drawing.anchorEdit;
            bool anySelected = false;
            for (const auto& g : ae.groups) if (g.hasSelected()) { anySelected = true; break; }
            if (ae.mode == AnchorEditState::Mode::Placing || anySelected) {
                for (auto& g : ae.groups) g.deselectAll();
                ae.mode = AnchorEditState::Mode::Idle;
                ae.activeGroupIndex = -1;
                ctx_.getState().drawing.isPlacingAnchors = false;
                ctx_.requestRepaint();
                return true;
            }
        }

        auto selectedNotes = ctx_.getSelectedNotes();
        if (!selectedNotes.empty()) {
            AppLogger::debug("[PianoRollToolHandler] keyPressed: escape deselecting " + juce::String(selectedNotes.size()) + " notes");
            ctx_.deselectAllNotes();
            ctx_.getState().selection.clearF0Selection();
            ctx_.requestRepaint();
        } else {
            AppLogger::debug("[PianoRollToolHandler] keyPressed: escape key, no selection, propagating");
            ctx_.notifyEscapeKey();
        }
        return true;
    }

    return false;
}


} // namespace OpenTune
