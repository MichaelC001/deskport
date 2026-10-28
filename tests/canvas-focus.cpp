#include "gui/sessioncanvas.h"
#include "gui/canvaspointer.h"
#include <QGuiApplication>
#include <QCursor>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QQuickWindow>
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
bool SessionPresentation::hasFrames() const { return false; }
void SessionPresentation::attach(QWindow*) {}
void SessionPresentation::detach() {}
void SessionPresentation::resized(bool) {}
void SessionPresentation::preedit(const QString&) {}
static int pointerMoveReleases = 0;
static bool pointerEnabled = false;
class TestCanvasPointer final : public CanvasPointer {
public:
    using CanvasPointer::CanvasPointer;
    void update(QWindow*, bool enabled) override { pointerEnabled = enabled; }
    void releaseForWindowMove() override { ++pointerMoveReleases; pointerEnabled = false; }
};
std::unique_ptr<CanvasPointer> createCanvasPointer(CanvasPointer::Motion motion) {
    return std::make_unique<TestCanvasPointer>(std::move(motion));
}

class CanvasFocusTest : public QObject {
    Q_OBJECT
private slots:
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
