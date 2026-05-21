#include "VocoderChunkSynthesizer.h"

#include "RenderCache.h"
#include "VocoderLifecycle.h"
#include "VocoderRenderScheduler.h"
#include "../DSP/ResamplingManager.h"
#include "../Utils/AppLogger.h"

namespace OpenTune {

VocoderChunkSynthesizer::VocoderChunkSynthesizer(VocoderLifecycle& lifecycle)
    : lifecycle_(lifecycle) {}

void VocoderChunkSynthesizer::submitChunkSynthesis(Request request) {
    auto* scheduler = lifecycle_.getScheduler();
    if (scheduler == nullptr || !request.renderCache) {
        if (request.onFinished) {
            request.onFinished();
        }
        return;
    }

    const int renderSampleRate = static_cast<int>(RenderCache::kSampleRate);
    auto renderCache = request.renderCache;
    const auto targetRevision = request.targetRevision;
    const double jobStartSeconds = request.chunkStartSeconds;
    auto resamplingManager = request.resamplingManager;
    const int deviceSampleRate = request.deviceSampleRate;
    auto onFinished = std::move(request.onFinished);

    VocoderRenderScheduler::Job schedulerJob;
    schedulerJob.f0 = std::move(request.f0);
    schedulerJob.energy = std::move(request.energy);
    schedulerJob.mel = std::move(request.mel);
    schedulerJob.onComplete = [renderCache, targetRevision, jobStartSeconds, renderSampleRate,
                             deviceSampleRate, resamplingManager,
                             onFinished = std::move(onFinished)](bool success, const juce::String& error,
                                                                const std::vector<float>& audio) {
        if (success && !audio.empty()) {
            const double chunkEndSeconds =
                jobStartSeconds + static_cast<double>(audio.size()) / RenderCache::kSampleRate;
            std::vector<float> renderedAudio(audio.begin(), audio.end());
            const bool baseChunkStored = renderCache->addChunk(
                jobStartSeconds, chunkEndSeconds, std::move(renderedAudio), targetRevision);

            if (baseChunkStored && resamplingManager != nullptr && deviceSampleRate > 0
                && deviceSampleRate != renderSampleRate) {
                auto resampledAudio = resamplingManager->upsampleForHost(
                    audio.data(), audio.size(), renderSampleRate, deviceSampleRate);
                if (!resampledAudio.empty()) {
                    const bool resampledStored = renderCache->addResampledChunk(
                        jobStartSeconds, chunkEndSeconds, deviceSampleRate, std::move(resampledAudio), targetRevision);
                    if (!resampledStored) {
                        AppLogger::log("RenderTrace: addResampledChunk skipped start="
                            + juce::String(jobStartSeconds, 3)
                            + " revision=" + juce::String(static_cast<juce::int64>(targetRevision))
                            + " sampleRate=" + juce::String(deviceSampleRate));
                    }
                }
            }

            renderCache->completeChunkRender(jobStartSeconds, targetRevision, RenderCache::CompletionResult::Succeeded);
            AppLogger::log("RenderTrace: chunk synthesis complete start="
                + juce::String(jobStartSeconds, 3)
                + " revision=" + juce::String(static_cast<juce::int64>(targetRevision)));
        } else {
            renderCache->completeChunkRender(jobStartSeconds, targetRevision,
                RenderCache::CompletionResult::TerminalFailure);
            AppLogger::log("RenderTrace: chunk synthesis failed start="
                + juce::String(jobStartSeconds, 3)
                + " error=" + error);
        }

        if (onFinished) {
            onFinished();
        }
    };

    scheduler->submit(std::move(schedulerJob));
}

} // namespace OpenTune
