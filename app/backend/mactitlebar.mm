#include "mactitlebar.h"

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#import <AppKit/AppKit.h>

MacTitleBar::MacTitleBar(QObject* parent) : QObject(parent) {}

MacTitleBar::~MacTitleBar()
{
    if (m_Monitor) [NSEvent removeMonitor:(__bridge id)m_Monitor];
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
    native.toolbar = [[NSToolbar alloc] initWithIdentifier:@"DeskPortTopBar"];
    native.toolbarStyle = NSWindowToolbarStyleUnified;
    // Tell the top bar where the window buttons end once AppKit has laid them
    // out; their size differs between macOS versions.
    QPointer<QQuickWindow> guarded(window);
    dispatch_async(dispatch_get_main_queue(), ^{
        NSButton* zoom = [native standardWindowButton:NSWindowZoomButton];
        if (guarded && zoom) guarded->setProperty("windowButtonsEnd", NSMaxX([zoom convertRect:zoom.bounds toView:nil]));
    });
    // The green button zooms the device list to fill the screen instead of
    // entering full screen, where the title bar and window buttons hide.
    native.collectionBehavior = (native.collectionBehavior & ~NSWindowCollectionBehaviorFullScreenPrimary)
        | NSWindowCollectionBehaviorFullScreenNone;
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
    m_Monitor = (__bridge_retained void*)monitor;
}
