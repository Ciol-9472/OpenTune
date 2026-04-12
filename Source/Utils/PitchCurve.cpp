#include "PitchCurve.h"
#include "PitchUtils.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace OpenTune {

constexpr int kUnifiedTransitionFrames = 10;

namespace {

bool hasSegmentOverlap(const std::vector<CorrectedSegment>& segments, int rangeStart, int rangeEnd)
{
    if (rangeEnd <= rangeStart) {
        return false;
    }
    for (const auto& seg : segments) {
        if (seg.endFrame <= rangeStart || seg.startFrame >= rangeEnd) {
            continue;
        }
        return true;
    }
    return false;
}

void insertSegmentSorted(std::vector<CorrectedSegment>& segments, CorrectedSegment&& seg)
{
    auto insertPos = std::lower_bound(segments.begin(), segments.end(), seg.startFrame,
        [](const CorrectedSegment& s, int frame) {
            return s.startFrame < frame;
        });
    segments.insert(insertPos, std::move(seg));
}

void clearSegmentsInRangePreserveOutside(std::vector<CorrectedSegment>& segments, int startFrame, int endFrame)
{
    if (startFrame >= endFrame) {
        return;
    }

    std::vector<CorrectedSegment> kept;
    kept.reserve(segments.size() + 1);

    for (const auto& seg : segments) {
        if (seg.endFrame <= startFrame || seg.startFrame >= endFrame) {
            kept.push_back(seg);
            continue;
        }

        if (seg.startFrame < startFrame) {
            CorrectedSegment left = seg;
            left.endFrame = startFrame;
            const int leftLen = left.endFrame - left.startFrame;
            if (leftLen > 0 && leftLen <= static_cast<int>(seg.f0Data.size())) {
                left.f0Data.assign(seg.f0Data.begin(), seg.f0Data.begin() + leftLen);
                kept.push_back(std::move(left));
            }
        }

        if (seg.endFrame > endFrame) {
            CorrectedSegment right = seg;
            right.startFrame = endFrame;
            const int offset = right.startFrame - seg.startFrame;
            const int rightLen = right.endFrame - right.startFrame;
            if (offset >= 0 && rightLen > 0 && offset + rightLen <= static_cast<int>(seg.f0Data.size())) {
                right.f0Data.assign(seg.f0Data.begin() + offset, seg.f0Data.begin() + offset + rightLen);
                kept.push_back(std::move(right));
            }
        }
    }

    segments.swap(kept);
}

std::optional<CorrectedSegment> buildLeftTransitionSegment(
    const CorrectedSegment& centerSeg,
    const std::vector<float>& originalF0,
    const std::vector<CorrectedSegment>& existingSegments,
    int transitionFrames)
{
    if (centerSeg.f0Data.empty() || transitionFrames <= 0 || originalF0.empty()) {
        return std::nullopt;
    }

    const float boundaryF0 = centerSeg.f0Data.front();
    if (boundaryF0 <= 0.0f || centerSeg.startFrame <= 0) {
        return std::nullopt;
    }

    const int transStart = std::max(0, centerSeg.startFrame - transitionFrames);
    const int transEnd = centerSeg.startFrame;
    if (transEnd <= transStart || transEnd > static_cast<int>(originalF0.size())) {
        return std::nullopt;
    }

    if (hasSegmentOverlap(existingSegments, transStart, transEnd)) {
        return std::nullopt;
    }

    for (int f = transStart; f < transEnd; ++f) {
        if (originalF0[static_cast<size_t>(f)] <= 0.0f) {
            return std::nullopt;
        }
    }

    std::vector<float> transitionData;
    transitionData.reserve(static_cast<size_t>(transEnd - transStart));
    const float safeBoundary = std::max(boundaryF0, 1.0e-6f);
    const int len = transEnd - transStart;

    for (int i = 0; i < len; ++i) {
        const int f = transStart + i;
        const float orig = std::max(originalF0[static_cast<size_t>(f)], 1.0e-6f);
        const float w = static_cast<float>(i + 1) / static_cast<float>(len + 1);
        const float logOrig = std::log2(orig);
        const float logBoundary = std::log2(safeBoundary);
        transitionData.push_back(std::pow(2.0f, logOrig + (logBoundary - logOrig) * w));
    }

    CorrectedSegment seg(transStart, transEnd, transitionData, centerSeg.source);
    seg.retuneSpeed = centerSeg.retuneSpeed;
    seg.vibratoDepth = centerSeg.vibratoDepth;
    seg.vibratoRate = centerSeg.vibratoRate;
    return seg;
}

std::optional<CorrectedSegment> buildRightTransitionSegment(
    const CorrectedSegment& centerSeg,
    const std::vector<float>& originalF0,
    const std::vector<CorrectedSegment>& existingSegments,
    int transitionFrames)
{
    if (centerSeg.f0Data.empty() || transitionFrames <= 0 || originalF0.empty()) {
        return std::nullopt;
    }

    const float boundaryF0 = centerSeg.f0Data.back();
    if (boundaryF0 <= 0.0f || centerSeg.endFrame >= static_cast<int>(originalF0.size())) {
        return std::nullopt;
    }

    const int transStart = centerSeg.endFrame;
    const int transEnd = std::min(centerSeg.endFrame + transitionFrames, static_cast<int>(originalF0.size()));
    if (transEnd <= transStart) {
        return std::nullopt;
    }

    if (hasSegmentOverlap(existingSegments, transStart, transEnd)) {
        return std::nullopt;
    }

    for (int f = transStart; f < transEnd; ++f) {
        if (originalF0[static_cast<size_t>(f)] <= 0.0f) {
            return std::nullopt;
        }
    }

    std::vector<float> transitionData;
    transitionData.reserve(static_cast<size_t>(transEnd - transStart));
    const float safeBoundary = std::max(boundaryF0, 1.0e-6f);
    const int len = transEnd - transStart;

    for (int i = 0; i < len; ++i) {
        const int f = transStart + i;
        const float orig = std::max(originalF0[static_cast<size_t>(f)], 1.0e-6f);
        const float w = static_cast<float>(len - i) / static_cast<float>(len + 1);
        const float logOrig = std::log2(orig);
        const float logBoundary = std::log2(safeBoundary);
        transitionData.push_back(std::pow(2.0f, logOrig + (logBoundary - logOrig) * w));
    }

    CorrectedSegment seg(transStart, transEnd, transitionData, centerSeg.source);
    seg.retuneSpeed = centerSeg.retuneSpeed;
    seg.vibratoDepth = centerSeg.vibratoDepth;
    seg.vibratoRate = centerSeg.vibratoRate;
    return seg;
}

void insertSegmentWithUnifiedTransitions(
    std::vector<CorrectedSegment>& segments,
    const std::vector<float>& originalF0,
    CorrectedSegment&& centerSeg,
    int transitionFrames)
{
    std::optional<CorrectedSegment> leftTransition = buildLeftTransitionSegment(centerSeg, originalF0, segments, transitionFrames);
    std::optional<CorrectedSegment> rightTransition = buildRightTransitionSegment(centerSeg, originalF0, segments, transitionFrames);

    if (leftTransition.has_value()) {
        insertSegmentSorted(segments, std::move(leftTransition.value()));
    }
    insertSegmentSorted(segments, std::move(centerSeg));
    if (rightTransition.has_value()) {
        insertSegmentSorted(segments, std::move(rightTransition.value()));
    }
}

} // namespace

bool PitchCurveSnapshot::hasCorrectionInRange(int startFrame, int endFrame) const {
    if (correctedSegments_.empty()) {
        return false;
    }

    auto it = std::lower_bound(correctedSegments_.begin(), correctedSegments_.end(), startFrame,
        [](const CorrectedSegment& seg, int frame) {
            return seg.endFrame <= frame;
        });

    while (it != correctedSegments_.end() && it->startFrame < endFrame) {
        if (it->endFrame > startFrame) {
            return true;
        }
        ++it;
    }

    return false;
}

void PitchCurveSnapshot::renderF0Range(int startFrame, int endFrame,
                                       std::function<void(int, const float*, int)> callback) const {
    if (startFrame >= endFrame || startFrame < 0) {
        return;
    }

    const int maxFrame = static_cast<int>(originalF0_.size());
    if (endFrame > maxFrame) {
        endFrame = maxFrame;
    }
    if (startFrame >= maxFrame) {
        return;
    }

    auto it = std::lower_bound(correctedSegments_.begin(), correctedSegments_.end(), startFrame,
        [](const CorrectedSegment& seg, int frame) {
            return seg.endFrame <= frame;
        });

    int currentPos = startFrame;
    std::vector<float> tempBuffer;

    while (currentPos < endFrame) {
        if (it != correctedSegments_.end() && it->startFrame < endFrame) {
            if (currentPos < it->startFrame) {
                int gapEnd = std::min(it->startFrame, endFrame);
                int gapLength = gapEnd - currentPos;
                callback(currentPos, originalF0_.data() + currentPos, gapLength);
                currentPos = gapEnd;
            }

            if (currentPos < it->endFrame && currentPos < maxFrame) {
                int segStart = std::max(currentPos, it->startFrame);
                int segEnd = std::min(endFrame, std::min(it->endFrame, maxFrame));
                int offset = segStart - it->startFrame;
                int length = segEnd - segStart;

                if (length <= 0) {
                    ++it;
                    continue;
                }

                if (static_cast<size_t>(offset + length) > it->f0Data.size()) {
                    ++it;
                    continue;
                }

                if (it->retuneSpeed >= 0.0f && it->source == CorrectedSegment::Source::LineAnchor) {
                    tempBuffer.resize(static_cast<size_t>(length));
                    for (int i = 0; i < length; ++i) {
                        int frameIdx = segStart + i;
                        float targetF0 = it->f0Data[static_cast<size_t>(offset + i)];
                        float originalF0 = originalF0_[static_cast<size_t>(frameIdx)];
                        if (originalF0 > 0.0f && targetF0 > 0.0f) {
                            tempBuffer[static_cast<size_t>(i)] = PitchUtils::mixRetune(originalF0, targetF0, it->retuneSpeed);
                        } else {
                            tempBuffer[static_cast<size_t>(i)] = targetF0;
                        }
                    }
                    callback(segStart, tempBuffer.data(), length);
                } else {
                    callback(segStart, it->f0Data.data() + offset, length);
                }
                currentPos = segEnd;
            }

            ++it;
        } else {
            int length = endFrame - currentPos;
            callback(currentPos, originalF0_.data() + currentPos, length);
            currentPos = endFrame;
        }
    }
}

void PitchCurveSnapshot::renderCorrectedOnlyRange(int startFrame, int endFrame,
                                                  std::function<void(int, const float*, int)> callback) const {
    if (startFrame >= endFrame || startFrame < 0) {
        return;
    }

    const int maxFrame = static_cast<int>(originalF0_.size());
    if (endFrame > maxFrame) {
        endFrame = maxFrame;
    }
    if (startFrame >= maxFrame) {
        return;
    }

    auto it = std::lower_bound(correctedSegments_.begin(), correctedSegments_.end(), startFrame,
        [](const CorrectedSegment& seg, int frame) {
            return seg.endFrame <= frame;
        });

    int currentPos = startFrame;
    std::vector<float> tempBuffer;

    while (currentPos < endFrame) {
        if (it != correctedSegments_.end() && it->startFrame < endFrame) {
            if (currentPos < it->startFrame) {
                const int gapEnd = std::min(it->startFrame, endFrame);
                const int gapLength = gapEnd - currentPos;
                tempBuffer.assign(static_cast<size_t>(gapLength), 0.0f);
                callback(currentPos, tempBuffer.data(), gapLength);
                currentPos = gapEnd;
            }

            if (currentPos < it->endFrame && currentPos < maxFrame) {
                const int segStart = std::max(currentPos, it->startFrame);
                const int segEnd = std::min(endFrame, std::min(it->endFrame, maxFrame));
                const int offset = segStart - it->startFrame;
                const int length = segEnd - segStart;

                if (length <= 0) {
                    ++it;
                    continue;
                }

                if (static_cast<size_t>(offset + length) > it->f0Data.size()) {
                    ++it;
                    continue;
                }

                if (it->retuneSpeed >= 0.0f && it->source == CorrectedSegment::Source::LineAnchor) {
                    tempBuffer.resize(static_cast<size_t>(length));
                    for (int i = 0; i < length; ++i) {
                        int frameIdx = segStart + i;
                        float targetF0 = it->f0Data[static_cast<size_t>(offset + i)];
                        float originalF0 = originalF0_[static_cast<size_t>(frameIdx)];
                        if (originalF0 > 0.0f && targetF0 > 0.0f) {
                            tempBuffer[static_cast<size_t>(i)] = PitchUtils::mixRetune(originalF0, targetF0, it->retuneSpeed);
                        } else {
                            tempBuffer[static_cast<size_t>(i)] = targetF0;
                        }
                    }
                    callback(segStart, tempBuffer.data(), length);
                } else {
                    callback(segStart, it->f0Data.data() + offset, length);
                }
                currentPos = segEnd;
            }

            ++it;
        } else {
            const int length = endFrame - currentPos;
            tempBuffer.assign(static_cast<size_t>(length), 0.0f);
            callback(currentPos, tempBuffer.data(), length);
            currentPos = endFrame;
        }
    }
}

bool PitchCurveSnapshot::hasCorrectedVisibleInRange(double startSeconds, double endSeconds) const {
    if (correctedSegments_.empty() || hopSize_ <= 0 || sampleRate_ <= 0.0) {
        return false;
    }
    if (endSeconds <= startSeconds) {
        return false;
    }

    if (startSeconds < 0) startSeconds = 0;

    const double framesPerSecond = sampleRate_ / static_cast<double>(hopSize_);
    int startFrame = static_cast<int>(std::floor(startSeconds * framesPerSecond));
    int endFrame = static_cast<int>(std::ceil(endSeconds * framesPerSecond));
    if (startFrame < 0) startFrame = 0;
    if (endFrame < startFrame) endFrame = startFrame;

    const int n = static_cast<int>(originalF0_.size());
    if (startFrame >= n) {
        return false;
    }
    if (endFrame >= n) {
        endFrame = n - 1;
    }

    return hasCorrectionInRange(startFrame, endFrame + 1);
}

bool PitchCurveSnapshot::getCorrectedVisibleTimeBounds(double& outStartSeconds, double& outEndSeconds) const {
    outStartSeconds = 0.0;
    outEndSeconds = 0.0;

    if (correctedSegments_.empty() || hopSize_ <= 0 || sampleRate_ <= 0.0) {
        return false;
    }

    int minFrame = correctedSegments_.front().startFrame;
    int maxFrameExcl = correctedSegments_.front().endFrame;
    for (const auto& seg : correctedSegments_) {
        if (seg.startFrame < minFrame) {
            minFrame = seg.startFrame;
        }
        if (seg.endFrame > maxFrameExcl) {
            maxFrameExcl = seg.endFrame;
        }
    }
    if (maxFrameExcl <= minFrame) {
        return false;
    }

    const double secondsPerFrame = static_cast<double>(hopSize_) / sampleRate_;
    outStartSeconds = static_cast<double>(minFrame) * secondsPerFrame;
    outEndSeconds = static_cast<double>(maxFrameExcl) * secondsPerFrame;
    if (outStartSeconds < 0) outStartSeconds = 0.0;
    if (outEndSeconds < outStartSeconds) outEndSeconds = outStartSeconds;
    return true;
}

bool PitchCurveSnapshot::getCorrectedVisibleOverlapInRange(double startSeconds, double endSeconds,
                                                            double& outOverlapStart, double& outOverlapEnd) const {
    outOverlapStart = 0.0;
    outOverlapEnd = 0.0;

    double visStart = 0.0;
    double visEnd = 0.0;
    if (!getCorrectedVisibleTimeBounds(visStart, visEnd)) {
        return false;
    }

    if (endSeconds <= startSeconds) {
        return false;
    }

    const double a0 = std::max(0.0, startSeconds);
    const double a1 = endSeconds;
    const double b0 = std::max(0.0, visStart);
    const double b1 = std::max(0.0, visEnd);
    const double s = std::max(a0, b0);
    const double e = std::min(a1, b1);
    if (e <= s) {
        return false;
    }

    outOverlapStart = s;
    outOverlapEnd = e;
    return true;
}

void PitchCurve::applyCorrectionToRange(
    const std::vector<Note>& notes,
    int startFrame,
    int endFrame,
    float retuneSpeed,
    float vibratoDepth,
    float vibratoRate,
    double audioSampleRate)
{
    auto oldSnapshot = getSnapshot();
    const auto& originalF0 = oldSnapshot->getOriginalF0();
    
    if (originalF0.empty() || startFrame >= endFrame) {
        return;
    }

    const int maxFrame = static_cast<int>(originalF0.size());
    if (startFrame >= maxFrame) return;
    if (endFrame > maxFrame) endFrame = maxFrame;
    if (startFrame < 0) startFrame = 0;

    auto correctedSegments = oldSnapshot->getCorrectedSegments();
    clearSegmentsInRangePreserveOutside(correctedSegments, startFrame, endFrame);

    const int hopSize = oldSnapshot->getHopSize();
    const double sampleRate = oldSnapshot->getSampleRate();
    if (hopSize <= 0 || sampleRate <= 0.0 || audioSampleRate <= 0.0) {
        return;
    }

    struct NoteCorrectionInfo {
        float anchorPitch = 0.0f;
        float anchorMidi = 0.0f;
        float offsetSemitones = 0.0f;
        float rotationRad = 0.0f;
        float timeCenterSeconds = 0.0f;
    };

    std::vector<NoteCorrectionInfo> noteInfos(notes.size());

    const float radToDeg = 180.0f / juce::MathConstants<float>::pi;
    const float slopeAngleMinDeg = 10.0f;
    const float slopeAngleMaxDeg = 30.0f;
    const float slopeAt45DegSemitonesPerSecond = 7.0f;

    const double framePerSecond = sampleRate / static_cast<double>(hopSize);
    std::vector<size_t> relevantNoteIndices;
    for (size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
        const auto& note = notes[noteIndex];

        size_t noteStartFrame = static_cast<size_t>(std::max(0, static_cast<int>(std::floor(note.startTime * framePerSecond))));
        size_t noteEndFrame = static_cast<size_t>(std::max(0, static_cast<int>(std::ceil(note.endTime * framePerSecond))));

        if (static_cast<int>(noteEndFrame) <= startFrame || static_cast<int>(noteStartFrame) >= endFrame) {
            continue;
        }

        relevantNoteIndices.push_back(noteIndex);

        NoteCorrectionInfo info;
        float anchorPitch = note.originalPitch;
        if (anchorPitch <= 0.0f) anchorPitch = note.pitch;
        info.anchorPitch = anchorPitch;
        info.anchorMidi = PitchUtils::freqToMidi(anchorPitch);
        info.timeCenterSeconds = static_cast<float>((note.startTime + note.endTime) * 0.5);

        float targetBaseF0 = note.getAdjustedPitch();
        if (targetBaseF0 > 0.0f && anchorPitch > 0.0f) {
            info.offsetSemitones = PitchUtils::freqToMidi(targetBaseF0) - PitchUtils::freqToMidi(anchorPitch);
        }

        if (info.anchorMidi > 0.0f && noteStartFrame < noteEndFrame) {
            std::vector<float> voicedTimes;
            std::vector<float> voicedMidis;
            for (size_t f = noteStartFrame; f < noteEndFrame && f < originalF0.size(); ++f) {
                float f0 = originalF0[f];
                if (f0 <= 0.0f) continue;
                float tSec = static_cast<float>(static_cast<double>(f) * static_cast<double>(hopSize) / sampleRate);
                voicedTimes.push_back(tSec);
                voicedMidis.push_back(PitchUtils::freqToMidi(f0));
            }

            if (voicedTimes.size() >= 6) {
                size_t n = voicedTimes.size();
                size_t segCount = std::max<size_t>(3, n / 5);

                std::vector<float> earlyMidis(voicedMidis.begin(), voicedMidis.begin() + segCount);
                std::vector<float> lateMidis(voicedMidis.end() - segCount, voicedMidis.end());
                std::sort(earlyMidis.begin(), earlyMidis.end());
                std::sort(lateMidis.begin(), lateMidis.end());

                float earlyMidi = earlyMidis[earlyMidis.size() / 2];
                float lateMidi = lateMidis[lateMidis.size() / 2];

                float earlyTime = voicedTimes[segCount / 2];
                float lateTime = voicedTimes[n - segCount + (segCount / 2)];

                float deltaTime = lateTime - earlyTime;
                if (deltaTime > 0.0001f) {
                    float slope = (lateMidi - earlyMidi) / deltaTime;
                    float signedAngleRad = std::atan(slope / slopeAt45DegSemitonesPerSecond);
                    float absAngleDeg = std::abs(signedAngleRad * radToDeg);

                    if (absAngleDeg >= slopeAngleMinDeg && absAngleDeg <= slopeAngleMaxDeg) {
                        info.rotationRad = -signedAngleRad;
                    }
                }
            }
        }

        noteInfos[noteIndex] = info;
    }

    if (relevantNoteIndices.empty()) {
        return;
    }

    std::vector<float> correctedF0Buffer(endFrame - startFrame, 0.0f);
    std::vector<int> activeNotePerFrame(endFrame - startFrame, -1);

    for (int i = startFrame; i < endFrame; ++i) {
        float f0 = originalF0[i];
        if (f0 <= 0.0f) {
            correctedF0Buffer[i - startFrame] = 0.0f;
            continue;
        }

        int64_t audioSamplePos = static_cast<int64_t>(std::llround(static_cast<double>(i) * static_cast<double>(hopSize) * audioSampleRate / sampleRate));
        double timeSeconds = static_cast<double>(audioSamplePos) / audioSampleRate;

        const Note* activeNote = nullptr;
        size_t activeNoteIndex = 0;
        for (size_t idx : relevantNoteIndices) {
            const auto& note = notes[idx];
            if (timeSeconds >= note.startTime && timeSeconds < note.endTime) {
                activeNote = &note;
                activeNoteIndex = idx;
                break;
            }
        }

        if (activeNote) {
            activeNotePerFrame[i - startFrame] = static_cast<int>(activeNoteIndex);

            float targetBaseF0 = activeNote->getAdjustedPitch();
            float targetF0 = targetBaseF0;

            float noteVibratoDepth = vibratoDepth;
            float noteVibratoRate = vibratoRate;
            if (activeNote->vibratoDepth >= 0.0f) noteVibratoDepth = activeNote->vibratoDepth;
            if (activeNote->vibratoRate >= 0.0f) noteVibratoRate = activeNote->vibratoRate;
            if (noteVibratoDepth > 0.0f) {
                double timeInNote = timeSeconds - activeNote->startTime;
                float depthSemitones = (noteVibratoDepth / 100.0f) * 1.0f;
                float lfoValue = depthSemitones * std::sin(2.0f * juce::MathConstants<float>::pi * noteVibratoRate * (float)timeInNote);
                targetF0 *= std::pow(2.0f, lfoValue / 12.0f);
            }

            float baseF0 = f0;
            if (activeNoteIndex < noteInfos.size() && noteInfos[activeNoteIndex].rotationRad != 0.0f) {
                float tSec = static_cast<float>(timeSeconds);
                float x = tSec - noteInfos[activeNoteIndex].timeCenterSeconds;
                float y = PitchUtils::freqToMidi(f0) - noteInfos[activeNoteIndex].anchorMidi;
                float c = std::cos(noteInfos[activeNoteIndex].rotationRad);
                float s = std::sin(noteInfos[activeNoteIndex].rotationRad);
                float yRot = x * s + y * c;
                baseF0 = PitchUtils::midiToFreq(noteInfos[activeNoteIndex].anchorMidi + yRot);
            }

            float shiftedF0 = baseF0;
            if (activeNoteIndex < noteInfos.size() && noteInfos[activeNoteIndex].anchorPitch > 0.0f && targetBaseF0 > 0.0f) {
                float shiftRatio = std::pow(2.0f, noteInfos[activeNoteIndex].offsetSemitones / 12.0f);
                shiftedF0 = baseF0 * shiftRatio;
            }

            float frameRetuneSpeed = retuneSpeed;
            if (activeNote->retuneSpeed >= 0.0f) {
                frameRetuneSpeed = activeNote->retuneSpeed;
            }

            correctedF0Buffer[i - startFrame] = PitchUtils::mixRetune(shiftedF0, targetF0, frameRetuneSpeed);
        } else {
            correctedF0Buffer[i - startFrame] = f0;
        }
    }

    // Instead of creating one giant segment for the entire range,
    // create individual segments per note to minimize undo/redo re-render scope
    for (size_t idx : relevantNoteIndices) {
        const auto& note = notes[idx];
        
        size_t noteStartFrame = static_cast<size_t>(std::max(0, static_cast<int>(std::floor(note.startTime * framePerSecond))));
        size_t noteEndFrame = static_cast<size_t>(std::max(0, static_cast<int>(std::ceil(note.endTime * framePerSecond))));
        
        // Clamp to the requested range
        int segStart = std::max(startFrame, static_cast<int>(noteStartFrame));
        int segEnd = std::min(endFrame, static_cast<int>(noteEndFrame));
        
        if (segStart >= segEnd) continue;
        
        // Extract corrected F0 for this note's range
        std::vector<float> noteF0Data(segEnd - segStart);
        for (int i = segStart; i < segEnd; ++i) {
            noteF0Data[i - segStart] = correctedF0Buffer[i - startFrame];
        }
        
        CorrectedSegment noteSeg(segStart, segEnd, noteF0Data, CorrectedSegment::Source::NoteBased);
        noteSeg.retuneSpeed = (note.retuneSpeed >= 0.0f) ? note.retuneSpeed : retuneSpeed;
        noteSeg.vibratoDepth = (note.vibratoDepth >= 0.0f) ? note.vibratoDepth : vibratoDepth;
        noteSeg.vibratoRate = (note.vibratoRate >= 0.0f) ? note.vibratoRate : vibratoRate;
        
        insertSegmentWithUnifiedTransitions(correctedSegments, originalF0, std::move(noteSeg), kUnifiedTransitionFrames);
    }

    uint64_t newGen = incrementGeneration();
    auto newSnapshot = std::make_shared<const PitchCurveSnapshot>(
        oldSnapshot->getOriginalF0(),
        oldSnapshot->getOriginalEnergy(),
        std::move(correctedSegments),
        oldSnapshot->getAnchorGroups(),
        hopSize,
        sampleRate,
        newGen
    );
    std::atomic_store(&snapshot_, newSnapshot);
}

void PitchCurve::setManualCorrectionRange(int startFrame, int endFrame, const std::vector<float>& f0Data,
                                          CorrectedSegment::Source source) {
    if (startFrame >= endFrame || f0Data.empty()) {
        return;
    }

    auto oldSnapshot = getSnapshot();
    auto correctedSegments = oldSnapshot->getCorrectedSegments();
    
    CorrectedSegment newSeg(startFrame, endFrame, f0Data, source);
    clearSegmentsInRangePreserveOutside(correctedSegments, startFrame, endFrame);
    insertSegmentWithUnifiedTransitions(correctedSegments, oldSnapshot->getOriginalF0(), std::move(newSeg), kUnifiedTransitionFrames);

    uint64_t newGen = incrementGeneration();
    auto newSnapshot = std::make_shared<const PitchCurveSnapshot>(
        oldSnapshot->getOriginalF0(),
        oldSnapshot->getOriginalEnergy(),
        std::move(correctedSegments),
        oldSnapshot->getAnchorGroups(),
        oldSnapshot->getHopSize(),
        oldSnapshot->getSampleRate(),
        newGen
    );
    std::atomic_store(&snapshot_, newSnapshot);
}

void PitchCurve::setManualCorrectionRange(int startFrame, int endFrame, const std::vector<float>& f0Data,
                                          CorrectedSegment::Source source, float retuneSpeed) {
    if (startFrame >= endFrame || f0Data.empty()) {
        return;
    }

    auto oldSnapshot = getSnapshot();
    auto correctedSegments = oldSnapshot->getCorrectedSegments();
    
    CorrectedSegment newSeg(startFrame, endFrame, f0Data, source);
    newSeg.retuneSpeed = retuneSpeed;
    clearSegmentsInRangePreserveOutside(correctedSegments, startFrame, endFrame);
    insertSegmentWithUnifiedTransitions(correctedSegments, oldSnapshot->getOriginalF0(), std::move(newSeg), kUnifiedTransitionFrames);

    uint64_t newGen = incrementGeneration();
    auto newSnapshot = std::make_shared<const PitchCurveSnapshot>(
        oldSnapshot->getOriginalF0(),
        oldSnapshot->getOriginalEnergy(),
        std::move(correctedSegments),
        oldSnapshot->getAnchorGroups(),
        oldSnapshot->getHopSize(),
        oldSnapshot->getSampleRate(),
        newGen
    );
    std::atomic_store(&snapshot_, newSnapshot);
}

void PitchCurve::clearCorrectionRange(int startFrame, int endFrame) {
    if (startFrame >= endFrame) {
        return;
    }

    auto oldSnapshot = getSnapshot();
    auto correctedSegments = oldSnapshot->getCorrectedSegments();
    clearSegmentsInRangePreserveOutside(correctedSegments, startFrame, endFrame);

    uint64_t newGen = incrementGeneration();
    auto newSnapshot = std::make_shared<const PitchCurveSnapshot>(
        oldSnapshot->getOriginalF0(),
        oldSnapshot->getOriginalEnergy(),
        std::move(correctedSegments),
        oldSnapshot->getAnchorGroups(),
        oldSnapshot->getHopSize(),
        oldSnapshot->getSampleRate(),
        newGen
    );
    std::atomic_store(&snapshot_, newSnapshot);
}

// ============================================================================
// Anchor-based correction
// ============================================================================

void PitchCurve::setAnchorGroups(const std::vector<AnchorGroup>& groups,
                                  const std::vector<Note>& notes,
                                  float retuneSpeed) {
    auto oldSnapshot = getSnapshot();
    const auto& originalF0 = oldSnapshot->getOriginalF0();
    const int hopSize = oldSnapshot->getHopSize();
    const double sampleRate = oldSnapshot->getSampleRate();

    if (originalF0.empty() || hopSize <= 0 || sampleRate <= 0.0) return;

    const double frameDuration = static_cast<double>(hopSize) / sampleRate;
    const double framePerSecond = sampleRate / static_cast<double>(hopSize);

    // Remove all existing LineAnchor segments, keep other sources
    auto segments = oldSnapshot->getCorrectedSegments();
    segments.erase(
        std::remove_if(segments.begin(), segments.end(),
            [](const CorrectedSegment& s) {
                return s.source == CorrectedSegment::Source::LineAnchor;
            }),
        segments.end());

    // For each anchor group with >= 2 points, generate F0 via Hermite interpolation
    for (const auto& group : groups) {
        if (group.points.size() < 2) continue;

        const double groupStart = group.getStartTime();
        const double groupEnd = group.getEndTime();
        int groupStartFrame = std::max(0, static_cast<int>(std::floor(groupStart * framePerSecond)));
        int groupEndFrame = std::min(static_cast<int>(originalF0.size()),
                                     static_cast<int>(std::ceil(groupEnd * framePerSecond)) + 1);
        if (groupEndFrame <= groupStartFrame) continue;

        // Hermite interpolation in MIDI pitch space
        auto midiPitchData = HermiteInterpolation::interpolate(group, groupStartFrame, groupEndFrame, frameDuration);
        HermiteInterpolation::midiToHz(midiPitchData);

        // Clip to note boundaries (only create segments inside notes)
        for (const auto& note : notes) {
            int noteStartFrame = static_cast<int>(std::floor(note.startTime * framePerSecond));
            int noteEndFrame = static_cast<int>(std::ceil(note.endTime * framePerSecond));

            int overlapStart = std::max(groupStartFrame, noteStartFrame);
            int overlapEnd = std::min(groupEndFrame, noteEndFrame);
            if (overlapEnd <= overlapStart) continue;

            std::vector<float> clippedF0;
            clippedF0.reserve(overlapEnd - overlapStart);
            for (int f = overlapStart; f < overlapEnd; ++f) {
                int idx = f - groupStartFrame;
                if (idx >= 0 && idx < static_cast<int>(midiPitchData.size()))
                    clippedF0.push_back(midiPitchData[idx]);
                else
                    clippedF0.push_back(0.0f);
            }

            // Clear any existing segment in this range before inserting
            clearSegmentsInRangePreserveOutside(segments, overlapStart, overlapEnd);

            CorrectedSegment newSeg(overlapStart, overlapEnd, clippedF0, CorrectedSegment::Source::LineAnchor);
            newSeg.retuneSpeed = -1.0f;
            insertSegmentWithUnifiedTransitions(segments, originalF0, std::move(newSeg), kUnifiedTransitionFrames);
        }
    }

    uint64_t newGen = incrementGeneration();
    auto newSnapshot = std::make_shared<const PitchCurveSnapshot>(
        oldSnapshot->getOriginalF0(),
        oldSnapshot->getOriginalEnergy(),
        std::move(segments),
        groups,
        oldSnapshot->getHopSize(),
        oldSnapshot->getSampleRate(),
        newGen
    );
    std::atomic_store(&snapshot_, newSnapshot);
}

void PitchCurve::restoreSegmentsAndAnchors(const std::vector<CorrectedSegment>& segments,
                                            const std::vector<AnchorGroup>& anchors) {
    auto oldSnapshot = getSnapshot();
    uint64_t newGen = incrementGeneration();
    auto newSnapshot = std::make_shared<const PitchCurveSnapshot>(
        oldSnapshot->getOriginalF0(),
        oldSnapshot->getOriginalEnergy(),
        segments,
        anchors,
        oldSnapshot->getHopSize(),
        oldSnapshot->getSampleRate(),
        newGen
    );
    std::atomic_store(&snapshot_, newSnapshot);
}

namespace {

std::vector<CorrectedSegment> extractCorrectedSegmentsForSubrange(
    const std::vector<CorrectedSegment>& segments,
    int lo,
    int hi,
    int rebase)
{
    std::vector<CorrectedSegment> out;
    if (hi <= lo) {
        return out;
    }

    for (const auto& seg : segments) {
        if (seg.endFrame <= lo || seg.startFrame >= hi) {
            continue;
        }

        const int ns = juce::jmax(seg.startFrame, lo);
        const int ne = juce::jmin(seg.endFrame, hi);
        if (ne <= ns) {
            continue;
        }

        const int srcOff = ns - seg.startFrame;
        const int len = ne - ns;
        if (srcOff < 0 || len <= 0 || srcOff + len > static_cast<int>(seg.f0Data.size())) {
            continue;
        }

        CorrectedSegment copy = seg;
        copy.startFrame = ns - rebase;
        copy.endFrame = ne - rebase;
        copy.f0Data.assign(seg.f0Data.begin() + srcOff, seg.f0Data.begin() + srcOff + len);
        out.push_back(std::move(copy));
    }

    std::sort(out.begin(), out.end(), [](const CorrectedSegment& a, const CorrectedSegment& b) {
        return a.startFrame < b.startFrame;
    });
    return out;
}

std::vector<AnchorGroup> extractAnchorGroupsForSubrange(
    const std::vector<AnchorGroup>& groups,
    double splitSec,
    bool leftPart)
{
    std::vector<AnchorGroup> result;
    for (const auto& g : groups) {
        AnchorGroup ng;
        for (const auto& pt : g.points) {
            if (leftPart) {
                if (pt.time < splitSec) {
                    ng.points.push_back(pt);
                }
            } else {
                if (pt.time >= splitSec) {
                    AnchorPoint p = pt;
                    p.time -= splitSec;
                    ng.points.push_back(p);
                }
            }
        }
        if (ng.points.size() >= 2u) {
            ng.sortByTime();
            result.push_back(std::move(ng));
        }
    }
    return result;
}

} // namespace

std::shared_ptr<PitchCurve> PitchCurve::createFrameSubrangeCopy(int startFrame, int endExclusive) const
{
    auto snap = getSnapshot();
    const auto& f0 = snap->getOriginalF0();
    const int n = static_cast<int>(f0.size());
    if (n <= 0 || startFrame < 0 || endExclusive <= startFrame) {
        return nullptr;
    }

    const int hi = juce::jmin(endExclusive, n);
    if (hi <= startFrame) {
        return nullptr;
    }

    const int hop = snap->getHopSize();
    const double sr = snap->getSampleRate();

    std::vector<float> subF0(f0.begin() + startFrame, f0.begin() + hi);
    std::vector<float> subEn;
    const auto& en = snap->getOriginalEnergy();
    if (en.size() >= static_cast<size_t>(hi)) {
        subEn.assign(en.begin() + startFrame, en.begin() + hi);
    } else if (!en.empty()) {
        subEn.assign(en.begin() + juce::jmin(startFrame, static_cast<int>(en.size())),
            en.begin() + juce::jmin(hi, static_cast<int>(en.size())));
        if (static_cast<int>(subEn.size()) != hi - startFrame) {
            subEn.resize(static_cast<size_t>(hi - startFrame), 0.0f);
        }
    } else {
        subEn.assign(static_cast<size_t>(hi - startFrame), 0.0f);
    }

    const auto segs = extractCorrectedSegmentsForSubrange(snap->getCorrectedSegments(), startFrame, hi, startFrame);
    std::vector<AnchorGroup> anchors;
    if (startFrame == 0) {
        const double boundarySec = static_cast<double>(hi) * static_cast<double>(hop) / sr;
        anchors = extractAnchorGroupsForSubrange(snap->getAnchorGroups(), boundarySec, true);
    } else {
        const double boundarySec = static_cast<double>(startFrame) * static_cast<double>(hop) / sr;
        anchors = extractAnchorGroupsForSubrange(snap->getAnchorGroups(), boundarySec, false);
    }

    auto out = std::make_shared<PitchCurve>();
    out->clear();
    out->setHopSize(hop);
    out->setSampleRate(sr);
    out->setOriginalF0(subF0);
    out->setOriginalEnergy(subEn);
    out->restoreSegmentsAndAnchors(segs, anchors);
    return out;
}

std::shared_ptr<PitchCurve> PitchCurve::mergeSequentialCurves(const PitchCurve& left, const PitchCurve& right)
{
    auto L = left.getSnapshot();
    auto R = right.getSnapshot();
    if (L->getHopSize() != R->getHopSize()) {
        return nullptr;
    }
    if (std::abs(L->getSampleRate() - R->getSampleRate()) > 1e-6) {
        return nullptr;
    }

    const int hop = L->getHopSize();
    const double sr = L->getSampleRate();
    const int leftFrames = static_cast<int>(L->getOriginalF0().size());
    const int rightFrames = static_cast<int>(R->getOriginalF0().size());
    if (leftFrames <= 0 || rightFrames <= 0) {
        return nullptr;
    }

    const double leftDurSec =
        static_cast<double>(leftFrames) * static_cast<double>(hop) / sr;

    std::vector<float> mergedF0 = L->getOriginalF0();
    mergedF0.insert(mergedF0.end(), R->getOriginalF0().begin(), R->getOriginalF0().end());

    std::vector<float> mergedEn = L->getOriginalEnergy();
    if (mergedEn.size() < static_cast<size_t>(leftFrames)) {
        mergedEn.resize(static_cast<size_t>(leftFrames), 0.0f);
    }
    const auto& rEn = R->getOriginalEnergy();
    if (rEn.size() >= static_cast<size_t>(rightFrames)) {
        mergedEn.insert(mergedEn.end(), rEn.begin(), rEn.begin() + rightFrames);
    } else {
        for (int i = 0; i < rightFrames; ++i) {
            mergedEn.push_back(i < static_cast<int>(rEn.size()) ? rEn[static_cast<size_t>(i)] : 0.0f);
        }
    }
    if (mergedEn.size() < mergedF0.size()) {
        mergedEn.resize(mergedF0.size(), 0.0f);
    }

    std::vector<CorrectedSegment> mergedSegs = L->getCorrectedSegments();
    for (auto seg : R->getCorrectedSegments()) {
        seg.startFrame += leftFrames;
        seg.endFrame += leftFrames;
        mergedSegs.push_back(std::move(seg));
    }
    std::sort(mergedSegs.begin(), mergedSegs.end(), [](const CorrectedSegment& a, const CorrectedSegment& b) {
        return a.startFrame < b.startFrame;
    });

    std::vector<AnchorGroup> mergedAnchors = L->getAnchorGroups();
    for (auto g : R->getAnchorGroups()) {
        for (auto& pt : g.points) {
            pt.time += leftDurSec;
        }
        if (g.points.size() >= 2u) {
            g.sortByTime();
            mergedAnchors.push_back(std::move(g));
        }
    }

    auto out = std::make_shared<PitchCurve>();
    out->clear();
    out->setHopSize(hop);
    out->setSampleRate(sr);
    out->setOriginalF0(mergedF0);
    out->setOriginalEnergy(mergedEn);
    out->restoreSegmentsAndAnchors(mergedSegs, mergedAnchors);
    return out;
}

std::shared_ptr<PitchCurve> PitchCurve::mergeSequentialCurvesWithGap(const PitchCurve& left, const PitchCurve& right,
                                                                     double gapSeconds)
{
    if (gapSeconds <= 1e-12) {
        return mergeSequentialCurves(left, right);
    }

    auto L = left.getSnapshot();
    auto R = right.getSnapshot();
    if (L->getHopSize() != R->getHopSize()) {
        return nullptr;
    }
    if (std::abs(L->getSampleRate() - R->getSampleRate()) > 1e-6) {
        return nullptr;
    }

    const int hop = L->getHopSize();
    const double sr = L->getSampleRate();
    const int leftFrames = static_cast<int>(L->getOriginalF0().size());
    const int rightFrames = static_cast<int>(R->getOriginalF0().size());
    if (leftFrames <= 0 || rightFrames <= 0 || hop <= 0 || sr <= 1e-9) {
        return nullptr;
    }

    const int gapFrames = static_cast<int>(std::llround(gapSeconds * sr / static_cast<double>(hop)));
    const int gapPad = juce::jmax(0, gapFrames);

    const double leftDurSec =
        static_cast<double>(leftFrames) * static_cast<double>(hop) / sr;

    std::vector<float> mergedF0 = L->getOriginalF0();
    mergedF0.insert(mergedF0.end(), static_cast<size_t>(gapPad), 0.0f);
    mergedF0.insert(mergedF0.end(), R->getOriginalF0().begin(), R->getOriginalF0().end());

    std::vector<float> mergedEn = L->getOriginalEnergy();
    if (mergedEn.size() < static_cast<size_t>(leftFrames)) {
        mergedEn.resize(static_cast<size_t>(leftFrames), 0.0f);
    }
    mergedEn.insert(mergedEn.end(), static_cast<size_t>(gapPad), 0.0f);
    const auto& rEn = R->getOriginalEnergy();
    if (rEn.size() >= static_cast<size_t>(rightFrames)) {
        mergedEn.insert(mergedEn.end(), rEn.begin(), rEn.begin() + rightFrames);
    } else {
        for (int i = 0; i < rightFrames; ++i) {
            mergedEn.push_back(i < static_cast<int>(rEn.size()) ? rEn[static_cast<size_t>(i)] : 0.0f);
        }
    }
    if (mergedEn.size() < mergedF0.size()) {
        mergedEn.resize(mergedF0.size(), 0.0f);
    }

    const int shift = leftFrames + gapPad;
    std::vector<CorrectedSegment> mergedSegs = L->getCorrectedSegments();
    for (auto seg : R->getCorrectedSegments()) {
        seg.startFrame += shift;
        seg.endFrame += shift;
        mergedSegs.push_back(std::move(seg));
    }
    std::sort(mergedSegs.begin(), mergedSegs.end(), [](const CorrectedSegment& a, const CorrectedSegment& b) {
        return a.startFrame < b.startFrame;
    });

    const double anchorShift = leftDurSec + gapSeconds;
    std::vector<AnchorGroup> mergedAnchors = L->getAnchorGroups();
    for (auto g : R->getAnchorGroups()) {
        for (auto& pt : g.points) {
            pt.time += anchorShift;
        }
        if (g.points.size() >= 2u) {
            g.sortByTime();
            mergedAnchors.push_back(std::move(g));
        }
    }

    auto out = std::make_shared<PitchCurve>();
    out->clear();
    out->setHopSize(hop);
    out->setSampleRate(sr);
    out->setOriginalF0(mergedF0);
    out->setOriginalEnergy(mergedEn);
    out->restoreSegmentsAndAnchors(mergedSegs, mergedAnchors);
    return out;
}

} // namespace OpenTune
