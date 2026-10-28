#include "mactitlebar.h"

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QCursor>
#include <QTimer>
#import <AppKit/AppKit.h>

MacTitleBar::MacTitleBar(QObject* parent) : QObject(parent) {}

MacTitleBar::~MacTitleBar()
{
    setDragInputSuspended(false);
    if (m_Monitor) {
        [NSEvent removeMonitor:(id)m_Monitor];
        [(id)m_Monitor release];
    }
    if (m_FullscreenObservers) {
        NSArray* observers = (NSArray*)m_FullscreenObservers;
        for (id observer in observers) [NSNotificationCenter.defaultCenter removeObserver:observer];
        [observers release];
    }
}

bool MacTitleBar::isDragArea(const QPointF& point) const
{
    // Walk down to the deepest item under the point; a control on the way
    // (button, click area or text field) keeps the press. Plain text drags.
    QQuickItem* item = m_Window->contentItem();
    bool onBar = false;
    while (item) {
        if (item->objectName() == QLatin1String("topBar")) onBar = true;
        else if (onBar && (item->inherits("QQuickAbstractButton") || item->inherits("QQuickMouseArea") ||
                           item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))) return false;
        const QPointF local = item->mapFromScene(point);
        QQuickItem* child = item->childAt(local.x(), local.y());
        if (!child) break;
        item = child;
    }
    return onBar;
}

bool MacTitleBar::isCanvasArea(const QPointF& point) const
{
    QQuickItem* item = m_Window ? m_Window->contentItem() : nullptr;
    while (item) {
        if (item->inherits("SessionCanvas")) return true;
        const auto local = item->mapFromScene(point);
        item = item->childAt(local.x(), local.y());
    }
    return false;
}

void MacTitleBar::setDragInputSuspended(bool suspended)
{
    m_DragInputSuspended = suspended;
    // SessionCanvas observes this synchronously: release remote held keys and
    // pointer capture before AppKit starts a local move or resize gesture.
    if (m_Window) m_Window->setProperty("deskportLocalWindowDrag", suspended);
    if (!suspended && m_Window) {
        m_Window->setProperty("deskportLocalWindowCursor", QVariant());
        m_Window->unsetCursor();
    }
}

void MacTitleBar::cancelWindowGesture(bool drainMouse)
{
    m_LocalGestureActive = false;
    m_ResizingWindow = false;
    if (!drainMouse || !m_ConsumeDragTail) {
        m_ConsumeDragTail = false;
        setDragInputSuspended(false);
    }
}

void MacTitleBar::attach(QQuickWindow* window)
{
    m_Window = window;
    NSWindow* native = window ? reinterpret_cast<NSView*>(window->winId()).window : nil;
    if (!native) return;
    m_NativeResizable = native.styleMask & NSWindowStyleMaskResizable;
    m_NativeMovable = native.movable;
    native.titleVisibility = NSWindowTitleHidden;
    // An empty unified toolbar makes the title bar 52 points tall and centres the
    // window buttons in it; it contributes no items of its own.
    native.toolbar = [[[NSToolbar alloc] initWithIdentifier:@"DeskPortTopBar"] autorelease];
    native.toolbarStyle = NSWindowToolbarStyleUnified;
    // Tell the top bar where the window buttons end once AppKit has laid them
    // out; their size differs between macOS versions.
    QPointer<QQuickWindow> guarded(window);
    dispatch_async(dispatch_get_main_queue(), ^{
        NSButton* zoom = [native standardWindowButton:NSWindowZoomButton];
        if (guarded && zoom) guarded->setProperty("windowButtonsEnd", NSMaxX([zoom convertRect:zoom.bounds toView:nil]));
    });
    // Keep the green button's zoom action without disabling full screen for
    // the entire window. The retained stream now uses this same native root.
    native.collectionBehavior = (native.collectionBehavior & ~NSWindowCollectionBehaviorFullScreenNone)
        | NSWindowCollectionBehaviorFullScreenPrimary;
    NSButton* zoom = [native standardWindowButton:NSWindowZoomButton];
    zoom.target = native;
    zoom.action = @selector(performZoom:);
    NSMutableArray* observers = [NSMutableArray array];
    [observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:NSWindowWillStartLiveResizeNotification
        object:native queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            m_ResizingWindow = true;
            setDragInputSuspended(true);
        }]];
    [observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidEndLiveResizeNotification
        object:native queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            m_ResizingWindow = false;
            if (!m_ConsumeDragTail) setDragInputSuspended(false);
        }]];
    for (NSString* name in @[NSWindowWillEnterFullScreenNotification, NSWindowWillExitFullScreenNotification,
                              NSWindowDidEnterFullScreenNotification, NSWindowDidExitFullScreenNotification]) {
        const bool starting = [name isEqualToString:NSWindowWillEnterFullScreenNotification] ||
                              [name isEqualToString:NSWindowWillExitFullScreenNotification];
        const bool entering = [name isEqualToString:NSWindowWillEnterFullScreenNotification];
        const bool exited = [name isEqualToString:NSWindowDidExitFullScreenNotification];
        [observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:name object:native
            queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
                if (starting) ++m_FullscreenGeneration;
                if (entering && m_CanvasVisible && m_StreamTitleBarHidden && !m_FullscreenRestoreFrame.isValid()) {
                    // A rapid re-entry can precede the previous exit's queued
                    // correction. Keep its original frame until it is applied.
                    const NSRect frame = native.frame;
                    m_FullscreenRestoreFrame = QRectF(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
                }
                cancelWindowGesture(true);
                m_FullscreenTransitioning = starting;
                updateChrome();
                // Qt also updates the native style during these notifications.
                // Reapply the final policy after its fullscreen handler returns.
                if (!starting) {
                    const auto gestureGeneration = m_GestureGeneration;
                    const auto fullscreenGeneration = m_FullscreenGeneration;
                    QTimer::singleShot(0, this, [this, exited, gestureGeneration, fullscreenGeneration] {
                        // An old completion must not consume the restore frame
                        // captured by a newer fullscreen transition.
                        if (fullscreenGeneration != m_FullscreenGeneration) return;
                        updateChrome();
                        if (!exited) return;
                        const auto saved = m_FullscreenRestoreFrame;
                        m_FullscreenRestoreFrame = {};
                        NSWindow* restored = m_Window ? reinterpret_cast<NSView*>(m_Window->winId()).window : nil;
                        // Qt's restored content size can lose the hidden title
                        // bar's height. Restore the saved outer frame once,
                        // after Qt's final style write, never after a new gesture
                        // or a hide/mode change that invalidated this snapshot.
                        if (restored && restored.visible && saved.isValid() &&
                            gestureGeneration == m_GestureGeneration && m_CanvasVisible && m_StreamTitleBarHidden &&
                            !m_FullscreenTransitioning && !isNativeFullscreen())
                            [restored setFrame:NSMakeRect(saved.x(), saved.y(), saved.width(), saved.height()) display:YES];
                    });
                }
            }]];
    }
    [observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidResignKeyNotification
        object:native queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            cancelWindowGesture(true);
        }]];
    [observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidBecomeKeyNotification
        object:native queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            // A release can go to another application while we are inactive.
            // This is recovery at activation, never a substitute for tracking
            // the queued down/drag/up events of an active gesture.
            if (!m_LocalGestureActive && !(NSEvent.pressedMouseButtons & 1)) {
                m_ConsumeDragTail = false;
                if (!(NSEvent.modifierFlags & (NSEventModifierFlagControl | NSEventModifierFlagCommand)))
                    setDragInputSuspended(false);
            }
        }]];
    m_FullscreenObservers = [observers retain];
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) { cancelWindowGesture(true); m_FullscreenRestoreFrame = {}; }
        updateChrome();
    });
    updateChrome();
    // The drag must start from the original AppKit event, before Qt delivers it.
    const auto dragModifiers = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    const auto shortcutModifiers = dragModifiers | NSEventModifierFlagOption | NSEventModifierFlagShift;
    const auto mask = NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp |
                      NSEventMaskFlagsChanged | NSEventMaskMouseMoved | NSEventMaskKeyDown | NSEventMaskKeyUp;
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent*(NSEvent* event) {
        if (!m_Window || (event.window != native &&
            !(!event.window && native.keyWindow && event.type == NSEventTypeFlagsChanged))) return event;
        const bool bare = m_CanvasVisible && m_StreamTitleBarHidden &&
            !m_FullscreenTransitioning && !(native.styleMask & NSWindowStyleMaskFullScreen);
        const bool interactive = bare && native.visible && native.keyWindow;
        const bool chord = (event.modifierFlags & shortcutModifiers) == dragModifiers;
        const bool modifiersReleased = !(event.modifierFlags & dragModifiers);
        const bool wasSuspended = m_DragInputSuspended;
        if (event.type == NSEventTypeLeftMouseDown ||
            (event.type == NSEventTypeMouseMoved && !m_LocalGestureActive)) {
            // A fresh press cannot inherit a missing release from an old drag.
            m_LocalGestureActive = false;
            m_ConsumeDragTail = false;
        }
        if (event.type == NSEventTypeLeftMouseDown) {
            ++m_GestureGeneration;
            if (bare) m_FullscreenRestoreFrame = {};
        }
        const NSPoint location = event.locationInWindow;
        const NSRect bounds = [native convertRectFromScreen:native.frame];
        constexpr CGFloat resizeRim = 8.0;
        enum { Left = 1, Right = 2, Bottom = 4, Top = 8 };
        const auto edgesAt = [&](NSPoint point) {
            int edges = 0;
            if (!NSPointInRect(point, NSInsetRect(bounds, -resizeRim, -resizeRim))) return edges;
            if (point.x <= NSMinX(bounds) + resizeRim) edges |= Left;
            else if (point.x >= NSMaxX(bounds) - resizeRim) edges |= Right;
            if (point.y <= NSMinY(bounds) + resizeRim) edges |= Bottom;
            else if (point.y >= NSMaxY(bounds) - resizeRim) edges |= Top;
            return edges;
        };
        const auto cursorForEdges = [](int edges) {
            if (edges == (Left | Top) || edges == (Right | Bottom)) return Qt::SizeFDiagCursor;
            if (edges == (Right | Top) || edges == (Left | Bottom)) return Qt::SizeBDiagCursor;
            if (edges & (Left | Right)) return Qt::SizeHorCursor;
            if (edges & (Top | Bottom)) return Qt::SizeVerCursor;
            return Qt::SizeAllCursor;
        };
        if (interactive && chord) {
            setDragInputSuspended(true);
            const NSPoint cursorPoint = event.type == NSEventTypeFlagsChanged ||
                event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp
                ? [native convertPointFromScreen:NSEvent.mouseLocation] : location;
            const auto cursor = cursorForEdges(m_LocalGestureActive ? m_GestureEdges : edgesAt(cursorPoint));
            m_Window->setProperty("deskportLocalWindowCursor", int(cursor));
            m_Window->setCursor(QCursor(cursor));
        }
        // Losing either key stops geometry changes. Drain this mouse gesture
        // locally even if both modifiers are released before the button is up.
        if (m_LocalGestureActive && (!interactive || !chord)) m_LocalGestureActive = false;
        if (m_DragInputSuspended && !chord) {
            m_Window->setProperty("deskportLocalWindowCursor", int(Qt::ArrowCursor));
            m_Window->unsetCursor();
        }
        if (m_ConsumeDragTail && (event.type == NSEventTypeLeftMouseDragged || event.type == NSEventTypeLeftMouseUp)) {
            if (m_LocalGestureActive && event.type == NSEventTypeLeftMouseDragged) {
                const NSPoint screen = [native convertPointToScreen:location];
                const CGFloat dx = screen.x - m_GestureOrigin.x();
                const CGFloat dy = screen.y - m_GestureOrigin.y();
                NSRect frame = NSMakeRect(m_GestureFrame.x(), m_GestureFrame.y(),
                                          m_GestureFrame.width(), m_GestureFrame.height());
                if (!m_GestureEdges) {
                    frame.origin.x += dx; frame.origin.y += dy;
                } else {
                    const NSSize minimum = native.minSize, maximum = native.maxSize;
                    const CGFloat maxWidth = qMax(qMax(CGFloat(1), minimum.width), maximum.width);
                    const CGFloat maxHeight = qMax(qMax(CGFloat(1), minimum.height), maximum.height);
                    if (m_GestureEdges & (Left | Right)) {
                        const CGFloat width = qBound(qMax(CGFloat(1), minimum.width),
                            frame.size.width + ((m_GestureEdges & Left) ? -dx : dx), maxWidth);
                        if (m_GestureEdges & Left) frame.origin.x = NSMaxX(frame) - width;
                        frame.size.width = width;
                    }
                    if (m_GestureEdges & (Bottom | Top)) {
                        const CGFloat height = qBound(qMax(CGFloat(1), minimum.height),
                            frame.size.height + ((m_GestureEdges & Bottom) ? -dy : dy), maxHeight);
                        if (m_GestureEdges & Bottom) frame.origin.y = NSMaxY(frame) - height;
                        frame.size.height = height;
                    }
                }
                // AppKit points preserve Retina and multi-display coordinates;
                // resize from the initial frame to keep the opposite edge fixed.
                [native setFrame:frame display:YES];
            }
            if (event.type == NSEventTypeLeftMouseUp) {
                m_LocalGestureActive = false;
                m_ConsumeDragTail = false;
                if (modifiersReleased) setDragInputSuspended(false);
            }
            return nil;
        }
        if (m_DragInputSuspended && !m_LocalGestureActive && !m_ConsumeDragTail &&
            !m_ResizingWindow && modifiersReleased) setDragInputSuspended(false);
        // The last reserved modifier release belongs to the local gesture too.
        if (event.type == NSEventTypeFlagsChanged && (wasSuspended || m_DragInputSuspended)) return nil;
        if (event.type != NSEventTypeLeftMouseDown) return event;
        if (m_FullscreenTransitioning || native.styleMask & NSWindowStyleMaskFullScreen) return event;
        if (bare) {
            if (!chord) return event;
            // Cocoa accepts first clicks in inactive Qt windows. The reserved
            // chord must never become a remote click while activating ours.
            setDragInputSuspended(true);
            m_ConsumeDragTail = true;
            if (!native.visible) return nil;
            if (!native.keyWindow) [native makeKeyAndOrderFront:nil];
            if (!native.keyWindow) return nil;
            const QPointF point(location.x, native.contentView.frame.size.height - location.y);
            const int edges = m_NativeResizable ? edgesAt(location) : 0;
            if (!edges && !isCanvasArea(point)) return nil;
            const NSRect frame = native.frame;
            const NSPoint screen = [native convertPointToScreen:location];
            m_GestureEdges = edges;
            m_GestureFrame = QRectF(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
            m_GestureOrigin = QPointF(screen.x, screen.y);
            m_LocalGestureActive = true;
            m_ConsumeDragTail = true;
            setDragInputSuspended(true);
            const auto cursor = cursorForEdges(edges);
            m_Window->setProperty("deskportLocalWindowCursor", int(cursor));
            m_Window->setCursor(QCursor(cursor));
            return nil;
        }
        // The Devices bar keeps native resizing and normal title-bar movement.
        if ((native.styleMask & NSWindowStyleMaskResizable) &&
            !NSPointInRect(location, NSInsetRect(bounds, resizeRim, resizeRim))) return event;
        for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
            NSButton* button = [native standardWindowButton:kind];
            if (button && !button.hidden && NSPointInRect(location, [button convertRect:button.bounds toView:nil])) return event;
        }
        const QPointF point(location.x, native.contentView.frame.size.height - location.y);
        if (!isDragArea(point)) return event;
        if (event.clickCount == 2) {
            NSString* action = [NSUserDefaults.standardUserDefaults stringForKey:@"AppleActionOnDoubleClick"];
            if ([action isEqualToString:@"Minimize"]) [native performMiniaturize:nil];
            else if (![action isEqualToString:@"None"]) [native performZoom:nil];
        } else {
            [native performWindowDragWithEvent:event];
        }
        return nil;
    }];
    m_Monitor = [monitor retain];
}

void MacTitleBar::setCanvasVisible(bool visible)
{
    if (m_CanvasVisible == visible) return;
    m_CanvasVisible = visible;
    m_FullscreenRestoreFrame = {};
    cancelWindowGesture(true);
    updateChrome();
}

void MacTitleBar::setStreamTitleBarHidden(bool hidden)
{
    if (m_StreamTitleBarHidden == hidden) return;
    m_StreamTitleBarHidden = hidden;
    m_FullscreenRestoreFrame = {};
    cancelWindowGesture(true);
    updateChrome();
}

void MacTitleBar::updateChrome()
{
    NSWindow* native = m_Window ? reinterpret_cast<NSView*>(m_Window->winId()).window : nil;
    if (!native) return;
    const NSRect frame = native.frame;
    // Devices use a unified application bar. Streaming uses only the normal
    // system title bar, outside the canvas, so no controls cover remote pixels.
    // Update the existing NSWindow rather than changing Qt flags/recreating it.
    // Optionally the canvas covers the title bar too: transparent, no title and
    // no buttons. Qt still receives clicks there, as on the Devices top bar.
    const bool bare = m_CanvasVisible && m_StreamTitleBarHidden &&
                      !m_FullscreenTransitioning && !isNativeFullscreen();
    const bool systemBar = m_CanvasVisible && !bare;
    auto style = systemBar ? native.styleMask & ~NSWindowStyleMaskFullSizeContentView
                                 : native.styleMask | NSWindowStyleMaskFullSizeContentView;
    // A bare canvas has no implicit local mouse operations. Its explicit chord
    // owns frame changes; restore normal AppKit controls outside this mode.
    if (bare) style &= ~NSWindowStyleMaskResizable;
    else if (m_NativeResizable) style |= NSWindowStyleMaskResizable;
    native.movable = bare ? NO : m_NativeMovable;
    if (native.styleMask != style) native.styleMask = style;
    native.titleVisibility = systemBar ? NSWindowTitleVisible : NSWindowTitleHidden;
    native.titlebarAppearsTransparent = !systemBar;
    native.titlebarSeparatorStyle = bare ? NSTitlebarSeparatorStyleNone : NSTitlebarSeparatorStyleAutomatic;
    for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton})
        [native standardWindowButton:kind].hidden = bare;
    native.toolbar.visible = !m_CanvasVisible && !m_FullscreenTransitioning && !isNativeFullscreen();
    // AppKit can grow the frame when restoring the unified toolbar. A page
    // change must preserve the outer window size, otherwise recall negotiates
    // another remote resolution even though the user never resized the window.
    if (!m_FullscreenTransitioning && !isNativeFullscreen() && !NSEqualRects(native.frame, frame))
        [native setFrame:frame display:YES];
}

bool MacTitleBar::isNativeFullscreen() const
{
    NSWindow* native = m_Window ? reinterpret_cast<NSView*>(m_Window->winId()).window : nil;
    return native && (native.styleMask & NSWindowStyleMaskFullScreen);
}

bool MacTitleBar::isToolbarVisible() const
{
    NSWindow* native = m_Window ? reinterpret_cast<NSView*>(m_Window->winId()).window : nil;
    return native && native.toolbar.visible;
}

bool MacTitleBar::isContentUnderTitleBar() const
{
    NSWindow* native = m_Window ? reinterpret_cast<NSView*>(m_Window->winId()).window : nil;
    return native && (native.styleMask & NSWindowStyleMaskFullSizeContentView);
}
