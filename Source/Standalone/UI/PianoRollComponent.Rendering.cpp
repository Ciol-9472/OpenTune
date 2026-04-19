#include "PianoRollComponent.h"
#include "../../PluginProcessor.h"
#include "../Utils/AppLogger.h"
#include "../Utils/PitchUtils.h"
#include "PianoRoll/PianoRollToolHints.h"
#include "ThemeTokens.h"

#include <algorithm>
#include <cmath>

namespace OpenTune {

namespace {

PianoRollRenderer::RenderContext renderContextForClipPitchSnapshot(
    const PianoRollRenderer::RenderContext& base,
    std::shared_ptr<const PitchCurveSnapshot> snap)
{
    if (!snap || snap->getHopSize() <= 0 || snap->getSampleRate() <= 1e-15) {
        return base;
    }
    PianoRollRenderer::RenderContext out = base;
    out.hopSize = snap->getHopSize();
    out.f0SampleRate = snap->getSampleRate();
    const int hop = out.hopSize;
    const double sr = out.f0SampleRate;
    out.clipSecondsToFrameIndex = [hop, sr](double seconds) -> double {
        const double fd = static_cast<double>(hop) / sr;
        if (fd <= 1e-15) {
            return 0.0;
        }
        return seconds / fd;
    };
    out.frameIndexToClipSeconds = [hop, sr](int frame) -> double {
        return static_cast<double>(frame) * static_cast<double>(hop) / sr;
    };
    return out;
}

} // namespace

void PianoRollComponent::drawToolHintOverlay(juce::Graphics& g)
{
    const auto lines = getPianoRollToolHintLines(currentTool_);
    if (lines.empty())
        return;

    constexpr int contentInset = 12;
    constexpr int scrollbarH = 15;
    constexpr float lineH = 18.0f;
    const int margin = 10;
    const int left = contentInset + pianoKeyWidth_ + margin;
    const int scrollbarTopY = getHeight() - contentInset - scrollbarH;
    const int bottomY = scrollbarTopY - margin;
    const int textW = juce::jmax(80, getWidth() - left - margin - contentInset);

    juce::Colour textCol = UIColors::textSecondary.withAlpha(0.68f);
    if (Theme::getActiveTheme() == ThemeId::DarkBlueGrey)
        textCol = UIColors::textSecondary.withAlpha(0.78f);

    g.setColour(textCol);
    g.setFont(UIColors::getLabelFont(13.5f));

    int y = bottomY - static_cast<int>(static_cast<float>(lines.size()) * lineH);
    for (const auto& ln : lines)
    {
        g.drawText(ln, left, y, textW, static_cast<int>(lineH), juce::Justification::topLeft, true);
        y += static_cast<int>(lineH);
    }
}

void PianoRollComponent::drawHoveredToolNameOverlay(juce::Graphics& g)
{
    if (!hasHoveredToolButton_ || hoveredToolName_.isEmpty())
        return;

    const juce::Font font = UIColors::getLabelFont(12.0f);
    g.setFont(font);

    constexpr int paddingX = 9;
    constexpr int bubbleHeight = 22;
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, hoveredToolName_, 0.0f, 0.0f);
    const auto textBounds = glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true);
    const int textWidth = static_cast<int>(std::ceil(textBounds.getWidth()));
    const int bubbleWidth = juce::jmax(56, textWidth + paddingX * 2);

    int x = hoveredToolButtonBounds_.getCentreX() - (bubbleWidth / 2);
    int y = hoveredToolButtonBounds_.getBottom() + 6;

    const int minX = pianoKeyWidth_ + 6;
    const int maxX = getWidth() - bubbleWidth - 20;
    if (maxX >= minX)
        x = juce::jlimit(minX, maxX, x);

    if ((y + bubbleHeight) > (getHeight() - 24))
        y = hoveredToolButtonBounds_.getY() - bubbleHeight - 6;

    const auto bubbleBounds = juce::Rectangle<float>(
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(bubbleWidth),
        static_cast<float>(bubbleHeight));

    g.setColour(UIColors::backgroundDark.withAlpha(0.92f));
    g.fillRoundedRectangle(bubbleBounds, 5.0f);
    g.setColour(UIColors::panelBorder.withAlpha(0.92f));
    g.drawRoundedRectangle(bubbleBounds, 5.0f, 1.0f);
    g.setColour(UIColors::textPrimary);
    g.drawText(hoveredToolName_, bubbleBounds.toNearestInt(), juce::Justification::centred, false);
}

void PianoRollComponent::paintOverChildren(juce::Graphics& g)
{
    drawHoveredToolNameOverlay(g);
    drawToolHintOverlay(g);
}

void PianoRollComponent::drawSelectedOriginalF0Curve(juce::Graphics& g, const std::vector<float>& originalF0, double offsetSeconds)
{
    const auto& notes = getCurrentClipNotes();
    bool hasNoteSelection = false;
    for (const auto& note : notes)
    {
        if (note.selected)
        {
            hasNoteSelection = true;
            break;
        }
    }

    if (!hasNoteSelection)
        return;

    const double frameDuration = hopSize_ / f0SampleRate_;
    juce::Path selectedPath;
    bool pathStarted = false;

    for (const auto& note : notes)
    {
        if (!note.selected)
            continue;

        int startFrame = static_cast<int>(note.startTime / frameDuration);
        int endFrame = static_cast<int>(note.endTime / frameDuration);
        startFrame = std::max(0, startFrame);
        endFrame = std::min(static_cast<int>(originalF0.size()) - 1, endFrame);

        if (endFrame <= startFrame)
            continue;

        bool segmentStarted = false;
        for (int i = startFrame; i <= endFrame; ++i)
        {
            float f0 = originalF0[i];
            if (f0 > 0.0f)
            {
                float y = freqToY(f0);
                double timePos = i * frameDuration;
                float x = static_cast<float>(timeToX(timePos + offsetSeconds));

                if (!pathStarted)
                {
                    selectedPath.startNewSubPath(x, y);
                    pathStarted = true;
                    segmentStarted = true;
                }
                else if (!segmentStarted)
                {
                    selectedPath.startNewSubPath(x, y);
                    segmentStarted = true;
                }
                else
                {
                    juce::Point<float> last = selectedPath.getCurrentPosition();
                    if (std::abs(x - last.x) > 50.0f)
                        selectedPath.startNewSubPath(x, y);
                    else
                        selectedPath.lineTo(x, y);
                }
            }
            else
            {
                segmentStarted = false;
            }
        }
    }

    if (!selectedPath.isEmpty())
    {
        g.setColour(UIColors::originalF0.withAlpha(0.85f));
        juce::PathStrokeType strokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.strokePath(selectedPath, strokeType);
    }
}

void PianoRollComponent::drawHandDrawPreview(juce::Graphics& g, double offsetSeconds)
{
    if (!interactionState_.drawing.isDrawingF0
        || currentTool_ != ToolId::HandDraw
        || interactionState_.drawing.handDrawBuffer.empty()
        || !currentCurve_)
    {
        return;
    }

    auto snapshot = currentCurve_->getSnapshot();
    const auto& originalF0 = snapshot->getOriginalF0();
    if (originalF0.empty() || interactionState_.drawing.handDrawBuffer.size() != originalF0.size())
        return;

    const double frameDuration = hopSize_ / f0SampleRate_;
    juce::Colour previewColour = juce::Colour(0xFF00DDDD);
    juce::Path previewPath;
    bool pathStarted = false;

    for (size_t i = 0; i < interactionState_.drawing.handDrawBuffer.size(); ++i)
    {
        float f0 = interactionState_.drawing.handDrawBuffer[i];
        if (f0 > 0.0f)
        {
            float y = freqToY(f0);
            double timePos = i * frameDuration;
            float x = static_cast<float>(timeToX(timePos + offsetSeconds));

            if (!pathStarted)
            {
                previewPath.startNewSubPath(x, y);
                pathStarted = true;
            }
            else
            {
                juce::Point<float> last = previewPath.getCurrentPosition();
                if (std::abs(x - last.x) > 30.0f)
                    previewPath.startNewSubPath(x, y);
                else
                    previewPath.lineTo(x, y);
            }
        }
        else if (pathStarted && f0 < -0.5f)
        {
            pathStarted = false;
        }
    }

    if (!previewPath.isEmpty())
    {
        g.setColour(previewColour.withAlpha(0.85f));
        juce::PathStrokeType strokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.strokePath(previewPath, strokeType);
    }
}

void PianoRollComponent::drawLineAnchorPreview(juce::Graphics& g, double offsetSeconds)
{
    const auto& ae = interactionState_.drawing.anchorEdit;
    if (currentTool_ != ToolId::LineAnchor || !ae.hasAnyPoints())
        return;

    const juce::Colour anchorColour = UIColors::correctedF0;
    constexpr int kSubdivisions = 4;

    for (const auto& grp : ae.groups)
    {
        const auto& pts = grp.points;
        if (pts.empty())
            continue;

        if (pts.size() >= 2)
        {
            const int n = static_cast<int>(pts.size());
            const auto slopes = HermiteInterpolation::computeClampedSlopes(pts);

            juce::Path curvePath;
            bool pathStarted = false;

            for (int seg = 0; seg < n - 1; ++seg)
            {
                const auto& p0 = pts[seg];
                const auto& p1 = pts[seg + 1];
                float h = static_cast<float>(p1.time - p0.time);
                if (h <= 0.0f)
                    continue;

                int steps = std::max(2, static_cast<int>(
                    std::abs(static_cast<float>(timeToX(p1.time + offsetSeconds))
                             - static_cast<float>(timeToX(p0.time + offsetSeconds))) / kSubdivisions));

                for (int s = 0; s <= steps; ++s)
                {
                    float t = static_cast<float>(s) / static_cast<float>(steps);
                    float midi = HermiteInterpolation::hermiteBasis(
                        p0.pitch, p1.pitch, slopes[seg], slopes[seg + 1], t, h);
                    float freq = PitchUtils::midiToFreq(midi);
                    float sx = static_cast<float>(timeToX(p0.time + (p1.time - p0.time) * t + offsetSeconds));
                    float sy = freqToY(freq);

                    if (!pathStarted)
                    {
                        curvePath.startNewSubPath(sx, sy);
                        pathStarted = true;
                    }
                    else
                    {
                        curvePath.lineTo(sx, sy);
                    }
                }
            }

            g.setColour(anchorColour.withAlpha(0.7f));
            juce::PathStrokeType strokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            g.strokePath(curvePath, strokeType);
        }

        for (const auto& pt : pts)
        {
            float ax = static_cast<float>(timeToX(pt.time + offsetSeconds));
            float ay = freqToY(PitchUtils::midiToFreq(pt.pitch));

            if (pt.selected)
            {
                g.setColour(juce::Colours::white);
                g.fillEllipse(ax - 5.0f, ay - 5.0f, 10.0f, 10.0f);
                g.setColour(anchorColour);
                g.fillEllipse(ax - 3.5f, ay - 3.5f, 7.0f, 7.0f);
            }
            else
            {
                g.setColour(anchorColour);
                g.fillEllipse(ax - 4.0f, ay - 4.0f, 8.0f, 8.0f);
                g.setColour(anchorColour.darker(0.3f));
                g.drawEllipse(ax - 4.0f, ay - 4.0f, 8.0f, 8.0f, 1.0f);
            }
        }
    }

    if (ae.mode == AnchorEditState::Mode::Placing && ae.activeGroupIndex >= 0
        && ae.activeGroupIndex < static_cast<int>(ae.groups.size()))
    {
        const auto& activePts = ae.groups[ae.activeGroupIndex].points;
        if (!activePts.empty())
        {
            const auto& last = activePts.back();
            float lastX = static_cast<float>(timeToX(last.time + offsetSeconds));
            float lastY = freqToY(PitchUtils::midiToFreq(last.pitch));
            g.setColour(anchorColour.withAlpha(0.35f));
            const float dashLengths[] = { 4.0f, 4.0f };
            juce::Path dashPath;
            dashPath.startNewSubPath(lastX, lastY);
            dashPath.lineTo(interactionState_.drawing.currentMousePos.x, interactionState_.drawing.currentMousePos.y);
            juce::PathStrokeType dashStroke(1.5f);
            dashStroke.createDashedStroke(dashPath, dashPath, dashLengths, 2);
            g.strokePath(dashPath, juce::PathStrokeType(1.5f));
        }
    }

    if (ae.mode == AnchorEditState::Mode::BoxSelecting)
    {
        double t0 = std::min(ae.boxStartTime, ae.boxEndTime);
        double t1 = std::max(ae.boxStartTime, ae.boxEndTime);
        float p0 = std::min(ae.boxStartPitch, ae.boxEndPitch);
        float p1 = std::max(ae.boxStartPitch, ae.boxEndPitch);

        float x0 = static_cast<float>(timeToX(t0 + offsetSeconds));
        float x1 = static_cast<float>(timeToX(t1 + offsetSeconds));
        float y0 = freqToY(PitchUtils::midiToFreq(p1));
        float y1 = freqToY(PitchUtils::midiToFreq(p0));

        g.setColour(anchorColour.withAlpha(0.15f));
        g.fillRect(x0, y0, x1 - x0, y1 - y0);
        g.setColour(anchorColour.withAlpha(0.5f));
        g.drawRect(x0, y0, x1 - x0, y1 - y0, 1.0f);
    }
}

void PianoRollComponent::drawSelectionBox(juce::Graphics& g, double offsetSeconds, ThemeId themeId)
{
    if (!toolHandler_ || !interactionState_.selection.isSelectingArea)
        return;

    double startTime = std::min(interactionState_.selection.dragStartTime, interactionState_.selection.dragEndTime);
    double endTime = std::max(interactionState_.selection.dragStartTime, interactionState_.selection.dragEndTime);
    float minMidi = std::min(interactionState_.selection.dragStartMidi, interactionState_.selection.dragEndMidi);
    float maxMidi = std::max(interactionState_.selection.dragStartMidi, interactionState_.selection.dragEndMidi);

    int x1 = timeToX(startTime + offsetSeconds);
    int x2 = timeToX(endTime + offsetSeconds);
    float y1 = midiToY(maxMidi);
    float y2 = midiToY(minMidi);

    float left = static_cast<float>(std::min(x1, x2));
    float top = std::min(y1, y2);
    float width = static_cast<float>(std::abs(x2 - x1));
    float height = std::abs(y2 - y1);

    juce::Rectangle<float> rect(left, top, width, height);
    juce::Colour fill = UIColors::lightPurple;
    juce::Colour stroke = UIColors::lightPurple;
    float fillAlpha = 0.12f;
    float strokeAlpha = 0.5f;
    float strokeThickness = 1.0f;

    if (themeId == ThemeId::DarkBlueGrey)
    {
        fill = juce::Colours::white;
        stroke = juce::Colours::white;
        fillAlpha = 0.20f;
        strokeAlpha = 0.90f;
        strokeThickness = 2.0f;
    }

    g.setColour(fill.withAlpha(fillAlpha));
    g.fillRoundedRectangle(rect, 3.0f);
    g.setColour(stroke.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(rect, 3.0f, strokeThickness);
}

void PianoRollComponent::drawRenderingProgress(juce::Graphics& g)
{
    if (!isRendering_)
        return;

    int margin = 10;
    int topY = 30;
    int rightX = getWidth() - margin - 30;

    const float spinnerRadius = 6.0f;
    const float spinnerX = static_cast<float>(rightX - 120);
    const float spinnerY = static_cast<float>(topY + 10);
    const float spinnerThickness = 1.5f;

    const double currentTime = juce::Time::getMillisecondCounterHiRes();
    const float rotationPhase = static_cast<float>(std::fmod(currentTime * 0.003, juce::MathConstants<double>::twoPi));

    g.setColour(UIColors::textPrimary.withAlpha(0.2f));
    g.drawEllipse(spinnerX - spinnerRadius, spinnerY - spinnerRadius,
                  spinnerRadius * 2.0f, spinnerRadius * 2.0f, spinnerThickness);

    juce::Path arcPath;
    const float arcLength = juce::MathConstants<float>::pi * 1.5f;
    arcPath.addCentredArc(spinnerX, spinnerY, spinnerRadius, spinnerRadius,
                          rotationPhase, 0.0f, arcLength, true);
    g.setColour(UIColors::textPrimary.withAlpha(0.85f));
    g.strokePath(arcPath, juce::PathStrokeType(spinnerThickness,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(UIColors::textPrimary);
    g.setFont(UIColors::getUIFont(12.0f));
    g.drawText("Rendering...", static_cast<int>(spinnerX + spinnerRadius + 8), topY + 5, 100, 15,
               juce::Justification::topLeft, false);
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    AppLogger::debug("[PianoRollComponent] paint: starting");
    auto ctx = buildRenderContext();
    constexpr double kStoredSr = OpenTuneAudioProcessor::getStoredAudioSampleRate();
    const auto clipDurationForIndex = [&](int clipIndex) -> double {
        if (processor_ == nullptr || currentTrackId_ < 0 || clipIndex < 0) {
            return -1.0;
        }
        const auto buf = processor_->getClipAudioBuffer(currentTrackId_, clipIndex);
        if (buf == nullptr || buf->getNumSamples() <= 0) {
            return -1.0;
        }
        return static_cast<double>(buf->getNumSamples()) / kStoredSr;
    };
    const auto clipDurationForActiveClipId = [&]() -> double {
        if (processor_ == nullptr || currentTrackId_ < 0 || currentClipId_ == 0) {
            return -1.0;
        }
        const int idx = processor_->findClipIndexById(currentTrackId_, currentClipId_);
        return clipDurationForIndex(idx);
    };
    auto bounds = getLocalBounds().toFloat().reduced(12.0f);
    const auto themeId = Theme::getActiveTheme();

    UIColors::drawShadow(g, bounds);

    juce::Path backgroundPath;
    backgroundPath.addRoundedRectangle(bounds, UIColors::cornerRadius);
    g.reduceClipRegion(backgroundPath);

    if (themeId == ThemeId::DarkBlueGrey)
        UIColors::fillSoothe2SpectrumBackground(g, bounds, UIColors::cornerRadius);
    else
        g.setColour(UIColors::rollBackground);

    if (themeId != ThemeId::DarkBlueGrey)
        g.fillPath(backgroundPath);

    renderer_->drawTimeRuler(g, ctx);

    {
        const juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(juce::Rectangle<int>(pianoKeyWidth_, 0, getWidth() - pianoKeyWidth_, getHeight()));

        renderer_->drawGridLines(g, ctx);

        if (showWaveform_ && hasUserAudio_)
            renderer_->drawWaveform(g, ctx);

        renderer_->drawLanes(g, ctx);

        if (processor_ != nullptr && currentTrackId_ >= 0 && processor_->getNumClips(currentTrackId_) > 0)
        {
            const int numClips = processor_->getNumClips(currentTrackId_);
            for (int ci = 0; ci < numClips; ++ci)
            {
                const double clipStartAbs = processor_->getClipStartSeconds(currentTrackId_, ci);
                const bool clipIsActive = (processor_->getClipId(currentTrackId_, ci) == currentClipId_);
                const auto clipNotes = processor_->getClipNotes(currentTrackId_, ci);
                renderer_->drawNotes(g, ctx, clipNotes, clipStartAbs, clipIsActive);
            }
        }
        else
        {
            renderer_->drawNotes(g, ctx, getCurrentClipNotes(), trackOffsetSeconds_, true);
        }

        const bool hasActiveAnchors = interactionState_.drawing.anchorEdit.hasAnyPoints();
        if (interactionState_.drawing.isDrawingF0 || interactionState_.drawing.isPlacingAnchors || hasActiveAnchors)
        {
            double drawStart = -1.0;
            double drawEnd = -1.0;
            if (interactionState_.drawing.isDrawingF0
                && interactionState_.drawing.dirtyStartTime >= 0.0
                && interactionState_.drawing.dirtyEndTime >= 0.0)
            {
                drawStart = std::min(interactionState_.drawing.dirtyStartTime, interactionState_.drawing.dirtyEndTime);
                drawEnd = std::max(interactionState_.drawing.dirtyStartTime, interactionState_.drawing.dirtyEndTime);
            }
            else if (hasActiveAnchors)
            {
                for (const auto& grp : interactionState_.drawing.anchorEdit.groups)
                {
                    if (grp.points.empty())
                        continue;

                    double gs = grp.points.front().time;
                    double ge = grp.points.back().time;
                    if (ge < gs)
                        std::swap(gs, ge);

                    if (drawStart < 0.0 || gs < drawStart)
                        drawStart = gs;
                    if (drawEnd < 0.0 || ge > drawEnd)
                        drawEnd = ge;
                }
            }
            else if (interactionState_.drawing.isPlacingAnchors && !interactionState_.drawing.pendingAnchors.empty())
            {
                drawStart = interactionState_.drawing.pendingAnchors.front().time;
                drawEnd = interactionState_.drawing.pendingAnchors.back().time;
                if (drawEnd < drawStart)
                    std::swap(drawStart, drawEnd);
            }

            if (drawStart >= 0.0 && drawEnd > drawStart)
            {
                for (const auto& note : getCurrentClipNotes())
                {
                    if (note.endTime <= drawStart || note.startTime >= drawEnd)
                        continue;

                    float adjustedPitch = note.getAdjustedPitch();
                    if (adjustedPitch <= 0.0f)
                        continue;

                    float midi = ctx.freqToMidi(adjustedPitch);
                    float ny = ctx.midiToY(midi) - (ctx.pixelsPerSemitone * 0.5f);
                    float nh = ctx.pixelsPerSemitone;
                    int nx1 = ctx.timeToX(note.startTime + trackOffsetSeconds_);
                    int nx2 = ctx.timeToX(note.endTime + trackOffsetSeconds_);
                    float nw = static_cast<float>(nx2 - nx1);
                    g.setColour(juce::Colour(0xFFFFD700).withAlpha(0.25f));
                    g.fillRect(static_cast<float>(nx1), ny, nw, nh);
                    g.setColour(juce::Colour(0xFFFFD700).withAlpha(0.6f));
                    g.drawRect(static_cast<float>(nx1), ny, nw, nh, 1.5f);
                }
            }
        }

        if (processor_ != nullptr && currentTrackId_ >= 0 && showOriginalF0_
            && processor_->getNumClips(currentTrackId_) > 0)
        {
            const int numClips = processor_->getNumClips(currentTrackId_);
            for (int ci = 0; ci < numClips; ++ci)
            {
                auto curve = processor_->getClipPitchCurve(currentTrackId_, ci);
                if (curve == nullptr)
                    continue;
                auto snapshot = curve->getSnapshot();
                if (snapshot == nullptr)
                    continue;
                const auto& originalF0 = snapshot->getOriginalF0();
                if (originalF0.empty())
                    continue;

                const double clipStartAbs = processor_->getClipStartSeconds(currentTrackId_, ci);
                const bool clipIsActive = (processor_->getClipId(currentTrackId_, ci) == currentClipId_);

                PianoRollRenderer::RenderContext ctxF0 = renderContextForClipPitchSnapshot(ctx, snapshot);
                if (!clipIsActive)
                    ctxF0.hasF0Selection = false;

                const juce::Colour origCol = clipIsActive ? UIColors::originalF0 : UIColors::originalF0.darker(0.5f);
                const float origAlpha = clipIsActive ? 0.55f : 0.36f;
                const double clipDurSec = clipDurationForIndex(ci);
                renderer_->drawF0Curve(
                    g, originalF0, origCol, origAlpha, true, ctxF0, curve, nullptr, clipStartAbs, clipDurSec);
                if (clipIsActive)
                    drawSelectedOriginalF0Curve(g, originalF0, trackOffsetSeconds_);
            }
        }
        else if (currentCurve_ != nullptr && showOriginalF0_)
        {
            auto snapshot = currentCurve_->getSnapshot();
            const auto& originalF0 = snapshot->getOriginalF0();
            if (!originalF0.empty())
            {
                const PianoRollRenderer::RenderContext ctxOrig = renderContextForClipPitchSnapshot(ctx, snapshot);
                renderer_->drawF0Curve(
                    g,
                    originalF0,
                    UIColors::originalF0,
                    0.55f,
                    true,
                    ctxOrig,
                    currentCurve_,
                    nullptr,
                    trackOffsetSeconds_,
                    clipDurationForActiveClipId());
                drawSelectedOriginalF0Curve(g, originalF0, trackOffsetSeconds_);
            }
        }

        // Corrected F0: same contract as original F0 above — when multiple clips exist on the track,
        // each clip must be drawn with its own PitchCurve at clipStartAbs. Drawing only currentCurve_
        // at trackOffsetSeconds_ hid the right clip's corrections after split (inactive clip had no green line).
        if (processor_ != nullptr && currentTrackId_ >= 0 && showCorrectedF0_
            && processor_->getNumClips(currentTrackId_) > 0)
        {
            const int numClips = processor_->getNumClips(currentTrackId_);
            for (int ci = 0; ci < numClips; ++ci)
            {
                auto curve = processor_->getClipPitchCurve(currentTrackId_, ci);
                if (curve == nullptr)
                    continue;
                auto snapshot = curve->getSnapshot();
                if (snapshot == nullptr)
                    continue;
                const int totalFrames = static_cast<int>(snapshot->size());
                if (totalFrames <= 0 || !snapshot->hasAnyCorrection())
                    continue;

                const double clipStartAbs = processor_->getClipStartSeconds(currentTrackId_, ci);
                const bool clipIsActive = (processor_->getClipId(currentTrackId_, ci) == currentClipId_);

                PianoRollRenderer::RenderContext ctxCorr = renderContextForClipPitchSnapshot(ctx, snapshot);
                if (!clipIsActive)
                    ctxCorr.hasF0Selection = false;

                renderer_->updateCorrectedF0Cache(snapshot);
                const float corrAlpha = clipIsActive ? 1.0f : 0.55f;
                const double clipDurSec = clipDurationForIndex(ci);
                renderer_->drawF0Curve(
                    g,
                    renderer_->getCorrectedF0Cache(),
                    UIColors::correctedF0,
                    corrAlpha,
                    false,
                    ctxCorr,
                    curve,
                    nullptr,
                    clipStartAbs,
                    clipDurSec);
            }
        }
        else if (currentCurve_ != nullptr && showCorrectedF0_)
        {
            auto currentSnapshot = currentCurve_->getSnapshot();
            const int totalFrames = static_cast<int>(currentSnapshot->size());
            if (totalFrames > 0 && currentSnapshot->hasAnyCorrection())
            {
                renderer_->updateCorrectedF0Cache(currentSnapshot);
                const PianoRollRenderer::RenderContext ctxCorr = renderContextForClipPitchSnapshot(ctx, currentSnapshot);
                renderer_->drawF0Curve(
                    g,
                    renderer_->getCorrectedF0Cache(),
                    UIColors::correctedF0,
                    1.0f,
                    false,
                    ctxCorr,
                    currentCurve_,
                    nullptr,
                    trackOffsetSeconds_,
                    clipDurationForActiveClipId());
            }
        }

        if (currentCurve_ != nullptr)
        {
            drawHandDrawPreview(g, trackOffsetSeconds_);
            drawLineAnchorPreview(g, trackOffsetSeconds_);
        }

        if (processor_ != nullptr && currentTrackId_ >= 0 && processor_->getNumClips(currentTrackId_) > 0)
        {
            const int numClips = processor_->getNumClips(currentTrackId_);
            for (int ci = 0; ci < numClips; ++ci)
            {
                const double clipStartAbs = processor_->getClipStartSeconds(currentTrackId_, ci);
                const bool clipIsActive = (processor_->getClipId(currentTrackId_, ci) == currentClipId_);
                const auto clipNotes = processor_->getClipNotes(currentTrackId_, ci);
                renderer_->drawNoteLabels(g, ctx, clipNotes, clipStartAbs, clipIsActive);
            }
        }
        else
        {
            renderer_->drawNoteLabels(g, ctx, getCurrentClipNotes(), trackOffsetSeconds_, true);
        }

        // 常时：当前编辑 clip 以外的轨道片段上叠灰色遮罩（与播放头位置无关）
        if (processor_ != nullptr && currentTrackId_ >= 0 && currentClipId_ != 0)
        {
            const int numClipsOther = processor_->getNumClips(currentTrackId_);
            g.setColour(juce::Colour(0xFF1E1E22).withAlpha(0.48f));
            for (int i = 0; i < numClipsOther; ++i)
            {
                if (processor_->getClipId(currentTrackId_, i) == currentClipId_)
                    continue;

                const double clipStartAbs = processor_->getClipStartSeconds(currentTrackId_, i);
                const auto buf = processor_->getClipAudioBuffer(currentTrackId_, i);
                const double dur = buf ? static_cast<double>(buf->getNumSamples()) / kStoredSr : 0.0;
                if (dur <= 0.0)
                    continue;

                const double clipEndAbs = clipStartAbs + dur;
                const int x1 = timeToX(clipStartAbs);
                const int x2 = timeToX(clipEndAbs);
                const int rw = juce::jmax(0, x2 - x1);
                if (rw <= 0)
                    continue;

                g.fillRect(x1, 0, rw, getHeight());
            }
        }
    }

    renderer_->drawPianoKeys(g, ctx);

    drawSelectionBox(g, trackOffsetSeconds_, themeId);
    drawRenderingProgress(g);

    AppLogger::debug("[PianoRollComponent] paint: completed");
}

PianoRollRenderer::RenderContext PianoRollComponent::buildRenderContext() const
{
    PianoRollRenderer::RenderContext ctx;
    ctx.width = getWidth();
    ctx.height = getHeight();
    ctx.pianoKeyWidth = pianoKeyWidth_;
    ctx.rulerHeight = rulerHeight_;
    ctx.zoomLevel = zoomLevel_;
    ctx.scrollOffset = scrollOffset_;
    ctx.pixelsPerSemitone = pixelsPerSemitone_;
    ctx.minMidi = minMidi_;
    ctx.maxMidi = maxMidi_;
    ctx.bpm = bpm_;
    ctx.timeSigNum = timeSigNum_;
    ctx.timeSigDenom = timeSigDenom_;
    ctx.trackOffsetSeconds = timelineAnchorSeconds_;
    ctx.editingClipStartSeconds = trackOffsetSeconds_;
    ctx.processor = processor_;
    ctx.waveformTrackId = currentTrackId_;
    ctx.waveformEditingClipId = currentClipId_;
    ctx.waveformCache = &waveformMipmapCache_;
    ctx.audioSampleRate = PianoRollComponent::kAudioSampleRate;
    ctx.hopSize = hopSize_;
    ctx.f0SampleRate = f0SampleRate_;
    ctx.scaleRootNote = scaleRootNote_;
    ctx.scaleType = scaleType_;
    ctx.noteNameMode = noteNameDisplayMode_;
    ctx.showNoteBlockNoteNames = showNoteBlockNoteNames_;
    ctx.showWaveform = showWaveform_;
    ctx.showLanes = showLanes_;
    ctx.showOriginalF0 = showOriginalF0_;
    ctx.showCorrectedF0 = showCorrectedF0_;
    ctx.isRendering = isRendering_;
    ctx.renderingProgress = renderingProgress_;
    ctx.hasUserAudio = hasUserAudio_;
    ctx.timeUnit = (timeUnit_ == TimeUnit::Bars)
        ? PianoRollRenderer::RenderContext::TimeUnit::Bars
        : PianoRollRenderer::RenderContext::TimeUnit::Seconds;

    ctx.midiToY = [this](float midi) { return midiToY(midi); };
    ctx.freqToY = [this](float freq) { return freqToY(freq); };
    ctx.freqToMidi = [this](float freq) { return freqToMidi(freq); };
    ctx.xToTime = [this](int x) { return xToTime(x); };
    ctx.timeToX = [this](double seconds) { return timeToX(seconds); };
    ctx.clipSecondsToFrameIndex = [this](double seconds) -> double {
        const double frameDuration = hopSize_ / f0SampleRate_;
        return seconds / frameDuration;
    };
    ctx.frameIndexToClipSeconds = [this](int frame) -> double {
        const double frameDuration = hopSize_ / f0SampleRate_;
        return frame * frameDuration;
    };

    ctx.hasF0Selection = interactionState_.selection.hasF0Selection;
    ctx.f0SelectionStartFrame = interactionState_.selection.selectedF0StartFrame;
    ctx.f0SelectionEndFrame = interactionState_.selection.selectedF0EndFrame;

    return ctx;
}

} // namespace OpenTune
