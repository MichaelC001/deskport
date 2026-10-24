#include "shortcutrecorderinhibitor.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QTimer>

ShortcutRecorderInhibitor::ShortcutRecorderInhibitor(QObject* parent)
    : ShortcutRecorderInhibitor(createShortcutRecorderInhibitionBackend(), parent) {}

ShortcutRecorderInhibitor::ShortcutRecorderInhibitor(
    std::unique_ptr<ShortcutRecorderInhibitionBackend> backend, QObject* parent)
    : QObject(parent), m_Backend(std::move(backend)) {}

ShortcutRecorderInhibitor::~ShortcutRecorderInhibitor() {
    release();
}

void ShortcutRecorderInhibitor::setWindow(QWindow* window) {
    if (window == m_Window) return;
    release();
    if (m_Window) {
        m_Window->removeEventFilter(this);
        disconnect(m_Window, nullptr, this, nullptr);
    }
    m_Window = window;
    if (window) {
        window->installEventFilter(this);
        connect(window, &QWindow::activeChanged, this, &ShortcutRecorderInhibitor::refresh);
        connect(window, &QWindow::visibleChanged, this, &ShortcutRecorderInhibitor::refresh);
        connect(window, &QObject::destroyed, this, [this] { release(); m_Window = nullptr; });
    }
    refresh();
}

void ShortcutRecorderInhibitor::setEnabled(bool enabled) {
    m_Enabled = enabled;
    refresh();
}

void ShortcutRecorderInhibitor::release() {
    // Also stop a failed/partial request; adapters must make stop idempotent.
    if (m_Backend) m_Backend->stop();
    m_Inhibited = false;
}

void ShortcutRecorderInhibitor::refresh() {
    if (!m_Enabled || !m_Window || !m_Window->isVisible() || !m_Window->isActive() ||
        !m_Window->handle()) {
        release();
        return;
    }
    if (!m_Inhibited && m_Backend) m_Inhibited = m_Backend->start(m_Window);
}

bool ShortcutRecorderInhibitor::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_Window) return false;
    switch (event->type()) {
    case QEvent::PlatformSurface:
        if (static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            release();
            return false;
        }
        Q_FALLTHROUGH();
    case QEvent::FocusIn:
    case QEvent::WindowActivate:
    case QEvent::Expose:
        // Qt updates the window's active/surface state after event filters run.
        QTimer::singleShot(0, this, &ShortcutRecorderInhibitor::refresh);
        break;
    case QEvent::FocusOut:
    case QEvent::WindowDeactivate:
        release();
        break;
    default:
        break;
    }
    return false;
}

#ifndef Q_OS_LINUX
namespace {
class NoShortcutRecorderInhibition final : public ShortcutRecorderInhibitionBackend {
    bool start(QWindow*) override { return false; }
    void stop() override {}
};
}
std::unique_ptr<ShortcutRecorderInhibitionBackend> createShortcutRecorderInhibitionBackend() {
    return std::make_unique<NoShortcutRecorderInhibition>();
}
#endif
