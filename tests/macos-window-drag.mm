#include "backend/mactitlebar.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QTest>
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
}

class MacWindowDragTest : public QObject {
    Q_OBJECT
private slots:
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
