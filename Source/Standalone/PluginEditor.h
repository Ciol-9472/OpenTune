#pragma once

/**
 * OpenTune 主编辑器窗口
 * 
 * 实现 JUCE AudioProcessorEditor 接口，作为插件的主 UI 界面。
 * 负责协调各个 UI 组件、处理用户交互、管理异步任务等。
 */

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <mutex>
#include "PluginProcessor.h"
#include "UI/ToolIds.h"
#include "UI/ParameterPanel.h"
#include "UI/PianoRollComponent.h"
#include "UI/MenuBarComponent.h"
#include "UI/TransportBarComponent.h"
#include "UI/TopBarComponent.h"
#include "UI/TrackPanelComponent.h"
#include "UI/ArrangementViewComponent.h"
#include "UI/OpenTuneLookAndFeel.h"
#include "UI/AuroraLookAndFeel.h"
#include "UI/UIColors.h"
#include "UI/AutoRenderOverlayComponent.h"
#include "Utils/RecentProjectsManager.h"
#include "Utils/LocalizationManager.h"
#include "Utils/PitchControlConfig.h"
#include "Audio/AsyncAudioLoader.h"
#include "Services/F0ExtractionService.h"

namespace OpenTune {

/** Vertical drag bar between arrangement timeline and piano roll. */
class MainWorkspaceSplitterBar : public juce::Component
{
public:
    MainWorkspaceSplitterBar() = default;

    std::function<void(int deltaY)> onDragDelta;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    int lastScreenY_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWorkspaceSplitterBar)
};

class OpenTuneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public ParameterPanel::Listener,
                                      public MenuBarComponent::Listener,
                                      public TransportBarComponent::Listener,
                                      public TrackPanelComponent::Listener,
                                      public ArrangementViewComponent::Listener,
                                      public PianoRollComponent::Listener,
                                      public juce::FileDragAndDropTarget,
                                      public LanguageChangeListener,  // 语言变化监听
                                      private juce::Timer
{
public:
    explicit OpenTuneAudioProcessorEditor(OpenTuneAudioProcessor&);
    ~OpenTuneAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // ParameterPanel::Listener
    void retuneSpeedChanged(float speed) override;
    void vibratoDepthChanged(float value) override;
    void vibratoRateChanged(float value) override;
    void noteSplitChanged(float value) override;
    void toolSelected(int toolId) override;
    void parameterDragEnded(int paramId, float oldValue, float newValue) override;

    // MenuBarComponent::Listener
    void importAudioRequested() override;  // 新版本：不再需要trackId参数
    void exportAudioRequested(MenuBarComponent::ExportType exportType) override;  // 使用ExportType枚举
    void exportStemsRequested() override;
    void newProjectRequested() override;
    void saveProjectRequested() override;
    void loadProjectRequested() override;
    void recentProjectOpenRequested(const juce::File& file) override;
    void preferencesRequested() override;
    void helpRequested() override;
    void showWaveformToggled(bool shouldShow) override;
    void showLanesToggled(bool shouldShow) override;
    void themeChanged(ThemeId themeId) override;
    void mouseTrailThemeChanged(MouseTrailConfig::TrailTheme theme) override;
    void undoRequested() override;
    void redoRequested() override;
    void noteNameModeChanged(int mode) override;
    void showNoteBlockNoteNamesToggled(bool shouldShow) override;

    // TransportBarComponent::Listener
    void playRequested() override;
    void pauseRequested() override;
    void stopRequested() override;
    void loopToggled(bool enabled) override;
    void bpmChanged(double newBpm) override;
    void scaleChanged(int rootNote, int scaleType) override;

    // TrackPanelComponent::Listener
    void trackSelected(int trackId) override;
    void trackMuteToggled(int trackId, bool muted) override;
    void trackSoloToggled(int trackId, bool solo) override;
    void trackVolumeChanged(int trackId, float volume) override;
    void trackHeightChanged(int newHeight) override;  // 与 ArrangementView 的 trackHeight 同步
    void arrangementHorizontalPanWheel(float deltaX, float deltaY) override;
    void arrangementTrackHeightWheel(float deltaY) override;
    void arrangementAltCtrlWheel(float deltaY) override;

    // ArrangementViewComponent::Listener
    void clipSelectionChanged(int trackId, int clipIndex) override;
    void clipTimingChanged(int trackId, int clipIndex) override;
    void clipDoubleClicked(int trackId, int clipIndex) override;
    void arrangementClipContextMenu(int trackId, int clipIndex, juce::Point<int> screenPos) override;
    void verticalScrollChanged(int newOffset) override;
    // trackHeightChanged已在TrackPanelComponent::Listener中声明

    // PianoRollComponent::Listener
    void playheadPositionChangeRequested(double timeSeconds) override;
    void playPauseToggleRequested() override;
    void stopPlaybackRequested() override;
    void autoTuneRequested() override;
    void autoTuneOptionsRequested() override;
    void pitchCurveEdited(int startFrame, int endFrame) override;
    void trackTimeOffsetChanged(int trackId, double newOffset) override;
    void escapeKeyPressed() override;
    void toolChanged(int toolId) override;

    // Keyboard handling
    bool keyPressed(const juce::KeyPress& key) override;

    // LanguageChangeListener
    void languageChanged(Language newLanguage) override;

private:
    bool shouldAcceptUndoRedoShortcut();

    // 调式状态辅助
    static int scaleToUiScaleType(Scale scale);
    static Scale uiScaleTypeToScale(int scaleType);
    static DetectedKey makeDetectedKeyFromUi(int rootNote, int scaleType, float confidence = 1.0f);
    DetectedKey resolveScaleForClip(int trackId, int clipIndex, juce::String* sourceOut = nullptr) const;
    void applyScaleToUi(int rootNote, int scaleType);
    void applyResolvedScaleForClip(int trackId, int clipIndex);
    void syncPianoRollFromClipSelection(int trackId, int clipIndex);

    void performScaleInferenceForClip(int trackId, int clipIndex);
    void requestOriginalF0ExtractionForImport(int trackId, int clipIndex);

    void timerCallback() override;
    void setInferenceActive(bool active);
    void syncParameterPanelFromSelection();
    void audioSettingsRequested();
    void playFromStartToggleRequested();  // 播放/暂停并回到起始位置
    void importAudioFileToTrack(int trackId, const juce::File& file);
    void processNextImportInQueue();  // 处理导入队列中的下一个文件
    void processDeferredImportPostProcessQueue();
    /** 解析拖放落点所在轨道；无法识别时返回 -1 */
    int resolveTrackIndexForAudioDrop(int editorX, int editorY) const;
    /** 菜单导入：第一个无任何 clip 的轨道；若无则扩展可见轨并仍无则返回 -1 */
    int findFirstEmptyTrackIndexForMenuImport();
    void clearAllClipsOnTrack(int trackId);
    /** 若轨道上已有片段则询问是否覆盖；replaceExisting true 时清空轨道后再导入 */
    void importAudioFileToTrackWithOverwritePrompt(int trackId, const juce::File& file, bool replaceExisting);
    void launchBackgroundUiTask(std::function<void()> task);
    void waitForBackgroundUiTasks();
    void refreshAfterUndoRedo();
    void syncUiAfterProjectLoad();
    void restorePersistedPianoRollZoomState();
    void restorePersistedWorkspaceSplitRatio();
    void restoreStandaloneWindowState();
    void persistUserUiState() const;
    void openProjectFromFileWithUiFeedback(const juce::File& file);
    void markSessionNeedsSave();
    void clearSessionNeedsSave();
    void finishNewProject();
    void quickSaveProject();
    void checkUnsavedChangesThen(std::function<void()> onProceed);
    /** 是否弹出“未保存”确认：无已保存工程文件且无片段时不提示（空白草稿） */
    bool shouldPromptForUnsavedSession() const;
    void runSaveProjectDialogThen(std::function<void()> onSavedToDisk);
    void launchStemExportFolderChooser(juce::String prefix, juce::Array<int> trackIds);
    void startStemExportWorker(juce::String prefix, juce::Array<int> trackIds, juce::File outputDir);
    void performUndoWithRangeTracking();
    void performRedoWithRangeTracking();
    int getWorkspaceUsableHeight() const;
    int resolveArrangementWorkspaceHeight(int usableHeight) const;
    double getArrangementWorkspaceSplitRatio() const;
    static bool isStandaloneWindowMaximised(const juce::ResizableWindow& window);
    static void setStandaloneWindowMaximised(juce::ResizableWindow& window, bool shouldBeMaximised);
    
    // AUTO 启动统一 helper
    void startAutoTuneAsUnifiedEdit(bool forceShowOptionsDialog = false);
    bool resolveActiveClipIndex(int trackId, uint64_t clipId, int& clipIndexOut) const;
    void applyAutoTunePromptSettingsToUi();
    void clearAutoTuneEditsForClip(int trackId, int clipIndex, uint64_t clipId);
    void executeAutoTuneForClip(int trackId, uint64_t clipId, bool clearExistingEdits);
    void showAutoTuneOptionsDialog(int trackId, uint64_t clipId, bool clearExistingEdits);

    OpenTuneAudioProcessor& processorRef_;

    // Custom LookAndFeel
    OpenTuneLookAndFeel openTuneLookAndFeel_;
    AuroraLookAndFeel auroraLookAndFeel_;
    juce::MouseCursor techCursor_;

    // Main GUI Components
    MenuBarComponent menuBar_;
    TransportBarComponent transportBar_;
    TopBarComponent topBar_;
    TrackPanelComponent trackPanel_;
    ParameterPanel parameterPanel_;
    ArrangementViewComponent arrangementView_;
    MainWorkspaceSplitterBar workspaceSplitter_;
    PianoRollComponent pianoRoll_;
    AutoRenderOverlayComponent autoRenderOverlay_;

    RecentProjectsManager recentProjects_;

    // Async loaders
    AsyncAudioLoader asyncAudioLoader_;
    F0ExtractionService f0ExtractionService_{2, 64};
    bool isImportInProgress_ = false;
    
    // 多文件导入队列（解决并发导入问题）
    struct PendingImport {
        int trackId;
        juce::File file;
    };
    std::vector<PendingImport> importQueue_;

    struct DeferredImportPostProcessRequest {
        int trackId{-1};
        uint64_t clipId{0};
    };
    std::vector<DeferredImportPostProcessRequest> deferredImportPostProcessQueue_;
    
    /** -1 = derive from default ratio on first layout; else pixel height of arrangement strip. */
    int arrangementWorkspaceHeight_{-1};
    double arrangementWorkspaceSplitRatio_{-1.0};
    bool suppressLinkedTimelineZoom_{false};
    bool suppressLinkedTimelineScroll_{false};

    // 现代布局：左右面板可折叠（用于“沉浸主画布”模式）
    bool isParameterPanelVisible_ = true;

    bool f0ParamsSyncedFromInference_ = false;
    bool inferenceActive_ = false;
    int inferenceActiveTickCounter_ = 0;
    int lastPianoRollSampleRate_ = 0;
    std::shared_ptr<const juce::AudioBuffer<float>> lastPianoRollBuffer_;
    bool lastPianoRollHasUserAudio_ = false;
    double lastSyncedBpm_ = 0.0;
    int lastSyncedTimeSigNum_ = 0;
    int lastSyncedTimeSigDenom_ = 0;
    bool showingSingleNoteParams_ = false;

    // Undo 状态追踪
    int lastScaleRootNote_ = 0;
    int lastScaleType_ = 1;  // 1=Major
    uint32_t lastUndoRedoShortcutMs_ = 0;
    bool suppressScaleChangedCallback_ = false;
    float autoTuneRetuneSpeedPercent_ = PitchControlConfig::kDefaultRetuneSpeedPercent;
    float autoTuneNoteSplitCents_ = PitchControlConfig::kDefaultNoteSplitCents;
    bool autoTuneSkipPrompt_ = false;
    std::array<float, OpenTuneAudioProcessor::MAX_TRACKS> lastTrackVolumes_;
    float volumeDragStartValue_ = 1.0f;
    int volumeDragTrackId_ = -1;
    
    // AUTO 阻塞事务锁：点击 AUTO 后立即 latch，直到完成才释放
    bool autoOverlayLatched_ = false;
    int autoOverlayTargetTrackId_ = -1;
    uint64_t autoOverlayTargetClipId_ = 0;

    // RMVPE OriginalF0 阻塞事务锁：提取开始后 latch，直到"提取成功且当前钢琴卷帘可见"才释放
    bool rmvpeOverlayLatched_ = false;
    int rmvpeOverlayTargetTrackId_ = -1;
    uint64_t rmvpeOverlayTargetClipId_ = 0;

    // Export worker thread management
    std::thread exportWorker_;
    std::atomic<bool> exportInProgress_{false};

    /** 最近成功保存或打开的工程文件；用于分轨导出默认前缀（未关联工程时为空） */
    juce::File sessionProjectFile_;

    /** 自上次保存/打开/新建以来是否有应写入工程的修改（用于新建前提示） */
    bool sessionNeedsSave_{false};
    /** syncUiAfterProjectLoad 等同步 UI 时不应标记为已修改 */
    bool suppressSessionNeedsSave_{false};

    // Detached-safe background tasks (import/deferred post-process)
    std::vector<std::future<void>> backgroundTasks_;

    // Layout constants
    static constexpr int MENU_BAR_HEIGHT = 25;
    static constexpr int TOP_PANEL_HEIGHT = 45;  // Single row: Scale and transport controls
    static constexpr int TRANSPORT_BAR_HEIGHT = 64; // Increased for larger buttons (was 60)
    static constexpr int TRACK_PANEL_WIDTH = 180;      // 左侧 Track Inspector (Reduced from 220)
    static constexpr int PARAMETER_PANEL_WIDTH = 240;  // 右侧 Properties Panel

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenTuneAudioProcessorEditor)
};

} // namespace OpenTune
