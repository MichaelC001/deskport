// SPDX-License-Identifier: GPL-3.0-or-later
// Session-only display configuration. Journal UUIDs rather than transient IDs.
#import <sys/stat.h>
#import <ApplicationServices/ApplicationServices.h>
static NSArray<NSDictionary *> *savedTopology;
static BOOL preserveTopologyJournal;
static NSString *topologyPath(void) {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/DeskPort/display-recovery.json"];
}
static NSString *displayUUID(CGDirectDisplayID ident) {
    if (!ident) return @"";
    CFUUIDRef uuid=CGDisplayCreateUUIDFromDisplayID(ident);
    if (!uuid) return @"";
    NSString *result=CFBridgingRelease(CFUUIDCreateString(NULL,uuid)); CFRelease(uuid); return result;
}
static NSArray<NSNumber *> *onlineDisplays(void) {
    CGDirectDisplayID ids[64]; uint32_t count=0;
    if (CGGetOnlineDisplayList(64,ids,&count)!=kCGErrorSuccess) return @[];
    NSMutableArray *result=[NSMutableArray new];
    for (uint32_t i=0;i<count;i++) [result addObject:@(ids[i])];
    return result;
}
static CGDirectDisplayID findDisplay(NSString *uuid) {
    for (NSNumber *n in onlineDisplays()) if ([displayUUID(n.unsignedIntValue) isEqual:uuid]) return n.unsignedIntValue;
    return 0;
}
static BOOL snapshotTopology(CGDirectDisplayID own) {
    if (savedTopology) return YES;
    NSMutableArray *entries=[NSMutableArray new];
    for (NSNumber *n in onlineDisplays()) {
        CGDirectDisplayID ident=n.unsignedIntValue; if (ident==own) continue;
        CGDisplayModeRef mode=CGDisplayCopyDisplayMode(ident); if (!mode) return NO;
        CGRect bounds=CGDisplayBounds(ident);
        [entries addObject:@{@"uuid":displayUUID(ident), @"main":@(CGDisplayIsMain(ident)),
            @"mirror":displayUUID(CGDisplayMirrorsDisplay(ident)), @"x":@(bounds.origin.x), @"y":@(bounds.origin.y),
            @"width":@(CGDisplayModeGetWidth(mode)), @"height":@(CGDisplayModeGetHeight(mode)),
            @"pixelsW":@(CGDisplayModeGetPixelWidth(mode)), @"pixelsH":@(CGDisplayModeGetPixelHeight(mode)),
            @"mode":@(CGDisplayModeGetIODisplayModeID(mode)), @"hz":@(CGDisplayModeGetRefreshRate(mode))}];
        CFRelease(mode);
    }
    NSString *path=topologyPath();
    if (![NSFileManager.defaultManager createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:nil]) return NO;
    NSData *data=[NSJSONSerialization dataWithJSONObject:entries options:0 error:nil];
    if (![data writeToFile:path options:NSDataWritingAtomic error:nil]) return NO;
    chmod(path.fileSystemRepresentation,0600); savedTopology=entries; return YES;
}
static CGDisplayModeRef savedMode(CGDirectDisplayID ident, NSDictionary *entry) {
    CFArrayRef modes=CGDisplayCopyAllDisplayModes(ident,(__bridge CFDictionaryRef)@{(__bridge NSString *)kCGDisplayShowDuplicateLowResolutionModes:@YES});
    CGDisplayModeRef result=NULL;
    for (id item in (__bridge NSArray *)modes) {
        CGDisplayModeRef mode=(__bridge CGDisplayModeRef)item;
        if (CGDisplayModeGetWidth(mode)==[entry[@"width"] unsignedIntegerValue] &&
            CGDisplayModeGetHeight(mode)==[entry[@"height"] unsignedIntegerValue] &&
            CGDisplayModeGetPixelWidth(mode)==[entry[@"pixelsW"] unsignedIntegerValue] &&
            CGDisplayModeGetPixelHeight(mode)==[entry[@"pixelsH"] unsignedIntegerValue]) {
            if (!result || CGDisplayModeGetIODisplayModeID(mode)==[entry[@"mode"] unsignedIntValue]) {
                if (result) CFRelease(result); result=CGDisplayModeRetain(mode);
            }
        }
    }
    if (modes) CFRelease(modes); return result;
}
static BOOL restoreTopology(CGDirectDisplayID own) {
    if (!savedTopology) return YES;
    CGDisplayConfigRef config; if (CGBeginDisplayConfiguration(&config)!=kCGErrorSuccess) return NO;
    CGError error=kCGErrorSuccess; CGFloat right=0;
    CGDirectDisplayID originalMain=0, fallback=0;
    for (NSDictionary *entry in savedTopology) {
        CGDirectDisplayID ident=findDisplay(entry[@"uuid"]); if (!ident) continue;
        if (!fallback) fallback=ident;
        if ([entry[@"main"] boolValue]) originalMain=ident;
        if (error==kCGErrorSuccess) error=CGConfigureDisplayMirrorOfDisplay(config,ident,kCGNullDirectDisplay);
        CGDisplayModeRef mode=savedMode(ident,entry);
        if (mode && error==kCGErrorSuccess) error=CGConfigureDisplayWithDisplayMode(config,ident,mode,NULL);
        if (mode) CFRelease(mode);
        if (error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,ident,[entry[@"x"] intValue],[entry[@"y"] intValue]);
        right=MAX(right,[entry[@"x"] doubleValue]+[entry[@"width"] doubleValue]);
    }
    if (own && fallback && error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,own,(int)MAX(1,right),0);
    if (!originalMain && fallback && error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,fallback,0,0);
    for (NSDictionary *entry in savedTopology) {
        CGDirectDisplayID ident=findDisplay(entry[@"uuid"]), source=findDisplay(entry[@"mirror"]);
        if (ident && source && error==kCGErrorSuccess) error=CGConfigureDisplayMirrorOfDisplay(config,ident,source);
    }
    if (error==kCGErrorSuccess) error=CGCompleteDisplayConfiguration(config,kCGConfigureForSession);
    else CGCancelDisplayConfiguration(config);
    if (error!=kCGErrorSuccess) return NO;
    if (!preserveTopologyJournal) { savedTopology=nil; [NSFileManager.defaultManager removeItemAtPath:topologyPath() error:nil]; }
    return YES;
}
static BOOL sessionTopologyReady(CGDirectDisplayID own) {
    if (!CGDisplayIsMain(own) || CGDisplayMirrorsDisplay(own)) return NO;
    for (NSNumber *n in onlineDisplays()) if (n.unsignedIntValue!=own && CGDisplayMirrorsDisplay(n.unsignedIntValue)!=own) return NO;
    return YES;
}
static BOOL applySessionTopology(CGDirectDisplayID own) {
    if (!snapshotTopology(own)) return NO;
    if (sessionTopologyReady(own)) return YES;
    CGDisplayConfigRef config; if (CGBeginDisplayConfiguration(&config)!=kCGErrorSuccess) return NO;
    CGError error=kCGErrorSuccess;
    for (NSNumber *n in onlineDisplays()) {
        if (error==kCGErrorSuccess) error=CGConfigureDisplayMirrorOfDisplay(config,n.unsignedIntValue,kCGNullDirectDisplay);
    }
    if (error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,own,0,0);
    for (NSNumber *n in onlineDisplays()) if (n.unsignedIntValue!=own && error==kCGErrorSuccess)
        error=CGConfigureDisplayMirrorOfDisplay(config,n.unsignedIntValue,own);
    if (error==kCGErrorSuccess) error=CGCompleteDisplayConfiguration(config,kCGConfigureForSession);
    else CGCancelDisplayConfiguration(config);
    return error==kCGErrorSuccess;
}
