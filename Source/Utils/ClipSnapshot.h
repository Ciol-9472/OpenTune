#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "../DSP/ChromaKeyDetector.h"
#include "SilentGapDetector.h"
#include "SingingEditDocument.h"

namespace OpenTune {

class PitchCurve;
class RenderCache;

enum class OriginalF0State : uint8_t {
    NotRequested = 0,
    Extracting,
    Ready,
    Failed
};

struct ClipSnapshot {
    std::shared_ptr<const juce::AudioBuffer<float>> audioBuffer;
    double startSeconds{0.0};
    float gain{1.0f};
    double fadeInDuration{0.0};
    double fadeOutDuration{0.0};
    juce::String name;
    juce::Colour colour;
    std::shared_ptr<PitchCurve> pitchCurve;
    OriginalF0State originalF0State{OriginalF0State::NotRequested};
    DetectedKey detectedKey;
    std::shared_ptr<RenderCache> renderCache;
    std::vector<SilentGap> silentGaps;
    juce::String sourceAudioAbsolutePath;
    /** Deep copy of clip-local singing edit (undo / clipboard / restore). */
    std::shared_ptr<const SingingEditDocument> singingEdit;
    /** Captured authoritative clipGeneration at snapshot time (restore uses monotonic rule vs this). */
    uint64_t clipGeneration{0};
};

} // namespace OpenTune
