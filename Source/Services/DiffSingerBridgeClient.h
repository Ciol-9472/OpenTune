#pragma once



#include <juce_core/juce_core.h>

#include <memory>

#include <atomic>



namespace OpenTune {



class SingingEditDocument;



/** Local Python refine service: framed stdio on Windows; other platforms return errors (no alternate CLI payload). */

class DiffSingerBridgeClient {

public:

    DiffSingerBridgeClient();

    ~DiffSingerBridgeClient();



    bool ensureServiceRunning(juce::String& errorOut);

    void shutdownService();



    bool requestRefineDurations(SingingEditDocument& doc, uint64_t clipId, double clipDurationSec, uint64_t clipGeneration,

                                uint64_t requestId, int sampleRate, juce::String& errorOut);



    juce::String getLastVersionString() const { return lastVersionString_; }



private:

#if JUCE_WINDOWS

    bool readFramedJsonResponse(juce::String& bodyOut, juce::String& errorOut);

    bool writeFramedJsonRequest(const juce::String& utf8Payload, juce::String& errorOut);

#endif

    void closeWindowsService();

    void markUnavailable(const juce::String& reason);



#if JUCE_WINDOWS

    void* winStdinWrite_{nullptr};

    void* winStdoutRead_{nullptr};

    void* winChildProcess_{nullptr};

    void* winStderrNull_{nullptr};

#endif



    juce::String lastVersionString_{"opentune_ds_framed_stdio_v1"};

    std::atomic<bool> running_{false};

    int restartCount_{0};

    int64_t cooldownUntilMs_{0};

    bool debugSidecar_{false};

};



} // namespace OpenTune

