#include "mactitlebar.h"

#include <QWindow>
#import <AppKit/AppKit.h>

static NSWindow* nativeWindow(QWindow* window)
{
    return window ? reinterpret_cast<NSView*>(window->winId()).window : nil;
}

void deskPortUnifyTitleBar(QWindow* window)
{
    NSWindow* native = nativeWindow(window);
    if (!native) return;
    native.titleVisibility = NSWindowTitleHidden;
    // An empty unified toolbar makes the title bar 52 points tall and centres the
    // window buttons in it; it contributes no items of its own.
    native.toolbar = [[NSToolbar alloc] initWithIdentifier:@"DeskPortTopBar"];
    native.toolbarStyle = NSWindowToolbarStyleUnified;
}

void MacTitleBar::startDrag()
{
    NSWindow* native = nativeWindow(m_Window);
    NSEvent* event = NSApp.currentEvent;
    if (native && event.type == NSEventTypeLeftMouseDown) [native performWindowDragWithEvent:event];
}

void MacTitleBar::doubleClick()
{
    NSWindow* native = nativeWindow(m_Window);
    if (!native) return;
    NSString* action = [NSUserDefaults.standardUserDefaults stringForKey:@"AppleActionOnDoubleClick"];
    if ([action isEqualToString:@"Minimize"]) [native performMiniaturize:nil];
    else if (![action isEqualToString:@"None"]) [native performZoom:nil];
}
