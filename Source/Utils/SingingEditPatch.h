#pragma once

#include "SingingEditDocument.h"
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace OpenTune {

struct SingingEditPatch {
    struct PhonemeUpdate {
        uint64_t id{0};
        double startSec{0.0};
        double endSec{0.0};
        juce::String token;
        bool hasToken{false};
        bool lockLeft{false};
        bool hasLockLeft{false};
        bool lockRight{false};
        bool hasLockRight{false};
    };

    std::vector<PhonemeUpdate> phonemeUpdates;
    std::vector<juce::String> conflicts;
    juce::String provenance;
};

std::vector<uint64_t> getAffectedPhonemeIds(const SingingEditPatch& patch);
std::vector<PhonemeItem> capturePhonemeSubset(const SingingEditDocument& doc, const std::vector<uint64_t>& ids);
/**
 * Apply phoneme updates. If restrictToPhonemeIds is non-null, every update id must appear in the set
 * (strict incremental apply).
 */
bool applySingingEditPatch(SingingEditDocument& doc, const SingingEditPatch& patch, juce::String& errorOut,
                           const std::unordered_set<uint64_t>* restrictToPhonemeIds = nullptr);

} // namespace OpenTune
