#pragma once

#include "Note.h"
#include "VocalSegment.h"
#include "TimeCoordinate.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <utility>
#include <cmath>

namespace juce {
class ValueTree;
}

namespace OpenTune {

struct PhonemeItem {
    uint64_t id{0};
    int64_t startTick{0};
    int64_t endTick{0};
    juce::String token;
    bool lockLeft{false};
    bool lockRight{false};

    double getStartClipSeconds() const { return TimeCoordinate::clipTickToSeconds(startTick); }
    double getEndClipSeconds() const { return TimeCoordinate::clipTickToSeconds(endTick); }
    void setStartClipSeconds(double sec) { startTick = TimeCoordinate::clipSecondsToTick(sec); }
    void setEndClipSeconds(double sec) { endTick = TimeCoordinate::clipSecondsToTick(sec); }
};

struct ParameterCurve {
    juce::String curveId;
    std::vector<std::pair<double, float>> points;
    bool userAuthored{false};
};

struct ManualOverrides {
    /** Clip-local boundaries on the render tick grid (1 tick = 1/44100 s). */
    std::vector<int64_t> lockedPhonemeBoundaryTicks;
    std::vector<uint64_t> lockedNoteIds;
    // Phase 1 freeze: kept only for binary/source compatibility.
    std::vector<ParameterCurve> curveOverrides;
};

/**
 * Single source of truth per clip for singing edit (segments, notes, phonemes, curves, overrides).
 * Notes / phonemes / segment geometry use clip-local int64 ticks (see TimeCoordinate).
 * UI and JSON may still exchange seconds at the boundary.
 */
class SingingEditDocument {
public:
    struct ValidationResult {
        bool ok{true};
        juce::String errorCode;
        juce::String errorMessage;
        std::vector<juce::String> warnings;
    };

    SingingEditDocument();
    SingingEditDocument(const SingingEditDocument& other);
    SingingEditDocument& operator=(const SingingEditDocument& other);
    SingingEditDocument(SingingEditDocument&& other) noexcept;
    SingingEditDocument& operator=(SingingEditDocument&& other) noexcept;
    ~SingingEditDocument() = default;

    uint64_t getDocumentRevision() const noexcept { return documentRevision_; }
    void bumpDocumentRevision() noexcept { ++documentRevision_; }

    VocalSegmentSequence& getSegments() noexcept { return segments_; }
    const VocalSegmentSequence& getSegments() const noexcept { return segments_; }

    std::vector<Note>& getNotes() noexcept { return notes_; }
    const std::vector<Note>& getNotes() const noexcept { return notes_; }

    std::vector<PhonemeItem>& getPhonemes() noexcept { return phonemes_; }
    const std::vector<PhonemeItem>& getPhonemes() const noexcept { return phonemes_; }

    std::vector<ParameterCurve>& getParameterCurves() noexcept { return parameterCurves_; }
    const std::vector<ParameterCurve>& getParameterCurves() const noexcept { return parameterCurves_; }

    ManualOverrides& getManualOverrides() noexcept { return manualOverrides_; }
    const ManualOverrides& getManualOverrides() const noexcept { return manualOverrides_; }

    void setDocumentRevisionForLoad(uint64_t rev) noexcept { documentRevision_ = rev; }

    /** Assign stableId to notes where missing (0). */
    void ensureStableNoteIds();

    /** If no segments, add one [0, durationSec). */
    void ensureDefaultFullClipSegment(double durationSec);

    /** Split document at clip-local seconds into left/right (for clip split). @return false if validation fails. */
    bool splitByLocalSeconds(double splitSec, SingingEditDocument& outLeft, SingingEditDocument& outRight) const;

    /** Merge right document into this, shifting right times by rightTimeShiftSec (for clip merge). */
    void mergeFromRight(const SingingEditDocument& right, double rightTimeShiftSec);

    /** Validate + normalize with explicit error boundary. */
    ValidationResult validateAndNormalize(double clipDurationSec);

    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

    static void appendNotesLegacy(juce::ValueTree& parent, const std::vector<Note>& notes);
    static void parseNotesLegacy(const juce::ValueTree& notesTree, std::vector<Note>& outNotes);

    /** After merge: ensure phoneme ids unique and sorted by time. */
    void renumberPhonemeIds();

private:
    static int64_t secondsToTick(double sec)
    {
        return static_cast<int64_t>(std::llround(sec * TimeCoordinate::kRenderSampleRate));
    }
    static double tickToSeconds(int64_t tick)
    {
        return static_cast<double>(tick) / TimeCoordinate::kRenderSampleRate;
    }

    uint64_t documentRevision_{1};
    VocalSegmentSequence segments_;
    std::vector<Note> notes_;
    std::vector<PhonemeItem> phonemes_;
    // Phase 1 freeze: no serialize/bridge/apply path.
    std::vector<ParameterCurve> parameterCurves_;
    ManualOverrides manualOverrides_;
    uint64_t nextPhonemeId_{1};
};

} // namespace OpenTune
