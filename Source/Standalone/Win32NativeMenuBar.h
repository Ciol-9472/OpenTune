#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace OpenTune {

class MenuBarComponent;

#if JUCE_WINDOWS
/** Win32 HMENU 系统菜单栏（非 Qt），与 MenuBarComponent 共用命令 ID。 */
class Win32NativeMenuBar
{
public:
    Win32NativeMenuBar();
    ~Win32NativeMenuBar();

    void attach(juce::Component& editor, MenuBarComponent& model);
    void detach();
    void refresh();

    /** WM_COMMAND 子类化回调（内部使用） */
    void onWin32MenuCommand(int commandId);

private:
    void rebuild();

    MenuBarComponent* model_ = nullptr;
    void* hwnd_ = nullptr;
    void* hMenuBar_ = nullptr;
    bool subclassInstalled_ = false;
};
#else
class Win32NativeMenuBar
{
public:
    void attach(juce::Component&, MenuBarComponent&) {}
    void detach() {}
    void refresh() {}
    void onWin32MenuCommand(int) {}
};
#endif

} // namespace OpenTune
