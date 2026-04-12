#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "UI/UIColors.h"
#include "UI/FrameScheduler.h"
#include "UI/OptionsDialogComponent.h"
#include "UI/StemExportDialogComponent.h"
#include "Utils/LastFileDialogPaths.h"
#include "Utils/AppLogger.h"
#include "Utils/UndoAction.h"

namespace OpenTune {

namespace {

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

    return "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3";
}

static juce::File getDefaultOpenTuneProjectsDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("OpenTune")
        .getChildFile("Projects");
}

} // namespace

// ============================================================================
// MenuBarComponent::Listener Implementation
// ============================================================================

// 瀵煎叆妯″紡鏋氫妇
enum class ImportMode
{
    SameTrack,      // 鎸夐『搴忓鍏ュ埌鍚屼竴涓建閬?
    SeparateTracks  // 鍒嗗埆瀵煎叆鍒板涓建閬擄紙榻愬ご锛?
};

void OpenTuneAudioProcessorEditor::importAudioRequested()
{
    // 鑿滃崟瀵煎叆锛氬鍏ュ埌绗竴鏉＄┖杞ㄩ亾锛涙棤绌鸿建鏃舵墿灞曗€?杞ㄢ€濆悗鍐嶅鍏?
    DBG("OpenTuneAudioProcessorEditor::importAudioRequested called");

    if (isImportInProgress_)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Import Audio",
            "An audio import is already in progress. Please try again in a moment."
        );
        return;
    }

    const auto wildcardFilter = getImportWildcardFilter();
    const juce::File importStartDir = LastFileDialogPaths::directoryForOpen(
        FileDialogKind::ImportAudio,
        juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    auto chooser = std::make_shared<juce::FileChooser>(
        "Choose Audio File(s)",
        importStartDir,
        wildcardFilter
    );

    // 鏀寔澶氶€?
    auto chooserFlags = juce::FileBrowserComponent::openMode 
                      | juce::FileBrowserComponent::canSelectFiles 
                      | juce::FileBrowserComponent::canSelectMultipleItems;

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);

    chooser->launchAsync(chooserFlags, [safeThis, chooser](const juce::FileChooser& fc)
    {
        if (safeThis == nullptr)
            return;

        const juce::Array<juce::File>& selectedFiles = fc.getResults();
        
        if (selectedFiles.isEmpty())
        {
            DBG("No files selected");
            return;
        }

        LastFileDialogPaths::rememberOpenSelection(FileDialogKind::ImportAudio,
                                                   selectedFiles.getReference(0));

        int baseTrack = safeThis->findFirstEmptyTrackIndexForMenuImport();
        if (baseTrack < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Import Audio",
                "All tracks already contain audio clips.\nClear a track before importing new audio."
            );
            return;
        }

        int visibleTracks = safeThis->trackPanel_.getVisibleTrackCount();

        if (selectedFiles.size() == 1)
        {
            safeThis->importAudioFileToTrack(baseTrack, selectedFiles[0]);
        }
        else
        {
            // 澶氭枃浠讹細妫€鏌ユ暟閲忛檺鍒讹紙鏈€澶?2涓級
            constexpr int MAX_IMPORT_FILES = 12;
            if (selectedFiles.size() > MAX_IMPORT_FILES)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Too Many Files",
                    "You can import at most 12 audio files at once.\nSelected: " + juce::String(selectedFiles.size())
                );
                return;
            }

            // 澶氭枃浠讹細寮圭獥璇㈤棶瀵煎叆妯″紡
            auto* alert = new juce::AlertWindow(
        "Choose Import Mode",
        "You selected " + juce::String(selectedFiles.size()) + " audio files. Choose how to import them:",
        juce::AlertWindow::QuestionIcon
    );

            alert->addButton("Import To Same Track", 1);
            alert->addButton("Import To Separate Tracks", 2);
            alert->addButton("Cancel", 0);

            // 淇濆瓨鏂囦欢鍒楄〃渚涘洖璋冧娇鐢?
            auto filesPtr = std::make_shared<juce::Array<juce::File>>(selectedFiles);

            alert->enterModalState(
                true,
                juce::ModalCallbackFunction::create([safeThis, filesPtr, baseTrack, visibleTracks](int result)
                {
                    if (safeThis == nullptr)
                        return;

                    if (result == 0)
                    {
                        // 鐢ㄦ埛鍙栨秷
                        return;
                    }
                    else if (result == 1)
                    {
                        // 椤哄簭瀵煎叆鍒板悓涓€绌鸿建閬擄紙杩藉姞澶氫釜 Clip锛?
                        for (int i = 0; i < filesPtr->size(); ++i)
                        {
                            safeThis->importAudioFileToTrack(baseTrack, (*filesPtr)[i]);
                        }
                    }
                    else if (result == 2)
                    {
                        // 浠庣涓€鏉＄┖杞ㄩ亾璧凤紝渚濇瀵煎叆鍒板悗缁建閬?
                        int numFiles = filesPtr->size();

                        const int requiredLastIndex = baseTrack + numFiles - 1;
                        if (requiredLastIndex >= OpenTuneAudioProcessor::MAX_TRACKS)
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "Import Audio",
                                "There are not enough tracks available to place each file on its own track."
                            );
                            return;
                        }

                        const int newVisible = juce::jmax(visibleTracks, requiredLastIndex + 1);
                        if (newVisible > visibleTracks)
                            safeThis->trackPanel_.setVisibleTrackCount(newVisible);

                        for (int i = 0; i < numFiles; ++i)
                        {
                            int targetTrack = baseTrack + i;
                            safeThis->importAudioFileToTrackWithOverwritePrompt(
                                targetTrack, (*filesPtr)[i], true);
                        }
                    }
                }),
                true  // 鑷姩鍒犻櫎AlertWindow
            );
        }
    });
}

int OpenTuneAudioProcessorEditor::resolveTrackIndexForAudioDrop(int editorX, int editorY) const
{
    juce::Point<int> p(editorX, editorY);

    if (isWorkspaceView_ && arrangementView_.isVisible() && arrangementView_.getBounds().contains(p))
    {
        const auto local = arrangementView_.getLocalPoint(this, p);
        return arrangementView_.getTrackIndexAtPoint(local);
    }

    if (trackPanel_.isVisible() && trackPanel_.getBounds().contains(p))
    {
        const auto local = trackPanel_.getLocalPoint(this, p);
        const int h = trackPanel_.getTrackHeight();
        if (h <= 0)
            return -1;
        const int yOff = trackPanel_.getTrackStartYOffset();
        const int scroll = trackPanel_.getVerticalScrollOffset();
        const int t = (local.y - yOff + scroll) / h;
        if (t >= 0 && t < trackPanel_.getVisibleTrackCount())
            return t;
        return -1;
    }

    if (!isWorkspaceView_ && pianoRoll_.isVisible() && pianoRoll_.getBounds().contains(p))
        return processorRef_.getActiveTrackId();

    return -1;
}

int OpenTuneAudioProcessorEditor::findFirstEmptyTrackIndexForMenuImport()
{
    for (int t = 0; t < OpenTuneAudioProcessor::MAX_TRACKS; ++t)
    {
        if (processorRef_.getNumClips(t) == 0)
        {
            const int visible = trackPanel_.getVisibleTrackCount();
            if (t >= visible)
                trackPanel_.setVisibleTrackCount(t + 1);
            return t;
        }
    }
    return -1;
}

void OpenTuneAudioProcessorEditor::clearAllClipsOnTrack(int trackId)
{
    if (trackId < 0 || trackId >= OpenTuneAudioProcessor::MAX_TRACKS)
        return;
    while (processorRef_.getNumClips(trackId) > 0)
        processorRef_.deleteClip(trackId, 0);
}

void OpenTuneAudioProcessorEditor::importAudioFileToTrackWithOverwritePrompt(int trackId, const juce::File& file, bool replaceExisting)
{
    if (!replaceExisting || processorRef_.getNumClips(trackId) == 0)
    {
        importAudioFileToTrack(trackId, file);
        return;
    }

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
    auto* alert = new juce::AlertWindow(
        "Import Audio",
        "This track already contains clips. Replace them before importing?",
        juce::AlertWindow::WarningIcon);
    alert->addButton("Replace", 1);
    alert->addButton("Cancel", 0);
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safeThis, trackId, file](int result)
        {
            if (safeThis == nullptr || result != 1)
                return;
            safeThis->clearAllClipsOnTrack(trackId);
            safeThis->importAudioFileToTrack(trackId, file);
        }),
        true);
}

void OpenTuneAudioProcessorEditor::importAudioFileToTrack(int trackId, const juce::File& file)
{
    // 濡傛灉姝ｅ湪瀵煎叆锛屽皢璇锋眰鍔犲叆闃熷垪
    if (isImportInProgress_)
    {
        importQueue_.push_back({trackId, file});
        return;
    }

    isImportInProgress_ = true;

    juce::ignoreUnused(trackId);

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);

    asyncAudioLoader_.loadAudioFile(
        file,
        {},
        [safeThis, trackId, fileName = file.getFileName(), sourcePath = file.getFullPathName()](AsyncAudioLoader::LoadResult result)
        {
            if (safeThis == nullptr)
                return;

            if (!result.success)
            {
                safeThis->isImportInProgress_ = false;
                safeThis->processNextImportInQueue();
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Import Failed",
                    result.errorMessage
                );
                return;
            }

            // 鍏抽敭淇锛歱repare 闃舵蹇呴』鍦ㄥ悗鍙扮嚎绋嬫墽琛岋紝涓嶈兘闃诲娑堟伅绾跨▼
            safeThis->launchBackgroundUiTask([safeThis,
                                              trackId,
                                              fileName,
                                              sourcePath,
                                              sampleRate = result.sampleRate,
                                              audioBuffer = std::move(result.audioBuffer)]() mutable
            {
                if (safeThis == nullptr)
                    return;

                OpenTuneAudioProcessor::PreparedImportClip prepared;
                {
                    PerfTimer perfPrepare("import_prepare_phase_background");
                    if (!safeThis->processorRef_.prepareImportClip(trackId, std::move(audioBuffer), sampleRate, fileName, prepared))
                    {
                        juce::MessageManager::callAsync([safeThis]()
                        {
                            if (safeThis == nullptr)
                                return;
                            safeThis->isImportInProgress_ = false;
                            safeThis->processNextImportInQueue();
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "Import Failed",
                                "Import pre-processing failed. Please try again."
                            );
                        });
                        return;
                    }
                    prepared.sourceAudioAbsolutePath = sourcePath;
                }

                juce::MessageManager::callAsync([safeThis, trackId, prepared = std::move(prepared)]() mutable
                {
                    if (safeThis == nullptr)
                        return;

                    PerfTimer perfCommit("import_commit_phase");

                    if (!safeThis->processorRef_.commitPreparedImportClip(std::move(prepared)))
                    {
                        safeThis->isImportInProgress_ = false;
                        safeThis->processNextImportInQueue();
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::WarningIcon,
                            "Import Failed",
                            "Import commit failed. Please try again."
                        );
                        return;
                    }

                    // 鑾峰彇鏂板垱寤虹殑 clip ID 骞跺垱寤?Undo Action
                    int newClipIndex = safeThis->processorRef_.getNumClips(trackId) - 1;
                    uint64_t newClipId = 0;
                    if (newClipIndex >= 0) {
                        newClipId = safeThis->processorRef_.getClipId(trackId, newClipIndex);
                        safeThis->processorRef_.getUndoManager().addAction(
                            std::make_unique<ClipCreateAction>(safeThis->processorRef_, trackId, newClipId)
                        );
                    }

                    // 鏍囪褰撳墠瀵煎叆瀹屾垚
                    safeThis->isImportInProgress_ = false;

                    // 璁剧疆閿洏鐒︾偣鍒?ArrangementView锛岀‘淇?Ctrl+Z/Y 蹇嵎閿兘姝ｅ父宸ヤ綔
                    safeThis->arrangementView_.grabKeyboardFocus();

                    safeThis->processorRef_.setActiveTrack(trackId);
                    safeThis->trackPanel_.setActiveTrack(trackId);

                    int clipIndex = safeThis->processorRef_.getNumClips(trackId) - 1;
                    if (clipIndex < 0) {
                        clipIndex = 0;
                    }
                    safeThis->processorRef_.setSelectedClip(trackId, clipIndex);
                    clipIndex = safeThis->processorRef_.getSelectedClip(trackId);
                    int numClips = safeThis->processorRef_.getNumClips(trackId);
                    if (numClips > 0) {
                        jassert(clipIndex == numClips - 1);
                    }


                    // 缁熶竴浣跨敤 syncPianoRollFromClipSelection 璁剧疆 PianoRoll 鐘舵€?
                    // 鍖呮嫭 setActiveTrackId銆乻etCurrentClipContext銆乻etAudioBuffer 绛?
                    safeThis->syncPianoRollFromClipSelection(trackId, clipIndex);

                    // 瀵煎叆鍒氬畬鎴愭椂 F0 鏁版嵁灏氫笉瀛樺湪锛岃鐩?sync 涓彲鑳芥仮澶嶇殑鏃ф暟鎹?
                    safeThis->pianoRoll_.setPitchCurve(nullptr);
                    safeThis->pianoRoll_.setNotes({});

                    if (newClipId != 0) {
                        safeThis->arrangementView_.prioritizeWaveformBuildForClip(trackId, newClipId);
                        safeThis->deferredImportPostProcessQueue_.push_back({trackId, newClipId});
                    }

                    // 瀵煎叆瀹屾垚 - 鍗曟 UI 鍒锋柊
                    safeThis->arrangementView_.resetUserZoomFlag();
                    safeThis->pianoRoll_.resetUserZoomFlag();

                    FrameScheduler::instance().requestInvalidate(safeThis->arrangementView_, FrameScheduler::Priority::Interactive);
                    FrameScheduler::instance().requestInvalidate(safeThis->pianoRoll_, FrameScheduler::Priority::Interactive);

                    juce::Timer::callAfterDelay(100, [safeThis]() {
                        if (safeThis != nullptr && !safeThis->arrangementView_.hasUserManuallyZoomed()) {
                            safeThis->arrangementView_.fitToContent();
                        }
                    });

                    safeThis->processNextImportInQueue();
                });
            });
        }
    );
}

// 澶勭悊瀵煎叆闃熷垪涓殑涓嬩竴涓枃浠?
void OpenTuneAudioProcessorEditor::processNextImportInQueue()
{
    if (importQueue_.empty())
        return;
    
    // 鍙栧嚭闃熷垪涓殑绗竴涓緟瀵煎叆椤?
    auto next = importQueue_.front();
    importQueue_.erase(importQueue_.begin());
    
    // 閫掑綊璋冪敤瀵煎叆鍑芥暟锛堟鏃?isImportInProgress_ 宸茬粡鏄?false锛?
    importAudioFileToTrack(next.trackId, next.file);
}

void OpenTuneAudioProcessorEditor::processDeferredImportPostProcessQueue()
{
    if (deferredImportPostProcessQueue_.empty()) {
        return;
    }

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);

    for (int i = static_cast<int>(deferredImportPostProcessQueue_.size()) - 1; i >= 0; --i)
    {
        const auto request = deferredImportPostProcessQueue_[static_cast<std::size_t>(i)];
        if (request.trackId < 0 || request.clipId == 0) {
            deferredImportPostProcessQueue_.erase(deferredImportPostProcessQueue_.begin() + i);
            continue;
        }

        deferredImportPostProcessQueue_.erase(deferredImportPostProcessQueue_.begin() + i);

        launchBackgroundUiTask([safeThis, request]() mutable
        {
            if (safeThis == nullptr) {
                return;
            }

            OpenTuneAudioProcessor::PreparedClipPostProcess prepared;
            if (!safeThis->processorRef_.prepareDeferredClipPostProcess(request.trackId, request.clipId, prepared)) {
                return;
            }

            juce::MessageManager::callAsync([safeThis, request, prepared = std::move(prepared)]() mutable
            {
                if (safeThis == nullptr) {
                    return;
                }

                if (!safeThis->processorRef_.commitDeferredClipPostProcess(request.trackId, request.clipId, std::move(prepared))) {
                    return;
                }

                const int clipIndex = safeThis->processorRef_.findClipIndexById(request.trackId, request.clipId);
                if (clipIndex >= 0) {
                    safeThis->requestOriginalF0ExtractionForImport(request.trackId, clipIndex);
                }
            });
        });
    }
}

void OpenTuneAudioProcessorEditor::exportAudioRequested(MenuBarComponent::ExportType exportType)
{
    using ExportType = MenuBarComponent::ExportType;
    
    // Check if export is already in progress
    if (exportInProgress_.load())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Export Audio",
            "An export is already in progress. Please try again later.");
        return;
    }
    
    // 鏍规嵁瀵煎嚭绫诲瀷纭畾榛樿鏂囦欢鍚?
    juce::String defaultFileName;
    switch (exportType)
    {
        case ExportType::SelectedClip:
            defaultFileName = "selected_clip.wav";
            break;
        case ExportType::Track:
            defaultFileName = "track_" + juce::String(processorRef_.getActiveTrackId() + 1) + ".wav";
            break;
        case ExportType::Bus:
            defaultFileName = "master_mix.wav";
            break;
    }

    const juce::File exportDefaultFile =
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(defaultFileName);
    const juce::File exportStartFile =
        LastFileDialogPaths::startingFileForSave(FileDialogKind::ExportAudioWav, exportDefaultFile);
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Audio File",
        exportStartFile,
        "*.wav");

    auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);

    chooser->launchAsync(chooserFlags, [safeThis, exportType, chooser](const juce::FileChooser& fc)
    {
        if (safeThis == nullptr)
            return;

        auto file = fc.getResult();
        if (file == juce::File{})
            return;

        LastFileDialogPaths::rememberSaveSelection(FileDialogKind::ExportAudioWav, file);

        struct ExportRequest final
        {
            ExportType type{ ExportType::Bus };
            int trackId{ -1 };
            int clipIndex{ -1 };
            juce::String targetName;
        };

        ExportRequest request;
        request.type = exportType;

        switch (exportType)
        {
            case ExportType::SelectedClip:
            {
                request.trackId = safeThis->processorRef_.getActiveTrackId();
                request.clipIndex = safeThis->processorRef_.getSelectedClip(request.trackId);

                if (request.clipIndex < 0)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Export Failed",
                        "No audio clip is selected. Select a clip before exporting.");
                    return;
                }

                request.targetName = "Selected Clip (Track "
                    + juce::String(request.trackId + 1)
                    + ", Clip " + juce::String(request.clipIndex + 1) + ")";
                break;
            }

            case ExportType::Track:
            {
                request.trackId = safeThis->processorRef_.getActiveTrackId();
                request.targetName = "Track " + juce::String(request.trackId + 1);
                break;
            }

            case ExportType::Bus:
            {
                request.targetName = "Bus (Master Mix)";
                break;
            }
        }

        auto* processor = &safeThis->processorRef_;
        const auto outFile = file;
        const auto outRequest = request;
        const juce::Component::SafePointer<OpenTuneAudioProcessorEditor> uiSafe = safeThis;

        // Join previous export thread if it exists
        if (safeThis->exportWorker_.joinable())
        {
            safeThis->exportWorker_.join();
        }

        // Set export in progress flag
        safeThis->exportInProgress_.store(true);

        // Create new controlled export thread
        safeThis->exportWorker_ = std::thread([processor, outFile, outRequest, uiSafe]()
            {
                bool ok = false;
                juce::String errorText;

                switch (outRequest.type)
                {
                    case ExportType::SelectedClip:
                        ok = processor->exportClipAudio(outRequest.trackId, outRequest.clipIndex, outFile);
                        break;
                    case ExportType::Track:
                        ok = processor->exportTrackAudio(outRequest.trackId, outFile);
                        break;
                    case ExportType::Bus:
                        ok = processor->exportMasterMixAudio(outFile);
                        break;
                }

                if (!ok)
                {
                    errorText = processor->getLastExportError();
                }

                juce::MessageManager::callAsync([ok, outFile, outRequest, errorText, uiSafe]()
                {
                    // Check if editor is still alive
                    if (uiSafe == nullptr)
                        return;

                    // Clear export in progress flag
                    uiSafe->exportInProgress_.store(false);

                    if (ok)
                    {
                        DBG("Successfully exported " + outRequest.targetName);
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::InfoIcon,
                            LOC(kExportStemsCompleteTitle),
                            Loc::format(LOC(kExportAudioCompleteMessage),
                                        outRequest.targetName,
                                        outFile.getFullPathName()));
                        return;
                    }

                    juce::String failText = Loc::format(LOC(kExportAudioFailedMessage), outFile.getFullPathName());
                    if (errorText.isNotEmpty())
                        failText += Loc::format(LOC(kExportAudioFailedReason), errorText);

                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        LOC(kExportAudioFailedTitle),
                        failText);
                });
            });
    });
}

void OpenTuneAudioProcessorEditor::exportStemsRequested()
{
    if (exportInProgress_.load())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Export Audio",
            "An export is already in progress. Please try again later.");
        return;
    }

    juce::String defaultPrefix;
    if (sessionProjectFile_.getFullPathName().isNotEmpty())
        defaultPrefix = sessionProjectFile_.getFileNameWithoutExtension();

    auto* content = new StemExportDialogContent(processorRef_, defaultPrefix);
    content->setSize(440, 400);

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);

    content->setOnAccepted([safeThis](juce::String prefix, juce::Array<int> trackIds)
    {
        if (safeThis == nullptr)
            return;

        safeThis->launchStemExportFolderChooser(std::move(prefix), std::move(trackIds));
    });

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = LOC(kExportStemsTitle);
    options.dialogBackgroundColour = UIColors::backgroundDark;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.content.setOwned(content);
    options.componentToCentreAround = this;
    options.launchAsync();
}

void OpenTuneAudioProcessorEditor::launchStemExportFolderChooser(juce::String prefix, juce::Array<int> trackIds)
{
    const juce::File stemsStartDir = LastFileDialogPaths::directoryForOpen(
        FileDialogKind::ExportStemsFolder,
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    auto chooser = std::make_shared<juce::FileChooser>(
        LOC(kExportStemsChooseFolder),
        stemsStartDir,
        juce::String());

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectDirectories
                     | juce::FileBrowserComponent::filenameBoxIsReadOnly;

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);

    chooser->launchAsync(chooserFlags, [safeThis, chooser, prefix = std::move(prefix), trackIds = std::move(trackIds)](
                                  const juce::FileChooser& fc) mutable
    {
        if (safeThis == nullptr)
            return;

        const juce::File dir = fc.getResult();
        if (dir == juce::File{})
            return;

        if (!dir.isDirectory())
            return;

        LastFileDialogPaths::rememberDirectory(FileDialogKind::ExportStemsFolder, dir);

        safeThis->startStemExportWorker(std::move(prefix), std::move(trackIds), dir);
    });
}

void OpenTuneAudioProcessorEditor::startStemExportWorker(juce::String prefix, juce::Array<int> trackIds, juce::File outputDir)
{
    trackIds.sort();

    if (exportWorker_.joinable())
        exportWorker_.join();

    exportInProgress_.store(true);

    auto* audioProcessor = &processorRef_;
    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> uiSafe(this);

    exportWorker_ = std::thread([audioProcessor, prefix = std::move(prefix), trackIds, outputDir, uiSafe]() mutable
    {
        int okCount = 0;
        juce::String lastError;

        for (int tid : trackIds)
        {
            juce::String fileName;
            if (prefix.isEmpty())
                fileName = "Track " + juce::String(tid + 1) + ".wav";
            else
                fileName = prefix + "-Track " + juce::String(tid + 1) + ".wav";

            const juce::File outFile = outputDir.getChildFile(fileName);
            if (audioProcessor->exportTrackAudio(tid, outFile))
            {
                ++okCount;
            }
            else if (lastError.isEmpty())
            {
                lastError = audioProcessor->getLastExportError();
            }
        }

        const int total = trackIds.size();
        const juce::String folderPath = outputDir.getFullPathName();

        juce::MessageManager::callAsync([uiSafe, okCount, total, folderPath, lastError]()
        {
            if (uiSafe == nullptr)
                return;

            uiSafe->exportInProgress_.store(false);

            if (okCount == total)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    LOC(kExportStemsCompleteTitle),
                    Loc::format(LOC(kExportStemsCompleteMessage), juce::String(okCount), folderPath));
            }
            else if (okCount > 0)
            {
                juce::String msg = Loc::format(LOC(kExportStemsCompleteMessage), juce::String(okCount), folderPath);
                if (lastError.isNotEmpty())
                    msg << juce::String::fromUTF8("\n") << lastError;

                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    LOC(kExportStemsCompleteTitle),
                    msg);
            }
            else
            {
                const juce::String msg = lastError.isNotEmpty()
                                              ? lastError
                                              : juce::String::fromUTF8("Unknown error");

                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    LOC(kExportStemsFailedTitle),
                    msg);
            }
        });
    });
}

void OpenTuneAudioProcessorEditor::markSessionNeedsSave()
{
    if (suppressSessionNeedsSave_) {
        return;
    }
    sessionNeedsSave_ = true;
}

void OpenTuneAudioProcessorEditor::clearSessionNeedsSave()
{
    sessionNeedsSave_ = false;
}

bool OpenTuneAudioProcessorEditor::shouldPromptForUnsavedSession() const
{
    if (!sessionNeedsSave_)
        return false;
    if (sessionProjectFile_.existsAsFile())
        return true;
    return processorRef_.hasAnyClipOnAnyTrack();
}

void OpenTuneAudioProcessorEditor::finishNewProject()
{
    processorRef_.resetToNewEmptyProject();
    sessionProjectFile_ = {};
    clearSessionNeedsSave();
    syncUiAfterProjectLoad();
    menuBar_.menuItemsChanged();
}

void OpenTuneAudioProcessorEditor::checkUnsavedChangesThen(std::function<void()> onProceed)
{
    if (!shouldPromptForUnsavedSession()) {
        clearSessionNeedsSave();
        onProceed();
        return;
    }

    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel(
        juce::MessageBoxIconType::QuestionIcon,
        LOC(kUnsavedChangesTitle),
        LOC(kUnsavedChangesLoadMessage),
        LOC(kSave),
        LOC(kDontSave),
        LOC(kCancel),
        this);

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, proceed = std::move(onProceed)](int result) mutable {
        if (safeThis == nullptr) {
            return;
        }

        if (result == 0 || result == 3) {
            return;
        }

        if (result == 2) {
            proceed();
            return;
        }

        if (result != 1) {
            return;
        }

        if (safeThis->sessionProjectFile_.existsAsFile()) {
            if (!safeThis->processorRef_.saveProjectToFile(safeThis->sessionProjectFile_)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    LOC(kProjectSaveFailedTitle),
                    LOC(kProjectSaveFailedMessage));
                return;
            }
            safeThis->recentProjects_.add(safeThis->sessionProjectFile_);
            safeThis->menuBar_.menuItemsChanged();
            safeThis->clearSessionNeedsSave();
            proceed();
            return;
        }

        safeThis->runSaveProjectDialogThen([proceed = std::move(proceed)]() {
            proceed();
        });
    });
}

void OpenTuneAudioProcessorEditor::runSaveProjectDialogThen(std::function<void()> onSavedToDisk)
{
    juce::File dir = getDefaultOpenTuneProjectsDirectory();
    if (!dir.exists()) {
        (void)dir.createDirectory();
    }

    const juce::File projectSaveStart = LastFileDialogPaths::directoryForOpen(FileDialogKind::SaveProject, dir);

    auto chooser = std::make_shared<juce::FileChooser>(
        LOC(kSaveProject),
        projectSaveStart,
        "*.otproject");

    const auto chooserFlags =
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this, chooser, onSaved = std::move(onSavedToDisk)](const juce::FileChooser& fc) mutable {
        juce::File file = fc.getResult();
        if (file == juce::File{}) {
            return;
        }
        if (!file.hasFileExtension(".otproject")) {
            file = file.withFileExtension(".otproject");
        }

        LastFileDialogPaths::rememberSaveSelection(FileDialogKind::SaveProject, file);

        if (!processorRef_.saveProjectToFile(file)) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                LOC(kProjectSaveFailedTitle),
                LOC(kProjectSaveFailedMessage));
            return;
        }

        sessionProjectFile_ = file;
        recentProjects_.add(file);
        menuBar_.menuItemsChanged();
        clearSessionNeedsSave();
        onSaved();
    });
}

void OpenTuneAudioProcessorEditor::newProjectRequested()
{
    if (!shouldPromptForUnsavedSession()) {
        clearSessionNeedsSave();
        finishNewProject();
        return;
    }

    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel(
        juce::MessageBoxIconType::QuestionIcon,
        LOC(kUnsavedChangesTitle),
        LOC(kUnsavedChangesMessage),
        LOC(kSave),
        LOC(kDontSave),
        LOC(kCancel),
        this);

    juce::Component::SafePointer<OpenTuneAudioProcessorEditor> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis](int result) {
        if (safeThis == nullptr) {
            return;
        }

        if (result == 0 || result == 3) {
            return;
        }

        if (result == 2) {
            safeThis->finishNewProject();
            return;
        }

        if (result != 1) {
            return;
        }

        if (safeThis->sessionProjectFile_.existsAsFile()) {
            if (!safeThis->processorRef_.saveProjectToFile(safeThis->sessionProjectFile_)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    LOC(kProjectSaveFailedTitle),
                    LOC(kProjectSaveFailedMessage));
                return;
            }
            safeThis->recentProjects_.add(safeThis->sessionProjectFile_);
            safeThis->menuBar_.menuItemsChanged();
            safeThis->clearSessionNeedsSave();
            safeThis->finishNewProject();
            return;
        }

        safeThis->runSaveProjectDialogThen([safeThis]() {
            if (safeThis != nullptr) {
                safeThis->finishNewProject();
            }
        });
    });
}

void OpenTuneAudioProcessorEditor::quickSaveProject()
{
    if (sessionProjectFile_.existsAsFile()) {
        if (processorRef_.saveProjectToFile(sessionProjectFile_)) {
            recentProjects_.add(sessionProjectFile_);
            menuBar_.menuItemsChanged();
            clearSessionNeedsSave();
        } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                LOC(kProjectSaveFailedTitle),
                LOC(kProjectSaveFailedMessage));
        }
        return;
    }

    saveProjectRequested();
}

void OpenTuneAudioProcessorEditor::saveProjectRequested()
{
    if (sessionProjectFile_.existsAsFile()) {
        quickSaveProject();
        return;
    }

    juce::File dir = getDefaultOpenTuneProjectsDirectory();
    if (!dir.exists()) {
        (void)dir.createDirectory();
    }

    const juce::File projectSaveStart = LastFileDialogPaths::directoryForOpen(FileDialogKind::SaveProject, dir);

    auto chooser = std::make_shared<juce::FileChooser>(
        LOC(kSaveProject),
        projectSaveStart,
        "*.otproject");

    const auto chooserFlags =
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File{}) {
            return;
        }
        if (!file.hasFileExtension(".otproject")) {
            file = file.withFileExtension(".otproject");
        }

        LastFileDialogPaths::rememberSaveSelection(FileDialogKind::SaveProject, file);

        if (processorRef_.saveProjectToFile(file)) {
            sessionProjectFile_ = file;
            recentProjects_.add(file);
            menuBar_.menuItemsChanged();
            clearSessionNeedsSave();
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                LOC(kProjectSavedTitle),
                Loc::format(LOC(kProjectSavedMessage), file.getFullPathName()));
        } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                LOC(kProjectSaveFailedTitle),
                LOC(kProjectSaveFailedMessage));
        }
    });
}

void OpenTuneAudioProcessorEditor::loadProjectRequested()
{
    auto doLoad = [this]() {
        juce::File dir = getDefaultOpenTuneProjectsDirectory();
        if (!dir.exists()) {
            (void)dir.createDirectory();
        }

        const juce::File projectLoadStart = LastFileDialogPaths::directoryForOpen(FileDialogKind::LoadProject, dir);

        auto chooser = std::make_shared<juce::FileChooser>(
            LOC(kLoadProject),
            projectLoadStart,
            "*.otproject");

        const auto chooserFlags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
            juce::File file = fc.getResult();
            if (file == juce::File{}) {
                return;
            }
            LastFileDialogPaths::rememberOpenSelection(FileDialogKind::LoadProject, file);
            openProjectFromFileWithUiFeedback(file);
        });
    };

    checkUnsavedChangesThen(std::move(doLoad));
}

void OpenTuneAudioProcessorEditor::recentProjectOpenRequested(const juce::File& file)
{
    checkUnsavedChangesThen([this, file]() {
        openProjectFromFileWithUiFeedback(file);
    });
}

void OpenTuneAudioProcessorEditor::openProjectFromFileWithUiFeedback(const juce::File& file)
{
    if (!processorRef_.loadProjectFromFile(file)) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            LOC(kProjectLoadFailedTitle),
            LOC(kProjectLoadFailedMessage));
        return;
    }

    sessionProjectFile_ = file;
    recentProjects_.add(file);
    menuBar_.menuItemsChanged();
    clearSessionNeedsSave();
    syncUiAfterProjectLoad();
}

void OpenTuneAudioProcessorEditor::preferencesRequested()
{
    auto* dialogContent = new OptionsDialogComponent();
    dialogContent->setSize(520, 540);
    
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialogContent);
    options.dialogTitle = "Options";
    options.dialogBackgroundColour = UIColors::backgroundDark;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    
    options.launchAsync();
}

void OpenTuneAudioProcessorEditor::helpRequested()
{
    auto exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto exeDir = exeFile.getParentDirectory();
#if JUCE_MAC
    // macOS: docs are in Contents/Resources/docs/ (executable is in Contents/MacOS/)
    auto helpFile = exeDir.getParentDirectory().getChildFile("Resources").getChildFile("docs").getChildFile("UserGuide.html");
#else
    // Windows: docs are alongside the executable
    auto helpFile = exeDir.getChildFile("docs").getChildFile("UserGuide.html");
#endif
    
    if (helpFile.exists())
    {
        helpFile.startAsProcess();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Help",
            "Unable to find the help document: " + helpFile.getFullPathName()
        );
    }
}

void OpenTuneAudioProcessorEditor::showWaveformToggled(bool shouldShow)
{
    pianoRoll_.setShowWaveform(shouldShow);
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::showLanesToggled(bool shouldShow)
{
    pianoRoll_.setShowLanes(shouldShow);
    markSessionNeedsSave();
}

void OpenTuneAudioProcessorEditor::mouseTrailThemeChanged(MouseTrailConfig::TrailTheme theme)
{
    MouseTrailConfig::setTheme(theme);
    repaint();
}

void OpenTuneAudioProcessorEditor::themeChanged(ThemeId themeId)
{
    Theme::setActiveTheme(themeId);
    UIColors::applyTheme(Theme::getActiveTokens());

    if (themeId == ThemeId::Aurora)
    {
        setLookAndFeel(&auroraLookAndFeel_);
    }
    else
    {
        setLookAndFeel(&openTuneLookAndFeel_);
        openTuneLookAndFeel_.setColour(juce::TextButton::buttonColourId, UIColors::buttonNormal);
        openTuneLookAndFeel_.setColour(juce::TextButton::buttonOnColourId, UIColors::accent);
        openTuneLookAndFeel_.setColour(juce::TextButton::textColourOffId, UIColors::textPrimary);
        openTuneLookAndFeel_.setColour(juce::TextButton::textColourOnId, UIColors::textPrimary);
    }

    getLookAndFeel().setColour(juce::ResizableWindow::backgroundColourId, UIColors::backgroundDark);

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        window->setColour(juce::DocumentWindow::backgroundColourId, UIColors::backgroundMedium);
        window->repaint();
    }

    topBar_.applyTheme();
    topBar_.setSidePanelsVisible(isTrackPanelVisible_, isParameterPanelVisible_);
    trackPanel_.applyTheme();
    parameterPanel_.applyTheme();

    // 鍚屾鎾斁澶撮鑹插埌楂樻€ц兘鎾斁澶磋鐩栧眰
    pianoRoll_.setPlayheadColour(UIColors::playhead);
    arrangementView_.setPlayheadColour(UIColors::playhead);

    menuBar_.repaint();
    topBar_.repaint();
    trackPanel_.repaint();
    parameterPanel_.repaint();
    arrangementView_.repaint();
    pianoRoll_.repaint();

    sendLookAndFeelChange();
    repaint();
}

void OpenTuneAudioProcessorEditor::undoRequested()
{
    performUndoWithRangeTracking();
}

void OpenTuneAudioProcessorEditor::redoRequested()
{
    performRedoWithRangeTracking();
}

void OpenTuneAudioProcessorEditor::noteNameModeChanged(int mode)
{
    pianoRoll_.setNoteNameDisplayMode(mode);
}

void OpenTuneAudioProcessorEditor::showNoteBlockNoteNamesToggled(bool shouldShow)
{
    pianoRoll_.setShowNoteBlockNoteNames(shouldShow);
}

void OpenTuneAudioProcessorEditor::languageChanged(Language newLanguage)
{
    juce::ignoreUnused(newLanguage);
    
    // 鍒锋柊鑿滃崟鏍?- JUCE 闇€瑕佽皟鐢?menuItemsChanged() 閲嶅缓鑿滃崟
    menuBar_.menuItemsChanged();
    menuBar_.repaint();
    
    // 鍒锋柊椤堕儴宸ュ叿鏍?
    transportBar_.refreshLocalizedText();
    topBar_.refreshLocalizedText();
    
    // 鍒锋柊鍙傛暟闈㈡澘
    parameterPanel_.refreshLocalizedText();
    
    // 鍒锋柊鏁翠釜鐣岄潰
    repaint();
}

void OpenTuneAudioProcessorEditor::refreshAfterUndoRedo()
{
    pianoRoll_.refreshAfterUndoRedo();
    arrangementView_.repaint();
    trackPanel_.repaint();
}

void OpenTuneAudioProcessorEditor::syncUiAfterProjectLoad()
{
    struct SuppressDirty {
        bool& flag;
        explicit SuppressDirty(bool& f) : flag(f) { flag = true; }
        ~SuppressDirty() { flag = false; }
    } guard(suppressSessionNeedsSave_);

    processorRef_.setPlaying(false);
    processorRef_.setPosition(0.0);
    transportBar_.setPlaying(false);
    transportBar_.setLooping(processorRef_.isLoopEnabled());
    transportBar_.setBpm(processorRef_.getBpm());
    pianoRoll_.setIsPlaying(false);
    arrangementView_.setIsPlaying(false);

    trackPanel_.setActiveTrack(processorRef_.getActiveTrackId());
    trackPanel_.setTrackHeight(processorRef_.getTrackHeight());
    for (int i = 0; i < OpenTuneAudioProcessor::MAX_TRACKS; ++i) {
        trackPanel_.setTrackMuted(i, processorRef_.isTrackMuted(i));
        trackPanel_.setTrackSolo(i, processorRef_.isTrackSolo(i));
        trackPanel_.setTrackVolume(i, processorRef_.getTrackVolume(i));
        lastTrackVolumes_[static_cast<size_t>(i)] = processorRef_.getTrackVolume(i);
    }

    arrangementView_.setZoomLevel(processorRef_.getZoomLevel());
    pianoRoll_.setShowWaveform(processorRef_.getShowWaveform());
    pianoRoll_.setShowLanes(processorRef_.getShowLanes());
    pianoRoll_.setZoomLevel(processorRef_.getZoomLevel());
    pianoRoll_.setBpm(processorRef_.getBpm());

    const int activeTrack = processorRef_.getActiveTrackId();
    const int clipIndex = processorRef_.getSelectedClip(activeTrack);
    const uint64_t clipId = processorRef_.getClipId(activeTrack, clipIndex);
    pianoRoll_.setCurrentClipContext(activeTrack, clipId);

    std::shared_ptr<const juce::AudioBuffer<float>> clipBuffer =
        processorRef_.getClipAudioBuffer(activeTrack, clipIndex);
    pianoRoll_.setAudioBuffer(clipBuffer, static_cast<int>(processorRef_.getSampleRate()));

    applyResolvedScaleForClip(activeTrack, clipIndex);
    syncPianoRollFromClipSelection(activeTrack, clipIndex);
    syncParameterPanelFromSelection();
    menuBar_.menuItemsChanged();
    refreshAfterUndoRedo();

    lastSyncedBpm_ = processorRef_.getBpm();
}

void OpenTuneAudioProcessorEditor::performUndoWithRangeTracking()
{
    CorrectedSegmentsChangeAction::resetLastAffectedRange();
    processorRef_.performUndo();
    
    int start = CorrectedSegmentsChangeAction::getLastAffectedStartFrame();
    int end = CorrectedSegmentsChangeAction::getLastAffectedEndFrame();
    
    AppLogger::log("performUndoWithRangeTracking: diffRange=[" + juce::String(start) + "," + juce::String(end) + "]");
    
    pianoRoll_.refreshAfterUndoRedoWithRange(start, end);
    arrangementView_.repaint();
    trackPanel_.repaint();
}

void OpenTuneAudioProcessorEditor::performRedoWithRangeTracking()
{
    CorrectedSegmentsChangeAction::resetLastAffectedRange();
    processorRef_.performRedo();
    
    int start = CorrectedSegmentsChangeAction::getLastAffectedStartFrame();
    int end = CorrectedSegmentsChangeAction::getLastAffectedEndFrame();
    
    AppLogger::log("performRedoWithRangeTracking: diffRange=[" + juce::String(start) + "," + juce::String(end) + "]");
    
    pianoRoll_.refreshAfterUndoRedoWithRange(start, end);
    arrangementView_.repaint();
    trackPanel_.repaint();
}

} // namespace OpenTune
