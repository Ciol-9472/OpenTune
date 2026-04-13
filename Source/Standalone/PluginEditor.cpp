#include "PluginEditor.h"
#include "UI/UIColors.h"
#include "UI/FrameScheduler.h"
#include "UI/OptionsDialogComponent.h"
#include "UI/StemExportDialogComponent.h"
#include "Audio/AsyncAudioLoader.h"
#include "DSP/ChromaKeyDetector.h"
#include "Utils/LastFileDialogPaths.h"
#include "Utils/PitchCurve.h"
#include "Utils/NoteGenerator.h"
#include "Utils/PitchControlConfig.h"
#include "Utils/AppLogger.h"
#include "Utils/TimeCoordinate.h"
#include "Utils/UndoAction.h"
#include "Utils/UserUiState.h"
#include "Utils/KeyShortcutConfig.h"
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <set>
#include <mutex>
#include <unordered_map>
#include <thread>
#include <future>
#include <chrono>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace OpenTune {

void MainWorkspaceSplitterBar::paint(juce::Graphics& g)
{
    g.fillAll(UIColors::backgroundMedium.darker(0.12f));
    auto r = getLocalBounds().toFloat().reduced(2.0f, 0.0f);
    g.setColour(UIColors::accent.withAlpha(0.45f));
    g.fillRoundedRectangle(r.withSizeKeepingCentre(r.getWidth(), 2.5f), 1.25f);
}

void MainWorkspaceSplitterBar::mouseDown(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    lastScreenY_ = e.getScreenY();
}

void MainWorkspaceSplitterBar::mouseDrag(const juce::MouseEvent& e)
{
    const int dy = e.getScreenY() - lastScreenY_;
    lastScreenY_ = e.getScreenY();
    if (dy != 0 && onDragDelta) {
        onDragDelta(dy);
    }
}

void MainWorkspaceSplitterBar::mouseEnter(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void MainWorkspaceSplitterBar::mouseExit(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

namespace {

constexpr int kHeartbeatHzIdle = 30;
constexpr int kHeartbeatHzInferenceActive = 10;

static void initialiseImportFormatManager(juce::AudioFormatManager& formatManager)
{
    formatManager.registerBasicFormats();
}

static juce::String getImportWildcardFilter()
{
    juce::AudioFormatManager formatManager;
    initialiseImportFormatManager(formatManager);
    const auto wildcard = formatManager.getWildcardForAllFormats();
    if (wildcard.isNotEmpty())
        return wildcard;

    // 鍏滃簳锛氬綋搴曞眰鏈繑鍥?wildcard 鏃朵粛鎻愪緵鍩虹鏍煎紡
    return "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3";
}

static juce::String getImportExtensionSpec()
{
    const auto wildcard = getImportWildcardFilter();
    juce::StringArray tokens;
    tokens.addTokens(wildcard, ";", "\"");

    juce::StringArray extensions;
    for (auto token : tokens)
    {
        token = token.trim();
        if (token.startsWith("*."))
            token = token.fromFirstOccurrenceOf("*.", false, false);
        token = token.toLowerCase();
        if (token.isNotEmpty())
            extensions.addIfNotAlreadyThere(token);
    }

    return extensions.joinIntoString(";");
}

struct RenderStatusUiState
{
    bool showRendering = false;
    int uiPendingTasks = 0;
    juce::String detailText;
};

static RenderStatusUiState buildRenderStatusUiState(bool isTxnActive)
{
    RenderStatusUiState state;
    state.showRendering = isTxnActive;
    state.uiPendingTasks = isTxnActive ? 1 : 0;

    if (state.showRendering)
    {
        state.detailText = "Rendering...";
    }

    return state;
}

} // namespace

#if JUCE_DEBUG
static bool runDebugSelfTests() {
    if (!ArrangementViewComponent::runDebugSelfTest()) {
        return false;
    }

    {
        const auto s0 = buildRenderStatusUiState(true);
        if (!s0.showRendering || s0.uiPendingTasks != 1) {
            return false;
        }
        if (!s0.detailText.contains("Rendering...")) {
            return false;
        }

        const auto s1 = buildRenderStatusUiState(false);
        if (s1.showRendering || s1.uiPendingTasks != 0) {
            return false;
        }
        if (s1.detailText.isNotEmpty()) {
            return false;
        }

    }

    {
        PitchCurve curve;
        curve.setHopSize(160);
        curve.setSampleRate(16000);
        std::vector<float> f0(200, 440.0f);
        std::vector<float> energy(200, 1.0f);
        curve.setOriginalF0(f0);
        curve.setOriginalEnergy(energy);
        constexpr int kHopSize = 160;
        constexpr double kF0SampleRate = 16000.0;
        constexpr double kHostSampleRate = 96000.0;
        NoteGeneratorParams params;
        params.policy.transitionThresholdCents = 512.0f;
        params.policy.minDurationMs = 100.0f;
        auto notes = NoteGenerator::generate(f0, energy, kHopSize, kF0SampleRate, kHostSampleRate, params);
        if (notes.empty()) {
            return false;
        }
        // Calculate expectedEnd using same logic as NoteGenerator.cpp
        // hopSecs = 160 / 16000 = 0.01 seconds per frame
        // samplesPerFrame = 0.01 * 96000 = 960 samples
        // tailExtendMs defaults to 15.0f in NoteGeneratorPolicy
        // tailExtendSamples = ceil(15ms / 1000ms / hopSecs) * samplesPerFrame = 2 * 960 = 1920
        const double hopSecs = 160.0 / 16000.0;
        const int64_t samplesPerFrame = static_cast<int64_t>(hopSecs * 96000.0);
        const double baseEndSeconds = static_cast<double>(f0.size()) * hopSecs;
        const double tailExtendSeconds = (std::ceil(params.policy.tailExtendMs / 1000.0 / hopSecs)) * hopSecs;
        const double expectedEndSeconds = baseEndSeconds + tailExtendSeconds;
        if (std::abs(notes[0].endTime - expectedEndSeconds) > 480.0 / 44100.0) {
            return false;
        }
    }

    return true;
}
#endif

int OpenTuneAudioProcessorEditor::scaleToUiScaleType(Scale scale)
{
    switch (scale) {
        case Scale::Minor: return 2;
        case Scale::Chromatic: return 3;
        case Scale::HarmonicMinor: return 4;
        case Scale::Dorian: return 5;
        case Scale::Mixolydian: return 6;
        case Scale::PentatonicMajor: return 7;
        case Scale::PentatonicMinor: return 8;
        case Scale::Major:
        default:
            return 1;
    }
}

Scale OpenTuneAudioProcessorEditor::uiScaleTypeToScale(int scaleType)
{
    switch (scaleType) {
        case 2: return Scale::Minor;
        case 3: return Scale::Chromatic;
        case 4: return Scale::HarmonicMinor;
        case 5: return Scale::Dorian;
        case 6: return Scale::Mixolydian;
        case 7: return Scale::PentatonicMajor;
        case 8: return Scale::PentatonicMinor;
        default: return Scale::Major;
    }
}

DetectedKey OpenTuneAudioProcessorEditor::makeDetectedKeyFromUi(int rootNote, int scaleType, float confidence)
{
    DetectedKey key;
    key.root = static_cast<Key>(juce::jlimit(0, 11, rootNote));
    key.scale = uiScaleTypeToScale(scaleType);
    key.confidence = confidence;
    return key;
}

DetectedKey OpenTuneAudioProcessorEditor::resolveScaleForClip(int trackId, int clipIndex, juce::String* sourceOut) const
{
    const auto defaultKey = []() {
        DetectedKey k;
        k.root = Key::C;
        k.scale = Scale::Major;
        k.confidence = 1.0f;
        return k;
    };

    const bool hasClip = (trackId >= 0
                       && trackId < OpenTuneAudioProcessor::MAX_TRACKS
                       && clipIndex >= 0
                       && clipIndex < processorRef_.getNumClips(trackId));

    if (hasClip) {
        const DetectedKey clipKey = processorRef_.getClipDetectedKey(trackId, clipIndex);
        if (clipKey.confidence > 0.0f) {
            if (sourceOut) *sourceOut = "clip";
            return clipKey;
        }
    }

    if (sourceOut) *sourceOut = "default";
    return defaultKey();
}

void OpenTuneAudioProcessorEditor::applyScaleToUi(int rootNote, int scaleType)
{
    const int clampedRoot = juce::jlimit(0, 11, rootNote);
    const int clampedType = juce::jlimit(1, 8, scaleType);

    suppressScaleChangedCallback_ = true;
    transportBar_.setScale(clampedRoot, clampedType);
    suppressScaleChangedCallback_ = false;

    pianoRoll_.setScale(clampedRoot, clampedType);
    lastScaleRootNote_ = clampedRoot;
    lastScaleType_ = clampedType;
}

void OpenTuneAudioProcessorEditor::applyResolvedScaleForClip(int trackId, int clipIndex)
{
    juce::String source;
    const DetectedKey key = resolveScaleForClip(trackId, clipIndex, &source);
    const int rootNote = static_cast<int>(key.root);
    const int scaleType = scaleToUiScaleType(key.scale);
    applyScaleToUi(rootNote, scaleType);

    const uint64_t clipId = (trackId >= 0 && clipIndex >= 0) ? processorRef_.getClipId(trackId, clipIndex) : 0;
    (void)clipId;
    DBG("ScaleSyncTrace: source=" + source
        + " trackId=" + juce::String(trackId)
        + " clipIndex=" + juce::String(clipIndex)
        + " clipId=" + juce::String(static_cast<juce::int64>(clipId))
        + " root=" + juce::String(rootNote)
        + " scale=" + juce::String(scaleType));
}

void OpenTuneAudioProcessorEditor::setInferenceActive(bool active)
{
    if (inferenceActive_ == active)
        return;

    inferenceActive_ = active;
    inferenceActiveTickCounter_ = 0;

    startTimerHz(inferenceActive_ ? kHeartbeatHzInferenceActive : kHeartbeatHzIdle);
    arrangementView_.setInferenceActive(inferenceActive_);
    pianoRoll_.setInferenceActive(inferenceActive_);
    trackPanel_.setInferenceActive(inferenceActive_);
}

OpenTuneAudioProcessorEditor::OpenTuneAudioProcessorEditor(OpenTuneAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef_(p), menuBar_(p), topBar_(menuBar_, transportBar_), arrangementView_(p)
{
    // Initialize track volumes array
    lastTrackVolumes_.fill(1.0f);
    
    // Hide original menu bar as we moved it to TransportBar
    menuBar_.setVisible(false);

    // Set larger default size for the complete UI (increased height for menu bar)
    setResizable(true, true);
    // Max dimensions must exceed typical desktop work areas or OS maximize is capped (e.g. 4K).
    setResizeLimits(1000, 700, 32000, 32000);
    setSize(1200, 900);

    // Apply custom LookAndFeel globally
    setLookAndFeel(&openTuneLookAndFeel_);
    UIColors::applyTheme(Theme::getActiveTokens());
    openTuneLookAndFeel_.setColour(juce::TextButton::buttonColourId, UIColors::buttonNormal);
    openTuneLookAndFeel_.setColour(juce::TextButton::buttonOnColourId, UIColors::accent);
    openTuneLookAndFeel_.setColour(juce::TextButton::textColourOffId, UIColors::textPrimary);
    openTuneLookAndFeel_.setColour(juce::TextButton::textColourOnId, UIColors::textPrimary);

    // Create Tech Cursor
    juce::Image cursorImg(juce::Image::ARGB, 32, 32, true);
    juce::Graphics g(cursorImg);
    g.setColour(UIColors::accent);
    g.drawLine(16.0f, 4.0f, 16.0f, 28.0f, 2.0f);
    g.drawLine(4.0f, 16.0f, 28.0f, 16.0f, 2.0f);
    g.drawEllipse(10.0f, 10.0f, 12.0f, 12.0f, 2.0f);
    g.fillEllipse(14.0f, 14.0f, 4.0f, 4.0f);
    techCursor_ = juce::MouseCursor(cursorImg, 16, 16);
    
    // Setup Menu Bar
    menuBar_.addListener(this);
    menuBar_.setRecentProjectsManager(&recentProjects_);
    processorRef_.getUndoManager().setStackChangeCallback([this]() { markSessionNeedsSave(); });

#if JUCE_MAC
    // Populate the macOS system menu bar with File/Edit/View menus.
    // JUCE automatically adds "About OpenTune" and "Quit OpenTune" to the app menu.
    juce::MenuBarModel::setMacMainMenu(&menuBar_);
#endif

    // Register language change listener
    LocalizationManager::getInstance().addListener(this);

    // Setup Transport Bar Menu Callbacks
    // menuName 浠庤繍琛屾椂鑾峰彇锛堣瑷€鍒囨崲鍚庤嚜鍔ㄥ弽鏄犲綋鍓嶈瑷€锛夛紝涓?getMenuForIndex 鐨勭储寮曞尮閰?
    transportBar_.onFileMenuRequested = [this]() {
        auto menuNames = menuBar_.getMenuBarNames();
        juce::PopupMenu menu = menuBar_.getMenuForIndex(0, menuNames.isEmpty() ? juce::String() : menuNames[0]);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&transportBar_.getFileButton())
                                                     .withParentComponent(this),
                           [this](int result) {
                               if (result != 0) menuBar_.menuItemSelected(result, 0);
                           });
    };
    transportBar_.onEditMenuRequested = [this]() {
        auto menuNames = menuBar_.getMenuBarNames();
        juce::PopupMenu menu = menuBar_.getMenuForIndex(1, menuNames.size() > 1 ? menuNames[1] : juce::String());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&transportBar_.getEditButton())
                                                     .withParentComponent(this),
                           [this](int result) {
                               if (result != 0) menuBar_.menuItemSelected(result, 1);
                           });
    };
    transportBar_.onViewMenuRequested = [this]() {
        auto menuNames = menuBar_.getMenuBarNames();
        juce::PopupMenu menu = menuBar_.getMenuForIndex(2, menuNames.size() > 2 ? menuNames[2] : juce::String());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&transportBar_.getViewButton())
                                                     .withParentComponent(this),
                           [this](int result) {
                               if (result != 0) menuBar_.menuItemSelected(result, 2);
                           });
    };

#if JUCE_DEBUG
    static std::atomic<bool> ran{ false };
    if (!ran.exchange(true)) {
        const bool ok = runDebugSelfTests();
        if (!ok) {
            AppLogger::log("Debug self-tests failed");
            jassertfalse;
        }
        const char* selfTestEnv = std::getenv("OPENTUNE_SELFTEST");
        if (selfTestEnv != nullptr && std::strcmp(selfTestEnv, "1") == 0) {
            std::exit(ok ? 0 : 1);
        }
    }
#endif

    // Setup Transport Bar (includes Scale controls)
    transportBar_.addListener(this);
    transportBar_.setPlaying(processorRef_.isPlaying());
    transportBar_.setLooping(processorRef_.isLoopEnabled());
    transportBar_.setBpm(processorRef_.getBpm());

    // Initialize Scale (clip > recent > default)
    {
        const int initTrack = processorRef_.getActiveTrackId();
        const int initClip = processorRef_.getSelectedClip(initTrack);
        applyResolvedScaleForClip(initTrack, initClip);
    }

    addAndMakeVisible(topBar_);

    // 椤堕儴鏉★細渚ц竟鏍忔姌鍙犲紑鍏?
    topBar_.onToggleParameterPanel = [this]() {
        isParameterPanelVisible_ = !isParameterPanelVisible_;
        parameterPanel_.setVisible(isParameterPanelVisible_);
        topBar_.setParameterPanelToggleState(isParameterPanelVisible_);
        resized();
        repaint();
    };

    topBar_.setParameterPanelToggleState(isParameterPanelVisible_);

    trackPanel_.addListener(this);
    trackPanel_.setActiveTrack(processorRef_.getActiveTrackId());
    // 鍒濆鍖栬建閬撻珮搴︼紙涓嶢rrangementView鍚屾锛?
    trackPanel_.setTrackHeight(processorRef_.getTrackHeight());
    // 鍒濆鍖栨墍鏈?2鏉¤建閬撶殑鐘舵€?
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        trackPanel_.setTrackMuted(i, processorRef_.isTrackMuted(i));
        trackPanel_.setTrackSolo(i, processorRef_.isTrackSolo(i));
        trackPanel_.setTrackVolume(i, processorRef_.getTrackVolume(i));
    }
    addAndMakeVisible(trackPanel_);

    // Setup Parameter Panel
    parameterPanel_.addListener(this);
    // parameterPanel_.setRetuneSpeed(processorRef_.getRetuneSpeed());
    parameterPanel_.setRetuneSpeed(PitchControlConfig::kDefaultRetuneSpeedPercent);
    pianoRoll_.setRetuneSpeed(PitchControlConfig::kDefaultRetuneSpeedNormalized);
    parameterPanel_.setNoteSplit(PitchControlConfig::kDefaultNoteSplitCents);
    pianoRoll_.setNoteSplit(PitchControlConfig::kDefaultNoteSplitCents);
    
    parameterPanel_.setF0Min(30.0f);
    parameterPanel_.setF0Max(2000.0f);
    
    addAndMakeVisible(parameterPanel_);

    arrangementView_.addListener(this);
    arrangementView_.setZoomLevel(processorRef_.getZoomLevel());
    addAndMakeVisible(arrangementView_);

    {
        auto timelineRowsFn = [this]() {
            int used = 0;
            for (int t = 0; t < OpenTuneAudioProcessor::MAX_TRACKS; ++t) {
                if (processorRef_.getNumClips(t) > 0) {
                    used = t + 1;
                }
            }
            return juce::jmax(trackPanel_.getVisibleTrackCount(), used);
        };
        trackPanel_.setTimelineTrackRowCountSource(timelineRowsFn);
    }

    addAndMakeVisible(workspaceSplitter_);
    workspaceSplitter_.onDragDelta = [this](int dy) {
        if (dy == 0) {
            return;
        }
        constexpr int kMinArr = 100;
        constexpr int kMinPiano = 160;
        const int usable = getWorkspaceUsableHeight();
        if (usable < kMinArr + kMinPiano) {
            return;
        }
        if (arrangementWorkspaceHeight_ < 0) {
            arrangementWorkspaceHeight_ = resolveArrangementWorkspaceHeight(usable);
        }
        arrangementWorkspaceHeight_ =
            juce::jlimit(kMinArr, usable - kMinPiano, arrangementWorkspaceHeight_ + dy);
        arrangementWorkspaceSplitRatio_ =
            static_cast<double>(arrangementWorkspaceHeight_) / static_cast<double>(usable);
        resized();
    };

    arrangementView_.onUserTimelineZoomChanged = [this](double z) {
        if (suppressLinkedTimelineZoom_) {
            return;
        }
        suppressLinkedTimelineZoom_ = true;
        processorRef_.setZoomLevel(z);
        pianoRoll_.setZoomLevel(z);
        suppressLinkedTimelineZoom_ = false;
    };

    // Setup Piano Roll (main editor area)
    pianoRoll_.addListener(this);
    pianoRoll_.setExternalToolSelectionHandler([this](int toolId) { toolSelected(toolId); });
    pianoRoll_.setRenderCompleteCallback([this]() {
        autoRenderOverlay_.setVisible(false);
    });
    pianoRoll_.setGlobalUndoManager(&processorRef_.getUndoManager());
    pianoRoll_.setProcessor(&processorRef_);
    int activeTrack = processorRef_.getActiveTrackId();
    int clipIndex = processorRef_.getSelectedClip(activeTrack);
    pianoRoll_.setCurrentClipContext(activeTrack, processorRef_.getClipId(activeTrack, clipIndex));
    std::shared_ptr<const juce::AudioBuffer<float>> clipBuffer =
        processorRef_.getClipAudioBuffer(activeTrack, clipIndex);
    if (clipBuffer)
        pianoRoll_.setAudioBuffer(clipBuffer, static_cast<int>(processorRef_.getSampleRate()));
    pianoRoll_.setBpm(processorRef_.getBpm());
    pianoRoll_.setTimeSignature(processorRef_.getTimeSigNumerator(), processorRef_.getTimeSigDenominator());
    pianoRoll_.setShowWaveform(processorRef_.getShowWaveform());
    pianoRoll_.setShowLanes(processorRef_.getShowLanes());
    pianoRoll_.setZoomLevel(processorRef_.getZoomLevel());
    restorePersistedPianoRollZoomState();
    restorePersistedWorkspaceSplitRatio();

    pianoRoll_.onUserTimelineZoomChanged = [this](double z) {
        if (suppressLinkedTimelineZoom_) {
            return;
        }
        suppressLinkedTimelineZoom_ = true;
        processorRef_.setZoomLevel(z);
        arrangementView_.setZoomLevel(z);
        suppressLinkedTimelineZoom_ = false;
    };

    arrangementView_.onVisibleStartTimeChanged = [this](double visibleStartTimeSeconds) {
        if (suppressLinkedTimelineScroll_) {
            return;
        }

        suppressLinkedTimelineScroll_ = true;
        pianoRoll_.setVisibleStartTimeSeconds(visibleStartTimeSeconds);
        suppressLinkedTimelineScroll_ = false;
    };

    pianoRoll_.onVisibleStartTimeChanged = [this](double visibleStartTimeSeconds) {
        if (suppressLinkedTimelineScroll_) {
            return;
        }

        suppressLinkedTimelineScroll_ = true;
        arrangementView_.setVisibleStartTimeSeconds(visibleStartTimeSeconds);
        suppressLinkedTimelineScroll_ = false;
    };
    
    // 璁剧疆楂樻€ц兘鎾斁澶翠綅缃簮 - 鐩存帴浠?Processor 璇诲彇锛岀粫杩?60Hz Timer 鐡堕
    pianoRoll_.setPlayheadPositionSource(processorRef_.getPositionAtomic());
    arrangementView_.setPlayheadPositionSource(processorRef_.getPositionAtomic());
    
    addAndMakeVisible(pianoRoll_);

    // Add AutoRenderOverlay (initially hidden, covers PianoRoll during AUTO)
    addAndMakeVisible(autoRenderOverlay_);
    autoRenderOverlay_.setVisible(false);

    // Ensure initial focus
    arrangementView_.grabKeyboardFocus();

    // Apply the purple theme to the window
    getLookAndFeel().setColour(juce::ResizableWindow::backgroundColourId, UIColors::backgroundDark);

    // 鍚敤鍘熺敓鏍囬鏍忥紙绯荤粺椋庢牸鐨勬渶澶у寲/鏈€灏忓寲/鍏抽棴鎸夐挳锛?
    juce::Timer::callAfterDelay(60, [safeThis = juce::Component::SafePointer<OpenTuneAudioProcessorEditor>(this)]
    {
        if (safeThis == nullptr) return;
        if (auto* window = safeThis->findParentComponentOfClass<juce::DocumentWindow>())
        {
            window->setTitleBarButtonsRequired(juce::DocumentWindow::allButtons, false);
            window->setUsingNativeTitleBar(true);
            window->setColour(juce::DocumentWindow::backgroundColourId, UIColors::backgroundMedium);
            window->repaint();

            juce::Component::SafePointer<OpenTuneAudioProcessorEditor> delayedSafeThis(safeThis);
            juce::MessageManager::callAsync([delayedSafeThis]() {
                if (delayedSafeThis != nullptr) {
                    delayedSafeThis->restoreStandaloneWindowState();
                }
            });
        }
    });

    // 鎾斁澶存覆鏌撹蛋 VBlank 瑕嗙洊灞傦紝涓荤紪杈戝櫒鍚屾蹇冭烦闄嶅埌 30Hz 鍑忚交娑堟伅绾跨▼鍘嬪姏
    startTimerHz(kHeartbeatHzIdle);

    // VocoderRenderScheduler queue depth is polled via getVocoderScheduler()->getQueueDepth()

    // Hide the standalone "Options" button and Mute Warning if running in standalone mode
    juce::Timer::callAfterDelay(50, [safeThis = juce::Component::SafePointer<OpenTuneAudioProcessorEditor>(this)]() {
        if (safeThis == nullptr) return;
        if (auto* topLevel = safeThis->getTopLevelComponent())
        {
            // 2. Hide Options Button & Notification
            for (auto* child : topLevel->getChildren())
            {
                if (auto* button = dynamic_cast<juce::Button*>(child))
                {
                    if (button->getButtonText().trim().equalsIgnoreCase("Options"))
                    {
                        button->setVisible(false);
                    }
                }
                
                // Try to find the Notification Component
                if (auto* label = dynamic_cast<juce::Label*>(child))
                {
                     if (label->getText().containsIgnoreCase("Audio input is muted"))
                         label->getParentComponent()->setVisible(false);
                }
            }
            
            topLevel->repaint();
        }
    });

    themeChanged(Theme::getActiveTheme());
}

OpenTuneAudioProcessorEditor::~OpenTuneAudioProcessorEditor()
{
#if JUCE_MAC
    // Clear the macOS system menu bar before menuBar_ is destroyed.
    juce::MenuBarModel::setMacMainMenu(nullptr);
#endif

    persistUserUiState();

    // Stop timer
    stopTimer();

    // Ensure import/deferred background tasks are fully completed
    // before tearing down UI/listeners to avoid lifetime races.
    waitForBackgroundUiTasks();

    // Safely join export worker thread if it exists
    if (exportWorker_.joinable())
    {
        exportWorker_.join();
    }

    // Remove custom LookAndFeel
    setLookAndFeel(nullptr);

    // Remove language change listener
    LocalizationManager::getInstance().removeListener(this);

    transportBar_.removeListener(this);
    trackPanel_.removeListener(this);
    arrangementView_.removeListener(this);
    parameterPanel_.removeListener(this);
    pianoRoll_.removeListener(this);
}

void OpenTuneAudioProcessorEditor::restorePersistedPianoRollZoomState()
{
    double horizontalZoom = 0.0;
    float verticalZoom = 0.0f;
    float verticalScroll = 0.0f;
    if (!UserUiState::getPianoRollViewState(horizontalZoom, verticalZoom, verticalScroll)) {
        return;
    }

    suppressLinkedTimelineZoom_ = true;
    suppressLinkedTimelineScroll_ = true;
    processorRef_.setZoomLevel(horizontalZoom);
    arrangementView_.setZoomLevel(horizontalZoom);
    pianoRoll_.restoreZoomState(horizontalZoom, verticalZoom);
    pianoRoll_.setVerticalScrollOffset(verticalScroll);
    suppressLinkedTimelineZoom_ = false;
    suppressLinkedTimelineScroll_ = false;
}

void OpenTuneAudioProcessorEditor::restorePersistedWorkspaceSplitRatio()
{
    double splitRatio = 0.0;
    if (!UserUiState::getWorkspaceSplitRatio(splitRatio)) {
        return;
    }

    if (splitRatio > 0.0 && splitRatio < 1.0) {
        arrangementWorkspaceSplitRatio_ = splitRatio;
        arrangementWorkspaceHeight_ = -1;
    }
}

void OpenTuneAudioProcessorEditor::restoreStandaloneWindowState()
{
    juce::String windowState;
    bool maximised = false;
    if (!UserUiState::getStandaloneWindowState(windowState, maximised)) {
        return;
    }

    auto* window = findParentComponentOfClass<juce::ResizableWindow>();
    if (window == nullptr) {
        return;
    }

    (void)window->restoreWindowStateFromString(windowState);

    if (maximised) {
        setStandaloneWindowMaximised(*window, true);
    }
}

void OpenTuneAudioProcessorEditor::persistUserUiState() const
{
    UserUiState::setPianoRollViewState(pianoRoll_.getZoomLevel(),
                                       pianoRoll_.getVerticalZoom(),
                                       pianoRoll_.getVerticalScrollOffset());
    UserUiState::setWorkspaceSplitRatio(getArrangementWorkspaceSplitRatio());

    auto* window = const_cast<OpenTuneAudioProcessorEditor*>(this)->findParentComponentOfClass<juce::ResizableWindow>();
    if (window == nullptr) {
        return;
    }

    UserUiState::setStandaloneWindowState(window->getWindowStateAsString(),
                                          isStandaloneWindowMaximised(*window));
}

bool OpenTuneAudioProcessorEditor::isStandaloneWindowMaximised(const juce::ResizableWindow& window)
{
#if JUCE_WINDOWS
    if (auto* peer = window.getPeer()) {
        if (auto* nativeHandle = peer->getNativeHandle()) {
            return ::IsZoomed(static_cast<HWND>(nativeHandle)) != FALSE;
        }
    }
#else
    juce::ignoreUnused(window);
#endif

    return false;
}

int OpenTuneAudioProcessorEditor::getWorkspaceUsableHeight() const
{
    constexpr int kSplitterH = 6;
    const int shadowMargin = 12;
    const int gap = 6;

    juce::Rectangle<int> probe = getLocalBounds();
    probe.reduce(gap, gap);

    const int topBarHeight = menuBar_.isVisible() ? (MENU_BAR_HEIGHT + TRANSPORT_BAR_HEIGHT) : TRANSPORT_BAR_HEIGHT;
    probe.removeFromTop(topBarHeight + shadowMargin * 2);
    probe.removeFromTop(juce::jmax(0, gap - shadowMargin));

    if (isParameterPanelVisible_) {
        probe.removeFromRight(PARAMETER_PANEL_WIDTH + shadowMargin * 2);
        probe.removeFromRight(juce::jmax(0, gap - shadowMargin));
    }

    return probe.getHeight() - kSplitterH;
}

int OpenTuneAudioProcessorEditor::resolveArrangementWorkspaceHeight(int usableHeight) const
{
    constexpr int kMinArrangement = 100;
    constexpr int kMinPiano = 160;
    constexpr double kDefaultRatio = 0.38;

    const double splitRatio =
        arrangementWorkspaceSplitRatio_ > 0.0 ? arrangementWorkspaceSplitRatio_ : kDefaultRatio;
    const int proposedHeight = static_cast<int>(std::lround(splitRatio * static_cast<double>(usableHeight)));

    return juce::jlimit(kMinArrangement, usableHeight - kMinPiano, proposedHeight);
}

double OpenTuneAudioProcessorEditor::getArrangementWorkspaceSplitRatio() const
{
    constexpr double kDefaultRatio = 0.38;

    const int usableHeight = const_cast<OpenTuneAudioProcessorEditor*>(this)->getWorkspaceUsableHeight();
    if (usableHeight <= 0) {
        return arrangementWorkspaceSplitRatio_ > 0.0 ? arrangementWorkspaceSplitRatio_ : kDefaultRatio;
    }

    int arrangementHeight = arrangementWorkspaceHeight_;
    if (arrangementHeight < 0) {
        arrangementHeight = resolveArrangementWorkspaceHeight(usableHeight);
    }

    return juce::jlimit(0.05, 0.95, static_cast<double>(arrangementHeight) / static_cast<double>(usableHeight));
}

void OpenTuneAudioProcessorEditor::setStandaloneWindowMaximised(juce::ResizableWindow& window,
                                                                bool shouldBeMaximised)
{
#if JUCE_WINDOWS
    if (auto* peer = window.getPeer()) {
        if (auto* nativeHandle = peer->getNativeHandle()) {
            ::ShowWindow(static_cast<HWND>(nativeHandle), shouldBeMaximised ? SW_MAXIMIZE : SW_RESTORE);
            return;
        }
    }
#else
    juce::ignoreUnused(window, shouldBeMaximised);
#endif
}

void OpenTuneAudioProcessorEditor::launchBackgroundUiTask(std::function<void()> task)
{
    if (!task)
        return;

    for (auto it = backgroundTasks_.begin(); it != backgroundTasks_.end();)
    {
        if (it->valid() && it->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try { it->get(); } catch (const std::exception& e) { AppLogger::error("[PluginEditor] Background task exception: " + juce::String(e.what())); } catch (...) { AppLogger::error("[PluginEditor] Background task unknown exception"); }
            it = backgroundTasks_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    backgroundTasks_.emplace_back(
        std::async(std::launch::async, [task = std::move(task)]() mutable { task(); }));
}

void OpenTuneAudioProcessorEditor::waitForBackgroundUiTasks()
{
    std::vector<std::future<void>> pending;
    pending.swap(backgroundTasks_);

    for (auto& future : pending)
    {
        if (!future.valid())
            continue;

        try { future.get(); } catch (const std::exception& e) { AppLogger::error("[PluginEditor] Wait for background task exception: " + juce::String(e.what())); } catch (...) { AppLogger::error("[PluginEditor] Wait for background task unknown exception"); }
    }
}

bool OpenTuneAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::Undo, key))
    {
        if (!shouldAcceptUndoRedoShortcut()) {
            return true;
        }
        performUndoWithRangeTracking();
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::Redo, key))
    {
        if (!shouldAcceptUndoRedoShortcut()) {
            return true;
        }
        performRedoWithRangeTracking();
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::PlayPause, key))
    {
        playPauseToggleRequested();
        return true;
    }
    
    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::PlayFromStart, key))
    {
        playFromStartToggleRequested();
        return true;
    }

    if (KeyShortcutConfig::matchesShortcut(KeyShortcutConfig::ShortcutId::SaveProject, key))
    {
        quickSaveProject();
        return true;
    }

    return false;
}

bool OpenTuneAudioProcessorEditor::shouldAcceptUndoRedoShortcut()
{
    const uint32_t nowMs = juce::Time::getMillisecondCounter();
    constexpr uint32_t debounceMs = 120;

    if (lastUndoRedoShortcutMs_ != 0 && (nowMs - lastUndoRedoShortcutMs_) < debounceMs) {
        return false;
    }

    lastUndoRedoShortcutMs_ = nowMs;
    return true;
}

bool OpenTuneAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    static const juce::String kImportExtensionSpec = getImportExtensionSpec();
    for (const auto& path : files)
    {
        const juce::File f(path);
        if (f.hasFileExtension(kImportExtensionSpec))
            return true;
    }
    return false;
}

void OpenTuneAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (isImportInProgress_)
    {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Import Audio",
                "An audio import is already in progress. Please try again in a moment."
            );
        return;
    }

    if (files.isEmpty())
        return;

    const juce::File file(files[0]);
    if (!file.existsAsFile())
        return;

    static const juce::String kImportExtensionSpec = getImportExtensionSpec();
    if (!file.hasFileExtension(kImportExtensionSpec))
    {
            const auto wildcard = getImportWildcardFilter().replaceCharacters("*", "");
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Import Audio",
                "Unsupported file type.\nSupported types: " + wildcard
            );
        return;
    }

    if (files.size() > 1)
    {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Import Audio",
                "Multiple files were dropped. Only the first file will be imported."
            );
    }

    int track = resolveTrackIndexForAudioDrop(x, y);
    if (track < 0 || track >= OpenTuneAudioProcessor::MAX_TRACKS)
        track = processorRef_.getActiveTrackId();
    track = juce::jlimit(0, OpenTuneAudioProcessor::MAX_TRACKS - 1, track);

    importAudioFileToTrackWithOverwritePrompt(track, file, true);
}

void OpenTuneAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Solid background (Soft Blue-Grey)
    g.fillAll(UIColors::backgroundDark);

    // Draw Buffering Indicator
    if (processorRef_.isBuffering()) {
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(24.0f);
        g.drawText("Buffering...", pianoRoll_.getBounds(), juce::Justification::centred, false);
        
        // Draw spinning circle
        int size = 40;
        juce::Rectangle<int> spinnerArea(0, 0, size, size);
        spinnerArea.setCentre(pianoRoll_.getBounds().getCentre().translated(0, 40));
        
        float angle = static_cast<float>(juce::Time::getMillisecondCounter() % 1000) / 1000.0f * juce::MathConstants<float>::twoPi;
        
        g.setColour(juce::Colours::white);
        juce::Path p;
        p.addArc((float)spinnerArea.getX(), (float)spinnerArea.getY(), (float)size, (float)size, angle, angle + 2.5f, true);
        g.strokePath(p, juce::PathStrokeType(3.0f));
    }
    
    // Draw Fallback Warning
    if (processorRef_.isDrySignalFallback()) {
        g.setColour(juce::Colours::red);
        g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        juce::Rectangle<int> warningArea = transportBar_.getBounds().removeFromRight(150).reduced(5);
        g.drawText("! DRY FALLBACK", warningArea, juce::Justification::centredRight, false);
    }
}

void OpenTuneAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // 闃村奖杈硅窛锛氫负鍚勯潰鏉块鐣欓槾褰辨覆鏌撶┖闂?
    // 鍚勭粍浠?paint() 浣跨敤 reduced(shadowMargin) 缁樺埗鑳屾櫙锛岄槾褰卞湪杈硅窛鍐呮覆鏌?
    const int shadowMargin = 12;
    const int gap = 6;  // Gap between panels (瑙嗚闂磋窛锛屼笉鍚槾褰?

    bounds.reduce(gap, gap); // Global padding

    // TopBar锛氶珮搴?+ 闃村奖杈硅窛锛堜笂涓嬪悇12px锛?
    const int topBarHeight = menuBar_.isVisible() ? (MENU_BAR_HEIGHT + TRANSPORT_BAR_HEIGHT) : TRANSPORT_BAR_HEIGHT;
    const int topBarHeightWithShadow = topBarHeight + shadowMargin * 2;
    topBar_.setBounds(bounds.removeFromTop(topBarHeightWithShadow));
    // 瑙嗚闂磋窛锛歡ap 鍑忓幓宸茶闃村奖鍗犵敤鐨勪笅杈硅窛
    bounds.removeFromTop(juce::jmax(0, gap - shadowMargin));

    // 宸︿晶 Track Inspector锛堝彲鎶樺彔锛?
    // 瀹藉害 + 闃村奖杈硅窛锛堝乏鍙冲悇12px锛?
    // 鍙充晶 Properties Panel锛堝彲鎶樺彔锛?
    // 瀹藉害 + 闃村奖杈硅窛锛堝乏鍙冲悇12px锛?
    if (isParameterPanelVisible_)
    {
        parameterPanel_.setVisible(true);
        const int paramPanelWidthWithShadow = PARAMETER_PANEL_WIDTH + shadowMargin * 2;
        parameterPanel_.setBounds(bounds.removeFromRight(paramPanelWidthWithShadow));
        bounds.removeFromRight(juce::jmax(0, gap - shadowMargin));
    }
    else
    {
        parameterPanel_.setVisible(false);
        parameterPanel_.setBounds({});
    }

    // Central column: arrangement (top) + splitter + piano roll (bottom)
    constexpr int kSplitterH = 6;
    constexpr int kMinArrangement = 100;
    constexpr int kMinPiano = 160;
    const int usable = bounds.getHeight() - kSplitterH;
    if (usable >= kMinArrangement + kMinPiano)
    {
        arrangementWorkspaceHeight_ = resolveArrangementWorkspaceHeight(usable);
        arrangementWorkspaceSplitRatio_ =
            static_cast<double>(arrangementWorkspaceHeight_) / static_cast<double>(usable);

        auto top = bounds.removeFromTop(arrangementWorkspaceHeight_);
        trackPanel_.setVisible(true);
        const int trackPanelWidthWithShadow = TRACK_PANEL_WIDTH + shadowMargin * 2;
        auto arrArea = top;
        trackPanel_.setBounds(arrArea.removeFromLeft(trackPanelWidthWithShadow));
        arrArea.removeFromLeft(juce::jmax(0, gap - shadowMargin));
        arrangementView_.setBounds(arrArea);
        workspaceSplitter_.setVisible(true);
        workspaceSplitter_.setBounds(bounds.removeFromTop(kSplitterH));
        pianoRoll_.setBounds(bounds);
    }
    else
    {
        auto narrowTop = bounds.removeFromTop(kMinArrangement);
        trackPanel_.setVisible(true);
        const int trackPanelWidthWithShadow = TRACK_PANEL_WIDTH + shadowMargin * 2;
        auto arrArea = narrowTop;
        trackPanel_.setBounds(arrArea.removeFromLeft(trackPanelWidthWithShadow));
        arrArea.removeFromLeft(juce::jmax(0, gap - shadowMargin));
        arrangementView_.setBounds(arrArea);
        workspaceSplitter_.setVisible(false);
        workspaceSplitter_.setBounds({});
        pianoRoll_.setBounds(bounds);
    }

    autoRenderOverlay_.setBounds(pianoRoll_.getBounds());
    autoRenderOverlay_.toFront(false);
}

void OpenTuneAudioProcessorEditor::syncParameterPanelFromSelection()
{
    float selectedRetuneSpeed = 0.0f;
    float selectedVibratoDepth = 0.0f;
    float selectedVibratoRate = 0.0f;

    if (pianoRoll_.getSingleSelectedNoteParameters(selectedRetuneSpeed, selectedVibratoDepth, selectedVibratoRate)) {
        parameterPanel_.setRetuneSpeed(selectedRetuneSpeed);
        parameterPanel_.setVibratoDepth(selectedVibratoDepth);
        parameterPanel_.setVibratoRate(selectedVibratoRate);
        showingSingleNoteParams_ = true;
        return;
    }

    if (showingSingleNoteParams_) {
        parameterPanel_.setRetuneSpeed(pianoRoll_.getCurrentRetuneSpeed() * 100.0f);
        parameterPanel_.setVibratoDepth(pianoRoll_.getCurrentVibratoDepth());
        parameterPanel_.setVibratoRate(pianoRoll_.getCurrentVibratoRate());
        showingSingleNoteParams_ = false;
    }
}

void OpenTuneAudioProcessorEditor::timerCallback()
{
    auto* vocoderDomain = processorRef_.getVocoderDomain();
    const bool inferenceNow = pianoRoll_.isAutoTuneProcessing();
    setInferenceActive(inferenceNow);

    syncParameterPanelFromSelection();

    if (arrangementView_.isShowing()) {
        arrangementView_.onHeartbeatTick();
    }
    // 閽㈢惔鍗峰笜鍦ㄥ伐绋嬪彴瑙嗗浘涓嬩笉鍙锛屼絾浠嶉渶娑堣€楀紓姝ヤ慨闊崇粨鏋滃苟瑙﹀彂鍒嗗潡娓叉煋
    pianoRoll_.onHeartbeatTick();

    processDeferredImportPostProcessQueue();

    const bool allowSecondaryRefresh = !inferenceActive_ || ((++inferenceActiveTickCounter_ % 4) == 0);

    // Sync other state if needed (e.g. from Toolbar or ParameterPanel)
    // if (pianoRoll_.getAlignmentOffset() != processorRef_.getAlignmentOffset()) {
    //     pianoRoll_.setAlignmentOffset(processorRef_.getAlignmentOffset());
    // }
    if (allowSecondaryRefresh && !f0ParamsSyncedFromInference_ && processorRef_.isInferenceReady()) {
        auto* f0Service = processorRef_.getF0Service();
        if (f0Service) {
            parameterPanel_.setF0Min(f0Service->getF0Min());
            parameterPanel_.setF0Max(f0Service->getF0Max());
            f0ParamsSyncedFromInference_ = true;
        }
    }

    // Update playhead position from processor
    double currentPositionSeconds = processorRef_.getPosition();
    double sampleRate = processorRef_.getSampleRate();

    const double bpm = processorRef_.getBpm();
    if (bpm > 0.0 && std::abs(bpm - lastSyncedBpm_) > 0.001) {
        transportBar_.setBpm(bpm);
        pianoRoll_.setBpm(bpm);
        lastSyncedBpm_ = bpm;
    }

    const int timeSigNum = processorRef_.getTimeSigNumerator();
    const int timeSigDenom = processorRef_.getTimeSigDenominator();
    if (timeSigNum > 0 && timeSigDenom > 0
        && (timeSigNum != lastSyncedTimeSigNum_ || timeSigDenom != lastSyncedTimeSigDenom_)) {
        pianoRoll_.setTimeSignature(timeSigNum, timeSigDenom);
        lastSyncedTimeSigNum_ = timeSigNum;
        lastSyncedTimeSigDenom_ = timeSigDenom;
    }
    
    if (allowSecondaryRefresh && sampleRate > 0.0) {
        const int sr = static_cast<int>(sampleRate);
        const int activeTrack = processorRef_.getActiveTrackId();
        const int clipIndex = processorRef_.getSelectedClip(activeTrack);
        std::shared_ptr<const juce::AudioBuffer<float>> clipBuffer =
            processorRef_.getClipAudioBuffer(activeTrack, clipIndex);
        if (sr != lastPianoRollSampleRate_ || clipBuffer != lastPianoRollBuffer_) {
            pianoRoll_.setAudioBuffer(clipBuffer, sr);
            lastPianoRollSampleRate_ = sr;
            lastPianoRollBuffer_ = clipBuffer;
        }

        const bool hasUserAudio = (clipBuffer != nullptr);
        if (hasUserAudio != lastPianoRollHasUserAudio_) {
            pianoRoll_.setHasUserAudio(hasUserAudio);
            lastPianoRollHasUserAudio_ = hasUserAudio;
        }
    }

    // 鎾斁澶翠綅缃敱鍚勭粍浠堕€氳繃 positionSource_ 鐩存帴浠?Processor 璇诲彇
    transportBar_.setPositionSeconds(currentPositionSeconds);

    // Sync Rendering Progress
    if (vocoderDomain != nullptr) {
        // [AUTO overlay completion] Use snapshot target (trackId+clipId) when latched, otherwise current selection
        int activeTrack = processorRef_.getActiveTrackId();
        int activeClip = processorRef_.getSelectedClip(activeTrack);
        bool autoOverlayTargetExists = true;

        if (autoOverlayLatched_) {
            activeTrack = autoOverlayTargetTrackId_;
            activeClip = (activeTrack >= 0 && autoOverlayTargetClipId_ != 0)
                ? processorRef_.findClipIndexById(activeTrack, autoOverlayTargetClipId_)
                : -1;
            autoOverlayTargetExists = (activeClip >= 0);
        }

        // 浠庣洰鏍?Clip 鐨?RenderCache 鑾峰彇 Chunk 鐘舵€?
        const auto chunkStats = processorRef_.getClipChunkStats(activeTrack, activeClip);
        const bool isTxnActive = chunkStats.hasActiveWork();
        const auto renderState = buildRenderStatusUiState(isTxnActive);
        const float progress = renderState.showRendering ? 1.0f : 0.0f;
        
        const bool isAutoProcessing = pianoRoll_.isAutoTuneProcessing();

        // RMVPE overlay 閲婃斁鍒ゅ畾锛欶0 Ready + 涓婁笅鏂囧尮閰?+ F0鍙
        if (rmvpeOverlayLatched_) {
            const int targetTrackId = rmvpeOverlayTargetTrackId_;
            const uint64_t targetClipId = rmvpeOverlayTargetClipId_;
            const int resolvedClipIndex = (targetTrackId >= 0 && targetClipId != 0)
                ? processorRef_.findClipIndexById(targetTrackId, targetClipId)
                : -1;

            bool shouldUnlatch = false;
            if (resolvedClipIndex < 0) {
                shouldUnlatch = true;
            } else {
                const auto f0State = processorRef_.getClipOriginalF0State(targetTrackId, resolvedClipIndex);
                if (f0State == OriginalF0State::Failed) {
                    shouldUnlatch = true;
                } else if (f0State == OriginalF0State::Ready) {
                    const bool contextMatches =
                        pianoRoll_.getCurrentTrackId() == targetTrackId
                        && pianoRoll_.getCurrentClipId() == targetClipId;
                    if (contextMatches && pianoRoll_.isCurrentClipOriginalF0Visible()) {
                        shouldUnlatch = true;
                    }
                }
            }

            if (shouldUnlatch) {
                rmvpeOverlayLatched_ = false;
                rmvpeOverlayTargetTrackId_ = -1;
                rmvpeOverlayTargetClipId_ = 0;
            }
        }

        // AUTO overlay 閲婃斁鍒ゅ畾锛氭覆鏌撳畬鎴?
        if (autoOverlayLatched_) {
            bool shouldUnlatch = false;
            if (!autoOverlayTargetExists) {
                shouldUnlatch = true;
            } else {
                // 妫€鏌?Chunk 鏄惁鍏ㄩ儴瀹屾垚锛堟棤 Pending/Running锛?
                const bool txnFinished = !chunkStats.hasActiveWork() && chunkStats.total() > 0;
                
                if (!isAutoProcessing && txnFinished) {
                    shouldUnlatch = true;
                    AppLogger::log("AUTO_OVERLAY_SUCCESS"
                        + juce::String(" track=") + juce::String(activeTrack)
                        + juce::String(" clip=") + juce::String(activeClip)
                        + juce::String(" chunks=") + juce::String(chunkStats.total()));
                }
            }

            if (shouldUnlatch) {
                autoOverlayLatched_ = false;
                autoOverlayTargetTrackId_ = -1;
                autoOverlayTargetClipId_ = 0;
            }
        }

        // 缁熶竴鏇存柊 overlay 鍙鎬?
        if (autoOverlayLatched_) {
            autoRenderOverlay_.setMessageText("Rendering...");
            autoRenderOverlay_.setVisible(true);
        } else if (rmvpeOverlayLatched_) {
            const bool isCurrentClipExtracting = 
                pianoRoll_.getCurrentTrackId() == rmvpeOverlayTargetTrackId_
                && pianoRoll_.getCurrentClipId() == rmvpeOverlayTargetClipId_;
            
            if (isCurrentClipExtracting) {
                autoRenderOverlay_.setMessageText(
                    juce::String::fromUTF8("璋冨紡妫€娴嬩腑......"),
                    juce::String::fromUTF8("姝ｅ湪鎻愬彇闊抽珮鏇茬嚎...")
                );
                autoRenderOverlay_.setVisible(true);
            } else {
                autoRenderOverlay_.setVisible(false);
            }
        } else {
            autoRenderOverlay_.setVisible(false);
        }
        
        // [娓叉煋寮圭獥] 浠呭湪 AUTO overlay 鏈攣瀹氭椂鏇存柊鍙充笂瑙掔姸鎬佹爮銆?
        if (!autoOverlayLatched_) {
            pianoRoll_.setRenderingProgress(progress, renderState.uiPendingTasks);
        }

        juce::String status;
        if (processorRef_.isBuffering()) status << "Buffering";
        if (processorRef_.isDrySignalFallback()) {
            if (status.isNotEmpty()) status << " | ";
            status << "Dry";
        }
        if (renderState.showRendering && !autoOverlayLatched_) {
            if (status.isNotEmpty()) status << " | ";
            status << renderState.detailText;
        }
        transportBar_.setRenderStatusText(status);
    }

    // Sync playing state (Fix for inconsistent UI state)
    if (transportBar_.isPlaying() != processorRef_.isPlaying())
    {
        transportBar_.setPlaying(processorRef_.isPlaying());
        pianoRoll_.setIsPlaying(processorRef_.isPlaying());
        arrangementView_.setIsPlaying(processorRef_.isPlaying());
    }

    if (allowSecondaryRefresh) {
        for (int i = 0; i < OpenTuneAudioProcessor::MAX_TRACKS; ++i) {
            float rmsDb = processorRef_.getTrackRMS(i);
            trackPanel_.setTrackLevel(i, rmsDb);
        }
    }
}

void OpenTuneAudioProcessorEditor::syncPianoRollFromClipSelection(int trackId, int clipIndex)
{

    if (clipIndex >= 0) {
        pianoRoll_.setCurrentClipContext(trackId, processorRef_.getClipId(trackId, clipIndex));
    } else {
        pianoRoll_.clearClipContext();
    }

    pianoRoll_.setTrackTimeOffset(processorRef_.getClipStartSeconds(trackId, clipIndex));
    pianoRoll_.setVisibleStartTimeSeconds(arrangementView_.getVisibleStartTimeSeconds());

    const int sr = static_cast<int>(processorRef_.getSampleRate());
    std::shared_ptr<const juce::AudioBuffer<float>> clipBuffer =
        processorRef_.getClipAudioBuffer(trackId, clipIndex);
    const bool hasUserAudio = (clipBuffer != nullptr);

    pianoRoll_.setAudioBuffer(clipBuffer, sr);
    pianoRoll_.setHasUserAudio(hasUserAudio);

    lastPianoRollSampleRate_ = sr;
    lastPianoRollBuffer_ = clipBuffer;
    lastPianoRollHasUserAudio_ = hasUserAudio;

    if (!hasUserAudio) {
        pianoRoll_.setPitchCurve(nullptr);
        pianoRoll_.setNotes({});
    }

    // Restore Pitch Curve
    auto curve = processorRef_.getClipPitchCurve(trackId, clipIndex);
    pianoRoll_.setPitchCurve(curve);

    // clip 鍒囨崲鍙簲鐢ㄨ clip 鐨勭敓鏁堣皟寮忥紙鏃犺法 clip 鍥為€€锛?
    applyResolvedScaleForClip(trackId, clipIndex);

    // Restore Notes
    pianoRoll_.setNotes(processorRef_.getClipNotes(trackId, clipIndex));
}

void OpenTuneAudioProcessorEditor::toolSelected(int toolId)
{
    if (toolId < 0 || toolId > static_cast<int>(ToolId::SplitNote)) {
        return;
    }

    auto tool = static_cast<ToolId>(toolId);
    if (tool == ToolId::AutoTune)
    {
        // 鍚堝苟閲嶅閫昏緫锛氳皟鐢ㄧ粺涓€ helper
        startAutoTuneAsUnifiedEdit();
        pianoRoll_.setCurrentTool(ToolId::Select);
        parameterPanel_.setActiveTool(1);
        return;
    }

    pianoRoll_.setCurrentTool(tool);
    parameterPanel_.setActiveTool(toolId);
}

// ============================================================================
// ParameterPanel::Listener Implementation
// ============================================================================

void OpenTuneAudioProcessorEditor::retuneSpeedChanged(float speed)
{
    float normalizedSpeed = speed / 100.0f;
    pianoRoll_.setRetuneSpeed(normalizedSpeed);
    if (pianoRoll_.applyRetuneSpeedToSelection(normalizedSpeed)) {
        return;
    }

    const float vibratoDepth = parameterPanel_.getVibratoDepth();
    const float vibratoRate = parameterPanel_.getVibratoRate();
    pianoRoll_.applyCorrectionAsyncForEntireClip(normalizedSpeed, vibratoDepth, vibratoRate);
}

void OpenTuneAudioProcessorEditor::vibratoDepthChanged(float value)
{
    if (pianoRoll_.applyVibratoDepthToSelection(value)) return;
    pianoRoll_.setVibratoDepth(value);
    const float speed = parameterPanel_.getRetuneSpeed() / 100.0f;
    const float rate = parameterPanel_.getVibratoRate();
    pianoRoll_.applyCorrectionAsyncForEntireClip(speed, value, rate);
}

void OpenTuneAudioProcessorEditor::vibratoRateChanged(float value)
{
    if (pianoRoll_.applyVibratoRateToSelection(value)) return;
    pianoRoll_.setVibratoRate(value);
    const float speed = parameterPanel_.getRetuneSpeed() / 100.0f;
    const float depth = parameterPanel_.getVibratoDepth();
    pianoRoll_.applyCorrectionAsyncForEntireClip(speed, depth, value);
}

void OpenTuneAudioProcessorEditor::noteSplitChanged(float value)
{
    pianoRoll_.setNoteSplit(value);
}

void OpenTuneAudioProcessorEditor::parameterDragEnded(int paramId, float oldValue, float newValue)
{
}

void OpenTuneAudioProcessorEditor::performScaleInferenceForClip(int trackId, int clipIndex)
{
    const uint64_t clipId = processorRef_.getClipId(trackId, clipIndex);

    const auto existingKey = processorRef_.getClipDetectedKey(trackId, clipIndex);
    if (existingKey.confidence > 0.0f) {
        return;
    }

    auto audioBuffer = processorRef_.getClipAudioBuffer(trackId, clipIndex);
    if (!audioBuffer || audioBuffer->getNumSamples() <= 0) {
        DBG("ChromaKeyDetection: skip=no_audio trackId=" + juce::String(trackId)
            + " clipIndex=" + juce::String(clipIndex)
            + " clipId=" + juce::String(static_cast<juce::int64>(clipId)));
        return;
    }

    DBG("Performing Chroma Key Detection for Track " + juce::String(trackId) + " Clip " + juce::String(clipIndex));

    const float* audioData = audioBuffer->getReadPointer(0);
    const int numSamples = audioBuffer->getNumSamples();
    constexpr int sampleRate = PianoRollComponent::kAudioSampleRate;

    ChromaKeyDetector detector;
    DetectedKey key = detector.detect(audioData, numSamples, sampleRate);

    DBG("ChromaKeyDetection: trackId=" + juce::String(trackId)
        + " clipIndex=" + juce::String(clipIndex)
        + " clipId=" + juce::String(static_cast<juce::int64>(clipId))
        + " samples=" + juce::String(numSamples)
        + " confidence=" + juce::String(key.confidence, 4));

    processorRef_.setClipDetectedKey(trackId, clipIndex, key);

    const int scaleType = scaleToUiScaleType(key.scale);

    const int activeTrack = processorRef_.getActiveTrackId();
    const int activeClip = processorRef_.getSelectedClip(activeTrack);
    const uint64_t activeClipId = processorRef_.getClipId(activeTrack, activeClip);
    const uint64_t targetClipId = clipId;
    const bool sameVisibleClip = (targetClipId != 0)
        ? (activeTrack == trackId && activeClipId == targetClipId)
        : (activeTrack == trackId && activeClip == clipIndex);

    if (sameVisibleClip) {
        applyScaleToUi(static_cast<int>(key.root), scaleType);
    }

    DBG("ScaleSyncTrace: source=chroma trackId=" + juce::String(trackId)
        + " clipIndex=" + juce::String(clipIndex)
        + " clipId=" + juce::String(static_cast<juce::int64>(targetClipId))
        + " root=" + juce::String(static_cast<int>(key.root))
        + " scale=" + juce::String(scaleType)
        + " sameVisible=" + juce::String(sameVisibleClip ? 1 : 0));

    DBG("Detected Key: " + DetectedKey::keyToString(key.root) + " " + DetectedKey::scaleToString(key.scale));
}

// Eager-first锛氫粎瀵煎叆瀹屾垚鏃惰Е鍙戜竴娆?OriginalF0 棰勬彁鍙?
void OpenTuneAudioProcessorEditor::requestOriginalF0ExtractionForImport(int trackId, int clipIndex)
{
    const uint64_t clipId = processorRef_.getClipId(trackId, clipIndex);
    const uint64_t requestKey = F0ExtractionService::makeRequestKey(clipId, trackId, clipIndex);
    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);

    if (clipId != 0) {
        processorRef_.setClipOriginalF0StateById(trackId, clipId, OriginalF0State::Extracting);
    } else {
        processorRef_.setClipOriginalF0State(trackId, clipIndex, OriginalF0State::Extracting);
    }

    auto submitResult = f0ExtractionService_.submit(
        requestKey,
        [safeThis, trackId, clipIndex, clipId, requestKey]() -> F0ExtractionService::Result {
            F0ExtractionService::Result out;
            out.trackId = trackId;
            out.clipIndexHint = clipIndex;
            out.clipId = clipId;
            out.requestKey = requestKey;

            if (safeThis == nullptr) {
                out.errorMessage = "editor_destroyed";
                return out;
            }

            auto& procRef = safeThis->processorRef_;

            if (!procRef.initializeInferenceIfNeeded()) {
                out.errorMessage = "inference_not_ready";
                return out;
            }

            OpenTuneAudioProcessor::ClipSnapshot snap;
            if (clipId != 0) {
                if (!procRef.getClipSnapshot(trackId, clipId, snap)) {
                    out.errorMessage = "clip_snapshot_failed";
                    return out;
                }
            } else {
                std::shared_ptr<const juce::AudioBuffer<float>> clipBuffer =
                    procRef.getClipAudioBuffer(trackId, clipIndex);
                if (clipBuffer == nullptr || clipBuffer->getNumSamples() <= 0) {
                    out.errorMessage = "empty_clip_audio";
                    return out;
                }
                snap.audioBuffer = clipBuffer;
            }

            const int numSamples = snap.audioBuffer->getNumSamples();
            const int numChannels = snap.audioBuffer->getNumChannels();
            if (numSamples <= 0 || numChannels <= 0) {
                out.errorMessage = "invalid_audio_buffer";
                return out;
            }

            std::vector<float> monoAudio(static_cast<size_t>(numSamples), 0.0f);
            const float invChannels = 1.0f / static_cast<float>(numChannels);
            for (int ch = 0; ch < numChannels; ++ch) {
                const float* src = snap.audioBuffer->getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i) {
                    monoAudio[static_cast<size_t>(i)] += src[i] * invChannels;
                }
            }

            // Use render sample rate (44.1kHz) for all calculations
            constexpr double internalSampleRate = TimeCoordinate::kRenderSampleRate;

            auto* f0Service = procRef.getF0Service();
            const int hopSize = f0Service ? f0Service->getF0HopSize() : 160;
            const int f0SampleRate = f0Service ? f0Service->getF0SampleRate() : 16000;

            const auto& silentGaps = snap.silentGaps;

            struct VoicedSegment {
                int64_t startSample;
                int64_t endSample;
            };
            std::vector<VoicedSegment> voicedSegments;

            if (silentGaps.empty()) {
                voicedSegments.push_back({0, static_cast<int64_t>(numSamples)});
            } else {
                int64_t prevEnd = 0;
                for (const auto& gap : silentGaps) {
                    const int64_t gapStart = static_cast<int64_t>(gap.startSeconds * internalSampleRate);
                    const int64_t gapEnd = static_cast<int64_t>(gap.endSeconds * internalSampleRate);
                    if (gapStart > prevEnd) {
                        voicedSegments.push_back({prevEnd, gapStart});
                    }
                    prevEnd = gapEnd;
                }
                if (prevEnd < numSamples) {
                    voicedSegments.push_back({prevEnd, numSamples});
                }
            }

            if (voicedSegments.empty()) {
                out.errorMessage = "no_voiced_segments";
                return out;
            }

            const size_t totalFrames = static_cast<size_t>(
                std::ceil(static_cast<double>(numSamples) * f0SampleRate / internalSampleRate / hopSize));
            std::vector<float> fullF0(totalFrames, 0.0f);
            std::vector<float> fullEnergy(totalFrames, 0.0f);

            for (const auto& seg : voicedSegments) {
                const size_t segStart = static_cast<size_t>(seg.startSample);
                const size_t segLength = static_cast<size_t>(seg.endSample - seg.startSample);
                if (segLength == 0) continue;

                std::vector<float> segAudio(segLength);
                std::copy(monoAudio.begin() + segStart, monoAudio.begin() + segStart + segLength, segAudio.begin());

                auto* segF0Service = procRef.getF0Service();
                if (!segF0Service) continue;
                
                auto f0Result = segF0Service->extractF0(segAudio.data(), segLength, static_cast<int>(internalSampleRate));
                if (!f0Result.ok()) continue;
                
                const auto& segF0 = f0Result.value();
                if (segF0.empty()) continue;
                
                std::vector<float> segEnergy(segF0.size(), 1.0f);

                const size_t segFrameOffset = static_cast<size_t>(
                    static_cast<double>(seg.startSample) * f0SampleRate / internalSampleRate / hopSize);

                for (size_t i = 0; i < segF0.size() && segFrameOffset + i < totalFrames; ++i) {
                    fullF0[segFrameOffset + i] = segF0[i];
                    if (i < segEnergy.size()) {
                        fullEnergy[segFrameOffset + i] = segEnergy[i];
                    }
                }
            }

            out.f0 = std::move(fullF0);
            out.energy = std::move(fullEnergy);
            out.hopSize = hopSize;
            out.f0SampleRate = f0SampleRate;
            out.modelName = "RMVPE";

            int voicedFrames = 0;
            for (float v : out.f0) {
                if (std::isfinite(v) && v > 0.0f) {
                    ++voicedFrames;
                }
            }
            const float voicedRatio = out.f0.empty() ? 0.0f : static_cast<float>(voicedFrames) / static_cast<float>(out.f0.size());
            if (out.f0.empty() || voicedRatio <= 0.001f) {
                out.errorMessage = "f0_empty_or_unvoiced";
                return out;
            }

            out.success = true;
            return out;
        },
        [safeThis](F0ExtractionService::Result&& result) {
            if (safeThis == nullptr) {
                return;
            }

            const auto resolveClipIndexFromRequest = [&result]() -> int {
                if (result.clipIndexHint >= 0) {
                    return result.clipIndexHint;
                }
                if (result.clipId == 0 && result.requestKey != 0) {
                    return static_cast<int>(static_cast<uint32_t>(result.requestKey));
                }
                return -1;
            };

            const auto applyTerminalState = [&result, safeThis, &resolveClipIndexFromRequest](OriginalF0State targetState) -> int {
                if (result.clipId != 0) {
                    if (safeThis->processorRef_.setClipOriginalF0StateById(result.trackId, result.clipId, targetState)) {
                        return safeThis->processorRef_.findClipIndexById(result.trackId, result.clipId);
                    }
                }

                const int fallbackIndex = resolveClipIndexFromRequest();
                if (fallbackIndex >= 0) {
                    safeThis->processorRef_.setClipOriginalF0State(result.trackId, fallbackIndex, targetState);
                }
                return fallbackIndex;
            };

            if (!result.success) {
                applyTerminalState(OriginalF0State::Failed);
                DBG("F0 extraction failed: key=" + juce::String(static_cast<juce::int64>(result.requestKey))
                    + " reason=" + juce::String(result.errorMessage));
                return;
            }

            const int resolvedClipIndex = applyTerminalState(OriginalF0State::Ready);
            if (resolvedClipIndex < 0) {
                DBG("F0 extraction completed without resolvable clip index: key="
                    + juce::String(static_cast<juce::int64>(result.requestKey))
                    + " track=" + juce::String(result.trackId)
                    + " clipId=" + juce::String(static_cast<juce::int64>(result.clipId)));
                return;
            }

            auto pitchCurve = std::make_shared<PitchCurve>();
            pitchCurve->setHopSize(result.hopSize);
            pitchCurve->setSampleRate(static_cast<double>(result.f0SampleRate));
            pitchCurve->setOriginalF0(result.f0);
            if (!result.energy.empty()) {
                pitchCurve->setOriginalEnergy(result.energy);
            }

            safeThis->processorRef_.setClipPitchCurve(result.trackId, resolvedClipIndex, pitchCurve);

            const int activeTrack = safeThis->processorRef_.getActiveTrackId();
            const int activeClip = safeThis->processorRef_.getSelectedClip(activeTrack);
            const uint64_t activeClipId = safeThis->processorRef_.getClipId(activeTrack, activeClip);
            const bool sameClip = (result.clipId != 0)
                ? (activeClipId == result.clipId)
                : (activeTrack == result.trackId && activeClip == resolvedClipIndex);

            if (sameClip) {
                safeThis->pianoRoll_.setPitchCurve(pitchCurve);
                safeThis->pianoRoll_.repaint();
            }

            {
                NoteGeneratorParams genParams;
                auto generatedNotes = NoteGenerator::generate(
                    result.f0,
                    result.energy,
                    result.hopSize,
                    static_cast<double>(result.f0SampleRate),
                    static_cast<double>(PianoRollComponent::kAudioSampleRate),
                    genParams);

                if (!generatedNotes.empty()) {
                    safeThis->processorRef_.setClipNotes(result.trackId, resolvedClipIndex, generatedNotes);
                    if (sameClip) {
                        safeThis->pianoRoll_.setNotes(generatedNotes);
                        safeThis->pianoRoll_.repaint();
                    }
                    AppLogger::log("Import note generation: track=" + juce::String(result.trackId)
                        + " clip=" + juce::String(resolvedClipIndex)
                        + " notes=" + juce::String(static_cast<int>(generatedNotes.size())));
                }
            }

            safeThis->performScaleInferenceForClip(result.trackId, resolvedClipIndex);

            int voicedFrames = 0;
            for (float v : result.f0) {
                if (std::isfinite(v) && v > 0.0f) ++voicedFrames;
            }
            const float voicedRatio = result.f0.empty() ? 0.0f
                : static_cast<float>(voicedFrames) / static_cast<float>(result.f0.size());

            DBG("F0 extraction completed model=" + juce::String(result.modelName)
                + " track=" + juce::String(result.trackId)
                + " clip=" + juce::String(resolvedClipIndex)
                + " frames=" + juce::String(static_cast<int>(result.f0.size()))
                + " voicedRatio=" + juce::String(voicedRatio, 3));
            (void)voicedRatio;
        }
    );

    if (submitResult == F0ExtractionService::SubmitResult::AlreadyInProgress) {
        if (clipId != 0) {
            processorRef_.setClipOriginalF0StateById(trackId, clipId, OriginalF0State::Extracting);
        } else {
            processorRef_.setClipOriginalF0State(trackId, clipIndex, OriginalF0State::Extracting);
        }
        return;
    }

    if (submitResult != F0ExtractionService::SubmitResult::Accepted) {
        if (clipId != 0) {
            processorRef_.setClipOriginalF0StateById(trackId, clipId, OriginalF0State::Failed);
        } else {
            processorRef_.setClipOriginalF0State(trackId, clipIndex, OriginalF0State::Failed);
        }
        DBG("F0 extraction request rejected key=" + juce::String(static_cast<juce::int64>(requestKey)));
        return;
    }

    // RMVPE 鎻愬彇 overlay latch锛堜粎 Accepted 鎵嶈繘鍏ワ級
    if (clipId != 0) {
        rmvpeOverlayLatched_ = true;
        rmvpeOverlayTargetTrackId_ = trackId;
        rmvpeOverlayTargetClipId_ = clipId;
    }
}

// ============================================================================
// PianoRollComponent::Listener Implementation
// ============================================================================

void OpenTuneAudioProcessorEditor::playheadPositionChangeRequested(double timeSeconds)
{
    processorRef_.setPosition(timeSeconds);
    arrangementView_.syncPlayheadPosition(timeSeconds);
    pianoRoll_.syncPlayheadPosition(timeSeconds);
}

void OpenTuneAudioProcessorEditor::playPauseToggleRequested()
{
    // Toggle play/pause
    if (processorRef_.isPlaying()) {
        pauseRequested();
    } else {
        playRequested();
    }
}

void OpenTuneAudioProcessorEditor::stopPlaybackRequested()
{
    stopRequested();
}

void OpenTuneAudioProcessorEditor::playFromStartToggleRequested()
{
    if (processorRef_.isPlaying()) {
        processorRef_.setPlaying(false);
        double startPos = processorRef_.getPlayStartPosition();
        processorRef_.setPosition(startPos);
        transportBar_.setPlaying(false);
        pianoRoll_.setIsPlaying(false);
        arrangementView_.setIsPlaying(false);
    } else {
        double startPos = processorRef_.getPlayStartPosition();
        processorRef_.setPosition(startPos);
        processorRef_.setPlaying(true);
        transportBar_.setPlaying(true);
        pianoRoll_.setIsPlaying(true);
        arrangementView_.setIsPlaying(true);
    }
}

void OpenTuneAudioProcessorEditor::autoTuneRequested()
{
    // 鍚堝苟閲嶅閫昏緫锛氳皟鐢ㄧ粺涓€ helper
    startAutoTuneAsUnifiedEdit();
    pianoRoll_.setCurrentTool(ToolId::Select);
    parameterPanel_.setActiveTool(1);
}

void OpenTuneAudioProcessorEditor::pitchCurveEdited(int startFrame, int endFrame)
{
    DBG("Editor: Pitch curve edited frames " + juce::String(startFrame) + " to " + juce::String(endFrame));

    const int trackId = processorRef_.getActiveTrackId();
    const int clipIndex = processorRef_.getSelectedClip(trackId);

    if (clipIndex < 0) {
        return;
    }

    processorRef_.enqueuePartialRenderForFrameRange(trackId, clipIndex, startFrame, endFrame);
}

void OpenTuneAudioProcessorEditor::trackTimeOffsetChanged(int trackId, double newOffset)
{
    juce::ignoreUnused(trackId, newOffset);
    // Update track offset in processor
    // NOTE: For now we just update it in UI, processor logic to be added
    // processorRef_.setTrackTimeOffset(trackId, newOffset);
}

void OpenTuneAudioProcessorEditor::arrangementClipContextMenu(int trackId, int clipIndex, juce::Point<int> screenPos)
{
    enum MenuIds : int {
        SplitAtPlayhead = 1,
        MergeWithNext
    };

    const double posSec = processorRef_.getPosition();
    const double cStart = processorRef_.getClipStartSeconds(trackId, clipIndex);
    std::shared_ptr<const juce::AudioBuffer<float>> clipBuffer =
        processorRef_.getClipAudioBuffer(trackId, clipIndex);
    double clipDurSec = 0.0;
    if (clipBuffer) {
        clipDurSec = static_cast<double>(clipBuffer->getNumSamples())
            / OpenTuneAudioProcessor::getStoredAudioSampleRate();
    }
    const double cEnd = cStart + clipDurSec;
    const bool playheadInside = clipDurSec > 0.2 && posSec > cStart + 0.08 && posSec < cEnd - 0.08;
    const bool canMerge = processorRef_.canMergeAdjacentClips(trackId, clipIndex);

    juce::PopupMenu menu;
    menu.addItem(SplitAtPlayhead, "Split at playhead", playheadInside, false);
    menu.addItem(MergeWithNext, "Merge with next clip", canMerge, false);

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
        [safeThis, trackId, clipIndex, playheadInside, canMerge](int result) {
            if (safeThis == nullptr || result == 0) {
                return;
            }

            if (result == SplitAtPlayhead && playheadInside) {
                const int originalClipIndex = clipIndex;
                const uint64_t originalClipId = safeThis->processorRef_.getClipId(trackId, clipIndex);
                double originalDuration = 0.0;
                std::shared_ptr<const juce::AudioBuffer<float>> buf =
                    safeThis->processorRef_.getClipAudioBuffer(trackId, clipIndex);
                if (buf) {
                    originalDuration = static_cast<double>(buf->getNumSamples())
                        / OpenTuneAudioProcessor::getStoredAudioSampleRate();
                }

                const double splitSeconds = safeThis->processorRef_.getPosition();
                if (safeThis->processorRef_.splitClipAtSeconds(trackId, clipIndex, splitSeconds)) {
                    int newClipIndex = safeThis->processorRef_.getSelectedClip(trackId);
                    uint64_t newClipId = 0;
                    if (newClipIndex >= 0 && newClipIndex < safeThis->processorRef_.getNumClips(trackId)) {
                        newClipId = safeThis->processorRef_.getClipId(trackId, newClipIndex);
                    }

                    ClipSplitAction::SplitResult splitResult;
                    splitResult.newClipId = newClipId;
                    splitResult.splitSeconds = splitSeconds;
                    splitResult.originalClipDuration = originalDuration;

                    safeThis->processorRef_.getUndoManager().addAction(std::make_unique<ClipSplitAction>(
                        safeThis->processorRef_, trackId, originalClipId, splitResult, originalClipIndex, newClipIndex));

                    safeThis->clipTimingChanged(trackId, newClipIndex);
                    safeThis->arrangementView_.repaint();
                }
                return;
            }

            if (result == MergeWithNext && canMerge) {
                const uint64_t leftId = safeThis->processorRef_.getClipId(trackId, clipIndex);
                std::shared_ptr<const juce::AudioBuffer<float>> leftBuf =
                    safeThis->processorRef_.getClipAudioBuffer(trackId, clipIndex);
                if (!leftBuf) {
                    return;
                }
                const double jointTimelineSeconds = safeThis->processorRef_.getClipStartSeconds(trackId, clipIndex + 1);

                if (safeThis->processorRef_.mergeAdjacentClips(trackId, clipIndex)) {
                    safeThis->processorRef_.getUndoManager().addAction(std::make_unique<ClipMergeAction>(
                        safeThis->processorRef_, trackId, leftId, jointTimelineSeconds));
                    safeThis->clipSelectionChanged(trackId, clipIndex);
                    safeThis->arrangementView_.repaint();
                }
            }
        });
}

void OpenTuneAudioProcessorEditor::escapeKeyPressed()
{
    // Previously toggled arrangement/piano full-screen; unified layout uses Escape only from piano roll
    // when there is no note selection (see PianoRollToolHandler).
}

void OpenTuneAudioProcessorEditor::toolChanged(int toolId)
{
    parameterPanel_.setActiveTool(toolId);
}

void OpenTuneAudioProcessorEditor::audioSettingsRequested()
{
    processorRef_.showAudioSettingsDialog(*this);
}

// AUTO 鍚姩缁熶竴 helper锛氬悎骞?toolSelected(AutoTune) 涓?autoTuneRequested() 鐨勯噸澶嶉€昏緫
void OpenTuneAudioProcessorEditor::startAutoTuneAsUnifiedEdit()
{
    const int trackId = processorRef_.getActiveTrackId();
    const int clipIndex = processorRef_.getSelectedClip(trackId);
    if (trackId < 0 || clipIndex < 0) {
        return;
    }

    const auto f0State = processorRef_.getClipOriginalF0State(trackId, clipIndex);

    if (f0State == OriginalF0State::Extracting) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "OriginalF0",
            "OriginalF0 is being extracted. Please retry in a moment.");
        return;
    }

    if (f0State == OriginalF0State::Failed) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "OriginalF0",
            "OriginalF0 extraction failed for this clip. Re-import the audio to regenerate OriginalF0.");
        return;
    }

    if (f0State != OriginalF0State::Ready) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "OriginalF0",
            "OriginalF0 is not ready for this clip.");
        return;
    }

    // 灏濊瘯鍚姩 AUTO 澶勭悊
    bool success = pianoRoll_.applyAutoTuneToSelection();
    if (success) {
        autoOverlayLatched_ = true;
        autoOverlayTargetTrackId_ = trackId;
        autoOverlayTargetClipId_ = processorRef_.getClipId(trackId, clipIndex);
        autoRenderOverlay_.setMessageText("Rendering...");
        autoRenderOverlay_.setVisible(true);
    }
}

} // namespace OpenTune
