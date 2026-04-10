#pragma once

#include <juce_core/juce_core.h>

namespace OpenTune {

/**
 * 记录最近打开的 .otproject 路径（最多 kMaxEntries 条），持久化到用户文档目录。
 */
class RecentProjectsManager {
public:
    static constexpr int kMaxEntries = 10;

    RecentProjectsManager();

    /** 将工程路径移到列表首位并保存；空路径将被忽略 */
    void add(const juce::File& projectFile);

    const juce::Array<juce::File>& getRecentProjects() const { return files_; }

    void clear();

private:
    void load();
    void save() const;
    void trimInPlace();
    juce::File getStorageFile() const;

    juce::Array<juce::File> files_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecentProjectsManager)
};

} // namespace OpenTune
