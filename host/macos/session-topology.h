// SPDX-License-Identifier: GPL-3.0-or-later
// Session-only display configuration. Journal UUIDs rather than transient IDs.
#import <sys/stat.h>
#import <fcntl.h>
#import <unistd.h>
#import <dlfcn.h>
#include "../../shared/deskport-core/include/deskport/protocol.h"
static int sessionDisplayPolicy = DP_DISPLAY_PRIMARY_MIRROR;
typedef CGError (*DPEnableDisplay)(CGDisplayConfigRef, CGDirectDisplayID, bool);
static DPEnableDisplay enableDisplayFunction(void) {
    if (!dlsym(RTLD_DEFAULT, "CGSGetDisplayList")) return NULL;
    return (DPEnableDisplay)dlsym(RTLD_DEFAULT, "CGSConfigureDisplayEnabled");
}
static BOOL wasEnabled(NSDictionary *entry) { return !entry[@"enabled"] || [entry[@"enabled"] boolValue]; }
#import <ApplicationServices/ApplicationServices.h>
static NSArray<NSDictionary *> *savedTopology;
static BOOL preserveTopologyJournal;
static CGDirectDisplayID recoveryOwnedDisplay;
static BOOL isolatedDisplay(void) {
    return [NSProcessInfo.processInfo.environment[@"DESKPORT_DISPLAY_ISOLATED"] isEqualToString:@"1"];
}
static NSString *topologyPath(void) {
    if (isolatedDisplay()) {
        NSString *directory=NSProcessInfo.processInfo.environment[@"DESKPORT_DISPLAY_STATE_DIR"];
        return [directory stringByAppendingPathComponent:@"display-recovery.json"];
    }
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
    typedef CGError (*DPDisplayList)(uint32_t, CGDirectDisplayID *, uint32_t *);
    DPDisplayList list=(DPDisplayList)dlsym(RTLD_DEFAULT,"CGSGetDisplayList");
    CGError error=list ? list(64,ids,&count) : CGGetOnlineDisplayList(64,ids,&count);
    if (error!=kCGErrorSuccess || count>=64) return nil;
    NSMutableArray *result=[NSMutableArray new];
    for (uint32_t i=0;i<count;i++) [result addObject:@(ids[i])];
    return result;
}
static CGDirectDisplayID findDisplay(NSString *uuid) {
    for (NSNumber *n in onlineDisplays()) if ([displayUUID(n.unsignedIntValue) isEqual:uuid]) return n.unsignedIntValue;
    return 0;
}
static NSArray *captureTopology(CGDirectDisplayID own) {
    NSArray *attached=onlineDisplays(); if (!attached) return nil;
    NSMutableArray *entries=[NSMutableArray new];
    for (NSNumber *n in attached) {
        CGDirectDisplayID ident=n.unsignedIntValue; if (ident==own) continue;
        BOOL enabled=CGDisplayIsActive(ident) || CGDisplayMirrorsDisplay(ident);
        CGDisplayModeRef mode=CGDisplayCopyDisplayMode(ident); if (!mode && enabled) return nil;
        NSString *uuid=displayUUID(ident);
        if (!uuid.length) {
            if (mode) CFRelease(mode);
            // WindowServer also enumerates disconnected connector placeholders.
            // They have no stable identity or state that can be restored.
            if (!enabled && !CGDisplayIsOnline(ident)) continue;
            return nil;
        }
        CGRect bounds=CGDisplayBounds(ident);
        [entries addObject:@{@"uuid":uuid, @"main":@(CGDisplayIsMain(ident)), @"enabled":@(enabled),
            @"mirror":displayUUID(CGDisplayMirrorsDisplay(ident)==own ? 0 : CGDisplayMirrorsDisplay(ident)), @"x":@(bounds.origin.x), @"y":@(bounds.origin.y),
            @"width":@(mode ? CGDisplayModeGetWidth(mode) : 0), @"height":@(mode ? CGDisplayModeGetHeight(mode) : 0),
            @"pixelsW":@(mode ? CGDisplayModeGetPixelWidth(mode) : 0), @"pixelsH":@(mode ? CGDisplayModeGetPixelHeight(mode) : 0),
            @"mode":@(mode ? CGDisplayModeGetIODisplayModeID(mode) : 0), @"hz":@(mode ? CGDisplayModeGetRefreshRate(mode) : 0)}];
        if (mode) CFRelease(mode);
    }
    return entries;
}
static BOOL snapshotTopology(CGDirectDisplayID own) {
    // Parallel acceptance hosts must never journal or restore another helper's
    // physical/virtual layout. They only move their own extended display.
    if (isolatedDisplay()) return YES;
    if (savedTopology) return YES;
    NSArray *entries=captureTopology(own); if (!entries) return NO;
    NSString *path=topologyPath();
    if (![NSFileManager.defaultManager createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:nil]) return NO;
    NSData *data=[NSJSONSerialization dataWithJSONObject:entries options:0 error:nil];
    if (![data writeToFile:path options:NSDataWritingAtomic error:nil]) return NO;
    chmod(path.fileSystemRepresentation,0600); savedTopology=entries; return YES;
}
// Local diagnostics contain only display configuration, never screen contents.
// Deduplicate unchanged failures and bound storage while preserving the journal.
static void recordLayoutEvent(NSString *reason) {
    NSDictionary *state=@{@"reason":reason, @"policy":@(sessionDisplayPolicy), @"original":savedTopology ?: @[],
        @"observed":captureTopology(0) ?: @[]};
    NSData *signature=[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingSortedKeys error:nil];
    static NSData *previous;
    if ([signature isEqual:previous]) return;
    previous=signature;
    NSMutableDictionary *event=[state mutableCopy];
    event[@"time"]=[NSISO8601DateFormatter stringFromDate:NSDate.date timeZone:[NSTimeZone timeZoneForSecondsFromGMT:0] formatOptions:NSISO8601DateFormatWithInternetDateTime];
    NSData *data=[NSJSONSerialization dataWithJSONObject:event options:NSJSONWritingSortedKeys error:nil];
    NSString *path=[topologyPath().stringByDeletingLastPathComponent stringByAppendingPathComponent:@"display-layout-events.jsonl"];
    NSFileManager *files=NSFileManager.defaultManager;
    if ([[files attributesOfItemAtPath:path error:nil] fileSize]>1024*1024) {
        NSString *old=[path stringByAppendingString:@".previous"];
        [files removeItemAtPath:old error:nil]; [files moveItemAtPath:path toPath:old error:nil];
    }
    [files createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:nil];
    int fd=open(path.fileSystemRepresentation,O_WRONLY|O_CREAT|O_APPEND,0600);
    if (fd>=0) { NSMutableData *line=[data mutableCopy]; [line appendBytes:"\n" length:1]; write(fd,line.bytes,line.length); close(fd); }
    fprintf(stderr,"DeskPort local layout: %s; original and observed layouts recorded\n",reason.UTF8String);
}
static CGDisplayModeRef savedMode(CGDirectDisplayID ident, NSDictionary *entry) {
    CFArrayRef modes=CGDisplayCopyAllDisplayModes(ident,(__bridge CFDictionaryRef)@{(__bridge NSString *)kCGDisplayShowDuplicateLowResolutionModes:@YES});
    CGDisplayModeRef result=NULL;
    for (id item in (__bridge NSArray *)modes) {
        CGDisplayModeRef mode=(__bridge CGDisplayModeRef)item;
        if (CGDisplayModeGetWidth(mode)==[entry[@"width"] unsignedIntegerValue] &&
            CGDisplayModeGetHeight(mode)==[entry[@"height"] unsignedIntegerValue] &&
            CGDisplayModeGetPixelWidth(mode)==[entry[@"pixelsW"] unsignedIntegerValue] &&
            CGDisplayModeGetPixelHeight(mode)==[entry[@"pixelsH"] unsignedIntegerValue] &&
            fabs(CGDisplayModeGetRefreshRate(mode)-[entry[@"hz"] doubleValue])<0.5) {
            if (!result || CGDisplayModeGetIODisplayModeID(mode)==[entry[@"mode"] unsignedIntValue]) {
                if (result) CFRelease(result); result=CGDisplayModeRetain(mode);
            }
        }
    }
    if (modes) CFRelease(modes); return result;
}
// A helper-owned workspace is not an original physical mirror source. Older
// journals can contain this reference because WindowServer restored a mirror
// association as soon as the virtual display was created.
static CGDirectDisplayID restorationSource(NSDictionary *entry) {
    CGDirectDisplayID source=findDisplay(entry[@"mirror"]);
    return source==recoveryOwnedDisplay ? 0 : source;
}
static BOOL restoredTopologyReady(void) {
    if (!onlineDisplays()) return NO;
    for (NSDictionary *entry in savedTopology) {
        CGDirectDisplayID ident=findDisplay(entry[@"uuid"]); if (!ident) continue;
        BOOL enabled=CGDisplayIsActive(ident) || CGDisplayMirrorsDisplay(ident);
        if (enabled!=wasEnabled(entry)) return NO;
        if (!enabled) continue;
        CGDirectDisplayID source=restorationSource(entry);
        if (CGDisplayMirrorsDisplay(ident)!=source) return NO;
        if ([entry[@"main"] boolValue] && !CGDisplayIsMain(ident)) return NO;
        CGRect bounds=CGDisplayBounds(ident);
        if (!source && (bounds.origin.x!=[entry[@"x"] intValue] || bounds.origin.y!=[entry[@"y"] intValue])) return NO;
        CGDisplayModeRef mode=CGDisplayCopyDisplayMode(ident);
        BOOL same=mode && CGDisplayModeGetPixelWidth(mode)==[entry[@"pixelsW"] unsignedIntegerValue] &&
            CGDisplayModeGetPixelHeight(mode)==[entry[@"pixelsH"] unsignedIntegerValue] &&
            CGDisplayModeGetWidth(mode)==[entry[@"width"] unsignedIntegerValue] &&
            CGDisplayModeGetHeight(mode)==[entry[@"height"] unsignedIntegerValue] &&
            fabs(CGDisplayModeGetRefreshRate(mode)-[entry[@"hz"] doubleValue])<0.5;
        if (mode) CFRelease(mode);
        if (!same) return NO;
    }
    return YES;
}
static BOOL restoreTopology(CGDirectDisplayID own) {
    if (isolatedDisplay()) return YES;
    if (own) recoveryOwnedDisplay=own;
    if (!savedTopology) return YES;
    if (!onlineDisplays()) return NO;
    if (restoredTopologyReady()) {
        if (!preserveTopologyJournal) { recordLayoutEvent(@"Local layout recovery verified; detached displays were skipped"); savedTopology=nil; [NSFileManager.defaultManager removeItemAtPath:topologyPath() error:nil]; }
        return YES;
    }
    CGDisplayConfigRef config; if (CGBeginDisplayConfiguration(&config)!=kCGErrorSuccess) return NO;
    // Mirror sinks expose mirror-constrained mode lists. Detach in a separate
    // transaction and let WindowServer publish native modes before selecting one.
    BOOL detach=NO;
    CGError detachError=kCGErrorSuccess;
    for (NSDictionary *entry in savedTopology) {
        CGDirectDisplayID ident=findDisplay(entry[@"uuid"]);
        if (ident && CGDisplayMirrorsDisplay(ident)) {
            detach=YES;
            if (detachError==kCGErrorSuccess)
                detachError=CGConfigureDisplayMirrorOfDisplay(config,ident,kCGNullDirectDisplay);
        }
    }
    if (detach) {
        if (detachError==kCGErrorSuccess) detachError=CGCompleteDisplayConfiguration(config,kCGConfigureForSession);
        else CGCancelDisplayConfiguration(config);
        if (detachError!=kCGErrorSuccess) recordLayoutEvent([NSString stringWithFormat:@"Mirror detach failed (%d); local recovery remains pending",detachError]);
        return NO;
    }
    CGError error=kCGErrorSuccess; BOOL missingMode=NO; CGFloat right=0;
    DPEnableDisplay enable=enableDisplayFunction();
    CGDirectDisplayID originalMain=0, fallback=0;
    for (NSDictionary *entry in savedTopology) {
        CGDirectDisplayID ident=findDisplay(entry[@"uuid"]); if (!ident) continue;
        BOOL enabled=CGDisplayIsActive(ident) || CGDisplayMirrorsDisplay(ident);
        if (enabled!=wasEnabled(entry)) {
            if (error==kCGErrorSuccess) error=enable ? enable(config,ident,wasEnabled(entry)) : kCGErrorFailure;
        }
        if (!wasEnabled(entry)) continue;
        if (!fallback) fallback=ident;
        if ([entry[@"main"] boolValue]) originalMain=ident;
        if (error==kCGErrorSuccess) error=CGConfigureDisplayMirrorOfDisplay(config,ident,kCGNullDirectDisplay);
        CGDisplayModeRef mode=savedMode(ident,entry);
        if (!mode) {
            // Never submit an incomplete restore and then poll an impossible mode.
            // Keep the original journal for a later idle recovery attempt.
            error=kCGErrorFailure; missingMode=YES;
            recordLayoutEvent([NSString stringWithFormat:@"Display %u no longer offers its saved mode; local recovery remains pending",ident]);
        }
        if (mode && error==kCGErrorSuccess) error=CGConfigureDisplayWithDisplayMode(config,ident,mode,NULL);
        if (mode) CFRelease(mode);
        if (error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,ident,[entry[@"x"] intValue],[entry[@"y"] intValue]);
        right=MAX(right,[entry[@"x"] doubleValue]+[entry[@"width"] doubleValue]);
    }
    if (own && fallback && error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,own,(int)MAX(1,right),0);
    if (!originalMain && fallback && error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,fallback,0,0);
    for (NSDictionary *entry in savedTopology) {
        CGDirectDisplayID ident=findDisplay(entry[@"uuid"]), source=restorationSource(entry);
        if (ident && source && error==kCGErrorSuccess) error=CGConfigureDisplayMirrorOfDisplay(config,ident,source);
    }
    if (error==kCGErrorSuccess) error=CGCompleteDisplayConfiguration(config,kCGConfigureForSession);
    else CGCancelDisplayConfiguration(config);
    if (error!=kCGErrorSuccess) { if (!missingMode) recordLayoutEvent([NSString stringWithFormat:@"Local restore transaction failed (%d); recovery remains pending",error]); return NO; }
    // Keep the journal until a subsequent observation verifies the OS state.
    return NO;
}
static BOOL sessionTopologyReady(CGDirectDisplayID own) {
    if (!CGDisplayIsActive(own) || CGDisplayMirrorsDisplay(own)) return NO;
    if (isolatedDisplay()) return !CGDisplayIsMain(own);
    if (sessionDisplayPolicy==DP_DISPLAY_EXTEND) {
        return restoredTopologyReady();
    }
    if (!CGDisplayIsMain(own)) return NO;
    for (NSDictionary *entry in savedTopology) {
        if (!wasEnabled(entry)) continue;
        CGDirectDisplayID ident=findDisplay(entry[@"uuid"]); if (!ident || ident==own) continue;
        if (sessionDisplayPolicy==DP_DISPLAY_PRIMARY_ONLY) {
            if (CGDisplayIsActive(ident) || CGDisplayMirrorsDisplay(ident)) return NO;
        } else if (CGDisplayMirrorsDisplay(ident)!=own) return NO;
    }
    return YES;
}
static BOOL applySessionTopology(CGDirectDisplayID own) {
    if (isolatedDisplay()) {
        if (sessionTopologyReady(own)) return YES;
        CGFloat right=0;
        for (NSNumber *item in onlineDisplays()) {
            CGDirectDisplayID other=item.unsignedIntValue;
            if (other!=own && CGDisplayIsActive(other)) right=MAX(right,CGRectGetMaxX(CGDisplayBounds(other)));
        }
        CGDisplayConfigRef config;
        if (CGBeginDisplayConfiguration(&config)!=kCGErrorSuccess) return NO;
        CGError error=CGConfigureDisplayMirrorOfDisplay(config,own,kCGNullDirectDisplay);
        if (error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,own,(int)MAX(1,right),0);
        if (error==kCGErrorSuccess) error=CGCompleteDisplayConfiguration(config,kCGConfigureForSession);
        else CGCancelDisplayConfiguration(config);
        return error==kCGErrorSuccess;
    }
    if (!snapshotTopology(own)) return NO;
    if (sessionTopologyReady(own)) return YES;
    DPEnableDisplay enable=enableDisplayFunction();
    if (sessionDisplayPolicy==DP_DISPLAY_PRIMARY_ONLY && !enable) return NO;
    CGDisplayConfigRef config; if (CGBeginDisplayConfiguration(&config)!=kCGErrorSuccess) return NO;
    CGError error=CGConfigureDisplayMirrorOfDisplay(config,own,kCGNullDirectDisplay);
    if (sessionDisplayPolicy==DP_DISPLAY_EXTEND) {
        CGFloat right=0;
        for (NSDictionary *entry in savedTopology) if (wasEnabled(entry))
            right=MAX(right,[entry[@"x"] doubleValue]+[entry[@"width"] doubleValue]);
        if (error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,own,(int)MAX(1,right),0);
    } else {
        for (NSDictionary *entry in savedTopology) {
            CGDirectDisplayID ident=findDisplay(entry[@"uuid"]);
            if (ident && wasEnabled(entry) && error==kCGErrorSuccess)
                error=CGConfigureDisplayMirrorOfDisplay(config,ident,kCGNullDirectDisplay);
        }
        if (error==kCGErrorSuccess) error=CGConfigureDisplayOrigin(config,own,0,0);
        for (NSDictionary *entry in savedTopology) {
            CGDirectDisplayID ident=findDisplay(entry[@"uuid"]);
            if (!ident || !wasEnabled(entry) || error!=kCGErrorSuccess) continue;
            error=sessionDisplayPolicy==DP_DISPLAY_PRIMARY_ONLY ? enable(config,ident,false) :
                CGConfigureDisplayMirrorOfDisplay(config,ident,own);
        }
    }
    if (error==kCGErrorSuccess) error=CGCompleteDisplayConfiguration(config,kCGConfigureForSession);
    else CGCancelDisplayConfiguration(config);
    return error==kCGErrorSuccess;
}
