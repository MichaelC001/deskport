#pragma once
#include <QEvent>
#include <QGuiApplication>
#include <QWindow>

namespace WindowFrame {
inline bool nativeWayland() {
#ifdef Q_OS_LINUX
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
#else
    return false;
#endif
}

// Install before the first show: a startup fullscreen/maximized window has no
// public QWindow normalGeometry accessor once its first configure arrives.
class NormalSizeTracker final : public QObject {
public:
    explicit NormalSizeTracker(QWindow* window)
        : QObject(window), m_Window(window), m_NormalSize(window->size()) {
        window->installEventFilter(this);
    }
    void rememberFrameChange() {
        if (!m_NormalSize.isEmpty()) m_RestoreSize = m_NormalSize;
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_Window &&
            (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Hide) &&
            m_Window->windowState() == Qt::WindowNoState && !m_RestoreSize.isEmpty()) {
            // The state-changed signal precedes the Wayland configure; this
            // native event follows it. Correct the client size once, without a
            // deferred callback that could override a later resize or reopen a
            // hidden window. Hide can discard the configure, so retain the same
            // normal size in the hidden window before its next show.
            const auto size = m_RestoreSize;
            m_RestoreSize = QSize();
            m_Window->resize(size);
            m_NormalSize = size;
        }
        if (watched == m_Window && event->type() == QEvent::Resize &&
            m_Window->windowState() == Qt::WindowNoState && m_RestoreSize.isEmpty())
            m_NormalSize = m_Window->size();
        return QObject::eventFilter(watched, event);
    }
private:
    QWindow* m_Window;
    QSize m_NormalSize, m_RestoreSize;
};

inline NormalSizeTracker* track(QWindow* window) {
    if (!window || !nativeWayland()) return nullptr;
    for (auto child : window->children())
        if (auto* existing = dynamic_cast<NormalSizeTracker*>(child)) return existing;
    return new NormalSizeTracker(window);
}

inline bool requiresSurfaceRefresh(QWindow* window) {
    return !window || !nativeWayland() ||
        !(window->windowStates() & (Qt::WindowFullScreen | Qt::WindowMaximized));
}

// Recreate the native surface only for an explicit decoration-setting change.
// Some Wayland compositors retain server decorations when Qt destroys its xdg
// decoration object. Hiding before changing flags avoids that stale object and
// prevents the compositor subtracting its border from the old client geometry.
// Wayland placement remains compositor-owned; setGeometry preserves the client
// size and requests the previous position where the platform permits it.
inline void setFrameless(QWindow* window, bool frameless) {
    if (!window || window->flags().testFlag(Qt::FramelessWindowHint) == frameless) return;
    const auto visibility = window->visibility();
    const auto state = window->windowState();
    const auto geometry = window->geometry();
    const bool active = window->isActive();
    if (!requiresSurfaceRefresh(window)) {
        // Recreating an expanded Wayland surface discards the compositor's
        // normal restore geometry. Keep it mapped and compensate only when
        // returning to normal for any changed decoration margins.
        if (auto* tracker = track(window)) tracker->rememberFrameChange();
        window->setFlag(Qt::FramelessWindowHint, frameless);
        return;
    }
    window->hide();
    window->setFlag(Qt::FramelessWindowHint, frameless);
    // A maximized/fullscreen geometry must not replace the window's normal
    // restore geometry. Hidden/minimized windows must remain hidden/minimized.
    if (state == Qt::WindowNoState) window->setGeometry(geometry);
    window->setWindowState(state);
    window->setVisibility(visibility);
    if (active && visibility != QWindow::Hidden && visibility != QWindow::Minimized)
        window->requestActivate();
}
}
