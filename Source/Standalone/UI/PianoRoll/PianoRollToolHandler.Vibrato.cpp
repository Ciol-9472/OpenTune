#include "PianoRollToolHandler.h"
#include "PianoRollToolHandler.Shared.h"

namespace OpenTune {

namespace {

constexpr int kVibratoEdgeThresholdPx = 6;
constexpr float kVibratoRateHzPerPixel = 0.045f;
constexpr float kVibratoDepthPercentPerPixel = 0.4f;

inline float effectiveVibratoRateHz(const Note& note, float globalRate)
{
    return note.vibratoRate >= 0.0f ? note.vibratoRate : globalRate;
}

inline float effectiveVibratoDepthPercent(const Note& note, float globalDepth)
{
    return note.vibratoDepth >= 0.0f ? note.vibratoDepth : globalDepth;
}

} // namespace

void PianoRollToolHandler::handleVibratoToolMouseMove(const juce::MouseEvent& e)
{
    if (!ctx_.getNoteScreenBounds)
    {
        ctx_.setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    auto& notes = ctx_.getNotes();
    Note* edgeNote = nullptr;
    NoteResizeEdge picked = NoteResizeEdge::None;
    const bool hit = pickVibratoToolEdgeHit(
        notes,
        e.x,
        e.y,
        kVibratoEdgeThresholdPx,
        [this](Note& note, int& x1, int& x2, float& y, float& h) {
            return ctx_.getNoteScreenBounds(note, x1, x2, y, h);
        },
        edgeNote,
        picked);

    if (hit && edgeNote != nullptr)
    {
        if (picked == NoteResizeEdge::Left || picked == NoteResizeEdge::Right)
            ctx_.setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else
            ctx_.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    ctx_.setMouseCursor(juce::MouseCursor::NormalCursor);
}

void PianoRollToolHandler::handleVibratoToolMouseDown(const juce::MouseEvent& e)
{
    if (!ctx_.getNoteScreenBounds || !ctx_.vibratoToolApplyLive)
        return;

    auto& notes = ctx_.getNotes();
    Note* edgeNote = nullptr;
    NoteResizeEdge picked = NoteResizeEdge::None;
    const bool hit = pickVibratoToolEdgeHit(
        notes,
        e.x,
        e.y,
        kVibratoEdgeThresholdPx,
        [this](Note& note, int& x1, int& x2, float& y, float& h) {
            return ctx_.getNoteScreenBounds(note, x1, x2, y, h);
        },
        edgeNote,
        picked);

    if (!hit || edgeNote == nullptr)
        return;

    ctx_.deselectAllNotes();
    edgeNote->selected = true;
    updateF0SelectionFromNotes();

    auto& vd = ctx_.getState().vibratoToolDrag;
    vd.clear();
    vd.active = true;
    vd.note = edgeNote;
    vd.edge = picked;
    vd.initialRateHz = effectiveVibratoRateHz(*edgeNote, ctx_.getVibratoRate());
    vd.initialDepthPercent = effectiveVibratoDepthPercent(*edgeNote, ctx_.getVibratoDepth());

    ctx_.requestRepaint();
}

void PianoRollToolHandler::handleVibratoToolMouseDrag(const juce::MouseEvent& e)
{
    auto& vd = ctx_.getState().vibratoToolDrag;
    if (!vd.active || vd.note == nullptr || !ctx_.vibratoToolApplyLive)
        return;

    const bool adjustRate = (vd.edge == NoteResizeEdge::Left || vd.edge == NoteResizeEdge::Right);
    float value = 0.0f;
    if (adjustRate)
        value = vd.initialRateHz + static_cast<float>(e.x - dragStartPos_.x) * kVibratoRateHzPerPixel;
    else
        value = vd.initialDepthPercent + static_cast<float>(dragStartPos_.y - e.y) * kVibratoDepthPercentPerPixel;

    const int dx = e.x - dragStartPos_.x;
    const int dy = e.y - dragStartPos_.y;
    const bool moved = (dx * dx + dy * dy) > 2;

    if (moved && !vd.undoOpen && ctx_.vibratoToolBeginUndo)
    {
        vd.undoOpen = true;
        ctx_.vibratoToolBeginUndo(adjustRate ? "Vibrato Rate" : "Vibrato Depth");
    }

    if (vd.undoOpen)
        ctx_.vibratoToolApplyLive(vd.note, adjustRate, value);
}

void PianoRollToolHandler::handleVibratoToolMouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    auto& vd = ctx_.getState().vibratoToolDrag;
    if (vd.undoOpen && ctx_.vibratoToolCommitUndo)
        ctx_.vibratoToolCommitUndo();
    vd.clear();
}

} // namespace OpenTune
