#include "VocalSegment.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace OpenTune {

void VocalSegmentSequence::sortAndValidate(double clipDurationSec)
{
    const bool clampToClip = clipDurationSec > 0.0 && std::isfinite(clipDurationSec);
    const int64_t clipEndTick = clampToClip ? TimeCoordinate::clipSecondsToTick(clipDurationSec) : (std::numeric_limits<int64_t>::max() / 4);
    const int64_t minLenTick = juce::jmax<int64_t>(1, TimeCoordinate::clipSecondsToTick(1e-6));
    for (auto& s : segments_) {
        s.startTick = juce::jmax<int64_t>(0, s.startTick);
        if (clampToClip) {
            s.startTick = juce::jmin(clipEndTick, s.startTick);
            s.endTick = juce::jmax<int64_t>(0, juce::jmin(clipEndTick, s.endTick));
        } else {
            s.endTick = juce::jmax(s.endTick, s.startTick + minLenTick);
        }
        if (s.endTick <= s.startTick) {
            const int64_t bump = clampToClip ? juce::jmin(TimeCoordinate::clipSecondsToTick(0.05), juce::jmax<int64_t>(1, clipEndTick - s.startTick))
                                              : TimeCoordinate::clipSecondsToTick(0.05);
            s.endTick = s.startTick + bump;
        }
    }
    std::sort(segments_.begin(), segments_.end(), [](const VocalSegment& a, const VocalSegment& b) {
        return a.startTick < b.startTick || (a.startTick == b.startTick && a.endTick < b.endTick);
    });
    uint64_t maxId = 0;
    for (const auto& s : segments_) {
        maxId = std::max(maxId, s.id);
    }
    nextId_ = std::max(nextId_, maxId + 1);
}

bool VocalSegmentSequence::moveBoundary(uint64_t segmentId, bool isLeftEdge, double newSec, double clipDurationSec,
                                        double minSegmentSec, juce::String& errOut)
{
    sortAndValidate(clipDurationSec);
    const int64_t clipEndTick = TimeCoordinate::clipSecondsToTick(clipDurationSec);
    const int64_t newTick = TimeCoordinate::clipSecondsToTick(newSec);
    const int64_t minSegTick = juce::jmax<int64_t>(1, TimeCoordinate::clipSecondsToTick(minSegmentSec));
    const int n = static_cast<int>(segments_.size());
    for (int i = 0; i < n; ++i) {
        if (segments_[static_cast<size_t>(i)].id != segmentId) {
            continue;
        }
        VocalSegment& seg = segments_[static_cast<size_t>(i)];
        if (isLeftEdge) {
            const int64_t prevEnd = (i > 0) ? segments_[static_cast<size_t>(i - 1)].endTick : 0;
            const int64_t maxStart = seg.endTick - minSegTick;
            if (newTick <= prevEnd) {
                errOut = "Left boundary would overlap previous segment.";
                return false;
            }
            if (newTick > maxStart) {
                errOut = "Segment would be shorter than minimum.";
                return false;
            }
            seg.startTick = std::clamp(newTick, prevEnd + 1, maxStart);
        } else {
            const int64_t nextStart = (i + 1 < n) ? segments_[static_cast<size_t>(i + 1)].startTick : clipEndTick;
            const int64_t minEnd = seg.startTick + minSegTick;
            if (newTick >= nextStart) {
                errOut = "Right boundary would overlap next segment.";
                return false;
            }
            if (newTick < minEnd) {
                errOut = "Segment would be shorter than minimum.";
                return false;
            }
            seg.endTick = std::clamp(newTick, minEnd, nextStart - 1);
        }
        return true;
    }
    errOut = "Segment not found.";
    return false;
}

void VocalSegmentSequence::insertSegment(VocalSegment seg)
{
    if (seg.id == 0) {
        seg.id = allocateId();
    } else {
        nextId_ = std::max(nextId_, seg.id + 1);
    }
    segments_.push_back(std::move(seg));
    sortAndValidate(0.0);
}

bool VocalSegmentSequence::eraseById(uint64_t id)
{
    const auto it = std::remove_if(segments_.begin(), segments_.end(),
                                   [id](const VocalSegment& s) { return s.id == id; });
    if (it == segments_.end()) {
        return false;
    }
    segments_.erase(it, segments_.end());
    return true;
}

bool VocalSegmentSequence::mergeAdjacent(uint64_t leftId, juce::String& errOut)
{
    sortAndValidate(0.0);
    for (size_t i = 0; i + 1 < segments_.size(); ++i) {
        if (segments_[i].id != leftId) {
            continue;
        }
        segments_[i].endTick = segments_[i + 1].endTick;
        segments_.erase(segments_.begin() + static_cast<std::ptrdiff_t>(i) + 1);
        return true;
    }
    errOut = "No adjacent segment to merge.";
    return false;
}

bool VocalSegmentSequence::splitAtTime(uint64_t segmentId, double splitSec, juce::String& errOut)
{
    sortAndValidate(1e9);
    const int64_t splitTick = TimeCoordinate::clipSecondsToTick(splitSec);
    for (size_t i = 0; i < segments_.size(); ++i) {
        VocalSegment& s = segments_[i];
        if (s.id != segmentId) {
            continue;
        }
        if (splitTick <= s.startTick || splitTick >= s.endTick) {
            errOut = "Split time must be strictly inside segment.";
            return false;
        }
        VocalSegment right;
        right.id = allocateId();
        right.startTick = splitTick;
        right.endTick = s.endTick;
        right.gainLinear = s.gainLinear;
        right.tension01 = s.tension01;
        right.flags = s.flags;
        s.endTick = splitTick;
        segments_.insert(segments_.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(right));
        return true;
    }
    errOut = "Segment not found.";
    return false;
}

VocalSegment* VocalSegmentSequence::findById(uint64_t id)
{
    for (auto& s : segments_) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

const VocalSegment* VocalSegmentSequence::findById(uint64_t id) const
{
    for (const auto& s : segments_) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

uint64_t VocalSegmentSequence::allocateId()
{
    return nextId_++;
}

void VocalSegmentSequence::reassignSequentialIds()
{
    uint64_t id = 1;
    for (auto& s : segments_) {
        s.id = id++;
    }
    nextId_ = id;
}

} // namespace OpenTune
