#include "ClipRenderInvalidation.h"

#include "../PluginProcessor.h"
#include "../Utils/AppLogger.h"

namespace OpenTune {

ClipRenderInvalidation::ClipRenderInvalidation(OpenTuneAudioProcessor& processor)
    : processor_(processor) {}

void ClipRenderInvalidation::invalidateByClipId(uint64_t clipId, int startFrame, int endFrame)
{
    if (clipId == 0) {
        return;
    }

    int trackId = -1;
    int clipIndex = -1;
    if (!processor_.findClipById(clipId, trackId, clipIndex)) {
        AppLogger::log("RenderTrace: invalidateClipRender clip not found clipId="
            + juce::String(static_cast<juce::int64>(clipId)));
        return;
    }

    auto curve = processor_.getClipPitchCurve(trackId, clipIndex);
    if (!curve) {
        return;
    }

    int hopSize = curve->getHopSize();
    double f0SampleRate = curve->getSampleRate();
    if (hopSize <= 0 || f0SampleRate <= 0.0) {
        if (auto* f0Service = processor_.getF0Service()) {
            hopSize = f0Service->getF0HopSize();
            f0SampleRate = static_cast<double>(f0Service->getF0SampleRate());
        }
    }

    const int numFrames = static_cast<int>(curve->size());
    if (numFrames <= 0 || hopSize <= 0 || f0SampleRate <= 0.0) {
        return;
    }

    int sf = startFrame;
    int ef = endFrame;
    if (sf > ef) {
        std::swap(sf, ef);
    }
    sf = std::max(0, sf);
    ef = std::min(ef, numFrames - 1);
    if (ef < sf) {
        return;
    }

    const double secondsPerFrame = static_cast<double>(hopSize) / f0SampleRate;
    const double editStartSec = static_cast<double>(sf) * secondsPerFrame;
    const double editEndSec = static_cast<double>(ef + 1) * secondsPerFrame;
    const uint64_t targetRevision = curve->getSnapshot()->getRenderGeneration();

    AppLogger::log("RenderTrace: ClipRenderInvalidation track=" + juce::String(trackId)
        + " clip=" + juce::String(clipIndex)
        + " clipId=" + juce::String(static_cast<juce::int64>(clipId))
        + " frameRange=[" + juce::String(sf) + "," + juce::String(ef) + "]"
        + " secRange=[" + juce::String(editStartSec, 3) + "," + juce::String(editEndSec, 3) + "]"
        + " renderGeneration=" + juce::String(static_cast<juce::int64>(targetRevision)));

    processor_.enqueuePartialRender(trackId, clipIndex, editStartSec, editEndSec, targetRevision);
}

} // namespace OpenTune
