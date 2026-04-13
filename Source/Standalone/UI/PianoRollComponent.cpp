#include "PianoRollComponent.h"
#include "PianoRoll/PianoRollUndoSupport.h"
#include "../Utils/AppLogger.h"
#include <algorithm>
#include <cmath>
#include "../Utils/NoteGenerator.h"
#include "../Utils/SimdPerceptualPitchEstimator.h"
#include "../Utils/ZoomSensitivityConfig.h"
#include "../../PluginProcessor.h"
#include "FrameScheduler.h"
#include "UiText.h"
#include "ToolbarIcons.h"
#include "PianoRoll/PianoRollToolHints.h"
#include "ThemeTokens.h"

namespace {

constexpr int kRightToolMenuLongPressMs = 200;

} // namespace

namespace OpenTune {

class PianoRollToolIconButton : public juce::Button
{
public:
    explicit PianoRollToolIconButton(ToolId toolId)
        : juce::Button("PianoRollTool"), toolId_(toolId)
    {
        setClickingTogglesState(true);
    }

    void setIcon(const juce::Path& path, bool fillIcon)
    {
        iconPath_ = path;
        fillIcon_ = fillIcon;
    }

    void setTextLabel(const juce::String& labelText)
    {
        textLabel_ = labelText;
    }

    ToolId getToolId() const { return toolId_; }

    std::function<void(ToolId, const juce::Rectangle<int>&)> onHoverEnter;
    std::function<void(ToolId)> onHoverExit;

    void mouseEnter(const juce::MouseEvent& e) override
    {
        juce::Button::mouseEnter(e);
        if (onHoverEnter)
            onHoverEnter(toolId_, getBounds());
    }

    void mouseExit(const juce::MouseEvent& e) override
    {
        juce::Button::mouseExit(e);
        if (onHoverExit)
            onHoverExit(toolId_);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(0.75f);
        const bool selected = getToggleState();

        juce::Colour bg = selected ? UIColors::accent.withAlpha(0.30f) : UIColors::backgroundDark.withAlpha(0.70f);
        juce::Colour border = selected ? UIColors::accent.withAlpha(0.95f) : UIColors::panelBorder.withAlpha(0.75f);
        juce::Colour icon = selected ? juce::Colours::white : UIColors::textSecondary.brighter(0.15f);

        if (shouldDrawButtonAsHighlighted)
        {
            bg = bg.brighter(selected ? 0.05f : 0.16f);
            border = border.brighter(0.08f);
            icon = icon.brighter(0.12f);
        }

        if (shouldDrawButtonAsDown)
            bg = bg.darker(0.12f);

        constexpr float corner = 4.5f;
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(border);
        g.drawRoundedRectangle(bounds, corner, selected ? 1.4f : 1.0f);

        if (textLabel_.isNotEmpty())
        {
            const float fontHeight = juce::jlimit(9.0f, 18.0f, bounds.getHeight() * 0.45f);
            g.setColour(icon);
            g.setFont(UIColors::getLabelFont(fontHeight));
            g.drawText(textLabel_, bounds.toNearestInt(), juce::Justification::centred);
        }
        else
        {
            auto iconArea = bounds.reduced(4.5f);
            ToolbarIcons::drawIcon(g, iconPath_, iconArea, icon, 1.6f, fillIcon_);
        }
    }

private:
    ToolId toolId_ = ToolId::Select;
    juce::Path iconPath_;
    bool fillIcon_ = false;
    juce::String textLabel_;
};

namespace {
ScaleMode scaleModeFromTransportScaleType(int scaleType) noexcept
{
    switch (scaleType) {
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

void PianoRollComponent::initializeUIComponents() {
    setWantsKeyboardFocus(true);
    addAndMakeVisible(horizontalScrollBar_);
    addAndMakeVisible(verticalScrollBar_);
    horizontalScrollBar_.addListener(this);
    verticalScrollBar_.addListener(this);
    horizontalScrollBar_.setAutoHide(false);
    verticalScrollBar_.setAutoHide(false);
    horizontalScrollBar_.onThumbResizeRequested = [this](double thumbStartNormalized, double thumbEndNormalized) {
        applyScrollBarThumbResize(thumbStartNormalized, thumbEndNormalized);
    };
    verticalScrollBar_.onThumbResizeRequested = [this](double thumbStartNormalized, double thumbEndNormalized) {
        applyVerticalScrollBarThumbResize(thumbStartNormalized, thumbEndNormalized);
    };

    scrollModeToggleButton_.setButtonText(scrollMode_ == ScrollMode::Continuous ? "Cont" : "Page");
    scrollModeToggleButton_.setFontHeight(11.0f);
    scrollModeToggleButton_.onClick = [this] {
        if (scrollMode_ == ScrollMode::Page) {
            scrollMode_ = ScrollMode::Continuous;
            scrollModeToggleButton_.setButtonText("Cont");
        } else {
            scrollMode_ = ScrollMode::Page;
            scrollModeToggleButton_.setButtonText("Page");
        }
        updateAutoScroll();
    };
    addAndMakeVisible(scrollModeToggleButton_);

    timeUnitToggleButton_.setButtonText("Time");
    timeUnitToggleButton_.setFontHeight(11.0f);
    timeUnitToggleButton_.onClick = [this] {
        if (timeUnit_ == TimeUnit::Seconds) {
            timeUnit_ = TimeUnit::Bars;
            timeUnitToggleButton_.setButtonText("BPM");
        } else {
            timeUnit_ = TimeUnit::Seconds;
            timeUnitToggleButton_.setButtonText("Time");
        }
        repaint();
    };
    addAndMakeVisible(timeUnitToggleButton_);

    initializeToolButtons();

    addAndMakeVisible(playheadOverlay_);
    playheadOverlay_.setPianoKeyWidth(pianoKeyWidth_);

    anchorFitOverlay_.setVisible(false);
    addChildComponent(anchorFitOverlay_);

    scrollVBlankAttachment_ = std::make_unique<juce::VBlankAttachment>(
        this, [this](double timestampSec) { onScrollVBlankCallback(timestampSec); });
}

void PianoRollComponent::initializeToolButtons()
{
    autoTuneToolButton_ = std::make_unique<PianoRollToolIconButton>(ToolId::AutoTune);
    selectToolButton_ = std::make_unique<PianoRollToolIconButton>(ToolId::Select);
    drawNoteToolButton_ = std::make_unique<PianoRollToolIconButton>(ToolId::DrawNote);
    lineAnchorToolButton_ = std::make_unique<PianoRollToolIconButton>(ToolId::LineAnchor);
    handDrawToolButton_ = std::make_unique<PianoRollToolIconButton>(ToolId::HandDraw);
    splitNoteToolButton_ = std::make_unique<PianoRollToolIconButton>(ToolId::SplitNote);

    autoTuneToolButton_->setTextLabel("AUTO");
    selectToolButton_->setIcon(ToolbarIcons::getSelectIcon(), false);
    drawNoteToolButton_->setIcon(ToolbarIcons::getDrawNoteIcon(), false);
    lineAnchorToolButton_->setIcon(ToolbarIcons::getLineAnchorIcon(), false);
    handDrawToolButton_->setIcon(ToolbarIcons::getHandDrawIcon(), false);
    splitNoteToolButton_->setIcon(ToolbarIcons::getCutIcon(), false);

    const int radioGroup = 2201;
    autoTuneToolButton_->setRadioGroupId(radioGroup);
    selectToolButton_->setRadioGroupId(radioGroup);
    drawNoteToolButton_->setRadioGroupId(radioGroup);
    lineAnchorToolButton_->setRadioGroupId(radioGroup);
    handDrawToolButton_->setRadioGroupId(radioGroup);
    splitNoteToolButton_->setRadioGroupId(radioGroup);

    autoTuneToolButton_->onClick = [this] { handleToolButtonClicked(ToolId::AutoTune); };
    selectToolButton_->onClick = [this] { handleToolButtonClicked(ToolId::Select); };
    drawNoteToolButton_->onClick = [this] { handleToolButtonClicked(ToolId::DrawNote); };
    lineAnchorToolButton_->onClick = [this] { handleToolButtonClicked(ToolId::LineAnchor); };
    handDrawToolButton_->onClick = [this] { handleToolButtonClicked(ToolId::HandDraw); };
    splitNoteToolButton_->onClick = [this] { handleToolButtonClicked(ToolId::SplitNote); };

    auto wireHoverCallbacks = [this](PianoRollToolIconButton* button) {
        button->onHoverEnter = [this](ToolId tool, const juce::Rectangle<int>& bounds) {
            setHoveredToolButton(tool, bounds);
        };
        button->onHoverExit = [this](ToolId tool) {
            clearHoveredToolButton(tool);
        };
    };

    wireHoverCallbacks(autoTuneToolButton_.get());
    wireHoverCallbacks(selectToolButton_.get());
    wireHoverCallbacks(drawNoteToolButton_.get());
    wireHoverCallbacks(lineAnchorToolButton_.get());
    wireHoverCallbacks(handDrawToolButton_.get());
    wireHoverCallbacks(splitNoteToolButton_.get());

    addAndMakeVisible(*autoTuneToolButton_);
    addAndMakeVisible(*selectToolButton_);
    addAndMakeVisible(*drawNoteToolButton_);
    addAndMakeVisible(*lineAnchorToolButton_);
    addAndMakeVisible(*handDrawToolButton_);
    addAndMakeVisible(*splitNoteToolButton_);

    refreshToolButtonTooltips();
    updateToolButtonStates();
}

void PianoRollComponent::layoutToolButtons()
{
    if (!autoTuneToolButton_ || !selectToolButton_ || !drawNoteToolButton_
        || !lineAnchorToolButton_ || !handDrawToolButton_ || !splitNoteToolButton_)
    {
        return;
    }

    auto contentBounds = getLocalBounds().reduced(12);
    contentBounds.removeFromBottom(15);
    contentBounds.removeFromRight(15);

    const float componentScale = juce::jlimit(1.0f, 2.25f,
        juce::Component::getApproximateScaleFactorForComponent(this));

    int buttonSize = juce::roundToInt(26.0f * componentScale);
    int gap = juce::roundToInt(4.0f * componentScale);
    int autoButtonWidth = juce::roundToInt(buttonSize * 1.75f);

    const int x0 = contentBounds.getX() + pianoKeyWidth_ + juce::roundToInt(8.0f * componentScale);
    const int y = contentBounds.getY() + juce::roundToInt(4.0f * componentScale);
    const int rightLimit = juce::jmax(x0, timeUnitToggleButton_.getX() - juce::roundToInt(8.0f * componentScale));

    auto computeTotalWidth = [](int iconSize, int spacing, int autoWidth) {
        return 5 * iconSize + autoWidth + 5 * spacing;
    };

    int totalWidth = computeTotalWidth(buttonSize, gap, autoButtonWidth);
    if (totalWidth > (rightLimit - x0))
    {
        const float shrinkRatio = static_cast<float>(rightLimit - x0) / static_cast<float>(juce::jmax(1, totalWidth));
        const float clampedShrink = juce::jlimit(0.80f, 1.0f, shrinkRatio);
        buttonSize = juce::roundToInt(static_cast<float>(buttonSize) * clampedShrink);
        gap = juce::roundToInt(static_cast<float>(gap) * clampedShrink);
        autoButtonWidth = juce::roundToInt(static_cast<float>(autoButtonWidth) * clampedShrink);
        totalWidth = computeTotalWidth(buttonSize, gap, autoButtonWidth);
    }

    int x = x0;
    selectToolButton_->setBounds(x, y, buttonSize, buttonSize);
    x += buttonSize + gap;
    drawNoteToolButton_->setBounds(x, y, buttonSize, buttonSize);
    x += buttonSize + gap;
    lineAnchorToolButton_->setBounds(x, y, buttonSize, buttonSize);
    x += buttonSize + gap;
    handDrawToolButton_->setBounds(x, y, buttonSize, buttonSize);
    x += buttonSize + gap;
    splitNoteToolButton_->setBounds(x, y, buttonSize, buttonSize);
    x += buttonSize + gap;
    autoTuneToolButton_->setBounds(x, y, autoButtonWidth, buttonSize);

    if (hasHoveredToolButton_)
    {
        std::vector<PianoRollToolIconButton*> buttons {
            selectToolButton_.get(),
            drawNoteToolButton_.get(),
            lineAnchorToolButton_.get(),
            handDrawToolButton_.get(),
            splitNoteToolButton_.get(),
            autoTuneToolButton_.get()
        };

        for (auto* button : buttons)
        {
            if (button->getToolId() == hoveredToolId_)
            {
                hoveredToolButtonBounds_ = button->getBounds();
                break;
            }
        }
    }
}

void PianoRollComponent::updateToolButtonStates()
{
    if (!autoTuneToolButton_ || !selectToolButton_ || !drawNoteToolButton_
        || !lineAnchorToolButton_ || !handDrawToolButton_ || !splitNoteToolButton_)
    {
        return;
    }

    autoTuneToolButton_->setToggleState(currentTool_ == ToolId::AutoTune, juce::dontSendNotification);
    selectToolButton_->setToggleState(currentTool_ == ToolId::Select, juce::dontSendNotification);
    drawNoteToolButton_->setToggleState(currentTool_ == ToolId::DrawNote, juce::dontSendNotification);
    lineAnchorToolButton_->setToggleState(currentTool_ == ToolId::LineAnchor, juce::dontSendNotification);
    handDrawToolButton_->setToggleState(currentTool_ == ToolId::HandDraw, juce::dontSendNotification);
    splitNoteToolButton_->setToggleState(currentTool_ == ToolId::SplitNote, juce::dontSendNotification);
}

juce::String PianoRollComponent::getToolDisplayName(ToolId tool) const
{
    switch (tool)
    {
        case ToolId::AutoTune: return LOC(kAuto);
        case ToolId::Select: return LOC(kSelect);
        case ToolId::DrawNote: return LOC(kDrawNotes);
        case ToolId::LineAnchor: return LOC(kLineAnchor);
        case ToolId::HandDraw: return LOC(kHandDraw);
        case ToolId::SplitNote: return LOC(kSplitNote);
        default: return {};
    }
}

void PianoRollComponent::refreshToolButtonTooltips()
{
    if (autoTuneToolButton_)
        autoTuneToolButton_->setTooltip(getToolDisplayName(ToolId::AutoTune));
    if (selectToolButton_)
        selectToolButton_->setTooltip(getToolDisplayName(ToolId::Select));
    if (drawNoteToolButton_)
        drawNoteToolButton_->setTooltip(getToolDisplayName(ToolId::DrawNote));
    if (lineAnchorToolButton_)
        lineAnchorToolButton_->setTooltip(getToolDisplayName(ToolId::LineAnchor));
    if (handDrawToolButton_)
        handDrawToolButton_->setTooltip(getToolDisplayName(ToolId::HandDraw));
    if (splitNoteToolButton_)
        splitNoteToolButton_->setTooltip(getToolDisplayName(ToolId::SplitNote));
}

void PianoRollComponent::handleToolButtonClicked(ToolId tool)
{
    if (externalToolSelectionHandler_)
    {
        externalToolSelectionHandler_(static_cast<int>(tool));
        return;
    }

    if (tool == ToolId::AutoTune)
    {
        listeners_.call([](Listener& l) { l.autoTuneRequested(); });
        setCurrentTool(ToolId::Select);
        return;
    }

    setCurrentTool(tool);
}

void PianoRollComponent::setHoveredToolButton(ToolId tool, const juce::Rectangle<int>& buttonBounds)
{
    hoveredToolId_ = tool;
    hoveredToolName_ = getToolDisplayName(tool);
    hoveredToolButtonBounds_ = buttonBounds;
    hasHoveredToolButton_ = hoveredToolName_.isNotEmpty();
    repaint();
}

void PianoRollComponent::clearHoveredToolButton(ToolId tool)
{
    if (!hasHoveredToolButton_ || hoveredToolId_ != tool)
        return;

    hasHoveredToolButton_ = false;
    hoveredToolName_.clear();
    hoveredToolButtonBounds_ = {};
    repaint();
}

void PianoRollComponent::initializeUndoSupport() {
    PianoRollUndoSupport::Context undoCtx;
    undoCtx.getNotesCopy = [this]() { return getCurrentClipNotesCopy(); };
    undoCtx.getPitchCurve = [this]() { return currentCurve_; };
    undoCtx.getCurrentClipId = [this]() { return currentClipId_; };
    undoCtx.getCurrentTrackId = [this]() { return currentTrackId_; };
    undoCtx.getProcessor = [this]() { return processor_; };
    undoCtx.getUndoManager = [this]() { return globalUndoManager_; };
    undoSupport_ = std::make_unique<PianoRollUndoSupport>(std::move(undoCtx));
}

void PianoRollComponent::initializeRenderer() {
    renderer_ = std::make_unique<PianoRollRenderer>();
}

void PianoRollComponent::initializeCorrectionWorker() {
    correctionWorker_ = std::make_unique<PianoRollCorrectionWorker>();
}

PianoRollToolHandler::Context PianoRollComponent::buildToolHandlerContext() {
    PianoRollToolHandler::Context toolCtx;
    toolCtx.getState = [this]() -> InteractionState& { return interactionState_; };

    toolCtx.xToTime = [this](int x) { return xToTime(x); };
    toolCtx.timeToX = [this](double seconds) { return timeToX(seconds); };
    toolCtx.yToFreq = [this](float y) { return yToFreq(y); };
    toolCtx.freqToY = [this](float f) { return freqToY(f); };

    toolCtx.getNotes = [this]() -> std::vector<Note>& { return getCurrentClipNotes(); };
    toolCtx.getSelectedNotes = [this]() -> std::vector<Note*> {
        std::vector<Note*> selected;
        auto& notes = getCurrentClipNotes();
        for (auto& n : notes) {
            if (n.selected) selected.push_back(&n);
        }
        return selected;
    };
    toolCtx.findNoteAt = [this](double time, float targetPitchHz, float pitchToleranceHz) -> Note* {
        juce::ignoreUnused(pitchToleranceHz);
        if (targetPitchHz <= 0.0f)
            return nullptr;
        const float clickMidiRow = freqToMidi(targetPitchHz);
        auto& notes = getCurrentClipNotes();
        for (auto& note : notes) {
            if (time < note.startTime || time >= note.endTime)
                continue;
            const float adjustedPitch = note.getAdjustedPitch();
            if (adjustedPitch <= 0.0f)
                continue;
            const float noteMidiRow = freqToMidi(adjustedPitch);
            if (std::abs(noteMidiRow - clickMidiRow) <= kPianoRollNoteHitHalfWidthSemis)
                return &note;
        }
        return nullptr;
    };
    toolCtx.deselectAllNotes = [this]() {
        auto& notes = getCurrentClipNotes();
        for (auto& n : notes) n.selected = false;
    };
    toolCtx.selectAllNotes = [this]() {
        auto& notes = getCurrentClipNotes();
        for (auto& n : notes) n.selected = true;
    };
    toolCtx.insertNoteSorted = [this](const Note& note) {
        auto& notes = getCurrentClipNotes();
        notes.push_back(note);
        std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
            return a.startTime < b.startTime;
        });
    };
    toolCtx.getPitchCurve = [this]() { return currentCurve_; };
    toolCtx.getCurveSize = [this]() -> int {
        if (!currentCurve_) return 0;
        auto snap = currentCurve_->getSnapshot();
        return snap ? static_cast<int>(snap->size()) : 0;
    };
    toolCtx.getCurveHopSize = [this]() { return hopSize_; };
    toolCtx.getCurveSampleRate = [this]() { return f0SampleRate_; };
    toolCtx.clearCorrectionRange = [this](int s, int e) {
        if (!currentCurve_) return;
        currentCurve_->clearCorrectionRange(s, e);
    };
    toolCtx.clearAllCorrections = [this]() {
        if (!currentCurve_) return;
        currentCurve_->clearAllCorrections();
    };
    toolCtx.restoreCorrectedSegment = [this](const CorrectedSegment& seg) {
        if (!currentCurve_) return;
        currentCurve_->restoreCorrectedSegment(seg);
    };
    toolCtx.getOriginalF0 = [this]() -> std::vector<float> {
        if (!currentCurve_) return {};
        auto snap = currentCurve_->getSnapshot();
        if (!snap) return {};
        return snap->getOriginalF0();
    };
    toolCtx.getMinMidi = [this]() { return minMidi_; };
    toolCtx.getMaxMidi = [this]() { return maxMidi_; };
    toolCtx.getRetuneSpeed = [this]() { return currentRetuneSpeed_; };
    toolCtx.getVibratoDepth = [this]() { return currentVibratoDepth_; };
    toolCtx.getVibratoRate = [this]() { return currentVibratoRate_; };
    toolCtx.recalculatePIP = [this](Note& note) -> float { return recalculatePIP(note); };
    toolCtx.setCurrentTool = [this](ToolId tool) { setCurrentTool(tool); };
    toolCtx.beginRightToolMenuLongPress = [this](juce::Point<int> p) { beginRightToolMenuLongPress(p); };
    toolCtx.notifyAutoTuneRequested = [this]() { listeners_.call([](Listener& l) { l.autoTuneRequested(); }); };
    toolCtx.notifyPlayPauseToggle = [this]() { listeners_.call([](Listener& l) { l.playPauseToggleRequested(); }); };
    toolCtx.notifyStopPlayback = [this]() { listeners_.call([](Listener& l) { l.stopPlaybackRequested(); }); };
    toolCtx.notifyEscapeKey = [this]() { listeners_.call([](Listener& l) { l.escapeKeyPressed(); }); };
    toolCtx.notifyNoteOffsetChanged = [this](size_t noteIndex, float oldOffset, float newOffset) {
        listeners_.call([noteIndex, oldOffset, newOffset](Listener& l) { l.noteOffsetChanged(noteIndex, oldOffset, newOffset); });
    };
    toolCtx.enqueueNoteBasedCorrection = [this](int startFrame,
                                                int endFrameExclusive,
                                                float retuneSpeed,
                                                float vibratoDepth,
                                                float vibratoRate) {
        enqueueNoteBasedCorrectionAsync(
            startFrame,
            endFrameExclusive,
            retuneSpeed,
            vibratoDepth,
            vibratoRate);
    };
    toolCtx.getPianoKeyWidth = [this]() { return pianoKeyWidth_; };
    toolCtx.getTrackOffsetSeconds = [this]() { return trackOffsetSeconds_; };
    toolCtx.getAudioSampleRate = [this]() { return PianoRollComponent::kAudioSampleRate; };
    toolCtx.getAudioBuffer = [this]() -> const juce::AudioBuffer<float>* { return audioBuffer_ ? audioBuffer_.get() : nullptr; };

    toolCtx.getDirtyStartTime = [this]() { return interactionState_.drawing.dirtyStartTime; };
    toolCtx.setDirtyStartTime = [this](double v) { interactionState_.drawing.dirtyStartTime = v; };
    toolCtx.getDirtyEndTime = [this]() { return interactionState_.drawing.dirtyEndTime; };
    toolCtx.setDirtyEndTime = [this](double v) { interactionState_.drawing.dirtyEndTime = v; };

    toolCtx.getDrawingNoteStartTime = [this]() { return interactionState_.drawing.drawingNoteStartTime; };
    toolCtx.setDrawingNoteStartTime = [this](double v) { interactionState_.drawing.drawingNoteStartTime = v; };
    toolCtx.getDrawingNoteEndTime = [this]() { return interactionState_.drawing.drawingNoteEndTime; };
    toolCtx.setDrawingNoteEndTime = [this](double v) { interactionState_.drawing.drawingNoteEndTime = v; };
    toolCtx.getDrawingNotePitch = [this]() { return interactionState_.drawing.drawingNotePitch; };
    toolCtx.setDrawingNotePitch = [this](float v) { interactionState_.drawing.drawingNotePitch = v; };
    toolCtx.getDrawingNoteIndex = [this]() { return interactionState_.drawing.drawingNoteIndex; };
    toolCtx.setDrawingNoteIndex = [this](int v) { interactionState_.drawing.drawingNoteIndex = v; };

    toolCtx.getDrawNoteToolPendingDrag = [this]() { return interactionState_.drawNoteToolPendingDrag; };
    toolCtx.setDrawNoteToolPendingDrag = [this](bool v) { interactionState_.drawNoteToolPendingDrag = v; };
    toolCtx.getDrawNoteToolMouseDownPos = [this]() { return interactionState_.drawNoteToolMouseDownPos; };
    toolCtx.setDrawNoteToolMouseDownPos = [this](juce::Point<int> v) { interactionState_.drawNoteToolMouseDownPos = v; };
    toolCtx.getDragThreshold = [this]() { return dragThreshold_; };

    toolCtx.getNoteDragManualStartTime = [this]() { return interactionState_.noteDrag.manualStartTime; };
    toolCtx.setNoteDragManualStartTime = [this](double v) { interactionState_.noteDrag.manualStartTime = v; };
    toolCtx.getNoteDragManualEndTime = [this]() { return interactionState_.noteDrag.manualEndTime; };
    toolCtx.setNoteDragManualEndTime = [this](double v) { interactionState_.noteDrag.manualEndTime = v; };
    toolCtx.getNoteDragInitialManualTargets = [this]() -> std::vector<std::pair<double, float>>& { return interactionState_.noteDrag.initialManualTargets; };

    toolCtx.requestRepaint = [this]() { requestInteractiveRepaint(); };
    toolCtx.setMouseCursor = [this](const juce::MouseCursor& c) { setMouseCursor(c); };
    toolCtx.grabKeyboardFocus = [this]() { grabKeyboardFocus(); };
    toolCtx.notifyPlayheadChange = [this](double time) {
        listeners_.call([time](Listener& l) { l.playheadPositionChangeRequested(time); });
        if (!isPlaying_.load(std::memory_order_relaxed)) {
            playheadOverlay_.setPlayheadSeconds(time);
            playheadOverlay_.repaint();
        }
    };
    toolCtx.notifyPitchCurveEdited = [this](int s, int e) {
        listeners_.call([s, e](Listener& l) { l.pitchCurveEdited(s, e); });
    };
    toolCtx.beginEditTransaction = [this](const juce::String& name) {
        undoSupport_->beginTransaction(name);
    };
    toolCtx.commitEditTransaction = [this]() {
        undoSupport_->commitTransaction();
    };
    toolCtx.isTransactionActive = [this]() { return undoSupport_->isTransactionActive(); };
    toolCtx.applyManualCorrection = [this](std::vector<PianoRollToolHandler::ManualCorrectionOp> ops, int s, int e, bool render) {
        enqueueManualCorrectionPatchAsync(ops, s, e, render);
    };
    toolCtx.setPitchPreview = [this](bool active, float hz) {
        if (processor_ != nullptr) processor_->setPitchPreview(active, hz);
    };
    return toolCtx;
}

void PianoRollComponent::initializeToolHandler() {
    toolHandler_ = std::make_unique<PianoRollToolHandler>(buildToolHandlerContext());
}

PianoRollComponent::PianoRollComponent() {
    LocalizationManager::getInstance().addListener(this);
    initializeUIComponents();
    initializeUndoSupport();
    initializeRenderer();
    initializeCorrectionWorker();
    initializeToolHandler();
}

PianoRollComponent::~PianoRollComponent() {
    LocalizationManager::getInstance().removeListener(this);
    if (correctionWorker_) {
        correctionWorker_->stop();
    }
    scrollVBlankAttachment_.reset();
    stopTimer();
    horizontalScrollBar_.removeListener(this);
    verticalScrollBar_.removeListener(this);
}

void PianoRollComponent::languageChanged(Language newLanguage)
{
    juce::ignoreUnused(newLanguage);
    refreshToolButtonTooltips();
    if (hasHoveredToolButton_)
        hoveredToolName_ = getToolDisplayName(hoveredToolId_);
    repaint();
}


void PianoRollComponent::resized() {
    auto bounds = getLocalBounds().reduced(12);

    // Reserve space for scrollbars
    horizontalScrollBar_.setBounds(bounds.removeFromBottom(15));
    verticalScrollBar_.setBounds(bounds.removeFromRight(15));

    updateScrollBars();

    // Position toggle buttons in top right of ruler
    int btnW = 50;
    int btnH = 20;
    int spacing = 5;
    int currentX = bounds.getRight() - spacing - btnW;
    const int buttonY = bounds.getY() + 5;
    
    scrollModeToggleButton_.setBounds(currentX, buttonY, btnW, btnH);
    currentX -= (btnW + spacing);
    timeUnitToggleButton_.setBounds(currentX, buttonY, btnW, btnH);
    layoutToolButtons();

    playheadOverlay_.setBounds(getLocalBounds());
    anchorFitOverlay_.setBounds(getLocalBounds());
}

void PianoRollComponent::setPitchCurve(std::shared_ptr<PitchCurve> curve) {
    currentCurve_ = curve;
    if (renderer_) {
        renderer_->clearCorrectedF0Cache();
    }

    // Deselect all notes when pitch curve changes
    interactionState_.selection.isSelectingArea = false;
    interactionState_.selection.marqueeAdditive = false;
    interactionState_.selection.marqueeBaseSelected.clear();
    interactionState_.selection.clearF0Selection();
    for (auto& n : getCurrentClipNotes()) {
        n.selected = false;
    }

    if (curve) {
        int curveHopSize = curve->getHopSize();
        if (curveHopSize > 0) {
            setHopSize(curveHopSize);
        }

        double curveSampleRate = curve->getSampleRate();
        if (curveSampleRate > 0.0) {
            setF0SampleRate(curveSampleRate);
        }
    } else {
    }
    
    snapNextScroll_ = true;
    repaint();
}

void PianoRollComponent::setAudioBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buffer, int sampleRate) {
    audioBuffer_ = buffer;
    
    timeConverter_.setContext(bpm_, timeSigNum_, timeSigDenom_);
    timeConverter_.setZoom(zoomLevel_);
    timeConverter_.setScrollOffset(scrollOffset_);
    
    playheadOverlay_.setZoomLevel(zoomLevel_);
    playheadOverlay_.setScrollOffset(static_cast<double>(scrollOffset_));

    waveformMipmap_.setAudioSource(buffer);
    renderer_->setWaveformMipmap(&waveformMipmap_);

    snapNextScroll_ = true;
    updateScrollBars();
    repaint();
}


void PianoRollComponent::setShowWaveform(bool shouldShow) {
    showWaveform_ = shouldShow;
    repaint();
}

void PianoRollComponent::setShowLanes(bool shouldShow) {
    showLanes_ = shouldShow;
    repaint();
}

void PianoRollComponent::setBpm(double bpm) {
    bpm_ = juce::jlimit(60.0, 240.0, bpm);
    timeConverter_.setContext(bpm_, timeSigNum_, timeSigDenom_);
    timeConverter_.setZoom(zoomLevel_);
    timeConverter_.setScrollOffset(scrollOffset_);
    repaint();
}

void PianoRollComponent::setTimeSignature(int numerator, int denominator) {
    if (numerator <= 0 || denominator <= 0) {
        return;
    }

    timeSigNum_ = numerator;
    timeSigDenom_ = denominator;
    timeConverter_.setContext(bpm_, timeSigNum_, timeSigDenom_);
    timeConverter_.setZoom(zoomLevel_);
    timeConverter_.setScrollOffset(scrollOffset_);
    repaint();
}

void PianoRollComponent::setTimeUnit(TimeUnit unit) {
    timeUnit_ = unit;
    repaint();
}

void PianoRollComponent::addListener(Listener* listener) {
    listeners_.add(listener);
}

void PianoRollComponent::removeListener(Listener* listener) {
    listeners_.remove(listener);
}

void PianoRollComponent::setHasUserAudio(bool hasAudio) {
    hasUserAudio_ = hasAudio;
    if (hasUserAudio_) {
        fitToScreen();
    }
    repaint();
}

void PianoRollComponent::setScale(int rootNote, int scaleType)
{
    scaleRootNote_ = juce::jlimit(0, 11, rootNote);
    scaleType_ = juce::jlimit(1, 8, scaleType);
    repaint();
}

void PianoRollComponent::setNoteNameDisplayMode(int mode)
{
    noteNameDisplayMode_ = juce::jlimit(0, 2, mode);
    repaint();
}

void PianoRollComponent::setShowNoteBlockNoteNames(bool show)
{
    showNoteBlockNoteNames_ = show;
    repaint();
}

void PianoRollComponent::fitToScreen() {
    // 如果用户已手动调整过缩放，不自动覆盖
    if (userHasManuallyZoomed_) {
        return;
    }

    // 1. Vertical Fit: Show C1 to C8 (minMidi_ to maxMidi_)
    // Total range: maxMidi_ - minMidi_
    // Available height: getHeight()
    float range = maxMidi_ - minMidi_;
    const int hAvail = getNoteGridViewportHeight();
    if (range > 0 && hAvail > 0) {
        pixelsPerSemitone_ = static_cast<float>(hAvail) / range;

        // Reset scroll to show top
        verticalScrollOffset_ = 0;
    }

    // 2. Horizontal Fit:
    // If has audio: fit audio length
    // If no audio: fit 16 seconds
    double duration = 16.0;
    if (hasUserAudio_ && audioBuffer_ && PianoRollComponent::kAudioSampleRate > 0) {
        duration = static_cast<double>(audioBuffer_->getNumSamples()) / PianoRollComponent::kAudioSampleRate;
    }
    
    // Available width: getWidth() - pianoKeyWidth_
    int viewWidth = getWidth() - pianoKeyWidth_;
    if (viewWidth > 0 && duration > 0) {
        // pixelsPerSecond * duration = viewWidth
        // pixelsPerSecond = viewWidth / duration
        double pixelsPerSecond = static_cast<double>(viewWidth) / duration;
        
        // zoomLevel = pixelsPerSecond / 100.0 (base scale)
        zoomLevel_ = pixelsPerSecond / 100.0;
        timeConverter_.setZoom(zoomLevel_);
        // 同步 zoomLevel 到 playheadOverlay
        playheadOverlay_.setZoomLevel(zoomLevel_);
    }

    if (hasUserAudio_ && audioBuffer_ && PianoRollComponent::kAudioSampleRate > 0) {
        int newScroll = (int) std::llround(trackOffsetSeconds_ * 100.0 * zoomLevel_);
        setScrollOffset(newScroll);
    } else {
        setScrollOffset(0);
    }
    
    repaint();
}

// HachiTune-style MIDI-based coordinate conversion
float PianoRollComponent::midiToY(float midiNote) const {
    return (maxMidi_ - midiNote) * pixelsPerSemitone_ - verticalScrollOffset_;
}

float PianoRollComponent::yToMidi(float y) const {
    return maxMidi_ - ((y + verticalScrollOffset_) / pixelsPerSemitone_);
}

float PianoRollComponent::getTotalHeight() const {
    return (maxMidi_ - minMidi_) * pixelsPerSemitone_;
}

float PianoRollComponent::getMinimumVerticalZoom() const noexcept
{
    const float midiRange = maxMidi_ - minMidi_;
    if (midiRange <= 0.0f)
        return 5.0f;

    const float viewportHeight = static_cast<float>(getNoteGridViewportHeight());
    const float fitZoom = viewportHeight / midiRange;
    return juce::jlimit(5.0f, 120.0f, fitZoom);
}

int PianoRollComponent::getNoteGridViewportHeight() const noexcept
{
    constexpr int kInset = 12;
    constexpr int kHScrollbar = 15;
    return juce::jmax(1, getHeight() - 2 * kInset - rulerHeight_ - kHScrollbar);
}

float PianoRollComponent::freqToMidi(float frequency) const {
    if (frequency <= 0.0f) return 0.0f;
    // 统一语义：频率↔MIDI 以“半音中心线”为锚点（不是键边界）。
    return 12.0f * std::log2(frequency / 440.0f) + 69.0f - 0.5f;
}

float PianoRollComponent::midiToFreq(float midiNote) const {
    // 与 freqToMidi 保持严格互逆的中心线锚点约定。
    return 440.0f * std::pow(2.0f, (midiNote + 0.5f - 69.0f) / 12.0f);
}

float PianoRollComponent::yToFreq(float y) const {
    return midiToFreq(yToMidi(y));
}

float PianoRollComponent::freqToY(float freq) const {
    return midiToY(freqToMidi(freq));
}

int PianoRollComponent::timeToX(double seconds) const {
    return timeConverter_.timeToPixel(seconds - trackOffsetSeconds_ + alignmentOffsetSeconds_) + pianoKeyWidth_;
}

double PianoRollComponent::xToTime(int x) const {
    return timeConverter_.pixelToTime(x - pianoKeyWidth_) + trackOffsetSeconds_ - alignmentOffsetSeconds_;
}


void PianoRollComponent::setCurrentClipContext(int trackId, uint64_t clipId)
{
    currentTrackId_ = trackId;
    currentClipId_ = clipId;
    if (correctionWorker_) {
        correctionWorker_->setClipContext(trackId, clipId);
    }
}

void PianoRollComponent::clearClipContext()
{
    currentTrackId_ = -1;
    currentClipId_ = 0;
    correctionInFlight_.store(false, std::memory_order_release);
    if (correctionWorker_) {
        correctionWorker_->setClipContext(-1, 0);
    }
}

std::vector<Note>& PianoRollComponent::getCurrentClipNotes() {
    if (!processor_ || currentTrackId_ < 0 || currentClipId_ == 0) {
        static std::vector<Note> empty;
        return empty;
    }
    int clipIndex = processor_->getClipIndexById(currentTrackId_, currentClipId_);
    if (clipIndex < 0) {
        static std::vector<Note> empty;
        return empty;
    }
    return processor_->getClipNotesRef(currentTrackId_, clipIndex);
}

std::vector<Note> PianoRollComponent::getCurrentClipNotesCopy() const {
    if (!processor_ || currentTrackId_ < 0 || currentClipId_ == 0) {
        return {};
    }
    int clipIndex = processor_->getClipIndexById(currentTrackId_, currentClipId_);
    if (clipIndex < 0) {
        return {};
    }
    return processor_->getClipNotes(currentTrackId_, clipIndex);
}

bool PianoRollComponent::isAutoTuneProcessing() const
{
    return correctionInFlight_.load(std::memory_order_acquire);
}

bool PianoRollComponent::isAnchorFitting() const
{
    return anchorFitting_.load(std::memory_order_acquire);
}


} // namespace OpenTune
