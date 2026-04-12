#include "PianoRollToolHandler.h"
#include "PianoRollToolHandler.Shared.h"
#include "../../../Utils/AppLogger.h"

#include <algorithm>
#include <cmath>

namespace OpenTune {

using ManualOp = PianoRollToolHandler::ManualCorrectionOp;

void PianoRollToolHandler::handleDrawCurveTool(const juce::MouseEvent& e)
{
    auto pitchCurve = ctx_.getPitchCurve();
    if (!pitchCurve)
    {
        AppLogger::warn("[PianoRollToolHandler] handleDrawCurveTool: pitchCurve is null");
        return;
    }

    double curveTime = ctx_.xToTime(e.x);
    auto* audioBuffer = ctx_.getAudioBuffer();
    if (audioBuffer != nullptr)
    {
        double maxTime = static_cast<double>(audioBuffer->getNumSamples()) / ctx_.getAudioSampleRate();
        curveTime = juce::jlimit(0.0, maxTime, curveTime);
    }

    float targetF0 = ctx_.yToFreq(static_cast<float>(e.y));

    const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
    int frameIndex = static_cast<int>(curveTime / frameDuration);
    const auto& originalF0 = ctx_.getOriginalF0();
    if (frameIndex < 0 || static_cast<size_t>(frameIndex) >= originalF0.size())
    {
        AppLogger::debug("[PianoRollToolHandler] handleDrawCurveTool: frameIndex out of range");
        return;
    }

    if (!ctx_.getState().drawing.isDrawingF0)
    {
        AppLogger::debug("[PianoRollToolHandler] handleDrawCurveTool: starting new curve draw at frame=" + juce::String(frameIndex));
        ctx_.getState().drawing.isDrawingF0 = true;
        ctx_.setDirtyStartTime(curveTime);
        ctx_.setDirtyEndTime(curveTime);
        lastDrawPoint_ = juce::Point<float>(static_cast<float>(curveTime), targetF0);

        auto& handDrawBuffer = ctx_.getState().drawing.handDrawBuffer;
        handDrawBuffer.clear();
        handDrawBuffer.resize(originalF0.size(), -1.0f);

        ctx_.beginEditTransaction("Draw F0 Curve");
    }
    else
    {
        AppLogger::debug("[PianoRollToolHandler] handleDrawCurveTool: continuing draw at frame="
            + juce::String(frameIndex) + ", f0=" + juce::String(targetF0));
    }

    auto& handDrawBuffer = ctx_.getState().drawing.handDrawBuffer;
    double lastTime = static_cast<double>(lastDrawPoint_.x);

    auto writeFrame = [&](int f, float v) -> void {
        if (f < 0 || static_cast<size_t>(f) >= originalF0.size())
            return;

        handDrawBuffer[static_cast<size_t>(f)] = v;
        double frameTime = static_cast<double>(f) * frameDuration;
        double dirtyStart = ctx_.getDirtyStartTime();
        double dirtyEnd = ctx_.getDirtyEndTime();
        dirtyStart = (dirtyStart < 0.0) ? frameTime : std::min(dirtyStart, frameTime);
        dirtyEnd = (dirtyEnd < 0.0) ? frameTime : std::max(dirtyEnd, frameTime);
        ctx_.setDirtyStartTime(dirtyStart);
        ctx_.setDirtyEndTime(dirtyEnd);
    };

    int lastFrame = static_cast<int>(lastTime / frameDuration);
    float lastF0 = lastDrawPoint_.y;
    writeFrame(frameIndex, targetF0);

    int startFrame = std::min(lastFrame, frameIndex);
    int endFrame = std::max(lastFrame, frameIndex);

    if (endFrame > startFrame && lastF0 > 0.0f && targetF0 > 0.0f)
    {
        float logA = std::log2(lastF0);
        float logB = std::log2(targetF0);
        for (int f = startFrame + 1; f < endFrame; ++f)
        {
            float t = static_cast<float>(f - startFrame) / static_cast<float>(endFrame - startFrame);
            float logV = logA + (logB - logA) * t;
            float v = std::pow(2.0f, logV);
            writeFrame(f, v);
        }
    }

    lastDrawPoint_ = juce::Point<float>(static_cast<float>(curveTime), targetF0);
    ctx_.requestRepaint();
}

void PianoRollToolHandler::handleDrawNoteMouseDown(const juce::MouseEvent& e)
{
    AppLogger::debug("[PianoRollToolHandler] handleDrawNoteMouseDown: pos=(" + juce::String(e.x) + "," + juce::String(e.y) + ")");

    ctx_.getState().noteDrag.isDraggingNotes = false;
    ctx_.getState().noteResize.isResizing = false;
    ctx_.getState().noteResize.isDirty = false;
    ctx_.getState().noteResize.note = nullptr;
    ctx_.getState().noteResize.edge = NoteResizeEdge::None;

    const double clickedTime = ctx_.xToTime(e.x);
    const double offsetSeconds = ctx_.getTrackOffsetSeconds();
    const double trackRelativeTime = clickedTime - offsetSeconds;
    const float clickedPitch = ctx_.yToFreq(static_cast<float>(e.y));

    Note* existingNote = nullptr;
    if (trackRelativeTime >= 0.0 && e.x > ctx_.getPianoKeyWidth())
        existingNote = ctx_.findNoteAt(trackRelativeTime, clickedPitch, 100.0f);

    if (existingNote != nullptr)
    {
        const bool isCtrlDown = e.mods.isCtrlDown() || e.mods.isCommandDown();
        const bool isShiftDown = e.mods.isShiftDown();
        constexpr int edgeThreshold = 6;
        const float mouseMidi = 69.0f + 12.0f * std::log2(clickedPitch / 440.0f) - 0.5f;

        auto& notes = ctx_.getNotes();
        Note* edgeNote = nullptr;
        NoteResizeEdge pickedEdge = NoteResizeEdge::None;
        if (pickBestNoteEdgeHit(
                notes,
                e.x,
                mouseMidi,
                edgeThreshold,
                offsetSeconds,
                [&](double t) { return ctx_.timeToX(t); },
                edgeNote,
                pickedEdge)
            && edgeNote != nullptr)
        {
            ctx_.getState().noteResize.isResizing = true;
            ctx_.getState().noteResize.isDirty = false;
            ctx_.getState().noteResize.note = edgeNote;
            ctx_.getState().noteResize.edge = pickedEdge;
            ctx_.getState().noteResize.originalStartTime = edgeNote->startTime;
            ctx_.getState().noteResize.originalEndTime = edgeNote->endTime;

            if (!edgeNote->selected && !isCtrlDown && !isShiftDown)
                ctx_.deselectAllNotes();
            edgeNote->selected = true;

            ctx_.getState().noteDrag.draggedNote = nullptr;
            ctx_.getState().noteDrag.initialNoteOffsets.clear();
            ctx_.getState().noteDrag.isDraggingNotes = false;
            ctx_.setDrawNoteToolPendingDrag(false);
            updateF0SelectionFromNotes();
            ctx_.requestRepaint();
            return;
        }

        if (isCtrlDown)
            existingNote->selected = !existingNote->selected;
        else if (!existingNote->selected)
        {
            ctx_.deselectAllNotes();
            existingNote->selected = true;
        }

        updateF0SelectionFromNotes();

        if (existingNote->selected)
            prepareNoteDragForSelectedNotes(existingNote);
        else
        {
            ctx_.getState().noteDrag.draggedNote = nullptr;
            ctx_.getState().noteDrag.initialNoteOffsets.clear();
        }

        ctx_.setDrawNoteToolPendingDrag(false);
        ctx_.requestRepaint();
        return;
    }

    ctx_.getState().noteDrag.draggedNote = nullptr;
    ctx_.getState().noteDrag.initialNoteOffsets.clear();
    ctx_.setDrawNoteToolPendingDrag(true);
    ctx_.setDrawNoteToolMouseDownPos(e.getPosition());
}

void PianoRollToolHandler::handleSplitNoteTool(const juce::MouseEvent& e)
{
    if (e.x <= ctx_.getPianoKeyWidth())
        return;

    const double offsetSeconds = ctx_.getTrackOffsetSeconds();
    const double clickedTime = ctx_.xToTime(e.x) - offsetSeconds;
    if (clickedTime < 0.0)
        return;

    const float clickedPitch = ctx_.yToFreq(static_cast<float>(e.y));
    Note* target = ctx_.findNoteAt(clickedTime, clickedPitch, 100.0f);
    if (target == nullptr)
        return;

    constexpr double kMinHalfDuration = 0.02;
    const double splitTime = clickedTime;
    if (splitTime <= target->startTime + kMinHalfDuration
        || splitTime >= target->endTime - kMinHalfDuration)
    {
        return;
    }

    const double origStart = target->startTime;
    const double origEnd = target->endTime;

    ctx_.beginEditTransaction("Split Note");

    Note left = *target;
    Note right = *target;
    left.endTime = splitTime;
    right.startTime = splitTime;
    left.selected = false;
    right.selected = true;
    left.dirty = true;
    right.dirty = true;

    auto& notes = ctx_.getNotes();
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (&notes[i] == target)
        {
            notes[i] = left;
            notes.insert(notes.begin() + static_cast<std::ptrdiff_t>(i) + 1, right);
            break;
        }
    }

    auto pitchCurve = ctx_.getPitchCurve();
    if (pitchCurve)
    {
        const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
        int startFrame = static_cast<int>(origStart / frameDuration);
        int endFrame = static_cast<int>(origEnd / frameDuration);
        if (startFrame < 0)
            startFrame = 0;
        if (endFrame < startFrame)
            endFrame = startFrame;

        ctx_.enqueueNoteBasedCorrection(
            startFrame,
            endFrame + 1,
            ctx_.getRetuneSpeed(),
            ctx_.getVibratoDepth(),
            ctx_.getVibratoRate());
    }
    else
    {
        ctx_.commitEditTransaction();
    }

    ctx_.requestRepaint();
}

void PianoRollToolHandler::handleDrawNoteTool(const juce::MouseEvent& e)
{
    double offsetSeconds = ctx_.getTrackOffsetSeconds();
    double currentTime = ctx_.xToTime(e.x) - offsetSeconds;
    if (currentTime < 0)
        currentTime = 0;

    float targetF0 = ctx_.yToFreq(static_cast<float>(e.y));
    float midiNote = 69.0f + 12.0f * std::log2(targetF0 / 440.0f);
    int roundedMidi = static_cast<int>(std::round(midiNote));
    float snappedF0 = 440.0f * std::pow(2.0f, (roundedMidi - 69) / 12.0f);

    if (!ctx_.getState().drawing.isDrawingNote)
    {
        AppLogger::debug("[PianoRollToolHandler] handleDrawNoteTool: starting note draw, midi="
            + juce::String(roundedMidi) + ", time=" + juce::String(currentTime, 3));
        ctx_.beginEditTransaction("Draw Note");
        ctx_.getState().drawing.isDrawingNote = true;
        ctx_.setDrawingNoteStartTime(currentTime);
        ctx_.setDrawingNoteEndTime(currentTime);
        ctx_.setDrawingNotePitch(snappedF0);

        Note newNote;
        newNote.startTime = currentTime;
        newNote.endTime = currentTime;
        newNote.pitch = snappedF0;
        newNote.pitchOffset = 0.0f;
        newNote.selected = false;
        newNote.dirty = true;

        float newPip = ctx_.recalculatePIP(newNote);

        if (newPip > 0.0f)
        {
            newNote.pitch = Note::midiToFrequency(Note::frequencyToMidi(newPip));
            newNote.originalPitch = newPip;
            int targetMidi = Note::frequencyToMidi(snappedF0);
            int sourceMidi = Note::frequencyToMidi(newNote.pitch);
            newNote.pitchOffset = static_cast<float>(targetMidi - sourceMidi);
        }
        else
        {
            newNote.pitch = snappedF0;
            newNote.originalPitch = snappedF0;
            newNote.pitchOffset = 0.0f;
        }

        auto& notes = ctx_.getNotes();
        notes.push_back(newNote);
        ctx_.setDrawingNoteIndex(static_cast<int>(notes.size()) - 1);

        if (ctx_.setPitchPreview) {
            const float hz = notes.back().getAdjustedPitch();
            if (hz > 0.0f) ctx_.setPitchPreview(true, hz);
        }

        ctx_.requestRepaint();
        return;
    }

    if (ctx_.getDrawingNoteIndex() >= 0)
    {
        ctx_.setDrawingNoteEndTime(currentTime);
        auto& notes = ctx_.getNotes();
        int idx = ctx_.getDrawingNoteIndex();
        if (idx < static_cast<int>(notes.size()))
        {
            double startTime = ctx_.getDrawingNoteStartTime();
            double endTime = ctx_.getDrawingNoteEndTime();
            notes[static_cast<size_t>(idx)].startTime = std::min(startTime, endTime);
            notes[static_cast<size_t>(idx)].endTime = std::max(startTime, endTime);
            notes[static_cast<size_t>(idx)].dirty = true;
        }
    }

    ctx_.requestRepaint();
}

void PianoRollToolHandler::handleAutoTuneTool(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    AppLogger::debug("[PianoRollToolHandler] handleAutoTuneTool: triggering AutoTune");
    ctx_.notifyAutoTuneRequested();
}

void PianoRollToolHandler::handleDrawCurveDrag(const juce::MouseEvent& e)
{
    handleDrawCurveTool(e);
}

void PianoRollToolHandler::handleDrawNoteDrag(const juce::MouseEvent& e)
{
    if (ctx_.getState().noteResize.isResizing && ctx_.getState().noteResize.note != nullptr)
    {
        handleSelectDrag(e);
        return;
    }

    if (ctx_.getState().noteDrag.draggedNote != nullptr && !ctx_.getState().drawing.isDrawingNote)
    {
        handleSelectDrag(e);
        return;
    }

    if (ctx_.getDrawNoteToolPendingDrag())
    {
        int dx = e.x - ctx_.getDrawNoteToolMouseDownPos().x;
        int dy = e.y - ctx_.getDrawNoteToolMouseDownPos().y;
        int threshold = ctx_.getDragThreshold();
        if (dx * dx + dy * dy > threshold * threshold)
        {
            ctx_.setDrawNoteToolPendingDrag(false);
            juce::Point<int> downPos = ctx_.getDrawNoteToolMouseDownPos();
            juce::MouseEvent startEvent = e.withNewPosition(downPos.toFloat());
            handleDrawNoteTool(startEvent);
        }
    }
    else if (ctx_.getState().drawing.isDrawingNote)
    {
        handleDrawNoteTool(e);
    }
}

void PianoRollToolHandler::handleDrawCurveUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    AppLogger::debug("[PianoRollToolHandler] handleDrawCurveUp: finishing curve draw");

    if (ctx_.getState().handDrawPendingDrag)
    {
        ctx_.getState().handDrawPendingDrag = false;
        return;
    }

    if (!ctx_.getState().drawing.isDrawingF0)
        return;

    auto pitchCurve = ctx_.getPitchCurve();
    auto& handDrawBuffer = ctx_.getState().drawing.handDrawBuffer;
    if (pitchCurve && ctx_.getDirtyStartTime() >= 0.0 && ctx_.getDirtyEndTime() >= 0.0 && !handDrawBuffer.empty())
    {
        const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
        int startFrame = static_cast<int>(std::min(ctx_.getDirtyStartTime(), ctx_.getDirtyEndTime()) / frameDuration);
        int endFrame = static_cast<int>(std::max(ctx_.getDirtyStartTime(), ctx_.getDirtyEndTime()) / frameDuration);

        const auto& originalF0 = ctx_.getOriginalF0();
        if (!originalF0.empty())
        {
            int maxFrame = static_cast<int>(originalF0.size()) - 1;
            if (maxFrame >= 0)
            {
                startFrame = juce::jlimit(0, maxFrame, startFrame);
                endFrame = juce::jlimit(0, maxFrame, endFrame);
                if (endFrame < startFrame)
                    std::swap(startFrame, endFrame);
            }
        }

        std::vector<float> drawnF0;
        drawnF0.reserve(endFrame - startFrame + 1);
        for (int f = startFrame; f <= endFrame; ++f)
        {
            if (f >= 0 && f < static_cast<int>(handDrawBuffer.size()))
            {
                float val = handDrawBuffer[static_cast<size_t>(f)];
                drawnF0.push_back(val >= 0.0f ? val : 0.0f);
            }
            else
            {
                drawnF0.push_back(0.0f);
            }
        }

        std::vector<int> affectedNoteIndices;
        std::vector<ManualOp> ops = clipDrawDataToNotes(
            startFrame,
            endFrame + 1,
            drawnF0,
            CorrectedSegment::Source::HandDraw,
            -1.0f,
            affectedNoteIndices);

        if (ops.empty())
        {
            AppLogger::debug("[PianoRollToolHandler] handleDrawCurveUp: no overlapping notes, discarding draw");
            ctx_.commitEditTransaction();
        }
        else
        {
            int globalStartFrame = ops.front().startFrame;
            int globalEndFrame = ops.back().endFrameExclusive - 1;
            for (const auto& op : ops)
            {
                globalStartFrame = std::min(globalStartFrame, op.startFrame);
                globalEndFrame = std::max(globalEndFrame, op.endFrameExclusive - 1);
            }

            ctx_.applyManualCorrection(std::move(ops), globalStartFrame, globalEndFrame, false);
            ctx_.notifyPitchCurveEdited(globalStartFrame, globalEndFrame);
            ctx_.commitEditTransaction();

            ctx_.deselectAllNotes();
            auto& notes = ctx_.getNotes();
            for (int ni : affectedNoteIndices)
            {
                if (ni >= 0 && ni < static_cast<int>(notes.size()))
                    notes[static_cast<size_t>(ni)].selected = true;
            }
            updateF0SelectionFromNotes();
        }
    }

    ctx_.getState().drawing.isDrawingF0 = false;
    ctx_.setDirtyStartTime(-1.0);
    ctx_.setDirtyEndTime(-1.0);
    ctx_.getState().drawing.handDrawBuffer.clear();
}

void PianoRollToolHandler::handleDrawNoteUp(const juce::MouseEvent& e)
{
    AppLogger::debug("[PianoRollToolHandler] handleDrawNoteUp: finishing note draw");

    if (ctx_.setPitchPreview) ctx_.setPitchPreview(false, 0.0f);

    if (ctx_.getDrawNoteToolPendingDrag())
    {
        ctx_.setDrawNoteToolPendingDrag(false);
        return;
    }

    if (ctx_.getState().noteResize.isResizing
        || ctx_.getState().noteDrag.draggedNote != nullptr
        || ctx_.getState().noteDrag.isDraggingNotes)
    {
        handleSelectUp(e);
        return;
    }

    if (!ctx_.getState().drawing.isDrawingNote)
        return;

    ctx_.getState().drawing.isDrawingNote = false;

    double offsetSeconds = ctx_.getTrackOffsetSeconds();
    double releaseTime = ctx_.xToTime(e.x) - offsetSeconds;
    if (releaseTime < 0)
        releaseTime = 0;

    ctx_.setDrawingNoteEndTime(releaseTime);

    double startTime = std::min(ctx_.getDrawingNoteStartTime(), ctx_.getDrawingNoteEndTime());
    double endTime = std::max(ctx_.getDrawingNoteStartTime(), ctx_.getDrawingNoteEndTime());
    double minDuration = 0.02;

    if ((endTime - startTime) < minDuration)
    {
        if (ctx_.getDrawingNoteEndTime() >= ctx_.getDrawingNoteStartTime())
            endTime = startTime + minDuration;
        else
            startTime = endTime - minDuration;

        if (startTime < 0)
        {
            endTime -= startTime;
            startTime = 0;
        }
    }

    auto& notes = ctx_.getNotes();
    if (ctx_.getDrawingNotePitch() > 0.0f)
    {
        std::vector<Note> updatedNotes;
        updatedNotes.reserve(notes.size() + 2);

        for (size_t i = 0; i < notes.size(); ++i)
        {
            if (ctx_.getDrawingNoteIndex() >= 0 && static_cast<int>(i) == ctx_.getDrawingNoteIndex())
                continue;

            const auto& note = notes[i];
            bool overlap = note.endTime > startTime && note.startTime < endTime;
            if (!overlap)
            {
                updatedNotes.push_back(note);
                continue;
            }

            if (note.startTime < startTime)
            {
                Note left = note;
                left.endTime = startTime;
                if (left.endTime > left.startTime)
                    updatedNotes.push_back(left);
            }

            if (note.endTime > endTime)
            {
                Note right = note;
                right.startTime = endTime;
                if (right.endTime > right.startTime)
                    updatedNotes.push_back(right);
            }
        }

        notes = std::move(updatedNotes);

        Note finalNote;
        finalNote.startTime = startTime;
        finalNote.endTime = endTime;
        finalNote.pitch = ctx_.getDrawingNotePitch();
        finalNote.pitchOffset = 0.0f;
        finalNote.retuneSpeed = ctx_.getRetuneSpeed();
        finalNote.vibratoDepth = ctx_.getVibratoDepth();
        finalNote.vibratoRate = ctx_.getVibratoRate();
        finalNote.selected = true;
        finalNote.dirty = true;

        float newPip = ctx_.recalculatePIP(finalNote);
        if (newPip > 0.0f)
        {
            float sourcePitch = Note::midiToFrequency(Note::frequencyToMidi(newPip));
            finalNote.pitch = sourcePitch;
            finalNote.originalPitch = newPip;

            int targetMidi = Note::frequencyToMidi(ctx_.getDrawingNotePitch());
            int sourceMidi = Note::frequencyToMidi(sourcePitch);
            finalNote.pitchOffset = static_cast<float>(targetMidi - sourceMidi);
        }
        else
        {
            finalNote.pitch = ctx_.getDrawingNotePitch();
            finalNote.originalPitch = ctx_.getDrawingNotePitch();
            finalNote.pitchOffset = 0.0f;
        }
        ctx_.insertNoteSorted(finalNote);

        ctx_.deselectAllNotes();
        double midTime = (startTime + endTime) / 2.0;
        auto* newSelected = ctx_.findNoteAt(midTime, finalNote.getAdjustedPitch(), 100.0f);
        if (newSelected != nullptr)
            newSelected->selected = true;
    }

    ctx_.setDrawingNoteIndex(-1);

    {
        const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
        int startFrame = static_cast<int>(startTime / frameDuration);
        int endFrame = static_cast<int>(endTime / frameDuration);
        if (startFrame < 0)
            startFrame = 0;
        if (endFrame < startFrame)
            endFrame = startFrame;

        auto pitchCurve = ctx_.getPitchCurve();
        if (pitchCurve)
        {
            ctx_.enqueueNoteBasedCorrection(
                startFrame,
                endFrame + 1,
                ctx_.getRetuneSpeed(),
                ctx_.getVibratoDepth(),
                ctx_.getVibratoRate());
        }
    }

    if (!ctx_.getPitchCurve())
        ctx_.commitEditTransaction();

    ctx_.requestRepaint();
}

std::vector<ManualOp> PianoRollToolHandler::clipDrawDataToNotes(
    int drawStartFrame,
    int drawEndFrameExclusive,
    const std::vector<float>& drawnF0,
    CorrectedSegment::Source source,
    float retuneSpeed,
    std::vector<int>& affectedNoteIndices) const
{
    std::vector<ManualOp> result;
    affectedNoteIndices.clear();

    const auto& notes = ctx_.getNotes();
    if (notes.empty() || drawnF0.empty())
        return result;

    const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
    if (frameDuration <= 0.0)
        return result;

    for (int ni = 0; ni < static_cast<int>(notes.size()); ++ni)
    {
        const auto& note = notes[static_cast<size_t>(ni)];

        int noteStartFrame = static_cast<int>(note.startTime / frameDuration);
        int noteEndFrame = static_cast<int>(note.endTime / frameDuration);

        int overlapStart = std::max(drawStartFrame, noteStartFrame);
        int overlapEnd = std::min(drawEndFrameExclusive, noteEndFrame);

        if (overlapEnd <= overlapStart)
            continue;

        std::vector<float> clippedF0;
        clippedF0.reserve(overlapEnd - overlapStart);
        for (int f = overlapStart; f < overlapEnd; ++f)
        {
            int idx = f - drawStartFrame;
            if (idx >= 0 && idx < static_cast<int>(drawnF0.size()))
                clippedF0.push_back(drawnF0[static_cast<size_t>(idx)]);
            else
                clippedF0.push_back(0.0f);
        }

        ManualOp op;
        op.startFrame = overlapStart;
        op.endFrameExclusive = overlapEnd;
        op.f0Data = std::move(clippedF0);
        op.source = source;
        op.retuneSpeed = retuneSpeed;
        result.push_back(std::move(op));
        affectedNoteIndices.push_back(ni);
    }

    return result;
}

} // namespace OpenTune
