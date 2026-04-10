#pragma once

/**
 * Monotonic Hermite spline interpolation for pitch curves.
 *
 * Uses Fritsch-Carlson slopes (harmonic mean) to guarantee monotonicity
 * between anchors, producing smooth C1-continuous curves without overshoot.
 * All pitch values are in MIDI semitone space (perceptually uniform).
 */

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

namespace OpenTune {

struct AnchorPoint {
    double time = 0.0;      // seconds
    float pitch = 0.0f;     // MIDI pitch (semitones, e.g. 69.0 = A4)
    bool selected = false;
    float dragBasePitch = 0.0f;  // transient: pitch snapshot at drag start
    uint32_t uid = 0;            // transient: unique ID for drag tracking

    bool operator<(const AnchorPoint& other) const { return time < other.time; }

    static uint32_t nextUid() {
        static uint32_t counter = 1;
        return counter++;
    }
};

class AnchorGroup {
public:
    std::vector<AnchorPoint> points;

    bool empty() const { return points.size() < 2; }
    double getStartTime() const { return points.empty() ? 0.0 : points.front().time; }
    double getEndTime() const { return points.empty() ? 0.0 : points.back().time; }

    void sortByTime() {
        std::sort(points.begin(), points.end());
    }

    int insertPoint(const AnchorPoint& pt) {
        auto it = std::lower_bound(points.begin(), points.end(), pt);
        auto pos = it - points.begin();
        points.insert(it, pt);
        return static_cast<int>(pos);
    }

    void removePoint(int index) {
        if (index >= 0 && index < static_cast<int>(points.size()))
            points.erase(points.begin() + index);
    }

    void moveSelectedPoints(double timeDelta, float pitchDelta) {
        for (auto& pt : points) {
            if (pt.selected) {
                pt.time += timeDelta;
                pt.pitch += pitchDelta;
            }
        }
        sortByTime();
    }

    void deselectAll() {
        for (auto& pt : points)
            pt.selected = false;
    }

    bool hasSelected() const {
        for (const auto& pt : points)
            if (pt.selected) return true;
        return false;
    }

    std::vector<int> getSelectedIndices() const {
        std::vector<int> indices;
        for (int i = 0; i < static_cast<int>(points.size()); ++i)
            if (points[i].selected) indices.push_back(i);
        return indices;
    }

    void deleteSelected() {
        points.erase(
            std::remove_if(points.begin(), points.end(),
                [](const AnchorPoint& p) { return p.selected; }),
            points.end());
    }
};

namespace HermiteInterpolation {

/**
 * Compute Fritsch-Carlson monotone slope at an interior point.
 * Endpoint slopes are 0 (curve enters/exits horizontally).
 */
inline float computeSlope(const std::vector<AnchorPoint>& pts, int index) {
    const int n = static_cast<int>(pts.size());
    if (index <= 0 || index >= n - 1)
        return 0.0f;

    const double dx0 = pts[index].time - pts[index - 1].time;
    const double dx1 = pts[index + 1].time - pts[index].time;
    if (dx0 <= 0.0 || dx1 <= 0.0)
        return 0.0f;

    const float kLeft  = (pts[index].pitch - pts[index - 1].pitch) / static_cast<float>(dx0);
    const float kRight = (pts[index + 1].pitch - pts[index].pitch) / static_cast<float>(dx1);

    const float product = kLeft * kRight;
    if (product <= 0.0f)
        return 0.0f;

    return 2.0f / (1.0f / kLeft + 1.0f / kRight);
}

/**
 * Hermite basis interpolation between two points.
 * @param y0, y1  Values at endpoints
 * @param m0, m1  Slopes at endpoints
 * @param t       Normalised parameter [0, 1]
 * @param h       Interval width (x1 - x0)
 */
inline float hermiteBasis(float y0, float y1, float m0, float m1, float t, float h) {
    const float s = 1.0f - t;
    const float d1 = t * h;
    const float d2 = (t - 1.0f) * h;
    return (1.0f + 2.0f * t) * s * s * y0
         + (1.0f + 2.0f * s) * t * t * y1
         + d1 * s * s * m0
         + d2 * t * t * m1;
}

/**
 * Compute clamped Fritsch-Carlson slopes for a set of anchor points.
 * Applies per-point harmonic mean followed by per-interval α²+β² ≤ 9
 * overshoot prevention. Used by both rendering and UI drawing.
 */
inline std::vector<float> computeClampedSlopes(const std::vector<AnchorPoint>& pts) {
    const int n = static_cast<int>(pts.size());
    std::vector<float> slopes(n);
    for (int i = 0; i < n; ++i)
        slopes[i] = computeSlope(pts, i);

    for (int k = 0; k < n - 1; ++k) {
        const float dx = static_cast<float>(pts[k + 1].time - pts[k].time);
        if (dx <= 0.0f) continue;

        const float delta = (pts[k + 1].pitch - pts[k].pitch) / dx;

        if (std::abs(delta) < 1e-7f) {
            slopes[k] = 0.0f;
            slopes[k + 1] = 0.0f;
            continue;
        }

        float alpha = slopes[k] / delta;
        float beta  = slopes[k + 1] / delta;

        if (alpha < 0.0f) { slopes[k] = 0.0f; alpha = 0.0f; }
        if (beta  < 0.0f) { slopes[k + 1] = 0.0f; beta = 0.0f; }

        const float r2 = alpha * alpha + beta * beta;
        if (r2 > 9.0f) {
            const float tau = 3.0f / std::sqrt(r2);
            slopes[k]     = tau * alpha * delta;
            slopes[k + 1] = tau * beta  * delta;
        }
    }
    return slopes;
}

/**
 * Interpolate an AnchorGroup at the given frame positions.
 *
 * @param group          Anchor group (must have >= 2 points, sorted by time)
 * @param startFrame     First frame index
 * @param endFrame       End frame index (exclusive)
 * @param frameDuration  Seconds per frame (hopSize / sampleRate)
 * @return               Vector of MIDI pitch values (one per frame)
 */
inline std::vector<float> interpolate(const AnchorGroup& group,
                                       int startFrame, int endFrame,
                                       double frameDuration) {
    const int count = endFrame - startFrame;
    if (count <= 0 || group.points.size() < 2)
        return {};

    const auto& pts = group.points;
    const int n = static_cast<int>(pts.size());

    const auto slopes = computeClampedSlopes(pts);

    std::vector<float> result(count);
    int ptIdx = 1;

    for (int f = 0; f < count; ++f) {
        const double t = static_cast<double>(startFrame + f) * frameDuration;

        if (t <= pts.front().time) {
            result[f] = pts.front().pitch;
            continue;
        }
        if (t >= pts.back().time) {
            result[f] = pts.back().pitch;
            continue;
        }

        while (ptIdx < n - 1 && pts[ptIdx].time < t)
            ++ptIdx;

        const auto& p0 = pts[ptIdx - 1];
        const auto& p1 = pts[ptIdx];
        const float h = static_cast<float>(p1.time - p0.time);
        if (h <= 0.0f) {
            result[f] = p0.pitch;
            continue;
        }

        const float param = static_cast<float>(t - p0.time) / h;
        result[f] = hermiteBasis(p0.pitch, p1.pitch,
                                  slopes[ptIdx - 1], slopes[ptIdx],
                                  param, h);
    }

    return result;
}

/**
 * Convert a MIDI-pitch array to Hz array in-place.
 */
inline void midiToHz(std::vector<float>& data) {
    for (auto& v : data) {
        if (v > 0.0f)
            v = 440.0f * std::pow(2.0f, (v - 69.0f) / 12.0f);
    }
}

/**
 * Auto-fit anchors to an F0 curve within a single contiguous voiced region.
 */
inline AnchorGroup fitToF0(const std::vector<float>& f0Hz,
                            int startFrame, int endFrame,
                            double frameDuration,
                            float toleranceCents = 10.0f,
                            int maxAnchors = 300) {
    AnchorGroup group;
    if (endFrame <= startFrame) return group;

    auto freqToMidi = [](float hz) -> float {
        return (hz > 0.0f) ? 69.0f + 12.0f * std::log2(hz / 440.0f) : 0.0f;
    };

    std::vector<float> midiValues(endFrame - startFrame);
    for (int f = startFrame; f < endFrame; ++f) {
        float hz = (f >= 0 && f < static_cast<int>(f0Hz.size())) ? f0Hz[f] : 0.0f;
        midiValues[f - startFrame] = freqToMidi(hz);
    }

    int firstVoiced = -1, lastVoiced = -1;
    for (int i = 0; i < static_cast<int>(midiValues.size()); ++i) {
        if (midiValues[i] > 0.0f) {
            if (firstVoiced < 0) firstVoiced = i;
            lastVoiced = i;
        }
    }
    if (firstVoiced < 0 || lastVoiced <= firstVoiced) return group;

    auto frameToTime = [&](int localIdx) -> double {
        return static_cast<double>(startFrame + localIdx) * frameDuration;
    };

    AnchorPoint first;
    first.time = frameToTime(firstVoiced);
    first.pitch = midiValues[firstVoiced];
    group.points.push_back(first);

    AnchorPoint last;
    last.time = frameToTime(lastVoiced);
    last.pitch = midiValues[lastVoiced];
    group.points.push_back(last);

    for (int iter = 0; iter < maxAnchors && static_cast<int>(group.points.size()) < maxAnchors; ++iter) {
        auto interp = interpolate(group, startFrame, endFrame, frameDuration);

        float maxErr = 0.0f;
        int maxErrLocalIdx = -1;

        for (int i = firstVoiced; i <= lastVoiced; ++i) {
            if (midiValues[i] <= 0.0f) continue;
            float errCents = std::abs(interp[i] - midiValues[i]) * 100.0f;
            if (errCents > maxErr) {
                maxErr = errCents;
                maxErrLocalIdx = i;
            }
        }

        if (maxErr <= toleranceCents || maxErrLocalIdx < 0)
            break;

        AnchorPoint newPt;
        newPt.time = frameToTime(maxErrLocalIdx);
        newPt.pitch = midiValues[maxErrLocalIdx];
        group.insertPoint(newPt);
    }

    return group;
}

/**
 * Per-note auto-fit: generates one AnchorGroup per note, only within voiced
 * regions. Notes with no voiced frames are skipped. Unvoiced gaps inside a
 * note are NOT bridged — the anchor group only covers the voiced extent.
 *
 * @param f0Hz           F0 in Hz (could be corrected or original)
 * @param notes          Note list (must be non-empty)
 * @param frameDuration  Seconds per frame (hopSize / sampleRate)
 * @param toleranceCents Max allowed fitting error
 * @param maxPerNote     Max anchors per note
 */
template <typename NoteT>
inline std::vector<AnchorGroup> fitToF0PerNote(
        const std::vector<float>& f0Hz,
        const std::vector<NoteT>& notes,
        double frameDuration,
        float toleranceCents = 10.0f,
        int maxPerNote = 80) {
    std::vector<AnchorGroup> result;
    if (f0Hz.empty() || notes.empty() || frameDuration <= 0.0) return result;

    const double framePerSecond = 1.0 / frameDuration;
    const int totalFrames = static_cast<int>(f0Hz.size());

    for (const auto& note : notes) {
        int noteStart = std::max(0, static_cast<int>(std::floor(note.startTime * framePerSecond)));
        int noteEnd = std::min(totalFrames, static_cast<int>(std::ceil(note.endTime * framePerSecond)));
        if (noteEnd <= noteStart) continue;

        auto group = fitToF0(f0Hz, noteStart, noteEnd, frameDuration,
                              toleranceCents, maxPerNote);
        if (group.points.size() >= 2)
            result.push_back(std::move(group));
    }

    return result;
}

} // namespace HermiteInterpolation
} // namespace OpenTune
