#pragma once

/**
 * 排列视图组件
 * 
 * 显示多轨道的音频片段排列视图，支持：
 * - 片段显示与拖拽
 * - 波形可视化
 * - 时间标尺和网格
 * - 播放头位置显示
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <set>
#include "../PluginProcessor.h"
#include "UIColors.h"
#include "TimeConverter.h"
#include "TimelineZoomScrollBar.h"
#include "../Utils/UndoAction.h"
#include "PlayheadOverlayComponent.h"
#include "WaveformMipmap.h"

namespace OpenTune {

class ArrangementViewComponent : public juce::Component,
                                 public juce::ScrollBar::Listener,
                                 public juce::Timer
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void clipSelectionChanged(int trackId, int clipIndex) = 0;
        virtual void clipTimingChanged(int trackId, int clipIndex) = 0;
        /** 片段在排列区通过内联编辑完成重命名后调用 */
        virtual void arrangementClipNameEdited(int /*trackId*/, int /*clipIndex*/) {}
        virtual void arrangementClipContextMenu(int /*trackId*/, int /*clipIndex*/, juce::Point<int> /*screenPos*/) {}
        virtual void playheadPositionChangeRequested(double timeSeconds) { juce::ignoreUnused(timeSeconds); }
        // Y轴缩放回调 - 通知外部轨道高度变化（用于同步TrackPanel）
        virtual void trackHeightChanged(int newHeight) { juce::ignoreUnused(newHeight); }
        // Y轴滚动回调 - 通知外部垂直滚动偏移变化（用于同步TrackPanel）
        virtual void verticalScrollChanged(int newOffset) { juce::ignoreUnused(newOffset); }
    };

    ArrangementViewComponent(OpenTuneAudioProcessor& processor);
    ~ArrangementViewComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void onHeartbeatTick();

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;

    void setIsPlaying(bool playing);
    /** After clip edits (split/move) or when stopping playback: align smooth scroll with committed scroll and playhead overlay. */
    void reconcileHorizontalScrollAfterEdit();
    // 设置播放头颜色（主题切换时调用）
    void setPlayheadColour(juce::Colour colour) {
        playheadOverlay_.setPlayheadColour(colour);
    }
    
    // 设置播放头位置源（由组件内部读取）
    void setPlayheadPositionSource(std::weak_ptr<std::atomic<double>> source) {
        positionSource_ = source;
    }
    void syncPlayheadPosition(double timeSeconds);
    void setZoomLevel(double zoom);
    double getZoomLevel() const { return zoomLevel_; }
    void setScrollOffset(int pixels);
    int getScrollOffset() const { return scrollOffset_; }
    double getVisibleStartTimeSeconds() const;
    double getVisibleDurationSeconds() const;
    void setVisibleStartTimeSeconds(double timeSeconds);
    int getVerticalScrollOffset() const { return verticalScrollOffset_; }
    void setVerticalScrollOffset(int offset);
    void setTimeUnitSeconds(bool useSeconds);
    bool isTimeUnitSeconds() const { return timeUnit_ == TimeUnit::Seconds; }
    void setTimelineTrackRowCountSource(std::function<int()> fn) { timelineTrackRowCountSource_ = std::move(fn); }

    /** 与 mouse 滚轮一致：水平平移时间线（Shift+滚轮） */
    void applyWheelHorizontalPan(float deltaX, float deltaY);
    /** 与 mouse 滚轮一致：轨道高度缩放（Alt+滚轮） */
    void applyWheelTrackHeightChange(float deltaY);
    /** 与 mouse 滚轮一致：横向时间缩放，anchorContentX 为排列视图本地 X（鼠标位置或视图中心） */
    void applyWheelTimelineZoom(float deltaY, int anchorContentX);
    void setInferenceActive(bool active) { inferenceActive_ = active; }
    /** Fired after user-driven horizontal timeline zoom (wheel), for syncing other views. */
    std::function<void(double zoomLevel)> onUserTimelineZoomChanged;
    std::function<void(double visibleStartTimeSeconds)> onVisibleStartTimeChanged;
    void fitToContent();
    void prioritizeWaveformBuildForClip(int trackId, uint64_t clipId);
    bool isWaveformCacheCompleteForClip(int trackId, uint64_t clipId) const;

    /** 排列视图本地坐标 → 轨道索引；点在时间标尺上方等区域时返回 -1 */
    int getTrackIndexAtPoint(juce::Point<int> localPoint) const;

    // 缩放状态管理
    void resetUserZoomFlag() { userHasManuallyZoomed_ = false; }
    bool hasUserManuallyZoomed() const { return userHasManuallyZoomed_; }

    void updateMeter(int trackId, float rmsDb);

    void addListener(Listener* listener);
    void removeListener(Listener* listener);
    void syncSelectionFromProcessor(int trackId);

    /** Edit 菜单 / 全局快捷键：由 PluginEditor 调用 */
    void cutSelectedClips();
    void copySelectedClips();
    void pasteClips();
    void deleteSelectedClips();
    void selectAllClipsInTrack(int trackId);

    /** 与轨道侧栏一致：在片段上弹出内联名称编辑（双击或菜单「重命名」） */
    void beginClipRenameForClip(int trackId, int clipIndex);

#if JUCE_DEBUG
    static bool runDebugSelfTest();
#endif

private:
    struct HitTestResult {
        int trackId{-1};
        int clipIndex{-1};
        juce::Rectangle<int> clipBounds;
        bool isTopEdge{false};
    };

    HitTestResult hitTestClip(juce::Point<int> p) const;

    void beginClipRename(int trackId, int clipIndex);
    void handleClipRenameEditorHidden();

    juce::Rectangle<int> getTrackLaneBounds(int trackId) const;
    juce::Rectangle<int> getClipBounds(int trackId, int clipIndex) const;

    /** 与 paint() 使用相同的 zoom/scroll，保证 hitTest 与屏幕绘制一致 */
    void syncTimeConverterForGeometryQueries() const;

    int timeToX(double seconds) const;
    double xToTime(int x) const;
    void updateAutoScroll();
    void onScrollVBlankCallback(double timestampSec);
    double readPlayheadTime() const;
    void updateScrollBars();
    void notifyVisibleStartTimeChanged();
    void applyScrollBarThumbResize(double thumbStartNormalized, double thumbEndNormalized);
    void applyVerticalScrollBarThumbResize(double thumbStartNormalized, double thumbEndNormalized);
    double getTimelineSpanSeconds() const;
    double getMinimumHorizontalZoomLevel() const;
    std::function<int()> timelineTrackRowCountSource_;
    int getTimelineLayoutTrackRows() const;
    void drawTimeRuler(juce::Graphics& g);
    void drawGridLines(juce::Graphics& g);

    OpenTuneAudioProcessor& processor_;
    juce::ListenerList<Listener> listeners_;

    uint64_t getClipCacheKey(int trackId, int clipIndex) const;
    bool buildWaveformCaches(double timeBudgetMs);

    WaveformMipmapCache waveformMipmapCache_;
    std::unique_ptr<juce::Label> clipRenameLabel_;
    int clipRenameTrack_{ -1 };
    int clipRenameClipIdx_{ -1 };
    double lastContextBpm_{ 0.0 };
    int lastContextTimeSigNum_{ 0 };
    int lastContextTimeSigDenom_{ 0 };

    TimelineZoomScrollBar horizontalScrollBar_{ false };
    TimelineZoomScrollBar verticalScrollBar_{ true };

    enum class TimeUnit { Seconds, Bars };
    TimeUnit timeUnit_{ TimeUnit::Seconds };

    std::atomic<bool> isPlaying_{false};  // atomic 确保 VBlank 线程安全
    double zoomLevel_{1.0};
    int scrollOffset_{0};
    int verticalScrollOffset_{0};
    mutable TimeConverter timeConverter_;
    
    // Smooth scrolling
    float smoothScrollCurrent_{0.0f}; // To track sub-pixel position for smoothness
    bool isSmoothScrolling_{false};
    double lastPaintedPlayheadTime_{-1.0};

    // 用户是否手动调整过缩放（用于避免自动缩放覆盖用户设置）
    bool userHasManuallyZoomed_ = false;

    int waveformBuildTickCounter_{ 0 }; // 播放状态下的限频计数器
    bool inferenceActive_{ false };
    uint64_t prioritizedWaveformKey_{ 0 };

    int selectedTrack_{0};
    int selectedClip_{0};
    uint64_t selectedClipId_{0};

    // === 多选支持 ===
    struct ClipSelectionKey {
        int trackId;
        uint64_t clipId;
        bool operator<(const ClipSelectionKey& other) const {
            return std::tie(trackId, clipId) < std::tie(other.trackId, other.clipId);
        }
        bool operator==(const ClipSelectionKey& other) const {
            return trackId == other.trackId && clipId == other.clipId;
        }
    };
    std::set<ClipSelectionKey> selectedClips_;
    bool isMultiSelectMode_{false};
    ClipSelectionKey shiftAnchor_;
    bool hasShiftAnchor_{false};

    // === 剪贴板 ===
    struct ClipboardClip {
        int sourceTrackId;
        OpenTune::ClipSnapshot snapshot;
        double relativeOffset;
    };
    std::vector<ClipboardClip> clipboard_;
    double clipboardReferenceTime_{0.0};

    bool isClipSelected(int trackId, uint64_t clipId) const;
    void toggleClipSelection(int trackId, uint64_t clipId, int clipIndex);
    void clearClipSelection();
    void selectClipsInRange(const ClipSelectionKey& from, const ClipSelectionKey& to);

    // === 多选拖拽状态 ===
    struct DragStartState {
        int trackId;
        uint64_t clipId;
        double startSeconds;
    };
    std::vector<DragStartState> multiDragStartStates_;

    bool isDraggingClip_{false};
    bool isAdjustingGain_{false};
    bool isDraggingPlayhead_{false};
    bool isPanning_{false};
    juce::Point<int> dragStartPos_;
    juce::Point<int> lastMousePos_;
    double dragStartClipSeconds_{0.0};
    float dragStartClipGain_{1.0f};
    uint64_t dragStartClipId_{0};
    int dragStartTrackId_{-1};  // 拖拽开始时的轨道ID（用于跨轨道移动）

    // 高性能播放头覆盖层（VBlank同步，独立于主组件重绘）
    PlayheadOverlayComponent playheadOverlay_;

    // 滚动跟随独立 VBlank 附件（仅负责滚动，不影响 Overlay 的 VBlank）
    std::unique_ptr<juce::VBlankAttachment> scrollVBlankAttachment_;

    // 播放头位置源（来自 Processor 的原子位置）
    std::weak_ptr<std::atomic<double>> positionSource_;

    static constexpr int rulerHeight_ = 30;
    static constexpr int kScrollbarBreadth_ = 15;
    static constexpr int kTrackAddButtonRegion_ = 60;
};

} // namespace OpenTune
