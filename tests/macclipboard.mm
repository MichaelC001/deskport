#import <AppKit/AppKit.h>
#include "backend/macclipboard.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc == 4 && !strcmp(argv[1], "--write")) {
            NSPasteboard* board = [NSPasteboard pasteboardWithName:[NSString stringWithUTF8String:argv[2]]];
            [board clearContents];
            assert([board setString:[NSString stringWithUTF8String:argv[3]] forType:NSPasteboardTypeString]);
            return 0;
        }
        // A unique named board cannot read or overwrite the user's clipboard.
        NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
        NSString* name = board.name;
        auto before = deskPortClipboardChangeCount(name.UTF8String);
        for (NSString* text in @[@"external copy", @"second copy 中文", @"external copy"]) {
            assert(!NSApp.isActive);
            NSTask* writer = [[NSTask alloc] init];
            writer.executableURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[0]]];
            writer.arguments = @[@"--write", name, text];
            NSError* error = nil;
            assert([writer launchAndReturnError:&error]);
            [writer waitUntilExit];
            assert(writer.terminationStatus == 0);
            auto after = deskPortClipboardChangeCount(name.UTF8String);
            assert(after != before);
            assert(!NSApp.isActive);
            assert([[board stringForType:NSPasteboardTypeString] isEqualToString:text]);
            assert(deskPortClipboardChangeCount(name.UTF8String) == after);
            before = after;
        }
        [board releaseGlobally];
        puts("PASS: native counter detects external-process copies without activation; named pasteboard only");
    }
}
