#include "DsJsonIO.h"
#include "../Utils/SingingEditDocument.h"
#include "../Utils/SingingEditPatch.h"
#include "../Utils/TimeCoordinate.h"
#include <juce_data_structures/juce_data_structures.h>
#include <unordered_set>

namespace OpenTune {
namespace DsJson {

namespace {

juce::var phonemeToVar(const PhonemeItem& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", static_cast<juce::int64>(p.id));
    o->setProperty("start_tick", static_cast<juce::int64>(p.startTick));
    o->setProperty("end_tick", static_cast<juce::int64>(p.endTick));
    o->setProperty("start_sec", p.getStartClipSeconds());
    o->setProperty("end_sec", p.getEndClipSeconds());
    o->setProperty("token", p.token);
    o->setProperty("lock_left", p.lockLeft);
    o->setProperty("lock_right", p.lockRight);
    return juce::var(o);
}

juce::var segmentToVar(const VocalSegment& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", static_cast<juce::int64>(s.id));
    o->setProperty("start_tick", static_cast<juce::int64>(s.startTick));
    o->setProperty("end_tick", static_cast<juce::int64>(s.endTick));
    o->setProperty("start_sec", s.getStartClipSeconds());
    o->setProperty("end_sec", s.getEndClipSeconds());
    o->setProperty("gain_linear", s.gainLinear);
    o->setProperty("tension01", s.tension01);
    return juce::var(o);
}

juce::var noteToVar(const Note& n)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", static_cast<juce::int64>(n.stableId));
    o->setProperty("pitch_hz", n.pitch);
    o->setProperty("start_sec", n.getStartClipSeconds());
    o->setProperty("duration_sec", n.getEndClipSeconds() - n.getStartClipSeconds());
    o->setProperty("lyric", juce::String());
    return juce::var(o);
}

} // namespace

juce::String buildRefineDurationsRequest(const SingingEditDocument& doc, uint64_t clipId, double clipDurationSec,
                                         uint64_t clipGeneration, uint64_t requestId, int sampleRate)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("schema_version", "opentune.ds.v1");
    root->setProperty("task", "refine_durations");
    root->setProperty("document_revision", static_cast<juce::int64>(doc.getDocumentRevision()));
    root->setProperty("request_id", static_cast<juce::int64>(requestId));

    auto* clipObj = new juce::DynamicObject();
    clipObj->setProperty("clip_id", static_cast<juce::int64>(clipId));
    clipObj->setProperty("clip_generation", static_cast<juce::int64>(clipGeneration));
    clipObj->setProperty("duration_sec", clipDurationSec);
    clipObj->setProperty("sample_rate", sampleRate);
    root->setProperty("clip", juce::var(clipObj));

    juce::Array<juce::var> segs;
    for (const auto& s : doc.getSegments().getSegments()) {
        segs.add(segmentToVar(s));
    }
    root->setProperty("segments", juce::var(segs));

    juce::Array<juce::var> notes;
    for (const auto& n : doc.getNotes()) {
        notes.add(noteToVar(n));
    }
    root->setProperty("notes", juce::var(notes));

    juce::Array<juce::var> phs;
    for (const auto& p : doc.getPhonemes()) {
        phs.add(phonemeToVar(p));
    }
    root->setProperty("phonemes", juce::var(phs));

    auto* mo = new juce::DynamicObject();
    juce::Array<juce::var> lb;
    for (int64_t tick : doc.getManualOverrides().lockedPhonemeBoundaryTicks) {
        lb.add(juce::var(static_cast<juce::int64>(tick)));
    }
    mo->setProperty("locked_phoneme_boundary_tick", juce::var(lb));
    juce::Array<juce::var> ln;
    for (uint64_t id : doc.getManualOverrides().lockedNoteIds) {
        ln.add(juce::var(static_cast<juce::int64>(id)));
    }
    mo->setProperty("locked_note_ids", juce::var(ln));
    // Phase 1 freeze: no curve overrides on bridge path.
    mo->setProperty("curve_overrides", juce::var(juce::Array<juce::var>()));
    root->setProperty("manual_overrides", juce::var(mo));

    return juce::JSON::toString(juce::var(root), false);
}

bool tryApplyRefineDurationsResponse(const juce::String& responseJson, SingingEditDocument& doc, uint64_t expectedClipId,
                                     uint64_t expectedClipGeneration, uint64_t expectedRequestId, juce::String& errorOut)
{
    auto parsed = juce::JSON::parse(responseJson);
    if (parsed.isVoid()) {
        errorOut = "Invalid JSON";
        return false;
    }
    const auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        errorOut = "Expected JSON object";
        return false;
    }
    const juce::String status = obj->getProperty("status").toString();
    if (status != "ok") {
        errorOut = "status=" + status + " " + obj->getProperty("error").toString();
        return false;
    }
    const uint64_t responseRequestId = static_cast<uint64_t>(static_cast<juce::int64>(obj->getProperty("request_id")));
    const uint64_t responseClipId = static_cast<uint64_t>(static_cast<juce::int64>(obj->getProperty("clip_id")));
    const uint64_t responseGeneration = static_cast<uint64_t>(static_cast<juce::int64>(obj->getProperty("clip_generation")));
    if (responseRequestId != expectedRequestId || responseClipId != expectedClipId
        || responseGeneration != expectedClipGeneration) {
        errorOut = "request gate mismatch";
        return false;
    }

    std::unordered_set<uint64_t> affectedAllow;
    juce::var affVar = obj->getProperty("affected_object_ids");
    if (affVar.isArray()) {
        for (const auto& av : *affVar.getArray()) {
            affectedAllow.insert(static_cast<uint64_t>(static_cast<juce::int64>(av)));
        }
    }
    const std::unordered_set<uint64_t>* restrictIds = nullptr;
    if (!affectedAllow.empty()) {
        restrictIds = &affectedAllow;
    }

    juce::var patchVar = obj->getProperty("patched_phonemes");
    if (!patchVar.isArray()) {
        errorOut = "missing patched_phonemes array";
        return false;
    }

    SingingEditPatch patch;
    for (const auto& v : *patchVar.getArray()) {
        const auto* p = v.getDynamicObject();
        if (p == nullptr) {
            continue;
        }
        SingingEditPatch::PhonemeUpdate upd;
        upd.id = static_cast<uint64_t>(static_cast<juce::int64>(p->getProperty("id")));
        if (p->hasProperty("start_tick") && p->hasProperty("end_tick")) {
            const int64_t st = static_cast<int64_t>(static_cast<juce::int64>(p->getProperty("start_tick")));
            const int64_t en = static_cast<int64_t>(static_cast<juce::int64>(p->getProperty("end_tick")));
            upd.startSec = TimeCoordinate::clipTickToSeconds(st);
            upd.endSec = TimeCoordinate::clipTickToSeconds(en);
        } else {
            upd.startSec = static_cast<double>(p->getProperty("start_sec"));
            upd.endSec = static_cast<double>(p->getProperty("end_sec"));
        }
        if (p->hasProperty("token")) {
            upd.token = p->getProperty("token").toString();
            upd.hasToken = true;
        }
        if (p->hasProperty("lock_left")) {
            upd.lockLeft = static_cast<bool>(static_cast<int>(p->getProperty("lock_left")) != 0);
            upd.hasLockLeft = true;
        }
        if (p->hasProperty("lock_right")) {
            upd.lockRight = static_cast<bool>(static_cast<int>(p->getProperty("lock_right")) != 0);
            upd.hasLockRight = true;
        }
        patch.phonemeUpdates.push_back(std::move(upd));
    }
    if (!applySingingEditPatch(doc, patch, errorOut, restrictIds)) {
        return false;
    }
    return true;
}

} // namespace DsJson
} // namespace OpenTune
