#include "PianoRollComponent.h"
#include "../Utils/AppLogger.h"
#include "../Utils/NoteGenerator.h"
#include "../../PluginProcessor.h"
#include "FrameScheduler.h"

#include <algorithm>
#include <cmath>

namespace OpenTune {

namespace {

ScaleMode scaleModeFromTransportScaleType(int scaleType) noexcept
{
    switch (scaleType)
    {
        case 1: return ScaleMode::Major;
        case 2: return ScaleMode::Minor;
        case 4: return ScaleMode::HarmonicMinor;
        case 5: return ScaleMode::Dorian;
        case 6: return ScaleMode::Mixolydian;
        case 7: return ScaleMode::PentatonicMajor;
        case 8: return ScaleMode::PentatonicMinor;
        default: return ScaleMode::Major;
    }
}

} // namespace

bool PianoRollComponent::applyCorrectionAsyncForEntireClip(float retuneSpeed, float vibratoDepth, float vibratoRate)
{
    if (isAutoTuneProcessing())
        return false;

    if (!currentCurve_)
        return false;

    auto snapshot = currentCurve_->getSnapshot();
    const int maxFrame = static_cast<int>(snapshot->size());
    if (maxFrame <= 0)
        return false;

    undoSupport_->beginTransaction("Change Parameters");
    auto request = std::make_shared<PianoRollCorrectionWorker::AsyncCorrectionRequest>();
    request->curve = currentCurve_;
    request->notes = getCurrentClipNotesCopy();
    request->startFrame = 0;
    request->endFrameExclusive = maxFrame;
    request->retuneSpeed = retuneSpeed;
    request->vibratoDepth = vibratoDepth;
    request->vibratoRate = vibratoRate;
    request->audioSampleRate = static_cast<double>(PianoRollComponent::kAudioSampleRate);

    correctionWorker_->enqueue(request);
    return true;
}

void PianoRollComponent::consumeCompletedCorrectionResults()
{
    if (!correctionWorker_)
        return;

    auto completed = correctionWorker_->takeCompleted();
    if (!completed)
        return;

    const bool wasAutoTune = completed->kind == PianoRollCorrectionWorker::AsyncCorrectionRequest::Kind::AutoTuneGenerate;

    if (completed->success && wasAutoTune)
        setNotes(completed->notes);

    if (undoSupport_ && undoSupport_->isTransactionActive() && !vibratoToolCorrectionCommitSuppressed_)
        undoSupport_->commitTransaction();

    if (completed->success)
    {
        const int notifyStart = wasAutoTune ? completed->autoStartFrame : completed->startFrame;
        const int notifyEndExclusive = wasAutoTune ? (completed->autoEndFrame + 1) : completed->endFrameExclusive;
        const int notifyEnd = std::max(notifyStart, notifyEndExclusive - 1);

        const uint64_t clipId = completed->clipIdSnapshot != 0 ? completed->clipIdSnapshot : currentClipId_;
        notifyPitchCurveRenderInvalidated(clipId, notifyStart, notifyEnd);
    }

    if (wasAutoTune)
    {
        correctionInFlight_.store(false, std::memory_order_release);
        if (onRenderComplete_)
            onRenderComplete_();
    }

    requestInteractiveRepaint();
}

void PianoRollComponent::enqueueManualCorrectionPatchAsync(const std::vector<PianoRollToolHandler::ManualCorrectionOp>& ops,
                                                           int dirtyStartFrame,
                                                           int dirtyEndFrame,
                                                           bool triggerRenderEvent)
{
    if (!currentCurve_ || ops.empty())
        return;

    for (const auto& op : ops)
    {
        if (op.endFrameExclusive <= op.startFrame)
            continue;

        currentCurve_->setManualCorrectionRange(
            op.startFrame,
            op.endFrameExclusive,
            op.f0Data,
            op.source,
            op.retuneSpeed);
    }

    if (triggerRenderEvent && dirtyEndFrame >= dirtyStartFrame) {
        notifyPitchCurveRenderInvalidated(currentClipId_, dirtyStartFrame, dirtyEndFrame);
    }

    requestInteractiveRepaint();
}

void PianoRollComponent::enqueueNoteBasedCorrectionAsync(int startFrame,
                                                         int endFrameExclusive,
                                                         float retuneSpeed,
                                                         float vibratoDepth,
                                                         float vibratoRate)
{
    if (!currentCurve_)
        return;

    auto request = std::make_shared<PianoRollCorrectionWorker::AsyncCorrectionRequest>();
    request->curve = currentCurve_;
    request->notes = getCurrentClipNotesCopy();
    request->startFrame = startFrame;
    request->endFrameExclusive = endFrameExclusive;
    request->retuneSpeed = retuneSpeed;
    request->vibratoDepth = vibratoDepth;
    request->vibratoRate = vibratoRate;
    request->audioSampleRate = static_cast<double>(PianoRollComponent::kAudioSampleRate);
    if (correctionWorker_) {
        request->clipContextGenerationSnapshot = correctionWorker_->getClipContextGeneration();
    }
    request->trackIdSnapshot = currentTrackId_;
    request->clipIdSnapshot = currentClipId_;
    correctionWorker_->enqueue(request);
}

void PianoRollComponent::setRenderingProgress(float progress, int pendingTasks)
{
    bool wasRendering = isRendering_;
    renderingProgress_ = progress;
    pendingRenderTasks_ = pendingTasks;
    isRendering_ = (pendingTasks > 0) || (progress > 0.0f);

    if (isRendering_)
        startTimerHz(30);
    else if (!inferenceActive_)
        stopTimer();

    if (wasRendering != isRendering_ || isRendering_)
        repaint();
}

void PianoRollComponent::setInferenceActive(bool active)
{
    inferenceActive_ = active;
    waveformBuildTickCounter_ = 0;

    if (isRendering_)
        startTimerHz(30);
    else
        stopTimer();
}

bool PianoRollComponent::applyRetuneSpeedToSelectedNotes(float speed)
{
    bool hasSelectedNotes = false;
    for (const auto& n : getCurrentClipNotes())
    {
        if (n.selected)
        {
            hasSelectedNotes = true;
            break;
        }
    }
    if (!hasSelectedNotes)
        return false;

    undoSupport_->beginTransaction("Retune Speed");
    double dirtyStartTime = 1e30;
    double dirtyEndTime = -1e30;
    const double frameDuration = hopSize_ / f0SampleRate_;
    for (auto& n : getCurrentClipNotes())
    {
        if (!n.selected)
            continue;

        n.retuneSpeed = speed;
        n.dirty = true;
        dirtyStartTime = std::min(dirtyStartTime, n.startTime);
        dirtyEndTime = std::max(dirtyEndTime, n.endTime);
    }

    if (dirtyEndTime > dirtyStartTime)
    {
        int startFrame = static_cast<int>(dirtyStartTime / frameDuration);
        int endFrame = static_cast<int>(dirtyEndTime / frameDuration);
        if (startFrame < 0)
            startFrame = 0;
        if (endFrame < startFrame)
            endFrame = startFrame;

        auto request = std::make_shared<PianoRollCorrectionWorker::AsyncCorrectionRequest>();
        request->curve = currentCurve_;
        request->notes = getCurrentClipNotesCopy();
        request->startFrame = startFrame;
        request->endFrameExclusive = endFrame + 1;
        request->retuneSpeed = speed;
        request->vibratoDepth = currentVibratoDepth_;
        request->vibratoRate = currentVibratoRate_;
        request->audioSampleRate = static_cast<double>(PianoRollComponent::kAudioSampleRate);
        correctionWorker_->enqueue(request);
        undoSupport_->commitTransaction();
        notifyPitchCurveRenderInvalidated(currentClipId_, startFrame, endFrame);
    }
    else
    {
        undoSupport_->commitTransaction();
    }

    repaint();
    return true;
}

bool PianoRollComponent::applyRetuneSpeedToSelection(float speed)
{
    if (isAutoTuneProcessing())
        return false;

    speed = juce::jlimit(0.0f, 1.0f, speed);
    if (!currentCurve_)
        return false;

    return applyRetuneSpeedToSelectedNotes(speed);
}

bool PianoRollComponent::hasSelectionRange() const
{
    const auto& notes = const_cast<PianoRollComponent*>(this)->getCurrentClipNotes();
    for (const auto& n : notes)
    {
        if (n.selected)
            return true;
    }
    return false;
}

std::pair<double, double> PianoRollComponent::getSelectionTimeRange() const
{
    double minTime = 1e30;
    double maxTime = -1e30;
    const auto& notes = const_cast<PianoRollComponent*>(this)->getCurrentClipNotes();
    for (const auto& n : notes)
    {
        if (n.selected)
        {
            minTime = std::min(minTime, n.startTime);
            maxTime = std::max(maxTime, n.endTime);
        }
    }

    if (minTime > maxTime)
        return { 0.0, 0.0 };

    return { minTime, maxTime };
}

bool PianoRollComponent::applyVibratoDepthToSelection(float depth)
{
    return applyVibratoParameterToSelection(VibratoParam::Depth, depth);
}

bool PianoRollComponent::applyVibratoRateToSelection(float rate)
{
    return applyVibratoParameterToSelection(VibratoParam::Rate, rate);
}

void PianoRollComponent::enqueueVibratoCorrectionForTimeSpan(
    VibratoParam primaryParam,
    float primaryValue,
    double dirtyStartTime,
    double dirtyEndTime)
{
    if (!currentCurve_ || isAutoTuneProcessing())
        return;
    if (dirtyEndTime <= dirtyStartTime)
        return;

    const double frameDuration = hopSize_ / f0SampleRate_;
    int startFrame = static_cast<int>(dirtyStartTime / frameDuration);
    int endFrame = static_cast<int>(dirtyEndTime / frameDuration);
    if (startFrame < 0)
        startFrame = 0;
    if (endFrame < startFrame)
        endFrame = startFrame;

    auto request = std::make_shared<PianoRollCorrectionWorker::AsyncCorrectionRequest>();
    request->curve = currentCurve_;
    request->notes = getCurrentClipNotesCopy();
    request->startFrame = startFrame;
    request->endFrameExclusive = endFrame + 1;
    request->retuneSpeed = currentRetuneSpeed_;
    request->vibratoDepth = (primaryParam == VibratoParam::Depth) ? primaryValue : currentVibratoDepth_;
    request->vibratoRate = (primaryParam == VibratoParam::Rate) ? primaryValue : currentVibratoRate_;
    request->audioSampleRate = static_cast<double>(PianoRollComponent::kAudioSampleRate);
    if (correctionWorker_)
        request->clipContextGenerationSnapshot = correctionWorker_->getClipContextGeneration();
    request->trackIdSnapshot = currentTrackId_;
    request->clipIdSnapshot = currentClipId_;
    correctionWorker_->enqueue(request);
}

void PianoRollComponent::beginVibratoToolUndo(const juce::String& description)
{
    if (!undoSupport_ || undoSupport_->isTransactionActive())
        return;
    undoSupport_->beginTransaction(description);
    vibratoToolCorrectionCommitSuppressed_ = true;
}

void PianoRollComponent::commitVibratoToolUndo()
{
    vibratoToolCorrectionCommitSuppressed_ = false;
    if (undoSupport_ && undoSupport_->isTransactionActive())
        undoSupport_->commitTransaction();
}

void PianoRollComponent::applyVibratoToolLiveOnNote(Note* note, bool adjustRate, float absoluteValue)
{
    if (note == nullptr || !currentCurve_ || isAutoTuneProcessing())
        return;

    if (adjustRate)
        note->vibratoRate = juce::jlimit(3.0f, 12.0f, absoluteValue);
    else
        note->vibratoDepth = juce::jlimit(0.0f, 100.0f, absoluteValue);

    note->dirty = true;
    enqueueVibratoCorrectionForTimeSpan(
        adjustRate ? VibratoParam::Rate : VibratoParam::Depth,
        adjustRate ? note->vibratoRate : note->vibratoDepth,
        note->startTime,
        note->endTime);

    if (adjustRate)
        repaint();
    else
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Normal);
}

bool PianoRollComponent::applyVibratoParameterToSelection(VibratoParam param, float value)
{
    if (isAutoTuneProcessing())
        return false;

    auto clampValue = [&]() -> float {
        return (param == VibratoParam::Depth) ? juce::jlimit(0.0f, 100.0f, value)
                                              : juce::jlimit(0.1f, 30.0f, value);
    };

    value = clampValue();
    if (!currentCurve_)
        return false;

    bool hasSelectedNotes = false;
    for (const auto& n : getCurrentClipNotes())
    {
        if (n.selected)
        {
            hasSelectedNotes = true;
            break;
        }
    }
    if (!hasSelectedNotes)
        return false;

    undoSupport_->beginTransaction(param == VibratoParam::Depth ? "Vibrato Depth" : "Vibrato Rate");
    double dirtyStartTime = 1e30;
    double dirtyEndTime = -1e30;

    for (auto& n : getCurrentClipNotes())
    {
        if (!n.selected)
            continue;

        if (param == VibratoParam::Depth)
            n.vibratoDepth = value;
        else
            n.vibratoRate = value;

        n.dirty = true;
        dirtyStartTime = std::min(dirtyStartTime, n.startTime);
        dirtyEndTime = std::max(dirtyEndTime, n.endTime);
    }

    if (dirtyEndTime > dirtyStartTime)
        enqueueVibratoCorrectionForTimeSpan(param, value, dirtyStartTime, dirtyEndTime);
    else
        undoSupport_->commitTransaction();

    if (param == VibratoParam::Depth)
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Normal);
    else
        repaint();

    return true;
}

bool PianoRollComponent::getSingleSelectedNoteParameters(float& retuneSpeedPercent, float& vibratoDepth, float& vibratoRate) const
{
    const auto notes = getCurrentClipNotesCopy();
    const Note* selectedNote = nullptr;

    for (const auto& note : notes)
    {
        if (!note.selected)
            continue;

        if (selectedNote != nullptr)
            return false;

        selectedNote = &note;
    }

    if (selectedNote == nullptr)
        return false;

    const float resolvedRetuneSpeed = selectedNote->retuneSpeed >= 0.0f ? selectedNote->retuneSpeed : currentRetuneSpeed_;
    const float resolvedVibratoDepth = selectedNote->vibratoDepth >= 0.0f ? selectedNote->vibratoDepth : currentVibratoDepth_;
    const float resolvedVibratoRate = selectedNote->vibratoRate >= 0.0f ? selectedNote->vibratoRate : currentVibratoRate_;

    retuneSpeedPercent = juce::jlimit(0.0f, 100.0f, resolvedRetuneSpeed * 100.0f);
    vibratoDepth = juce::jlimit(0.0f, 100.0f, resolvedVibratoDepth);
    vibratoRate = juce::jlimit(3.0f, 12.0f, resolvedVibratoRate);
    return true;
}

void PianoRollComponent::setNoteSplit(float value)
{
    segmentationPolicy_.transitionThresholdCents = juce::jlimit(
        OpenTune::PitchControlConfig::kMinNoteSplitCents,
        OpenTune::PitchControlConfig::kMaxNoteSplitCents,
        value);

    requestInteractiveRepaint();
}

float PianoRollComponent::recalculatePIP(Note& note)
{
    if (!currentCurve_)
        return -1.0f;

    if (note.endTime <= note.startTime)
        return -1.0f;

    auto snapshot = currentCurve_->getSnapshot();
    const auto& originalF0 = snapshot->getOriginalF0();

    const double frameDuration = hopSize_ / f0SampleRate_;
    int startFrame = static_cast<int>(note.startTime / frameDuration);
    int endFrame = static_cast<int>(note.endTime / frameDuration);

    if (startFrame < 0)
        startFrame = 0;
    if (endFrame > static_cast<int>(originalF0.size()))
        endFrame = static_cast<int>(originalF0.size());

    if (startFrame >= endFrame)
        return -1.0f;

    int numFrames = endFrame - startFrame;
    std::vector<float> noteF0(numFrames);
    std::copy(originalF0.begin() + startFrame, originalF0.begin() + endFrame, noteF0.begin());

    std::vector<float> voicedF0;
    voicedF0.reserve(noteF0.size());
    for (float f : noteF0)
    {
        if (f > 0.0f)
            voicedF0.push_back(f);
    }

    if (voicedF0.empty())
        return -1.0f;

    std::sort(voicedF0.begin(), voicedF0.end());
    float medianF0 = voicedF0[voicedF0.size() / 2];
    return medianF0;
}

bool PianoRollComponent::applyAutoTuneToSelection()
{
    AppLogger::log("AutoTuneTrace: applyAutoTuneToSelection called");
    DBG("PianoRollComponent::applyAutoTuneToSelection - called");
    if (!currentCurve_)
    {
        AppLogger::log("AutoTuneTrace: applyAutoTuneToSelection - currentCurve_ is null");
        DBG("PianoRollComponent::applyAutoTuneToSelection - currentCurve_ is null");
        return false;
    }

    if (currentTrackId_ < 0 || currentClipId_ == 0)
    {
        AppLogger::log("AutoTuneTrace: AUTO failed - missing valid clip context trackId="
            + juce::String(currentTrackId_) + " clipId=" + juce::String(static_cast<juce::int64>(currentClipId_)));
        DBG("PianoRoll: AUTO failed - missing valid clip context (trackId="
            + juce::String(currentTrackId_) + ", clipId=" + juce::String(static_cast<juce::int64>(currentClipId_)) + ")");
        return false;
    }

    if (correctionInFlight_.exchange(true, std::memory_order_acq_rel))
    {
        AppLogger::log("AutoTuneTrace: AUTO processing, ignoring duplicate request");
        DBG("PianoRoll: AUTO processing, ignoring duplicate request");
        return false;
    }

    AppLogger::log("AutoTuneTrace: proceeding with AUTO trackId=" + juce::String(currentTrackId_)
        + " clipId=" + juce::String(static_cast<juce::int64>(currentClipId_)));
    DBG("PianoRollComponent::applyAutoTuneToSelection - proceeding with AUTO");

    repaint();

    auto snapshot = currentCurve_->getSnapshot();
    const double frameDuration = hopSize_ / f0SampleRate_;

    double startTime = 0.0;
    double endTime = 0.0;

    // AUTO always targets the full clip range to avoid selection-dependent partial rendering.
    const size_t f0Length = snapshot->size();
    if (f0Length < 2)
    {
        correctionInFlight_.store(false, std::memory_order_release);
        if (onRenderComplete_)
            onRenderComplete_();
        return false;
    }

    startTime = 0.0;
    int lastFrame = static_cast<int>(f0Length) - 1;
    endTime = (lastFrame + 1) * frameDuration;
    if (audioBuffer_ != nullptr)
    {
        double maxTime = static_cast<double>(audioBuffer_->getNumSamples()) / PianoRollComponent::kAudioSampleRate;
        endTime = std::min(endTime, maxTime);
    }
    endTime = std::max(0.0, endTime);

    if (endTime <= startTime)
    {
        correctionInFlight_.store(false, std::memory_order_release);
        if (onRenderComplete_)
            onRenderComplete_();
        return false;
    }

    int startFrame = static_cast<int>(startTime / frameDuration);
    int endFrame = static_cast<int>(endTime / frameDuration);
    startFrame = std::max(0, startFrame);

    const auto& originalF0 = snapshot->getOriginalF0();
    if (originalF0.empty())
    {
        correctionInFlight_.store(false, std::memory_order_release);
        if (onRenderComplete_)
            onRenderComplete_();
        return false;
    }

    endFrame = std::min(static_cast<int>(originalF0.size()) - 1, endFrame);
    if (endFrame <= startFrame)
    {
        correctionInFlight_.store(false, std::memory_order_release);
        if (onRenderComplete_)
            onRenderComplete_();
        return false;
    }

    const bool useScaleSnap = (scaleType_ != 3);

    DBG("AutoTuneTrace: trackId=" + juce::String(currentTrackId_)
        + " clipId=" + juce::String(static_cast<juce::int64>(currentClipId_))
        + " root=" + juce::String(scaleRootNote_)
        + " scaleType=" + juce::String(scaleType_)
        + " useScaleSnap=" + juce::String(useScaleSnap ? 1 : 0));

    NoteGeneratorParams genParams;
    genParams.policy = segmentationPolicy_;
    genParams.policy.energyValleySplitEnabled = true;
    genParams.policy.adjacentNextNoteMaxGapMs = 150.0f;
    genParams.retuneSpeed = currentRetuneSpeed_;
    genParams.vibratoDepth = currentVibratoDepth_;
    genParams.vibratoRate = currentVibratoRate_;
    if (useScaleSnap)
    {
        ScaleSnapConfig snapCfg;
        snapCfg.root = scaleRootNote_ % 12;
        snapCfg.mode = scaleModeFromTransportScaleType(scaleType_);
        genParams.scaleSnap = snapCfg;
    }

    undoSupport_->beginTransaction("Auto Tune");

    auto request = std::make_shared<PianoRollCorrectionWorker::AsyncCorrectionRequest>();
    request->kind = PianoRollCorrectionWorker::AsyncCorrectionRequest::Kind::AutoTuneGenerate;
    request->curve = currentCurve_;
    request->startFrame = startFrame;
    request->endFrameExclusive = endFrame + 1;
    request->retuneSpeed = currentRetuneSpeed_;
    request->vibratoDepth = currentVibratoDepth_;
    request->vibratoRate = currentVibratoRate_;
    request->audioSampleRate = static_cast<double>(PianoRollComponent::kAudioSampleRate);

    request->autoOriginalF0Full = originalF0;
    const auto& originalEnergy = snapshot->getOriginalEnergy();
    if (originalEnergy.size() == originalF0.size())
        request->autoOriginalEnergyFull = originalEnergy;
    else
        request->autoOriginalEnergyFull.clear();

    request->autoHopSize = hopSize_;
    request->autoF0SampleRate = f0SampleRate_;
    request->autoStartFrame = startFrame;
    request->autoEndFrame = endFrame;
    request->autoGenParams = genParams;

    if (correctionWorker_) {
        request->clipContextGenerationSnapshot = correctionWorker_->getClipContextGeneration();
    }
    request->trackIdSnapshot = currentTrackId_;
    request->clipIdSnapshot = currentClipId_;

    DBG("PianoRollComponent::applyAutoTuneToSelection - enqueuing request: startFrame=" + juce::String(startFrame)
        + " endFrame=" + juce::String(endFrame)
        + " autoStartFrame=" + juce::String(request->autoStartFrame)
        + " autoEndFrame=" + juce::String(request->autoEndFrame)
        + " autoOriginalF0Full.size=" + juce::String(static_cast<int>(request->autoOriginalF0Full.size())));

    correctionWorker_->enqueue(request);

    DBG("PianoRollComponent::applyAutoTuneToSelection - enqueue done, returning true");

    return true;
}

void PianoRollComponent::setNotes(const std::vector<Note>& notes)
{
    auto& clipNotes = getCurrentClipNotes();
    clipNotes = notes;
    std::sort(clipNotes.begin(), clipNotes.end(), [](const Note& a, const Note& b) {
        return a.startTime < b.startTime;
    });
    for (size_t i = 1; i < clipNotes.size(); ++i)
    {
        if (clipNotes[i - 1].endTime > clipNotes[i].startTime)
            clipNotes[i - 1].endTime = clipNotes[i].startTime;
    }
    clipNotes.erase(
        std::remove_if(clipNotes.begin(), clipNotes.end(), [](const Note& n) {
            return n.endTime <= n.startTime;
        }),
        clipNotes.end());

    updateScrollBars();

    // When switching clip while LineAnchor tool is active, rebuild anchor display from the
    // current pitch curve snapshot (otherwise previous clip's anchors may remain).
    if (currentTool_ == ToolId::LineAnchor && currentCurve_)
    {
        auto snap = currentCurve_->getSnapshot();
        if (snap)
        {
            interactionState_.drawing.clearAnchors();
            interactionState_.drawing.anchorEdit.groups = snap->getAnchorGroups();
            interactionState_.drawing.anchorEdit.mode = AnchorEditState::Mode::Idle;
            interactionState_.drawing.anchorEdit.activeGroupIndex = -1;
            interactionState_.drawing.anchorEdit.draggedAnchorIndex = -1;
            anchorFitOverlay_.setVisible(false);
            anchorFitting_.store(false, std::memory_order_release);
        }
        else
        {
            interactionState_.drawing.clearAnchors();
            interactionState_.drawing.anchorEdit.clear();
            anchorFitOverlay_.setVisible(false);
            anchorFitting_.store(false, std::memory_order_release);
        }
    }
    repaint();
}

void PianoRollComponent::refreshAfterUndoRedo()
{
    refreshAfterUndoRedoWithRange(-1, -1);
}

void PianoRollComponent::refreshAfterUndoRedoWithRange(int startFrame, int endFrame)
{
    updateScrollBars();

    if (currentTool_ == ToolId::LineAnchor && currentCurve_)
    {
        auto snap = currentCurve_->getSnapshot();
        if (snap)
        {
            const auto& groups = snap->getAnchorGroups();
            interactionState_.drawing.anchorEdit.groups = groups;
            interactionState_.drawing.anchorEdit.mode = AnchorEditState::Mode::Idle;
            interactionState_.drawing.anchorEdit.activeGroupIndex = -1;
            interactionState_.drawing.anchorEdit.draggedAnchorIndex = -1;
        }
        else
        {
            interactionState_.drawing.anchorEdit.clear();
        }
    }
    else
    {
        interactionState_.drawing.anchorEdit.clear();
    }

    if (currentCurve_)
    {
        int affectedStartFrame = startFrame;
        int affectedEndFrame = endFrame;

        if (affectedStartFrame < 0 || affectedEndFrame < 0)
        {
            AppLogger::log("refreshAfterUndoRedo: no diff range (no changes detected), skipping render");
            repaint();
            return;
        }

        AppLogger::log("refreshAfterUndoRedo: using provided range [" + juce::String(affectedStartFrame)
            + ", " + juce::String(affectedEndFrame) + "]");

        notifyPitchCurveRenderInvalidated(currentClipId_, affectedStartFrame, affectedEndFrame);
    }
    repaint();
}

} // namespace OpenTune
