#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "../../Utils/UndoAction.h"

namespace OpenTune {

class OpenTuneAudioProcessor;
class PianoRollComponent;

/**
 * Clip-local vocal segment strip (above piano roll). Drag segment edges; commits undo + optional refine.
 */
class VocalTimelineStrip : public juce::Component, private juce::Timer {
public:
    VocalTimelineStrip(OpenTuneAudioProcessor& processor, PianoRollComponent& pianoRoll, UndoManager& undoManager);

    void setTrackClip(int trackId, int clipIndex);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    double xToClipLocalSec(float x) const;
    float clipLocalSecToX(double sec) const;

    OpenTuneAudioProcessor& processor_;
    PianoRollComponent& pianoRoll_;
    UndoManager& undoManager_;

    int trackId_{-1};
    int clipIndex_{-1};

    bool dragging_{false};
    bool dragIsLeft_{false};
    uint64_t dragSegmentId_{0};
    double dragStartSec_{0.0};
    std::shared_ptr<class SingingEditDocument> undoSnapshot_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalTimelineStrip)
};

} // namespace OpenTune
