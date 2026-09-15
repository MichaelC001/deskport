// SPDX-License-Identifier: GPL-3.0-or-later
// Query geometry only. Never read the element's text, value or selected text.
static NSDictionary *textCaret(CGDirectDisplayID displayID) {
    NSDictionary *invalid=@{@"valid":@NO};
    if (!AXIsProcessTrusted()) return invalid;
    AXUIElementRef system=AXUIElementCreateSystemWide();
    AXUIElementSetMessagingTimeout(system,0.05);
    CFTypeRef app=NULL, element=NULL, rangeValue=NULL, boundsValue=NULL;
    AXError error=AXUIElementCopyAttributeValue(system,kAXFocusedApplicationAttribute,&app);
    if (app) AXUIElementSetMessagingTimeout((AXUIElementRef)app,0.05);
    if (error==kAXErrorSuccess) error=AXUIElementCopyAttributeValue((AXUIElementRef)app,kAXFocusedUIElementAttribute,&element);
    if (element) AXUIElementSetMessagingTimeout((AXUIElementRef)element,0.05);
    if (error==kAXErrorSuccess) error=AXUIElementCopyAttributeValue((AXUIElementRef)element,kAXSelectedTextRangeAttribute,&rangeValue);
    CFRange range; CGRect bounds=CGRectZero;
    BOOL valid=error==kAXErrorSuccess && rangeValue && CFGetTypeID(rangeValue)==AXValueGetTypeID() && AXValueGetValue((AXValueRef)rangeValue,kAXValueCFRangeType,&range);
    if (valid) {
        range.location+=range.length; range.length=0;
        AXValueRef insertion=AXValueCreate(kAXValueCFRangeType,&range);
        error=AXUIElementCopyParameterizedAttributeValue((AXUIElementRef)element,kAXBoundsForRangeParameterizedAttribute,insertion,&boundsValue);
        CFRelease(insertion);
        valid=error==kAXErrorSuccess && boundsValue && CFGetTypeID(boundsValue)==AXValueGetTypeID() && AXValueGetValue((AXValueRef)boundsValue,kAXValueCGRectType,&bounds);
    }
    CGRect screen=CGDisplayBounds(displayID);
    valid=valid && screen.size.width>0 && screen.size.height>0 && bounds.size.height>0 &&
        isfinite(bounds.origin.x) && isfinite(bounds.origin.y) &&
        CGRectContainsPoint(CGRectInset(screen,-1,-1),CGPointMake(CGRectGetMidX(bounds),CGRectGetMidY(bounds)));
    NSDictionary *result=valid ? @{@"valid":@YES,
        @"x":@((CGRectGetMidX(bounds)-screen.origin.x)/screen.size.width),
        @"y":@((CGRectGetMaxY(bounds)-screen.origin.y)/screen.size.height)} : invalid;
    if (boundsValue) CFRelease(boundsValue); if (rangeValue) CFRelease(rangeValue);
    if (element) CFRelease(element); if (app) CFRelease(app); CFRelease(system);
    return result;
}
