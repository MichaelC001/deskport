#include "windowactivation.h"
#include <QMutex>
#include <QGuiApplication>
#include <QWindow>
#include <SDL.h>
#if defined(HAS_WAYLAND)
#include <SDL_syswm.h>
#include <wayland-client.h>
#include <cstring>
#include "xdg-activation-v1.h"
#endif
#ifdef HAS_X11
#include <X11/Xlib.h>
#endif

namespace {
QMutex s_Lock;
QByteArray s_Pending;
bool s_Shortcut = false;
}

void WindowActivation::setPending(const QByteArray& token, bool shortcut) {
    QMutexLocker lock(&s_Lock);
    s_Pending = token;
    s_Shortcut = shortcut;
}

static bool activate(SDL_Window* window, const QByteArray& token) {
    if (token.isEmpty() || !window) return false;
#if defined(HAS_WAYLAND)
    SDL_SysWMinfo info {}; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WAYLAND) return false;
    wl_display* display = info.info.wl.display;
    xdg_activation_v1* activation = nullptr;
    static const wl_registry_listener listener {
        [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
            if (std::strcmp(interface, xdg_activation_v1_interface.name) == 0)
                *static_cast<xdg_activation_v1**>(data) = static_cast<xdg_activation_v1*>(
                    wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1));
        },
        [](void*, wl_registry*, uint32_t) {}
    };
    auto registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, &activation);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    if (!activation) return false;
    xdg_activation_v1_activate(activation, token.constData(), info.info.wl.surface);
    xdg_activation_v1_destroy(activation);
    wl_display_flush(display);
    return true;
#else
    return false;
#endif
}

void WindowActivation::recall(SDL_Window* window, bool userRequested) {
    QByteArray token;
    bool shortcut;
    {
        QMutexLocker lock(&s_Lock);
        token.swap(s_Pending);
        shortcut = s_Shortcut;
        s_Shortcut = false;
    }
    if (!window) return;
    const auto flags = SDL_GetWindowFlags(window);
#if defined(HAS_WAYLAND)
    // A portal gesture without a token cannot activate an existing background
    // surface with Qt/SDL's old input serial. Remap only for this explicit
    // recall; automatic presentation must never borrow the shortcut's intent.
    if (userRequested && shortcut && token.isEmpty() &&
        !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_INPUT_FOCUS))) {
        SDL_SysWMinfo info {}; SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WAYLAND)
            SDL_HideWindow(window);
    }
#else
    Q_UNUSED(shortcut);
    Q_UNUSED(userRequested);
#endif
    if (flags & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(window);
    SDL_ShowWindow(window);
    if (!activate(window, token)) SDL_RaiseWindow(window);
}

void WindowActivation::prepareShortcutRecall(QWindow* window, const QByteArray& token) {
#ifdef Q_OS_LINUX
    if (window && token.isEmpty() && QGuiApplication::platformName().startsWith("wayland") &&
        window->isVisible() && !window->isActive() && window->windowState() != Qt::WindowMinimized)
        window->hide();
#else
    Q_UNUSED(window);
    Q_UNUSED(token);
#endif
}

#ifdef HAS_X11
bool WindowActivation::shortcutWindowIsActive(QWindow* window) {
    if (!window) return false;
    if (QGuiApplication::platformName() != "xcb") return window->isActive();
    auto* display = XOpenDisplay(nullptr);
    if (!display) return window->isActive();
    Window focus = None;
    int revert = RevertToNone;
    const bool queried = XGetInputFocus(display, &focus, &revert) != 0;
    const bool active = queried ? focus == static_cast<Window>(window->winId()) : window->isActive();
    XCloseDisplay(display);
    return active;
}
#endif
