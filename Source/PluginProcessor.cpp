#include "PluginProcessor.h"
#include "Editor/EditorFactory.h"
#include "Host/HostIntegration.h"
#include "Inference/ClipChunkRenderPipeline.h"
#include "DSP/ResamplingManager.h"
#include "DSP/MelSpectrogram.h"
#include "Utils/ModelPathResolver.h"
#include "Utils/AppLogger.h"
#include "Utils/AccelerationDetector.h"
#include "Utils/TimeCoordinate.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

namespace OpenTune {

namespace {

static std::vector<double> buildChunkBoundariesFromSilentGaps(int64_t clipNumSamples,
                                                               const std::vector<SilentGap>& gaps)
{
    std::vector<double> boundaries;
    if (clipNumSamples <= 0) {
        return boundaries;
    }

    const double clipDurationSec = TimeCoordinate::samplesToSeconds(clipNumSamples, SilentGapDetector::kInternalSampleRate);
    boundaries.reserve(gaps.size() + 2);
    boundaries.push_back(0.0);
    for (const auto& gap : gaps) {
        boundaries.push_back((gap.startSeconds + gap.endSeconds) * 0.5);
    }
    boundaries.push_back(clipDurationSec);

    std::sort(boundaries.begin(), boundaries.end());
    auto last = std::unique(boundaries.begin(), boundaries.end());
    boundaries.erase(last, boundaries.end());
    return boundaries;
}

} // anonymous namespace

void OpenTuneAudioProcessor::copyClipToSnapshot(const TrackState::AudioClip& clip, OpenTune::ClipSnapshot& out)
{
    out.audioBuffer = clip.audioBuffer;
    out.startSeconds = clip.startSeconds;
    out.gain = clip.gain;
    out.fadeInDuration = clip.fadeInDuration;
    out.fadeOutDuration = clip.fadeOutDuration;
    out.name = clip.name;
    out.colour = clip.colour;
    out.pitchCurve = clip.pitchCurve;
    out.originalF0State = clip.originalF0State;
    out.detectedKey = clip.detectedKey;
    out.renderCache = clip.renderCache;
    out.silentGaps = clip.silentGaps;
    out.sourceAudioAbsolutePath = clip.sourceAudioAbsolutePath;
}

void OpenTuneAudioProcessor::copySnapshotToClip(const OpenTune::ClipSnapshot& snap, TrackState::AudioClip& clip, uint64_t clipId)
{
    clip.clipId = clipId;
    clip.audioBuffer = snap.audioBuffer;
    clip.startSeconds = snap.startSeconds;
    clip.gain = snap.gain;
    clip.fadeInDuration = snap.fadeInDuration;
    clip.fadeOutDuration = snap.fadeOutDuration;
    clip.name = snap.name;
    clip.colour = snap.colour;
    clip.pitchCurve = snap.pitchCurve;
    clip.originalF0State = snap.originalF0State;
    clip.detectedKey = snap.detectedKey;
    clip.renderCache = snap.renderCache;
    clip.silentGaps = snap.silentGaps;
    clip.sourceAudioAbsolutePath = snap.sourceAudioAbsolutePath;
}

OpenTuneAudioProcessor::TrackState::AudioClip::AudioClip(
    const OpenTuneAudioProcessor::TrackState::AudioClip& other)
    : clipId(other.clipId)
    , audioBuffer(other.audioBuffer)
    , drySignalBuffer_(other.drySignalBuffer_)
    , startSeconds(other.startSeconds)
    , gain(other.gain)
    , fadeInDuration(other.fadeInDuration)
    , fadeOutDuration(other.fadeOutDuration)
    , name(other.name)
    , colour(other.colour)
    , pitchCurve(other.pitchCurve)
    , originalF0State(other.originalF0State)
    , detectedKey(other.detectedKey)
    , renderCache(other.renderCache)
    , notes(other.notes)
    , silentGaps(other.silentGaps)
    , sourceAudioAbsolutePath(other.sourceAudioAbsolutePath)
{
}

OpenTuneAudioProcessor::TrackState::AudioClip& OpenTuneAudioProcessor::TrackState::AudioClip::operator=(
    const OpenTuneAudioProcessor::TrackState::AudioClip& other)
{
    if (this == &other) {
        return *this;
    }

    clipId = other.clipId;
    audioBuffer = other.audioBuffer;
    drySignalBuffer_ = other.drySignalBuffer_;
    startSeconds = other.startSeconds;
    gain = other.gain;
    fadeInDuration = other.fadeInDuration;
    fadeOutDuration = other.fadeOutDuration;
    name = other.name;
    colour = other.colour;
    pitchCurve = other.pitchCurve;
    originalF0State = other.originalF0State;
    detectedKey = other.detectedKey;
    renderCache = other.renderCache;
    notes = other.notes;
    silentGaps = other.silentGaps;
    sourceAudioAbsolutePath = other.sourceAudioAbsolutePath;
    return *this;
}

OpenTuneAudioProcessor::TrackState::AudioClip::AudioClip(
    OpenTuneAudioProcessor::TrackState::AudioClip&& other) noexcept
    : clipId(other.clipId)
    , audioBuffer(std::move(other.audioBuffer))
    , drySignalBuffer_(std::move(other.drySignalBuffer_))
    , startSeconds(other.startSeconds)
    , gain(other.gain)
    , fadeInDuration(other.fadeInDuration)
    , fadeOutDuration(other.fadeOutDuration)
    , name(std::move(other.name))
    , colour(other.colour)
    , pitchCurve(std::move(other.pitchCurve))
    , originalF0State(other.originalF0State)
    , detectedKey(std::move(other.detectedKey))
    , renderCache(std::move(other.renderCache))
    , notes(std::move(other.notes))
    , silentGaps(std::move(other.silentGaps))
    , sourceAudioAbsolutePath(std::move(other.sourceAudioAbsolutePath))
{
}

OpenTuneAudioProcessor::TrackState::AudioClip& OpenTuneAudioProcessor::TrackState::AudioClip::operator=(
    OpenTuneAudioProcessor::TrackState::AudioClip&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    clipId = other.clipId;
    audioBuffer = std::move(other.audioBuffer);
    drySignalBuffer_ = std::move(other.drySignalBuffer_);
    startSeconds = other.startSeconds;
    gain = other.gain;
    fadeInDuration = other.fadeInDuration;
    fadeOutDuration = other.fadeOutDuration;
    name = std::move(other.name);
    colour = other.colour;
    pitchCurve = std::move(other.pitchCurve);
    originalF0State = other.originalF0State;
    detectedKey = other.detectedKey;
    renderCache = std::move(other.renderCache);
    notes = std::move(other.notes);
    silentGaps = std::move(other.silentGaps);
    sourceAudioAbsolutePath = std::move(other.sourceAudioAbsolutePath);
    return *this;
}

OpenTuneAudioProcessor::OpenTuneAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    AppLogger::initialize();
    AppLogger::log("OpenTuneAudioProcessor: ctor");
    AccelerationDetector::getInstance().detect();

    editVersionParam_ = new juce::AudioParameterInt("editVersion", "EditVersion", 0, 100000, 0);
    addParameter(editVersionParam_);

    // Initialize tracks
    for (int i = 0; i < MAX_TRACKS; ++i) {
        tracks_[i].name = "Track " + juce::String(i + 1);
        tracks_[i].colour = juce::Colour::fromHSV(i * 0.3f, 0.6f, 0.8f, 1.0f);
    }

    hostIntegration_ = createHostIntegration();
    if (hostIntegration_) {
        hostIntegration_->configureInitialState(*this);
    }

    resamplingManager_ = std::make_unique<ResamplingManager>();
    f0Service_ = std::make_unique<F0InferenceService>();
    vocoderLifecycle_ = std::make_unique<VocoderLifecycle>();
    clipRenderInvalidation_ = std::make_unique<ClipRenderInvalidation>(*this);
    resetPerfProbeCounters();

    chunkRenderWorkerRunning_ = true;
    chunkRenderWorkerThread_ = std::thread([this]() { chunkRenderWorkerLoop(); });
}

OpenTuneAudioProcessor::~OpenTuneAudioProcessor() {
    isPlaying_.store(false);

    {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        chunkRenderWorkerRunning_ = false;
    }
    schedulerCv_.notify_all();
    if (chunkRenderWorkerThread_.joinable()) {
        chunkRenderWorkerThread_.join();
    }

    if (vocoderLifecycle_) {
        vocoderLifecycle_->shutdown();
    }
    if (f0Service_) {
        f0Service_->shutdown();
    }

    AppLogger::shutdown();
}

bool OpenTuneAudioProcessor::ensureF0Ready()
{
    if (f0Ready_.load()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(f0InitMutex_);

    if (f0Ready_.load()) {
        return true;
    }

    if (f0InitAttempted_.load()) {
        return f0Ready_.load();
    }

    f0InitAttempted_.store(true);

    const auto modelsDir = ModelPathResolver::getModelsDirectory();
    AppLogger::log("F0 models dir: " + juce::String(modelsDir));

    if (!ModelPathResolver::ensureOnnxRuntimeLoaded()) {
        AppLogger::log("ensureOnnxRuntimeLoaded failed");
        f0Ready_.store(false);
        return false;
    }

    bool ok = false;
    try {
        ok = f0Service_->initialize(modelsDir);
        if (!ok) {
            AppLogger::log("F0InferenceService initialize failed");
            f0Ready_.store(false);
            return false;
        }

        AppLogger::log("F0 inference service initialized successfully");
    } catch (const std::exception& e) {
        AppLogger::log("F0 initialize exception: " + juce::String(e.what()));
        ok = false;
    } catch (...) {
        AppLogger::log("F0 initialize unknown exception");
        ok = false;
    }

    f0Ready_.store(ok);
    return ok;
}

bool OpenTuneAudioProcessor::ensureVocoderReady()
{
    if (vocoderReady_.load()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(vocoderInitMutex_);

    if (vocoderReady_.load()) {
        return true;
    }

    if (vocoderInitAttempted_.load()) {
        return vocoderReady_.load();
    }

    vocoderInitAttempted_.store(true);

    const auto modelsDir = ModelPathResolver::getModelsDirectory();
    AppLogger::log("Vocoder models dir: " + juce::String(modelsDir));

    if (!ModelPathResolver::ensureOnnxRuntimeLoaded()) {
        AppLogger::log("ensureOnnxRuntimeLoaded failed");
        vocoderReady_.store(false);
        return false;
    }

    bool ok = false;
    try {
        ok = vocoderLifecycle_->initialize(modelsDir);
        if (!ok) {
            AppLogger::log("VocoderLifecycle initialize failed");
            vocoderReady_.store(false);
            return false;
        }

        AppLogger::log("Vocoder inference service initialized successfully");
    } catch (const std::exception& e) {
        AppLogger::log("Vocoder initialize exception: " + juce::String(e.what()));
        ok = false;
    } catch (...) {
        AppLogger::log("Vocoder initialize unknown exception");
        ok = false;
    }

    vocoderReady_.store(ok);
    return ok;
}

bool OpenTuneAudioProcessor::initializeInferenceIfNeeded()
{
    return ensureF0Ready();
}

const juce::String OpenTuneAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool OpenTuneAudioProcessor::acceptsMidi() const {
    return false;
}

bool OpenTuneAudioProcessor::producesMidi() const {
    return false;
}

bool OpenTuneAudioProcessor::isMidiEffect() const {
    return false;
}

double OpenTuneAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int OpenTuneAudioProcessor::getNumPrograms() {
    return 1;
}

int OpenTuneAudioProcessor::getCurrentProgram() {
    return 0;
}

void OpenTuneAudioProcessor::setCurrentProgram(int index) {
    juce::ignoreUnused(index);
}

const juce::String OpenTuneAudioProcessor::getProgramName(int index) {
    if (index == 0) {
        return "Default";
    }
    return {};
}

void OpenTuneAudioProcessor::changeProgramName(int index, const juce::String& newName) {
    juce::ignoreUnused(index, newName);
}

void OpenTuneAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const bool wasInitialized = (currentSampleRate_ > 0.0);
    const double oldSampleRate = currentSampleRate_;
    const bool sampleRateChanged = wasInitialized && (std::abs(oldSampleRate - sampleRate) > 1.0);
    
    AppLogger::log("prepareToPlay: sampleRate=" + juce::String(sampleRate, 2) +
                   " blockSize=" + juce::String(samplesPerBlock) +
                   " oldRate=" + juce::String(oldSampleRate, 0) +
                   " changed=" + (sampleRateChanged ? "true" : "false"));

    currentSampleRate_ = sampleRate;
    currentBlockSize_ = samplesPerBlock;
    
    // Calculate fade-out duration: 200ms = 0.2 seconds
    fadeOutTotalSamples_ = static_cast<int>(sampleRate * 0.2);
    
    if (sampleRateChanged && oldSampleRate > 0.0) {
        AppLogger::log("Sample rate changed: " + juce::String(oldSampleRate, 0) + 
                       " -> " + juce::String(sampleRate, 0) + 
                       ", resampling drySignalBuffer, clearing resampled cache");
        
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        for (auto& track : tracks_) {
            for (auto& clip : track.clips) {
                // Pre-resample dry signal to new device rate
                // audioBuffer stays at 44100Hz (fixed, never resampled on device rate change)
                resampleDrySignal(clip, sampleRate);
                
                // Clear only the resampled cache (not the 44.1kHz audio)
                // New renders will be resampled to the new sample rate
                if (clip.renderCache) {
                    clip.renderCache->clearResampledCache();
                }
                
                // silentGaps are in seconds, unchanged
            }
        }
    }

    doublePrecisionScratch_.setSize(std::max(1, getTotalNumOutputChannels()), std::max(1, currentBlockSize_), false, true, true);

#if JucePlugin_Enable_ARA
    prepareToPlayForARA(sampleRate,
                        samplesPerBlock,
                        getMainBusNumOutputChannels(),
                        getProcessingPrecision());
#endif
}

void OpenTuneAudioProcessor::releaseResources() {
    isPlaying_.store(false);

#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif
}

void OpenTuneAudioProcessor::setPitchPreview(bool active, float frequencyHz)
{
    if (active && frequencyHz > 0.0f && std::isfinite(frequencyHz)) {
        pitchPreviewTargetHz_.store(juce::jlimit(20.0f, 20000.0f, frequencyHz), std::memory_order_relaxed);
        pitchPreviewRequested_.store(true, std::memory_order_relaxed);
    } else {
        pitchPreviewRequested_.store(false, std::memory_order_relaxed);
    }
}

void OpenTuneAudioProcessor::mixPitchPreviewIntoBuffer(juce::AudioBuffer<float>& buffer,
                                                       double sampleRate,
                                                       int numSamples,
                                                       int numChannels)
{
    if (numSamples <= 0 || numChannels <= 0 || sampleRate <= 0.0) {
        return;
    }

    const bool wantOn = pitchPreviewRequested_.load(std::memory_order_relaxed);
    const float targetHzAtomic = pitchPreviewTargetHz_.load(std::memory_order_relaxed);

    constexpr float kMaxGain = 0.12f;
    const float targetGain = (wantOn && targetHzAtomic > 0.0f) ? kMaxGain : 0.0f;

    const double twoPi = juce::MathConstants<double>::twoPi;
    const float omega = static_cast<float>(twoPi / sampleRate);

    for (int i = 0; i < numSamples; ++i) {
        pitchPreviewSmoothedGain_ += (targetGain - pitchPreviewSmoothedGain_) * 0.025f;

        if (wantOn && targetHzAtomic > 0.0f) {
            pitchPreviewSmoothedHz_ += (targetHzAtomic - pitchPreviewSmoothedHz_) * 0.12f;
        }

        if (pitchPreviewSmoothedGain_ <= 1.0e-6f) {
            continue;
        }

        const float hz = juce::jmax(20.0f, pitchPreviewSmoothedHz_);
        pitchPreviewPhase_ += static_cast<double>(omega) * static_cast<double>(hz);
        while (pitchPreviewPhase_ >= twoPi) {
            pitchPreviewPhase_ -= twoPi;
        }

        const float s = pitchPreviewSmoothedGain_ * static_cast<float>(std::sin(pitchPreviewPhase_));
        for (int ch = 0; ch < numChannels; ++ch) {
            buffer.addSample(ch, i, s);
        }
    }
}

bool OpenTuneAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo()) {
        return false;
    }

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) {
        return false;
    }

    return true;
}

bool OpenTuneAudioProcessor::supportsDoublePrecisionProcessing() const {
    return true;
}

void OpenTuneAudioProcessor::processBlock(juce::AudioBuffer<double>& buffer,
                                          juce::MidiBuffer& midiMessages) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0) {
        return;
    }

    if (numChannels > doublePrecisionScratch_.getNumChannels() || numSamples > doublePrecisionScratch_.getNumSamples()) {
        buffer.clear();
        return;
    }

    for (int ch = 0; ch < numChannels; ++ch) {
        const double* src = buffer.getReadPointer(ch);
        float* dst = doublePrecisionScratch_.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            dst[i] = static_cast<float>(src[i]);
        }
    }

    juce::AudioBuffer<float> floatBuffer(doublePrecisionScratch_.getArrayOfWritePointers(), numChannels, numSamples);
    processBlock(floatBuffer, midiMessages);

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* src = floatBuffer.getReadPointer(ch);
        double* dst = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            dst[i] = static_cast<double>(src[i]);
        }
    }
}

void OpenTuneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    const double perfStartMs = juce::Time::getMillisecondCounterHiRes();
    const auto finalizePerf = [this, perfStartMs]() {
        const double durationMs = juce::Time::getMillisecondCounterHiRes() - perfStartMs;
        recordAudioCallbackDurationMs(durationMs);
    };
    
    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

#if JucePlugin_Enable_ARA
    if (isBoundToARA()) {
        if (auto* playHead = getPlayHead()) {
            const auto pos = playHead->getPosition().orFallback(juce::AudioPlayHead::PositionInfo{});
            if (auto s = pos.getTimeInSeconds()) {
                positionAtomic_->store(*s, std::memory_order_relaxed);
            }
            isPlaying_.store(pos.getIsPlaying());
            hostIsRecording_.store(pos.getIsRecording());
            hostIsLooping_.store(pos.getIsLooping());
            if (auto bpm = pos.getBpm()) {
                hostBpm_.store(*bpm);
            }
            if (auto ts = pos.getTimeSignature()) {
                hostTimeSigNum_.store(ts->numerator);
                hostTimeSigDenom_.store(ts->denominator);
            }
            if (auto ppq = pos.getPpqPosition()) {
                hostPpqPosition_.store(*ppq);
            }
            if (auto loop = pos.getLoopPoints()) {
                hostPpqLoopStart_.store(loop->ppqStart);
                hostPpqLoopEnd_.store(loop->ppqEnd);
            }
        }
    }

    if (processBlockForARA(buffer, isRealtime(), getPlayHead())) {
        finalizePerf();
        return;
    }
#endif

    if (hostIntegration_ && hostIntegration_->processIfApplicable(*this, buffer, totalNumInputChannels, totalNumOutputChannels, numSamples)) {
        finalizePerf();
        return;
    }

    // Clear output buffer
    for (int i = 0; i < totalNumOutputChannels; ++i) {
        buffer.clear(i, 0, numSamples);
    }

    const bool isFading = isFadingOut_.load();
    const bool isPlaying = isPlaying_.load();
    const bool transportStopped = !isPlaying && !isFading;

    if (transportStopped) {
        for (auto& track : tracks_) {
            track.currentRMS.store(-100.0f);
        }
        isBuffering_.store(false);
        const bool previewOn = pitchPreviewRequested_.load(std::memory_order_relaxed);
        if (!previewOn && pitchPreviewSmoothedGain_ <= 1.0e-5f) {
            finalizePerf();
            return;
        }
    }

    const double deviceSampleRate = currentSampleRate_.load();
    const double blockDurationSeconds = static_cast<double>(numSamples) / deviceSampleRate;
    const double currentPosSeconds = positionAtomic_->load(std::memory_order_relaxed);
    const double blockEndSeconds = currentPosSeconds + blockDurationSeconds;
    const int64_t blockStartSample = TimeCoordinate::secondsToSamples(currentPosSeconds, deviceSampleRate);
    const int64_t blockEndSample = blockStartSample + static_cast<int64_t>(numSamples);

    double projectEndSec = 0.0;

    if (!transportStopped) {
    const juce::ScopedReadLock tracksReadLock(tracksLock_);

    {
        constexpr double kSr = AudioConstants::StoredAudioSampleRate;
        for (int t = 0; t < MAX_TRACKS; ++t) {
            for (const auto& clip : tracks_[t].clips) {
                if (!clip.audioBuffer) {
                    continue;
                }
                const double dur = static_cast<double>(clip.audioBuffer->getNumSamples()) / kSr;
                projectEndSec = juce::jmax(projectEndSec, clip.startSeconds + dur);
            }
        }
    }
    
    for (int trackId = 0; trackId < MAX_TRACKS; ++trackId) {
        auto& track = tracks_[trackId];
        
        bool shouldPlay = true;
        if (anyTrackSoloed_) {
            if (!track.isSolo) shouldPlay = false;
        } else {
            if (track.isMuted) shouldPlay = false;
        }

        if (!shouldPlay) {
            track.currentRMS.store(-100.0f);
            continue;
        }

        float trackVolume = track.volume;
        double trackRmsSum = 0.0;
        int trackSampleCount = 0;
        
        juce::AudioBuffer<float> trackBuffer(totalNumOutputChannels, numSamples);
        trackBuffer.clear();

        bool trackHasOutput = false;

        int clipIndex = 0;
        for (auto& clip : track.clips) {
            const int64_t dryLenSamples = clip.drySignalBuffer_.getNumSamples();
            if (dryLenSamples <= 0) {
                ++clipIndex;
                continue;
            }

            const int64_t clipStartSample = TimeCoordinate::secondsToSamples(clip.startSeconds, deviceSampleRate);
            const int64_t clipEndSample = clipStartSample + dryLenSamples;

            if (clipEndSample <= blockStartSample || clipStartSample >= blockEndSample) {
                ++clipIndex;
                continue;
            }

            const int64_t overlapStartSample = std::max(blockStartSample, clipStartSample);
            const int64_t overlapEndSample = std::min(blockEndSample, clipEndSample);
            const int64_t samplesToCopy64 = overlapEndSample - overlapStartSample;
            if (samplesToCopy64 <= 0) {
                ++clipIndex;
                continue;
            }

            const int64_t readStartSample = overlapStartSample - clipStartSample;
            const int64_t readEndSample = readStartSample + samplesToCopy64;
            const double clipDurationSeconds = TimeCoordinate::samplesToSeconds(dryLenSamples, deviceSampleRate);
            const double readStartSeconds = TimeCoordinate::samplesToSeconds(readStartSample, deviceSampleRate);
            const double readEndSeconds = TimeCoordinate::samplesToSeconds(readEndSample, deviceSampleRate);
            const int offsetInBlock = static_cast<int>(overlapStartSample - blockStartSample);
            const int samplesToCopy = static_cast<int>(samplesToCopy64);

            float clipGain = clip.gain * trackVolume;
            const double fadeInSeconds = clip.fadeInDuration;
            const double fadeOutSeconds = clip.fadeOutDuration;

            double visibleStartSec = 0.0;
            double visibleEndSec = 0.0;
            bool inVisibleRange = false;
            std::shared_ptr<const PitchCurveSnapshot> snap;
            if (!bypassEnabled_.load() && !useDrySignalFallback_.load() && clip.renderCache && clip.pitchCurve) {
                snap = clip.pitchCurve->getSnapshot();
                if (snap->hasRenderableCorrectedF0() &&
                    snap->getCorrectedVisibleTimeBounds(visibleStartSec, visibleEndSec)) {
                    inVisibleRange = (readStartSeconds < visibleEndSec && readEndSeconds > visibleStartSec);
                }
            }

            std::vector<float> renderedBlock;
            bool hasRenderedAudio = false;
            
            if (inVisibleRange && clip.renderCache) {
                const int targetSampleRateInt = static_cast<int>(deviceSampleRate);
                const int numSamplesToRead = samplesToCopy;
                
                if (numSamplesToRead > 0) {
                    renderedBlock.resize(static_cast<size_t>(numSamplesToRead));
                    const double readStartSecondsInClip = readStartSeconds;
                    
                    const int readCount = clip.renderCache->readAtTimeForRate(
                        renderedBlock.data(), numSamplesToRead, readStartSecondsInClip, 
                        targetSampleRateInt, true);
                    
                    if (readCount > 0) {
                        if (readCount < numSamplesToRead) {
                            renderedBlock.resize(static_cast<size_t>(readCount));
                        }
                        hasRenderedAudio = !renderedBlock.empty();
                    } else {
                        renderedBlock.clear();
                    }
                }
            }

            for (int ch = 0; ch < totalNumOutputChannels; ++ch) {
                const int numChannels = clip.drySignalBuffer_.getNumChannels();
                int sourceCh = (numChannels > 0) ? ch % numChannels : 0;
                const float* src = (dryLenSamples > 0) 
                    ? clip.drySignalBuffer_.getReadPointer(sourceCh) 
                    : nullptr;
                float* dst = trackBuffer.getWritePointer(ch, offsetInBlock);

                const int64_t firstSampleInClip = readStartSample;
                double timeInClip = readStartSeconds;
                const double dt = 1.0 / deviceSampleRate;
                for (int s = 0; s < samplesToCopy; ++s) {
                    const int64_t sampleInClip = firstSampleInClip + static_cast<int64_t>(s);
                    float gain = clipGain;
                    
                    if (fadeInSeconds > 0.0 && timeInClip < fadeInSeconds) {
                        gain *= static_cast<float>(timeInClip / fadeInSeconds);
                    }
                    if (fadeOutSeconds > 0.0 && timeInClip >= clipDurationSeconds - fadeOutSeconds) {
                        gain *= static_cast<float>((clipDurationSeconds - timeInClip) / fadeOutSeconds);
                    }

                    float dry = (src != nullptr && sampleInClip < dryLenSamples) ? src[sampleInClip] : 0.0f;
                    float out = dry;

                    if (hasRenderedAudio && s < static_cast<int>(renderedBlock.size())) {
                        out = renderedBlock[s];
                    }

                    dst[s] += out * gain;
                    timeInClip += dt;
                }
            }

            trackHasOutput = true;
            ++clipIndex;
        }

        if (trackHasOutput) {
            for (int ch = 0; ch < totalNumOutputChannels; ++ch) {
                const float* src = trackBuffer.getReadPointer(ch);
                float* dst = buffer.getWritePointer(ch);
                
                for (int s = 0; s < numSamples; ++s) {
                    dst[s] += src[s];
                    trackRmsSum += src[s] * src[s];
                }
            }
            trackSampleCount = numSamples * totalNumOutputChannels;
        }

        if (trackSampleCount > 0) {
            float rms = std::sqrt(static_cast<float>(trackRmsSum / trackSampleCount));
            float db = (rms > 1e-9f) ? 20.0f * std::log10(rms) : -100.0f;
            track.currentRMS.store(db);
        } else {
            track.currentRMS.store(-100.0f);
        }
    }

    if (isFadingOut_.load()) {
        int fadeCount = fadeOutSampleCount_.load();
        int fadeTotal = fadeOutTotalSamples_;
        
        for (int sample = 0; sample < numSamples; ++sample) {
            int currentSample = fadeCount + sample;
            float fadeGain = 1.0f;
            
            if (currentSample < fadeTotal) {
                fadeGain = 1.0f - static_cast<float>(currentSample) / static_cast<float>(fadeTotal);
            } else {
                fadeGain = 0.0f;
            }
            
            for (int ch = 0; ch < totalNumOutputChannels; ++ch) {
                buffer.setSample(ch, sample, buffer.getSample(ch, sample) * fadeGain);
            }
        }
        
        fadeCount += numSamples;
        fadeOutSampleCount_.store(fadeCount);
        
        if (fadeCount >= fadeTotal) {
            isFadingOut_.store(false);
            isPlaying_.store(false);
            AppLogger::log("Playback: fade-out complete, stopped");
            
            for (int ch = 0; ch < totalNumOutputChannels; ++ch) {
                for (int s = 0; s < numSamples; ++s) {
                    buffer.setSample(ch, s, 0.0f);
                }
            }
        }
    }

    } // !transportStopped

    if (deviceSampleRate > 0.0 && totalNumOutputChannels > 0) {
        mixPitchPreviewIntoBuffer(buffer, deviceSampleRate, numSamples, totalNumOutputChannels);
    }

    if (!transportStopped) {
        double nextPos = blockEndSeconds;
        if (!loopEnabled_.load(std::memory_order_relaxed) && projectEndSec > 1e-9) {
            if (currentPosSeconds < projectEndSec && blockEndSeconds >= projectEndSec && isPlaying_.load(std::memory_order_relaxed)) {
                setPlaying(false);
            }
            nextPos = std::min(nextPos, projectEndSec);
        }
        positionAtomic_->store(nextPos, std::memory_order_relaxed);
    }
    finalizePerf();
}

void OpenTuneAudioProcessor::recordAudioCallbackDurationMs(double durationMs)
{
    if (durationMs < 0.0) {
        durationMs = 0.0;
    }
    const double cappedMs = std::min(20.0, durationMs);
    int bin = static_cast<int>(std::floor(cappedMs / PerfHistogramStepMs));
    bin = juce::jlimit(0, PerfHistogramBins - 1, bin);
    perfAudioDurationHistogram_[(size_t)bin].fetch_add(1, std::memory_order_relaxed);
    perfAudioCallbackCount_.fetch_add(1, std::memory_order_relaxed);
}

void OpenTuneAudioProcessor::recordCacheCheck(bool cacheHit)
{
    perfCacheChecks_.fetch_add(1, std::memory_order_relaxed);
    if (!cacheHit) {
        perfCacheMisses_.fetch_add(1, std::memory_order_relaxed);
    }
}

double OpenTuneAudioProcessor::computeAudioCallbackPercentileMs(double percentile) const
{
    const uint64_t total = perfAudioCallbackCount_.load(std::memory_order_relaxed);
    if (total == 0) {
        return 0.0;
    }

    const double clampedPercentile = juce::jlimit(0.0, 1.0, percentile);
    const uint64_t targetRank = static_cast<uint64_t>(std::ceil(clampedPercentile * static_cast<double>(total)));
    uint64_t cumulative = 0;

    for (int i = 0; i < PerfHistogramBins; ++i) {
        cumulative += static_cast<uint64_t>(perfAudioDurationHistogram_[(size_t)i].load(std::memory_order_relaxed));
        if (cumulative >= targetRank) {
            return static_cast<double>(i) * PerfHistogramStepMs;
        }
    }

    return static_cast<double>(PerfHistogramBins - 1) * PerfHistogramStepMs;
}

OpenTuneAudioProcessor::PerfProbeSnapshot OpenTuneAudioProcessor::getPerfProbeSnapshot() const
{
    PerfProbeSnapshot snapshot;
    snapshot.audioCallbackP99Ms = computeAudioCallbackPercentileMs(0.99);

    snapshot.cacheChecks = perfCacheChecks_.load(std::memory_order_relaxed);
    snapshot.cacheMisses = perfCacheMisses_.load(std::memory_order_relaxed);
    if (snapshot.cacheChecks > 0) {
        snapshot.cacheMissRate = static_cast<double>(snapshot.cacheMisses) / static_cast<double>(snapshot.cacheChecks);
    }

    {
        const juce::ScopedReadLock rl(tracksLock_);
        int totalPending = 0;
        for (int t = 0; t < MAX_TRACKS; ++t) {
            for (const auto& clip : tracks_[t].clips) {
                if (clip.renderCache) {
                    totalPending += clip.renderCache->getPendingCount();
                }
            }
        }
        snapshot.renderQueueDepth = totalPending;
    }

    return snapshot;
}

RenderCache::ChunkStats OpenTuneAudioProcessor::getClipChunkStats(int trackId, int clipIndex) const
{
    if (trackId < 0 || trackId >= MAX_TRACKS) {
        return {};
    }

    const juce::ScopedReadLock rl(tracksLock_);
    const auto& clips = tracks_[trackId].clips;
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size())) {
        return {};
    }

    const auto& clip = clips[clipIndex];
    if (!clip.renderCache) {
        return {};
    }

    return clip.renderCache->getChunkStats();
}

void OpenTuneAudioProcessor::resetPerfProbeCounters()
{
    perfCacheChecks_.store(0, std::memory_order_relaxed);
    perfCacheMisses_.store(0, std::memory_order_relaxed);
    perfAudioCallbackCount_.store(0, std::memory_order_relaxed);
    for (auto& bin : perfAudioDurationHistogram_) {
        bin.store(0, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* OpenTuneAudioProcessor::createEditor() {
    return createOpenTuneEditor(*this);
}

bool OpenTuneAudioProcessor::hasEditor() const {
    return true;
}

void OpenTuneAudioProcessor::bumpEditVersion() {
    if (editVersionParam_ == nullptr) {
        return;
    }
    const int v = editVersionParam_->get();
    const int next = (v + 1) % 100001;
    const float norm = static_cast<float>(next) / 100000.0f;
    editVersionParam_->beginChangeGesture();
    editVersionParam_->setValueNotifyingHost(norm);
    editVersionParam_->endChangeGesture();
}

void OpenTuneAudioProcessor::showAudioSettingsDialog(juce::AudioProcessorEditor& editor)
{
    if (hostIntegration_) {
        hostIntegration_->audioSettingsRequested(editor);
        return;
    }
    juce::ignoreUnused(editor);
}

void OpenTuneAudioProcessor::invalidateClipRender(uint64_t clipId, int startFrame, int endFrame)
{
    if (clipRenderInvalidation_) {
        clipRenderInvalidation_->invalidateByClipId(clipId, startFrame, endFrame);
    }
}

void OpenTuneAudioProcessor::enqueuePartialRender(int trackId,
                                                  int clipIndex,
                                                  double relStartSeconds,
                                                  double relEndSeconds,
                                                  uint64_t targetRevision)
{
    AppLogger::log("RenderTrace: enqueuePartialRender called track=" + juce::String(trackId)
        + " clip=" + juce::String(clipIndex)
        + " range=[" + juce::String(relStartSeconds, 3) + "," + juce::String(relEndSeconds, 3) + "]");

    if (trackId < 0 || trackId >= MAX_TRACKS || clipIndex < 0) {
        AppLogger::log("RenderTrace: enqueuePartialRender early-return invalid track/clip");
        return;
    }

    if (relEndSeconds <= relStartSeconds) {
        AppLogger::log("RenderTrace: enqueuePartialRender early-return invalid range");
        return;
    }

    std::shared_ptr<RenderCache> renderCache;
    std::vector<double> chunkBoundaries;
    {
        const juce::ScopedReadLock rl(tracksLock_);
        const auto& clips = tracks_[(size_t)trackId].clips;
        if (clipIndex >= static_cast<int>(clips.size())) {
            return;
        }

        const auto& clip = clips[(size_t)clipIndex];
        renderCache = clip.renderCache;

        chunkBoundaries = buildChunkBoundariesFromSilentGaps(
            clip.audioBuffer->getNumSamples(),
            clip.silentGaps);
    }

    if (!renderCache) {
        return;
    }

    // 对每个受影响的 Chunk 请求渲染
    // - 若 Chunk 处于 Idle：状态变为 Pending，加入 pendingChunks_ 集合
    // - 若 Chunk 处于 Pending/Running：只更新 desiredRevision（版本去重）
    int requestedCount = 0;
    if (chunkBoundaries.size() < 2) {
        renderCache->requestRenderPending(relStartSeconds, relEndSeconds, targetRevision);
        requestedCount = 1;
    } else {
        for (size_t i = 0; i + 1 < chunkBoundaries.size(); ++i) {
            const double chunkStartSec = chunkBoundaries[i];
            const double chunkEndSec = chunkBoundaries[i + 1];

            const double overlapStart = std::max(relStartSeconds, chunkStartSec);
            const double overlapEnd = std::min(relEndSeconds, chunkEndSec);

            if (overlapEnd > overlapStart) {
                renderCache->requestRenderPending(chunkStartSec, chunkEndSec, targetRevision);
                ++requestedCount;
            }
        }
    }

    AppLogger::log("RenderTrace: enqueuePartialRender requested=" + juce::String(requestedCount)
        + " pendingTotal=" + juce::String(renderCache->getPendingCount()));

    // 唤醒 Worker
    schedulerCv_.notify_one();
}

bool OpenTuneAudioProcessor::hasPendingRenderJobs() const
{
    const juce::ScopedReadLock rl(tracksLock_);
    for (int t = 0; t < MAX_TRACKS; ++t) {
        for (const auto& clip : tracks_[t].clips) {
            if (clip.renderCache && clip.renderCache->getPendingCount() > 0) {
                return true;
            }
        }
    }
    return false;
}

void OpenTuneAudioProcessor::chunkRenderWorkerLoop()
{
    AppLogger::log("RenderTrace: chunkRenderWorkerLoop started");

    while (true) {
        {
            std::unique_lock<std::mutex> lock(schedulerMutex_);
            schedulerCv_.wait(lock, [this]() {
                return !chunkRenderWorkerRunning_.load(std::memory_order_acquire)
                    || hasPendingRenderJobs();
            });

            if (!chunkRenderWorkerRunning_) {
                AppLogger::log("RenderTrace: chunkRenderWorkerLoop exiting");
                return;
            }
        }

        ClipChunkRenderPipeline::runOneIteration(*this);
    }
}
void OpenTuneAudioProcessor::performUndo() {
    if (globalUndoManager_.canUndo()) {
        DBG("Global Undo: " + globalUndoManager_.getUndoDescription());
        globalUndoManager_.undo();
    }
}

void OpenTuneAudioProcessor::performRedo() {
    if (globalUndoManager_.canRedo()) {
        DBG("Global Redo: " + globalUndoManager_.getRedoDescription());
        globalUndoManager_.redo();
    }
}

} // namespace OpenTune

// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpenTune::OpenTuneAudioProcessor();
}
