#include "macclipboard.h"
#import <AppKit/AppKit.h>

long long deskPortClipboardChangeCount(const char* name) {
    @autoreleasepool {
        NSPasteboard* board = name ? [NSPasteboard pasteboardWithName:[NSString stringWithUTF8String:name]]
                                  : [NSPasteboard generalPasteboard];
        return board.changeCount;
    }
}
