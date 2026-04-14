#include "VocalTimelineStrip.h"
#include "../../PluginProcessor.h"
#include "PianoRollComponent.h"
#include "../../Utils/SingingEditDocument.h"

namespace OpenTune {

VocalTimelineStrip::VocalTimelineStrip(OpenTuneAudioProcessor& processor, PianoRollComponent& pianoRoll,
                                       UndoManager& undoManager)
    : processor_(processor)
    , pianoRoll_(pianoRoll)
    , undoManager_(undoManager)
{
    startTimerHz(10);
}

void VocalTimelineStrip::setTrackClip(int trackId, int clipIndex)
{
    trackId_ = trackId;
    clipIndex_ = clipIndex;
    repaint();
}

double VocalTimelineStrip::xToClipLocalSec(float x) const
{
    const double visStart = pianoRoll_.getVisibleStartTimeSeconds();
    const double visDur = juce::jmax(1e-6, pianoRoll_.getVisibleDurationSeconds());
    const float w = juce::jmax(1.0f, static_cast<float>(getWidth()));
    return visStart + static_cast<double>(x / w) * visDur;
}

float VocalTimelineStrip::clipLocalSecToX(double sec) const
{
    const double visStart = pianoRoll_.getVisibleStartTimeSeconds();
    const double visDur = juce::jmax(1e-6, pianoRoll_.getVisibleDurationSeconds());
    const float w = static_cast<float>(getWidth());
    return static_cast<float>((sec - visStart) / visDur * static_cast<double>(w));
}

void VocalTimelineStrip::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff22262c));
    if (trackId_ < 0 || clipIndex_ < 0) {
        return;
    }

    processor_.readClipSingingEdit(trackId_, clipIndex_, [this, &g](const SingingEditDocument& doc) {
        const auto& segs = doc.getSegments().getSegments();
        g.setColour(juce::Colour(0xff3d8b6e).withAlpha(0.55f));
        for (const auto& s : segs) {
            const float x0 = clipLocalSecToX(s.getStartClipSeconds());
            const float x1 = clipLocalSecToX(s.getEndClipSeconds());
            juce::Rectangle<float> r(x0, 4.0f, juce::jmax(2.0f, x1 - x0), static_cast<float>(getHeight()) - 8.0f);
            g.fillRoundedRectangle(r, 3.0f);
        }
    });

    if (processor_.isRefinePendingForClip(trackId_, clipIndex_)) {
        g.setColour(juce::Colours::orange.withAlpha(0.9f));
        g.drawText("Refine pending...", getLocalBounds().reduced(6), juce::Justification::centredRight, false);
    }
}

void VocalTimelineStrip::mouseDown(const juce::MouseEvent& e)
{
    if (trackId_ < 0 || clipIndex_ < 0) {
        return;
    }
    undoSnapshot_.reset();
    processor_.readClipSingingEdit(trackId_, clipIndex_, [this](const SingingEditDocument& doc) {
        undoSnapshot_ = std::make_shared<SingingEditDocument>(doc);
    });
    if (undoSnapshot_ == nullptr) {
        return;
    }

    const float mx = static_cast<float>(e.x);
    const auto& segs = undoSnapshot_->getSegments().getSegments();
    const float hit = 6.0f;
    dragging_ = false;
    for (const auto& s : segs) {
        const float xL = clipLocalSecToX(s.getStartClipSeconds());
        const float xR = clipLocalSecToX(s.getEndClipSeconds());
        if (std::abs(mx - xL) <= hit) {
            dragging_ = true;
            dragIsLeft_ = true;
            dragSegmentId_ = s.id;
            dragStartSec_ = s.getStartClipSeconds();
            break;
        }
        if (std::abs(mx - xR) <= hit) {
            dragging_ = true;
            dragIsLeft_ = false;
            dragSegmentId_ = s.id;
            dragStartSec_ = s.getEndClipSeconds();
            break;
        }
    }
}

void VocalTimelineStrip::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_ || trackId_ < 0 || clipIndex_ < 0) {
        return;
    }
    juce::String err;
    const double sec = xToClipLocalSec(static_cast<float>(e.x));
    if (!processor_.moveVocalSegmentBoundary(trackId_, clipIndex_, dragSegmentId_, dragIsLeft_, sec, err)) {
        juce::ignoreUnused(err);
    }
    repaint();
    pianoRoll_.repaint();
}

void VocalTimelineStrip::mouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    if (!dragging_ || undoSnapshot_ == nullptr || trackId_ < 0 || clipIndex_ < 0) {
        dragging_ = false;
        return;
    }
    dragging_ = false;
    const uint64_t clipId = processor_.getClipId(trackId_, clipIndex_);
    std::shared_ptr<SingingEditDocument> after = processor_.cloneClipSingingEdit(trackId_, clipIndex_);
    if (after != nullptr) {
        undoManager_.addAction(std::make_unique<SingingEditDocumentChangeAction>(
            processor_, trackId_, clipId, undoSnapshot_, after, "Vocal segment boundary"));
    }
    undoSnapshot_.reset();

    pianoRoll_.flushWorkingNotesToProcessor();

    juce::String err;
    if (!processor_.isRefinePendingForClip(trackId_, clipIndex_)
        && !processor_.refinePhonemeDurationsForClip(trackId_, clipIndex_, err)) {
        juce::ignoreUnused(err);
    }
    repaint();
}

void VocalTimelineStrip::timerCallback()
{
    if (trackId_ >= 0 && clipIndex_ >= 0) {
        repaint();
    }
}

} // namespace OpenTune
