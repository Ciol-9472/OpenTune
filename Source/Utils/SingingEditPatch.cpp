#include "SingingEditPatch.h"

#include <algorithm>
#include <unordered_map>

namespace OpenTune {

std::vector<uint64_t> getAffectedPhonemeIds(const SingingEditPatch& patch)
{
    std::vector<uint64_t> ids;
    ids.reserve(patch.phonemeUpdates.size());
    for (const auto& u : patch.phonemeUpdates) {
        ids.push_back(u.id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::vector<PhonemeItem> capturePhonemeSubset(const SingingEditDocument& doc, const std::vector<uint64_t>& ids)
{
    std::vector<PhonemeItem> out;
    if (ids.empty()) {
        return out;
    }
    std::unordered_map<uint64_t, bool> idMap;
    idMap.reserve(ids.size());
    for (uint64_t id : ids) {
        idMap[id] = true;
    }
    for (const auto& p : doc.getPhonemes()) {
        if (idMap.find(p.id) != idMap.end()) {
            out.push_back(p);
        }
    }
    return out;
}

bool applySingingEditPatch(SingingEditDocument& doc, const SingingEditPatch& patch, juce::String& errorOut,
                           const std::unordered_set<uint64_t>* restrictToPhonemeIds)
{
    auto& phonemes = doc.getPhonemes();
    std::unordered_map<uint64_t, size_t> idToIndex;
    idToIndex.reserve(phonemes.size());
    for (size_t i = 0; i < phonemes.size(); ++i) {
        idToIndex[phonemes[i].id] = i;
    }

    if (restrictToPhonemeIds != nullptr) {
        for (const auto& upd : patch.phonemeUpdates) {
            if (restrictToPhonemeIds->find(upd.id) == restrictToPhonemeIds->end()) {
                errorOut = "Patch contains phoneme id outside allowed affected set";
                return false;
            }
        }
    }

    for (const auto& upd : patch.phonemeUpdates) {
        auto it = idToIndex.find(upd.id);
        if (it == idToIndex.end()) {
            errorOut = "Patch references missing phoneme id";
            return false;
        }
        PhonemeItem& p = phonemes[it->second];
        p.setStartClipSeconds(upd.startSec);
        p.setEndClipSeconds(upd.endSec);
        if (upd.hasToken) {
            p.token = upd.token;
        }
        if (upd.hasLockLeft) {
            p.lockLeft = upd.lockLeft;
        }
        if (upd.hasLockRight) {
            p.lockRight = upd.lockRight;
        }
    }
    auto validation = doc.validateAndNormalize(0.0);
    if (!validation.ok) {
        errorOut = "Patch validation failed: " + validation.errorCode;
        return false;
    }
    return true;
}

} // namespace OpenTune
