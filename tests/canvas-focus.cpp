#include "gui/sessioncanvas.h"
#include "gui/canvaspointer.h"
#include "streaming/canvasbridge.h"
#include <QGuiApplication>
#include <QCursor>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QQuickWindow>
#include <QTouchEvent>
#include <QPointingDevice>
#include <QtGui/private/qeventpoint_p.h>
#include <QSignalSpy>
#include <QTest>
#include <SDL.h>
#include <initializer_list>

// Exercise the production Qt item and event routing without a renderer, host,
// user settings, compositor shortcut registration or native input injection.
struct SessionPresentation::Impl {};
SessionPresentation::SessionPresentation(QObject* p) : QObject(p) {}
SessionPresentation::~SessionPresentation() = default;
bool SessionPresentation::ready() const { return property("readyForTest").toBool(); }
bool SessionPresentation::active() const { return false; }
bool SessionPresentation::hasFrames() const { return property("framesForTest").toBool(); }
QString SessionPresentation::backdrop() const { return {}; }
QImage SessionPresentation::backdropImage() const { return {}; }
void SessionPresentation::attach(QWindow*) {}
void SessionPresentation::detach() {}
void SessionPresentation::resized(bool) {}
void SessionPresentation::preedit(const QString&) {}
static int pointerMoveReleases = 0;
static bool pointerEnabled = false;
static QRect pointerConfined;
class TestCanvasPointer final : public CanvasPointer {
public:
    using CanvasPointer::CanvasPointer;
    void update(QWindow*, bool enabled) override { pointerEnabled = enabled; }
    void confine(QWindow*, const QRect& region, bool enabled) override { pointerConfined = enabled ? region : QRect(); }
    void releaseForWindowMove() override { ++pointerMoveReleases; pointerEnabled = false; }
};
std::unique_ptr<CanvasPointer> createCanvasPointer(CanvasPointer::Motion motion) {
    return std::make_unique<TestCanvasPointer>(std::move(motion));
}

class CanvasFocusTest : public QObject {
    Q_OBJECT
private slots:
#ifdef Q_OS_LINUX
    void transitionCoverWaitsForTheFirstSubmittedFrame() {
        QQuickWindow window;
        SessionPresentation presentation;
        SessionCanvas canvas(window.contentItem());
        canvas.setProperty("transitionLabel", "Connecting to desktop…");
        canvas.setProperty("transitionCancelLabel", "Cancel connection");
        canvas.setSize({640, 480}); canvas.setPresentation(&presentation);
        window.show(); window.requestActivate(); canvas.forceActiveFocus();
        auto surface = window.findChild<QWindow*>("canvasVideoSurface");
        auto cover = window.findChild<QWindow*>("canvasTransitionCover");
        QVERIFY(surface); QVERIFY(cover);
        QVERIFY(cover->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        presentation.setProperty("framesForTest", true);
        QVERIFY(QMetaObject::invokeMethod(&presentation, "activeChanged"));
        QTRY_VERIFY(cover->isVisible());
        QTRY_VERIFY(surface->isVisible());
        QCOMPARE(cover->geometry(), surface->geometry());
        canvas.setSize({500, 360});
        QTRY_COMPARE(cover->size(), QSize(500, 360));
        QCOMPARE(cover->geometry(), surface->geometry());
        QSignalSpy cancel(&canvas, &SessionCanvas::transitionCancelRequested);
        QTest::mouseClick(cover, Qt::LeftButton, Qt::NoModifier, QPoint(250, 220));
        QCOMPARE(cancel.size(), 1);
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(cancel.size(), 2);
        canvas.setSize({0, 0});
        QTRY_VERIFY(!surface->isVisible() && !cover->isVisible());
        canvas.setSize({500, 360});
        QTRY_VERIFY(surface->isVisible() && cover->isVisible());
        canvas.setVisible(false);
        QTRY_VERIFY(!surface->isVisible() && !cover->isVisible());
        canvas.setVisible(true);
        QTRY_VERIFY(surface->isVisible() && cover->isVisible());
        // The cover remains until the new decoder reports its first submission.
        presentation.setProperty("readyForTest", true);
        QVERIFY(QMetaObject::invokeMethod(&presentation, "readyChanged"));
        QTRY_VERIFY(!cover->isVisible());
        QVERIFY(surface->isVisible());
        // Waiting hides both native children, exposing the QML loading page.
        presentation.setProperty("readyForTest", false);
        canvas.setProperty("transitionWaiting", true);
        QVERIFY(QMetaObject::invokeMethod(&presentation, "readyChanged"));
        QTRY_VERIFY(!surface->isVisible() && !cover->isVisible());
        canvas.setProperty("transitionWaiting", false);
        QTRY_VERIFY(surface->isVisible() && cover->isVisible());
        canvas.setPresentation(nullptr);
        QTRY_VERIFY(!surface->isVisible() && !cover->isVisible());
    }
#endif
    void zeroSizeAndWaitingReleaseWorkerInput() {
        SDL_SetMainReady();
        QVERIFY(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
        SDL_Window* proxy = SDL_CreateWindow("canvas input fixture", 0, 0, 640, 480, SDL_WINDOW_HIDDEN);
        QVERIFY(proxy);
        QQuickWindow window;
        SessionPresentation presentation;
        presentation.setProperty("readyForTest", true);
        SessionCanvas canvas(window.contentItem());
        canvas.setSize({640, 480}); canvas.setPresentation(&presentation);
        QSignalSpy viewport(&canvas, &SessionCanvas::viewportChanged);
        QObject::connect(&canvas, &SessionCanvas::viewportChanged, &canvas, [](const QJsonObject& value) { CanvasBridge::state(value); });
        window.show(); window.requestActivate(); canvas.forceActiveFocus();
        QTRY_VERIFY(window.isActive() && canvas.hasActiveFocus());
        QTRY_VERIFY(!viewport.isEmpty() && viewport.last()[0].toJsonObject()["focused"].toBool());
        CanvasBridge::configure(viewport.last()[0].toJsonObject());
        CanvasBridge::requested(true); CanvasBridge::inputEnabled(true); CanvasBridge::registerWindow(proxy);
        SDL_Event event; while (SDL_PollEvent(&event)) {}
        auto surface = window.findChild<QWindow*>("canvasVideoSurface"); QVERIFY(surface);
        QMouseEvent down(QEvent::MouseButtonPress, QPointF(50, 50), QPointF(50, 50),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(surface, &down);
        QCOMPARE(viewport.last()[0].toJsonObject()["buttons"].toInt(), 1);
        canvas.setProperty("transitionWaiting", true);
        QVERIFY(!viewport.last()[0].toJsonObject()["focused"].toBool());
        QCOMPARE(viewport.last()[0].toJsonObject()["buttons"].toInt(), 0);
        CanvasBridge::synchronize(proxy);
        bool focusLost = false;
        while (SDL_PollEvent(&event)) if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) focusLost = true;
        QVERIFY(focusLost);
        canvas.setProperty("transitionWaiting", false);
        QTRY_VERIFY(viewport.last()[0].toJsonObject()["focused"].toBool());
        QCOMPARE(viewport.last()[0].toJsonObject()["buttons"].toInt(), 0);
        canvas.setSize({0, 0});
        QTRY_VERIFY(!viewport.last()[0].toJsonObject()["visible"].toBool());
        QCOMPARE(viewport.last()[0].toJsonObject()["width"].toInt(), 640);
        QCOMPARE(viewport.last()[0].toJsonObject()["height"].toInt(), 480);
        CanvasBridge::synchronize(proxy);
        QVERIFY(!(CanvasBridge::flags(proxy) & SDL_WINDOW_INPUT_FOCUS));
        focusLost = false;
        while (SDL_PollEvent(&event)) if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) focusLost = true;
        QVERIFY(focusLost);
        CanvasBridge::requested(false); SDL_DestroyWindow(proxy); SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    void touchAndAbsolutePointerRouteThroughCanvas() {
        QQuickWindow window;
        SessionPresentation presentation;
        presentation.setProperty("readyForTest", true);
        SessionCanvas canvas(window.contentItem());
        canvas.setSize({640, 480}); canvas.setPresentation(&presentation);
        window.show(); window.requestActivate(); canvas.forceActiveFocus();
        QTRY_VERIFY(window.isActive() && canvas.hasActiveFocus());
        auto surface = window.findChild<QWindow*>();
        QVERIFY(surface);
        QSignalSpy input(&canvas, &SessionCanvas::input);
        QPointingDevice touchscreen("fixture", 1, QInputDevice::DeviceType::TouchScreen,
            QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 5, 0);
        // The public QEventPoint constructor sets scene/global coordinates but
        // leaves the receiver-local position at zero. Platform delivery fills it.
        auto point = QMutableEventPoint::withTimeStamp(1, 7, QEventPoint::State::Pressed,
                                                       QPointF(160, 240), QPointF(160, 240), QPointF(160, 240));
        QTouchEvent down(QEvent::TouchBegin, &touchscreen, Qt::NoModifier, {point});
        QCoreApplication::sendEvent(surface, &down);
        QVERIFY(down.isAccepted());
        QCOMPARE(input.size(), 1);
        auto touch = input.first().first().toJsonObject();
        QCOMPARE(touch.value("type").toString(), QStringLiteral("touch"));
        QCOMPARE(touch.value("phase").toString(), QStringLiteral("down"));
        QCOMPARE(touch.value("id").toInt(), 0);
        QCOMPARE(touch.value("x").toDouble(), 0.25);
        QCOMPARE(touch.value("y").toDouble(), 0.5);
        // Ever-increasing platform IDs map to small stable slots.
        auto second = QMutableEventPoint::withTimeStamp(2, 700000, QEventPoint::State::Pressed,
                                                        QPointF(320, 240), QPointF(320, 240), QPointF(320, 240));
        auto held = QMutableEventPoint::withTimeStamp(2, 7, QEventPoint::State::Stationary,
                                                      QPointF(160, 240), QPointF(160, 240), QPointF(160, 240));
        QTouchEvent another(QEvent::TouchUpdate, &touchscreen, Qt::NoModifier, {held, second});
        QCoreApplication::sendEvent(surface, &another);
        QCOMPARE(input.size(), 2);
        QCOMPARE(input.last().first().toJsonObject().value("id").toInt(), 1);
        QTouchEvent cancel(QEvent::TouchCancel, &touchscreen);
        QCoreApplication::sendEvent(surface, &cancel);
        QCOMPARE(input.last().first().toJsonObject().value("type").toString(), QStringLiteral("touch-cancel"));
        // A contact that starts before the canvas is focused is held until the
        // focused viewport has been published, rather than losing its down.
        presentation.setProperty("readyForTest", false);
        QTest::qWait(35);
        input.clear();
        auto early = QMutableEventPoint::withTimeStamp(3, 9, QEventPoint::State::Pressed,
                                                       QPointF(160, 240), QPointF(160, 240), QPointF(160, 240));
        QTouchEvent earlyDown(QEvent::TouchBegin, &touchscreen, Qt::NoModifier, {early});
        QCoreApplication::sendEvent(surface, &earlyDown);
        QCOMPARE(input.size(), 0);
        presentation.setProperty("readyForTest", true);
        QTRY_COMPARE(input.size(), 1);
        QCOMPARE(input.first().first().toJsonObject().value("phase").toString(), QStringLiteral("down"));
        QTouchEvent earlyCancel(QEvent::TouchCancel, &touchscreen);
        QCoreApplication::sendEvent(surface, &earlyCancel);
        canvas.setProperty("pointerRegion", QJsonObject{{"x", 20}, {"y", 30}, {"width", 400}, {"height", 300}});
        QCOMPARE(pointerConfined, QRect(20, 30, 400, 300));
        canvas.setProperty("pointerRegion", QJsonObject{});
    }
    void localWindowMoveReleasesFocusAndSuppressesInput() {
        QQuickWindow window;
        SessionPresentation presentation;
        presentation.setProperty("readyForTest", true);
        SessionCanvas canvas(window.contentItem());
        canvas.setSize({640, 480});
        canvas.setPresentation(&presentation);
        canvas.setProperty("relativePointer", true);
        canvas.setProperty("textInputEnabled", true);
        window.show(); window.requestActivate(); canvas.forceActiveFocus();
        QTRY_VERIFY(window.isActive() && canvas.hasActiveFocus());
        auto surface = window.findChild<QWindow*>();
        QVERIFY(surface);
        QSignalSpy viewport(&canvas, &SessionCanvas::viewportChanged);
        QSignalSpy input(&canvas, &SessionCanvas::input);
        QMouseEvent down(QEvent::MouseButtonPress, QPointF(30, 40), QPointF(30, 40),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&canvas, &down);
        QVERIFY(!viewport.isEmpty());
        QCOMPARE(viewport.last()[0].toJsonObject()["buttons"].toInt(), int(SDL_BUTTON_LMASK));
        QVERIFY(pointerEnabled);
        const int releases = pointerMoveReleases;
        window.setProperty("deskportLocalWindowDrag", true);
        // This boundary is synchronous even when AppKit's nested event loop
        // does not run a Qt timer or consumes the modifier key releases.
        QCOMPARE(pointerMoveReleases, releases + 1);
        QVERIFY(!pointerEnabled);
        QVERIFY(!viewport.last()[0].toJsonObject()["focused"].toBool());
        QCOMPARE(viewport.last()[0].toJsonObject()["buttons"].toInt(), 0);
        window.setProperty("deskportLocalWindowCursor", int(Qt::SizeBDiagCursor));
        QCOMPARE(surface->cursor().shape(), Qt::SizeBDiagCursor);
        QTest::qWait(35); // The regular canvas refresh must preserve the local resize cursor.
        QCOMPARE(surface->cursor().shape(), Qt::SizeBDiagCursor);
        input.clear();
        for (QObject* target : {static_cast<QObject*>(&canvas), static_cast<QObject*>(surface)}) {
            QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
            QMouseEvent motion(QEvent::MouseMove, QPointF(50, 60), QPointF(50, 60),
                               Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, QPointF(50, 60), QPointF(50, 60),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QWheelEvent wheel(QPointF(50, 60), QPointF(50, 60), QPoint(), QPoint(0, 120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QInputMethodEvent text;
            text.setCommitString("local gesture");
            for (QEvent* event : std::initializer_list<QEvent*>{&key, &motion, &up, &wheel, &text})
                QCoreApplication::sendEvent(target, event);
        }
        QCOMPARE(input.size(), 0);
        QInputMethodQueryEvent query(Qt::ImEnabled);
        QCoreApplication::sendEvent(surface, &query);
        QVERIFY(!query.value(Qt::ImEnabled).toBool());
        window.setProperty("deskportLocalWindowDrag", false);
        QCOMPARE(surface->cursor().shape(), Qt::ArrowCursor);
        QVERIFY(viewport.last()[0].toJsonObject()["focused"].toBool());
        QCOMPARE(viewport.last()[0].toJsonObject()["buttons"].toInt(), 0);
        QVERIFY(pointerEnabled);
        QKeyEvent normal(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
        QCoreApplication::sendEvent(&canvas, &normal);
        QCOMPARE(input.size(), 2); // Physical key plus local text commit.
    }
    void remoteModeDisablesImeOnBothFocusPaths() {
        QQuickWindow window;
        SessionCanvas canvas(window.contentItem());
        canvas.setSize({640, 480});
        window.show(); window.requestActivate(); canvas.forceActiveFocus();
        QTRY_VERIFY(canvas.hasActiveFocus());
        QCOMPARE(window.activeFocusItem(), &canvas);
        auto surface = window.findChild<QWindow*>();
        QVERIFY(surface);
        for (bool enabled : {false, true, false, true, false}) {
            QVERIFY(canvas.setProperty("textInputEnabled", enabled));
            QCOMPARE(bool(canvas.flags() & QQuickItem::ItemAcceptsInputMethod), enabled);
            for (QObject* target : {static_cast<QObject*>(&window), static_cast<QObject*>(surface)}) {
                QInputMethodQueryEvent query(Qt::ImEnabled);
                QCoreApplication::sendEvent(target, &query);
                QCOMPARE(query.value(Qt::ImEnabled).toBool(), enabled);
            }
        }
    }
    void compositionCommitsOnlyInLocalMode() {
        QQuickWindow window;
        SessionCanvas canvas(window.contentItem());
        canvas.setSize({640, 480}); canvas.forceActiveFocus();
        auto surface = window.findChild<QWindow*>();
        QVERIFY(surface);
        QSignalSpy input(&canvas, &SessionCanvas::input);
        for (bool enabled : {false, true, false}) {
            canvas.setProperty("textInputEnabled", enabled);
            for (QObject* target : {static_cast<QObject*>(&canvas), static_cast<QObject*>(surface)}) {
                input.clear();
                QInputMethodEvent commit;
                commit.setCommitString(QString::fromUtf8("測試"));
                QCoreApplication::sendEvent(target, &commit);
                QCOMPARE(input.size(), enabled ? 1 : 0);
                if (enabled) QCOMPARE(input.first().first().toJsonObject().value("text").toString(), QString::fromUtf8("測試"));
            }
        }
    }
    void metaSpaceRemainsAPhysicalChord() {
        QQuickWindow window;
        SessionCanvas canvas(window.contentItem());
        QSignalSpy input(&canvas, &SessionCanvas::input);
#ifdef Q_OS_MACOS
        const auto modifier = Qt::ControlModifier;
#else
        const auto modifier = Qt::MetaModifier;
#endif
        for (bool local : {false, true}) {
            canvas.setProperty("textInputEnabled", local);
            input.clear();
#ifdef Q_OS_MACOS
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, modifier, 0, 49, 0, " ");
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, modifier, 0, 49, 0);
#else
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, modifier, " ");
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, modifier);
#endif
            QCoreApplication::sendEvent(&canvas, &press);
            QCoreApplication::sendEvent(&canvas, &release);
            QCOMPARE(input.size(), 2);
            for (int i = 0; i < 2; ++i) {
                auto value = input[i][0].toJsonObject();
                QCOMPARE(value.value("type").toString(), QString("key"));
                QCOMPARE(value.value("scan").toInt(), int(SDL_SCANCODE_SPACE));
                QVERIFY(value.value("modifiers").toInt() & KMOD_GUI);
                QCOMPARE(value.value("down").toBool(), i == 0);
            }
        }
    }
};
QTEST_MAIN(CanvasFocusTest)
#include "canvas-focus.moc"
