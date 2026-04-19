#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")

#include "Win32NativeMenuBar.h"
#include "UI/MenuBarComponent.h"

namespace OpenTune {

namespace {

/** Alt+顶层菜单助记键 → 横向索引（与 getMenuBarNames 顺序一致：File/Edit/View/Help） */
int letterToTopLevelMenuIndex(juce::juce_wchar upper)
{
    switch (upper)
    {
        case 'F': return 0;
        case 'E': return 1;
        case 'V': return 2;
        case 'H': return 3;
        default: return -1;
    }
}

/** JUCE 的 peer 有时是子 HWND，SetMenu 必须作用在顶层帧窗口上才会出现系统菜单栏。 */
HWND resolveFrameHwndForMenu(HWND peerHwnd)
{
    if (peerHwnd == nullptr)
        return nullptr;

    HWND root = GetAncestor(peerHwnd, GA_ROOT);
    if (root != nullptr)
        return root;

    HWND w = peerHwnd;
    for (int guard = 0; guard < 32; ++guard)
    {
        HWND p = GetParent(w);
        if (p == nullptr)
            return w;
        w = p;
    }
    return w;
}

LRESULT CALLBACK menuSubclassProc(HWND hwnd,
                                  UINT msg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  UINT_PTR subclassId,
                                  DWORD_PTR refData)
{
    auto* self = reinterpret_cast<Win32NativeMenuBar*>(refData);
    if (self == nullptr)
        return DefSubclassProc(hwnd, msg, wParam, lParam);

    if (msg == WM_COMMAND)
    {
        if (HIWORD(wParam) == 0)
        {
            const int cmd = static_cast<int>(LOWORD(wParam));
            if (cmd != 0)
            {
                self->onWin32MenuCommand(cmd);
                return 0;
            }
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/** "File (F)" -> "File (&F)"，供系统菜单栏 Alt+字母 与下划线助记符使用。 */
juce::String toWin32MenuBarTitleWithMnemonic(const juce::String& displayTitle)
{
    const juce::String s = displayTitle.trimEnd();
    if (s.length() < 4)
        return displayTitle;
    if (!s.endsWithChar(')'))
        return displayTitle;
    const int openIdx = s.lastIndexOfChar('(');
    if (openIdx < 0 || openIdx >= s.length() - 2)
        return displayTitle;
    if (s.length() - openIdx != 3)
        return displayTitle;
    const juce::juce_wchar letter = s[s.length() - 2];
    if (!juce::CharacterFunctions::isLetter(letter))
        return displayTitle;
    return s.substring(0, openIdx).trimEnd() + " (&" + juce::String::charToString(letter) + ")";
}

void appendPopupToHMENU(HMENU hMenu, const juce::PopupMenu& pm)
{
    juce::PopupMenu::MenuItemIterator it(pm, false);
    while (it.next())
    {
        const juce::PopupMenu::Item& item = it.getItem();

        if (item.isSeparator)
        {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            continue;
        }

        if (item.isSectionHeader)
            continue;

        juce::String label = item.text;
        if (item.shortcutKeyDescription.isNotEmpty())
            label << "\t" << item.shortcutKeyDescription;

        if (item.subMenu != nullptr)
        {
            HMENU sub = CreatePopupMenu();
            appendPopupToHMENU(sub, *item.subMenu);

            UINT flags = MF_POPUP | MF_STRING;
            if (!item.isEnabled)
                flags |= MF_GRAYED;
            if (item.isTicked)
                flags |= MF_CHECKED;

            AppendMenuW(hMenu, flags, reinterpret_cast<UINT_PTR>(sub), label.toWideCharPointer());
            continue;
        }

        UINT flags = MF_STRING;
        if (!item.isEnabled)
            flags |= MF_GRAYED;
        if (item.isTicked)
            flags |= MF_CHECKED;
        if (item.itemID <= 0)
            flags |= MF_GRAYED;

        if (item.itemID <= 0)
            continue;

        AppendMenuW(hMenu, flags, static_cast<UINT_PTR>(item.itemID), label.toWideCharPointer());
    }
}

} // namespace

void Win32NativeMenuBar::onWin32MenuCommand(int commandId)
{
    if (model_ == nullptr || commandId == 0)
        return;
    MenuBarComponent* model = model_;
    juce::MessageManager::callAsync([model, commandId]() {
        model->menuItemSelected(commandId, 0);
    });
}

Win32NativeMenuBar::Win32NativeMenuBar() = default;

Win32NativeMenuBar::~Win32NativeMenuBar()
{
    detach();
}

void Win32NativeMenuBar::attach(juce::Component& editor, MenuBarComponent& model)
{
    juce::Component* top = editor.getTopLevelComponent();
    if (top == nullptr)
        return;

    auto* peer = top->getPeer();
    if (peer == nullptr)
        return;

    void* native = peer->getNativeHandle();
    if (native == nullptr)
        return;

    HWND frame = resolveFrameHwndForMenu(static_cast<HWND>(native));
    if (frame == nullptr)
        return;

    if (model_ == &model
        && hwnd_ == reinterpret_cast<void*>(frame)
        && hMenuBar_ != nullptr
        && subclassInstalled_)
    {
        return;
    }

    detach();
    model_ = &model;
    hwnd_ = reinterpret_cast<void*>(frame);

    rebuild();

    if (hMenuBar_ != nullptr)
    {
        if (SetMenu(static_cast<HWND>(hwnd_), static_cast<HMENU>(hMenuBar_)) == FALSE)
        {
            DestroyMenu(static_cast<HMENU>(hMenuBar_));
            hMenuBar_ = nullptr;
            model_ = nullptr;
            hwnd_ = nullptr;
            return;
        }
        DrawMenuBar(static_cast<HWND>(hwnd_));
    }

    if (!subclassInstalled_)
    {
        if (SetWindowSubclass(static_cast<HWND>(hwnd_),
                              &menuSubclassProc,
                              1,
                              reinterpret_cast<DWORD_PTR>(this)) == TRUE)
        {
            subclassInstalled_ = true;
        }
    }
}

void Win32NativeMenuBar::detach()
{
    if (hwnd_ != nullptr && subclassInstalled_)
    {
        RemoveWindowSubclass(static_cast<HWND>(hwnd_), &menuSubclassProc, 1);
        subclassInstalled_ = false;
    }

    if (hwnd_ != nullptr)
    {
        SetMenu(static_cast<HWND>(hwnd_), nullptr);
        DrawMenuBar(static_cast<HWND>(hwnd_));
    }

    if (hMenuBar_ != nullptr)
    {
        DestroyMenu(static_cast<HMENU>(hMenuBar_));
        hMenuBar_ = nullptr;
    }

    hwnd_ = nullptr;
    model_ = nullptr;
}

void Win32NativeMenuBar::refresh()
{
    if (model_ == nullptr || hwnd_ == nullptr)
        return;
    rebuild();
    if (hMenuBar_ != nullptr)
    {
        if (SetMenu(static_cast<HWND>(hwnd_), static_cast<HMENU>(hMenuBar_)) != FALSE)
            DrawMenuBar(static_cast<HWND>(hwnd_));
    }
}

bool Win32NativeMenuBar::tryPostMenuMnemonicKey(juce::juce_wchar letter)
{
    if (hwnd_ == nullptr)
        return false;

    const juce::juce_wchar upper = juce::CharacterFunctions::toUpperCase(letter);
    const int menuIdx = letterToTopLevelMenuIndex(upper);
    if (menuIdx < 0)
        return false;

    HWND h = static_cast<HWND>(hwnd_);
    HMENU hBar = GetMenu(h);
    if (hBar == nullptr)
        return false;

    if (static_cast<int>(GetMenuItemCount(hBar)) <= menuIdx)
        return false;

    HMENU hPopup = GetSubMenu(hBar, menuIdx);
    if (hPopup == nullptr)
        return false;

    RECT rcItem{};
    if (!GetMenuItemRect(h, hBar, static_cast<UINT>(menuIdx), &rcItem))
        return false;

    const int x = rcItem.left;
    const int y = rcItem.bottom;

    // 在 JUCE 的 keyPressed 内 SendMessage(SC_KEYMENU) 往往无法真正弹出菜单栏下拉。
    // 使用与 HMENU 绑定的 TrackPopupMenu，并在异步回调里执行，避免嵌套消息派发问题。
    juce::MessageManager::callAsync([h, hPopup, x, y]() {
        if (!IsWindow(h))
            return;
        SetForegroundWindow(h);
        TrackPopupMenu(
            hPopup,
            TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
            x,
            y,
            0,
            h,
            nullptr);
    });
    return true;
}

void Win32NativeMenuBar::rebuild()
{
    if (model_ == nullptr)
        return;

    if (hMenuBar_ != nullptr)
    {
        DestroyMenu(static_cast<HMENU>(hMenuBar_));
        hMenuBar_ = nullptr;
    }

    HMENU bar = CreateMenu();
    if (bar == nullptr)
        return;

    const juce::StringArray names = model_->getMenuBarNames();
    for (int i = 0; i < names.size(); ++i)
    {
        HMENU pop = CreatePopupMenu();
        if (pop == nullptr)
            continue;

        const juce::PopupMenu sub = model_->getMenuForIndex(i, names[i]);
        appendPopupToHMENU(pop, sub);

        AppendMenuW(bar,
                    MF_POPUP | MF_STRING,
                    reinterpret_cast<UINT_PTR>(pop),
                    toWin32MenuBarTitleWithMnemonic(names[i]).toWideCharPointer());
    }

    hMenuBar_ = bar;
}

} // namespace OpenTune
