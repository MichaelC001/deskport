#include "mactitlebar.h"

#include <QWindow>
#import <AppKit/AppKit.h>

void deskPortUnifyTitleBar(QWindow* window)
{
    if (!window) return;
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    NSWindow* nativeWindow = view.window;
    if (!nativeWindow) return;
    nativeWindow.titleVisibility = NSWindowTitleHidden;
    // An empty unified toolbar makes the title bar 52 points tall and centres the
    // window buttons in it; it contributes no items of its own.
    NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"DeskPortTopBar"];
    nativeWindow.toolbar = toolbar;
    nativeWindow.toolbarStyle = NSWindowToolbarStyleUnified;
}
