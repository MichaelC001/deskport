#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QTest>
#include <QWindow>
#include "backend/shortcutrecorderinhibitor.h"

class FakeRecorderInhibition final : public ShortcutRecorderInhibitionBackend {
public:
    bool start(QWindow* window) override {
        ++starts;
        target = window;
        active = succeeds;
        return active;
    }
    void stop() override { active = false; target = nullptr; }
    bool succeeds = true;
    bool active = false;
    int starts = 0;
    QWindow* target = nullptr;
};

class ShortcutRecorderInhibitorTest : public QObject {
    Q_OBJECT
private slots:
    void onlyTheVisibleActiveRecorderIsInhibited() {
        auto backend = std::make_unique<FakeRecorderInhibition>();
        auto* fake = backend.get();
        ShortcutRecorderInhibitor recorder(std::move(backend));
        QWindow window;
        recorder.setWindow(&window);
        recorder.setEnabled(true);
        QVERIFY(!fake->active);
        window.show();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QTRY_VERIFY(fake->active);
        QCOMPARE(fake->target, &window);
        const auto starts = fake->starts;
        recorder.setEnabled(true);
        QCOMPARE(fake->starts, starts); // Never send duplicate Wayland requests.

        QWindow other;
        other.show();
        other.requestActivate();
        QTRY_VERIFY(other.isActive());
        QTRY_VERIFY(!fake->active);
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QTRY_VERIFY(fake->active);

        window.hide();
        QVERIFY(!fake->active);
        QVERIFY(!recorder.isInhibited());
    }

    void closingRecordingReleasesImmediately() {
        auto backend = std::make_unique<FakeRecorderInhibition>();
        auto* fake = backend.get();
        ShortcutRecorderInhibitor recorder(std::move(backend));
        QWindow window;
        window.show();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        recorder.setWindow(&window);
        recorder.setEnabled(true);
        QVERIFY(fake->active);
        recorder.setEnabled(false);
        QVERIFY(!fake->active);
        recorder.setEnabled(true);
        QVERIFY(fake->active);
        recorder.setWindow(nullptr);
        QVERIFY(!fake->active);
    }

    void surfaceAndWindowDestructionReleaseTheRequest() {
        auto backend = std::make_unique<FakeRecorderInhibition>();
        auto* fake = backend.get();
        ShortcutRecorderInhibitor recorder(std::move(backend));
        auto window = std::make_unique<QWindow>();
        window->show();
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        recorder.setWindow(window.get());
        recorder.setEnabled(true);
        QVERIFY(fake->active);
        QPlatformSurfaceEvent destroying(QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed);
        QCoreApplication::sendEvent(window.get(), &destroying);
        QVERIFY(!fake->active);
        window.reset();
        QVERIFY(!fake->active);
        QCoreApplication::processEvents();
        QVERIFY(!recorder.isInhibited());
    }

    void rejectedRequestCanBeRetried() {
        auto backend = std::make_unique<FakeRecorderInhibition>();
        auto* fake = backend.get();
        fake->succeeds = false;
        ShortcutRecorderInhibitor recorder(std::move(backend));
        QWindow window;
        window.show();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        recorder.setWindow(&window);
        recorder.setEnabled(true);
        QVERIFY(!recorder.isInhibited());
        fake->succeeds = true;
        recorder.setEnabled(true);
        QVERIFY(recorder.isInhibited());
        QVERIFY(fake->active);
    }

    void nativeAdapterDoesNotConnectOnOffscreen() {
        QVERIFY(!QGuiApplication::platformName().startsWith(QStringLiteral("wayland")));
        auto backend = createShortcutRecorderInhibitionBackend();
        QWindow window;
        window.show();
        QVERIFY(!backend->start(&window));
        backend->stop();
        backend->stop();
    }
};

QTEST_MAIN(ShortcutRecorderInhibitorTest)
#include "shortcut-recorder-inhibitor.moc"
