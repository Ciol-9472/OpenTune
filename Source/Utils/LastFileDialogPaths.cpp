#include "LastFileDialogPaths.h"

namespace OpenTune {

namespace {

constexpr int kKindCount = static_cast<int>(FileDialogKind::LoadProject) + 1;

juce::CriticalSection gLock;
bool gLoaded = false;
juce::String gPaths[kKindCount];

} // namespace

const char* LastFileDialogPaths::tagForKind(FileDialogKind kind)
{
    switch (kind)
    {
        case FileDialogKind::ImportAudio:       return "ImportAudio";
        case FileDialogKind::ExportAudioWav:    return "ExportAudioWav";
        case FileDialogKind::ExportStemsFolder: return "ExportStemsFolder";
        case FileDialogKind::SaveProject:       return "SaveProject";
        case FileDialogKind::LoadProject:       return "LoadProject";
    }
    return "";
}

juce::File LastFileDialogPaths::storageFile()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("OpenTune")
        .getChildFile("last_file_dialog_paths.xml");
}

void LastFileDialogPaths::ensureLoaded()
{
    const juce::ScopedLock sl(gLock);
    if (gLoaded)
        return;
    gLoaded = true;

    for (auto& p : gPaths)
        p.clear();

    const juce::File file = storageFile();
    if (!file.existsAsFile())
        return;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr || !xml->hasTagName("LastFileDialogPaths"))
        return;

    for (auto* e : xml->getChildWithTagNameIterator("Path"))
    {
        const juce::String kindStr = e->getStringAttribute("kind");
        const juce::String value = e->getStringAttribute("value");
        if (value.isEmpty())
            continue;

        for (int i = 0; i < kKindCount; ++i)
        {
            const auto k = static_cast<FileDialogKind>(i);
            if (kindStr == tagForKind(k))
            {
                gPaths[i] = value;
                break;
            }
        }
    }
}

void LastFileDialogPaths::persist()
{
    juce::XmlElement root("LastFileDialogPaths");
    const juce::ScopedLock sl(gLock);
    for (int i = 0; i < kKindCount; ++i)
    {
        if (gPaths[i].isEmpty())
            continue;
        auto* child = root.createNewChildElement("Path");
        child->setAttribute("kind", tagForKind(static_cast<FileDialogKind>(i)));
        child->setAttribute("value", gPaths[i]);
    }

    const juce::File file = storageFile();
    (void)file.getParentDirectory().createDirectory();
    (void)root.writeTo(file);
}

juce::String LastFileDialogPaths::getPathForKind(FileDialogKind kind)
{
    ensureLoaded();
    const juce::ScopedLock sl(gLock);
    return gPaths[static_cast<int>(kind)];
}

void LastFileDialogPaths::setPathForKind(FileDialogKind kind, juce::String path)
{
    {
        const juce::ScopedLock sl(gLock);
        gLoaded = true;
        gPaths[static_cast<int>(kind)] = std::move(path);
    }
    persist();
}

static juce::File resolveStoredPathToDirectory(const juce::String& stored)
{
    if (stored.isEmpty())
        return {};

    juce::File f(stored);
    if (f.isDirectory() && f.exists())
        return f;
    if (f.existsAsFile())
    {
        const juce::File parent = f.getParentDirectory();
        if (parent.exists())
            return parent;
    }
    return {};
}

juce::File LastFileDialogPaths::directoryForOpen(FileDialogKind kind, juce::File defaultDirectory)
{
    const juce::File resolved = resolveStoredPathToDirectory(getPathForKind(kind));
    if (resolved != juce::File{})
        return resolved;

    return defaultDirectory;
}

juce::File LastFileDialogPaths::startingFileForSave(FileDialogKind kind, juce::File defaultStartingFile)
{
    juce::File dir = directoryForOpen(kind, defaultStartingFile.getParentDirectory());
    return dir.getChildFile(defaultStartingFile.getFileName());
}

void LastFileDialogPaths::rememberOpenSelection(FileDialogKind kind, const juce::File& chosenFile)
{
    if (chosenFile == juce::File{})
        return;

    const juce::File dir = chosenFile.isDirectory() ? chosenFile : chosenFile.getParentDirectory();
    if (dir == juce::File{})
        return;

    setPathForKind(kind, dir.getFullPathName());
}

void LastFileDialogPaths::rememberSaveSelection(FileDialogKind kind, const juce::File& savedFile)
{
    if (savedFile == juce::File{})
        return;

    const juce::File dir = savedFile.getParentDirectory();
    if (dir == juce::File{})
        return;

    setPathForKind(kind, dir.getFullPathName());
}

void LastFileDialogPaths::rememberDirectory(FileDialogKind kind, const juce::File& directory)
{
    if (directory == juce::File{} || !directory.isDirectory())
        return;

    setPathForKind(kind, directory.getFullPathName());
}

} // namespace OpenTune
