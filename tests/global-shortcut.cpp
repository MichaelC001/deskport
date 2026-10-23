#include "backend/globalshortcut.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

namespace {
int created = 0;
int stopped = 0;
bool synchronousRegistration = true;

class FakeBackend final : public GlobalShortcutBackend {
public:
    using GlobalShortcutBackend::GlobalShortcutBackend;
    bool start() override { ++starts; return synchronousRegistration; }
    void stop() override { ++stopped; }
    int starts = 0;
};
FakeBackend* backend = nullptr;
}

GlobalShortcutBackend* createGlobalShortcutBackend(QObject* parent) {
    ++created;
    backend = new FakeBackend(parent);
    return backend;
}

class GlobalShortcutTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        created = stopped = 0;
        backend = nullptr;
        synchronousRegistration = true;
        qunsetenv("DESKPORT_DISABLE_GLOBAL_SHORTCUT");
    }

    void nativeReleaseActivatesOnceAfterLongHold() {
        ShortcutActivationGate gate;
        QVERIFY(!gate.nativeEdge(false, 0));
        QVERIFY(!gate.nativeEdge(true, 1));
        QVERIFY(!gate.nativeEdge(true, 500));
        QVERIFY(!gate.nativeEdge(true, 5000));
        QVERIFY(gate.nativeEdge(false, 9000));
        QVERIFY(!gate.nativeEdge(false, 9001));
    }

    void rapidNativePressesAreNotRateLimited() {
        ShortcutActivationGate gate;
        for (qint64 now = 0; now < 20; now += 2) {
            QVERIFY(!gate.nativeEdge(true, now));
            QVERIFY(gate.nativeEdge(false, now + 1));
        }
    }

    void rapidFallbackPressesAreNotRateLimited() {
        ShortcutActivationGate gate;
        for (qint64 now = 0; now < 20; ++now) QVERIFY(gate.fallback(now));
    }

    void nativeThenFallbackActivatesOnce() {
        ShortcutActivationGate gate;
        QVERIFY(!gate.nativeEdge(true, 1));
        QVERIFY(gate.nativeEdge(false, 2));
        QVERIFY(!gate.fallback(3));
        QVERIFY(gate.fallback(1000));
    }

    void fallbackDuringNativeHoldActivatesOnce() {
        ShortcutActivationGate gate;
        QVERIFY(!gate.nativeEdge(true, 1));
        QVERIFY(!gate.fallback(8000));
        QVERIFY(gate.nativeEdge(false, 8001));
        QVERIFY(!gate.nativeEdge(false, 8002));
        QVERIFY(gate.fallback(8020)); // The preceding fallback was already paired.
    }

    void fallbackBeforeNativeDoesNotEatNextPress() {
        ShortcutActivationGate gate;
        QVERIFY(gate.fallback(10));
        QVERIFY(!gate.nativeEdge(true, 11));
        QVERIFY(!gate.nativeEdge(false, 12));
        QVERIFY(!gate.nativeEdge(true, 20));
        QVERIFY(gate.nativeEdge(false, 21));
    }

    void nativeBeforeFallbackDoesNotEatNextPress() {
        ShortcutActivationGate gate;
        QVERIFY(!gate.nativeEdge(true, 10));
        QVERIFY(gate.nativeEdge(false, 11));
        QVERIFY(!gate.fallback(12));
        QVERIFY(gate.fallback(20));
    }

    void resetDiscardsHeldKeysAndDeduplicationState() {
        ShortcutActivationGate gate;
        QVERIFY(!gate.nativeEdge(true, 1));
        gate.reset();
        QVERIFY(!gate.nativeEdge(false, 2));
        QVERIFY(gate.fallback(3));
        gate.reset();
        QVERIFY(!gate.nativeEdge(true, 4));
        QVERIFY(gate.nativeEdge(false, 5));
    }

    void registrationAndLifetimeAreOwnedOnce() {
        {
            GlobalShortcut shortcut;
            QSignalSpy availability(&shortcut, &GlobalShortcut::availabilityChanged);
            QVERIFY(!shortcut.available());
            shortcut.start();
            QVERIFY(shortcut.available());
            QCOMPARE(created, 1);
            QCOMPARE(backend->starts, 1);
            QCOMPARE(availability.count(), 1);
            shortcut.start();
            QCOMPARE(created, 1);
            QCOMPARE(backend->starts, 1);
            backend->availability(true);
            QCOMPARE(availability.count(), 1);
        }
        QCOMPARE(stopped, 1);
    }

    void asynchronousRegistrationGatesBothInputPaths() {
        synchronousRegistration = false;
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        QSignalSpy availability(&shortcut, &GlobalShortcut::availabilityChanged);
        shortcut.start();
        QVERIFY(!shortcut.available());
        shortcut.fallbackActivated();
        backend->edge(true, "not-registered");
        backend->edge(false, {});
        QCOMPARE(activated.count(), 0);
        backend->availability(true);
        QVERIFY(shortcut.available());
        QCOMPARE(availability.count(), 1);
        backend->edge(true, "activation-token");
        backend->edge(false, {});
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.at(0).at(0).toByteArray(), QByteArray("activation-token"));
    }

    void repeatedDownDoesNotOverwriteActivationToken() {
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        shortcut.start();
        backend->edge(true, "initial-token");
        backend->edge(true, {});
        QCOMPARE(activated.count(), 0);
        backend->edge(false, {});
        backend->edge(false, {});
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.at(0).at(0).toByteArray(), QByteArray("initial-token"));
    }

    void suppressedNativePressDoesNotLeakItsToken() {
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        shortcut.start();
        shortcut.fallbackActivated();
        backend->edge(true, "already-handled-token");
        backend->edge(false, {});
        QCOMPARE(activated.count(), 1);
        backend->edge(true, {});
        backend->edge(false, {});
        QCOMPARE(activated.count(), 2);
        QVERIFY(activated.at(1).at(0).toByteArray().isEmpty());
    }

    void compositorRebindingDisablesOnlyTheFixedFallback() {
        synchronousRegistration = false;
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        QSignalSpy fallback(&shortcut, &GlobalShortcut::fallbackAvailabilityChanged);
        shortcut.start();
        backend->fixedChord = false;
        backend->availability(true);
        QVERIFY(shortcut.available());
        QCOMPARE(fallback.count(), 1);
        QVERIFY(!fallback.last().at(0).toBool());
        shortcut.fallbackActivated();
        QCOMPARE(activated.count(), 0);
        backend->edge(true, "rebound-token");
        backend->edge(false, {});
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.at(0).at(0).toByteArray(), QByteArray("rebound-token"));
        backend->fixedChord = true;
        backend->availability(true);
        QCOMPARE(fallback.count(), 2);
        QVERIFY(fallback.last().at(0).toBool());
        backend->availability(false);
        QVERIFY(!fallback.last().at(0).toBool());
    }

    void registrationLossDiscardsPendingPressAndToken() {
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        shortcut.start();
        backend->edge(true, "old-token");
        backend->availability(false);
        QVERIFY(!shortcut.available());
        backend->edge(false, {});
        shortcut.fallbackActivated();
        QCOMPARE(activated.count(), 0);
        backend->availability(true);
        backend->edge(false, {});
        QCOMPARE(activated.count(), 0);
        backend->edge(true, {});
        backend->edge(false, {});
        QCOMPARE(activated.count(), 1);
        QVERIFY(activated.at(0).at(0).toByteArray().isEmpty());
    }

    void backendErrorDisablesNativeAndFallbackActivation() {
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        shortcut.start();
        backend->edge(true, "old-token");
        QTest::ignoreMessage(QtWarningMsg,
            "DeskPort global shortcut Meta+Apostrophe unavailable: fixture registration failure");
        backend->error(QStringLiteral("fixture registration failure"));
        QVERIFY(!shortcut.available());
        backend->edge(false, {});
        shortcut.fallbackActivated();
        QCOMPARE(activated.count(), 0);
    }

    void disableOverridePreventsBackendCreation() {
        qputenv("DESKPORT_DISABLE_GLOBAL_SHORTCUT", "1");
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        shortcut.start();
        shortcut.fallbackActivated();
        QVERIFY(!shortcut.available());
        QCOMPARE(created, 0);
        QCOMPARE(activated.count(), 0);
    }

    void disabledPreferenceSkipsRegistrationUntilEnabled() {
        GlobalShortcut shortcut;
        shortcut.setEnabled(false);
        shortcut.start();
        shortcut.fallbackActivated();
        QCOMPARE(created, 0);
        QVERIFY(!shortcut.available());
        shortcut.setEnabled(true);
        QVERIFY(shortcut.available());
        QCOMPARE(created, 1);
        shortcut.setEnabled(true);
        QCOMPARE(created, 1);
    }

    void disablingReleasesBindingAndIgnoresLateCallbacks() {
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        QSignalSpy fallback(&shortcut, &GlobalShortcut::fallbackAvailabilityChanged);
        shortcut.start();
        auto* oldBackend = backend;
        oldBackend->edge(true, "discarded-token");
        shortcut.setEnabled(false);
        QCOMPARE(stopped, 1);
        QVERIFY(!shortcut.available());
        QVERIFY(!fallback.last().at(0).toBool());
        oldBackend->availability(true);
        oldBackend->edge(false, {});
        shortcut.fallbackActivated();
        QVERIFY(!shortcut.available());
        QCOMPARE(activated.count(), 0);
        shortcut.setEnabled(false);
        QCOMPARE(stopped, 1);
        shortcut.setEnabled(true);
        QCOMPARE(created, 2);
        QVERIFY(shortcut.available());
        oldBackend->availability(false);
        oldBackend->error("stale registration error");
        QVERIFY(shortcut.available());
        backend->edge(false, {});
        QCOMPARE(activated.count(), 0);
        backend->edge(true, {});
        backend->edge(false, {});
        QCOMPARE(activated.count(), 1);
        QVERIFY(activated.last().at(0).toByteArray().isEmpty());
    }

    void disablingCancelsPendingPortalRegistration() {
        synchronousRegistration = false;
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::activated);
        shortcut.start();
        auto* oldBackend = backend;
        shortcut.setEnabled(false);
        oldBackend->availability(true);
        QVERIFY(!shortcut.available());
        shortcut.setEnabled(true);
        QCOMPARE(created, 2);
        QVERIFY(!shortcut.available());
        oldBackend->availability(true);
        QVERIFY(!shortcut.available());
        backend->availability(true);
        QVERIFY(shortcut.available());
        shortcut.fallbackActivated();
        QCOMPARE(activated.count(), 1);
    }
};

int main(int argc, char** argv) {
    if (argc > 1 && QByteArray(argv[1]) == "--isolated-gui") {
        // This process contains only the fake factory. Even a regression in the
        // platform guard cannot register an actual desktop shortcut.
        QGuiApplication app(argc, argv);
        GlobalShortcut shortcut;
        shortcut.start();
        shortcut.fallbackActivated();
        return created == 0 && !shortcut.available() ? 0 : 1;
    }
    QCoreApplication app(argc, argv);
    GlobalShortcutTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "global-shortcut.moc"
