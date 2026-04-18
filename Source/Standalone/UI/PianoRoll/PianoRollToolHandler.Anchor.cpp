#include "PianoRollToolHandler.h"
#include "../../../Utils/PitchUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenTune {

namespace {

void clampAnchorTimeToItsNoteAtBase(double& timeInOut, double baseTime, const std::vector<Note>& notes)
{
    for (const auto& note : notes)
    {
        if (baseTime >= note.startTime && baseTime <= note.endTime)
        {
            timeInOut = juce::jlimit(note.startTime, note.endTime, timeInOut);
            return;
        }
    }
}

} // namespace

void PianoRollToolHandler::loadAnchorsFromCurve()
{
    auto& ae = ctx_.getState().drawing.anchorEdit;
    ae.clear();

    auto pitchCurve = ctx_.getPitchCurve();
    if (!pitchCurve)
        return;
    auto snap = pitchCurve->getSnapshot();
    if (!snap)
        return;

    const auto& notes = ctx_.getNotes();
    const auto& originalF0 = snap->getOriginalF0();
    if (originalF0.empty() || notes.empty())
        return;

    const int hopSize = snap->getHopSize();
    const double sampleRate = snap->getSampleRate();
    if (hopSize <= 0 || sampleRate <= 0.0)
        return;

    const double frameDuration = static_cast<double>(hopSize) / sampleRate;
    const int totalFrames = static_cast<int>(originalF0.size());

    std::vector<float> renderedF0(totalFrames, 0.0f);
    snap->renderF0Range(0, totalFrames,
        [&](int startFrame, const float* data, int length) {
            for (int i = 0; i < length; ++i)
            {
                int idx = startFrame + i;
                if (idx >= 0 && idx < totalFrames)
                    renderedF0[idx] = data[i];
            }
        });

    ae.groups = HermiteInterpolation::fitToF0PerNote(renderedF0, notes, frameDuration, 10.0f, 80);

    if (!ae.groups.empty())
    {
        if (!ctx_.isTransactionActive())
            ctx_.beginEditTransaction("Auto-fit Anchors");

        pitchCurve->setAnchorGroups(ae.groups, notes, ctx_.getRetuneSpeed());
        commitAnchorEdit();
    }
}

PianoRollToolHandler::AnchorHit PianoRollToolHandler::hitTestAnchor(float screenX, float screenY, float radius) const
{
    const auto& ae = ctx_.getState().drawing.anchorEdit;
    const double offsetSeconds = ctx_.getTrackOffsetSeconds();
    const float radiusSq = radius * radius;

    for (int gi = 0; gi < static_cast<int>(ae.groups.size()); ++gi)
    {
        const auto& grp = ae.groups[gi];
        for (int pi = 0; pi < static_cast<int>(grp.points.size()); ++pi)
        {
            const auto& pt = grp.points[pi];
            float ax = static_cast<float>(ctx_.timeToX(pt.time + offsetSeconds));
            float ay = ctx_.freqToY(PitchUtils::midiToFreq(pt.pitch));
            float dx = screenX - ax;
            float dy = screenY - ay;
            if (dx * dx + dy * dy <= radiusSq)
                return { gi, pi };
        }
    }
    return {};
}

void PianoRollToolHandler::regenerateAnchorsF0()
{
    auto& ae = ctx_.getState().drawing.anchorEdit;
    auto pitchCurve = ctx_.getPitchCurve();
    if (!pitchCurve)
        return;

    for (auto& g : ae.groups)
        g.sortByTime();

    std::vector<AnchorGroup> validGroups;
    for (const auto& g : ae.groups)
    {
        if (g.points.size() >= 2)
            validGroups.push_back(g);
    }

    pitchCurve->setAnchorGroups(validGroups, ctx_.getNotes(), ctx_.getRetuneSpeed());

    auto snap = pitchCurve->getSnapshot();
    if (snap)
    {
        const auto& segs = snap->getCorrectedSegments();
        int minF = std::numeric_limits<int>::max();
        int maxF = std::numeric_limits<int>::min();
        for (const auto& s : segs)
        {
            if (s.source == CorrectedSegment::Source::LineAnchor)
            {
                minF = std::min(minF, s.startFrame);
                maxF = std::max(maxF, s.endFrame);
            }
        }
        if (minF < maxF)
            ctx_.notifyPitchCurveEdited(minF, maxF);
    }
}

void PianoRollToolHandler::commitAnchorEdit()
{
    if (ctx_.isTransactionActive())
        ctx_.commitEditTransaction();
    ctx_.requestRepaint();
}

void PianoRollToolHandler::handleLineAnchorMouseDown(const juce::MouseEvent& e)
{
    auto& ae = ctx_.getState().drawing.anchorEdit;
    const double offsetSeconds = ctx_.getTrackOffsetSeconds();
    const double clickTime = ctx_.xToTime(e.x) - offsetSeconds;
    float clickFreq = ctx_.yToFreq(static_cast<float>(e.y));
    clickFreq = std::max(20.0f, clickFreq);
    float clickMidi = PitchUtils::freqToMidi(clickFreq);

    if (e.mods.isCtrlDown())
    {
        for (auto& g : ae.groups)
            g.deselectAll();
        ae.mode = AnchorEditState::Mode::BoxSelecting;
        ae.boxStartTime = clickTime;
        ae.boxStartPitch = clickMidi;
        ae.boxEndTime = clickTime;
        ae.boxEndPitch = clickMidi;
        ctx_.requestRepaint();
        return;
    }

    auto hit = hitTestAnchor(static_cast<float>(e.x), static_cast<float>(e.y));

    if (hit.groupIdx >= 0 && hit.pointIdx >= 0)
    {
        if (ae.mode == AnchorEditState::Mode::Placing
            && ae.activeGroupIndex >= 0
            && ae.activeGroupIndex != hit.groupIdx)
        {
            bool sameNote = false;
            const auto& notes = ctx_.getNotes();
            const auto& srcGrp = ae.groups[ae.activeGroupIndex];
            const auto& dstGrp = ae.groups[hit.groupIdx];
            if (!srcGrp.points.empty() && !dstGrp.points.empty())
            {
                double srcMid = (srcGrp.points.front().time + srcGrp.points.back().time) * 0.5;
                double dstMid = (dstGrp.points.front().time + dstGrp.points.back().time) * 0.5;
                for (const auto& note : notes)
                {
                    bool srcIn = srcMid >= note.startTime && srcMid <= note.endTime;
                    bool dstIn = dstMid >= note.startTime && dstMid <= note.endTime;
                    if (srcIn && dstIn)
                    {
                        sameNote = true;
                        break;
                    }
                }
            }

            if (sameNote)
            {
                if (!ctx_.isTransactionActive())
                    ctx_.beginEditTransaction("Connect Anchors");

                auto& srcGroup = ae.groups[ae.activeGroupIndex];
                auto& dstGroup = ae.groups[hit.groupIdx];
                for (const auto& pt : srcGroup.points)
                    dstGroup.insertPoint(pt);
                dstGroup.sortByTime();

                int oldActive = ae.activeGroupIndex;
                ae.groups.erase(ae.groups.begin() + oldActive);
                int newIdx = (hit.groupIdx > oldActive) ? hit.groupIdx - 1 : hit.groupIdx;
                ae.activeGroupIndex = newIdx;
                ae.mode = AnchorEditState::Mode::Idle;
                ae.draggedAnchorIndex = -1;
                for (auto& g : ae.groups)
                    g.deselectAll();
                ctx_.getState().drawing.isPlacingAnchors = false;

                regenerateAnchorsF0();
                commitAnchorEdit();
                ctx_.requestRepaint();
                return;
            }

            ae.mode = AnchorEditState::Mode::Idle;
            ae.draggedAnchorIndex = -1;
            ctx_.getState().drawing.isPlacingAnchors = false;
        }

        if (!e.mods.isShiftDown() && !ae.groups[hit.groupIdx].points[hit.pointIdx].selected)
        {
            for (auto& g : ae.groups)
                g.deselectAll();
        }
        ae.groups[hit.groupIdx].points[hit.pointIdx].selected = true;

        ae.mode = AnchorEditState::Mode::Dragging;
        ae.activeGroupIndex = hit.groupIdx;
        ae.draggedAnchorIndex = hit.pointIdx;
        ae.dragStartTime = ae.groups[hit.groupIdx].points[hit.pointIdx].time;
        ae.dragStartPitch = ae.groups[hit.groupIdx].points[hit.pointIdx].pitch;

        if (ae.groups[hit.groupIdx].points[hit.pointIdx].uid == 0)
            ae.groups[hit.groupIdx].points[hit.pointIdx].uid = AnchorPoint::nextUid();

        for (auto& g : ae.groups)
            for (auto& p : g.points)
            {
                p.dragBasePitch = p.pitch;
                p.dragBaseTime = p.time;
            }

        if (!ctx_.isTransactionActive())
            ctx_.beginEditTransaction("Move Anchor");

        ctx_.requestRepaint();
        return;
    }

    const auto& notes = ctx_.getNotes();
    int targetGroupIdx = -1;
    for (int gi = 0; gi < static_cast<int>(ae.groups.size()); ++gi)
    {
        const auto& grp = ae.groups[gi];
        if (grp.points.empty())
            continue;
        double gStart = grp.points.front().time;
        double gEnd = grp.points.back().time;
        for (const auto& note : notes)
        {
            if (clickTime >= note.startTime && clickTime <= note.endTime
                && gStart >= note.startTime - 0.05 && gEnd <= note.endTime + 0.05)
            {
                targetGroupIdx = gi;
                break;
            }
        }
        if (targetGroupIdx >= 0)
            break;
    }

    if (targetGroupIdx < 0)
    {
        for (const auto& note : notes)
        {
            if (clickTime >= note.startTime && clickTime <= note.endTime)
            {
                AnchorGroup newGroup;
                ae.groups.push_back(newGroup);
                targetGroupIdx = static_cast<int>(ae.groups.size()) - 1;
                break;
            }
        }
    }

    if (targetGroupIdx < 0)
    {
        ctx_.requestRepaint();
        return;
    }

    if (!ctx_.isTransactionActive())
        ctx_.beginEditTransaction("Add Anchor");

    for (auto& g : ae.groups)
        g.deselectAll();

    AnchorPoint newPt;
    newPt.time = clickTime;
    newPt.pitch = clickMidi;
    newPt.selected = true;
    newPt.uid = AnchorPoint::nextUid();
    int newIdx = ae.groups[targetGroupIdx].insertPoint(newPt);

    ae.mode = AnchorEditState::Mode::Placing;
    ae.activeGroupIndex = targetGroupIdx;
    ae.draggedAnchorIndex = newIdx;
    ae.dragStartTime = clickTime;
    ae.dragStartPitch = clickMidi;

    ctx_.getState().drawing.isPlacingAnchors = true;
    ctx_.getState().drawing.currentMousePos = e.position;

    if (ae.groups[targetGroupIdx].points.size() >= 2)
        regenerateAnchorsF0();

    ctx_.requestRepaint();
}

void PianoRollToolHandler::handleLineAnchorMouseDrag(const juce::MouseEvent& e)
{
    auto& ae = ctx_.getState().drawing.anchorEdit;
    ctx_.getState().drawing.currentMousePos = e.position;

    if (ae.mode == AnchorEditState::Mode::BoxSelecting)
    {
        const double offsetSeconds = ctx_.getTrackOffsetSeconds();
        ae.boxEndTime = ctx_.xToTime(e.x) - offsetSeconds;
        ae.boxEndPitch = PitchUtils::freqToMidi(std::max(20.0f, ctx_.yToFreq(static_cast<float>(e.y))));
        ctx_.requestRepaint();
        return;
    }

    if ((ae.mode == AnchorEditState::Mode::Dragging || ae.mode == AnchorEditState::Mode::Placing)
        && ae.activeGroupIndex >= 0
        && ae.activeGroupIndex < static_cast<int>(ae.groups.size())
        && ae.draggedAnchorIndex >= 0
        && ae.draggedAnchorIndex < static_cast<int>(ae.groups[ae.activeGroupIndex].points.size()))
    {
        float newFreq = ctx_.yToFreq(static_cast<float>(e.y));
        newFreq = std::max(20.0f, newFreq);
        float newMidi = PitchUtils::freqToMidi(newFreq);

        int totalSelected = 0;
        for (const auto& g : ae.groups)
            for (const auto& p : g.points)
                if (p.selected)
                    totalSelected++;

        if (totalSelected > 1 && ae.mode == AnchorEditState::Mode::Dragging)
        {
            const double offsetSeconds = ctx_.getTrackOffsetSeconds();
            const double primaryNewTime = ctx_.xToTime(e.x) - offsetSeconds;
            const double timeDelta = primaryNewTime - ae.dragStartTime;
            const float pitchDelta = newMidi - ae.dragStartPitch;

            const auto& notes = ctx_.getNotes();
            for (auto& g : ae.groups)
            {
                bool touched = false;
                for (auto& p : g.points)
                {
                    if (!p.selected)
                        continue;
                    p.time = p.dragBaseTime + timeDelta;
                    clampAnchorTimeToItsNoteAtBase(p.time, p.dragBaseTime, notes);
                    p.pitch = p.dragBasePitch + pitchDelta;
                    touched = true;
                }
                if (touched)
                    g.sortByTime();
            }

            regenerateAnchorsF0();
        }
        else
        {
            const double offsetSeconds = ctx_.getTrackOffsetSeconds();
            double newTime = ctx_.xToTime(e.x) - offsetSeconds;

            auto& grp = ae.groups[ae.activeGroupIndex];
            const auto& notes = ctx_.getNotes();
            double clampMin = -1e9;
            double clampMax = 1e9;
            if (!grp.points.empty())
            {
                double grpMidTime = ae.dragStartTime;
                for (const auto& note : notes)
                {
                    if (grpMidTime >= note.startTime && grpMidTime <= note.endTime)
                    {
                        clampMin = note.startTime;
                        clampMax = note.endTime;
                        break;
                    }
                }
            }
            newTime = std::max(clampMin, std::min(clampMax, newTime));

            auto& pt = grp.points[ae.draggedAnchorIndex];
            const uint32_t dragUid = pt.uid;
            pt.time = newTime;
            pt.pitch = newMidi;

            grp.sortByTime();
            for (int i = 0; i < static_cast<int>(grp.points.size()); ++i)
            {
                if (grp.points[i].uid == dragUid && dragUid != 0)
                {
                    ae.draggedAnchorIndex = i;
                    break;
                }
            }

            if (grp.points.size() >= 2)
                regenerateAnchorsF0();
        }
    }

    ctx_.requestRepaint();
}

void PianoRollToolHandler::handleLineAnchorMouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    auto& ae = ctx_.getState().drawing.anchorEdit;

    if (ae.mode == AnchorEditState::Mode::BoxSelecting)
    {
        double t0 = std::min(ae.boxStartTime, ae.boxEndTime);
        double t1 = std::max(ae.boxStartTime, ae.boxEndTime);
        float p0 = std::min(ae.boxStartPitch, ae.boxEndPitch);
        float p1 = std::max(ae.boxStartPitch, ae.boxEndPitch);

        for (auto& g : ae.groups)
            for (auto& pt : g.points)
                if (pt.time >= t0 && pt.time <= t1 && pt.pitch >= p0 && pt.pitch <= p1)
                    pt.selected = true;

        ae.mode = AnchorEditState::Mode::Idle;
        ctx_.requestRepaint();
        return;
    }

    if (ae.mode == AnchorEditState::Mode::Dragging || ae.mode == AnchorEditState::Mode::Placing)
    {
        ae.draggedAnchorIndex = -1;
        if (ae.mode == AnchorEditState::Mode::Dragging)
            ae.mode = AnchorEditState::Mode::Idle;
        commitAnchorEdit();
    }
}

void PianoRollToolHandler::commitLineAnchorOperation()
{
    auto& ae = ctx_.getState().drawing.anchorEdit;

    if (ae.hasAnyPoints())
        regenerateAnchorsF0();

    commitAnchorEdit();
    ctx_.getState().drawing.isPlacingAnchors = false;
    ctx_.requestRepaint();
}

} // namespace OpenTune
