#include "SingingEditDocument.h"
#include "TimeCoordinate.h"
#include "AppLogger.h"
#include <juce_data_structures/juce_data_structures.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace OpenTune {

namespace {

void splitNotesAtLocalSecondsImpl(const std::vector<Note>& src, double splitT, std::vector<Note>& outLeft,
                                    std::vector<Note>& outRight, uint64_t& nextStableId)
{
    outLeft.clear();
    outRight.clear();
    const int64_t splitTick = TimeCoordinate::clipSecondsToTick(splitT);
    for (Note n : src) {
        if (n.endTick <= splitTick) {
            outLeft.push_back(n);
        } else if (n.startTick >= splitTick) {
            n.startTick -= splitTick;
            n.endTick -= splitTick;
            outRight.push_back(n);
        } else {
            Note nl = n;
            nl.endTick = splitTick;
            if (nl.endTick > nl.startTick) {
                outLeft.push_back(nl);
            }
            Note nr = n;
            nr.startTick = 0;
            nr.endTick = n.endTick - splitTick;
            if (nr.endTick > nr.startTick) {
                nr.stableId = nextStableId++;
                outRight.push_back(nr);
            }
        }
    }
}

void clipPhonemesLeft(const std::vector<PhonemeItem>& src, double splitT, std::vector<PhonemeItem>& out)
{
    const int64_t splitTick = TimeCoordinate::clipSecondsToTick(splitT);
    out.clear();
    for (PhonemeItem p : src) {
        if (p.endTick <= splitTick) {
            out.push_back(p);
        } else if (p.startTick < splitTick) {
            p.endTick = splitTick;
            if (p.endTick > p.startTick) {
                out.push_back(p);
            }
        }
    }
}

void clipPhonemesRight(const std::vector<PhonemeItem>& src, double splitT, std::vector<PhonemeItem>& out)
{
    const int64_t splitTick = TimeCoordinate::clipSecondsToTick(splitT);
    out.clear();
    for (PhonemeItem p : src) {
        if (p.startTick >= splitTick) {
            p.startTick -= splitTick;
            p.endTick -= splitTick;
            out.push_back(p);
        } else if (p.endTick > splitTick) {
            p.startTick = 0;
            p.endTick -= splitTick;
            if (p.endTick > p.startTick) {
                out.push_back(p);
            }
        }
    }
}

void clipSegmentsLeft(const std::vector<VocalSegment>& src, double splitT, std::vector<VocalSegment>& out)
{
    const int64_t splitTick = TimeCoordinate::clipSecondsToTick(splitT);
    out.clear();
    for (VocalSegment s : src) {
        if (s.endTick <= splitTick) {
            out.push_back(s);
        } else if (s.startTick < splitTick) {
            s.endTick = splitTick;
            if (s.endTick > s.startTick) {
                out.push_back(s);
            }
        }
    }
}

void clipSegmentsRight(const std::vector<VocalSegment>& src, double splitT, std::vector<VocalSegment>& out)
{
    const int64_t splitTick = TimeCoordinate::clipSecondsToTick(splitT);
    out.clear();
    for (VocalSegment s : src) {
        if (s.startTick >= splitTick) {
            s.startTick -= splitTick;
            s.endTick -= splitTick;
            out.push_back(s);
        } else if (s.endTick > splitTick) {
            s.startTick = 0;
            s.endTick -= splitTick;
            if (s.endTick > s.startTick) {
                out.push_back(s);
            }
        }
    }
}

template <typename T>
void sortAndUnique(std::vector<T>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

SingingEditDocument::SingingEditDocument() = default;

SingingEditDocument::SingingEditDocument(const SingingEditDocument& other) = default;

SingingEditDocument& SingingEditDocument::operator=(const SingingEditDocument& other) = default;

SingingEditDocument::SingingEditDocument(SingingEditDocument&& other) noexcept = default;

SingingEditDocument& SingingEditDocument::operator=(SingingEditDocument&& other) noexcept = default;

void SingingEditDocument::ensureStableNoteIds()
{
    uint64_t maxId = 0;
    for (const auto& n : notes_) {
        maxId = std::max(maxId, n.stableId);
    }
    uint64_t next = std::max<uint64_t>(1, maxId + 1);
    for (auto& n : notes_) {
        if (n.stableId == 0) {
            n.stableId = next++;
        }
    }
}

void SingingEditDocument::ensureDefaultFullClipSegment(double durationSec)
{
    auto& segs = segments_.getSegments();
    if (!segs.empty() || durationSec <= 1e-9) {
        return;
    }
    VocalSegment s;
    s.id = segments_.allocateId();
    s.setStartClipSeconds(0.0);
    s.setEndClipSeconds(durationSec);
    s.gainLinear = 1.0f;
    s.tension01 = 0.5f;
    segs.push_back(s);
    segments_.sortAndValidate(durationSec);
}

bool SingingEditDocument::splitByLocalSeconds(double splitSec, SingingEditDocument& outLeft,
                                              SingingEditDocument& outRight) const
{
    outLeft = *this;
    outRight = *this;

    uint64_t nextStable = 1;
    for (const auto& n : notes_) {
        nextStable = std::max(nextStable, n.stableId + 1);
    }
    splitNotesAtLocalSecondsImpl(notes_, splitSec, outLeft.notes_, outRight.notes_, nextStable);

    const std::vector<VocalSegment> segCopy = segments_.getSegments();
    clipSegmentsLeft(segCopy, splitSec, outLeft.segments_.getSegments());
    clipSegmentsRight(segCopy, splitSec, outRight.segments_.getSegments());

    const std::vector<PhonemeItem> phCopy = phonemes_;
    clipPhonemesLeft(phCopy, splitSec, outLeft.phonemes_);
    clipPhonemesRight(phCopy, splitSec, outRight.phonemes_);

    outLeft.segments_.sortAndValidate(splitSec);
    outRight.segments_.sortAndValidate(0.0);

    const int64_t splitTick = TimeCoordinate::clipSecondsToTick(splitSec);
    outLeft.manualOverrides_.lockedPhonemeBoundaryTicks.clear();
    outRight.manualOverrides_.lockedPhonemeBoundaryTicks.clear();
    for (int64_t tick : manualOverrides_.lockedPhonemeBoundaryTicks) {
        if (tick >= 0 && tick <= splitTick) {
            outLeft.manualOverrides_.lockedPhonemeBoundaryTicks.push_back(tick);
        }
        if (tick >= splitTick) {
            outRight.manualOverrides_.lockedPhonemeBoundaryTicks.push_back(tick - splitTick);
        }
    }
    sortAndUnique(outLeft.manualOverrides_.lockedPhonemeBoundaryTicks);
    sortAndUnique(outRight.manualOverrides_.lockedPhonemeBoundaryTicks);

    std::unordered_set<uint64_t> leftNoteIds;
    for (const auto& n : outLeft.notes_) {
        leftNoteIds.insert(n.stableId);
    }
    std::unordered_set<uint64_t> rightNoteIds;
    for (const auto& n : outRight.notes_) {
        rightNoteIds.insert(n.stableId);
    }

    outLeft.manualOverrides_.lockedNoteIds.clear();
    outRight.manualOverrides_.lockedNoteIds.clear();
    for (uint64_t id : manualOverrides_.lockedNoteIds) {
        if (leftNoteIds.find(id) != leftNoteIds.end()) {
            outLeft.manualOverrides_.lockedNoteIds.push_back(id);
        }
        if (rightNoteIds.find(id) != rightNoteIds.end()) {
            outRight.manualOverrides_.lockedNoteIds.push_back(id);
        }
    }
    sortAndUnique(outLeft.manualOverrides_.lockedNoteIds);
    sortAndUnique(outRight.manualOverrides_.lockedNoteIds);

    // Phase 1 freeze: never carry curve overrides/parameter curves through split.
    outLeft.parameterCurves_.clear();
    outRight.parameterCurves_.clear();
    outLeft.manualOverrides_.curveOverrides.clear();
    outRight.manualOverrides_.curveOverrides.clear();

    outLeft.documentRevision_ = documentRevision_;
    outRight.documentRevision_ = documentRevision_;
    outLeft.nextPhonemeId_ = nextPhonemeId_;
    outRight.nextPhonemeId_ = nextPhonemeId_;
    const auto vLeft = outLeft.validateAndNormalize(splitSec);
    const auto vRight = outRight.validateAndNormalize(0.0);
    return vLeft.ok && vRight.ok;
}

void SingingEditDocument::mergeFromRight(const SingingEditDocument& right, double rightTimeShiftSec)
{
    std::vector<Note> rn = right.notes_;
    const int64_t shiftTick = TimeCoordinate::clipSecondsToTick(rightTimeShiftSec);
    for (auto& n : rn) {
        n.startTick += shiftTick;
        n.endTick += shiftTick;
    }
    const size_t firstRightNote = notes_.size();
    notes_.insert(notes_.end(), rn.begin(), rn.end());
    std::unordered_set<uint64_t> usedStable;
    usedStable.reserve(notes_.size());
    for (size_t i = 0; i < firstRightNote; ++i) {
        usedStable.insert(notes_[i].stableId);
    }
    std::unordered_map<uint64_t, uint64_t> stableRemap;
    uint64_t nextAlloc = 1;
    for (const auto& n : notes_) {
        nextAlloc = std::max(nextAlloc, n.stableId + 1);
    }
    for (size_t i = firstRightNote; i < notes_.size(); ++i) {
        Note& n = notes_[i];
        if (n.stableId == 0 || usedStable.find(n.stableId) != usedStable.end()) {
            const uint64_t oldId = n.stableId;
            while (usedStable.find(nextAlloc) != usedStable.end()) {
                ++nextAlloc;
            }
            n.stableId = nextAlloc++;
            usedStable.insert(n.stableId);
            if (oldId != 0) {
                stableRemap[oldId] = n.stableId;
            }
        } else {
            usedStable.insert(n.stableId);
        }
    }
    std::sort(notes_.begin(), notes_.end(), [](const Note& a, const Note& b) {
        return a.startTick < b.startTick || (a.startTick == b.startTick && a.endTick < b.endTick);
    });

    std::vector<VocalSegment> rs = right.segments_.getSegments();
    for (auto& s : rs) {
        s.startTick += shiftTick;
        s.endTick += shiftTick;
    }
    auto& segs = segments_.getSegments();
    segs.insert(segs.end(), rs.begin(), rs.end());
    segments_.sortAndValidate(0.0);
    segments_.reassignSequentialIds();

    std::vector<PhonemeItem> rp = right.phonemes_;
    for (auto& p : rp) {
        p.startTick += shiftTick;
        p.endTick += shiftTick;
    }
    phonemes_.insert(phonemes_.end(), rp.begin(), rp.end());
    std::sort(phonemes_.begin(), phonemes_.end(),
              [](const PhonemeItem& a, const PhonemeItem& b) { return a.startTick < b.startTick; });

    for (int64_t tick : right.manualOverrides_.lockedPhonemeBoundaryTicks) {
        manualOverrides_.lockedPhonemeBoundaryTicks.push_back(tick + shiftTick);
    }
    sortAndUnique(manualOverrides_.lockedPhonemeBoundaryTicks);
    for (uint64_t id : right.manualOverrides_.lockedNoteIds) {
        auto itRem = stableRemap.find(id);
        manualOverrides_.lockedNoteIds.push_back(itRem != stableRemap.end() ? itRem->second : id);
    }
    sortAndUnique(manualOverrides_.lockedNoteIds);
    // Phase 1 freeze.
    manualOverrides_.curveOverrides.clear();
    parameterCurves_.clear();

    renumberPhonemeIds();
    // Caller must validate with real clip duration; internal merge keeps tick ordering.
}

void SingingEditDocument::renumberPhonemeIds()
{
    std::sort(phonemes_.begin(), phonemes_.end(),
              [](const PhonemeItem& a, const PhonemeItem& b) { return a.startTick < b.startTick; });
    uint64_t id = 1;
    for (auto& p : phonemes_) {
        p.id = id++;
    }
    nextPhonemeId_ = id;
}

void SingingEditDocument::appendNotesLegacy(juce::ValueTree& parent, const std::vector<Note>& notes)
{
    juce::ValueTree notesTree("Notes");
    for (const auto& n : notes) {
        juce::ValueTree nv("Note");
        nv.setProperty("stableId", static_cast<juce::int64>(n.stableId), nullptr);
        nv.setProperty("start_tick", static_cast<juce::int64>(n.startTick), nullptr);
        nv.setProperty("end_tick", static_cast<juce::int64>(n.endTick), nullptr);
        nv.setProperty("pitch", n.pitch, nullptr);
        nv.setProperty("originalPitch", n.originalPitch, nullptr);
        nv.setProperty("pitchOffset", n.pitchOffset, nullptr);
        nv.setProperty("retuneSpeed", n.retuneSpeed, nullptr);
        nv.setProperty("vibratoDepth", n.vibratoDepth, nullptr);
        nv.setProperty("vibratoRate", n.vibratoRate, nullptr);
        nv.setProperty("velocity", n.velocity, nullptr);
        nv.setProperty("isVoiced", n.isVoiced, nullptr);
        nv.setProperty("selected", n.selected, nullptr);
        nv.setProperty("dirty", n.dirty, nullptr);
        notesTree.addChild(nv, -1, nullptr);
    }
    parent.addChild(notesTree, -1, nullptr);
}

void SingingEditDocument::parseNotesLegacy(const juce::ValueTree& notesTree, std::vector<Note>& outNotes)
{
    outNotes.clear();
    if (!notesTree.isValid()) {
        return;
    }
    for (auto nv : notesTree) {
        if (!nv.hasType("Note")) {
            continue;
        }
        Note n;
        n.stableId = static_cast<uint64_t>(static_cast<juce::int64>(nv.getProperty("stableId", 0)));
        if (nv.hasProperty("start_tick") && nv.hasProperty("end_tick")) {
            n.startTick = static_cast<int64_t>(static_cast<juce::int64>(nv.getProperty("start_tick", 0)));
            n.endTick = static_cast<int64_t>(static_cast<juce::int64>(nv.getProperty("end_tick", 0)));
        } else {
            const double st = static_cast<double>(nv.getProperty("startTime", 0.0));
            const double en = static_cast<double>(nv.getProperty("endTime", 0.0));
            n.startTick = TimeCoordinate::clipSecondsToTick(st);
            n.endTick = TimeCoordinate::clipSecondsToTick(en);
        }
        n.pitch = static_cast<float>(static_cast<double>(nv.getProperty("pitch", 0.0)));
        n.originalPitch = static_cast<float>(static_cast<double>(nv.getProperty("originalPitch", 0.0)));
        n.pitchOffset = static_cast<float>(static_cast<double>(nv.getProperty("pitchOffset", 0.0)));
        n.retuneSpeed = static_cast<float>(static_cast<double>(nv.getProperty("retuneSpeed", -1.0)));
        n.vibratoDepth = static_cast<float>(static_cast<double>(nv.getProperty("vibratoDepth", -1.0)));
        n.vibratoRate = static_cast<float>(static_cast<double>(nv.getProperty("vibratoRate", -1.0)));
        n.velocity = static_cast<float>(static_cast<double>(nv.getProperty("velocity", 1.0)));
        n.isVoiced = static_cast<bool>(nv.getProperty("isVoiced", true));
        n.selected = static_cast<bool>(nv.getProperty("selected", false));
        n.dirty = static_cast<bool>(nv.getProperty("dirty", false));
        outNotes.push_back(n);
    }
}

juce::ValueTree SingingEditDocument::toValueTree() const
{
    juce::ValueTree root("SingingEdit");
    root.setProperty("revision", static_cast<juce::int64>(documentRevision_), nullptr);

    juce::ValueTree segs("Segments");
    for (const auto& s : segments_.getSegments()) {
        juce::ValueTree sv("Segment");
        sv.setProperty("id", static_cast<juce::int64>(s.id), nullptr);
        sv.setProperty("start_tick", static_cast<juce::int64>(s.startTick), nullptr);
        sv.setProperty("end_tick", static_cast<juce::int64>(s.endTick), nullptr);
        sv.setProperty("gainLinear", s.gainLinear, nullptr);
        sv.setProperty("tension01", s.tension01, nullptr);
        sv.setProperty("flags", static_cast<int>(s.flags), nullptr);
        sv.setProperty("label", s.label, nullptr);
        segs.addChild(sv, -1, nullptr);
    }
    root.addChild(segs, -1, nullptr);

    appendNotesLegacy(root, notes_);

    juce::ValueTree phTree("Phonemes");
    for (const auto& p : phonemes_) {
        juce::ValueTree pv("Phoneme");
        pv.setProperty("id", static_cast<juce::int64>(p.id), nullptr);
        pv.setProperty("start_tick", static_cast<juce::int64>(p.startTick), nullptr);
        pv.setProperty("end_tick", static_cast<juce::int64>(p.endTick), nullptr);
        pv.setProperty("token", p.token, nullptr);
        pv.setProperty("lockLeft", p.lockLeft, nullptr);
        pv.setProperty("lockRight", p.lockRight, nullptr);
        phTree.addChild(pv, -1, nullptr);
    }
    root.addChild(phTree, -1, nullptr);

    juce::ValueTree mo("ManualOverrides");
    juce::ValueTree lb("LockedPhonemeBoundaries");
    for (int64_t tick : manualOverrides_.lockedPhonemeBoundaryTicks) {
        juce::ValueTree tv("T");
        tv.setProperty("v", static_cast<juce::int64>(tick), nullptr);
        lb.addChild(tv, -1, nullptr);
    }
    mo.addChild(lb, -1, nullptr);
    juce::ValueTree ln("LockedNoteIds");
    for (uint64_t id : manualOverrides_.lockedNoteIds) {
        juce::ValueTree iv("Id");
        iv.setProperty("v", static_cast<juce::int64>(id), nullptr);
        ln.addChild(iv, -1, nullptr);
    }
    mo.addChild(ln, -1, nullptr);
    root.addChild(mo, -1, nullptr);

    return root;
}

void SingingEditDocument::fromValueTree(const juce::ValueTree& tree)
{
    if (!tree.isValid() || !tree.hasType("SingingEdit")) {
        return;
    }
    documentRevision_ = static_cast<uint64_t>(static_cast<juce::int64>(tree.getProperty("revision", 1)));

    segments_.getSegments().clear();
    auto segs = tree.getChildWithName("Segments");
    if (segs.isValid()) {
        for (auto sv : segs) {
            if (!sv.hasType("Segment")) {
                continue;
            }
            VocalSegment s;
            s.id = static_cast<uint64_t>(static_cast<juce::int64>(sv.getProperty("id", 0)));
            if (sv.hasProperty("start_tick") && sv.hasProperty("end_tick")) {
                s.startTick = static_cast<int64_t>(static_cast<juce::int64>(sv.getProperty("start_tick", 0)));
                s.endTick = static_cast<int64_t>(static_cast<juce::int64>(sv.getProperty("end_tick", 0)));
            } else {
                s.setStartClipSeconds(static_cast<double>(sv.getProperty("startSec", 0.0)));
                s.setEndClipSeconds(static_cast<double>(sv.getProperty("endSec", 0.0)));
            }
            s.gainLinear = static_cast<float>(static_cast<double>(sv.getProperty("gainLinear", 1.0)));
            s.tension01 = static_cast<float>(static_cast<double>(sv.getProperty("tension01", 0.5)));
            s.flags = static_cast<uint32_t>(static_cast<int>(sv.getProperty("flags", 0)));
            s.label = sv.getProperty("label", "").toString();
            segments_.getSegments().push_back(s);
        }
    }
    segments_.sortAndValidate(0.0);

    auto notesTree = tree.getChildWithName("Notes");
    parseNotesLegacy(notesTree, notes_);
    ensureStableNoteIds();

    phonemes_.clear();
    auto phTree = tree.getChildWithName("Phonemes");
    if (phTree.isValid()) {
        for (auto pv : phTree) {
            if (!pv.hasType("Phoneme")) {
                continue;
            }
            PhonemeItem p;
            p.id = static_cast<uint64_t>(static_cast<juce::int64>(pv.getProperty("id", 0)));
            if (pv.hasProperty("start_tick") && pv.hasProperty("end_tick")) {
                p.startTick = static_cast<int64_t>(static_cast<juce::int64>(pv.getProperty("start_tick", 0)));
                p.endTick = static_cast<int64_t>(static_cast<juce::int64>(pv.getProperty("end_tick", 0)));
            } else {
                p.setStartClipSeconds(static_cast<double>(pv.getProperty("startSec", 0.0)));
                p.setEndClipSeconds(static_cast<double>(pv.getProperty("endSec", 0.0)));
            }
            p.token = pv.getProperty("token", "").toString();
            p.lockLeft = static_cast<bool>(pv.getProperty("lockLeft", false));
            p.lockRight = static_cast<bool>(pv.getProperty("lockRight", false));
            phonemes_.push_back(p);
        }
    }
    uint64_t maxPh = 0;
    for (const auto& p : phonemes_) {
        maxPh = std::max(maxPh, p.id);
    }
    nextPhonemeId_ = std::max<uint64_t>(nextPhonemeId_, maxPh + 1);

    manualOverrides_.lockedPhonemeBoundaryTicks.clear();
    manualOverrides_.lockedNoteIds.clear();
    auto mo = tree.getChildWithName("ManualOverrides");
    if (mo.isValid()) {
        auto lb = mo.getChildWithName("LockedPhonemeBoundaries");
        if (lb.isValid()) {
            for (auto c : lb) {
                if (c.hasType("T")) {
                    const juce::var v = c.getProperty("v", 0);
                    if (v.isDouble()) {
                        manualOverrides_.lockedPhonemeBoundaryTicks.push_back(
                            TimeCoordinate::clipSecondsToTick(static_cast<double>(v)));
                    } else {
                        manualOverrides_.lockedPhonemeBoundaryTicks.push_back(
                            static_cast<int64_t>(static_cast<juce::int64>(v)));
                    }
                }
            }
        }
        auto ln = mo.getChildWithName("LockedNoteIds");
        if (ln.isValid()) {
            for (auto c : ln) {
                if (c.hasType("Id")) {
                    manualOverrides_.lockedNoteIds.push_back(
                        static_cast<uint64_t>(static_cast<juce::int64>(c.getProperty("v", 0))));
                }
            }
        }
    }

    // Phase 1 freeze.
    parameterCurves_.clear();
    manualOverrides_.curveOverrides.clear();
    const auto loadVal = validateAndNormalize(0.0);
    if (!loadVal.ok) {
        AppLogger::error("SingingEditDocument::fromValueTree: " + loadVal.errorMessage);
    }
}

SingingEditDocument::ValidationResult SingingEditDocument::validateAndNormalize(double clipDurationSec)
{
    ValidationResult result;
    const int64_t clipEndTick = (clipDurationSec > 0.0) ? secondsToTick(clipDurationSec) : -1;

    segments_.sortAndValidate(clipDurationSec > 0.0 ? clipDurationSec : 0.0);
    ensureStableNoteIds();

    for (auto& n : notes_) {
        int64_t st = n.startTick;
        int64_t en = n.endTick;
        if (clipEndTick >= 0) {
            st = std::max<int64_t>(0, std::min<int64_t>(st, clipEndTick));
            en = std::max<int64_t>(0, std::min<int64_t>(en, clipEndTick));
        } else {
            st = std::max<int64_t>(0, st);
            en = std::max<int64_t>(0, en);
        }
        if (en <= st) {
            result.ok = false;
            result.errorCode = "INVALID_RANGE";
            result.errorMessage = "Invalid note range";
            return result;
        }
        n.startTick = st;
        n.endTick = en;
    }
    std::sort(notes_.begin(), notes_.end(), [](const Note& a, const Note& b) {
        return a.startTick < b.startTick || (a.startTick == b.startTick && a.endTick < b.endTick);
    });

    for (auto& p : phonemes_) {
        int64_t st = p.startTick;
        int64_t en = p.endTick;
        if (clipEndTick >= 0) {
            st = std::max<int64_t>(0, std::min<int64_t>(st, clipEndTick));
            en = std::max<int64_t>(0, std::min<int64_t>(en, clipEndTick));
        } else {
            st = std::max<int64_t>(0, st);
            en = std::max<int64_t>(0, en);
        }
        if (en <= st) {
            result.ok = false;
            result.errorCode = "INVALID_RANGE";
            result.errorMessage = "Invalid phoneme range";
            return result;
        }
        p.startTick = st;
        p.endTick = en;
    }
    std::sort(phonemes_.begin(), phonemes_.end(),
              [](const PhonemeItem& a, const PhonemeItem& b) { return a.startTick < b.startTick; });
    renumberPhonemeIds();

    std::unordered_set<uint64_t> validNoteIds;
    for (const auto& n : notes_) {
        validNoteIds.insert(n.stableId);
    }
    std::vector<uint64_t> filteredLockedNotes;
    filteredLockedNotes.reserve(manualOverrides_.lockedNoteIds.size());
    for (uint64_t id : manualOverrides_.lockedNoteIds) {
        if (validNoteIds.find(id) != validNoteIds.end()) {
            filteredLockedNotes.push_back(id);
        }
    }
    manualOverrides_.lockedNoteIds = std::move(filteredLockedNotes);
    sortAndUnique(manualOverrides_.lockedNoteIds);

    std::vector<int64_t> filteredBoundaryTicks;
    filteredBoundaryTicks.reserve(manualOverrides_.lockedPhonemeBoundaryTicks.size());
    for (int64_t tick : manualOverrides_.lockedPhonemeBoundaryTicks) {
        int64_t t = tick;
        if (clipEndTick >= 0) {
            t = std::max<int64_t>(0, std::min<int64_t>(t, clipEndTick));
        } else {
            t = std::max<int64_t>(0, t);
        }
        filteredBoundaryTicks.push_back(t);
    }
    manualOverrides_.lockedPhonemeBoundaryTicks = std::move(filteredBoundaryTicks);
    sortAndUnique(manualOverrides_.lockedPhonemeBoundaryTicks);

    // Phase 1 freeze.
    parameterCurves_.clear();
    manualOverrides_.curveOverrides.clear();
    return result;
}

} // namespace OpenTune
