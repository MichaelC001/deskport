#import <Cocoa/Cocoa.h>
#include <SDL.h>
#include <SDL_syswm.h>

// A bare test executable has no application bundle/launcher to activate it.
// Production DeskPort is already a regular GUI application.
void activateNativeTextTest(SDL_Window* window) {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info)) {
        [info.info.cocoa.window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
}
