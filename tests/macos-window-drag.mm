#include "backend/mactitlebar.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QTest>
#include <vector>
#import <AppKit/AppKit.h>
#import <objc/runtime.h>

// Only this process receives the synthetic events. The fixture intercepts the
// native tracking entry point on its own window, so no physical input, remote
// session or user's window can be moved by this regression test.
class SessionCanvas : public QQuickItem {
    Q_OBJECT
public:
    explicit SessionCanvas(QQuickItem* parent) : QQuickItem(parent) { setAcceptedMouseButtons(Qt::AllButtons); }
    int presses = 0, moves = 0, releases = 0;
protected:
    void mousePressEvent(QMouseEvent* event) override { ++presses; event->accept(); }
    void mouseMoveEvent(QMouseEvent* event) override { ++moves; event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { ++releases; event->accept(); }
};

namespace {
struct DragProbe {
    QQuickWindow* window;
    int count = 0;
    bool inputSuspended = false;
};
char probeKey;
IMP originalDrag = nullptr;
IMP originalSendEvent = nullptr;
constexpr NSInteger EdgeEventNumber = 0x4450;
struct ForwardedMouse {
    NSInteger eventNumber;
    NSInteger windowNumber;
    NSEventType type;
    NSPoint location;
    NSEventModifierFlags modifiers;
};
struct EdgeProbe {
    std::vector<ForwardedMouse> forwarded;
};
char edgeProbeKey;
void captureEdgeDelivery(id window, SEL selector, NSEvent* event) {
    auto probe = static_cast<EdgeProbe*>([(NSValue*)objc_getAssociatedObject(window, &edgeProbeKey) pointerValue]);
    if (probe && (event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeLeftMouseDragged ||
                  event.type == NSEventTypeLeftMouseUp) && event.eventNumber >= EdgeEventNumber && event.eventNumber < EdgeEventNumber + 65536) {
        // Capture at the NSWindow boundary, after local monitors. Do not enter
        // AppKit's real resize loop, which waits for physical device events.
        probe->forwarded.push_back({event.eventNumber, event.windowNumber, event.type,
                                   event.locationInWindow, event.modifierFlags});
        return;
    }
    reinterpret_cast<void(*)(id, SEL, NSEvent*)>(originalSendEvent)(window, selector, event);
}
void trackDrag(id window, SEL, NSEvent* event) {
    auto probe = static_cast<DragProbe*>([(NSValue*)objc_getAssociatedObject(window, &probeKey) pointerValue]);
    if (!probe) { reinterpret_cast<void(*)(id, SEL, NSEvent*)>(originalDrag)(window, @selector(performWindowDragWithEvent:), event); return; }
    ++probe->count;
    probe->inputSuspended = probe->window->property("deskportLocalWindowDrag").toBool();
}
void postMouse(NSWindow* window, NSEventType type, NSEventModifierFlags modifiers) {
    const NSPoint point = type == NSEventTypeLeftMouseDown ? NSMakePoint(200, 200) : NSMakePoint(220, 210);
    NSEvent* event = [NSEvent mouseEventWithType:type location:point
        modifierFlags:modifiers timestamp:NSProcessInfo.processInfo.systemUptime
        windowNumber:window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];
    [NSApp postEvent:event atStart:NO];
    QTest::qWait(20);
}
void postFlags(NSWindow* window, NSEventModifierFlags modifiers) {
    NSEvent* event = [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSMakePoint(220, 210)
        modifierFlags:modifiers timestamp:NSProcessInfo.processInfo.systemUptime
        windowNumber:window.windowNumber context:nil characters:@"" charactersIgnoringModifiers:@""
        isARepeat:NO keyCode:59];
    [NSApp postEvent:event atStart:NO];
    QTest::qWait(20);
}
NSInteger postEdgeMouse(NSWindow* window, NSEventType type, NSPoint point, NSEventModifierFlags modifiers) {
    static NSInteger nextEvent = EdgeEventNumber;
    const NSInteger eventNumber = nextEvent++;
    NSEvent* event = [NSEvent mouseEventWithType:type location:point modifierFlags:modifiers
        timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.windowNumber
        context:nil eventNumber:eventNumber clickCount:1 pressure:1];
    [NSApp postEvent:event atStart:NO];
    QTest::qWait(20);
    return eventNumber;
}
}

class MacWindowDragTest : public QObject {
    Q_OBJECT
private slots:
    void nativeLiveResizeKeepsInputSuspendedUntilEnd() {
        QQuickWindow window;
        window.setFlags(Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
        window.resize(640, 480);
        SessionCanvas canvas(window.contentItem());
        canvas.setSize({640, 480});
        MacTitleBar titleBar;
        titleBar.attach(&window);
        titleBar.setCanvasVisible(true);
        titleBar.setStreamTitleBarHidden(true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(30);
        auto native = reinterpret_cast<NSView*>(window.winId()).window;
        // Create another native window without showing it or changing focus.
        // Notifications from it must never start or finish this window's resize.
        QQuickWindow other;
        other.resize(320, 240);
        auto otherNative = reinterpret_cast<NSView*>(other.winId()).window;
        QVERIFY(otherNative && otherNative != native);
        const auto notify = [](NSString* name, NSWindow* owner) {
            [NSNotificationCenter.defaultCenter postNotificationName:name object:owner];
        };
        const auto suspended = [&] { return window.property("deskportLocalWindowDrag").toBool(); };
        QVERIFY(!suspended());
        notify(NSWindowWillStartLiveResizeNotification, otherNative);
        QVERIFY(!suspended());
        notify(NSWindowWillStartLiveResizeNotification, native);
        QVERIFY(suspended());
        // Ordinary native resizing has no Control+Command chord. The monitor
        // must not mistake these events for the end of a local move gesture.
        postMouse(native, NSEventTypeMouseMoved, 0);
        QVERIFY(suspended());
        postFlags(native, 0);
        QVERIFY(suspended());
        notify(NSWindowDidEndLiveResizeNotification, otherNative);
        QVERIFY(suspended());
        notify(NSWindowDidResignKeyNotification, otherNative);
        QVERIFY(suspended());
        notify(NSWindowDidEndLiveResizeNotification, native);
        QVERIFY(!suspended());
        postMouse(native, NSEventTypeMouseMoved, 0);
        QVERIFY(!suspended());

        // Native tracking may be interrupted before DidEndLiveResize. Hiding
        // or losing key status must still release the input suspension.
        notify(NSWindowWillStartLiveResizeNotification, native);
        QVERIFY(suspended());
        window.hide();
        QVERIFY(!suspended());
        notify(NSWindowDidEndLiveResizeNotification, native);
        QVERIFY(!suspended());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(30);
        QVERIFY(!suspended());
        notify(NSWindowWillStartLiveResizeNotification, native);
        QVERIFY(suspended());
        notify(NSWindowDidResignKeyNotification, native);
        QVERIFY(!suspended());
        postMouse(native, NSEventTypeMouseMoved, 0);
        postFlags(native, 0);
        QVERIFY(!suspended());
        notify(NSWindowDidEndLiveResizeNotification, native);
        QVERIFY(!suspended());
        // All notifications and events above belong only to fixture windows;
        // this verifies the pause boundary, not a physical resize operation.
        QCOMPARE(canvas.presses, 0);
        QCOMPARE(canvas.releases, 0);
    }
    void resizeEdgesBypassBothLocalMoveGestures() {
        QQuickWindow window;
        window.setFlags(Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
        window.resize(640, 480);
        window.setPosition(137, 219);
        SessionCanvas canvas(window.contentItem());
        canvas.setSize({640, 480});
        QQuickItem topBar(window.contentItem());
        topBar.setObjectName("topBar");
        topBar.setSize({640, 52});
        MacTitleBar titleBar;
        titleBar.attach(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto native = reinterpret_cast<NSView*>(window.winId()).window;
        QVERIFY(native.styleMask & NSWindowStyleMaskResizable);
        DragProbe drag{&window};
        EdgeProbe edge;
        Class windowClass = object_getClass(native);
        const auto replace = [windowClass](SEL selector, IMP replacement) {
            Method method = class_getInstanceMethod(windowClass, selector);
            const IMP original = method_getImplementation(method);
            class_addMethod(windowClass, selector, original, method_getTypeEncoding(method));
            method_setImplementation(class_getInstanceMethod(windowClass, selector), replacement);
            return original;
        };
        originalDrag = replace(@selector(performWindowDragWithEvent:), (IMP)trackDrag);
        originalSendEvent = replace(@selector(sendEvent:), (IMP)captureEdgeDelivery);
        objc_setAssociatedObject(native, &probeKey, [NSValue valueWithPointer:&drag], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(native, &edgeProbeKey, [NSValue valueWithPointer:&edge], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        struct RestoreMethods {
            NSWindow* window;
            Class windowClass;
            ~RestoreMethods() {
                method_setImplementation(class_getInstanceMethod(windowClass, @selector(sendEvent:)), originalSendEvent);
                method_setImplementation(class_getInstanceMethod(windowClass, @selector(performWindowDragWithEvent:)), originalDrag);
                objc_setAssociatedObject(window, &probeKey, nil, OBJC_ASSOCIATION_ASSIGN);
                objc_setAssociatedObject(window, &edgeProbeKey, nil, OBJC_ASSOCIATION_ASSIGN);
            }
        } restore{native, windowClass};
        const auto chord = NSEventModifierFlagControl | NSEventModifierFlagCommand;
        QString forwardingError;
        const auto checkPassThrough = [&](NSPoint location, NSEventModifierFlags modifiers) {
            const auto priorMoves = drag.count;
            for (auto type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseDragged, NSEventTypeLeftMouseUp}) {
                const auto before = edge.forwarded.size();
                const auto eventNumber = postEdgeMouse(native, type, location, modifiers);
                if (edge.forwarded.size() != before + 1) {
                    forwardingError = QString("Native event %1 at (%2, %3), modifiers %4 was consumed before NSWindow")
                        .arg(type).arg(location.x).arg(location.y).arg(modifiers);
                    return false;
                }
                const auto& received = edge.forwarded.back();
                // NSApplication may copy a posted event; its unique sequence
                // and payload identify the unchanged original mouse input.
                if (received.eventNumber != eventNumber || received.windowNumber != native.windowNumber ||
                    received.type != type || received.location.x != location.x ||
                    received.location.y != location.y || received.modifiers != modifiers) {
                    forwardingError = QString("Native event %1 at (%2, %3) was replaced or modified")
                        .arg(type).arg(location.x).arg(location.y);
                    return false;
                }
            }
            if (drag.count != priorMoves) {
                forwardingError = "Resize input started a local window move";
                return false;
            }
            return true;
        };
        // All event positions and the protected rim are AppKit points. Moving
        // the window to a different nonzero screen origin must not change the
        // result, regardless of the current screen's backing pixel density.
        for (const auto& origin : {QPoint(137, 219), QPoint(347, 141)}) {
            window.setPosition(origin);
            QTest::qWait(30);
            QVERIFY(native.frame.origin.x != 0 || native.frame.origin.y != 0);
            qInfo("WINDOW_EDGE_SCALE backing=%g qt=%g", native.backingScaleFactor, window.devicePixelRatio());
            for (const bool viewing : {false, true}) {
                canvas.setVisible(viewing);
                topBar.setVisible(!viewing);
                titleBar.setStreamTitleBarHidden(viewing);
                titleBar.setCanvasVisible(viewing);
                const NSRect frame = [native convertRectFromScreen:native.frame];
                const CGFloat left = NSMinX(frame) + 1, right = NSMaxX(frame) - 1;
                const CGFloat bottom = NSMinY(frame) + 1, top = NSMaxY(frame) - 1;
                const CGFloat midX = NSMidX(frame), midY = NSMidY(frame);
                const std::vector<NSPoint> edgePoints = viewing ? std::vector<NSPoint>{
                    {left, midY}, {right, midY}, {midX, top}, {midX, bottom},
                    {left, top}, {right, top}, {left, bottom}, {right, bottom}}
                    : std::vector<NSPoint>{{midX, top}, {left, top}, {right, top}};
                for (const auto point : edgePoints) {
                    // Ordinary resize is the user's reported failure. The
                    // canvas modifier gesture must also yield at the rim.
                    QVERIFY2(checkPassThrough(point, 0), qPrintable(forwardingError));
                    if (viewing) QVERIFY2(checkPassThrough(point, chord), qPrintable(forwardingError));
                }
                const NSPoint inside = viewing ? NSMakePoint(midX, midY) : NSMakePoint(midX, top - 25);
                const auto moves = drag.count;
                const auto forwarded = edge.forwarded.size();
                postEdgeMouse(native, NSEventTypeLeftMouseDown, inside, viewing ? chord : 0);
                QCOMPARE(drag.count, moves + 1);
                QCOMPARE(edge.forwarded.size(), forwarded);
                // An incomplete local gesture must not consume the following
                // resize's down/drag/up after the next press resets its tail.
                QVERIFY2(checkPassThrough({right, midY}, 0), qPrintable(forwardingError));
                if (viewing) QVERIFY2(checkPassThrough({left, midY}, chord), qPrintable(forwardingError));
                else {
                    for (auto kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
                        NSButton* button = [native standardWindowButton:kind];
                        QVERIFY(button && !button.hidden);
                        const NSPoint center = [button convertPoint:NSMakePoint(NSMidX(button.bounds), NSMidY(button.bounds)) toView:nil];
                        QVERIFY2(checkPassThrough(center, 0), qPrintable(forwardingError));
                    }
                }
            }
        }
    }
    void nativeMonitorKeepsWindowMovesLocal() {
        QQuickWindow window;
        window.setFlags(Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
        window.resize(640, 480);
        SessionCanvas canvas(window.contentItem());
        canvas.setSize({640, 480});
        MacTitleBar titleBar;
        titleBar.attach(&window);
        titleBar.setCanvasVisible(true);
        titleBar.setStreamTitleBarHidden(true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto native = reinterpret_cast<NSView*>(window.winId()).window;
        DragProbe probe{&window};
        Class windowClass = object_getClass(native);
        const SEL selector = @selector(performWindowDragWithEvent:);
        Method method = class_getInstanceMethod(windowClass, selector);
        originalDrag = method_getImplementation(method);
        class_addMethod(windowClass, selector, originalDrag, method_getTypeEncoding(method));
        method = class_getInstanceMethod(windowClass, selector);
        method_setImplementation(method, (IMP)trackDrag);
        objc_setAssociatedObject(native, &probeKey, [NSValue valueWithPointer:&probe], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        struct RestoreMethod {
            NSWindow* window;
            Method method;
            ~RestoreMethod() {
                method_setImplementation(method, originalDrag);
                objc_setAssociatedObject(window, &probeKey, nil, OBJC_ASSOCIATION_ASSIGN);
            }
        } restore{native, method};
        const auto chord = NSEventModifierFlagControl | NSEventModifierFlagCommand;
        postMouse(native, NSEventTypeLeftMouseDown, chord);
        QCOMPARE(probe.count, 1);
        QVERIFY(probe.inputSuspended);
        QCOMPARE(canvas.presses, 0);
        postMouse(native, NSEventTypeLeftMouseDragged, chord);
        postMouse(native, NSEventTypeLeftMouseUp, chord);
        QCOMPARE(canvas.moves, 0);
        QCOMPARE(canvas.releases, 0);
        // The next ordinary press resets only this gesture's tail suppression.
        postMouse(native, NSEventTypeLeftMouseDown, 0);
        postMouse(native, NSEventTypeLeftMouseDragged, 0);
        postMouse(native, NSEventTypeLeftMouseUp, 0);
        QCOMPARE(probe.count, 1);
        QCOMPARE(canvas.presses, 1);
        QCOMPARE(canvas.moves, 1);
        QCOMPARE(canvas.releases, 1);
        QVERIFY(!window.property("deskportLocalWindowDrag").toBool());
        // A remote chord with extra modifiers remains a remote input gesture.
        postMouse(native, NSEventTypeLeftMouseDown, chord | NSEventModifierFlagOption);
        QCOMPARE(probe.count, 1);
        QCOMPARE(canvas.presses, 2);
        titleBar.setStreamTitleBarHidden(false);
        postMouse(native, NSEventTypeLeftMouseDown, chord);
        QCOMPARE(probe.count, 1);
        titleBar.setStreamTitleBarHidden(true);
        titleBar.setCanvasVisible(false);
        postMouse(native, NSEventTypeLeftMouseDown, chord);
        QCOMPARE(probe.count, 1);
        titleBar.setCanvasVisible(true);
        [NSNotificationCenter.defaultCenter postNotificationName:NSWindowWillEnterFullScreenNotification object:native];
        postMouse(native, NSEventTypeLeftMouseDown, chord);
        QCOMPARE(probe.count, 1);
        [NSNotificationCenter.defaultCenter postNotificationName:NSWindowDidExitFullScreenNotification object:native];
        postMouse(native, NSEventTypeLeftMouseDown, chord);
        QCOMPARE(probe.count, 2);
        // Events for another native window must not start or finish this drag.
        QQuickWindow other;
        other.resize(320, 240);
        SessionCanvas otherCanvas(other.contentItem());
        otherCanvas.setSize({320, 240});
        other.show();
        QVERIFY(QTest::qWaitForWindowExposed(&other));
        postMouse(reinterpret_cast<NSView*>(other.winId()).window, NSEventTypeLeftMouseDown, chord);
        QCOMPARE(probe.count, 2);
        QCOMPARE(otherCanvas.presses, 1);
        window.setProperty("deskportLocalWindowDrag", true);
        [NSNotificationCenter.defaultCenter postNotificationName:NSWindowDidResignKeyNotification object:native];
        QVERIFY(!window.property("deskportLocalWindowDrag").toBool());
        window.setProperty("deskportLocalWindowDrag", true);
        window.hide();
        QVERIFY(!window.property("deskportLocalWindowDrag").toBool());
    }
};
QTEST_MAIN(MacWindowDragTest)
#include "macos-window-drag.moc"
