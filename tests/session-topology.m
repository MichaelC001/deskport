// SPDX-License-Identifier: GPL-3.0-or-later
// Run the production topology adapter against an isolated in-memory WindowServer.
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <dlfcn.h>
#import <assert.h>
static NSMutableDictionary *screens, *pending;
static NSString *testHome;
static BOOL rejectCommit, enableAvailable=YES;
static NSMutableDictionary *screen(unsigned i) { return screens[@(i)]; }
static CGError listScreens(uint32_t max, CGDirectDisplayID *ids, uint32_t *count) {
    *count=0; for (NSNumber *n in [[screens allKeys] sortedArrayUsingSelector:@selector(compare:)]) if (*count<max) ids[(*count)++]=n.unsignedIntValue;
    return kCGErrorSuccess;
}
static CGError setEnabled(CGDisplayConfigRef c, CGDirectDisplayID i, bool enabled) { pending[@(i)][@"enabled"]=@(enabled); return 0; }
static void *symbol(void *h,const char *name) {
    if (!strcmp(name,"CGSConfigureDisplayEnabled")) return enableAvailable ? (void *)setEnabled : NULL;
    if (!strcmp(name,"CGSGetDisplayList")) return (void *)listScreens;
    return NULL;
}
static CFUUIDRef fakeUuid(unsigned i) { return CFUUIDCreateFromString(NULL,(__bridge CFStringRef)[NSString stringWithFormat:@"00000000-0000-0000-0000-%012u",i]); }
static CGDisplayModeRef fakeMode(unsigned i) { return screen(i) ? (CGDisplayModeRef)CFBridgingRetain([screen(i) copy]) : NULL; }
static NSDictionary *modeData(CGDisplayModeRef m) { return (__bridge NSDictionary *)m; }
static CFArrayRef fakeModes(unsigned i, CFDictionaryRef opts) { return CFBridgingRetain(@[[screen(i) copy]]); }
static CGError begin(CGDisplayConfigRef *c) { pending=[NSMutableDictionary new]; for (NSNumber *n in screens) pending[n]=[screens[n] mutableCopy]; *c=(CGDisplayConfigRef)1; return 0; }
static CGError mirror(CGDisplayConfigRef c,unsigned i,unsigned source) { pending[@(i)][@"mirror"]=@(source); return 0; }
static CGError origin(CGDisplayConfigRef c,unsigned i,int x,int y) {
    pending[@(i)][@"x"]=@(x); pending[@(i)][@"y"]=@(y);
    if (!x && !y) for (NSNumber *n in pending) pending[n][@"main"]=@([n unsignedIntValue]==i);
    return 0;
}
static CGError setMode(CGDisplayConfigRef c,unsigned i,CGDisplayModeRef m,CFDictionaryRef opts) { return 0; }
static CGError complete(CGDisplayConfigRef c,CGConfigureOption option) { if (rejectCommit) return kCGErrorFailure; screens=pending; pending=nil; return 0; }
static CGError cancel(CGDisplayConfigRef c) { pending=nil; return 0; }
static CGRect fakeBounds(unsigned i) { return CGRectMake([screen(i)[@"x"] intValue],[screen(i)[@"y"] intValue],1920,1080); }
#define dlsym symbol
#define NSHomeDirectory() testHome
#define CGGetOnlineDisplayList listScreens
#define CGDisplayCreateUUIDFromDisplayID fakeUuid
#define CGDisplayCopyDisplayMode fakeMode
#define CGDisplayCopyAllDisplayModes fakeModes
#define CGDisplayModeGetWidth(m) [modeData(m)[@"width"] unsignedIntegerValue]
#define CGDisplayModeGetHeight(m) [modeData(m)[@"height"] unsignedIntegerValue]
#define CGDisplayModeGetPixelWidth(m) [modeData(m)[@"width"] unsignedIntegerValue]
#define CGDisplayModeGetPixelHeight(m) [modeData(m)[@"height"] unsignedIntegerValue]
#define CGDisplayModeGetIODisplayModeID(m) 1
#define CGDisplayModeGetRefreshRate(m) 60.0
#define CGDisplayModeRetain(m) ((CGDisplayModeRef)CFRetain(m))
#define CGDisplayIsMain(i) [screen(i)[@"main"] boolValue]
#define CGDisplayIsActive(i) [screen(i)[@"enabled"] boolValue]
#define CGDisplayMirrorsDisplay(i) [screen(i)[@"mirror"] unsignedIntValue]
#define CGDisplayBounds fakeBounds
#define CGBeginDisplayConfiguration begin
#define CGConfigureDisplayMirrorOfDisplay mirror
#define CGConfigureDisplayOrigin origin
#define CGConfigureDisplayWithDisplayMode setMode
#define CGCompleteDisplayConfiguration complete
#define CGCancelDisplayConfiguration cancel
#include "../host/macos/session-topology.h"
static NSMutableDictionary *entry(BOOL enabled,BOOL main,int x) {
    return [@{@"enabled":@(enabled),@"main":@(main),@"mirror":@0,@"x":@(x),@"y":@0,@"width":@1920,@"height":@1080} mutableCopy];
}
static void reset(void) {
    savedTopology=nil; preserveTopologyJournal=NO; rejectCommit=NO; enableAvailable=YES;
    screens=[@{@1:entry(YES,YES,0),@2:entry(YES,NO,1920),@3:entry(NO,NO,3840),@9:entry(YES,NO,5760)} mutableCopy];
    [NSFileManager.defaultManager removeItemAtPath:topologyPath() error:nil];
}
int main(void) { @autoreleasepool {
    testHome=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    for (int policy=0;policy<=2;policy++) {
        reset(); sessionDisplayPolicy=policy;
        assert(snapshotTopology(9)); NSArray *before=savedTopology;
        screens[@4]=entry(YES,NO,7680); // Added after lease acquisition.
        assert(applySessionTopology(9)); assert(sessionTopologyReady(9));
        assert(savedTopology==before); assert([screen(4)[@"enabled"] boolValue]); assert(![screen(4)[@"mirror"] intValue]);
        assert(![screen(3)[@"enabled"] boolValue]);
        if (policy==0) assert([screen(1)[@"mirror"] intValue]==9);
        if (policy==1) assert(![screen(1)[@"enabled"] boolValue] && ![screen(2)[@"enabled"] boolValue]);
        if (policy==2) assert([screen(1)[@"main"] boolValue] && [screen(2)[@"x"] intValue]==1920);
        assert(snapshotTopology(9) && savedTopology==before); // Resize keeps original snapshot.
        restoreTopology(9); assert(restoreTopology(9)); assert(!savedTopology);
        assert([screen(1)[@"main"] boolValue] && [screen(1)[@"enabled"] boolValue]);
        assert([screen(2)[@"enabled"] boolValue] && ![screen(3)[@"enabled"] boolValue]);
    }
    reset(); sessionDisplayPolicy=1; assert(snapshotTopology(9)); enableAvailable=NO;
    assert(!applySessionTopology(9)); assert([screen(1)[@"main"] boolValue]);
    enableAvailable=YES; assert(applySessionTopology(9)); rejectCommit=YES;
    assert(!restoreTopology(9)); assert(savedTopology && [NSFileManager.defaultManager fileExistsAtPath:topologyPath()]);
    rejectCommit=NO;
    [screens removeObjectForKey:@2]; // Detached display is skipped during recovery.
    assert(!restoreTopology(9)); assert(restoreTopology(9)); assert(!savedTopology);
    reset(); sessionDisplayPolicy=0; assert(snapshotTopology(9)); assert(applySessionTopology(9));
    savedTopology=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:topologyPath()] options:0 error:nil];
    assert(!restoreTopology(9)); assert(restoreTopology(9)); // Simulated helper crash/restart.
    [NSFileManager.defaultManager removeItemAtPath:testHome error:nil];
    puts("PASS: three policies, original snapshot, disabled displays, hotplug, failed commit and crash recovery");
} }
