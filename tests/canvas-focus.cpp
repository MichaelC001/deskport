#include "gui/sessioncanvas.h"
#include "gui/canvaspointer.h"
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <SDL.h>

// Exercise the production Qt item and event routing without a renderer, host,
// user settings, compositor shortcut registration or native input injection.
struct SessionPresentation::Impl {};
SessionPresentation::SessionPresentation(QObject* p) : QObject(p) {}
SessionPresentation::~SessionPresentation() = default;
bool SessionPresentation::ready() const { return false; }
bool SessionPresentation::active() const { return false; }
bool SessionPresentation::hasFrames() const { return false; }
void SessionPresentation::attach(QWindow*) {}
void SessionPresentation::detach() {}
void SessionPresentation::resized(bool) {}
void SessionPresentation::preedit(const QString&) {}
std::unique_ptr<CanvasPointer> createCanvasPointer(CanvasPointer::Motion) { return {}; }

class CanvasFocusTest : public QObject {
    Q_OBJECT
private slots:
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
