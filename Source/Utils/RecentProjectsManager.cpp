#include "RecentProjectsManager.h"

namespace OpenTune {

RecentProjectsManager::RecentProjectsManager() {
    load();
}

juce::File RecentProjectsManager::getStorageFile() const {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("OpenTune")
        .getChildFile("recent_projects.xml");
}

void RecentProjectsManager::trimInPlace() {
    while (files_.size() > kMaxEntries) {
        files_.removeLast();
    }
}

void RecentProjectsManager::load() {
    files_.clear();
    const juce::File file = getStorageFile();
    if (!file.existsAsFile()) {
        return;
    }

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr || !xml->hasTagName("RecentProjects")) {
        return;
    }

    for (auto* e : xml->getChildWithTagNameIterator("File")) {
        const juce::String p = e->getStringAttribute("path");
        if (p.isNotEmpty()) {
            files_.add(juce::File(p));
        }
    }
    trimInPlace();
}

void RecentProjectsManager::save() const {
    juce::XmlElement root("RecentProjects");
    for (const auto& f : files_) {
        auto* child = root.createNewChildElement("File");
        child->setAttribute("path", f.getFullPathName());
    }

    const juce::File file = getStorageFile();
    (void)file.getParentDirectory().createDirectory();
    root.writeTo(file);
}

void RecentProjectsManager::add(const juce::File& projectFile) {
    if (projectFile.getFullPathName().isEmpty()) {
        return;
    }

    const juce::String path = projectFile.getFullPathName();
    for (int i = files_.size(); --i >= 0;) {
        if (files_.getReference(i).getFullPathName() == path) {
            files_.remove(i);
        }
    }

    files_.insert(0, projectFile);
    trimInPlace();
    save();
}

void RecentProjectsManager::clear() {
    files_.clear();
    save();
}

} // namespace OpenTune
