#include <QInputMethodEvent>
#include <QDynamicPropertyChangeEvent>
#include <QInputMethodQueryEvent>
#include <QInputMethod>
#include <QGuiApplication>
#include "sessioncanvas.h"
#include "canvaskey.h"
#include "backend/shortcutbinding.h"
#include <QQuickWindow>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QWheelEvent>
#include <QTouchEvent>
#include <QPointingDevice>
#include <SDL.h>
#include <algorithm>
#include <utility>

namespace {
SDL_Keycode symbol(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return SDLK_a + key - Qt::Key_A;
    if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde) return key;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return key <= Qt::Key_F12 ? SDLK_F1 + key - Qt::Key_F1 : SDLK_F13 + key - Qt::Key_F13;
    switch (key) {
    case Qt::Key_Return: return SDLK_RETURN; case Qt::Key_Enter: return SDLK_KP_ENTER;
    case Qt::Key_Tab: case Qt::Key_Backtab: return SDLK_TAB;
    case Qt::Key_Backspace: return SDLK_BACKSPACE; case Qt::Key_Delete: return SDLK_DELETE;
    case Qt::Key_Insert: return SDLK_INSERT; case Qt::Key_Escape: return SDLK_ESCAPE;
    case Qt::Key_Home: return SDLK_HOME; case Qt::Key_End: return SDLK_END;
    case Qt::Key_PageUp: return SDLK_PAGEUP; case Qt::Key_PageDown: return SDLK_PAGEDOWN;
    case Qt::Key_Left: return SDLK_LEFT; case Qt::Key_Right: return SDLK_RIGHT;
    case Qt::Key_Up: return SDLK_UP; case Qt::Key_Down: return SDLK_DOWN;
    case Qt::Key_Shift: return SDLK_LSHIFT; case Qt::Key_Alt: return SDLK_LALT;
    case Qt::Key_AltGr: return SDLK_RALT; case Qt::Key_CapsLock: return SDLK_CAPSLOCK;
#ifdef Q_OS_MACOS
    case Qt::Key_Control: return SDLK_LGUI; case Qt::Key_Meta: return SDLK_LCTRL;
#else
    case Qt::Key_Control: return SDLK_LCTRL; case Qt::Key_Meta: return SDLK_LGUI;
#endif
    default: return SDLK_UNKNOWN;
    }
}
int button(Qt::MouseButton value) {
    switch (value) {
    case Qt::LeftButton: return SDL_BUTTON_LEFT; case Qt::RightButton: return SDL_BUTTON_RIGHT;
    case Qt::MiddleButton: return SDL_BUTTON_MIDDLE; case Qt::BackButton: return SDL_BUTTON_X1;
    case Qt::ForwardButton: return SDL_BUTTON_X2; default: return 0;
    }
}
}
SessionCanvas::SessionCanvas(QQuickItem* parent) : QQuickItem(parent), m_Inhibitor(this) {
    setAcceptedMouseButtons(Qt::AllButtons); setAcceptHoverEvents(true);
    setAcceptTouchEvents(true);
    setActiveFocusOnTab(true);
    setFlag(ItemAcceptsInputMethod, m_TextInput);
    m_Pointer = createCanvasPointer([this](qreal dx, qreal dy) {
        if (m_LocalWindowDrag) return;
        m_RelativeRemainder += QPointF(dx, dy);
        const QPoint delta(int(m_RelativeRemainder.x()), int(m_RelativeRemainder.y()));
        m_RelativeRemainder -= delta;
        if (!delta.isNull()) emit input({{"type","motion"},{"x",m_Mouse.x()},{"y",m_Mouse.y()},
                                       {"dx",delta.x()},{"dy",delta.y()}});
    });
    connect(this, &SessionCanvas::relativePointerChanged, this, &SessionCanvas::refresh);
    connect(this, &SessionCanvas::pointerRegionChanged, this, &SessionCanvas::refresh);
    connect(this, &SessionCanvas::textInputChanged, this, [this] {
        setFlag(ItemAcceptsInputMethod, m_TextInput);
        QGuiApplication::inputMethod()->update(Qt::ImEnabled);
    });
    connect(this, &SessionCanvas::cursorHiddenChanged, this, &SessionCanvas::refresh);
    m_ResizeTimer.setSingleShot(true); m_ResizeTimer.setInterval(120);
    connect(&m_ResizeTimer, &QTimer::timeout, this, [this] {
        if (isVisible() && m_Presentation) m_Presentation->resized();
    });
    const auto attachWindow = [this](QQuickWindow* window) {
        if (m_RootWindow) m_RootWindow->removeEventFilter(this);
        m_RootWindow = window;
        m_LocalWindowDrag = window && window->property("deskportLocalWindowDrag").toBool();
        if (window) window->installEventFilter(this);
        if (m_Presentation) m_Presentation->detach();
        m_Inhibitor.setWindow(nullptr);
        delete m_Surface; m_Surface = nullptr;
        if (window) {
#if defined(Q_OS_LINUX) && QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
            // Stop exposing the embedded surface in the same event turn as the
            // parent hides; a timer leaves queued presentations using stale WSI.
            connect(window, &QWindow::visibleChanged, this, &SessionCanvas::refresh, Qt::UniqueConnection);
            connect(window, &QWindow::windowStateChanged, this, &SessionCanvas::refresh, Qt::UniqueConnection);
#endif
            m_Surface = new QWindow(window);
            connect(m_Surface, &QWindow::screenChanged, this, [this] {
                if (isVisible()) m_ResizeTimer.start();
            });
            m_Surface->setFlags(Qt::FramelessWindowHint);
#ifdef Q_OS_LINUX
            m_Surface->setSurfaceType(QSurface::VulkanSurface);
#endif
            m_Surface->installEventFilter(this);
            // Wayland keyboard focus belongs to the shell's root surface;
            // the video surface is a subsurface and only receives pointer focus.
            m_Inhibitor.setWindow(QGuiApplication::platformName().startsWith("wayland")
                                  ? static_cast<QWindow*>(window) : m_Surface);
            refresh();
            if (m_Presentation) m_Presentation->attach(m_Surface);
        }
    };
    connect(this, &QQuickItem::windowChanged, this, attachWindow);
    if (window()) attachWindow(window());
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        if (isVisible()) forceActiveFocus();
        refresh();
        // A resize pending before hide still needs to be applied on recall.
        // Presentation ignores a size that already matches its live decoder.
        if (isVisible() && m_Presentation) m_ResizeTimer.start();
    });
    connect(this, &SessionCanvas::captureRequestedChanged, this, &SessionCanvas::refresh);
    connect(&m_Timer, &QTimer::timeout, this, &SessionCanvas::refresh);
    m_Timer.start(16);
}
SessionCanvas::~SessionCanvas() {
    // QQuickItem's destructor emits windowChanged after our members have
    // already been destroyed. Disconnect callbacks before entering it.
    disconnect(this, nullptr, this, nullptr);
    m_Timer.stop(); m_ResizeTimer.stop();
    if (m_RootWindow) m_RootWindow->removeEventFilter(this);
    m_Pointer.reset();
    if (m_Presentation) m_Presentation->detach();
    m_Inhibitor.setEnabled(false);
    if (m_Grabbed && m_Surface) m_Surface->setKeyboardGrabEnabled(false);
    delete m_Surface; m_Surface = nullptr;
}
void SessionCanvas::setPresentation(QObject* value) {
    auto presentation = qobject_cast<SessionPresentation*>(value);
    if (presentation == m_Presentation) return;
    m_ResizeTimer.stop();
    if (m_Presentation) { disconnect(m_Presentation, nullptr, this, nullptr); m_Presentation->detach(); }
    m_Presentation = presentation;
    if (presentation) {
        connect(presentation, &SessionPresentation::readyChanged, this, &SessionCanvas::frameReadyChanged);
        connect(presentation, &SessionPresentation::activeChanged, this, &SessionCanvas::refresh);
        if (m_Surface) presentation->attach(m_Surface);
    }
    emit presentationChanged(); emit frameReadyChanged(); refresh();
}
void SessionCanvas::geometryChange(const QRectF& next, const QRectF& previous) {
    QQuickItem::geometryChange(next, previous); refresh();
}
QVariant SessionCanvas::inputMethodQuery(Qt::InputMethodQuery query) const {
    // Qt Quick queries the focused item, not the embedded presentation window.
    if (query == Qt::ImEnabled) return m_TextInput && !m_LocalWindowDrag;
    if (query == Qt::ImCursorRectangle) return QRect(0, 0, 1, 48);
    return QQuickItem::inputMethodQuery(query);
}
void SessionCanvas::inputMethodEvent(QInputMethodEvent* event) {
    if (m_LocalWindowDrag) { event->accept(); return; }
    if (m_Presentation) m_Presentation->preedit(m_TextInput ? event->preeditString().left(1024) : QString());
    if (m_TextInput && !event->commitString().isEmpty())
        emit input({{"type","text"},{"text",event->commitString().left(1024)}});
    event->accept();
}
bool SessionCanvas::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_RootWindow && event->type() == QEvent::DynamicPropertyChange &&
        static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == "deskportLocalWindowCursor") {
        refresh();
    }
    if (watched == m_RootWindow && event->type() == QEvent::DynamicPropertyChange &&
        static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == "deskportLocalWindowDrag") {
        m_LocalWindowDrag = m_RootWindow->property("deskportLocalWindowDrag").toBool();
        if (m_LocalWindowDrag) {
            m_Buttons = 0;
            m_Touches.clear(); m_PendingTouches.clear();
            m_RelativeRemainder = {};
            if (m_Pointer) m_Pointer->releaseForWindowMove();
            if (m_Presentation) m_Presentation->preedit({});
        }
        refresh();
        QGuiApplication::inputMethod()->update(Qt::ImEnabled);
    }
    if (watched != m_Surface) return QQuickItem::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::InputMethodQuery: {
        auto query = static_cast<QInputMethodQueryEvent*>(event);
        query->setValue(Qt::ImEnabled, m_TextInput && !m_LocalWindowDrag);
        query->setValue(Qt::ImCursorRectangle, QRect(0, 0, 1, 48));
        event->accept(); return true;
    }
    case QEvent::InputMethod: {
        inputMethodEvent(static_cast<QInputMethodEvent*>(event));
        return true;
    }
    case QEvent::KeyPress: keyPressEvent(static_cast<QKeyEvent*>(event)); return true;
    case QEvent::KeyRelease: keyReleaseEvent(static_cast<QKeyEvent*>(event)); return true;
    case QEvent::MouseButtonPress: mousePressEvent(static_cast<QMouseEvent*>(event)); return true;
    case QEvent::MouseButtonRelease: mouseReleaseEvent(static_cast<QMouseEvent*>(event)); return true;
    case QEvent::MouseMove: mouseMoveEvent(static_cast<QMouseEvent*>(event)); return true;
    case QEvent::Wheel: wheelEvent(static_cast<QWheelEvent*>(event)); return true;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: touchEvent(static_cast<QTouchEvent*>(event)); return event->isAccepted();
    case QEvent::Enter: m_Hovered = true; refresh(); break;
    case QEvent::Leave: m_Hovered = false; refresh(); break;
    case QEvent::FocusIn: forceActiveFocus(); refresh(); break;
    case QEvent::FocusOut: refresh(); break;
    default: break;
    }
    return QQuickItem::eventFilter(watched, event);
}
QPoint SessionCanvas::position(const QPointF& point) const {
    return QPoint(qBound(0, qRound(point.x()), qMax(0, int(width()) - 1)),
                  qBound(0, qRound(point.y()), qMax(0, int(height()) - 1)));
}
void SessionCanvas::refresh() {
    if (!window() || width() < 1 || height() < 1) return;
    const bool visible = isVisible() && window()->isVisible() && window()->windowState() != Qt::WindowMinimized;
    if (m_Surface) {
        // Devices can change the content geometry (notably macOS title bars).
        // A hidden canvas keeps its remote geometry and decoder. Apply only the
        // final visible geometry so recalling the same desktop needs no reset.
        if (visible) {
            const QSize size(qRound(width()), qRound(height()));
            const bool resized = m_Surface->size() != size;
            m_Surface->setGeometry(QRect(mapToScene(QPointF()).toPoint(), size));
            if (resized && m_Presentation) m_ResizeTimer.start();
        }
        m_Surface->setVisible(visible && m_Presentation && m_Presentation->hasFrames() &&
                              !property("transitionWaiting").toBool());
        const auto localCursor = window()->property("deskportLocalWindowCursor");
        m_Surface->setCursor(m_LocalWindowDrag && localCursor.isValid()
            ? Qt::CursorShape(localCursor.toInt())
            : m_CursorHidden && !m_LocalWindowDrag ? Qt::BlankCursor : Qt::ArrowCursor);
    }
    const bool focused = !m_LocalWindowDrag && visible && m_Presentation && m_Presentation->ready() &&
        ((window()->isActive() && hasActiveFocus()) || (m_Surface && m_Surface->isActive()));
    const bool capture = focused && m_Surface && m_Hovered && m_CaptureRequested;
    if (m_Pointer) {
        const QRect region(m_PointerRegion.value("x").toInt(), m_PointerRegion.value("y").toInt(),
                           m_PointerRegion.value("width").toInt(), m_PointerRegion.value("height").toInt());
        if (focused && m_Hovered && m_RelativePointer) m_Pointer->update(m_Surface, true);
        else if (focused && m_Hovered && region.isValid()) m_Pointer->confine(m_Surface, region, true);
        else m_Pointer->update(nullptr, false);
    }
    m_Inhibitor.setEnabled(capture);
    if (!capture && m_Grabbed) { m_Surface->setKeyboardGrabEnabled(false); m_Grabbed = false; }
#ifndef Q_OS_WIN
    // Qt's Win32 grab only redirects application events. It does not suppress
    // shell shortcuts, so only the native hook may confirm Windows capture.
    else if (capture && !QGuiApplication::platformName().startsWith("wayland") && !m_Inhibitor.isInhibited() && !m_Grabbed) m_Grabbed = m_Surface->setKeyboardGrabEnabled(true);
#endif
    const QJsonObject state {{"width",int(width())},{"height",int(height())},{"scale",window()->devicePixelRatio()},
        {"visible",visible},{"focused",focused},{"hovered",m_Hovered},{"fullscreen",window()->windowState() == Qt::WindowFullScreen},
        {"grabbed",m_Inhibitor.isInhibited() || m_Grabbed},{"x",m_Mouse.x()},{"y",m_Mouse.y()},{"buttons",m_Buttons}};
    if (state != m_LastState) { m_LastState = state; emit viewportChanged(state); }
    flushPendingTouches();
}
void SessionCanvas::key(QKeyEvent* event, bool down) {
    if (m_LocalWindowDrag) { event->accept(); return; }
    auto key = symbol(event->key());
    const auto scan = CanvasKey::physical(*event, key);
    if (!key) key = SDL_GetKeyFromScancode(scan);
    if (!key && scan == SDL_SCANCODE_UNKNOWN) { event->ignore(); return; }
    const auto mods = ShortcutBinding::eventModifiers(event->modifiers());
    int modifiers = 0;
    if (mods & Qt::ControlModifier) modifiers |= KMOD_CTRL;
    if (mods & Qt::AltModifier) modifiers |= KMOD_ALT;
    if (mods & Qt::ShiftModifier) modifiers |= KMOD_SHIFT;
    if (mods & Qt::MetaModifier) modifiers |= KMOD_GUI;
    refresh();
    emit input({{"type","key"},{"down",down},{"repeat",event->isAutoRepeat()},
                {"symbol",int(key)},{"scan",int(scan)},{"modifiers",modifiers}});
    if (down && m_TextInput && !event->text().isEmpty() &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier)) && event->text().at(0).unicode() >= 0x20)
        emit input({{"type","text"},{"text",event->text().left(1024)}});
    event->accept();
}
void SessionCanvas::keyPressEvent(QKeyEvent* event) { key(event, true); }
void SessionCanvas::keyReleaseEvent(QKeyEvent* event) { key(event, false); }
void SessionCanvas::mouse(QMouseEvent* event, bool down, bool isButton) {
    if (m_LocalWindowDrag) { event->accept(); return; }
    if (down) { forceActiveFocus(); if (m_Surface) m_Surface->requestActivate(); }
    if (isButton && button(event->button())) {
        const int mask = SDL_BUTTON(button(event->button()));
        if (down) m_Buttons |= mask; else m_Buttons &= ~mask;
    }
    const auto old = m_Mouse; m_Mouse = position(event->position()); m_Hovered = true; refresh();
    QJsonObject value {{"type",isButton ? "button" : "motion"},{"x",m_Mouse.x()},{"y",m_Mouse.y()}};
    if (isButton) { value["button"] = button(event->button()); value["down"] = down; }
    else { value["dx"] = m_Mouse.x() - old.x(); value["dy"] = m_Mouse.y() - old.y(); }
    emit input(value); event->accept();
}
void SessionCanvas::mousePressEvent(QMouseEvent* event) { mouse(event, true, true); }
void SessionCanvas::mouseReleaseEvent(QMouseEvent* event) { mouse(event, false, true); }
void SessionCanvas::mouseMoveEvent(QMouseEvent* event) { mouse(event, false, false); }
void SessionCanvas::hoverEnterEvent(QHoverEvent* event) { m_Hovered = true; m_Mouse = position(event->position()); forceActiveFocus(); refresh(); }
void SessionCanvas::hoverLeaveEvent(QHoverEvent*) { m_Hovered = false; refresh(); }
void SessionCanvas::hoverMoveEvent(QHoverEvent* event) {
    if (m_LocalWindowDrag) { event->accept(); return; }
    const auto old = m_Mouse; m_Mouse = position(event->position()); refresh();
    emit input({{"type","motion"},{"x",m_Mouse.x()},{"y",m_Mouse.y()},{"dx",m_Mouse.x()-old.x()},{"dy",m_Mouse.y()-old.y()}});
}
void SessionCanvas::wheelEvent(QWheelEvent* event) {
    if (m_LocalWindowDrag) { event->accept(); return; }
    emit input({{"type","wheel"},{"x",event->angleDelta().x()/120.0},{"y",event->angleDelta().y()/120.0}}); event->accept();
}
void SessionCanvas::touchEvent(QTouchEvent* event) {
    if (!event->pointingDevice() || event->pointingDevice()->type() != QInputDevice::DeviceType::TouchScreen) { event->ignore(); return; }
    if (m_LocalWindowDrag || width() <= 0 || height() <= 0) { m_Touches.clear(); m_PendingTouches.clear(); event->accept(); return; }
    if (event->type() == QEvent::TouchCancel) {
        if (!m_Touches.isEmpty()) emit input({{"type", "touch-cancel"}});
        m_Touches.clear(); m_PendingTouches.clear(); event->accept(); return;
    }
    if (event->type() == QEvent::TouchBegin) {
        forceActiveFocus(); if (m_Surface) m_Surface->requestActivate();
        m_Hovered = true; refresh();
    }
    for (const auto& point : event->points()) {
        const auto state = point.state();
        if (state == QEventPoint::State::Stationary) continue;
        const QPointF current(qBound(0.0, point.position().x() / width(), 1.0),
                              qBound(0.0, point.position().y() / height(), 1.0));
        QString phase;
        if (state == QEventPoint::State::Pressed) phase = "down";
        else if (state == QEventPoint::State::Updated) phase = "move";
        else if (state == QEventPoint::State::Released) phase = "up";
        else continue;
        // Platform touch IDs can be ever-increasing sequence numbers. Send a
        // small slot that stays stable for the lifetime of each contact.
        auto touch = m_Touches.find(point.id());
        if (phase == "down") {
            if (touch != m_Touches.end()) continue;
            int slot = 0;
            while (std::any_of(m_Touches.cbegin(), m_Touches.cend(), [slot](const Touch& t) { return t.slot == slot; })) slot++;
            if (slot >= MaxTouches) continue;
            touch = m_Touches.insert(point.id(), {slot, current});
        } else if (touch == m_Touches.end()) continue;
        const auto previous = touch->position;
        touchInput({{"type", "touch"}, {"phase", phase}, {"id", touch->slot},
                    {"x", current.x()}, {"y", current.y()}, {"dx", current.x() - previous.x()},
                    {"dy", current.y() - previous.y()}, {"pressure", qBound(0.0, double(point.pressure()), 1.0)}});
        if (phase == "up") m_Touches.erase(touch); else touch->position = current;
    }
    event->accept();
}
void SessionCanvas::touchInput(const QJsonObject& event) {
    // The first contact on an inactive window arrives before activation. The
    // worker ignores unfocused input, so hold the gesture briefly instead of
    // losing its down event and with it every following move and release.
    if (m_LastState.value("focused").toBool() && m_PendingTouches.isEmpty()) { emit input(event); return; }
    if (m_PendingTouches.isEmpty()) m_PendingTouchAge.start();
    if (m_PendingTouches.size() < MaxPendingTouches) m_PendingTouches.append(event);
    flushPendingTouches();
}
void SessionCanvas::flushPendingTouches() {
    if (m_PendingTouches.isEmpty()) return;
    if (m_PendingTouchAge.elapsed() > PendingTouchMs) { m_PendingTouches.clear(); return; }
    if (!m_LastState.value("focused").toBool()) return;
    const auto pending = std::exchange(m_PendingTouches, {});
    for (const auto& event : pending) emit input(event);
}
void SessionCanvas::focusOutEvent(QFocusEvent* event) { m_Touches.clear(); m_PendingTouches.clear(); QQuickItem::focusOutEvent(event); refresh(); }
