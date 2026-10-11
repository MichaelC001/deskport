#include "windowactivation.h"
#include <QMutex>
#if defined(HAS_WAYLAND)
#include <SDL_syswm.h>
#include <wayland-client.h>
#include <cstring>
#include "xdg-activation-v1.h"
#endif

namespace {
QMutex s_Lock;
QByteArray s_Pending;
}

void WindowActivation::setPending(const QByteArray& token) {
    QMutexLocker lock(&s_Lock);
    s_Pending = token;
}

bool WindowActivation::activate(SDL_Window* window) {
    QByteArray token;
    {
        QMutexLocker lock(&s_Lock);
        token.swap(s_Pending);
    }
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
