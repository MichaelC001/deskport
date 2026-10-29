#include "canvaspointer.h"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QCursor>
#include <QPointer>
#include <QPlatformSurfaceEvent>

namespace {
class WarpedPointer final : public CanvasPointer {
public:
    using CanvasPointer::CanvasPointer;
    ~WarpedPointer() override { stop(); }
    void releaseForWindowMove() override { stop(false); }
    void update(QWindow* window, bool enabled) override {
        if (!enabled || m_Window != window || m_Confined) stop();
        if (!enabled || !window || m_Window) return;
        if (!window->setMouseGrabEnabled(true)) return;
        m_Window = window; m_ReturnPosition = QCursor::pos();
        window->installEventFilter(this);
        recenter();
    }
    void confine(QWindow* window, const QRect& region, bool enabled) override {
        if (!enabled || m_Window != window || !m_Confined) stop();
        if (!enabled || !window) return;
        if (!m_Window) {
            if (!window->setMouseGrabEnabled(true)) return;
            m_Window = window;
            m_Confined = true;
            window->installEventFilter(this);
        }
        m_Region = region.intersected(QRect(QPoint(0, 0), window->size()));
    }
private:
    void recenter() {
        m_Center = QPoint(m_Window->width()/2, m_Window->height()/2);
        QCursor::setPos(m_Window->mapToGlobal(m_Center));
    }
    void stop(bool restorePosition = true) {
        if (!m_Window) return;
        m_Window->removeEventFilter(this);
        m_Window->setMouseGrabEnabled(false);
        if (restorePosition && !m_Confined) QCursor::setPos(m_ReturnPosition);
        m_Window = nullptr;
        m_Confined = false;
    }
    bool eventFilter(QObject*, QEvent* event) override {
        if (!m_Window) return false;
        if (event->type() == QEvent::MouseMove) {
            if (m_Confined) {
                if (m_Region.isValid()) {
                    const auto pos = static_cast<QMouseEvent*>(event)->position().toPoint();
                    const QPoint bounded(qBound(m_Region.left(), pos.x(), m_Region.right()),
                                         qBound(m_Region.top(), pos.y(), m_Region.bottom()));
                    if (bounded != pos) { QCursor::setPos(m_Window->mapToGlobal(bounded)); return true; }
                }
                return false;
            }
            const auto delta = static_cast<QMouseEvent*>(event)->position() - QPointF(m_Center);
            if (!delta.isNull()) { m_Motion(delta.x(), delta.y()); recenter(); }
            return true;
        }
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
            (event->type() == QEvent::PlatformSurface && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)) stop();
        return false;
    }
    QPointer<QWindow> m_Window;
    QPoint m_Center, m_ReturnPosition;
    QRect m_Region;
    bool m_Confined = false;
};
}
std::unique_ptr<CanvasPointer> createCanvasPointer(CanvasPointer::Motion motion) {
#ifdef Q_OS_LINUX
    if (QGuiApplication::platformName().startsWith("wayland")) return createWaylandCanvasPointer(std::move(motion));
#endif
    return std::make_unique<WarpedPointer>(std::move(motion));
}
