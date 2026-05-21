#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <string>

namespace OpenTune {

class VocoderInferenceService;
class VocoderRenderScheduler;

/**
 * VocoderLifecycle - Owns VocoderInferenceService + VocoderRenderScheduler init order (D2).
 */
class VocoderLifecycle {
public:
    VocoderLifecycle();
    ~VocoderLifecycle();

    bool initialize(const std::string& modelDir);
    void shutdown();

    bool isInitialized() const;
    bool isRunning() const;
    int getQueueDepth() const;
    int getVocoderHopSize() const;
    int getMelBins() const;

    VocoderInferenceService* getInferenceService() const;
    VocoderRenderScheduler* getScheduler() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderLifecycle)
};

} // namespace OpenTune
