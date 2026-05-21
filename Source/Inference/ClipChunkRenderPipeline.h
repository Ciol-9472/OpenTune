#pragma once

namespace OpenTune {

class OpenTuneAudioProcessor;

/**
 * ClipChunkRenderPipeline - Mel/F0 prep and vocoder handoff for one pending chunk (PR2, C-借用).
 */
class ClipChunkRenderPipeline {
public:
    /** Pull and process one pending chunk job; returns false if no job was available. */
    static bool runOneIteration(OpenTuneAudioProcessor& processor);
};

} // namespace OpenTune
