#include "MenuBarComponent.h"
#include "../PluginProcessor.h"
#include "TransportBarComponent.h"
#include "TopBarComponent.h"
#include "UIColors.h"
#include "../../Utils/LocalizationManager.h"
#include "../../Utils/KeyShortcutConfig.h"
#include "../../Utils/RecentProjectsManager.h"

namespace OpenTune {

MenuBarComponent::MenuBarComponent(OpenTuneAudioProcessor& processor)
    : processor_(processor)
{
    menuBar_.setModel(this);
    addAndMakeVisible(menuBar_);
    // JUCE 8 MenuBarComponent 无独立 ColourId；外观由 LookAndFeel 绘制
}

MenuBarComponent::~MenuBarComponent()
{
    menuBar_.setModel(nullptr);
}

void MenuBarComponent::paint(juce::Graphics& g)
{
    juce::ignoreUnused(g);
}

void MenuBarComponent::resized()
{
    menuBar_.setBounds(getLocalBounds());
}

void MenuBarComponent::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void MenuBarComponent::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void MenuBarComponent::refreshLocalizedText()
{
    menuItemsChanged();
}

void MenuBarComponent::setRecentProjectsManager(RecentProjectsManager* mgr)
{
    recentProjects_ = mgr;
    menuItemsChanged();
}

juce::StringArray MenuBarComponent::getMenuBarNames()
{
    return { LOC(kMenuBarTitleFile), LOC(kMenuBarTitleEdit), LOC(kMenuBarTitleView), LOC(kMenuBarTitleHelp) };
}

juce::PopupMenu MenuBarComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    switch (topLevelMenuIndex)
    {
        case 0:
        {
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                ImportAudio, LOC(kImportAudio), KeyShortcutConfig::ShortcutId::ImportAudio));

            juce::PopupMenu exportMenu;
            exportMenu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                ExportSelectedClip, LOC(kExportSelectedClip), KeyShortcutConfig::ShortcutId::ExportSelectedClip));
            exportMenu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                ExportTrack, LOC(kExportTrack), KeyShortcutConfig::ShortcutId::ExportTrack));
            exportMenu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                ExportBus, LOC(kExportBus), KeyShortcutConfig::ShortcutId::ExportBus));
            exportMenu.addSeparator();
            exportMenu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                ExportStems, LOC(kExportStems), KeyShortcutConfig::ShortcutId::ExportStems));
            menu.addSubMenu(LOC(kExportAudio), exportMenu);

            menu.addSeparator();
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                NewProject, LOC(kNewProject), KeyShortcutConfig::ShortcutId::NewProject));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                SaveProject, LOC(kSaveProject), KeyShortcutConfig::ShortcutId::SaveProject));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                SaveProjectAs, LOC(kSaveProjectAs), KeyShortcutConfig::ShortcutId::SaveProjectAs));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                LoadProject, LOC(kLoadProject), KeyShortcutConfig::ShortcutId::LoadProject));

            juce::PopupMenu recentMenu;
            if (recentProjects_ != nullptr) {
                const auto& files = recentProjects_->getRecentProjects();
                if (files.isEmpty()) {
                    recentMenu.addItem(RecentProjectsEmpty, LOC(kRecentProjectsEmpty), false);
                } else {
                    const int n = juce::jmin(static_cast<int>(files.size()),
                                             RecentProjectsManager::kMaxEntries);
                    for (int i = 0; i < n; ++i) {
                        juce::PopupMenu::Item it;
                        it.itemID = RecentProjectFirst + i;
                        it.text = files.getReference(i).getFileName();
                        it.isEnabled = files.getReference(i).existsAsFile();
                        recentMenu.addItem(it);
                    }
                }
            } else {
                recentMenu.addItem(RecentProjectsEmpty, LOC(kRecentProjectsEmpty), false);
            }
            menu.addSubMenu(LOC(kRecentProjects), recentMenu);

            menu.addSeparator();
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                OpenPreferences, LOC(kOptions), KeyShortcutConfig::ShortcutId::OpenPreferences));
            break;
        }
        case 1:
        {
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                EditUndo, LOC(kUndo), KeyShortcutConfig::ShortcutId::Undo, true));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                EditRedo, LOC(kRedo), KeyShortcutConfig::ShortcutId::Redo, true));
            menu.addSeparator();
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                EditCut, LOC(kCut), KeyShortcutConfig::ShortcutId::Cut, true));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                EditCopy, LOC(kCopy), KeyShortcutConfig::ShortcutId::Copy, true));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                EditPaste, LOC(kPaste), KeyShortcutConfig::ShortcutId::Paste, true));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                EditSelectAll, LOC(kSelectAll), KeyShortcutConfig::ShortcutId::SelectAll, true));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                EditDelete, LOC(kDelete), KeyShortcutConfig::ShortcutId::Delete, true));
            break;
        }
        case 2:
        {
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                ShowWaveform,
                LOC(kShowWaveform),
                KeyShortcutConfig::ShortcutId::ToggleShowWaveform,
                true,
                processor_.getShowWaveform()));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                ShowLanes,
                LOC(kShowLanes),
                KeyShortcutConfig::ShortcutId::ToggleShowLanes,
                true,
                processor_.getShowLanes()));
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                ShowNoteBlockNoteNames,
                LOC(kShowNoteBlockNoteNames),
                KeyShortcutConfig::ShortcutId::ToggleNoteBlockNoteNames,
                true,
                showNoteBlockNoteNames_));

            {
                juce::PopupMenu noteNamesMenu;
                noteNamesMenu.addItem(NoteNamesAll, LOC(kShowAllNotes), true, currentNoteNameMode_ == 0);
                noteNamesMenu.addItem(NoteNamesCOnly, LOC(kShowCOnly), true, currentNoteNameMode_ == 1);
                noteNamesMenu.addItem(NoteNamesHide, LOC(kHideNoteNames), true, currentNoteNameMode_ == 2);
                menu.addSubMenu(LOC(kNoteNames), noteNamesMenu);
            }

            juce::PopupMenu themeMenu;
            themeMenu.addItem(ThemeBlueBreeze, LOC(kThemeBlueBreeze), true, Theme::getActiveTheme() == ThemeId::BlueBreeze);
            themeMenu.addItem(ThemeDarkBlueGrey, LOC(kThemeDarkBlueGrey), true, Theme::getActiveTheme() == ThemeId::DarkBlueGrey);
            themeMenu.addItem(ThemeAurora, LOC(kThemeAurora), true, Theme::getActiveTheme() == ThemeId::Aurora);
            menu.addSeparator();
            menu.addSubMenu(LOC(kTheme), themeMenu);

            {
                const auto currentTrail = MouseTrailConfig::getTheme();
                juce::PopupMenu mouseTrailMenu;
                mouseTrailMenu.addItem(MouseTrailNone, LOC(kOff), true, currentTrail == MouseTrailConfig::TrailTheme::None);
                mouseTrailMenu.addSeparator();
                mouseTrailMenu.addItem(MouseTrailClassic, LOC(kClassic), true, currentTrail == MouseTrailConfig::TrailTheme::Classic);
                mouseTrailMenu.addItem(MouseTrailNeon, LOC(kNeon), true, currentTrail == MouseTrailConfig::TrailTheme::Neon);
                mouseTrailMenu.addItem(MouseTrailFire, LOC(kFire), true, currentTrail == MouseTrailConfig::TrailTheme::Fire);
                mouseTrailMenu.addItem(MouseTrailOcean, LOC(kOcean), true, currentTrail == MouseTrailConfig::TrailTheme::Ocean);
                mouseTrailMenu.addItem(MouseTrailGalaxy, LOC(kGalaxy), true, currentTrail == MouseTrailConfig::TrailTheme::Galaxy);
                mouseTrailMenu.addItem(MouseTrailCherryBlossom, LOC(kCherryBlossom), true, currentTrail == MouseTrailConfig::TrailTheme::CherryBlossom);
                mouseTrailMenu.addItem(MouseTrailMatrix, LOC(kMatrix), true, currentTrail == MouseTrailConfig::TrailTheme::Matrix);
                menu.addSeparator();
                menu.addSubMenu(LOC(kMouseTrail), mouseTrailMenu);
            }
            break;
        }
        case 3:
        {
            menu.addItem(KeyShortcutConfig::makeMenuItemWithShortcut(
                OpenHelp, LOC(kHelp), KeyShortcutConfig::ShortcutId::OpenHelp));
            menu.addItem(OpenSourceRepository, LOC(kOpenSourceRepository));
            break;
        }
        default:
            break;
    }

    return menu;
}

void MenuBarComponent::menuItemSelected(int menuItemID, int topLevelMenuIndex)
{
    juce::ignoreUnused(topLevelMenuIndex);
    DBG("MenuBarComponent::menuItemSelected called with ID: " + juce::String(menuItemID));

    switch (menuItemID)
    {
        case ImportAudio:
            listeners_.call([](Listener& l) { l.importAudioRequested(); });
            break;

        case ExportSelectedClip:
            listeners_.call([](Listener& l) { l.exportAudioRequested(ExportType::SelectedClip); });
            break;
        case ExportTrack:
            listeners_.call([](Listener& l) { l.exportAudioRequested(ExportType::Track); });
            break;
        case ExportBus:
            listeners_.call([](Listener& l) { l.exportAudioRequested(ExportType::Bus); });
            break;
        case ExportStems:
            listeners_.call([](Listener& l) { l.exportStemsRequested(); });
            break;

        case NewProject:
            listeners_.call([](Listener& l) { l.newProjectRequested(); });
            break;
        case SaveProject:
            listeners_.call([](Listener& l) { l.saveProjectRequested(); });
            break;
        case SaveProjectAs:
            listeners_.call([](Listener& l) { l.saveProjectAsRequested(); });
            break;
        case LoadProject:
            listeners_.call([](Listener& l) { l.loadProjectRequested(); });
            break;

        case OpenPreferences:
            listeners_.call([](Listener& l) { l.preferencesRequested(); });
            break;
        case OpenHelp:
            listeners_.call([](Listener& l) { l.helpRequested(); });
            break;
        case OpenSourceRepository:
            listeners_.call([](Listener& l) { l.openSourceRepositoryRequested(); });
            break;

        case ShowWaveform:
        {
            bool newState = !processor_.getShowWaveform();
            processor_.setShowWaveform(newState);
            listeners_.call([newState](Listener& l) { l.showWaveformToggled(newState); });
            menuItemsChanged();
            break;
        }
        case ShowLanes:
        {
            bool newState = !processor_.getShowLanes();
            processor_.setShowLanes(newState);
            listeners_.call([newState](Listener& l) { l.showLanesToggled(newState); });
            menuItemsChanged();
            break;
        }
        case ShowNoteBlockNoteNames:
        {
            showNoteBlockNoteNames_ = !showNoteBlockNoteNames_;
            const bool s = showNoteBlockNoteNames_;
            listeners_.call([s](Listener& l) { l.showNoteBlockNoteNamesToggled(s); });
            menuItemsChanged();
            break;
        }
        case NoteNamesAll:
            currentNoteNameMode_ = 0;
            listeners_.call([](Listener& l) { l.noteNameModeChanged(0); });
            menuItemsChanged();
            break;
        case NoteNamesCOnly:
            currentNoteNameMode_ = 1;
            listeners_.call([](Listener& l) { l.noteNameModeChanged(1); });
            menuItemsChanged();
            break;
        case NoteNamesHide:
            currentNoteNameMode_ = 2;
            listeners_.call([](Listener& l) { l.noteNameModeChanged(2); });
            menuItemsChanged();
            break;

        case ThemeBlueBreeze:
            listeners_.call([](Listener& l) { l.themeChanged(ThemeId::BlueBreeze); });
            menuItemsChanged();
            break;
        case ThemeDarkBlueGrey:
            listeners_.call([](Listener& l) { l.themeChanged(ThemeId::DarkBlueGrey); });
            menuItemsChanged();
            break;
        case ThemeAurora:
            listeners_.call([](Listener& l) { l.themeChanged(ThemeId::Aurora); });
            menuItemsChanged();
            break;

        case MouseTrailNone:
            listeners_.call([](Listener& l) { l.mouseTrailThemeChanged(MouseTrailConfig::TrailTheme::None); });
            menuItemsChanged();
            break;
        case MouseTrailClassic:
            listeners_.call([](Listener& l) { l.mouseTrailThemeChanged(MouseTrailConfig::TrailTheme::Classic); });
            menuItemsChanged();
            break;
        case MouseTrailNeon:
            listeners_.call([](Listener& l) { l.mouseTrailThemeChanged(MouseTrailConfig::TrailTheme::Neon); });
            menuItemsChanged();
            break;
        case MouseTrailFire:
            listeners_.call([](Listener& l) { l.mouseTrailThemeChanged(MouseTrailConfig::TrailTheme::Fire); });
            menuItemsChanged();
            break;
        case MouseTrailOcean:
            listeners_.call([](Listener& l) { l.mouseTrailThemeChanged(MouseTrailConfig::TrailTheme::Ocean); });
            menuItemsChanged();
            break;
        case MouseTrailGalaxy:
            listeners_.call([](Listener& l) { l.mouseTrailThemeChanged(MouseTrailConfig::TrailTheme::Galaxy); });
            menuItemsChanged();
            break;
        case MouseTrailCherryBlossom:
            listeners_.call([](Listener& l) { l.mouseTrailThemeChanged(MouseTrailConfig::TrailTheme::CherryBlossom); });
            menuItemsChanged();
            break;
        case MouseTrailMatrix:
            listeners_.call([](Listener& l) { l.mouseTrailThemeChanged(MouseTrailConfig::TrailTheme::Matrix); });
            menuItemsChanged();
            break;

        case EditUndo:
            listeners_.call([](Listener& l) { l.undoRequested(); });
            break;
        case EditRedo:
            listeners_.call([](Listener& l) { l.redoRequested(); });
            break;
        case EditCut:
            listeners_.call([](Listener& l) { l.editCutRequested(); });
            break;
        case EditCopy:
            listeners_.call([](Listener& l) { l.editCopyRequested(); });
            break;
        case EditPaste:
            listeners_.call([](Listener& l) { l.editPasteRequested(); });
            break;
        case EditSelectAll:
            listeners_.call([](Listener& l) { l.editSelectAllRequested(); });
            break;
        case EditDelete:
            listeners_.call([](Listener& l) { l.editDeleteRequested(); });
            break;

        default:
            if (menuItemID == RecentProjectsEmpty) {
                break;
            }
            if (recentProjects_ != nullptr
                && menuItemID >= RecentProjectFirst
                && menuItemID < RecentProjectFirst + RecentProjectsManager::kMaxEntries) {
                const int idx = menuItemID - RecentProjectFirst;
                const auto& files = recentProjects_->getRecentProjects();
                if (idx >= 0 && idx < files.size()) {
                    const juce::File f = files.getReference(idx);
                    if (f.existsAsFile()) {
                        listeners_.call([&f](Listener& l) { l.recentProjectOpenRequested(f); });
                    }
                }
            }
            break;
    }
}

bool MenuBarComponent::tryHandleTopLevelMenuMnemonic(const juce::KeyPress& key,
                                                     TransportBarComponent& transport,
                                                     TopBarComponent& topBar,
                                                     juce::Component* parentForPopup)
{
#if JUCE_MAC || JUCE_WINDOWS
    juce::ignoreUnused(key, transport, topBar, parentForPopup);
    return false;
#else
    if (!key.getModifiers().isAltDown())
        return false;
    if (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown())
        return false;

    const int k = key.getKeyCode();
    int menuIndex = -1;
    if (k == 'f' || k == 'F')
        menuIndex = 0;
    else if (k == 'e' || k == 'E')
        menuIndex = 1;
    else if (k == 'v' || k == 'V')
        menuIndex = 2;
    else if (k == 'h' || k == 'H')
        menuIndex = 3;
    else
        return false;

    const auto menuNames = getMenuBarNames();
    if (menuIndex < 0 || menuIndex >= menuNames.size())
        return false;

    juce::PopupMenu menu = getMenuForIndex(menuIndex, menuNames[menuIndex]);

    juce::Component* target = nullptr;
    if (transport.isMenuButtonsVisible())
    {
        switch (menuIndex)
        {
            case 0:
                target = &transport.getFileButton();
                break;
            case 1:
                target = &transport.getEditButton();
                break;
            case 2:
                target = &transport.getViewButton();
                break;
            case 3:
                target = &topBar.getMenuBar();
                break;
            default:
                break;
        }
    }
    else
    {
        target = &topBar.getMenuBar();
    }

    if (target == nullptr)
        return false;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target).withParentComponent(parentForPopup),
                       [this, menuIndex](int result) {
                           if (result != 0)
                               menuItemSelected(result, menuIndex);
                       });
    return true;
#endif
}

} // namespace OpenTune
