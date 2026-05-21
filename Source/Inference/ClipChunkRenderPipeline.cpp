#include "ClipChunkRenderPipeline.h"
#include "VocoderChunkSynthesizer.h"
#include "../PluginProcessor.h"
#include "../DSP/MelSpectrogram.h"
#include "../Utils/AppLogger.h"
#include "../Utils/TimeCoordinate.h"
#include <algorithm>
#include <cmath>

namespace OpenTune {

namespace {

void fillF0GapsForVocoder(
    std::vector<float>& f0,
    const std::shared_ptr<const PitchCurveSnapshot>& snap,
    double frameStartTimeSec,
    double frameEndTimeSec,
    double hopDuration,
    double f0FrameRate)
{
    if (f0.empty() || !snap) return;

    constexpr int maxGapFrames = 50;  // ~580ms at 86fps
    const int n = static_cast<int>(f0.size());

    // ---- Step 1: Fill internal gaps with log-domain interpolation ----
    {
        int i = 0;
        while (i < n) {
            // Find next voiced frame
            while (i < n && f0[static_cast<size_t>(i)] <= 0.0f) ++i;
            if (i >= n) break;

            // Find voiced segment end
            int segEnd = i;
            while (segEnd < n && f0[static_cast<size_t>(segEnd)] > 0.0f) ++segEnd;

            // Find next voiced segment after gap
            int gapStart = segEnd;
            while (gapStart < n && f0[static_cast<size_t>(gapStart)] <= 0.0f) ++gapStart;

            if (gapStart >= n) break;  // No more voiced segments

            int gapLen = gapStart - segEnd;
            if (gapLen > 0 && gapLen <= maxGapFrames) {
                // Fill gap with log-domain interpolation
                const float fStart = f0[static_cast<size_t>(segEnd - 1)];
                const float fEnd = f0[static_cast<size_t>(gapStart)];
                const float logStart = std::log2(std::max(fStart, 1e-6f));
                const float logEnd = std::log2(std::max(fEnd, 1e-6f));
                for (int j = 0; j < gapLen; ++j) {
                    float t = static_cast<float>(j + 1) / static_cast<float>(gapLen + 1);
                    f0[static_cast<size_t>(segEnd + j)] = std::pow(2.0f, logStart + (logEnd - logStart) * t);
                }
            }

            i = gapStart;
        }
    }

    // ---- Step 2: Extend leading zeros (f0[0] == 0) ----
    if (n > 0 && f0[0] <= 0.0f) {
        // Find first voiced frame in current chunk
        int firstVoicedIdx = 0;
        while (firstVoicedIdx < n && f0[static_cast<size_t>(firstVoicedIdx)] <= 0.0f) ++firstVoicedIdx;

        if (firstVoicedIdx < n) {
            const float firstVoicedF0 = f0[static_cast<size_t>(firstVoicedIdx)];

            // Query PitchCurve for F0 before this chunk's start
            // We need to look backward from frameStartTimeSec
            const int lookbackF0Frames = 100;  // Look back up to 1 second (100 frames at 100fps)
            const int queryStartFrame = static_cast<int>(std::floor(frameStartTimeSec * f0FrameRate)) - lookbackF0Frames;
            const int queryEndFrame = static_cast<int>(std::floor(frameStartTimeSec * f0FrameRate));

            std::vector<float> prevF0(static_cast<size_t>(queryEndFrame - queryStartFrame), 0.0f);
            snap->renderF0Range(queryStartFrame, queryEndFrame,
                [&prevF0, queryStartFrame](int frameIndex, const float* data, int length) {
                    if (!data || length <= 0) return;
                    const int offset = frameIndex - queryStartFrame;
                    if (offset < 0) return;
                    const int copyLen = std::min(length, static_cast<int>(prevF0.size()) - offset);
                    if (copyLen > 0) {
                        std::copy(data, data + copyLen, prevF0.begin() + offset);
                    }
                });

            // Find the nearest voiced F0 going backward
            float extendF0 = 0.0f;
            for (int j = static_cast<int>(prevF0.size()) - 1; j >= 0; --j) {
                if (prevF0[static_cast<size_t>(j)] > 0.0f) {
                    extendF0 = prevF0[static_cast<size_t>(j)];
                    break;
                }
            }

            // If we found a voiced F0 before, fill the leading zeros
            // But check if there's a voiced segment between extend point and firstVoicedIdx
            if (extendF0 > 0.0f) {
                // Use the closer F0 value (extendF0 or firstVoicedF0) for smoother transition
                const float fillF0 = (extendF0 > 0.0f && firstVoicedF0 > 0.0f)
                    ? std::sqrt(extendF0 * firstVoicedF0)  // Geometric mean
                    : (firstVoicedF0 > 0.0f ? firstVoicedF0 : extendF0);

                // Fill leading zeros with gradual transition
                for (int j = 0; j < firstVoicedIdx; ++j) {
                    float t = static_cast<float>(j) / static_cast<float>(firstVoicedIdx + 1);
                    // Linear interpolation in log domain
                    float logFill = std::log2(std::max(fillF0, 1e-6f));
                    float logFirst = std::log2(std::max(firstVoicedF0, 1e-6f));
                    f0[static_cast<size_t>(j)] = std::pow(2.0f, logFill + (logFirst - logFill) * t);
                }
            }
        }
    }

    // ---- Step 3: Extend trailing zeros (f0[n-1] == 0) ----
    if (n > 0 && f0[static_cast<size_t>(n - 1)] <= 0.0f) {
        // Find last voiced frame in current chunk
        int lastVoicedIdx = n - 1;
        while (lastVoicedIdx >= 0 && f0[static_cast<size_t>(lastVoicedIdx)] <= 0.0f) --lastVoicedIdx;

        if (lastVoicedIdx >= 0) {
            const float lastVoicedF0 = f0[static_cast<size_t>(lastVoicedIdx)];

            // Query PitchCurve for F0 after this chunk's end
            const int lookaheadF0Frames = 100;  // Look ahead up to 1 second
            const int queryStartFrame = static_cast<int>(std::ceil(frameEndTimeSec * f0FrameRate));
            const int queryEndFrame = queryStartFrame + lookaheadF0Frames;

            std::vector<float> nextF0(static_cast<size_t>(queryEndFrame - queryStartFrame), 0.0f);
            snap->renderF0Range(queryStartFrame, queryEndFrame,
                [&nextF0, queryStartFrame](int frameIndex, const float* data, int length) {
                    if (!data || length <= 0) return;
                    const int offset = frameIndex - queryStartFrame;
                    if (offset < 0) return;
                    const int copyLen = std::min(length, static_cast<int>(nextF0.size()) - offset);
                    if (copyLen > 0) {
                        std::copy(data, data + copyLen, nextF0.begin() + offset);
                    }
                });

            // Find the nearest voiced F0 going forward
            float extendF0 = 0.0f;
            for (size_t j = 0; j < nextF0.size(); ++j) {
                if (nextF0[j] > 0.0f) {
                    extendF0 = nextF0[j];
                    break;
                }
            }

            // If we found a voiced F0 after, fill the trailing zeros
            if (extendF0 > 0.0f || lastVoicedF0 > 0.0f) {
                const float fillF0 = (extendF0 > 0.0f && lastVoicedF0 > 0.0f)
                    ? std::sqrt(extendF0 * lastVoicedF0)
                    : (lastVoicedF0 > 0.0f ? lastVoicedF0 : extendF0);

                // Fill trailing zeros with gradual transition
                const int trailingLen = n - lastVoicedIdx - 1;
                for (int j = 0; j < trailingLen; ++j) {
                    float t = static_cast<float>(j + 1) / static_cast<float>(trailingLen + 1);
                    float logLast = std::log2(std::max(lastVoicedF0, 1e-6f));
                    float logFill = std::log2(std::max(fillF0, 1e-6f));
                    f0[static_cast<size_t>(lastVoicedIdx + 1 + j)] = std::pow(2.0f, logLast + (logFill - logLast) * t);
                }
            }
        }
    }
}

} // namespace

bool ClipChunkRenderPipeline::runOneIteration(OpenTuneAudioProcessor& processor)
{
    constexpr double kHopDuration = 512.0 / RenderCache::kSampleRate;

    struct WorkerRenderJob {
        std::shared_ptr<RenderCache> cache;
        int trackId{0};
        uint64_t clipId{0};
        double startSeconds{0.0};
        double endSeconds{0.0};
        uint64_t targetRevision{0};
    };

    std::shared_ptr<WorkerRenderJob> job;
    {
        const juce::ScopedReadLock rl(processor.tracksLock_);
        for (int t = 0; t < OpenTuneAudioProcessor::MAX_TRACKS; ++t) {
            for (auto& clip : processor.tracks_[t].clips) {
                if (!clip.renderCache) {
                    continue;
                }

                RenderCache::PendingJob pendingJob;
                if (clip.renderCache->getNextPendingJob(pendingJob)) {
                    job = std::make_shared<WorkerRenderJob>();
                    job->trackId = t;
                    job->clipId = clip.clipId;
                    job->startSeconds = pendingJob.startSeconds;
                    job->endSeconds = pendingJob.endSeconds;
                    job->targetRevision = pendingJob.targetRevision;
                    job->cache = clip.renderCache;

                    AppLogger::log("RenderTrace: chunkRenderWorkerLoop pulled track=" + juce::String(t)
                        + " clipId=" + juce::String(static_cast<juce::int64>(clip.clipId))
                        + " start=" + juce::String(pendingJob.startSeconds, 3)
                        + " revision=" + juce::String(static_cast<juce::int64>(pendingJob.targetRevision)));
                    break;
                }
            }
            if (job) {
                break;
            }
        }
    }

    if (!job) {
        return false;
    }

    const double relChunkStartSec = job->startSeconds;
        const double relChunkEndSec = job->endSeconds;
        const double lengthSeconds = relChunkEndSec - relChunkStartSec;

        // 2. 准备渲染数据（读取 Clip 中的音频和 PitchCurve）
        std::shared_ptr<PitchCurve> pitchCurve;
        std::vector<float> monoAudio;
        int numFrames = 0;
        int64_t targetSamples = 0;
        bool clipFound = false;

        {
            const juce::ScopedReadLock rl(processor.tracksLock_);
            // 按 clipId 全轨道查找：任务入队时的 trackId 可能在等待/work 期间因横向/纵向拖动而过期
            for (int t = 0; t < OpenTuneAudioProcessor::MAX_TRACKS && !clipFound; ++t) {
                const auto& clips = processor.tracks_[(size_t)t].clips;
                for (const auto& clip : clips) {
                    if (clip.clipId != job->clipId) {
                        continue;
                    }

                    pitchCurve = clip.pitchCurve;

                    if (lengthSeconds <= 0.0) {
                        break;
                    }

                    const int audioNumSamples = clip.audioBuffer->getNumSamples();
                    const int audioNumChannels = clip.audioBuffer->getNumChannels();

                    const int64_t startSample = TimeCoordinate::secondsToSamples(relChunkStartSec, RenderCache::kSampleRate);
                    const int64_t endSample = TimeCoordinate::secondsToSamples(relChunkEndSec, RenderCache::kSampleRate);
                    const int64_t clampedStart = std::max<int64_t>(0, startSample);
                    const int64_t clampedEnd = std::min<int64_t>(audioNumSamples, endSample);
                    const int64_t audioLen = clampedEnd - clampedStart;

                    if (audioLen > 0 && audioNumChannels > 0) {
                        numFrames = static_cast<int>((audioLen + 512 - 1) / 512);
                        if (numFrames < 1) numFrames = 1;

                        monoAudio.resize(static_cast<size_t>(audioLen));
                        targetSamples = audioLen;
                        for (int64_t i = 0; i < audioLen; ++i) {
                            float sum = 0.0f;
                            for (int ch = 0; ch < audioNumChannels; ++ch) {
                                const float* chData = clip.audioBuffer->getReadPointer(ch);
                                sum += chData[static_cast<int>(clampedStart + i)];
                            }
                            monoAudio[static_cast<size_t>(i)] = sum / static_cast<float>(audioNumChannels);
                        }
                        clipFound = true;
                    }
                    break;
                }
            }
        }

        if (!clipFound || !pitchCurve || monoAudio.empty() || numFrames <= 0 || targetSamples <= 0) {
            AppLogger::log("RenderTrace: TerminalFailure: missing_audio_data"
                " clipFound=" + juce::String(clipFound ? 1 : 0)
                + " pitchCurve=" + juce::String(pitchCurve ? 1 : 0)
                + " monoAudio.empty=" + juce::String(monoAudio.empty() ? 1 : 0)
                + " numFrames=" + juce::String(numFrames)
                + " targetSamples=" + juce::String(static_cast<juce::int64>(targetSamples))
                + " start=" + juce::String(relChunkStartSec, 3)
                + " revision=" + juce::String(static_cast<juce::int64>(job->targetRevision)));

            job->cache->completeChunkRender(relChunkStartSec, job->targetRevision,
                RenderCache::CompletionResult::TerminalFailure);
            processor.schedulerCv_.notify_one();
            return true;
        }

        auto snap = pitchCurve->getSnapshot();
        if (!snap->hasRenderableCorrectedF0()) {
            AppLogger::log("RenderTrace: Blank: no_renderable_corrected_F0"
                " start=" + juce::String(relChunkStartSec, 3)
                + " revision=" + juce::String(static_cast<juce::int64>(job->targetRevision)));
            job->cache->markChunkAsBlank(relChunkStartSec);
            processor.schedulerCv_.notify_one();
            return true;
        }

        const int f0HopSize = snap->getHopSize();
        const double f0SampleRate = snap->getSampleRate();
        if (f0HopSize <= 0 || f0SampleRate <= 0.0) {
            AppLogger::log("RenderTrace: TerminalFailure: invalid_f0_timebase"
                " hop=" + juce::String(f0HopSize)
                + " sampleRate=" + juce::String(f0SampleRate, 3)
                + " start=" + juce::String(relChunkStartSec, 3)
                + " revision=" + juce::String(static_cast<juce::int64>(job->targetRevision)));
            job->cache->completeChunkRender(relChunkStartSec, job->targetRevision,
                RenderCache::CompletionResult::TerminalFailure);
            processor.schedulerCv_.notify_one();
            return true;
        }

        const double f0FrameRate = f0SampleRate / static_cast<double>(f0HopSize);

        // 3. 构造 F0 数据
        const int f0StartFrame = static_cast<int>(std::floor(relChunkStartSec * f0FrameRate));
        const int f0EndFrame = static_cast<int>(std::ceil(relChunkEndSec * f0FrameRate)) + 1;
        const int numF0Frames = std::max(1, f0EndFrame - f0StartFrame);

        std::vector<float> sourceF0(static_cast<size_t>(numF0Frames), 0.0f);
        snap->renderF0Range(f0StartFrame, f0EndFrame,
            [&sourceF0, f0StartFrame](int frameIndex, const float* data, int length) {
                if (!data || length <= 0) return;
                const int offset = frameIndex - f0StartFrame;
                if (offset < 0) return;
                const int copyLen = std::min(length, static_cast<int>(sourceF0.size()) - offset);
                if (copyLen > 0) {
                    std::copy(data, data + copyLen, sourceF0.begin() + offset);
                }
            });

        bool hasValidF0 = false;
        for (float f : sourceF0) {
            if (f > 0.0f) {
                hasValidF0 = true;
                break;
            }
        }

        if (!hasValidF0) {
            AppLogger::log("RenderTrace: Blank: no_valid_F0"
                " start=" + juce::String(relChunkStartSec, 3)
                + " revision=" + juce::String(static_cast<juce::int64>(job->targetRevision)));
            job->cache->markChunkAsBlank(relChunkStartSec);
            processor.schedulerCv_.notify_one();
            return true;
        }

        if (!processor.ensureVocoderReady() || !processor.vocoderLifecycle_) {
            AppLogger::log("RenderTrace: TerminalFailure: vocoder_not_ready"
                " start=" + juce::String(relChunkStartSec, 3)
                + " revision=" + juce::String(static_cast<juce::int64>(job->targetRevision)));
            job->cache->completeChunkRender(relChunkStartSec, job->targetRevision,
                RenderCache::CompletionResult::TerminalFailure);
            processor.schedulerCv_.notify_one();
            return true;
        }

        // 4. 构造 Mel Spectrogram
        MelSpectrogramConfig melConfig;
        melConfig.sampleRate = static_cast<int>(RenderCache::kSampleRate);
        melConfig.nMels = processor.vocoderLifecycle_->getMelBins();

        auto melResult = computeLogMelSpectrogram(monoAudio.data(), static_cast<int>(monoAudio.size()), numFrames, melConfig);
        if (!melResult.ok() || melResult.value().empty()) {
            AppLogger::log("RenderTrace: TerminalFailure: mel_computation_failed"
                " mel.ok=" + juce::String(melResult.ok() ? 1 : 0)
                + " mel.empty=" + juce::String(!melResult.ok() ? -1 : (melResult.value().empty() ? 1 : 0))
                + " start=" + juce::String(relChunkStartSec, 3)
                + " revision=" + juce::String(static_cast<juce::int64>(job->targetRevision)));
            job->cache->completeChunkRender(relChunkStartSec, job->targetRevision,
                RenderCache::CompletionResult::TerminalFailure);
            processor.schedulerCv_.notify_one();
            return true;
        }

        auto mel = std::move(melResult).value();
        const int actualFrames = static_cast<int>(mel.size() / melConfig.nMels);

        // 5. F0-to-Mel 插值
        std::vector<float> correctedF0(static_cast<size_t>(actualFrames), 0.0f);
        for (int i = 0; i < actualFrames; ++i) {
            const double melTimeSec = relChunkStartSec + i * kHopDuration;
            const double srcPos = melTimeSec * f0FrameRate - static_cast<double>(f0StartFrame);
            if (srcPos < 0.0) continue;

            const int srcIdx0 = static_cast<int>(srcPos);
            if (srcIdx0 >= numF0Frames) continue;
            const int srcIdx1 = std::min(srcIdx0 + 1, numF0Frames - 1);
            const double frac = srcPos - static_cast<double>(srcIdx0);

            const float f0_0 = sourceF0[static_cast<size_t>(srcIdx0)];
            const float f0_1 = sourceF0[static_cast<size_t>(srcIdx1)];

            if (f0_0 > 0.0f && f0_1 > 0.0f) {
                correctedF0[static_cast<size_t>(i)] = static_cast<float>(std::exp(std::log(f0_0) * (1.0 - frac) + std::log(f0_1) * frac));
            } else if (f0_0 > 0.0f) {
                correctedF0[static_cast<size_t>(i)] = f0_0;
            } else if (f0_1 > 0.0f) {
                correctedF0[static_cast<size_t>(i)] = f0_1;
            }
        }

        fillF0GapsForVocoder(correctedF0, snap, relChunkStartSec, relChunkEndSec, kHopDuration, f0FrameRate);

    if (!processor.vocoderLifecycle_) {
        return true;
    }

    VocoderChunkSynthesizer synthesizer(*processor.vocoderLifecycle_);
    VocoderChunkSynthesizer::Request synthReq;
    synthReq.f0 = std::move(correctedF0);
    synthReq.energy.resize(synthReq.f0.size(), 1.0f);
    synthReq.mel = std::move(mel);
    synthReq.renderCache = job->cache;
    synthReq.chunkStartSeconds = job->startSeconds;
    synthReq.targetRevision = job->targetRevision;
    synthReq.deviceSampleRate = static_cast<int>(std::lround(
        processor.currentSampleRate_.load(std::memory_order_relaxed)));
    synthReq.resamplingManager = processor.resamplingManager_.get();
    synthReq.onFinished = [&processor]() { processor.schedulerCv_.notify_one(); };
    synthesizer.submitChunkSynthesis(std::move(synthReq));
    AppLogger::log("RenderTrace: chunkRenderWorkerLoop submitted start=" + juce::String(relChunkStartSec, 3));
    return true;
}

} // namespace OpenTune
