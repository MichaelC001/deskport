#pragma once
#include <QWindow>

namespace WindowFrame {
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
