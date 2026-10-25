#include "mactitlebar.h"

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#import <AppKit/AppKit.h>

MacTitleBar::MacTitleBar(QObject* parent) : QObject(parent) {}

MacTitleBar::~MacTitleBar()
{
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

void MacTitleBar::attach(QQuickWindow* window)
{
    m_Window = window;
    NSWindow* native = window ? reinterpret_cast<NSView*>(window->winId()).window : nil;
    if (!native) return;
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
    for (NSString* name in @[NSWindowWillEnterFullScreenNotification, NSWindowWillExitFullScreenNotification,
                              NSWindowDidEnterFullScreenNotification, NSWindowDidExitFullScreenNotification]) {
        const bool starting = [name isEqualToString:NSWindowWillEnterFullScreenNotification] ||
                              [name isEqualToString:NSWindowWillExitFullScreenNotification];
        [observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:name object:native
            queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
                m_FullscreenTransitioning = starting;
                updateChrome();
            }]];
    }
    m_FullscreenObservers = [observers retain];
    connect(window, &QWindow::visibleChanged, this, [this] { updateChrome(); });
    updateChrome();
    // The drag must start from the original AppKit event, before Qt delivers it.
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown handler:^NSEvent*(NSEvent* event) {
        if (event.window != native || native.styleMask & NSWindowStyleMaskFullScreen) return event;
        const NSPoint location = event.locationInWindow;
        // The close, minimize and zoom buttons handle their own clicks.
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
    updateChrome();
}

void MacTitleBar::updateChrome()
{
    NSWindow* native = m_Window ? reinterpret_cast<NSView*>(m_Window->winId()).window : nil;
    if (!native) return;
    // Devices use a unified application bar. Streaming uses only the normal
    // system title bar, outside the canvas, so no controls cover remote pixels.
    // Update the existing NSWindow rather than changing Qt flags/recreating it.
    const auto style = m_CanvasVisible ? native.styleMask & ~NSWindowStyleMaskFullSizeContentView
                                       : native.styleMask | NSWindowStyleMaskFullSizeContentView;
    if (native.styleMask != style) native.styleMask = style;
    native.titleVisibility = m_CanvasVisible ? NSWindowTitleVisible : NSWindowTitleHidden;
    native.titlebarAppearsTransparent = !m_CanvasVisible;
    native.toolbar.visible = !m_CanvasVisible && !m_FullscreenTransitioning && !isNativeFullscreen();
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
