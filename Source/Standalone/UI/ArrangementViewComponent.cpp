#include "ArrangementViewComponent.h"
#include "AuroraTheme.h"
#include "FrameScheduler.h"

#include <cmath>

namespace OpenTune {

ArrangementViewComponent::ArrangementViewComponent(OpenTuneAudioProcessor& processor)
    : processor_(processor)
{
    setWantsKeyboardFocus(true);
    timeConverter_.setContext(processor_.getBpm(), processor_.getTimeSigNumerator(), processor_.getTimeSigDenominator());
    timeConverter_.setZoom(zoomLevel_);
    timeConverter_.setScrollOffset(scrollOffset_);

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

    addAndMakeVisible(playheadOverlay_);
    playheadOverlay_.setPianoKeyWidth(8);
    scrollVBlankAttachment_ = std::make_unique<juce::VBlankAttachment>(
        this, [this](double timestampSec) { onScrollVBlankCallback(timestampSec); });
}

ArrangementViewComponent::~ArrangementViewComponent()
{
    if (clipRenameLabel_)
        clipRenameLabel_->onEditorHide = nullptr;
    clipRenameLabel_.reset();

    scrollVBlankAttachment_.reset();
    stopTimer();
    horizontalScrollBar_.removeListener(this);
    verticalScrollBar_.removeListener(this);
}

void ArrangementViewComponent::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void ArrangementViewComponent::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void ArrangementViewComponent::setZoomLevel(double zoom)
{
    const double minZoom = getMinimumHorizontalZoomLevel();
    zoomLevel_ = juce::jlimit(minZoom, 10.0, zoom);
    timeConverter_.setZoom(zoomLevel_);
    // 同步缩放级别到高性能播放头覆盖层
    playheadOverlay_.setZoomLevel(zoomLevel_);
    updateScrollBars();
    FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Normal);
}

void ArrangementViewComponent::setIsPlaying(bool playing)
{
    const bool wasPlaying = isPlaying_.exchange(playing, std::memory_order_relaxed);
    playheadOverlay_.setPlaying(playing);
    if (wasPlaying && !playing)
    {
        reconcileHorizontalScrollAfterEdit();
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
    }
}

void ArrangementViewComponent::reconcileHorizontalScrollAfterEdit()
{
    const float nextSmooth = static_cast<float>(scrollOffset_);
    const bool smoothMismatch = std::abs(smoothScrollCurrent_ - nextSmooth) > 1.0e-4f;
    smoothScrollCurrent_ = nextSmooth;
    timeConverter_.setScrollOffset(static_cast<double>(scrollOffset_));
    playheadOverlay_.setScrollOffset(static_cast<double>(scrollOffset_));
    if (smoothMismatch)
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
}

void ArrangementViewComponent::setScrollOffset(int pixels)
{
    const int visibleWidth = juce::jmax(1, getWidth() - kScrollbarBreadth_);
    const double pixelsPerSecond = 100.0 * zoomLevel_;
    const int totalContentWidth = static_cast<int>(std::ceil(getTimelineSpanSeconds() * pixelsPerSecond));
    const int maxScrollOffset = juce::jmax(0, totalContentWidth - visibleWidth);
    const int newOffset = juce::jlimit(0, maxScrollOffset, pixels);
    if (newOffset == scrollOffset_)
        return;

    scrollOffset_ = newOffset;
    timeConverter_.setScrollOffset(scrollOffset_);
    horizontalScrollBar_.setCurrentRangeStart(scrollOffset_);

    if (!processor_.isPlaying())
        smoothScrollCurrent_ = static_cast<float>(scrollOffset_);

    playheadOverlay_.setScrollOffset(processor_.isPlaying()
        ? static_cast<double>(smoothScrollCurrent_)
        : static_cast<double>(scrollOffset_));
    FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
}

void ArrangementViewComponent::syncPlayheadPosition(double timeSeconds)
{
    playheadOverlay_.setPlayheadSeconds(timeSeconds);

    if (!isPlaying_.load(std::memory_order_relaxed))
    {
        playheadOverlay_.repaint();
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Interactive);
    }
}

double ArrangementViewComponent::getVisibleStartTimeSeconds() const
{
    return static_cast<double>(scrollOffset_) / (100.0 * zoomLevel_);
}

double ArrangementViewComponent::getVisibleDurationSeconds() const
{
    const int visibleWidth = juce::jmax(1, getWidth() - kScrollbarBreadth_);
    const double pixelsPerSecond = juce::jmax(1.0e-6, 100.0 * zoomLevel_);
    return static_cast<double>(visibleWidth) / pixelsPerSecond;
}

void ArrangementViewComponent::setVisibleStartTimeSeconds(double timeSeconds)
{
    const int newOffset = static_cast<int>(std::llround(juce::jmax(0.0, timeSeconds) * 100.0 * zoomLevel_));
    setScrollOffset(newOffset);
}

void ArrangementViewComponent::setTimeUnitSeconds(bool useSeconds)
{
    const TimeUnit newUnit = useSeconds ? TimeUnit::Seconds : TimeUnit::Bars;
    if (timeUnit_ == newUnit)
        return;

    timeUnit_ = newUnit;
    repaint();
}

void ArrangementViewComponent::setVerticalScrollOffset(int offset)
{
    const int rows = getTimelineLayoutTrackRows();
    const int totalContentHeight = rulerHeight_ + rows * processor_.getTrackHeight() + kTrackAddButtonRegion_;
    const int visibleHeight = juce::jmax(0, getHeight() - kScrollbarBreadth_);
    const int maxScrollOffset = juce::jmax(0, totalContentHeight - visibleHeight);
    
    // 限制滚动范围 [0, maxScrollOffset]
    verticalScrollOffset_ = juce::jlimit(0, maxScrollOffset, offset);
    verticalScrollBar_.setCurrentRangeStart(verticalScrollOffset_);
    FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Normal);
}

void ArrangementViewComponent::fitToContent()
{
    // 如果用户已手动调整过缩放，不自动覆盖
    if (userHasManuallyZoomed_) {
        return;
    }

    const double maxEndTime = getTimelineSpanSeconds();

    if (maxEndTime <= 0.0 || getWidth() <= 8) {
        return;
    }

    int viewWidth = getWidth() - kScrollbarBreadth_;
    int paddingPx = 12;
    int drawableWidth = juce::jmax(1, viewWidth - paddingPx);
    double zoom = (static_cast<double>(drawableWidth) / maxEndTime) / 100.0;
    // 限制缩放范围：0.02~10.0（支持自动缩放到更长音频）
    zoom = juce::jlimit(0.02, 10.0, zoom);
    setZoomLevel(zoom);
    setScrollOffset(0);
}

void ArrangementViewComponent::updateMeter(int trackId, float rmsDb)
{
    juce::ignoreUnused(trackId, rmsDb);
}

void ArrangementViewComponent::prioritizeWaveformBuildForClip(int trackId, uint64_t clipId)
{
    juce::ignoreUnused(trackId);
    prioritizedWaveformKey_ = clipId;
}

bool ArrangementViewComponent::isWaveformCacheCompleteForClip(int trackId, uint64_t clipId) const
{
    juce::ignoreUnused(trackId);
    if (clipId == 0)
        return false;

    const auto* mipmap = waveformMipmapCache_.get(clipId);
    if (!mipmap)
        return false;

    return mipmap->isComplete();
}

void ArrangementViewComponent::resized()
{
    auto bounds = getLocalBounds();
    auto vBarArea = bounds.removeFromRight(kScrollbarBreadth_);
    verticalScrollBar_.setBounds(vBarArea);
    horizontalScrollBar_.setBounds(bounds.removeFromBottom(kScrollbarBreadth_));

    updateScrollBars();

    // 播放头覆盖层覆盖整个组件区域
    playheadOverlay_.setBounds(getLocalBounds());

    if (clipRenameLabel_ != nullptr && clipRenameLabel_->isVisible() && clipRenameTrack_ >= 0 && clipRenameClipIdx_ >= 0)
    {
        auto cb = getClipBounds(clipRenameTrack_, clipRenameClipIdx_);
        if (!cb.isEmpty())
        {
            auto nameRow = cb.removeFromTop(juce::jmin(20, cb.getHeight())).reduced(4, 2);
            clipRenameLabel_->setBounds(nameRow);
            clipRenameLabel_->toFront(false);
        }
    }
}

void ArrangementViewComponent::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart)
{
    if (scrollBar == &horizontalScrollBar_)
    {
        setScrollOffset(static_cast<int>(newRangeStart));
        smoothScrollCurrent_ = (float)newRangeStart; // Sync for manual scroll
        notifyVisibleStartTimeChanged();
    }
    else if (scrollBar == &verticalScrollBar_)
    {
        setVerticalScrollOffset(static_cast<int>(newRangeStart));
        // 通知监听器垂直滚动偏移变化（用于同步TrackPanel）
        listeners_.call([this](Listener& l) { l.verticalScrollChanged(verticalScrollOffset_); });
        FrameScheduler::instance().requestInvalidate(*this, FrameScheduler::Priority::Normal);
    }
}

void ArrangementViewComponent::updateScrollBars()
{
    const double maxEndTime = getTimelineSpanSeconds();

    double pixelsPerSecond = 100.0 * zoomLevel_;
    int totalContentWidth = static_cast<int>(maxEndTime * pixelsPerSecond);
    int visibleWidth = getWidth() - kScrollbarBreadth_;
    visibleWidth = juce::jmax(1, visibleWidth);

    const int maxRange = juce::jmax(totalContentWidth, visibleWidth);
    const int maxScroll = juce::jmax(0, maxRange - visibleWidth);
    scrollOffset_ = juce::jlimit(0, maxScroll, scrollOffset_);

    if (processor_.isPlaying())
        smoothScrollCurrent_ = juce::jlimit(0.0f, static_cast<float>(maxScroll), smoothScrollCurrent_);
    else
        smoothScrollCurrent_ = static_cast<float>(scrollOffset_);

    playheadOverlay_.setScrollOffset(processor_.isPlaying()
        ? static_cast<double>(smoothScrollCurrent_)
        : static_cast<double>(scrollOffset_));

    horizontalScrollBar_.setRangeLimits(0.0, static_cast<double>(maxRange));
    horizontalScrollBar_.setCurrentRange(scrollOffset_, visibleWidth);

    const int rows = getTimelineLayoutTrackRows();
    const int totalTrackHeight = rulerHeight_ + rows * processor_.getTrackHeight() + kTrackAddButtonRegion_;
    int visibleHeight = juce::jmax(1, getHeight() - kScrollbarBreadth_);
    const int maxVerticalScroll = juce::jmax(0, totalTrackHeight - visibleHeight);
    verticalScrollOffset_ = juce::jlimit(0, maxVerticalScroll, verticalScrollOffset_);
    verticalScrollBar_.setRangeLimits(0.0, static_cast<double>(totalTrackHeight));
    verticalScrollBar_.setCurrentRange(verticalScrollOffset_, visibleHeight);
}

void ArrangementViewComponent::notifyVisibleStartTimeChanged()
{
    if (onVisibleStartTimeChanged)
        onVisibleStartTimeChanged(getVisibleStartTimeSeconds());
}

void ArrangementViewComponent::applyScrollBarThumbResize(double thumbStartNormalized, double thumbEndNormalized)
{
    const int visibleWidth = juce::jmax(1, getWidth() - kScrollbarBreadth_);
    const double normalizedSpan = thumbEndNormalized - thumbStartNormalized;
    const double timelineSpanSeconds = getTimelineSpanSeconds();

    if (timelineSpanSeconds <= 0.0 || normalizedSpan <= 1.0e-6)
        return;

    const double newTotalRange = static_cast<double>(visibleWidth) / normalizedSpan;
    const double newZoom = juce::jlimit(
        getMinimumHorizontalZoomLevel(),
        10.0,
        newTotalRange / (timelineSpanSeconds * 100.0));

    if (!std::isfinite(newZoom))
        return;

    setZoomLevel(newZoom);
    userHasManuallyZoomed_ = true;

    const int newScrollOffset = static_cast<int>(std::llround(thumbStartNormalized * newTotalRange));
    setScrollOffset(newScrollOffset);

    if (onUserTimelineZoomChanged)
        onUserTimelineZoomChanged(newZoom);

    notifyVisibleStartTimeChanged();
}

void ArrangementViewComponent::applyVerticalScrollBarThumbResize(double thumbStartNormalized, double thumbEndNormalized)
{
    const int visibleHeight = juce::jmax(1, getHeight() - kScrollbarBreadth_);
    const int rows = getTimelineLayoutTrackRows();
    const double normalizedSpan = thumbEndNormalized - thumbStartNormalized;

    if (rows <= 0 || normalizedSpan <= 1.0e-6)
        return;

    const double newTotalRange = static_cast<double>(visibleHeight) / normalizedSpan;
    const double trackRegionHeight = newTotalRange - static_cast<double>(rulerHeight_ + kTrackAddButtonRegion_);
    const int newTrackHeight = juce::jlimit(70,
                                            300,
                                            static_cast<int>(std::lround(trackRegionHeight / static_cast<double>(rows))));

    if (newTrackHeight != processor_.getTrackHeight())
    {
        processor_.setTrackHeight(newTrackHeight);
        updateScrollBars();
        listeners_.call([newTrackHeight](Listener& l) { l.trackHeightChanged(newTrackHeight); });
    }

    const double actualTotalRange =
        static_cast<double>(rulerHeight_ + rows * processor_.getTrackHeight() + kTrackAddButtonRegion_);
    const int newOffset = static_cast<int>(std::lround(thumbStartNormalized * actualTotalRange));
    setVerticalScrollOffset(newOffset);
    listeners_.call([this](Listener& l) { l.verticalScrollChanged(verticalScrollOffset_); });
}

double ArrangementViewComponent::getTimelineSpanSeconds() const
{
    const double projectEndSeconds = processor_.getProjectTimelineEndSeconds();
    if (projectEndSeconds <= 0.0)
        return 1.0;

    constexpr double kTimelineSnapSeconds = 5.0;
    const double snappedTimelineEnd =
        std::ceil((projectEndSeconds - 1.0e-6) / kTimelineSnapSeconds) * kTimelineSnapSeconds;
    return juce::jmax(snappedTimelineEnd, 1.0);
}

double ArrangementViewComponent::getMinimumHorizontalZoomLevel() const
{
    const double spanSeconds = getTimelineSpanSeconds();
    if (spanSeconds <= 0.0)
        return 0.02;

    const int visibleWidth = juce::jmax(1, getWidth() - kScrollbarBreadth_);
    const double fitZoom = static_cast<double>(visibleWidth) / (spanSeconds * 100.0);
    return juce::jlimit(0.02, 10.0, fitZoom);
}

int ArrangementViewComponent::getTimelineLayoutTrackRows() const
{
    int rows = 2;
    if (timelineTrackRowCountSource_) {
        rows = juce::jmax(rows, timelineTrackRowCountSource_());
    }
    rows = juce::jmax(rows, processor_.getActiveTrackId() + 1);

    for (int trackId = 0; trackId < OpenTuneAudioProcessor::MAX_TRACKS; ++trackId)
    {
        if (processor_.getNumClips(trackId) > 0)
            rows = trackId + 1;
    }

    return juce::jlimit(1, OpenTuneAudioProcessor::MAX_TRACKS, rows);
}

void ArrangementViewComponent::syncTimeConverterForGeometryQueries() const
{
    const double bpm = processor_.getBpm();
    const int timeSigNum = processor_.getTimeSigNumerator();
    const int timeSigDenom = processor_.getTimeSigDenominator();
    timeConverter_.setContext(bpm, timeSigNum, timeSigDenom);
    timeConverter_.setZoom(zoomLevel_);
    if (processor_.isPlaying())
        timeConverter_.setScrollOffset(static_cast<double>(smoothScrollCurrent_));
    else
        timeConverter_.setScrollOffset(static_cast<double>(scrollOffset_));
}

int ArrangementViewComponent::timeToX(double seconds) const
{
    syncTimeConverterForGeometryQueries();
    return timeConverter_.timeToPixel(seconds) + 8;
}

double ArrangementViewComponent::xToTime(int x) const
{
    syncTimeConverterForGeometryQueries();
    return timeConverter_.pixelToTime(x - 8);
}

juce::Rectangle<int> ArrangementViewComponent::getTrackLaneBounds(int trackId) const
{
    auto bounds = getLocalBounds().withTrimmedTop(rulerHeight_);
    bounds.removeFromRight(kScrollbarBreadth_);
    bounds.removeFromBottom(kScrollbarBreadth_);
    int h = processor_.getTrackHeight();
    return bounds.withY(rulerHeight_ + trackId * h - verticalScrollOffset_).withHeight(h);
}

juce::Rectangle<int> ArrangementViewComponent::getClipBounds(int trackId, int clipIndex) const
{
    std::shared_ptr<const juce::AudioBuffer<float>> audioBuffer =
        processor_.getClipAudioBuffer(trackId, clipIndex);
    if (audioBuffer == nullptr)
        return {};

    double startSeconds = processor_.getClipStartSeconds(trackId, clipIndex);
    int numSamples = audioBuffer->getNumSamples();
    if (numSamples <= 0)
        return {};

    // [TIME-01] Audio is stored at fixed 44.1kHz - use stored sample rate for duration calculation
    constexpr double storedSampleRate = 44100.0;
    double durationSeconds = static_cast<double>(numSamples) / storedSampleRate;
    auto lane = getTrackLaneBounds(trackId).reduced(6, 8);

    int x1 = timeToX(startSeconds);
    int x2 = timeToX(startSeconds + durationSeconds);
    int w = juce::jmax(8, x2 - x1);

    return { x1, lane.getY(), w, lane.getHeight() };
}

int ArrangementViewComponent::getTrackIndexAtPoint(juce::Point<int> p) const
{
    if (p.y < rulerHeight_)
        return -1;

    int adjustedY = p.y + verticalScrollOffset_;
    int h = processor_.getTrackHeight();
    if (h <= 0)
        return -1;
    int trackId = (adjustedY - rulerHeight_) / h;
    if (trackId < 0 || trackId >= OpenTuneAudioProcessor::MAX_TRACKS)
        return -1;
    return trackId;
}

void ArrangementViewComponent::beginClipRenameForClip(int trackId, int clipIndex)
{
    beginClipRename(trackId, clipIndex);
}

void ArrangementViewComponent::beginClipRename(int trackId, int clipIndex)
{
    if (trackId < 0 || clipIndex < 0)
        return;

    auto bounds = getClipBounds(trackId, clipIndex);
    if (bounds.isEmpty())
        return;

    if (clipRenameLabel_ == nullptr)
    {
        clipRenameLabel_ = std::make_unique<juce::Label>();
        clipRenameLabel_->setEditable(false, true, false);
        clipRenameLabel_->setJustificationType(juce::Justification::centredLeft);
        clipRenameLabel_->setFont(UIColors::getUIFont(13.0f));
        clipRenameLabel_->setColour(juce::Label::backgroundColourId, UIColors::backgroundLight);
        clipRenameLabel_->setColour(juce::Label::outlineColourId, UIColors::accent);
        clipRenameLabel_->setColour(juce::Label::textColourId, UIColors::textPrimary);
        addChildComponent(*clipRenameLabel_);
    }

    clipRenameTrack_ = trackId;
    clipRenameClipIdx_ = clipIndex;

    clipRenameLabel_->onEditorHide = [this] { handleClipRenameEditorHidden(); };

    juce::String cur = processor_.getClipName(trackId, clipIndex);
    if (cur.isEmpty())
        cur = "Clip";
    clipRenameLabel_->setText(cur, juce::dontSendNotification);

    auto nameRow = bounds.removeFromTop(juce::jmin(20, bounds.getHeight())).reduced(4, 2);
    clipRenameLabel_->setBounds(nameRow);
    clipRenameLabel_->setVisible(true);
    clipRenameLabel_->toFront(false);
    clipRenameLabel_->showEditor();
}

void ArrangementViewComponent::handleClipRenameEditorHidden()
{
    const int tid = clipRenameTrack_;
    const int cid = clipRenameClipIdx_;
    if (tid < 0 || cid < 0 || clipRenameLabel_ == nullptr)
        return;

    juce::String name = clipRenameLabel_->getText().trim();
    if (name.isEmpty())
        name = "Clip";

    clipRenameTrack_ = -1;
    clipRenameClipIdx_ = -1;

    processor_.setClipName(tid, cid, name);
    listeners_.call([tid, cid](Listener& l) { l.arrangementClipNameEdited(tid, cid); });

    clipRenameLabel_->setVisible(false);
    repaint();
}

ArrangementViewComponent::HitTestResult ArrangementViewComponent::hitTestClip(juce::Point<int> p) const
{
    HitTestResult r;
    const int trackId = getTrackIndexAtPoint(p);
    if (trackId < 0)
        return r;

    int numClips = processor_.getNumClips(trackId);
    for (int i = 0; i < numClips; ++i)
    {
        auto bounds = getClipBounds(trackId, i);
        if (bounds.isEmpty())
            continue;
        if (bounds.contains(p))
        {
            r.trackId = trackId;
            r.clipIndex = i;
            r.clipBounds = bounds;
            r.isTopEdge = (p.y - bounds.getY()) <= 6;
            return r;
        }
    }
    return r;
}

uint64_t ArrangementViewComponent::getClipCacheKey(int trackId, int clipIndex) const
{
    const uint64_t id = processor_.getClipId(trackId, clipIndex);
    if (id != 0)
        return id;

    return (static_cast<uint64_t>(static_cast<uint32_t>(trackId)) << 32) | static_cast<uint32_t>(clipIndex);
}

bool ArrangementViewComponent::buildWaveformCaches(double timeBudgetMs)
{
    if (timeBudgetMs <= 0.0)
        return false;

    std::unordered_set<uint64_t> alive;
    alive.reserve(static_cast<std::size_t>(OpenTuneAudioProcessor::MAX_TRACKS * 16));
    
    for (int trackId = 0; trackId < OpenTuneAudioProcessor::MAX_TRACKS; ++trackId)
    {
        const int numClips = processor_.getNumClips(trackId);
        for (int clipIndex = 0; clipIndex < numClips; ++clipIndex)
        {
            const uint64_t key = getClipCacheKey(trackId, clipIndex);
            alive.insert(key);
            
            auto audioBuffer = processor_.getClipAudioBuffer(trackId, clipIndex);
            if (audioBuffer)
            {
                auto& mipmap = waveformMipmapCache_.getOrCreate(key);
                mipmap.setAudioSource(audioBuffer);
            }
        }
    }

    waveformMipmapCache_.prune(alive);

    return waveformMipmapCache_.buildIncremental(timeBudgetMs);
}

void ArrangementViewComponent::paint(juce::Graphics& g)
{
    const double bpm = processor_.getBpm();
    const int timeSigNum = processor_.getTimeSigNumerator();
    const int timeSigDenom = processor_.getTimeSigDenominator();
    if (lastContextBpm_ != bpm || lastContextTimeSigNum_ != timeSigNum || lastContextTimeSigDenom_ != timeSigDenom)
    {
        timeConverter_.setContext(bpm, timeSigNum, timeSigDenom);
        lastContextBpm_ = bpm;
        lastContextTimeSigNum_ = timeSigNum;
        lastContextTimeSigDenom_ = timeSigDenom;
    }
    timeConverter_.setZoom(zoomLevel_);
    
    if (processor_.isPlaying())
        timeConverter_.setScrollOffset(static_cast<double>(smoothScrollCurrent_));
    else
        timeConverter_.setScrollOffset(static_cast<double>(scrollOffset_));

    const auto themeId = Theme::getActiveTheme();
    
    auto bounds = getLocalBounds().toFloat();
    
    if (themeId == ThemeId::DarkBlueGrey) {
        // Soothe 2 Spectrum Background Style
        // It has a specific gradient and grid look
        UIColors::fillSoothe2SpectrumBackground(g, bounds, 0.0f);
    } else {
        g.fillAll(UIColors::backgroundMedium);
    }

    {
        const juce::Graphics::ScopedSaveState viewportClip(g);
        g.reduceClipRegion(juce::Rectangle<int>(0, 0, getWidth() - kScrollbarBreadth_, getHeight() - kScrollbarBreadth_));

        drawGridLines(g);

        const int activeTrack = processor_.getActiveTrackId();
        const int trackH = processor_.getTrackHeight();
        if (activeTrack >= 0 && activeTrack < OpenTuneAudioProcessor::MAX_TRACKS && trackH > 0)
        {
            const int y = rulerHeight_ + activeTrack * trackH - verticalScrollOffset_;
            if (y + trackH > 0 && y < getHeight())
            {
                juce::Rectangle<int> row(0, y, getWidth(), trackH);
                g.setColour(UIColors::accent.withAlpha(0.10f));
                g.fillRect(row);
            }
        }

        for (int trackId = 0; trackId < OpenTuneAudioProcessor::MAX_TRACKS; ++trackId)
        {
            int numClips = processor_.getNumClips(trackId);
            for (int clipIndex = 0; clipIndex < numClips; ++clipIndex)
            {
            auto clipBounds = getClipBounds(trackId, clipIndex);
            if (clipBounds.isEmpty())
                continue;

            uint64_t clipId = processor_.getClipId(trackId, clipIndex);
            bool isSelected = (trackId == selectedTrack_ && clipIndex == selectedClip_) 
                           || isClipSelected(trackId, clipId);

            auto clipArea = clipBounds.toFloat();
            if (themeId == ThemeId::DarkBlueGrey && isSelected)
            {
                juce::ColourGradient sel(juce::Colour { 0xFFF7F3EA }, clipArea.getX(), clipArea.getBottom(),
                                         juce::Colour { 0xFFBFE0EF }, clipArea.getX(), clipArea.getY(), false);
                g.setGradientFill(sel);
                g.fillRoundedRectangle(clipArea, 6.0f);
                g.setColour(UIColors::panelBorder.withAlpha(0.55f));
                g.drawRoundedRectangle(clipArea.reduced(0.5f), 6.0f, 1.0f);
            }
            else if (themeId == ThemeId::BlueBreeze)
            {
                // Blue Breeze Clip Style - Sky Blue Gradient
                juce::Colour topColor = isSelected ? juce::Colour(BlueBreeze::Colors::ClipSelectedTop) 
                                                   : juce::Colour(BlueBreeze::Colors::ClipGradientTop);
                juce::Colour bottomColor = isSelected ? juce::Colour(BlueBreeze::Colors::ClipSelectedBottom) 
                                                      : juce::Colour(BlueBreeze::Colors::ClipGradientBottom);
                
                juce::ColourGradient grad(topColor, clipArea.getX(), clipArea.getY(),
                                          bottomColor, clipArea.getX(), clipArea.getBottom(), false);
                g.setGradientFill(grad);
                g.fillRoundedRectangle(clipArea, 6.0f);
                
                g.setColour(juce::Colour(BlueBreeze::Colors::ClipBorder));
                g.drawRoundedRectangle(clipArea.reduced(0.5f), 6.0f, 1.0f);
            }
            else if (themeId == ThemeId::Aurora)
            {
                // Aurora主题：CLIP背景色跟随轨道面板颜色（使用相同的霓虹色系）
                // 与 TrackPanelComponent 的轨道颜色循环保持一致（6色循环）
                juce::Colour trackColor;
                switch(trackId % 6) {
                    case 0: trackColor = juce::Colour(Aurora::Colors::Cyan); break;
                    case 1: trackColor = juce::Colour(Aurora::Colors::Violet); break;
                    case 2: trackColor = juce::Colour(Aurora::Colors::NeonGreen); break;
                    case 3: trackColor = juce::Colour(Aurora::Colors::Magenta); break;
                    case 4: trackColor = juce::Colour(Aurora::Colors::ElectricBlue); break;
                    case 5: trackColor = juce::Colour(Aurora::Colors::Warning); break;
                }
                
                if (isSelected)
                {
                    // 选中状态：使用更深的颜色
                    g.setColour(trackColor.withAlpha(0.45f));
                    g.fillRoundedRectangle(clipArea, 6.0f);
                    // 选中边框使用霓虹蓝色
                    g.setColour(juce::Colour(Aurora::Colors::Cyan));
                    g.drawRoundedRectangle(clipArea.reduced(0.5f), 6.0f, 2.0f);
                }
                else
                {
                    // 正常状态：使用与轨道背景相同的颜色，透明度稍高使CLIP更明显
                    g.setColour(trackColor.withAlpha(0.30f));
                    g.fillRoundedRectangle(clipArea, 6.0f);
                    g.setColour(trackColor.withAlpha(0.6f));
                    g.drawRoundedRectangle(clipArea.reduced(0.5f), 6.0f, 1.0f);
                }
            }
            else
            {
                juce::Colour fill = isSelected ? UIColors::primaryPurple : UIColors::buttonNormal;
                g.setColour(fill);
                g.fillRoundedRectangle(clipArea, 6.0f);
                g.setColour(UIColors::panelBorder);
                g.drawRoundedRectangle(clipArea.reduced(0.5f), 6.0f, 1.0f);
            }

    std::shared_ptr<const juce::AudioBuffer<float>> audioBuffer =
        processor_.getClipAudioBuffer(trackId, clipIndex);
    float gain = processor_.getClipGain(trackId, clipIndex);

    if (audioBuffer != nullptr)
    {
        const uint64_t key = getClipCacheKey(trackId, clipIndex);
        auto& mipmap = waveformMipmapCache_.getOrCreate(key);
        mipmap.setAudioSource(audioBuffer);
        
        auto waveformBounds = clipBounds.reduced(6, 6);

                // Aurora主题使用更亮的波形颜色，其他主题使用深灰色
                if (themeId == ThemeId::Aurora)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.85f));
                }
                else
                {
                    g.setColour(juce::Colour(0xFF3E4652).withAlpha(0.85f));
                }

                // 使用MIP-map渲染波形
                const double pixelsPerSecond = 100.0 * zoomLevel_;
                const int levelIndex = mipmap.selectBestLevelIndex(pixelsPerSecond);
                const auto& level = mipmap.getLevel(levelIndex);
                
                if (!level.peaks.empty())
                {
                    const float midY = static_cast<float>(waveformBounds.getCentreY());
                    const float halfH = waveformBounds.getHeight() * 0.45f;
                    const int x0 = waveformBounds.getX();
                    const int clipWidth = waveformBounds.getWidth();
                    
                    const double startSeconds = processor_.getClipStartSeconds(trackId, clipIndex);
                    const int samplesPerPeak = WaveformMipmap::kSamplesPerPeak[levelIndex];
                    const double timePerPeak = static_cast<double>(samplesPerPeak) / WaveformMipmap::kBaseSampleRate;
                    
                    const int64_t numPeaks = static_cast<int64_t>(level.peaks.size());
                    const int64_t builtPeaks = level.complete ? numPeaks : level.buildProgress;
                    
                    juce::Path waveformPath;
                    
                    for (int x = 0; x < clipWidth; ++x)
                    {
                        const double absTime = xToTime(x0 + x);
                        const double relativeTime = absTime - startSeconds;
                        
                        if (relativeTime < 0.0)
                            continue;
                        
                        const int64_t peakIndex = static_cast<int64_t>(relativeTime / timePerPeak);
                        
                        if (peakIndex < 0 || peakIndex >= builtPeaks)
                            continue;
                        
                        const auto& peak = level.peaks[static_cast<std::size_t>(peakIndex)];
                        const float magnitude = peak.getMagnitude() * gain;
                        float displayHeight = magnitude * halfH * 2.0f;
                        
                        if (magnitude > 0.0001f)
                            displayHeight = juce::jmax(displayHeight, 2.0f);

                        const float y1 = midY - displayHeight * 0.5f;
                        const float y2 = midY + displayHeight * 0.5f;
                        
                        waveformPath.startNewSubPath(static_cast<float>(x0 + x), y1);
                        waveformPath.lineTo(static_cast<float>(x0 + x), y2);
                    }
                    
                    if (!waveformPath.isEmpty())
                        g.strokePath(waveformPath, juce::PathStrokeType(1.0f));
                }
            }

            // 在CLIP左上角显示文件名（小字体）
            juce::String clipName = processor_.getClipName(trackId, clipIndex);
            if (clipName.isNotEmpty())
            {
                // 截断过长的文件名
                if (clipName.length() > 20)
                    clipName = clipName.substring(0, 17) + "...";
                
                g.setColour(UIColors::textPrimary.withAlpha(0.85f));
                g.setFont(UIColors::getUIFont(10.0f));  // 小字体
                g.drawText(clipName, clipBounds.reduced(6, 4), juce::Justification::topLeft);
            }

            // 在右上角显示增益值
            float db = 0.0f;
            if (gain > 0.0001f)
                db = 20.0f * std::log10(gain);
            else
                db = -100.0f;

            juce::String gainStr;
            if (db > -90.0f)
                gainStr = (db >= 0 ? "+" : "") + juce::String(db, 1) + " dB";
            else
                gainStr = "-inf dB";

            g.setColour(UIColors::textSecondary.withAlpha(0.9f));
            g.setFont(UIColors::getUIFont(11.0f));
            g.drawText(gainStr, clipBounds.reduced(6, 4), juce::Justification::topRight);
            }
        }
    }

    drawTimeRuler(g);

}

#if JUCE_DEBUG
bool ArrangementViewComponent::runDebugSelfTest()
{
    juce::AudioBuffer<float> audio(2, 4096);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        float* w = audio.getWritePointer(ch);
        const float v = (ch == 0) ? 0.5f : -0.25f;
        for (int i = 0; i < audio.getNumSamples(); ++i)
            w[i] = v;
    }

    WaveformMipmap mipmap;
    auto sharedAudio = std::make_shared<const juce::AudioBuffer<float>>(audio);
    mipmap.setAudioSource(sharedAudio);
    
    if (!mipmap.hasSource())
        return false;
    if (mipmap.getNumSamples() != 4096)
        return false;

    int guard = 0;
    while (!mipmap.isComplete() && guard < 10000)
    {
        if (!mipmap.buildIncremental(0.25))
            break;
        ++guard;
    }

    if (!mipmap.isComplete())
        return false;
    
    // 测试层级选择
    const auto& level = mipmap.selectBestLevel(100.0);
    if (level.peaks.empty())
        return false;

    // 测试WaveformMipmapCache
    WaveformMipmapCache cache;
    auto& m1 = cache.getOrCreate(1);
    auto& m2 = cache.getOrCreate(2);
    m1.setAudioSource(sharedAudio);
    m2.setAudioSource(sharedAudio);
    
    std::unordered_set<uint64_t> alive;
    alive.insert(2u);
    cache.prune(alive);
    
    if (cache.get(1u) != nullptr)
        return false;
    if (cache.get(2u) == nullptr)
        return false;

    return true;
}
#endif

void ArrangementViewComponent::drawTimeRuler(juce::Graphics& g)
{
    const auto themeId = Theme::getActiveTheme();
    auto bounds = getLocalBounds();
    auto rulerArea = bounds.removeFromTop(rulerHeight_);
    
    g.setColour(UIColors::backgroundMedium);
    g.fillRect(rulerArea);

    g.setColour(themeId == ThemeId::DarkBlueGrey ? UIColors::panelBorder.withAlpha(0.18f) : UIColors::panelBorder);
    g.drawLine(0.0f, static_cast<float>(rulerHeight_), static_cast<float>(getWidth()), static_cast<float>(rulerHeight_), 1.0f);

    double sr = processor_.getSampleRate();
    if (sr <= 0.0)
        sr = 44100.0;
    
    // Switch between Seconds and Bars based on timeUnit_
    if (timeUnit_ == TimeUnit::Bars)
    {
        double bpm = processor_.getBpm();
        if (bpm <= 0.0) bpm = 120.0;
        
        // Calculate pixels per beat
        double pixelsPerSecond = 100.0 * zoomLevel_;
        double secondsPerBeat = 60.0 / bpm;
        double pixelsPerBeat = pixelsPerSecond * secondsPerBeat;
        
        // Determine interval (in beats) based on density
        // We want at least ~40 pixels between labels
        double beatInterval = 1.0;
        if (pixelsPerBeat < 40.0) beatInterval = 4.0;       // Every measure
        if (pixelsPerBeat < 10.0) beatInterval = 8.0;       // Every 2 measures
        if (pixelsPerBeat < 5.0) beatInterval = 16.0;       // Every 4 measures
        if (pixelsPerBeat < 2.5) beatInterval = 32.0;       // Every 8 measures
        
        // Convert visible range to beats
        double startTime = xToTime(0);
        double endTime = xToTime(getWidth());
        
        int64_t startBeat = static_cast<int64_t>(startTime / secondsPerBeat);
        if (startBeat < 0) startBeat = 0;
        // Align to interval
        startBeat = (startBeat / (int64_t)beatInterval) * (int64_t)beatInterval;
        
        int64_t endBeat = static_cast<int64_t>(endTime / secondsPerBeat) + 1;
        
        g.setFont(UIColors::getUIFont(13.0f));
        
        for (int64_t beat = startBeat; beat <= endBeat; beat += (int64_t)beatInterval)
        {
            double time = beat * secondsPerBeat;
            int pixelX = timeToX(time);
            
            // Draw tick
            g.setColour(themeId == ThemeId::DarkBlueGrey ? UIColors::gridLine.withAlpha(0.10f) : UIColors::gridLine);
            g.drawLine(static_cast<float>(pixelX), static_cast<float>(rulerHeight_ - 10),
                       static_cast<float>(pixelX), static_cast<float>(rulerHeight_), 1.0f);
            
            // Draw label (Bar:Beat) -> actually just Bar number usually for overview
            // Let's show Bar number (1-based)
            int64_t bar = (beat / 4) + 1;
            int64_t beatInBar = (beat % 4) + 1;
            
            juce::String label;
            if (beatInterval >= 4.0)
                label = juce::String(bar);
            else
                label = juce::String::formatted("%lld.%lld", (long long) bar, (long long) beatInBar);
            
            g.setColour(UIColors::textSecondary);
            g.drawText(label, pixelX - 20, 2, 40, rulerHeight_ - 12, juce::Justification::centred);
        }
    }
    else
    {
        // Seconds mode
        double pixelsPerSecond = 100.0 * zoomLevel_;
        // double secondsPerPixel = 1.0 / pixelsPerSecond;
        
        // Adaptive interval logic based on user request to omit small ticks when zoomed out
        // User wants: only show 5s, 10s etc when dense.
        // Let's check pixel spacing for labels.
        
        double markerInterval = 1.0;
        
        // If 1s takes less than 40px, switch to 5s
        if (pixelsPerSecond < 40.0) markerInterval = 5.0;
        
        // If 5s takes less than 40px (pixelsPerSecond < 8), switch to 10s
        if (pixelsPerSecond < 8.0) markerInterval = 10.0;
        
        // If 10s takes less than 40px (pixelsPerSecond < 4), switch to 30s or 60s
        if (pixelsPerSecond < 4.0) markerInterval = 30.0;
        
        // If 30s takes less than 40px (pixelsPerSecond < 1.33), switch to 60s
        if (pixelsPerSecond < 1.33) markerInterval = 60.0;

        double startTime = xToTime(0);
        if (startTime < 0.0) startTime = 0.0;
        startTime = std::floor(startTime / markerInterval) * markerInterval;

        double endTime = xToTime(getWidth());

        g.setFont(UIColors::getUIFont(13.0f));
        for (double time = startTime; time < endTime; time += markerInterval) {
            int pixelX = timeToX(time);
            
            g.setColour(themeId == ThemeId::DarkBlueGrey ? UIColors::gridLine.withAlpha(0.10f) : UIColors::gridLine);
            g.drawLine(static_cast<float>(pixelX), static_cast<float>(rulerHeight_ - 10),
                       static_cast<float>(pixelX), static_cast<float>(rulerHeight_), 1.0f);

            int totalSecs = static_cast<int>(time);
            int mins = totalSecs / 60;
            int secs = totalSecs % 60;
            juce::String timeStr = juce::String::formatted("%d:%02d", mins, secs);

            g.setColour(UIColors::textSecondary);
            g.drawText(timeStr, pixelX - 20, 2, 40, rulerHeight_ - 12, juce::Justification::centred);
        }
    }

}

void ArrangementViewComponent::drawGridLines(juce::Graphics& g)
{
    const auto themeId = Theme::getActiveTheme();
    double sr = processor_.getSampleRate();
    if (sr <= 0.0)
        sr = 44100.0;
    
    // Switch between Seconds and Bars based on timeUnit_ (match drawTimeRuler logic)
    if (timeUnit_ == TimeUnit::Bars)
    {
        double bpm = processor_.getBpm();
        if (bpm <= 0.0) bpm = 120.0;
        
        double pixelsPerSecond = 100.0 * zoomLevel_;
        double secondsPerBeat = 60.0 / bpm;
        double pixelsPerBeat = pixelsPerSecond * secondsPerBeat;
        
        double beatInterval = 1.0;
        if (pixelsPerBeat < 40.0) beatInterval = 4.0;
        if (pixelsPerBeat < 10.0) beatInterval = 8.0;
        if (pixelsPerBeat < 5.0) beatInterval = 16.0;
        if (pixelsPerBeat < 2.5) beatInterval = 32.0;
        
        double startTime = xToTime(0);
        double endTime = xToTime(getWidth());
        
        int64_t startBeat = static_cast<int64_t>(startTime / secondsPerBeat);
        if (startBeat < 0) startBeat = 0;
        startBeat = (startBeat / (int64_t)beatInterval) * (int64_t)beatInterval;
        
        int64_t endBeat = static_cast<int64_t>(endTime / secondsPerBeat) + 1;
        
        // Safety limit
        if (endBeat - startBeat > 2000) endBeat = startBeat + 2000;

        for (int64_t beat = startBeat; beat <= endBeat; beat += (int64_t)beatInterval)
        {
            double time = beat * secondsPerBeat;
            int pixelX = timeToX(time);
            
            // Only draw lines that are visible and within bounds
            // timeToX already handles scroll offset
            if (pixelX < -2 || pixelX > getWidth() + 2) continue; // Allow slight margin
            
            // Determine if this is a major measure line
            // If beatInterval >= 4 (measures), all are measure lines
            // If beatInterval < 4, only multiples of 4 are measures
            bool isMeasure = false;
            if (beatInterval >= 4.0) {
                isMeasure = true; 
            } else {
                isMeasure = (beat % 4) == 0;
            }

            if (themeId == ThemeId::DarkBlueGrey)
            {
                g.setColour(UIColors::panelBorder.withAlpha(0.12f));
                g.drawVerticalLine(pixelX, 0.0f, static_cast<float>(getHeight()));
            }
            else
            {
                if (isMeasure) {
                    g.setColour(UIColors::panelBorder.brighter(0.3f));
                    g.drawVerticalLine(pixelX, 0.0f, static_cast<float>(getHeight()));
                } else {
                    g.setColour(UIColors::panelBorder.withAlpha(0.25f));
                    g.drawVerticalLine(pixelX, 0.0f, static_cast<float>(getHeight()));
                }
            }
        }
    }
    else
    {
        // Seconds mode
        double pixelsPerSecond = 100.0 * zoomLevel_;
        // double secondsPerPixel = 1.0 / pixelsPerSecond;
        
        double markerInterval = 1.0;
        if (pixelsPerSecond < 40.0) markerInterval = 5.0;
        if (pixelsPerSecond < 8.0) markerInterval = 10.0;
        if (pixelsPerSecond < 4.0) markerInterval = 30.0;
        if (pixelsPerSecond < 1.33) markerInterval = 60.0;

        double startTime = xToTime(0);
        if (startTime < 0.0) startTime = 0.0;
        startTime = std::floor(startTime / markerInterval) * markerInterval;

        double endTime = xToTime(getWidth());

        // Safety limit: prevent infinite loop if markerInterval is somehow 0
        if (markerInterval < 0.001) markerInterval = 1.0;

        for (double time = startTime; time < endTime + markerInterval; time += markerInterval) {
            int pixelX = timeToX(time);
            
            if (pixelX < -2 || pixelX > getWidth() + 2) continue;

            g.setColour(themeId == ThemeId::DarkBlueGrey ? UIColors::panelBorder.withAlpha(0.12f) : UIColors::panelBorder.withAlpha(0.25f));
            g.drawVerticalLine(pixelX, 0.0f, static_cast<float>(getHeight()));
        }
    }
}


} // namespace OpenTune
