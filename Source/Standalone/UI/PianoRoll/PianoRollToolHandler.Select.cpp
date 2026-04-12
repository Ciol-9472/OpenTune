#include "PianoRollToolHandler.h"
#include "PianoRollToolHandler.Shared.h"
#include "../../../Utils/AppLogger.h"

#include <algorithm>
#include <climits>

namespace OpenTune {

using ManualOp = PianoRollToolHandler::ManualCorrectionOp;

void PianoRollToolHandler::handleDeleteKey()
{
    AppLogger::debug("[PianoRollToolHandler] handleDeleteKey: starting delete operation");

    int globalDirtyStartFrame = INT_MAX;
    int globalDirtyEndFrame = INT_MIN;

    struct CorrectionDigest
    {
        int segCount = 0;
        int64_t totalSpan = 0;
        uint64_t checksum = 1469598103934665603ull;
        bool operator!=(const CorrectionDigest& other) const
        {
            return segCount != other.segCount || totalSpan != other.totalSpan || checksum != other.checksum;
        }
    };

    auto buildCorrectionDigest = [this]() -> CorrectionDigest {
        CorrectionDigest d;
        auto curve = ctx_.getPitchCurve();
        if (!curve)
            return d;
        auto snapshot = curve->getSnapshot();
        const auto& segs = snapshot->getCorrectedSegments();
        d.segCount = static_cast<int>(segs.size());
        for (const auto& seg : segs)
        {
            const uint64_t span = static_cast<uint64_t>(std::max(0, seg.endFrame - seg.startFrame));
            d.totalSpan += static_cast<int64_t>(span);
            d.checksum ^= static_cast<uint64_t>(seg.startFrame + 0x9e3779b9);
            d.checksum *= 1099511628211ull;
            d.checksum ^= static_cast<uint64_t>(seg.endFrame + 0x9e3779b9);
            d.checksum *= 1099511628211ull;
            d.checksum ^= static_cast<uint64_t>(seg.f0Data.size() + 0x9e3779b9);
            d.checksum *= 1099511628211ull;
            d.checksum ^= static_cast<uint64_t>(seg.source);
            d.checksum *= 1099511628211ull;
        }
        return d;
    };

    const CorrectionDigest correctionBeforeDelete = buildCorrectionDigest();

    auto selectedNotes = ctx_.getSelectedNotes();
    if (selectedNotes.empty())
    {
        AppLogger::debug("[PianoRollToolHandler] handleDeleteKey: nothing to delete");
        return;
    }

    AppLogger::debug("[PianoRollToolHandler] handleDeleteKey: deleting "
        + juce::String(selectedNotes.size()) + " notes");

    auto curve = ctx_.getPitchCurve();

    ctx_.beginEditTransaction("Delete Notes");

    {
        AppLogger::debug("[PianoRollToolHandler] handleDeleteKey: deleting notes");

        double deletedNotesStartTime = 1e30;
        double deletedNotesEndTime = -1e30;
        for (const auto* n : selectedNotes)
        {
            deletedNotesStartTime = std::min(deletedNotesStartTime, n->startTime);
            deletedNotesEndTime = std::max(deletedNotesEndTime, n->endTime);
        }

        if (curve && deletedNotesEndTime > deletedNotesStartTime)
        {
            const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
            int noteStartFrame = static_cast<int>(deletedNotesStartTime / frameDuration);
            int noteEndFrameExclusive = static_cast<int>(deletedNotesEndTime / frameDuration);
            if (noteEndFrameExclusive > noteStartFrame)
            {
                ctx_.clearCorrectionRange(noteStartFrame, noteEndFrameExclusive);
                globalDirtyStartFrame = std::min(globalDirtyStartFrame, noteStartFrame);
                globalDirtyEndFrame = std::max(globalDirtyEndFrame, noteEndFrameExclusive - 1);
            }
        }

        deleteSelectedNotes();
    }

    ctx_.commitEditTransaction();
    AppLogger::debug("[PianoRollToolHandler] handleDeleteKey: delete completed, notifying pitch curve edit");
    const CorrectionDigest correctionAfterDelete = buildCorrectionDigest();
    const bool correctionChanged = (correctionBeforeDelete != correctionAfterDelete);

    if (correctionChanged && globalDirtyEndFrame >= globalDirtyStartFrame && globalDirtyStartFrame != INT_MAX)
        ctx_.notifyPitchCurveEdited(globalDirtyStartFrame, globalDirtyEndFrame);
}

void PianoRollToolHandler::cancelDrag()
{
    AppLogger::debug("[PianoRollToolHandler] cancelDrag: canceling all drag operations");
    ctx_.getState().selection.isSelectingArea = false;
    ctx_.getState().selection.marqueeAdditive = false;
    ctx_.getState().selection.marqueeBaseSelected.clear();
    ctx_.getState().noteResize.isResizing = false;
    ctx_.getState().noteResize.isDirty = false;
    ctx_.getState().noteResize.note = nullptr;
    ctx_.getState().noteResize.edge = NoteResizeEdge::None;
    ctx_.getState().noteDrag.draggedNote = nullptr;
    ctx_.getState().noteDrag.initialNoteOffsets.clear();
    ctx_.getState().noteDrag.isDraggingNotes = false;
    if (ctx_.setPitchPreview) ctx_.setPitchPreview(false, 0.0f);
}

void PianoRollToolHandler::prepareNoteDragForSelectedNotes(Note* primaryNote)
{
    if (primaryNote == nullptr)
        return;

    ctx_.getState().noteDrag.draggedNote = primaryNote;

    ctx_.getState().noteDrag.initialNoteOffsets.clear();
    auto selectedNotes = ctx_.getSelectedNotes();
    for (auto* note : selectedNotes)
        ctx_.getState().noteDrag.initialNoteOffsets.push_back({ note, note->pitchOffset });

    ctx_.setNoteDragManualStartTime(-1.0);
    ctx_.setNoteDragManualEndTime(-1.0);
    ctx_.getNoteDragInitialManualTargets().clear();

    auto pitchCurve = ctx_.getPitchCurve();
    if (pitchCurve && !ctx_.getState().noteDrag.initialNoteOffsets.empty())
    {
        double rangeStart = 1e30;
        double rangeEnd = -1e30;
        for (const auto& p : ctx_.getState().noteDrag.initialNoteOffsets)
        {
            rangeStart = std::min(rangeStart, p.first->startTime);
            rangeEnd = std::max(rangeEnd, p.first->endTime);
        }

        if (rangeStart < 0)
            rangeStart = 0;

        auto* audioBuffer = ctx_.getAudioBuffer();
        if (audioBuffer)
        {
            double maxTime = static_cast<double>(audioBuffer->getNumSamples()) / ctx_.getAudioSampleRate();
            rangeEnd = std::min(rangeEnd, maxTime);
        }
        if (rangeEnd < rangeStart)
            std::swap(rangeStart, rangeEnd);

        const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
        int startFrame = static_cast<int>(rangeStart / frameDuration);
        int endFrame = static_cast<int>(rangeEnd / frameDuration);
        if (startFrame < 0)
            startFrame = 0;
        if (endFrame < startFrame)
            endFrame = startFrame;

        auto originalF0 = ctx_.getOriginalF0();
        int maxFrame = static_cast<int>(originalF0.size());
        if (maxFrame > 0)
        {
            startFrame = std::max(0, std::min(startFrame, maxFrame));
            endFrame = std::max(0, std::min(endFrame, maxFrame));

            if (endFrame > startFrame && pitchCurve->hasCorrectionInRange(startFrame, endFrame))
            {
                int rangeSize = endFrame - startFrame;
                std::vector<float> renderedF0(rangeSize, -1.0f);

                pitchCurve->renderF0Range(startFrame, endFrame,
                    [&](int frameIndex, const float* data, int length) {
                        if (data == nullptr || length <= 0)
                            return;

                        int relStart = frameIndex - startFrame;
                        int copyOffset = 0;
                        if (relStart < 0)
                        {
                            copyOffset = -relStart;
                            relStart = 0;
                        }

                        if (relStart >= rangeSize || copyOffset >= length)
                            return;

                        int copyLength = std::min(length - copyOffset, rangeSize - relStart);
                        if (copyLength <= 0)
                            return;

                        std::copy_n(data + copyOffset, copyLength, renderedF0.begin() + relStart);
                    });

                ctx_.setNoteDragManualStartTime(static_cast<double>(startFrame) * frameDuration);
                ctx_.setNoteDragManualEndTime(static_cast<double>(endFrame) * frameDuration);

                auto& manualTargets = ctx_.getNoteDragInitialManualTargets();
                for (int relIdx = 0; relIdx < rangeSize; ++relIdx)
                {
                    int f = startFrame + relIdx;
                    float v = renderedF0[static_cast<size_t>(relIdx)];
                    if (v <= 0.0f)
                        continue;
                    manualTargets.push_back({ static_cast<double>(f) * frameDuration, v });
                }

                if (manualTargets.empty())
                {
                    ctx_.setNoteDragManualStartTime(-1.0);
                    ctx_.setNoteDragManualEndTime(-1.0);
                }
            }
        }
    }
}

void PianoRollToolHandler::handleSelectTool(const juce::MouseEvent& e)
{
    AppLogger::debug("[PianoRollToolHandler] handleSelectTool: pos=(" + juce::String(e.x) + "," + juce::String(e.y) + ")");

    ctx_.getState().noteDrag.isDraggingNotes = false;
    ctx_.getState().noteResize.isResizing = false;
    ctx_.getState().noteResize.note = nullptr;
    ctx_.getState().noteResize.edge = NoteResizeEdge::None;

    double clickedTime = ctx_.xToTime(e.x);
    double offsetSeconds = ctx_.getTrackOffsetSeconds();
    double trackRelativeTime = clickedTime - offsetSeconds;

    float clickedPitch = ctx_.yToFreq(static_cast<float>(e.y));
    Note* clickedNote = nullptr;
    if (trackRelativeTime >= 0)
        clickedNote = ctx_.findNoteAt(trackRelativeTime, clickedPitch, 100.0f);

    bool isCtrlDown = e.mods.isCtrlDown() || e.mods.isCommandDown();
    int edgeThreshold = 6;
    float mouseMidi = 69.0f + 12.0f * std::log2(clickedPitch / 440.0f) - 0.5f;
    bool isShiftDown = e.mods.isShiftDown();

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

        ctx_.requestRepaint();
        return;
    }

    if (clickedNote)
    {
        if (isCtrlDown)
        {
            clickedNote->selected = !clickedNote->selected;
            updateF0SelectionFromNotes();
        }
        else if (isShiftDown)
        {
            Note* lastSelected = findLastSelectedNote();
            if (lastSelected && lastSelected != clickedNote)
                selectNotesBetween(lastSelected, clickedNote);
            else
                clickedNote->selected = true;
            updateF0SelectionFromNotes();
        }
        else if (!clickedNote->selected)
        {
            ctx_.deselectAllNotes();
            clickedNote->selected = true;
            updateF0SelectionFromNotes();
        }

        if (clickedNote->selected)
            prepareNoteDragForSelectedNotes(clickedNote);
        else
        {
            ctx_.getState().noteDrag.draggedNote = nullptr;
            ctx_.getState().noteDrag.initialNoteOffsets.clear();
        }

        ctx_.requestRepaint();
    }
    else
    {
        ctx_.getState().noteDrag.draggedNote = nullptr;
        ctx_.getState().noteDrag.initialNoteOffsets.clear();
        ctx_.getState().noteResize.isResizing = false;
        ctx_.getState().noteResize.note = nullptr;
        ctx_.getState().noteResize.edge = NoteResizeEdge::None;

        const bool additiveMarquee = isCtrlDown || isShiftDown;
        ctx_.getState().selection.marqueeAdditive = false;
        ctx_.getState().selection.marqueeBaseSelected.clear();

        if (!additiveMarquee)
        {
            ctx_.deselectAllNotes();
            updateF0SelectionFromNotes();
        }
        else
        {
            for (auto& note : notes)
                if (note.selected)
                    ctx_.getState().selection.marqueeBaseSelected.push_back(&note);
        }

        if (e.x > ctx_.getPianoKeyWidth())
        {
            ctx_.getState().selection.isSelectingArea = true;
            ctx_.getState().selection.marqueeAdditive = additiveMarquee;
            ctx_.getState().selection.dragStartTime = std::max(0.0, trackRelativeTime);
            ctx_.getState().selection.dragEndTime = ctx_.getState().selection.dragStartTime;
            float midiVal = 69.0f + 12.0f * std::log2(clickedPitch / 440.0f) - 0.5f;
            ctx_.getState().selection.dragStartMidi = midiVal;
            ctx_.getState().selection.dragEndMidi = midiVal;
        }
        else
        {
            ctx_.getState().selection.isSelectingArea = false;
            ctx_.getState().selection.marqueeAdditive = false;
            ctx_.getState().selection.marqueeBaseSelected.clear();
        }

        ctx_.requestRepaint();
    }
}

void PianoRollToolHandler::handleSelectDrag(const juce::MouseEvent& e)
{
    if (ctx_.getState().selection.isSelectingArea)
    {
        double offsetSeconds = ctx_.getTrackOffsetSeconds();
        double currentTime = ctx_.xToTime(e.x) - offsetSeconds;
        ctx_.getState().selection.dragEndTime = std::max(0.0, currentTime);

        float currentMidi = 69.0f + 12.0f * std::log2(ctx_.yToFreq(static_cast<float>(e.y)) / 440.0f) - 0.5f;
        ctx_.getState().selection.dragEndMidi = currentMidi;

        double selStartTime = std::min(ctx_.getState().selection.dragStartTime, ctx_.getState().selection.dragEndTime);
        double selEndTime = std::max(ctx_.getState().selection.dragStartTime, ctx_.getState().selection.dragEndTime);
        float selMinMidi = std::min(ctx_.getState().selection.dragStartMidi, ctx_.getState().selection.dragEndMidi);
        float selMaxMidi = std::max(ctx_.getState().selection.dragStartMidi, ctx_.getState().selection.dragEndMidi);

        auto& notes = ctx_.getNotes();
        const bool additive = ctx_.getState().selection.marqueeAdditive;
        const auto& baseSel = ctx_.getState().selection.marqueeBaseSelected;
        for (auto& note : notes)
        {
            float noteMidi = 69.0f + 12.0f * std::log2(note.getAdjustedPitch() / 440.0f) - 0.5f;
            bool timeOverlap = (note.endTime > selStartTime && note.startTime < selEndTime);
            bool pitchOverlap = (noteMidi >= selMinMidi - 0.5f && noteMidi <= selMaxMidi + 0.5f);
            const bool inBox = timeOverlap && pitchOverlap;
            if (additive)
            {
                const bool inBase = std::find(baseSel.begin(), baseSel.end(), &note) != baseSel.end();
                note.selected = inBase || inBox;
            }
            else
            {
                note.selected = inBox;
            }
        }

        ctx_.requestRepaint();
        return;
    }

    if (ctx_.getState().noteResize.isResizing && ctx_.getState().noteResize.note)
    {
        if (!ctx_.getState().noteResize.isDirty)
        {
            ctx_.getState().noteResize.isDirty = true;
            ctx_.beginEditTransaction("Resize Note");
        }

        double offsetSeconds = ctx_.getTrackOffsetSeconds();
        double currentTime = ctx_.xToTime(e.x) - offsetSeconds;
        currentTime = std::max(0.0, currentTime);
        constexpr double kMinNoteDuration = 0.02;
        auto& notes = ctx_.getNotes();
        applyNoteEdgeResizeWithNeighborTrim(
            notes,
            ctx_.getState().noteResize.note,
            ctx_.getState().noteResize.edge,
            currentTime,
            kMinNoteDuration);
        return;
    }

    if (ctx_.getState().noteDrag.draggedNote)
    {
        if (ctx_.getState().noteDrag.initialNoteOffsets.empty())
            return;

        if (!ctx_.getState().noteDrag.isDraggingNotes)
        {
            ctx_.getState().noteDrag.isDraggingNotes = true;
            ctx_.beginEditTransaction("Move Notes");
        }

        float startF0 = ctx_.yToFreq(static_cast<float>(dragStartPos_.y));
        float currentF0 = ctx_.yToFreq(static_cast<float>(e.y));

        float deltaSemitones = 0.0f;
        if (startF0 > 0.0f && currentF0 > 0.0f)
            deltaSemitones = 12.0f * std::log2(currentF0 / startF0);

        for (auto& pair : ctx_.getState().noteDrag.initialNoteOffsets)
        {
            Note* note = pair.first;
            float initialOffset = pair.second;
            int baseMidi = note->getBaseMidiNote();
            float targetMidi = static_cast<float>(baseMidi) + initialOffset + deltaSemitones;
            float snappedOffset = std::round(targetMidi) - static_cast<float>(baseMidi);
            note->pitchOffset = snappedOffset;
            note->dirty = true;
        }

        float appliedDeltaSemitones = 0.0f;
        if (!ctx_.getState().noteDrag.initialNoteOffsets.empty())
        {
            appliedDeltaSemitones = ctx_.getState().noteDrag.initialNoteOffsets[0].first->pitchOffset
                - ctx_.getState().noteDrag.initialNoteOffsets[0].second;
        }
        float shiftFactor = std::pow(2.0f, appliedDeltaSemitones / 12.0f);

        for (auto& pair : ctx_.getState().noteDrag.initialNoteOffsets)
        {
            float newPip = ctx_.recalculatePIP(*pair.first);
            if (newPip > 0.0f)
                pair.first->originalPitch = newPip;
        }

        const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
        auto& manualTargets = ctx_.getNoteDragInitialManualTargets();
        double manualStartTime = ctx_.getNoteDragManualStartTime();
        double manualEndTime = ctx_.getNoteDragManualEndTime();
        int manualStartFrame = static_cast<int>(manualStartTime / frameDuration);
        int manualEndFrame = static_cast<int>(manualEndTime / frameDuration);
        if (manualStartTime >= 0.0 && manualEndTime > manualStartTime && manualEndFrame > manualStartFrame && !manualTargets.empty())
        {
            int rangeSize = manualEndFrame - manualStartFrame;
            std::vector<float> shiftedF0(rangeSize, -1.0f);

            for (const auto& fv : manualTargets)
            {
                int f = static_cast<int>(std::lround(fv.first / frameDuration));
                int relIdx = f - manualStartFrame;
                if (relIdx >= 0 && relIdx < rangeSize)
                    shiftedF0[static_cast<size_t>(relIdx)] = fv.second * shiftFactor;
            }

            std::vector<ManualOp> ops;
            ManualOp op;
            op.startFrame = manualStartFrame;
            op.endFrameExclusive = manualEndFrame;
            op.f0Data = std::move(shiftedF0);
            op.source = CorrectedSegment::Source::HandDraw;
            ops.push_back(std::move(op));

            ctx_.applyManualCorrection(std::move(ops), manualStartFrame, manualEndFrame - 1, false);
        }

        if (ctx_.setPitchPreview) {
            const auto& offs = ctx_.getState().noteDrag.initialNoteOffsets;
            if (offs.size() == 1 && offs[0].first != nullptr) {
                const float hz = offs[0].first->getAdjustedPitch();
                if (hz > 0.0f) ctx_.setPitchPreview(true, hz);
            } else {
                ctx_.setPitchPreview(false, 0.0f);
            }
        }
    }
    else
    {
        auto selected = ctx_.getSelectedNotes();
        if (!selected.empty())
        {
            ctx_.getState().noteDrag.draggedNote = selected[0];
            ctx_.getState().noteDrag.initialNoteOffsets.clear();
            for (auto* note : selected)
                ctx_.getState().noteDrag.initialNoteOffsets.push_back({ note, note->pitchOffset });
        }
    }
}

void PianoRollToolHandler::handleSelectUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    AppLogger::debug("[PianoRollToolHandler] handleSelectUp: finishing select operation");

    if (ctx_.setPitchPreview) ctx_.setPitchPreview(false, 0.0f);

    bool queuedAsyncCommit = false;

    if (ctx_.getState().noteDrag.draggedNote && ctx_.getState().noteDrag.isDraggingNotes)
    {
        double dirtyStartTime = 1e30;
        double dirtyEndTime = -1e30;

        auto& notes = ctx_.getNotes();
        for (const auto& pair : ctx_.getState().noteDrag.initialNoteOffsets)
        {
            Note* note = pair.first;
            float initialOffset = pair.second;
            float finalOffset = note->pitchOffset;

            if (std::abs(finalOffset - initialOffset) > 0.001f)
            {
                dirtyStartTime = std::min(dirtyStartTime, note->startTime);
                dirtyEndTime = std::max(dirtyEndTime, note->endTime);

                auto it = std::find_if(notes.begin(), notes.end(), [note](const Note& n) { return &n == note; });
                size_t noteIndex = static_cast<size_t>(std::distance(notes.begin(), it));
                ctx_.notifyNoteOffsetChanged(noteIndex, initialOffset, finalOffset);
            }
        }

        bool hasManualTargets = ctx_.getState().noteDrag.isDraggingNotes
            && (ctx_.getNoteDragManualStartTime() >= 0.0
                && ctx_.getNoteDragManualEndTime() > ctx_.getNoteDragManualStartTime()
                && !ctx_.getNoteDragInitialManualTargets().empty());

        if (ctx_.getState().noteDrag.isDraggingNotes && (dirtyEndTime > dirtyStartTime || hasManualTargets))
        {
            const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
            double rangeStartTime = 0.0;
            double rangeEndTime = 0.0;

            if (dirtyEndTime > dirtyStartTime)
            {
                rangeStartTime = dirtyStartTime;
                rangeEndTime = dirtyEndTime;
            }

            if (hasManualTargets)
            {
                double manualStart = ctx_.getNoteDragManualStartTime();
                double manualEnd = ctx_.getNoteDragManualEndTime();
                if (dirtyEndTime > dirtyStartTime)
                {
                    rangeStartTime = std::min(rangeStartTime, manualStart);
                    rangeEndTime = std::max(rangeEndTime, manualEnd);
                }
                else
                {
                    rangeStartTime = manualStart;
                    rangeEndTime = manualEnd;
                }
            }

            int startFrame = static_cast<int>(rangeStartTime / frameDuration);
            int endFrame = static_cast<int>(rangeEndTime / frameDuration);
            if (startFrame < 0)
                startFrame = 0;
            if (endFrame < startFrame)
                endFrame = startFrame;

            auto pitchCurve = ctx_.getPitchCurve();
            if (pitchCurve && !hasManualTargets)
            {
                ctx_.enqueueNoteBasedCorrection(
                    startFrame,
                    endFrame + 1,
                    ctx_.getRetuneSpeed(),
                    ctx_.getVibratoDepth(),
                    ctx_.getVibratoRate());
                queuedAsyncCommit = true;
            }
            else
            {
                ctx_.notifyPitchCurveEdited(startFrame, endFrame);
                ctx_.commitEditTransaction();
            }
        }

        ctx_.getState().noteDrag.initialNoteOffsets.clear();
        ctx_.setNoteDragManualStartTime(-1.0);
        ctx_.setNoteDragManualEndTime(-1.0);
        ctx_.getNoteDragInitialManualTargets().clear();
    }

    bool resizeWasDirty = false;
    double resizedStartTime = 0;
    double resizedEndTime = 0;
    if (ctx_.getState().noteResize.isResizing && ctx_.getState().noteResize.note != nullptr && ctx_.getState().noteResize.isDirty)
    {
        resizeWasDirty = ctx_.getState().noteResize.note->dirty;
        resizedStartTime = ctx_.getState().noteResize.note->startTime;
        resizedEndTime = ctx_.getState().noteResize.note->endTime;
    }

    auto& notes = ctx_.getNotes();
    notes.erase(
        std::remove_if(notes.begin(), notes.end(), [](const Note& n) {
            return n.startTime >= n.endTime;
        }),
        notes.end());

    if (ctx_.getState().noteResize.isResizing && resizeWasDirty)
    {
        double dirtyStartTime = std::min(ctx_.getState().noteResize.originalStartTime, resizedStartTime);
        double dirtyEndTime = std::max(ctx_.getState().noteResize.originalEndTime, resizedEndTime);
        const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
        int startFrame = static_cast<int>(dirtyStartTime / frameDuration);
        int endFrame = static_cast<int>(dirtyEndTime / frameDuration);
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
            queuedAsyncCommit = true;
        }
        else if (ctx_.isTransactionActive())
        {
            ctx_.commitEditTransaction();
        }
    }

    if (!queuedAsyncCommit && ctx_.isTransactionActive())
        ctx_.commitEditTransaction();

    ctx_.getState().noteResize.isResizing = false;
    ctx_.getState().noteResize.isDirty = false;
    ctx_.getState().noteResize.note = nullptr;
    ctx_.getState().noteResize.edge = NoteResizeEdge::None;

    if (ctx_.getState().selection.isSelectingArea)
    {
        const bool wasMarqueeAdditive = ctx_.getState().selection.marqueeAdditive;
        ctx_.getState().selection.isSelectingArea = false;
        ctx_.getState().selection.marqueeAdditive = false;
        ctx_.getState().selection.marqueeBaseSelected.clear();

        double timeDelta = std::abs(ctx_.getState().selection.dragEndTime - ctx_.getState().selection.dragStartTime);
        float midiDelta = std::abs(ctx_.getState().selection.dragEndMidi - ctx_.getState().selection.dragStartMidi);
        if ((timeDelta < 0.01 || midiDelta < 0.5f) && !wasMarqueeAdditive)
            ctx_.deselectAllNotes();

        auto selectedNotes = ctx_.getSelectedNotes();
        if (!selectedNotes.empty())
        {
            ctx_.getState().noteDrag.initialNoteOffsets.clear();
            for (auto* note : selectedNotes)
                ctx_.getState().noteDrag.initialNoteOffsets.push_back({ note, note->pitchOffset });
        }
        updateF0SelectionFromNotes();
    }

    ctx_.getState().noteDrag.draggedNote = nullptr;
}

void PianoRollToolHandler::deleteSelectedNotes()
{
    auto& notes = ctx_.getNotes();
    int beforeCount = static_cast<int>(notes.size());
    notes.erase(
        std::remove_if(notes.begin(), notes.end(), [](const Note& n) {
            return n.selected;
        }),
        notes.end());
    int afterCount = static_cast<int>(notes.size());
    AppLogger::debug("[PianoRollToolHandler] deleteSelectedNotes: removed "
        + juce::String(beforeCount - afterCount) + " notes");
}

Note* PianoRollToolHandler::findLastSelectedNote()
{
    auto selectedNotes = ctx_.getSelectedNotes();
    if (selectedNotes.empty())
        return nullptr;

    Note* lastSelected = selectedNotes[0];
    for (auto* note : selectedNotes)
    {
        if (note->startTime > lastSelected->startTime)
            lastSelected = note;
    }
    return lastSelected;
}

void PianoRollToolHandler::selectNotesBetween(Note* start, Note* end)
{
    if (!start || !end)
        return;

    auto& notes = ctx_.getNotes();
    double minTime = std::min(start->startTime, end->startTime);
    double maxTime = std::max(start->startTime, end->startTime);

    for (auto& note : notes)
    {
        if (note.startTime >= minTime && note.startTime <= maxTime)
            note.selected = true;
    }
}

void PianoRollToolHandler::updateF0SelectionFromNotes()
{
    auto selectedNotes = ctx_.getSelectedNotes();
    if (selectedNotes.empty())
    {
        ctx_.getState().selection.clearF0Selection();
        return;
    }

    double minStart = 1e30;
    double maxEnd = -1e30;
    for (auto* note : selectedNotes)
    {
        minStart = std::min(minStart, note->startTime);
        maxEnd = std::max(maxEnd, note->endTime);
    }

    const double frameDuration = static_cast<double>(ctx_.getCurveHopSize()) / ctx_.getCurveSampleRate();
    int startFrame = static_cast<int>(minStart / frameDuration);
    int endFrame = static_cast<int>(maxEnd / frameDuration);

    ctx_.getState().selection.setF0Range(startFrame, endFrame);
}

} // namespace OpenTune
