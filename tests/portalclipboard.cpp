#include "clipboard/native.h"
#include <QtTest>
#include <QGuiApplication>
#include <QDBusVirtualObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusArgument>
#include <QDBusUnixFileDescriptor>
#include <QDBusObjectPath>
#include <QSocketNotifier>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

namespace {
const QString Path = "/org/freedesktop/portal/desktop";
const QString Session = Path + "/session/synthetic/deskport";
const QString Clipboard = "org.freedesktop.portal.Clipboard";
// Runs only inside this script's private dbus-run-session, never on a desktop bus.
class Portal final : public QDBusVirtualObject {
public:
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "synthetic-portal");
    uint response = 0;
    bool objectHandle = false, readFailure = false, writeFailure = false, doneFailure = false;
    bool available = true, grant = true, setFailure = false, stall = false, delay = false;
    int reads = 0, writes = 0, closes = 0, done = 0;
    bool success = false;
    QByteArray output;
    QList<int> fds;
    QStringList advertised;
    QVariantMap selected;
    Portal() {
        if (!qEnvironmentVariableIsSet("DESKPORT_TEST_PRIVATE_BUS")) qFatal("private test bus required");
        if (!bus.registerService("org.freedesktop.portal.Desktop") || !bus.registerVirtualObject(Path, this, QDBusConnection::SubPath)) qFatal("test portal registration failed");
    }
    ~Portal() override { bus.unregisterObject(Path, QDBusConnection::UnregisterTree); for (int fd : fds) ::close(fd); bus.unregisterService("org.freedesktop.portal.Desktop"); }
    QString introspect(const QString&) const override { return {}; }
    void signal(const QString& member, QVariantList args) {
        auto message = QDBusMessage::createSignal(Path, Clipboard, member); message.setArguments(args); bus.send(message);
    }
    void owner(QStringList types, bool mine = false) {
        signal("SelectionOwnerChanged", {QVariant::fromValue(QDBusObjectPath(Session)), QVariantMap{{"mime_types", types}, {"session_is_owner", mine}}});
    }
    void closed() { auto m = QDBusMessage::createSignal(Session, "org.freedesktop.portal.Session", "Closed"); m.setArguments({QVariantMap{}}); bus.send(m); }
    bool handleMessage(const QDBusMessage& m, const QDBusConnection& connection) override {
        const auto method = m.member(); const auto args = m.arguments();
        auto reply = [&](QVariantList values = {}) { connection.send(m.createReply(values)); };
        if (method == "Get") {
            if (!available) connection.send(m.createErrorReply("org.freedesktop.DBus.Error.UnknownInterface", "synthetic unavailable"));
            else reply({QVariant::fromValue(QDBusVariant(uint(1)))});
        } else if (method == "CreateSession" || method == "SelectDevices" || method == "Start") {
            auto options = qdbus_cast<QVariantMap>(args.last()); auto sender = m.service().mid(1); sender.replace('.', '_');
            const auto request = Path + "/request/" + sender + '/' + options["handle_token"].toString();
            if (method == "SelectDevices") selected = options;
            reply({QVariant::fromValue(QDBusObjectPath(request))});
            QTimer::singleShot(0, this, [this, request, method] {
                QVariantMap result;
                if (method == "CreateSession") result["session_handle"] = objectHandle ? QVariant::fromValue(QDBusObjectPath(Session)) : QVariant(Session);
                if (method == "Start") { owner({"text/plain", "image/png", "application/synthetic"}); result["clipboard_enabled"] = grant; result["devices"] = uint(0); }
                auto event = QDBusMessage::createSignal(request, "org.freedesktop.portal.Request", "Response");
                event.setArguments({response, result}); bus.send(event);
            });
        } else if (method == "Close") { ++closes; reply(); }
        else if (method == "RequestClipboard") reply();
        else if (method == "SetSelection") {
            if (setFailure) connection.send(m.createErrorReply("org.freedesktop.portal.Error.Failed", "synthetic failure"));
            else { advertised = qdbus_cast<QStringList>(qdbus_cast<QVariantMap>(args[1]).value("mime_types")); owner(advertised, true); reply(); }
        } else if (method == "SelectionRead") {
            ++reads;
            if (readFailure) { connection.send(m.createErrorReply("org.freedesktop.portal.Error.Failed", "synthetic unreadable selection")); return true; }
            int pipefd[2]; if (::pipe(pipefd) != 0) qFatal("synthetic pipe creation failed");
            if (!stall) { ::write(pipefd[1], "synthetic text", 14); ::close(pipefd[1]); }
            else fds.append(pipefd[1]);
            const auto descriptor = QDBusUnixFileDescriptor(pipefd[0]); ::close(pipefd[0]);
            const auto response = m.createReply({QVariant::fromValue(descriptor)});
            if (delay) QTimer::singleShot(80, this, [connection, response] { connection.send(response); });
            else connection.send(response);
        } else if (method == "SelectionWrite") {
            ++writes;
            if (writeFailure) { connection.send(m.createErrorReply("org.freedesktop.portal.Error.Failed", "synthetic transfer cancelled")); return true; }
            int pipefd[2]; if (::pipe(pipefd) != 0) qFatal("synthetic pipe creation failed");
            auto input = new QSocketNotifier(pipefd[0], QSocketNotifier::Read, this); fds.append(pipefd[0]);
            connect(input, &QSocketNotifier::activated, this, [this, input, pipefd] {
                char bytes[4096]; const auto count = ::read(pipefd[0], bytes, sizeof(bytes));
                if (count > 0) output.append(bytes, count); else input->setEnabled(false);
            });
            const auto descriptor = QDBusUnixFileDescriptor(pipefd[1]); ::close(pipefd[1]);
            reply({QVariant::fromValue(descriptor)});
        } else if (method == "SelectionWriteDone") { ++done; success = args[2].toBool(); if (doneFailure) connection.send(m.createErrorReply("org.freedesktop.portal.Error.Failed", "synthetic expired serial")); else reply(); }
        else connection.send(m.createErrorReply("org.freedesktop.DBus.Error.UnknownMethod", "synthetic unknown"));
        return true;
    }
};
}
class PortalTest : public QObject {
    Q_OBJECT
private slots:
    void consentMimeAndLazyOwnership_data() {
        QTest::addColumn<bool>("objectHandle"); QTest::newRow("string-handle") << false; QTest::newRow("object-path-handle") << true;
    }
    void consentMimeAndLazyOwnership() {
        QFETCH(bool, objectHandle); Portal portal; portal.objectHandle = objectHandle; auto native = makePortalClipboardNative();
        QTRY_VERIFY(native->valid() && native->stateMessage().isEmpty());
        QCOMPARE(portal.selected["types"].toUInt(), uint(0));
        QCOMPARE(portal.reads, 0); QCOMPARE(native->revision(), qint64(0));
        QVERIFY(native->formats().contains("image/png"));
        QVERIFY(native->read("text/uri-list").isNull()); QCOMPARE(portal.reads, 0);
        QCOMPARE(native->read("text/plain"), QByteArray("synthetic text"));
        int reads = 0;
        native->publish({"image/png", "text/uri-list"}, [&](const QString&) { ++reads; return QByteArray("synthetic lazy"); });
        QTRY_VERIFY(portal.advertised.contains("image/png"));
        QCOMPARE(reads, 0); // Formats and background ownership never materialize.
        portal.signal("SelectionTransfer", {QVariant::fromValue(QDBusObjectPath(Session)), "image/png", uint(1)});
        QTRY_COMPARE(portal.done, 1); QVERIFY(portal.success); QCOMPARE(reads, 1); QCOMPARE(portal.output, QByteArray("synthetic lazy"));
        portal.owner({"text/plain"}); QTRY_VERIFY(native->formats() == QStringList{"text/plain"});
        const auto previous = portal.advertised;
        native->publish({}, {}); QTest::qWait(20); QCOMPARE(portal.advertised, previous); // Preserve newer local owner.
        native->cancel(); QTRY_VERIFY(portal.closes > 0);
    }
    void unavailableDeniedAndRevoked_data() {
        QTest::addColumn<int>("mode"); for (int i = 0; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void unavailableDeniedAndRevoked() {
        QFETCH(int, mode); Portal portal;
        portal.available = mode != 0; portal.response = mode == 1 ? 1 : 0; portal.grant = mode != 2;
        auto native = makePortalClipboardNative();
        if (mode >= 3) {
            QTRY_VERIFY(native->valid() && native->stateMessage().isEmpty());
            if (mode == 3) portal.closed();
            else { portal.setFailure = true; native->publish({"image/png"}, [](const QString&) { return QByteArray("unused"); }); }
        }
        QTRY_VERIFY(!native->valid()); QVERIFY(native->stateMessage().contains("clipboard")); QCOMPARE(portal.reads, 0);
    }
    void cancelledRead_data() {
        QTest::addColumn<bool>("delay"); QTest::addColumn<int>("action");
        for (bool delay : {false, true}) for (int action = 0; action < 4; ++action)
            QTest::newRow(qPrintable(QString("delay-%1-action-%2").arg(delay).arg(action))) << delay << action;
    }
    void cancelledRead() {
        QFETCH(bool, delay); QFETCH(int, action); Portal portal; portal.stall = true; portal.delay = delay;
        auto native = makePortalClipboardNative(); QTRY_VERIFY(native->stateMessage().isEmpty());
        QTimer::singleShot(20, this, [&] {
            if (action == 0) native->cancel();
            if (action == 1) portal.owner({"application/synthetic"});
            if (action == 2) portal.closed();
            if (action == 3) portal.bus.unregisterService("org.freedesktop.portal.Desktop");
        });
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY(native->read("text/plain").isNull()); QVERIFY(elapsed.elapsed() < 1000);
        QTest::qWait(150); // Late D-Bus reply must not touch a returned stack frame.
    }
    void transferFailureRecovers() {
        Portal portal; auto native = makePortalClipboardNative(); QTRY_VERIFY(native->stateMessage().isEmpty());
        portal.readFailure = true; QVERIFY(native->read("text/plain").isNull()); QVERIFY(native->valid());
        portal.readFailure = false; QCOMPARE(native->read("text/plain"), QByteArray("synthetic text"));
        portal.writeFailure = true;
        native->publish({"text/uri-list"}, [](const QString&) { return QByteArray("file:///synthetic/complete"); });
        QTRY_VERIFY(portal.advertised.contains("text/uri-list"));
        portal.signal("SelectionTransfer", {QVariant::fromValue(QDBusObjectPath(Session)), "text/uri-list", uint(11)});
        QTRY_COMPARE(portal.done, 1); QVERIFY(!portal.success); QVERIFY(native->valid()); QVERIFY(portal.output.isEmpty());
        portal.writeFailure = false; portal.doneFailure = true;
        portal.signal("SelectionTransfer", {QVariant::fromValue(QDBusObjectPath(Session)), "text/uri-list", uint(12)});
        QTRY_COMPARE(portal.done, 2); QTest::qWait(20); QVERIFY(native->valid());
        QCOMPARE(portal.output, QByteArray("file:///synthetic/complete"));
    }
    void failedLazyFileAndOwnerRace() {
        Portal portal; auto native = makePortalClipboardNative(); QTRY_VERIFY(native->stateMessage().isEmpty());
        native->publish({"text/uri-list"}, [](const QString&) { return QByteArray(); });
        QTRY_VERIFY(!portal.advertised.isEmpty());
        portal.signal("SelectionTransfer", {QVariant::fromValue(QDBusObjectPath(Session)), "text/uri-list", uint(2)});
        QTRY_COMPARE(portal.done, 1); QVERIFY(!portal.success); QVERIFY(portal.output.isEmpty());
        portal.signal("SelectionTransfer", {QVariant::fromValue(QDBusObjectPath(Session)), "application/not-offered", uint(3)});
        QTRY_COMPARE(portal.done, 2); QVERIFY(!portal.success); QCOMPARE(portal.writes, 1);
        native->publish({"image/png"}, [&](const QString&) {
            portal.owner({"text/plain"}); QTest::qWait(50); return QByteArray("stale");
        });
        QTest::qWait(30);
        portal.signal("SelectionTransfer", {QVariant::fromValue(QDBusObjectPath(Session)), "image/png", uint(4)});
        QTRY_COMPARE(portal.done, 3); QVERIFY(!portal.success); QVERIFY(portal.output.isEmpty());
    }
};
int main(int argc, char** argv) { signal(SIGPIPE, SIG_IGN); QGuiApplication app(argc, argv); PortalTest test; return QTest::qExec(&test, argc, argv); }
#include "portalclipboard.moc"
