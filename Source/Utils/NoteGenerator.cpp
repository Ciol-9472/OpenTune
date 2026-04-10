#include "NoteGenerator.h"
#include "SimdPerceptualPitchEstimator.h"
#include "PitchUtils.h"

#include <algorithm>
#include <cmath>

namespace OpenTune {

static constexpr int kMajorSemitones[]     = {0, 2, 4, 5, 7, 9, 11};
static constexpr int kMinorSemitones[]     = {0, 2, 3, 5, 7, 8, 10};
static constexpr int kChromaticSemitones[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

const int* ScaleSnapConfig::semitones(ScaleMode mode, int& outCount) noexcept
{
    switch (mode) {
        case ScaleMode::Major:
            outCount = 7;
            return kMajorSemitones;
        case ScaleMode::Minor:
            outCount = 7;
            return kMinorSemitones;
        case ScaleMode::Chromatic:
        default:
            outCount = 12;
            return kChromaticSemitones;
    }
}

float ScaleSnapConfig::snapMidi(float midiNote) const noexcept
{
    if (mode == ScaleMode::Chromatic) return midiNote;

    int count = 0;
    const int* tones = semitones(mode, count);

    float pc = std::fmod(midiNote - static_cast<float>(root), 12.0f);
    if (pc < 0.0f) pc += 12.0f;

    float bestDiff = 999.0f;
    int   bestTone = 0;
    for (int i = 0; i < count; ++i) {
        float diff = pc - static_cast<float>(tones[i]);
        if (diff >  6.0f) diff -= 12.0f;
        if (diff < -6.0f) diff += 12.0f;
        if (std::abs(diff) < bestDiff) {
            bestDiff = std::abs(diff);
            bestTone = tones[i];
        }
    }

    float adj = static_cast<float>(bestTone) - pc;
    if (adj >  6.0f) adj -= 12.0f;
    if (adj < -6.0f) adj += 12.0f;

    return midiNote + adj;
}

float NoteGenerator::representativePitch(
    const float* pitches,
    const float* energyWeights,
    int          count,
    float        hopSizeTime)
{
    if (count <= 0 || pitches == nullptr) return 0.0f;

    if (energyWeights != nullptr && hopSizeTime > 0.0f) {
        float pip = SimdPerceptualPitchEstimator::estimatePIP(
            pitches, energyWeights, count, hopSizeTime);
        if (pip > 0.0f) return pip;
    }

    std::vector<float> voiced;
    voiced.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (pitches[i] > 0.0f) voiced.push_back(pitches[i]);
    }
    if (voiced.empty()) return 0.0f;
    std::sort(voiced.begin(), voiced.end());
    return voiced[voiced.size() / 2];
}

float NoteGenerator::quantisePitch(
    float hz,
    const std::optional<ScaleSnapConfig>& snap)
{
    if (hz <= 0.0f) return 0.0f;

    float midi = PitchUtils::freqToMidi(hz);
    if (snap.has_value()) midi = snap->snapMidi(midi);

    return Note::midiToFrequency(static_cast<int>(std::round(midi)));
}

void NoteGenerator::commitNote(
    std::vector<Note>&         out,
    Note&                      current,
    std::vector<float>&        pitches,
    std::vector<float>&        energyBuf,
    float                      hopSizeTime,
    double                     endTime,
    double                     minNoteDuration,
    double                     tailExtendDuration,
    const NoteGeneratorParams& params)
{
    if (pitches.empty()) return;

    current.endTime = endTime + tailExtendDuration;

    if (current.getDuration() >= minNoteDuration) {
        const float* ep  = energyBuf.empty() ? nullptr : energyBuf.data();
        const float  rep = representativePitch(
            pitches.data(), ep,
            static_cast<int>(pitches.size()), hopSizeTime);

        if (rep > 0.0f) {
            current.originalPitch = rep;
            current.pitch         = quantisePitch(rep, params.scaleSnap);
            current.pitchOffset   = 0.0f;
            current.retuneSpeed   = params.retuneSpeed;
            current.vibratoDepth  = params.vibratoDepth;
            current.vibratoRate   = params.vibratoRate;
            out.push_back(current);
        }
    }

    pitches.clear();
    energyBuf.clear();
    current = Note{};
}

void NoteGenerator::commitPartialAtSplit(
    std::vector<Note>&         out,
    Note&                      current,
    std::vector<float>&        pitches,
    std::vector<float>&        energyBuf,
    std::vector<int>&          voicedFrameIndices,
    int                        splitIdx,
    float                      hopSizeTime,
    double                     minNoteDuration,
    double                     tailExtendDuration,
    const NoteGeneratorParams& params)
{
    if (splitIdx <= 0 || splitIdx > static_cast<int>(pitches.size())) return;
    if (splitIdx > static_cast<int>(voicedFrameIndices.size())) return;

    Note done = current;
    std::vector<float> sp(pitches.begin(), pitches.begin() + splitIdx);
    std::vector<float> se;
    if (!energyBuf.empty())
        se.assign(energyBuf.begin(), energyBuf.begin() + splitIdx);

    const int lastF = voicedFrameIndices[static_cast<size_t>(splitIdx) - 1];
    const double endTime = static_cast<double>(lastF + 1) * static_cast<double>(hopSizeTime);
    commitNote(out, done, sp, se, hopSizeTime, endTime, minNoteDuration, tailExtendDuration, params);

    pitches.erase(pitches.begin(), pitches.begin() + splitIdx);
    if (!energyBuf.empty())
        energyBuf.erase(energyBuf.begin(), energyBuf.begin() + splitIdx);
    voicedFrameIndices.erase(voicedFrameIndices.begin(), voicedFrameIndices.begin() + splitIdx);

    if (!voicedFrameIndices.empty())
        current.startTime = static_cast<double>(voicedFrameIndices.front()) * static_cast<double>(hopSizeTime);
}

void NoteGenerator::applyNeighborTolerantScaleSnap(std::vector<Note>& notes, const ScaleSnapConfig& snap)
{
    if (notes.empty()) return;
    if (snap.mode == ScaleMode::Chromatic) return;

    for (size_t i = 0; i < notes.size(); ++i) {
        Note& n = notes[i];
        if (n.originalPitch <= 0.0f) continue;

        const float rawMidi    = PitchUtils::freqToMidi(n.originalPitch);
        const float snappedMidi = snap.snapMidi(rawMidi);
        const int   snappedInt  = static_cast<int>(std::lround(snappedMidi));
        const int   chromaticInt = static_cast<int>(std::lround(rawMidi));

        if (std::abs(snappedMidi - rawMidi) < 0.08f) {
            n.pitch = Note::midiToFrequency(snappedInt);
            n.pitchOffset = 0.0f;
            continue;
        }

        bool nearNeighbor = false;
        if (i > 0 && notes[i - 1].originalPitch > 0.0f) {
            const float prevRaw = PitchUtils::freqToMidi(notes[i - 1].originalPitch);
            if (std::abs(rawMidi - prevRaw) <= 1.0f + 1e-3f) nearNeighbor = true;
        }
        if (!nearNeighbor && i + 1 < notes.size() && notes[i + 1].originalPitch > 0.0f) {
            const float nextRaw = PitchUtils::freqToMidi(notes[i + 1].originalPitch);
            if (std::abs(rawMidi - nextRaw) <= 1.0f + 1e-3f) nearNeighbor = true;
        }

        if (nearNeighbor) {
            n.pitch = Note::midiToFrequency(chromaticInt);
            n.pitchOffset = 0.0f;
        } else {
            n.pitch = Note::midiToFrequency(snappedInt);
            n.pitchOffset = 0.0f;
        }
    }
}

std::vector<Note> NoteGenerator::generate(
    const float*               f0,
    int                        f0Count,
    const float*               energy,
    int                        startFrame,
    int                        endFrameExclusive,
    int                        hopSize,
    double                     f0SampleRate,
    double                     hostSampleRate,
    const NoteGeneratorParams& params)
{
    std::vector<Note> out;

    if (f0 == nullptr || f0Count <= 0 || hopSize <= 0 || f0SampleRate <= 0.0) return out;

    startFrame        = std::max(0, startFrame);
    endFrameExclusive = std::min(endFrameExclusive, f0Count);
    if (startFrame >= endFrameExclusive) return out;

    const double hopSecs = static_cast<double>(hopSize) / f0SampleRate;

    const float effectiveMinDurationMs = std::max(0.0f, params.policy.minDurationMs);
    const double minNoteDuration = std::max(hopSecs, effectiveMinDurationMs / 1000.0);

    const double tailExtendDuration = std::max(0.0, params.policy.tailExtendMs / 1000.0);

    const int gapBridgeFrames = std::max<int>(
        0,
        static_cast<int>(std::ceil(
            (params.policy.gapBridgeMs / 1000.0) / hopSecs)));

    auto frameToTime = [hopSecs](int frame) -> double {
        // 帧索引转换为时间（秒）
        return static_cast<double>(frame) * hopSecs;
    };

    Note               current;
    bool               inNote           = false;
    int                trailingUnvoiced = 0;
    int                lastVoicedFrame  = -1;
    double             segmentPitchSum  = 0.0;
    int                segmentPitchCount = 0;
    std::vector<float> pitches;
    std::vector<float> energyBuf;
    std::vector<int>   voicedFrameIndices;
    pitches.reserve(512);
    if (energy != nullptr) energyBuf.reserve(512);
    voicedFrameIndices.reserve(512);

    const bool trackEnergyValley =
        params.policy.energyValleySplitEnabled && energy != nullptr;
    float segmentPeakEnergy = 0.0f;
    int   energyValleyRun   = 0;

    for (int i = startFrame; i < endFrameExclusive; ++i) {
        const float f0val  = f0[i];
        const bool  voiced = (f0val > 0.0f);

        if (voiced) {
            if (inNote && !pitches.empty() && f0val > 0.0f && pitches.back() > 0.0f) {
                const float jumpCents =
                    std::abs(1200.0f * std::log2(f0val / pitches.back()));
                if (jumpCents >= params.policy.largeJumpSplitCents) {
                    commitNote(out, current, pitches, energyBuf,
                               static_cast<float>(hopSecs), frameToTime(i),
                               minNoteDuration, tailExtendDuration, params);
                    voicedFrameIndices.clear();
                    segmentPeakEnergy = 0.0f;
                    energyValleyRun   = 0;
                    inNote            = false;
                }
            }

            if (!inNote) {
                current           = Note{};
                current.startTime = frameToTime(i);
                current.isVoiced  = true;
                pitches.clear();
                energyBuf.clear();
                voicedFrameIndices.clear();
                inNote             = true;
                trailingUnvoiced   = 0;
                segmentPitchSum    = 0.0;
                segmentPitchCount  = 0;
                segmentPeakEnergy  = 0.0f;
                energyValleyRun    = 0;
            } else {
                trailingUnvoiced = 0;
            }

            if (segmentPitchCount > 0) {
                const float avgPitch = static_cast<float>(segmentPitchSum / static_cast<double>(segmentPitchCount));
                const float diffFromAvg = (avgPitch > 0.0f)
                    ? std::abs(1200.0f * std::log2(f0val / avgPitch)) : 0.0f;

                if (diffFromAvg >= params.policy.transitionThresholdCents) {
                    commitNote(out, current, pitches, energyBuf,
                               static_cast<float>(hopSecs), frameToTime(i),
                               minNoteDuration, tailExtendDuration, params);
                    voicedFrameIndices.clear();
                    segmentPeakEnergy = 0.0f;
                    energyValleyRun   = 0;

                    current             = Note{};
                    current.startTime   = frameToTime(i);
                    current.isVoiced    = true;
                    segmentPitchSum     = 0.0;
                    segmentPitchCount   = 0;
                }
            }

            pitches.push_back(f0val);
            if (energy != nullptr) energyBuf.push_back(energy[i]);
            if (trackEnergyValley) voicedFrameIndices.push_back(i);

            segmentPitchSum += static_cast<double>(f0val);
            ++segmentPitchCount;
            lastVoicedFrame = i;

            if (trackEnergyValley && !energyBuf.empty()) {
                segmentPeakEnergy = std::max(segmentPeakEnergy, energyBuf.back());
                const float e = energyBuf.back();
                if (segmentPeakEnergy > 1e-24f && e < segmentPeakEnergy * params.policy.energyValleyMaxRatio)
                    ++energyValleyRun;
                else
                    energyValleyRun = 0;

                const int minSide = std::max(1, params.policy.energyValleyMinPitchesEachSide);
                if (energyValleyRun >= params.policy.energyValleyMinFrames
                    && static_cast<int>(pitches.size())
                           >= energyValleyRun + minSide * 2) {
                    const int lowStartFrame = i - energyValleyRun + 1;
                    auto      it = std::lower_bound(voicedFrameIndices.begin(), voicedFrameIndices.end(), lowStartFrame);
                    const int splitIdx = static_cast<int>(it - voicedFrameIndices.begin());
                    if (splitIdx >= minSide
                        && static_cast<int>(pitches.size()) - splitIdx >= minSide) {
                        commitPartialAtSplit(
                            out,
                            current,
                            pitches,
                            energyBuf,
                            voicedFrameIndices,
                            splitIdx,
                            static_cast<float>(hopSecs),
                            minNoteDuration,
                            tailExtendDuration,
                            params);

                        segmentPitchSum   = 0.0;
                        segmentPitchCount = 0;
                        for (float p : pitches) {
                            segmentPitchSum += static_cast<double>(p);
                            ++segmentPitchCount;
                        }
                        segmentPeakEnergy = 0.0f;
                        for (float eb : energyBuf)
                            segmentPeakEnergy = std::max(segmentPeakEnergy, eb);
                        energyValleyRun = 0;
                    }
                }
            }

        } else {
            if (inNote) {
                ++trailingUnvoiced;
                if (trailingUnvoiced > gapBridgeFrames) {
                    commitNote(out, current, pitches, energyBuf,
                               static_cast<float>(hopSecs), frameToTime(lastVoicedFrame + 1),
                               minNoteDuration, tailExtendDuration, params);
                    inNote               = false;
                    trailingUnvoiced     = 0;
                    lastVoicedFrame      = -1;
                    segmentPitchSum      = 0.0;
                    segmentPitchCount    = 0;
                    voicedFrameIndices.clear();
                    segmentPeakEnergy    = 0.0f;
                    energyValleyRun      = 0;
                }
            }
        }
    }

    if (inNote && !pitches.empty() && lastVoicedFrame >= 0) {
        commitNote(out, current, pitches, energyBuf,
                   static_cast<float>(hopSecs), frameToTime(lastVoicedFrame + 1),
                   minNoteDuration, tailExtendDuration, params);
    }

    std::sort(out.begin(), out.end(),
              [](const Note& a, const Note& b) {
                  return a.startTime < b.startTime;
              });

    for (size_t k = 1; k < out.size(); ++k) {
        if (out[k - 1].endTime > out[k].startTime)
            out[k - 1].endTime = out[k].startTime;
    }

    out.erase(
        std::remove_if(out.begin(), out.end(),
                       [](const Note& n) {
                         return n.endTime <= n.startTime;
                       }),
        out.end());

    if (params.scaleSnap.has_value())
        applyNeighborTolerantScaleSnap(out, *params.scaleSnap);

    return out;
}

std::vector<Note> NoteGenerator::generate(
    const std::vector<float>&  f0,
    const std::vector<float>&  energy,
    int                        hopSize,
    double                     f0SampleRate,
    double                     hostSampleRate,
    const NoteGeneratorParams& params)
{
    const float* energyPtr = (energy.size() == f0.size() && !energy.empty()) ? energy.data() : nullptr;
    return generate(
        f0.data(),
        static_cast<int>(f0.size()),
        energyPtr,
        0,
        static_cast<int>(f0.size()),
        hopSize,
        f0SampleRate,
        hostSampleRate,
        params);
}

bool NoteGenerator::validate(const std::vector<Note>& notes)
{
    bool ok = true;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].endTime <= notes[i].startTime) {
            DBG("NoteGenerator::validate: zero/negative duration note at index " << i);
            ok = false;
        }
        if (i > 0) {
            if (notes[i].startTime < notes[i - 1].startTime) {
                DBG("NoteGenerator::validate: notes not sorted at index " << i);
                ok = false;
            }
            if (notes[i].startTime < notes[i - 1].endTime) {
                DBG("NoteGenerator::validate: overlapping notes at index " << i);
                ok = false;
            }
        }
    }
    return ok;
}

} // namespace OpenTune
