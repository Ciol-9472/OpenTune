#pragma once

#include <juce_core/juce_core.h>

namespace OpenTune {

/** 记住各文件对话框上次使用的目录（持久化到用户文档下 OpenTune）。 */
enum class FileDialogKind : int
{
    ImportAudio = 0,
    ExportAudioWav,
    ExportStemsFolder,
    SaveProject,
    LoadProject,
};

class LastFileDialogPaths
{
public:
    /** 打开文件 / 选文件夹：若曾记住的路径仍存在则使用该目录，否则用 defaultDirectory。 */
    static juce::File directoryForOpen(FileDialogKind kind, juce::File defaultDirectory);

    /** 保存文件：目录为上述规则，文件名为 defaultStartingFile 的文件名。 */
    static juce::File startingFileForSave(FileDialogKind kind, juce::File defaultStartingFile);

    static void rememberOpenSelection(FileDialogKind kind, const juce::File& chosenFile);
    static void rememberSaveSelection(FileDialogKind kind, const juce::File& savedFile);
    static void rememberDirectory(FileDialogKind kind, const juce::File& directory);

private:
    static const char* tagForKind(FileDialogKind kind);
    static juce::File storageFile();
    static void ensureLoaded();
    static void setPathForKind(FileDialogKind kind, juce::String path);
    static juce::String getPathForKind(FileDialogKind kind);
    static void persist();
};

} // namespace OpenTune
