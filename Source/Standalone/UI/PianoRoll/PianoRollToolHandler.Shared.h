#pragma once

#include "PianoRollToolHandler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace OpenTune {
namespace {

inline std::vector<size_t> sortedNoteIndicesByStart(const std::vector<Note>& notes)
{
    std::vector<size_t> idx(notes.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        if (notes[a].startTime != notes[b].startTime)
            return notes[a].startTime < notes[b].startTime;
        return notes[a].endTime < notes[b].endTime;
    });
    return idx;
}

inline float midiBandForNote(const Note& note)
{
    const float pitch = note.getAdjustedPitch();
    if (pitch <= 0.0f)
        return 0.0f;
    return 69.0f + 12.0f * std::log2(pitch / 440.0f) - 0.5f;
}

inline bool isTimeAdjacentToPrev(const Note& prev, const Note& target, double eps)
{
    return target.startTime <= prev.endTime + eps;
}

inline bool isTimeAdjacentToNext(const Note& target, const Note& next, double eps)
{
    return next.startTime <= target.endTime + eps;
}

inline void applyNoteEdgeResizeWithNeighborTrim(
    std::vector<Note>& notes,
    Note* target,
    NoteResizeEdge edge,
    double rawTimeSeconds,
    double minDurationSeconds)
{
    if (target == nullptr || notes.empty())
        return;

    static constexpr double kTimeAdjacencyEps = 1e-4;

    const size_t selfIndex = static_cast<size_t>(
        std::find_if(notes.begin(), notes.end(), [target](const Note& note) { return &note == target; }) - notes.begin());
    if (selfIndex >= notes.size())
        return;

    const std::vector<size_t> indices = sortedNoteIndicesByStart(notes);
    size_t sortedPosition = indices.size();
    for (size_t i = 0; i < indices.size(); ++i)
    {
        if (indices[i] == selfIndex)
        {
            sortedPosition = i;
            break;
        }
    }
    if (sortedPosition >= indices.size())
        return;

    if (edge == NoteResizeEdge::Left)
    {
        double newStart = rawTimeSeconds;
        newStart = std::max(0.0, newStart);
        newStart = std::min(newStart, target->endTime - minDurationSeconds);
        if (sortedPosition > 0)
        {
            Note& prev = notes[indices[sortedPosition - 1]];
            if (isTimeAdjacentToPrev(prev, *target, kTimeAdjacencyEps))
            {
                const double prevMinEnd = prev.startTime + minDurationSeconds;
                newStart = std::max(newStart, prevMinEnd);
                prev.endTime = newStart;
                prev.dirty = true;
            }
        }
        target->startTime = newStart;
        target->dirty = true;
    }
    else if (edge == NoteResizeEdge::Right)
    {
        double newEnd = rawTimeSeconds;
        newEnd = std::max(newEnd, target->startTime + minDurationSeconds);
        if (sortedPosition + 1 < indices.size())
        {
            Note& next = notes[indices[sortedPosition + 1]];
            if (isTimeAdjacentToNext(*target, next, kTimeAdjacencyEps))
            {
                const double nextMaxStart = next.endTime - minDurationSeconds;
                newEnd = std::min(newEnd, nextMaxStart);
                next.startTime = newEnd;
                next.dirty = true;
            }
        }
        target->endTime = newEnd;
        target->dirty = true;
    }
}

template<typename TimeToXFn>
bool pickBestNoteEdgeHit(
    std::vector<Note>& notes,
    int mouseX,
    float mouseMidiVal,
    int edgeThreshold,
    double offsetSeconds,
    TimeToXFn&& timeToX,
    Note*& outNote,
    NoteResizeEdge& outEdge)
{
    outNote = nullptr;
    outEdge = NoteResizeEdge::None;
    int bestDist = std::numeric_limits<int>::max();

    for (auto& note : notes)
    {
        const int startX = timeToX(note.startTime + offsetSeconds);
        const int endX = timeToX(note.endTime + offsetSeconds);
        const float noteMidi = midiBandForNote(note);
        if (std::abs(mouseMidiVal - noteMidi) >= 1.0f)
            continue;

        const int leftDist = std::abs(mouseX - startX);
        const int rightDist = std::abs(mouseX - endX);
        int localBest = std::numeric_limits<int>::max();
        NoteResizeEdge localEdge = NoteResizeEdge::None;
        if (leftDist <= edgeThreshold)
        {
            localBest = leftDist;
            localEdge = NoteResizeEdge::Left;
        }
        if (rightDist <= edgeThreshold && rightDist < localBest)
        {
            localBest = rightDist;
            localEdge = NoteResizeEdge::Right;
        }
        if (localEdge != NoteResizeEdge::None && localBest < bestDist)
        {
            bestDist = localBest;
            outNote = &note;
            outEdge = localEdge;
        }
    }

    return outNote != nullptr;
}

} // namespace
} // namespace OpenTune
