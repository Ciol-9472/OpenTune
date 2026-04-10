#pragma once

/**
 * 菜单栏组件
 * 
 * 实现 JUCE MenuBarModel 接口，提供应用程序菜单：
 * - File（导入、导出、工程等）
 * - Edit（撤销、重做等）
 * - View（波形显示、调式、主题等）
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemeTokens.h"
namespace OpenTune {

class OpenTuneAudioProcessor;
class RecentProjectsManager;

class MenuBarComponent : public juce::Component,
                         public juce::MenuBarModel
{
public:
    enum class ExportType
    {
        SelectedClip,
        Track,
        Bus
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void importAudioRequested() = 0;
        virtual void exportAudioRequested(ExportType exportType) = 0;
        /** 分轨导出：选择音轨与文件名前缀后导出到文件夹 */
        virtual void exportStemsRequested() = 0;
        virtual void newProjectRequested() = 0;
        virtual void saveProjectRequested() = 0;
        virtual void loadProjectRequested() = 0;
        virtual void recentProjectOpenRequested(const juce::File& file) = 0;
        virtual void preferencesRequested() = 0;
        virtual void helpRequested() = 0;
        virtual void showWaveformToggled(bool shouldShow) = 0;
        virtual void showLanesToggled(bool shouldShow) = 0;
        virtual void themeChanged(ThemeId themeId) = 0;
        virtual void undoRequested() = 0;
        virtual void redoRequested() = 0;
    };

    explicit MenuBarComponent(OpenTuneAudioProcessor& processor);
    ~MenuBarComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    void refreshLocalizedText();  // 刷新本地化文本

    void setRecentProjectsManager(RecentProjectsManager* manager);

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    OpenTuneAudioProcessor& processor_;
    juce::MenuBarComponent menuBar_;
    juce::ListenerList<Listener> listeners_;
    RecentProjectsManager* recentProjects_{nullptr};

    enum MenuItemIDs
    {
        ImportAudio = 1,
        ExportSelectedClip,
        ExportTrack,
        ExportBus,
        ExportStems,
        NewProject = 39,
        SaveProject = 40,
        LoadProject,
        RecentProjectsEmpty = 305,
        RecentProjectFirst = 310,

        EditUndo = 50,
        EditRedo,

        ShowWaveform = 100,
        ShowLanes,
        ThemeBlueBreeze,
        ThemeDarkBlueGrey,
        ThemeAurora,

        OpenPreferences = 200,
        OpenHelp
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MenuBarComponent)
};

} // namespace OpenTune
