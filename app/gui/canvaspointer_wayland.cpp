#include "canvaspointer.h"
#include "backend/relative-pointer-v1.h"
#include "backend/pointer-constraints-v1.h"
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QPlatformSurfaceEvent>
#include <cstring>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <qpa/qplatformwindow_p.h>
#else
#include <qpa/qplatformnativeinterface.h>
#endif

namespace {
class WaylandPointer final : public CanvasPointer {
public:
    explicit WaylandPointer(Motion motion) : CanvasPointer(std::move(motion)) {
        m_Dispatch.setInterval(8);
        connect(&m_Dispatch, &QTimer::timeout, this, [this] {
            if (m_Display && wl_display_dispatch_queue_pending(m_Display, m_Queue) < 0) stop();
        });
    }
    ~WaylandPointer() override { stop(); }
    void update(QWindow* window, bool enabled) override {
        if (!enabled || m_Window != window) stop();
        if (!enabled || !window || !window->handle() || m_Window) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#if QT_CONFIG(wayland)
        auto* app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        auto* native = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        if (app && native) { m_Display = app->display(); m_Pointer = app->pointer(); m_Surface = native->surface(); }
#endif
#else
        auto* native = QGuiApplication::platformNativeInterface();
        if (native) {
            m_Display = static_cast<wl_display*>(native->nativeResourceForIntegration("display"));
            m_Pointer = static_cast<wl_pointer*>(native->nativeResourceForIntegration("pointer"));
            m_Surface = static_cast<wl_surface*>(native->nativeResourceForWindow("surface", window));
        }
#endif
        if (!m_Display || !m_Pointer || !m_Surface) { stop(); return; }
        m_Queue = wl_display_create_queue(m_Display);
        if (!m_Queue) { stop(); return; }
        m_Registry = wl_display_get_registry(m_Display);
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(m_Registry), m_Queue);
        static const wl_registry_listener listener {global, removed};
        wl_registry_add_listener(m_Registry, &listener, this);
        m_Window = window; window->installEventFilter(this);
        m_Dispatch.start(); wl_display_flush(m_Display);
    }
private:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
            (event->type() == QEvent::PlatformSurface && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)) stop();
        // Absolute Qt motions are suppressed only after the compositor locks.
        return m_Locked && event->type() == QEvent::MouseMove;
    }
    void stop() {
        ++m_Generation; m_Dispatch.stop();
        if (m_Window) m_Window->removeEventFilter(this);
        if (m_Lock) zwp_locked_pointer_v1_destroy(m_Lock);
        if (m_Relative) zwp_relative_pointer_v1_destroy(m_Relative);
        if (m_Constraints) zwp_pointer_constraints_v1_destroy(m_Constraints);
        if (m_Manager) zwp_relative_pointer_manager_v1_destroy(m_Manager);
        if (m_Registry) wl_registry_destroy(m_Registry);
        if (m_Queue) wl_event_queue_destroy(m_Queue);
        if (m_Display) wl_display_flush(m_Display);
        m_Window = nullptr; m_Display = nullptr; m_Pointer = nullptr; m_Surface = nullptr;
        m_Lock = nullptr; m_Relative = nullptr; m_Constraints = nullptr; m_Manager = nullptr;
        m_Registry = nullptr; m_Queue = nullptr; m_Locked = false;
        m_ManagerName = m_ConstraintsName = 0;
    }
    void request() {
        if (!m_Manager || !m_Constraints || m_Lock) return;
        m_Relative = zwp_relative_pointer_manager_v1_get_relative_pointer(m_Manager, m_Pointer);
        static const zwp_relative_pointer_v1_listener motionListener {motion};
        zwp_relative_pointer_v1_add_listener(m_Relative, &motionListener, this);
        m_Lock = zwp_pointer_constraints_v1_lock_pointer(m_Constraints, m_Surface, m_Pointer, nullptr,
                                                       ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        static const zwp_locked_pointer_v1_listener lockListener {locked, unlocked};
        zwp_locked_pointer_v1_add_listener(m_Lock, &lockListener, this);
        wl_display_flush(m_Display);
    }
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
        auto* self = static_cast<WaylandPointer*>(data);
        if (!self->m_Manager && !std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name)) {
            self->m_Manager = static_cast<zwp_relative_pointer_manager_v1*>(wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1));
            self->m_ManagerName = name;
        } else if (!self->m_Constraints && !std::strcmp(interface, zwp_pointer_constraints_v1_interface.name)) {
            self->m_Constraints = static_cast<zwp_pointer_constraints_v1*>(wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1));
            self->m_ConstraintsName = name;
        }
        self->request();
    }
    static void removed(void* data, wl_registry*, uint32_t name) {
        auto* self = static_cast<WaylandPointer*>(data);
        if (name != self->m_ManagerName && name != self->m_ConstraintsName) return;
        const auto generation = self->m_Generation;
        QTimer::singleShot(0, self, [self, generation] { if (generation == self->m_Generation) self->stop(); });
    }
    static void locked(void* data, zwp_locked_pointer_v1*) { static_cast<WaylandPointer*>(data)->m_Locked = true; }
    static void unlocked(void* data, zwp_locked_pointer_v1*) { static_cast<WaylandPointer*>(data)->m_Locked = false; }
    static void motion(void* data, zwp_relative_pointer_v1*, uint32_t, uint32_t, wl_fixed_t, wl_fixed_t, wl_fixed_t dx, wl_fixed_t dy) {
        auto* self = static_cast<WaylandPointer*>(data);
        if (self->m_Locked) self->m_Motion(wl_fixed_to_double(dx), wl_fixed_to_double(dy));
    }
    QPointer<QWindow> m_Window;
    QTimer m_Dispatch;
    wl_display* m_Display = nullptr;
    wl_pointer* m_Pointer = nullptr; // Qt owns display, pointer and surface.
    wl_surface* m_Surface = nullptr;
    wl_event_queue* m_Queue = nullptr;
    wl_registry* m_Registry = nullptr;
    zwp_relative_pointer_manager_v1* m_Manager = nullptr;
    zwp_pointer_constraints_v1* m_Constraints = nullptr;
    zwp_relative_pointer_v1* m_Relative = nullptr;
    zwp_locked_pointer_v1* m_Lock = nullptr;
    bool m_Locked = false;
    uint32_t m_ManagerName = 0, m_ConstraintsName = 0;
    quint64 m_Generation = 0;
};
}
std::unique_ptr<CanvasPointer> createWaylandCanvasPointer(CanvasPointer::Motion motion) {
    return std::make_unique<WaylandPointer>(std::move(motion));
}
