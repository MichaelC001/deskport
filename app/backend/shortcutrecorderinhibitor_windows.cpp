#include "shortcutrecorderinhibitor.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <qt_windows.h>
#include <array>

namespace {
int qtKey(DWORD key, DWORD scan, HKL layout) {
    if (key >= VK_F1 && key <= VK_F24) return Qt::Key_F1 + key - VK_F1;
    switch (key) {
    case VK_LWIN: case VK_RWIN: return Qt::Key_Meta;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return Qt::Key_Shift;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return Qt::Key_Control;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return Qt::Key_Alt;
    case VK_SPACE: return Qt::Key_Space;
    case VK_TAB: return Qt::Key_Tab;
    case VK_RETURN: return (scan & 0x100) ? Qt::Key_Enter : Qt::Key_Return;
    case VK_ESCAPE: return Qt::Key_Escape;
    case VK_BACK: return Qt::Key_Backspace;
    case VK_DELETE: return Qt::Key_Delete;
    case VK_INSERT: return Qt::Key_Insert;
    case VK_HOME: return Qt::Key_Home;
    case VK_END: return Qt::Key_End;
    case VK_PRIOR: return Qt::Key_PageUp;
    case VK_NEXT: return Qt::Key_PageDown;
    case VK_LEFT: return Qt::Key_Left;
    case VK_RIGHT: return Qt::Key_Right;
    case VK_UP: return Qt::Key_Up;
    case VK_DOWN: return Qt::Key_Down;
    case VK_CAPITAL: return Qt::Key_CapsLock;
    case VK_NUMLOCK: return Qt::Key_NumLock;
    case VK_SCROLL: return Qt::Key_ScrollLock;
    case VK_PAUSE: return Qt::Key_Pause;
    case VK_SNAPSHOT: return Qt::Key_Print;
    case VK_APPS: return Qt::Key_Menu;
    default: {
        const UINT character = MapVirtualKeyExW(key, MAPVK_VK_TO_CHAR, layout) & 0xffff;
        return character ? QChar(ushort(character)).toUpper().unicode() : Qt::Key_unknown;
    }
    }
}

// QWindow::setKeyboardGrabEnabled does not intercept Windows shell shortcuts.
// Keep the native hook on the UI thread, and deliver through the same QWindow
// key adapter as ordinary canvas input (including physical scan codes).
class WindowsShortcutInhibition final : public QObject, public ShortcutRecorderInhibitionBackend {
public:
    ~WindowsShortcutInhibition() override { stop(); }

    bool start(QWindow* window) override {
        if (m_Hook) return m_Window == window;
        if (!window || !window->handle() || s_Owner) return false;
        m_Window = window;
        m_Handle = reinterpret_cast<HWND>(window->winId());
        if (!foreground()) { m_Window = nullptr; m_Handle = nullptr; return false; }
        for (unsigned i = 0; i < m_Down.size(); ++i)
            m_Down[i] = (GetAsyncKeyState(int(i)) & 0x8000) != 0;
        s_Owner = this;
        m_Hook = SetWindowsHookExW(WH_KEYBOARD_LL, hook, GetModuleHandleW(nullptr), 0);
        if (!m_Hook) { s_Owner = nullptr; m_Window = nullptr; m_Handle = nullptr; }
        return m_Hook != nullptr;
    }

    void stop() override {
        ++m_Generation; // Drop queued input from a released or destroyed surface.
        if (m_Hook) UnhookWindowsHookEx(m_Hook);
        m_Hook = nullptr;
        if (s_Owner == this) s_Owner = nullptr;
        m_Window = nullptr;
        m_Handle = nullptr;
        m_Down.fill(false);
    }

    bool isActive() const override { return m_Hook && foreground(); }

private:
    bool foreground() const {
        return m_Window && m_Handle && IsWindowVisible(m_Handle) &&
            GetForegroundWindow() == GetAncestor(m_Handle, GA_ROOT);
    }

    Qt::KeyboardModifiers modifiers() const {
        Qt::KeyboardModifiers result;
        if (m_Down[VK_LWIN] || m_Down[VK_RWIN]) result |= Qt::MetaModifier;
        if (m_Down[VK_LSHIFT] || m_Down[VK_RSHIFT]) result |= Qt::ShiftModifier;
        if (m_Down[VK_LCONTROL] || m_Down[VK_RCONTROL]) result |= Qt::ControlModifier;
        if (m_Down[VK_LMENU] || m_Down[VK_RMENU]) result |= Qt::AltModifier;
        return result;
    }

    static LRESULT CALLBACK hook(int code, WPARAM message, LPARAM data) {
        auto self = s_Owner;
        if (code != HC_ACTION || !self || !self->isActive())
            return CallNextHookEx(nullptr, code, message, data);
        const auto key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        // Unicode packets are not physical keyboard input. Leave accessibility
        // and IME packet delivery to Windows rather than inventing a scan code.
        if (key.vkCode >= 256 || key.vkCode == VK_PACKET ||
            (message != WM_KEYDOWN && message != WM_SYSKEYDOWN &&
             message != WM_KEYUP && message != WM_SYSKEYUP))
            return CallNextHookEx(nullptr, code, message, data);
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool repeat = down && self->m_Down[key.vkCode];
        // The asynchronous key state has not been updated when this hook runs;
        // suppressed keys never update it. Track both sides of each modifier.
        self->m_Down[key.vkCode] = down;
        const auto mods = self->modifiers();
        const DWORD scan = key.scanCode | ((key.flags & LLKHF_EXTENDED) ? 0x100 : 0);
        const int symbol = qtKey(key.vkCode, scan, GetKeyboardLayout(0));
        const auto generation = self->m_Generation;
        QMetaObject::invokeMethod(self, [self, generation, key, scan, symbol, mods, down, repeat] {
            if (generation != self->m_Generation || !self->isActive()) return;
            QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, symbol, mods,
                            scan, key.vkCode, 0, QString(), repeat);
            event.setTimestamp(key.time);
            QCoreApplication::sendEvent(self->m_Window, &event);
        }, Qt::QueuedConnection);
        return 1;
    }

    static WindowsShortcutInhibition* s_Owner;
    QPointer<QWindow> m_Window;
    HWND m_Handle = nullptr;
    HHOOK m_Hook = nullptr;
    std::array<bool, 256> m_Down {};
    quint64 m_Generation = 0;
};
WindowsShortcutInhibition* WindowsShortcutInhibition::s_Owner = nullptr;
}

std::unique_ptr<ShortcutRecorderInhibitionBackend> createShortcutRecorderInhibitionBackend() {
    return std::make_unique<WindowsShortcutInhibition>();
}
