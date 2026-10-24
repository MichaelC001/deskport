#include "globalshortcut.h"

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QTimer>
#include <qt_windows.h>
#include <cwchar>

namespace {
constexpr int HotKeyId = 0x4450;

QString shortcutText(const char* text) {
    return QCoreApplication::translate("GlobalShortcut", text);
}

SHORT layoutKey(const ShortcutBinding& binding, HKL layout) {
    if (binding.key >= Qt::Key_F1 && binding.key <= Qt::Key_F24)
        return SHORT(VK_F1 + binding.key - Qt::Key_F1);
    switch (binding.key) {
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Return: return VK_RETURN;
    default: return VkKeyScanExW(wchar_t(binding.character()), layout);
    }
}

class WindowsGlobalShortcut final : public GlobalShortcutBackend, private QAbstractNativeEventFilter {
public:
    explicit WindowsGlobalShortcut(QObject* parent) : GlobalShortcutBackend(parent) {
        m_ReleaseTimer.setInterval(15);
        connect(&m_ReleaseTimer, &QTimer::timeout, this, [this] { checkRelease(); });
        m_LayoutTimer.setInterval(500);
        connect(&m_LayoutTimer, &QTimer::timeout, this, [this] {
            if (!m_Pressed && activeLayout() != m_Layout) rebind();
        });
    }
    ~WindowsGlobalShortcut() override { stop(); }

    bool start() override {
        if (m_Running) return m_Registered;
        m_Running = true;
        QCoreApplication::instance()->installNativeEventFilter(this);
        const bool registered = rebind();
        // A later layout change can make the fixed symbol available. Do not
        // retry an occupied binding until that layout actually changes.
        m_LayoutTimer.start();
        return registered;
    }

    void stop() override {
        m_Running = false;
        m_ReleaseTimer.stop();
        m_LayoutTimer.stop();
        // Cancellation never produces a release edge or activates the window.
        m_Pressed = false;
        if (m_Registered) UnregisterHotKey(nullptr, HotKeyId);
        m_Registered = false;
        m_Layout = nullptr;
        if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
    }

private:
    static HKL activeLayout() {
        const HWND window = GetForegroundWindow();
        const DWORD thread = window ? GetWindowThreadProcessId(window, nullptr) : 0;
        return GetKeyboardLayout(thread);
    }

    static bool inputDesktopActive() {
        // GetAsyncKeyState returns zero on an inactive/inaccessible desktop,
        // which is not evidence of a physical release. Compare desktop names
        // within this process's window station; handles can differ for the
        // same desktop and must not be compared directly.
        const HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        if (!input) return false;
        const HDESK current = GetThreadDesktop(GetCurrentThreadId());
        wchar_t inputName[256] {}, currentName[256] {};
        DWORD required = 0;
        const bool readable = current &&
            GetUserObjectInformationW(input, UOI_NAME, inputName, sizeof(inputName), &required) &&
            GetUserObjectInformationW(current, UOI_NAME, currentName, sizeof(currentName), &required);
        CloseDesktop(input);
        return readable && std::wcscmp(inputName, currentName) == 0;
    }

    bool rebind() {
        if (!m_Running) return false;
        const HKL layout = activeLayout();
        // Resolve the unmodified symbol in this layout. Never silently add
        // modifiers that the user did not record.
        const SHORT mapping = layoutKey(binding, layout);
        if (m_Registered) UnregisterHotKey(nullptr, HotKeyId);
        m_Registered = false;
        m_Layout = layout;
        if (mapping == -1 || HIBYTE(mapping) != 0) {
            if (error) error(shortcutText("This keyboard layout cannot register the selected shortcut without extra modifiers."));
            return false;
        }
        m_Key = LOBYTE(mapping);
        m_Modifiers = 0;
        if (binding.modifiers & Qt::MetaModifier) m_Modifiers |= MOD_WIN;
        if (binding.modifiers & Qt::ControlModifier) m_Modifiers |= MOD_CONTROL;
        if (binding.modifiers & Qt::AltModifier) m_Modifiers |= MOD_ALT;
        if (binding.modifiers & Qt::ShiftModifier) m_Modifiers |= MOD_SHIFT;
        // A failed registration leaves other applications' bindings intact.
        if (!RegisterHotKey(nullptr, HotKeyId, m_Modifiers | MOD_NOREPEAT, m_Key)) {
            const DWORD code = GetLastError();
            if (error) error(shortcutText("The selected shortcut is unavailable or already in use (Windows error %1).").arg(code));
            return false;
        }
        m_Registered = true;
        if (availability) availability(true);
        return true;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override {
#else
    bool nativeEventFilter(const QByteArray&, void* message, long* result) override {
#endif
        const auto msg = static_cast<MSG*>(message);
        if (!m_Registered || msg->message != WM_HOTKEY || msg->wParam != HotKeyId) return false;
        if (result) *result = 0;
        // WM_HOTKEY contains only the physical modifier mask; MOD_NOREPEAT is
        // a registration option, not a modifier delivered in this message.
        if (HIWORD(msg->lParam) != m_Key || LOWORD(msg->lParam) != m_Modifiers) return true;
        if (!inputDesktopActive()) { cancelGesture(); return true; }
        if (!m_Pressed) {
            m_Pressed = true;
            m_ReleaseTimer.start();
            if (edge) edge(true, {});
        }
        return true;
    }

    void checkRelease() {
        if (!m_Pressed) return;
        if (!inputDesktopActive()) { cancelGesture(); return; }
        // WM_HOTKEY has no release notification. Wait for the primary key and
        // every chord modifier to be up before the shared controller recalls a
        // viewer, so the viewer cannot inherit the held Windows key.
        for (int key : {int(m_Key), VK_LWIN, VK_RWIN, VK_SHIFT, VK_CONTROL, VK_MENU}) {
            if (GetAsyncKeyState(key) & 0x8000) return;
        }
        m_ReleaseTimer.stop();
        m_Pressed = false;
        if (edge) edge(false, {});
    }

    void cancelGesture() {
        m_ReleaseTimer.stop();
        m_Pressed = false;
        // Reset the shell's press/release gate without reporting a release.
        // Keep the registered shortcut ready for the next normal desktop use.
        if (availability) availability(false);
        if (m_Running && m_Registered && availability) availability(true);
    }

    QTimer m_ReleaseTimer, m_LayoutTimer;
    HKL m_Layout = nullptr;
    UINT m_Key = 0, m_Modifiers = 0;
    bool m_Running = false, m_Registered = false, m_Pressed = false;
};
}

GlobalShortcutBackend* createGlobalShortcutBackend(QObject* parent) {
    return new WindowsGlobalShortcut(parent);
}
#endif
