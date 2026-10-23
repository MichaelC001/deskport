#include "backend/globalshortcut.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVirtualObject>
#include <QKeySequence>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUuid>

struct FixtureShortcut { QString id; QVariantMap options; };
using FixtureShortcuts = QList<FixtureShortcut>;
Q_DECLARE_METATYPE(FixtureShortcut)
Q_DECLARE_METATYPE(FixtureShortcuts)
static QDBusArgument& operator<<(QDBusArgument& arg, const FixtureShortcut& value) {
    arg.beginStructure(); arg << value.id << value.options; arg.endStructure(); return arg;
}
static const QDBusArgument& operator>>(const QDBusArgument& arg, FixtureShortcut& value) {
    arg.beginStructure(); arg >> value.id >> value.options; arg.endStructure(); return arg;
}

namespace {
const QString Service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString Path = QStringLiteral("/org/freedesktop/portal/desktop");
const QString Interface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString Action = QStringLiteral("toggle-window");

class Portal final : public QDBusVirtualObject {
public:
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "synthetic-shortcut-portal");
    const QString session = Path + "/session/synthetic/" + QUuid::createUuid().toString(QUuid::Id128);
    bool objectHandle = false, holdCreate = false, holdRegister = false, bindGranted = true, registryAvailable = true;
    uint createResponse = 0, bindResponse = 0;
    int registrations = 0, creates = 0, binds = 0, requestCloses = 0, sessionCloses = 0;
    QString appId, requestedSession, parentWindow, heldRequest;
    QVariantMap createOptions, bindOptions;
    FixtureShortcuts shortcuts;
    QList<QDBusMessage> registerReplies;

    Portal() {
        if (!qEnvironmentVariableIsSet("DESKPORT_TEST_PRIVATE_BUS")) qFatal("private test bus required");
        qDBusRegisterMetaType<FixtureShortcut>();
        qDBusRegisterMetaType<FixtureShortcuts>();
        if (!bus.registerService(Service) || !bus.registerVirtualObject(Path, this, QDBusConnection::SubPath))
            qFatal("synthetic shortcut portal registration failed: %s", qPrintable(bus.lastError().message()));
    }
    ~Portal() override {
        bus.unregisterObject(Path, QDBusConnection::UnregisterTree);
        bus.unregisterService(Service);
    }
    QString introspect(const QString&) const override { return {}; }
    void response(const QString& request, uint code, const QVariantMap& results) {
        auto signal = QDBusMessage::createSignal(request, "org.freedesktop.portal.Request", "Response");
        signal.setArguments({code, results});
        bus.send(signal);
    }
    void created(const QString& request) {
        response(request, createResponse, {{"session_handle", objectHandle
            ? QVariant::fromValue(QDBusObjectPath(session)) : QVariant(session)}});
    }
    FixtureShortcuts bound() const {
        return {{Action, bindGranted ? QVariantMap{{"trigger_description",
            QKeySequence(Qt::META | Qt::Key_Apostrophe).toString(QKeySequence::NativeText)}} : QVariantMap{}}};
    }
    void edge(bool pressed, QString id = Action, QString sourceSession = {}, QByteArray token = {}) {
        auto signal = QDBusMessage::createSignal(Path, Interface, pressed ? "Activated" : "Deactivated");
        signal.setArguments({QVariant::fromValue(QDBusObjectPath(sourceSession.isEmpty() ? session : sourceSession)),
                             id, QVariant::fromValue(qulonglong(100)),
                             QVariantMap{{"activation_token", QString::fromUtf8(token)}}});
        bus.send(signal);
    }
    void changed(const FixtureShortcuts& bindings) {
        auto signal = QDBusMessage::createSignal(Path, Interface, "ShortcutsChanged");
        signal.setArguments({QVariant::fromValue(QDBusObjectPath(session)), QVariant::fromValue(bindings)});
        bus.send(signal);
    }
    void closed() {
        auto signal = QDBusMessage::createSignal(session, "org.freedesktop.portal.Session", "Closed");
        signal.setArguments({QVariantMap{}});
        bus.send(signal);
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override {
        const auto args = message.arguments();
        const auto method = message.member();
        auto reply = [&](const QVariantList& values = {}) { connection.send(message.createReply(values)); };
        if (method == "Register") {
            ++registrations;
            appId = args.at(0).toString();
            if (holdRegister) registerReplies.append(message.createReply());
            else if (registryAvailable) reply();
            else connection.send(message.createErrorReply("org.freedesktop.DBus.Error.UnknownInterface", "synthetic older portal"));
        } else if (method == "CreateSession" || method == "BindShortcuts") {
            const auto options = qdbus_cast<QVariantMap>(args.last());
            auto sender = message.service().mid(1); sender.replace('.', '_');
            const auto request = Path + "/request/" + sender + '/' + options.value("handle_token").toString();
            reply({QVariant::fromValue(QDBusObjectPath(request))});
            if (method == "CreateSession") {
                ++creates;
                createOptions = options;
                heldRequest = request;
                if (!holdCreate) QTimer::singleShot(0, this, [this, request] { created(request); });
            } else {
                ++binds;
                requestedSession = qvariant_cast<QDBusObjectPath>(args.at(0)).path();
                shortcuts = qdbus_cast<FixtureShortcuts>(args.at(1));
                parentWindow = args.at(2).toString();
                bindOptions = options;
                QTimer::singleShot(0, this, [this, request] {
                    response(request, bindResponse, {{"shortcuts", QVariant::fromValue(bound())}});
                });
            }
        } else if (method == "Close") {
            if (message.interface() == "org.freedesktop.portal.Session") ++sessionCloses;
            else ++requestCloses;
            reply();
        } else {
            connection.send(message.createErrorReply("org.freedesktop.DBus.Error.UnknownMethod", "synthetic unknown method"));
        }
        return true;
    }
};

struct Observer {
    QList<QPair<bool, QByteArray>> edges;
    QStringList errors;
    bool ready = false;
    int availabilityUpdates = 0;
    QScopedPointer<GlobalShortcutBackend> backend{createGlobalShortcutBackend(nullptr)};
    Observer() {
        backend->edge = [this](bool down, const QByteArray& token) { edges.append({down, token}); };
        backend->error = [this](const QString& error) { errors.append(error); ready = false; };
        backend->availability = [this](bool available) { ready = available; ++availabilityUpdates; };
    }
};
}

class GlobalShortcutPortalTest : public QObject {
    Q_OBJECT
private slots:
    void bindingAndEdges_data() {
        QTest::addColumn<bool>("objectHandle");
        QTest::addColumn<bool>("registryAvailable");
        QTest::newRow("string-session") << false << true;
        QTest::newRow("object-path-session") << true << true;
        QTest::newRow("older-portal") << false << false;
    }
    void bindingAndEdges() {
        QFETCH(bool, objectHandle); QFETCH(bool, registryAvailable);
        Portal portal; portal.objectHandle = objectHandle; portal.registryAvailable = registryAvailable;
        Observer observer;
        QVERIFY(!observer.backend->start()); // Registration is asynchronous.
        QVERIFY(!observer.ready);
        QTRY_VERIFY(observer.ready);
        QVERIFY(observer.errors.isEmpty());
        QCOMPARE(portal.appId, QString("io.github.keithxc.DeskPort"));
        QCOMPARE(portal.creates, 1);
        QCOMPARE(portal.binds, 1);
        QVERIFY(!portal.createOptions.value("session_handle_token").toString().isEmpty());
        QVERIFY(!portal.createOptions.value("handle_token").toString().isEmpty());
        QVERIFY(!portal.bindOptions.value("handle_token").toString().isEmpty());
        QCOMPARE(portal.requestedSession, portal.session);
        QVERIFY(portal.parentWindow.isEmpty());
        QCOMPARE(portal.shortcuts.size(), 1);
        QCOMPARE(portal.shortcuts.at(0).id, Action);
        QCOMPARE(portal.shortcuts.at(0).options.value("preferred_trigger").toString(), QString("LOGO+apostrophe"));
        QVERIFY(!portal.shortcuts.at(0).options.value("description").toString().isEmpty());

        portal.edge(false); // Unmatched release and unrelated shortcuts are ignored.
        portal.edge(true, "unrelated");
        portal.edge(true, Action, portal.session + "_other");
        portal.edge(true, Action, {}, "synthetic-token");
        portal.edge(true); // A repeated Activated must not lose the token.
        portal.edge(false);
        portal.edge(false);
        QTRY_COMPARE(observer.edges.size(), 2);
        QCOMPARE(observer.edges.at(0), qMakePair(true, QByteArray("synthetic-token")));
        QCOMPARE(observer.edges.at(1).first, false);
        observer.backend->stop();
        QTRY_COMPARE(portal.sessionCloses, 1);
        portal.edge(true); portal.edge(false);
        QTest::qWait(20);
        QCOMPARE(observer.edges.size(), 2);
    }

    void deniedOrUnbound_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("create-cancelled") << 0;
        QTest::newRow("bind-denied") << 1;
        QTest::newRow("no-trigger-granted") << 2;
    }
    void deniedOrUnbound() {
        QFETCH(int, mode);
        Portal portal;
        if (mode == 0) portal.createResponse = 1;
        if (mode == 1) portal.bindResponse = 2;
        if (mode == 2) portal.bindGranted = false;
        Observer observer; observer.backend->start();
        QTRY_VERIFY(!observer.errors.isEmpty());
        QVERIFY(!observer.ready);
        portal.edge(true); portal.edge(false);
        QTest::qWait(20);
        QVERIFY(observer.edges.isEmpty());
        QCOMPARE(portal.binds, mode == 0 ? 0 : 1);
    }

    void revocationAndServiceLoss_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("session-closed") << 0;
        QTest::newRow("binding-removed") << 1;
        QTest::newRow("portal-disappeared") << 2;
    }
    void revocationAndServiceLoss() {
        QFETCH(int, mode);
        Portal portal; Observer observer; observer.backend->start();
        QTRY_VERIFY(observer.ready);
        portal.edge(true);
        QTRY_COMPARE(observer.edges.size(), 1);
        if (mode == 0) portal.closed();
        if (mode == 1) portal.changed({});
        if (mode == 2) QVERIFY(portal.bus.unregisterService(Service));
        QTRY_VERIFY(!observer.ready);
        portal.edge(false);
        QTest::qWait(20);
        QCOMPARE(observer.edges.size(), 1);
        if (mode != 2) QVERIFY(!observer.errors.isEmpty());
    }

    void stopCancelsPendingRequestAndIgnoresLateResponse() {
        Portal portal; portal.holdCreate = true;
        Observer observer; observer.backend->start();
        QTRY_COMPARE(portal.creates, 1);
        observer.backend->stop();
        QTRY_COMPARE(portal.requestCloses, 1);
        portal.created(portal.heldRequest);
        QTest::qWait(20);
        QCOMPARE(portal.binds, 0);
        QVERIFY(!observer.ready);
        QVERIFY(observer.edges.isEmpty());
    }

    void compositorRebindingUpdatesFixedChordAvailability() {
        Portal portal; Observer observer; observer.backend->start();
        QTRY_VERIFY(observer.ready);
        QVERIFY(observer.backend->fixedChord);
        portal.changed({{Action, {{"trigger_description", "Meta+F9"}}}});
        QTRY_VERIFY(!observer.backend->fixedChord);
        QVERIFY(observer.ready);
        portal.edge(true); portal.edge(false);
        QTRY_COMPARE(observer.edges.size(), 2);
        portal.changed(portal.bound());
        QTRY_VERIFY(observer.backend->fixedChord);
    }

    void serviceAppearanceDuringRegistryReplyCreatesOnlyOneSession() {
        // Install the watcher before the service appears, then start before its
        // queued registration signal is delivered. Registry remains pending
        // until the fixture explicitly releases its reply.
        Observer observer;
        auto watcher = observer.backend->findChild<QDBusServiceWatcher*>();
        QVERIFY(watcher);
        QSignalSpy appeared(watcher, &QDBusServiceWatcher::serviceRegistered);
        Portal portal;
        portal.holdRegister = true;
        bool appearedWhilePending = false;
        connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [&] {
            appearedWhilePending = !observer.backend->findChildren<QDBusPendingCallWatcher*>().isEmpty()
                && portal.creates == 0;
        });
        QCOMPARE(appeared.count(), 0);
        observer.backend->start();
        QTRY_COMPARE(appeared.count(), 1);
        QVERIFY(appearedWhilePending);
        QTRY_COMPARE(portal.registrations, 1);
        QTest::qWait(20); // Allow a duplicate Register, if any, to reach the fixture.
        QCOMPARE(portal.registrations, 1);
        QCOMPARE(portal.creates, 0);
        for (const auto& reply : portal.registerReplies) QVERIFY(portal.bus.send(reply));
        QTRY_VERIFY(observer.ready);
        QCOMPARE(portal.registrations, 1);
        QCOMPARE(portal.creates, 1);
        QCOMPARE(portal.binds, 1);
        QVERIFY(observer.errors.isEmpty());
        observer.backend->stop();
        QTRY_COMPARE(portal.sessionCloses, 1);
    }
};

QTEST_GUILESS_MAIN(GlobalShortcutPortalTest)
#include "global-shortcut-portal.moc"
