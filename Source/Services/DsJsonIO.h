#pragma once

#include <juce_core/juce_core.h>
#include <memory>

namespace OpenTune {

class SingingEditDocument;

namespace DsJson {

/** Build POST /v1/refine_durations JSON body (opentune.ds.v1). */
juce::String buildRefineDurationsRequest(const SingingEditDocument& doc, uint64_t clipId, double clipDurationSec,
                                         uint64_t clipGeneration, uint64_t requestId, int sampleRate);

/**
 * Apply refine response if status ok and clip_id / clip_generation / request_id match.
 * When response includes non-empty affected_object_ids, phoneme updates are restricted to that set.
 */
bool tryApplyRefineDurationsResponse(const juce::String& responseJson, SingingEditDocument& doc, uint64_t expectedClipId,
                                     uint64_t expectedClipGeneration, uint64_t expectedRequestId, juce::String& errorOut);

} // namespace DsJson

} // namespace OpenTune
