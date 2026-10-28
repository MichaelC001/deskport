#include "backend/mactitlebar.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QTest>
#include <vector>
#import <AppKit/AppKit.h>
#import <objc/runtime.h>

// Events are posted only to windows owned by this process. Geometry assertions
// exercise the real NSWindow frame, without replacing setFrame or window moves.
class SessionCanvas : public QQuickItem {
    Q_OBJECT
public:
    explicit SessionCanvas(QQuickItem* parent) : QQuickItem(parent) {
        setAcceptedMouseButtons(Qt::AllButtons);
    }
    int presses = 0, moves = 0, releases = 0;
protected:
    void mousePressEvent(QMouseEvent* e) override { ++presses; e->accept(); }
    void mouseMoveEvent(QMouseEvent* e) override { ++moves; e->accept(); }
    void mouseReleaseEvent(QMouseEvent* e) override { ++releases; e->accept(); }
};

namespace {
constexpr auto Chord = NSEventModifierFlagControl | NSEventModifierFlagCommand;
constexpr int Left = 1, Right = 2, Bottom = 4, Top = 8;
QRectF rectangle(NSRect r) { return {r.origin.x, r.origin.y, r.size.width, r.size.height}; }
NSPoint screenPoint(NSWindow* window, NSPoint local) { return [window convertPointToScreen:local]; }
NSPoint shifted(NSPoint p, qreal dx, qreal dy) { return {p.x + dx, p.y + dy}; }
NSInteger postMouse(NSWindow* window, NSEventType type, NSPoint screen, NSEventModifierFlags modifiers) {
    static NSInteger nextEvent = 0x4450;
    const NSInteger number = nextEvent++;
    NSEvent* event = [NSEvent mouseEventWithType:type location:[window convertPointFromScreen:screen]
        modifierFlags:modifiers timestamp:NSProcessInfo.processInfo.systemUptime
        windowNumber:window.windowNumber context:nil eventNumber:number clickCount:1 pressure:1];
    [NSApp postEvent:event atStart:NO];
    QTest::qWait(10);
    return number;
}
void postFlags(NSWindow* window, NSEventModifierFlags modifiers) {
    NSEvent* event = [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSMakePoint(200, 200)
        modifierFlags:modifiers timestamp:NSProcessInfo.processInfo.systemUptime
        windowNumber:window.windowNumber context:nil characters:@"" charactersIgnoringModifiers:@""
        isARepeat:NO keyCode:59];
    [NSApp postEvent:event atStart:NO];
    QTest::qWait(10);
}
void notify(NSString* name, NSWindow* window) {
    [NSNotificationCenter.defaultCenter postNotificationName:name object:window];
}
struct Fixture {
    QQuickWindow window;
    SessionCanvas canvas{window.contentItem()};
    QQuickItem topBar{window.contentItem()};
    MacTitleBar titleBar;
    NSWindow* native = nil;
    Fixture() {
        window.setFlags(Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
        window.setTitle("DeskPort isolated chord regression");
        window.setMinimumSize({640, 560});
        window.resize(1120, 760);
        window.setPosition(137, 100);
        topBar.setObjectName("topBar");
        const auto layout = [this] {
            canvas.setSize(window.size());
            topBar.setSize({qreal(window.width()), 52});
        };
        QObject::connect(&window, &QWindow::widthChanged, &window, layout);
        QObject::connect(&window, &QWindow::heightChanged, &window, layout);
        layout();
        titleBar.attach(&window);
        bare();
        native = reinterpret_cast<NSView*>(window.winId()).window;
    }
    bool show() {
        window.show(); window.requestActivate();
        return QTest::qWaitForWindowExposed(&window) && QTest::qWaitForWindowActive(&window);
    }
    void bare() {
        topBar.setVisible(false); canvas.setVisible(true);
        titleBar.setCanvasVisible(true); titleBar.setStreamTitleBarHidden(true);
    }
    void devices() {
        canvas.setVisible(false); topBar.setVisible(true); titleBar.setCanvasVisible(false);
    }
    void systemBar() {
        topBar.setVisible(false); canvas.setVisible(true);
        titleBar.setCanvasVisible(true); titleBar.setStreamTitleBarHidden(false);
    }
    bool suspended() const { return window.property("deskportLocalWindowDrag").toBool(); }
    NSPoint startPoint(int edges) const {
        const NSRect bounds = [native convertRectFromScreen:native.frame];
        return screenPoint(native, {
            edges & Left ? NSMinX(bounds) + 4 : edges & Right ? NSMaxX(bounds) - 4 : NSMidX(bounds),
            edges & Bottom ? NSMinY(bounds) + 4 : edges & Top ? NSMaxY(bounds) - 4 : NSMidY(bounds)});
    }
    void finish(NSPoint point) {
        postMouse(native, NSEventTypeLeftMouseUp, point, 0); postFlags(native, 0);
    }
};
QRectF expectedFrame(QRectF original, int edges, qreal dx, qreal dy, QSizeF minimum, QSizeF maximum) {
    if (!edges) return original.translated(dx, dy);
    const auto right = original.x() + original.width(), top = original.y() + original.height();
    if (edges & (Left | Right)) {
        const auto width = qBound(minimum.width(), original.width() + (edges & Left ? -dx : dx), maximum.width());
        original.setX(edges & Left ? right - width : original.x());
        original.setWidth(width);
    }
    if (edges & (Bottom | Top)) {
        const auto height = qBound(minimum.height(), original.height() + (edges & Bottom ? -dy : dy), maximum.height());
        original.setY(edges & Bottom ? top - height : original.y());
        original.setHeight(height);
    }
    return original;
}

// Capture test mouse delivery after local monitors. For Devices this avoids
// entering native resize tracking; for interrupted chords it proves that no
// tail reaches NSWindow even when Qt would ignore an unmatched drag or release.
struct ForwardedMouse { NSInteger number; NSEventType type; NSPoint location; NSEventModifierFlags modifiers; };
char forwardingKey;
IMP originalSendEvent = nullptr;
void captureDelivery(id window, SEL selector, NSEvent* event) {
    auto events = static_cast<std::vector<ForwardedMouse>*>(
        [(NSValue*)objc_getAssociatedObject(window, &forwardingKey) pointerValue]);
    if (events &&
        (event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeLeftMouseDragged || event.type == NSEventTypeLeftMouseUp) &&
        event.eventNumber >= 0x4450) {
        events->push_back({event.eventNumber, event.type, event.locationInWindow, event.modifierFlags});
        return;
    }
    reinterpret_cast<void(*)(id, SEL, NSEvent*)>(originalSendEvent)(window, selector, event);
}
struct ForwardingCapture {
    NSWindow* window;
    Method method;
    std::vector<ForwardedMouse> events;
    explicit ForwardingCapture(NSWindow* value) : window(value) {
        const auto type = object_getClass(window);
        const auto selector = @selector(sendEvent:);
        method = class_getInstanceMethod(type, selector);
        originalSendEvent = method_getImplementation(method);
        class_addMethod(type, selector, originalSendEvent, method_getTypeEncoding(method));
        method = class_getInstanceMethod(type, selector);
        method_setImplementation(method, (IMP)captureDelivery);
        objc_setAssociatedObject(window, &forwardingKey, [NSValue valueWithPointer:&events], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    ~ForwardingCapture() {
        method_setImplementation(method, originalSendEvent);
        objc_setAssociatedObject(window, &forwardingKey, nil, OBJC_ASSOCIATION_ASSIGN);
    }
};
}

class MacWindowDragTest : public QObject {
    Q_OBJECT
private slots:
    void chordChangesActualFrame_data() {
        QTest::addColumn<int>("edges"); QTest::addColumn<bool>("negativeOrigin");
        for (const bool negative : {false, true})
            for (int edges : {0, Left, Right, Bottom, Top, Left | Bottom, Left | Top, Right | Bottom, Right | Top})
                QTest::newRow(qPrintable(QString("edges-%1-%2").arg(edges).arg(negative ? "negative" : "positive"))) << edges << negative;
    }
    void chordChangesActualFrame() {
        QFETCH(int, edges); QFETCH(bool, negativeOrigin);
        Fixture f; QVERIFY(f.show());
        [f.native setFrame:NSMakeRect(negativeOrigin ? -137 : 137, negativeOrigin ? -60 : 180, 1120, 760) display:YES];
        const auto initial = rectangle(f.native.frame);
        if (negativeOrigin) QVERIFY(initial.x() < 0 && initial.y() < 0);
        QVERIFY(!(f.native.styleMask & NSWindowStyleMaskResizable)); QVERIFY(!f.native.movable);
        const auto start = f.startPoint(edges);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, Chord);
        QVERIFY(f.suspended());
        const QSizeF minimum(f.native.minSize.width, f.native.minSize.height);
        const QSizeF maximum(f.native.maxSize.width, f.native.maxSize.height);
        // The second event is an absolute displacement from down, not another
        // increment. This also covers reversing a drag across its starting point.
        for (const QPointF delta : {QPointF(36, 24), QPointF(-20, -16)}) {
            postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, delta.x(), delta.y()), Chord);
            QCOMPARE(rectangle(f.native.frame), expectedFrame(initial, edges, delta.x(), delta.y(), minimum, maximum));
        }
        f.finish(shifted(start, -20, -16)); QVERIFY(!f.suspended());
        QCOMPARE(f.canvas.presses, 0); QCOMPARE(f.canvas.moves, 0); QCOMPARE(f.canvas.releases, 0);
        qInfo("CHORD_FRAME backing=%g qt=%g", f.native.backingScaleFactor, f.window.devicePixelRatio());
    }
    void resizeClampsAndRetainsOppositeEdge_data() {
        QTest::addColumn<int>("edges");
        for (int edges : {Left, Right, Bottom, Top, Left | Bottom, Left | Top, Right | Bottom, Right | Top})
            QTest::newRow(qPrintable(QString::number(edges))) << edges;
    }
    void resizeClampsAndRetainsOppositeEdge() {
        QFETCH(int, edges);
        Fixture f; QVERIFY(f.show());
        [f.native setFrame:NSMakeRect(180, 180, 1120, 760) display:YES];
        f.native.contentMinSize = NSMakeSize(640, 560); f.native.contentMaxSize = NSMakeSize(1280, 960);
        const auto initial = rectangle(f.native.frame); const auto start = f.startPoint(edges);
        const QSizeF minimum(f.native.minSize.width, f.native.minSize.height), maximum(f.native.maxSize.width, f.native.maxSize.height);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, Chord);
        for (const qreal amount : {-900., 900., 20.}) {
            const auto dx = edges & Left ? -amount : edges & Right ? amount : 0;
            const auto dy = edges & Bottom ? -amount : edges & Top ? amount : 0;
            postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, dx, dy), Chord);
            QCOMPARE(rectangle(f.native.frame), expectedFrame(initial, edges, dx, dy, minimum, maximum));
        }
        f.finish(start); QVERIFY(!f.suspended());
    }
    void ordinaryInputCannotChangeBareFrame() {
        Fixture f; QVERIFY(f.show());
        const auto initial = rectangle(f.native.frame);
        for (int edges : {0, Left, Right, Bottom, Top, Left | Bottom, Left | Top, Right | Bottom, Right | Top}) {
            const auto start = f.startPoint(edges);
            postMouse(f.native, NSEventTypeLeftMouseDown, start, 0);
            postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 24, 16), 0);
            postMouse(f.native, NSEventTypeLeftMouseUp, shifted(start, 24, 16), 0);
            QCOMPARE(rectangle(f.native.frame), initial);
        }
        QVERIFY(f.canvas.presses > 0); QVERIFY(f.canvas.releases > 0); QVERIFY(!f.suspended());
        for (auto modifiers : std::vector<NSEventModifierFlags>{NSEventModifierFlagControl, NSEventModifierFlagCommand, Chord | NSEventModifierFlagShift, Chord | NSEventModifierFlagOption}) {
            const auto start = f.startPoint(0);
            postMouse(f.native, NSEventTypeLeftMouseDown, start, modifiers);
            postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 24, 16), modifiers);
            postMouse(f.native, NSEventTypeLeftMouseUp, shifted(start, 24, 16), 0);
            QCOMPARE(rectangle(f.native.frame), initial);
        }
    }
    void modifierLossDrainsGestureWithoutResuming_data() {
        QTest::addColumn<qulonglong>("interruptedModifiers");
        QTest::newRow("control-released") << qulonglong(NSEventModifierFlagCommand);
        QTest::newRow("command-released") << qulonglong(NSEventModifierFlagControl);
        QTest::newRow("both-released") << qulonglong(0);
        QTest::newRow("shift-added") << qulonglong(Chord | NSEventModifierFlagShift);
    }
    void modifierLossDrainsGestureWithoutResuming() {
        QFETCH(qulonglong, interruptedModifiers);
        Fixture f; QVERIFY(f.show()); const auto start = f.startPoint(Right);
        ForwardingCapture capture(f.native);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, Chord);
        postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 20, 0), Chord);
        const auto stopped = rectangle(f.native.frame);
        postFlags(f.native, interruptedModifiers); QVERIFY(f.suspended());
        postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 40, 0), interruptedModifiers);
        QCOMPARE(rectangle(f.native.frame), stopped);
        postFlags(f.native, Chord);
        postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 60, 0), Chord);
        QCOMPARE(rectangle(f.native.frame), stopped);
        f.finish(shifted(start, 60, 0)); QVERIFY(!f.suspended());
        QCOMPARE(capture.events.size(), std::size_t(0));
        QCOMPARE(f.canvas.presses, 0); QCOMPARE(f.canvas.moves, 0); QCOMPARE(f.canvas.releases, 0);
    }
    void mouseUpBeforeModifiersKeepsInputSuspended() {
        Fixture f; QVERIFY(f.show()); const auto start = f.startPoint(0);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, Chord);
        postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 20, 10), Chord);
        postMouse(f.native, NSEventTypeLeftMouseUp, shifted(start, 20, 10), Chord);
        QVERIFY(f.suspended()); postFlags(f.native, NSEventModifierFlagCommand); QVERIFY(f.suspended());
        postFlags(f.native, 0); QVERIFY(!f.suspended());
        const auto frame = rectangle(f.native.frame);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, 0);
        postMouse(f.native, NSEventTypeLeftMouseUp, start, 0);
        QCOMPARE(rectangle(f.native.frame), frame); QCOMPARE(f.canvas.presses, 1); QCOMPARE(f.canvas.releases, 1);
    }
    void inactiveFirstChordStaysLocalAndActivatesGesture_data() {
        QTest::addColumn<int>("edges");
        QTest::newRow("move") << 0;
        QTest::newRow("resize-corner") << (Right | Top);
    }
    void inactiveFirstChordStaysLocalAndActivatesGesture() {
        QFETCH(int, edges);
        Fixture f; QVERIFY(f.show());
        QQuickWindow other;
        other.setTitle("DeskPort isolated activation peer");
        other.resize(320, 240); other.setPosition(1400, 100);
        other.show(); other.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QTRY_VERIFY(!f.native.keyWindow);
        auto otherNative = reinterpret_cast<NSView*>(other.winId()).window;
        const auto otherFrame = rectangle(otherNative.frame);
        const auto original = rectangle(f.native.frame);
        const auto start = f.startPoint(edges);
        ForwardingCapture capture(f.native);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, Chord);
        QCOMPARE(capture.events.size(), std::size_t(0));
        QVERIFY(f.native.keyWindow); QVERIFY(f.suspended());
        postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 24, 16), Chord);
        QCOMPARE(rectangle(f.native.frame), expectedFrame(original, edges, 24, 16,
            {f.native.minSize.width, f.native.minSize.height}, {f.native.maxSize.width, f.native.maxSize.height}));
        f.finish(shifted(start, 24, 16));
        QCOMPARE(capture.events.size(), std::size_t(0)); QVERIFY(!f.suspended());
        QCOMPARE(f.canvas.presses, 0); QCOMPARE(f.canvas.moves, 0); QCOMPARE(f.canvas.releases, 0);
        QCOMPARE(rectangle(otherNative.frame), otherFrame);
    }
    void interruptionCancelsGeometryAndConsumesTail_data() {
        QTest::addColumn<QString>("reason");
        for (const char* reason : {"hide", "resign", "devices", "system-bar"}) QTest::newRow(reason) << QString::fromLatin1(reason);
    }
    void interruptionCancelsGeometryAndConsumesTail() {
        QFETCH(QString, reason);
        Fixture f; QVERIFY(f.show()); const auto start = f.startPoint(0);
        ForwardingCapture capture(f.native);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, Chord);
        postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 12, 10), Chord);
        const auto stopped = rectangle(f.native.frame);
        // Drain while hidden: synthetic events do not set the physical mouse
        // button state that the reactivation safety check deliberately samples.
        if (reason == "hide") f.window.hide();
        else if (reason == "resign") notify(NSWindowDidResignKeyNotification, f.native);
        else if (reason == "devices") { f.devices(); f.bare(); }
        else { f.systemBar(); f.bare(); }
        postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 80, 60), Chord);
        QCOMPARE(rectangle(f.native.frame), stopped);
        f.finish(shifted(start, 80, 60)); QVERIFY(!f.suspended());
        QCOMPARE(capture.events.size(), std::size_t(0));
        QCOMPARE(f.canvas.presses, 0); QCOMPARE(f.canvas.moves, 0); QCOMPARE(f.canvas.releases, 0);
        if (reason == "hide") { QVERIFY(f.show()); QCOMPARE(rectangle(f.native.frame), stopped); }
    }
    void devicesEdgesAndButtonsKeepNativeEvents() {
        Fixture f; f.devices(); QVERIFY(f.show());
        QVERIFY(f.native.styleMask & NSWindowStyleMaskResizable); QVERIFY(f.native.movable);
        ForwardingCapture capture(f.native);
        std::vector<NSPoint> locations;
        for (int edges : {Left, Right, Bottom, Top, Left | Bottom, Left | Top, Right | Bottom, Right | Top})
            locations.push_back(f.startPoint(edges));
        for (auto kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
            NSButton* button = [f.native standardWindowButton:kind]; QVERIFY(button && !button.hidden);
            locations.push_back(screenPoint(f.native, [button convertPoint:NSMakePoint(NSMidX(button.bounds), NSMidY(button.bounds)) toView:nil]));
        }
        for (const auto location : locations) {
            for (auto type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseDragged, NSEventTypeLeftMouseUp}) {
                const auto count = capture.events.size();
                const auto expectedLocation = [f.native convertPointFromScreen:location];
                const auto number = postMouse(f.native, type, location, 0);
                QCOMPARE(capture.events.size(), count + 1);
                const auto received = capture.events.back();
                QCOMPARE(received.number, number); QCOMPARE(received.type, type); QCOMPARE(received.modifiers, NSEventModifierFlags(0));
                QCOMPARE(received.location.x, expectedLocation.x); QCOMPARE(received.location.y, expectedLocation.y);
            }
        }
        const auto frame = rectangle(f.native.frame);
        f.bare(); QCOMPARE(rectangle(f.native.frame), frame);
        QVERIFY(!(f.native.styleMask & NSWindowStyleMaskResizable)); QVERIFY(!f.native.movable);
        f.systemBar(); QCOMPARE(rectangle(f.native.frame), frame);
        QVERIFY(f.native.styleMask & NSWindowStyleMaskResizable); QVERIFY(f.native.movable);
    }
    void nativeLiveResizePauseIsScopedToItsWindow() {
        Fixture f; f.systemBar(); QVERIFY(f.show());
        QQuickWindow other; auto otherNative = reinterpret_cast<NSView*>(other.winId()).window;
        notify(NSWindowWillStartLiveResizeNotification, otherNative); QVERIFY(!f.suspended());
        notify(NSWindowWillStartLiveResizeNotification, f.native); QVERIFY(f.suspended());
        postMouse(f.native, NSEventTypeMouseMoved, f.startPoint(0), 0); postFlags(f.native, 0); QVERIFY(f.suspended());
        notify(NSWindowDidEndLiveResizeNotification, otherNative); QVERIFY(f.suspended());
        notify(NSWindowDidEndLiveResizeNotification, f.native); QVERIFY(!f.suspended());
        notify(NSWindowWillStartLiveResizeNotification, f.native); f.window.hide(); QVERIFY(!f.suspended());
        QVERIFY(f.show()); notify(NSWindowWillStartLiveResizeNotification, f.native);
        notify(NSWindowDidResignKeyNotification, f.native); QVERIFY(!f.suspended());
    }
    void fullscreenNotificationsRestoreBarePolicyAfterQt() {
        Fixture f; QVERIFY(f.show()); const auto start = f.startPoint(0);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, Chord);
        notify(NSWindowWillEnterFullScreenNotification, f.native);
        QVERIFY(f.titleBar.isFullscreenTransitioning()); QVERIFY(f.native.styleMask & NSWindowStyleMaskResizable);
        // This is a notification-order regression, not a physical Space switch.
        // Model Qt's final style write after our synchronous notification handler.
        notify(NSWindowDidExitFullScreenNotification, f.native);
        f.native.styleMask |= NSWindowStyleMaskResizable;
        QTest::qWait(20);
        QVERIFY(!f.titleBar.isFullscreenTransitioning()); QVERIFY(!(f.native.styleMask & NSWindowStyleMaskResizable));
        QVERIFY(!f.native.movable);
        f.finish(start); QVERIFY(!f.suspended());
    }
    void nativeFullscreenRoundTripRestoresFrameAndGesture() {
        Fixture f; QVERIFY(f.show());
        const auto original = rectangle(f.native.frame);
        f.window.showFullScreen();
        QTRY_VERIFY_WITH_TIMEOUT(f.titleBar.isNativeFullscreen() && !f.titleBar.isFullscreenTransitioning(), 7000);
        QVERIFY(f.native.styleMask & NSWindowStyleMaskResizable);
        QVERIFY(!f.suspended());
        f.window.showNormal();
        QTRY_VERIFY_WITH_TIMEOUT(!f.titleBar.isNativeFullscreen() && !f.titleBar.isFullscreenTransitioning(), 7000);
        QTRY_VERIFY_WITH_TIMEOUT(!(f.native.styleMask & NSWindowStyleMaskResizable), 1000);
        QVERIFY(!f.native.movable);
        QTRY_COMPARE_WITH_TIMEOUT(rectangle(f.native.frame), original, 1000);
        f.window.requestActivate(); QVERIFY(QTest::qWaitForWindowActive(&f.window));
        const auto start = f.startPoint(Right);
        postMouse(f.native, NSEventTypeLeftMouseDown, start, Chord);
        postMouse(f.native, NSEventTypeLeftMouseDragged, shifted(start, 20, 0), Chord);
        QCOMPARE(rectangle(f.native.frame), QRectF(original.x(), original.y(), original.width() + 20, original.height()));
        f.finish(shifted(start, 20, 0)); QVERIFY(!f.suspended());
    }
    void queuedFullscreenRestoreHonorsCancellation_data() {
        QTest::addColumn<bool>("hide");
        QTest::newRow("hide") << true;
        QTest::newRow("mode-change") << false;
    }
    void queuedFullscreenRestoreHonorsCancellation() {
        QFETCH(bool, hide);
        Fixture f; QVERIFY(f.show());
        notify(NSWindowWillEnterFullScreenNotification, f.native);
        notify(NSWindowDidExitFullScreenNotification, f.native);
        // Cancel before Qt delivers the queued restoration. It must not later
        // restore the old fullscreen snapshot over the caller's newer frame.
        if (hide) f.window.hide(); else f.systemBar();
        const NSRect changed = NSMakeRect(180, 180, 900, 660);
        [f.native setFrame:changed display:YES];
        QTest::qWait(30);
        QCOMPARE(rectangle(f.native.frame), rectangle(changed));
        QVERIFY(!f.suspended());
    }
    void staleFullscreenCallbackCannotEraseNewCycleSnapshot() {
        Fixture f; QVERIFY(f.show());
        const NSRect original = f.native.frame;
        notify(NSWindowWillEnterFullScreenNotification, f.native);
        // Qt restores content dimensions before our queued outer-frame fix.
        // A new fullscreen request can arrive during this shortened state.
        NSRect firstQtRestore = original; firstQtRestore.size.height -= 32;
        notify(NSWindowDidExitFullScreenNotification, f.native);
        [f.native setFrame:firstQtRestore display:YES];
        QCOMPARE(rectangle(f.native.frame), rectangle(firstQtRestore));
        // Start the second cycle before draining the first exit callback. It
        // must preserve the pending original, not capture the shortened frame;
        // the old callback must not clear the newer cycle's restore state.
        notify(NSWindowWillEnterFullScreenNotification, f.native);
        QTest::qWait(30);
        notify(NSWindowWillExitFullScreenNotification, f.native);
        NSRect secondQtRestore = firstQtRestore; secondQtRestore.size.height -= 32;
        notify(NSWindowDidExitFullScreenNotification, f.native);
        [f.native setFrame:secondQtRestore display:YES];
        QTest::qWait(30);
        QCOMPARE(rectangle(f.native.frame), rectangle(original));
        QVERIFY(!(f.native.styleMask & NSWindowStyleMaskResizable));
        QVERIFY(!f.native.movable); QVERIFY(!f.suspended());
    }
};
QTEST_MAIN(MacWindowDragTest)
#include "macos-window-drag.moc"
