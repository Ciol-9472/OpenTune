#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>

namespace OpenTune {

class OpenTuneAudioProcessor;

/**
 * ClipRenderInvalidation (A2) — maps clipId + F0 frame edits to partial chunk render requests.
 */
class ClipRenderInvalidation {
public:
    explicit ClipRenderInvalidation(OpenTuneAudioProcessor& processor);

    void invalidateByClipId(uint64_t clipId, int startFrame, int endFrame);

private:
    OpenTuneAudioProcessor& processor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipRenderInvalidation)
};

} // namespace OpenTune
