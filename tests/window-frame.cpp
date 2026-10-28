#include <QGuiApplication>
#include <QQuickWindow>
#include <QtTest>
#include "windowframe.h"

class WindowFrameTest : public QObject {
    Q_OBJECT
private slots:
    void repeatedChangesKeepClientGeometry_data() {
        QTest::addColumn<bool>("initialFrameless");
        QTest::newRow("initially-framed") << false;
        QTest::newRow("initially-frameless") << true;
    }
    void repeatedChangesKeepClientGeometry() {
        QFETCH(bool, initialFrameless);
        QQuickWindow window;
        window.setColor(QColor("#304858"));
        window.resize(700, 500);
        window.setFlag(Qt::FramelessWindowHint, initialFrameless);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(200);
        const auto initialSize = window.size();
        for (int i = 0; i < 10; ++i) {
            const bool frameless = i % 2 == 0 ? !initialFrameless : initialFrameless;
            window.setTitle("DeskPort frame fixture changing");
            WindowFrame::setFrameless(&window, frameless);
            QTest::qWait(250);
            QCOMPARE(window.size(), initialSize);
            QVERIFY(window.isVisible());
            QCOMPARE(window.flags().testFlag(Qt::FramelessWindowHint), frameless);
            // A private compositor fixture can sample these settled stages and
            // compare actual server frame/client geometry, beyond Qt flags.
            window.setTitle(QString("DeskPort frame fixture %1 %2").arg(frameless ? "frameless" : "framed").arg(i));
            qInfo("WINDOW_FRAME_STAGE frameless=%d width=%d height=%d", frameless, window.width(), window.height());
            QTest::qWait(250);
        }
        window.hide();
    }
    void preservesVisibilityAndRestoreGeometry() {
        QQuickWindow window;
        window.resize(700, 500);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(200);
        const auto initialSize = window.size();
        for (auto visibility : {QWindow::Maximized, QWindow::FullScreen, QWindow::Minimized}) {
            window.setVisibility(visibility);
            QTest::qWait(200);
            WindowFrame::setFrameless(&window, !window.flags().testFlag(Qt::FramelessWindowHint));
            QTest::qWait(200);
            QCOMPARE(window.visibility(), visibility);
            window.showNormal();
            QTest::qWait(200);
            QCOMPARE(window.size(), initialSize);
        }
        window.hide();
        WindowFrame::setFrameless(&window, !window.flags().testFlag(Qt::FramelessWindowHint));
        QTest::qWait(200);
        QVERIFY(!window.isVisible());
        QCOMPARE(window.size(), initialSize);
    }
};
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    WindowFrameTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "window-frame.moc"
