#include "shortcutrecorderinhibitor.h"
#include "keyboard-shortcuts-inhibit-v1.h"

#include <QGuiApplication>
#include <QTimer>
#include <cstring>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <qpa/qplatformwindow_p.h>
#else
#include <qpa/qplatformnativeinterface.h>
#endif

namespace {
class WaylandShortcutRecorderInhibition final : public QObject,
                                               public ShortcutRecorderInhibitionBackend {
public:
    WaylandShortcutRecorderInhibition() {
        // Qt reads its own display connection. Dispatch only our private queue,
        // without reading its socket or changing Qt's input dispatch.
        connect(&m_Dispatch, &QTimer::timeout, this, [this] {
            if (m_Display && m_Queue && wl_display_dispatch_queue_pending(m_Display, m_Queue) < 0)
                stop();
        });
        m_Dispatch.setInterval(32);
    }
    ~WaylandShortcutRecorderInhibition() override { stop(); }

    bool start(QWindow* window) override {
        if (m_Registry) return true;
        if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) return false;
        wl_seat* seat = nullptr;
        wl_surface* surface = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#if QT_CONFIG(wayland)
        auto* application = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        auto* nativeWindow = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        if (application && nativeWindow) {
            m_Display = application->display();
            seat = application->lastInputSeat();
            surface = nativeWindow->surface();
        }
#endif
#else
        // Compatibility with older Qt's native resource API. Current Qt uses
        // the typed interfaces above, including the seat that last had input.
        auto* native = QGuiApplication::platformNativeInterface();
        if (native) {
            m_Display = static_cast<wl_display*>(native->nativeResourceForIntegration("display"));
            seat = static_cast<wl_seat*>(native->nativeResourceForIntegration("wl_seat"));
            surface = static_cast<wl_surface*>(native->nativeResourceForWindow("surface", window));
        }
#endif
        if (!m_Display || !seat || !surface) { stop(); return false; }
        m_Seat = seat;
        m_Surface = surface;
        m_Queue = wl_display_create_queue(m_Display);
        if (!m_Queue) { stop(); return false; }
        m_Registry = wl_display_get_registry(m_Display);
        if (!m_Registry) { stop(); return false; }
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(m_Registry), m_Queue);
        static const wl_registry_listener registryListener {global, globalRemoved};
        wl_registry_add_listener(m_Registry, &registryListener, this);
        // Discover and bind asynchronously. Never block the recorder on a
        // compositor roundtrip, and leave unsupported compositors unchanged.
        wl_display_flush(m_Display);
        m_Dispatch.start();
        return true;
    }

    bool isActive() const override { return m_Active; }

    void stop() override {
        m_Active = false;
        ++m_Generation;
        m_Dispatch.stop();
        if (m_Inhibitor) zwp_keyboard_shortcuts_inhibitor_v1_destroy(m_Inhibitor);
        if (m_Manager) zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(m_Manager);
        if (m_Registry) wl_registry_destroy(m_Registry);
        if (m_Queue) wl_event_queue_destroy(m_Queue);
        if (m_Display) wl_display_flush(m_Display);
        m_Inhibitor = nullptr;
        m_Manager = nullptr;
        m_Registry = nullptr;
        m_Queue = nullptr;
        m_Display = nullptr;
        m_Seat = nullptr;
        m_Surface = nullptr;
        m_ManagerName = 0;
    }

private:
    static void global(void* data, wl_registry* registry, uint32_t name,
                       const char* interface, uint32_t) {
        auto* self = static_cast<WaylandShortcutRecorderInhibition*>(data);
        if (!self->m_Manager && std::strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) == 0) {
            self->m_Manager = static_cast<zwp_keyboard_shortcuts_inhibit_manager_v1*>(
                wl_registry_bind(registry, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1));
            self->m_ManagerName = name;
            self->m_Inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
                self->m_Manager, self->m_Surface, self->m_Seat);
            static const zwp_keyboard_shortcuts_inhibitor_v1_listener listener {active, inactive};
            zwp_keyboard_shortcuts_inhibitor_v1_add_listener(self->m_Inhibitor, &listener, self);
            wl_display_flush(self->m_Display);
        }
    }
    static void globalRemoved(void* data, wl_registry*, uint32_t name) {
        auto* self = static_cast<WaylandShortcutRecorderInhibition*>(data);
        if (name == self->m_ManagerName) {
            // Do not destroy the event queue while dispatching its callback.
            // A window switch may start a new request before this runs.
            const auto generation = self->m_Generation;
            QTimer::singleShot(0, self, [self, generation] {
                if (generation == self->m_Generation) self->stop();
            });
        }
    }
    static void active(void* data, zwp_keyboard_shortcuts_inhibitor_v1*) {
        static_cast<WaylandShortcutRecorderInhibition*>(data)->m_Active = true;
    }
    static void inactive(void* data, zwp_keyboard_shortcuts_inhibitor_v1*) {
        static_cast<WaylandShortcutRecorderInhibition*>(data)->m_Active = false;
    }

    bool m_Active = false;
    QTimer m_Dispatch;
    wl_display* m_Display = nullptr; // Borrowed from Qt; never disconnect it.
    wl_seat* m_Seat = nullptr;
    wl_surface* m_Surface = nullptr;
    wl_event_queue* m_Queue = nullptr;
    wl_registry* m_Registry = nullptr;
    zwp_keyboard_shortcuts_inhibit_manager_v1* m_Manager = nullptr;
    zwp_keyboard_shortcuts_inhibitor_v1* m_Inhibitor = nullptr;
    uint32_t m_ManagerName = 0;
    quint64 m_Generation = 0;
};
}

std::unique_ptr<ShortcutRecorderInhibitionBackend> createShortcutRecorderInhibitionBackend() {
    return std::make_unique<WaylandShortcutRecorderInhibition>();
}
