#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>

namespace OpenTune {

class TimelineZoomScrollBar : public juce::ScrollBar
{
public:
    explicit TimelineZoomScrollBar(bool isVertical)
        : juce::ScrollBar(isVertical)
    {
    }

    std::function<void(double thumbStartNormalized, double thumbEndNormalized)> onThumbResizeRequested;

    void mouseMove(const juce::MouseEvent& e) override
    {
        updateCursorForPoint(e.position);
        juce::ScrollBar::mouseMove(e);
    }

    void mouseExit(const juce::MouseEvent& e) override
    {
        juce::ignoreUnused(e);

        if (resizeEdge_ == ThumbEdge::None)
            setMouseCursor(juce::MouseCursor::NormalCursor);

        juce::ScrollBar::mouseExit(e);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (onThumbResizeRequested
            && (resizeEdge_ = hitTestThumbEdge(e.position)) != ThumbEdge::None)
        {
            const auto thumbBounds = getThumbBounds();
            const float trackLength = static_cast<float>(juce::jmax(1, isVertical() ? getHeight() : getWidth()));

            if (resizeEdge_ == ThumbEdge::Start)
                fixedOppositeEdgeNormalized_ = isVertical() ? (thumbBounds.getBottom() / trackLength)
                                                            : (thumbBounds.getRight() / trackLength);
            else
                fixedOppositeEdgeNormalized_ = isVertical() ? (thumbBounds.getY() / trackLength)
                                                            : (thumbBounds.getX() / trackLength);

            setMouseCursor(getResizeCursor());
            return;
        }

        resizeEdge_ = ThumbEdge::None;
        juce::ScrollBar::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (resizeEdge_ != ThumbEdge::None && onThumbResizeRequested)
        {
            const float trackLength = static_cast<float>(juce::jmax(1, isVertical() ? getHeight() : getWidth()));
            const float minNormalizedSpan = juce::jmin(0.95f, kMinThumbPixels_ / trackLength);
            const float pointerNormalized = juce::jlimit(0.0f,
                                                         1.0f,
                                                         (isVertical() ? e.position.y : e.position.x) / trackLength);

            float thumbStartNormalized = 0.0f;
            float thumbEndNormalized = 1.0f;

            if (resizeEdge_ == ThumbEdge::Start)
            {
                thumbStartNormalized = juce::jlimit(
                    0.0f,
                    fixedOppositeEdgeNormalized_ - minNormalizedSpan,
                    pointerNormalized);
                thumbEndNormalized = fixedOppositeEdgeNormalized_;
            }
            else
            {
                thumbStartNormalized = fixedOppositeEdgeNormalized_;
                thumbEndNormalized = juce::jlimit(
                    fixedOppositeEdgeNormalized_ + minNormalizedSpan,
                    1.0f,
                    pointerNormalized);
            }

            if ((thumbEndNormalized - thumbStartNormalized) > 1.0e-6f)
                onThumbResizeRequested(static_cast<double>(thumbStartNormalized),
                                       static_cast<double>(thumbEndNormalized));

            setMouseCursor(getResizeCursor());
            return;
        }

        juce::ScrollBar::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const bool wasResizing = (resizeEdge_ != ThumbEdge::None);
        resizeEdge_ = ThumbEdge::None;
        fixedOppositeEdgeNormalized_ = 0.0f;

        if (!wasResizing)
            juce::ScrollBar::mouseUp(e);

        updateCursorForPoint(e.position);
    }

private:
    enum class ThumbEdge
    {
        None,
        Start,
        End
    };

    juce::Rectangle<float> getThumbBounds() const
    {
        const auto rangeLimit = getRangeLimit();
        const double rangeLength = rangeLimit.getLength();
        const double currentSize = getCurrentRangeSize();

        if (rangeLength <= 0.0 || currentSize <= 0.0)
            return {};

        const float trackLength = static_cast<float>(juce::jmax(1, isVertical() ? getHeight() : getWidth()));
        const float thumbStartPosition =
            static_cast<float>((getCurrentRangeStart() - rangeLimit.getStart()) / rangeLength) * trackLength;
        float thumbLength = static_cast<float>(currentSize / rangeLength) * trackLength;
        thumbLength = juce::jlimit(2.0f, trackLength, thumbLength);

        const float clampedThumbStart =
            juce::jlimit(0.0f, juce::jmax(0.0f, trackLength - thumbLength), thumbStartPosition);

        if (isVertical())
            return { 0.0f, clampedThumbStart, static_cast<float>(getWidth()), thumbLength };

        return { clampedThumbStart, 0.0f, thumbLength, static_cast<float>(getHeight()) };
    }

    ThumbEdge hitTestThumbEdge(juce::Point<float> localPoint) const
    {
        const auto thumbBounds = getThumbBounds();
        const auto expandedThumbBounds = isVertical() ? thumbBounds.expanded(0.0f, kThumbEdgeHitPadding_)
                                                      : thumbBounds.expanded(kThumbEdgeHitPadding_, 0.0f);
        if (thumbBounds.isEmpty() || !expandedThumbBounds.contains(localPoint))
            return ThumbEdge::None;

        const float startDistance = isVertical() ? std::abs(localPoint.y - thumbBounds.getY())
                                                 : std::abs(localPoint.x - thumbBounds.getX());
        const float endDistance = isVertical() ? std::abs(localPoint.y - thumbBounds.getBottom())
                                               : std::abs(localPoint.x - thumbBounds.getRight());
        const float grabDistance = kThumbEdgeHitPadding_;

        if (startDistance > grabDistance && endDistance > grabDistance)
            return ThumbEdge::None;

        return (startDistance <= endDistance) ? ThumbEdge::Start : ThumbEdge::End;
    }

    void updateCursorForPoint(juce::Point<float> localPoint)
    {
        if (resizeEdge_ != ThumbEdge::None || hitTestThumbEdge(localPoint) != ThumbEdge::None)
            setMouseCursor(getResizeCursor());
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    juce::MouseCursor getResizeCursor() const
    {
        return juce::MouseCursor(isVertical() ? juce::MouseCursor::UpDownResizeCursor
                                              : juce::MouseCursor::LeftRightResizeCursor);
    }

    ThumbEdge resizeEdge_{ThumbEdge::None};
    float fixedOppositeEdgeNormalized_{0.0f};

    static constexpr float kThumbEdgeHitPadding_ = 8.0f;
    static constexpr float kMinThumbPixels_ = 22.0f;
};

} // namespace OpenTune
