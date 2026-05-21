#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

namespace OpenTune {

class RenderCache;
class ResamplingManager;
class VocoderLifecycle;

/**
 * VocoderChunkSynthesizer - Chunk ONNX synthesis + RenderCache publish (D1).
 */
class VocoderChunkSynthesizer {
public:
    struct Request {
        std::vector<float> f0;
        std::vector<float> energy;
        std::vector<float> mel;
        std::shared_ptr<RenderCache> renderCache;
        double chunkStartSeconds{0.0};
        uint64_t targetRevision{0};
        int deviceSampleRate{0};
        ResamplingManager* resamplingManager{nullptr};
        std::function<void()> onFinished;
    };

    explicit VocoderChunkSynthesizer(VocoderLifecycle& lifecycle);

    void submitChunkSynthesis(Request request);

private:
    VocoderLifecycle& lifecycle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderChunkSynthesizer)
};

} // namespace OpenTune
