#include "native.h"
#include "pipeio.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDBusServiceWatcher>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QUuid>
#include <QPointer>
#include <QScopedValueRollback>

namespace {
const QString Service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString Path = QStringLiteral("/org/freedesktop/portal/desktop");
const QString Clipboard = QStringLiteral("org.freedesktop.portal.Clipboard");
const QString Desktop = QStringLiteral("org.freedesktop.portal.RemoteDesktop");
class PortalClipboard final : public QObject, public ClipboardNative {
    Q_OBJECT
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusServiceWatcher watcher;
    QString session, pendingRequest;
    QString notice = QStringLiteral("Wayland clipboard: waiting for portal permission.");
    QStringList types, publishedTypes;
    Reader reader;
    qint64 generation = 0, sourceGeneration = 0;
    bool healthy = true, ready = false, owned = false, reading = false, serving = false;
    std::function<void(const QVariantMap&)> response;
    QTimer permissionDeadline;
    using Reply = std::function<void(const QDBusMessage&)>;
    void fail(const QString& reason) {
        healthy = false; ready = false; owned = false; reader = {}; types.clear();
        ++generation; ++sourceGeneration;
        notice = reason;
        close();
    }
    void call(const QString& interface, const QString& method, const QVariantList& arguments, Reply done, bool fatal = true) {
        auto message = QDBusMessage::createMethodCall(Service, Path, interface, method);
        message.setArguments(arguments);
        auto pending = new QDBusPendingCallWatcher(bus.asyncCall(message, 5000), this);
        connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, done, fatal] {
            const QDBusMessage reply = pending->reply(); pending->deleteLater();
            if (!healthy) return;
            if (reply.type() == QDBusMessage::ErrorMessage && fatal) {
                fail(QStringLiteral("Wayland clipboard unavailable: portal operation failed (%1).").arg(reply.errorName())); return;
            }
            done(reply);
        });
    }
    void close() {
        permissionDeadline.stop();
        for (const auto& pair : {qMakePair(pendingRequest, QStringLiteral("org.freedesktop.portal.Request")),
                                qMakePair(session, QStringLiteral("org.freedesktop.portal.Session"))}) {
            if (!pair.first.isEmpty()) {
                auto message = QDBusMessage::createMethodCall(Service, pair.first, pair.second, "Close");
                message.setAutoStartService(false); // Revocation must not restart a vanished portal.
                bus.asyncCall(message);
            }
        }
        pendingRequest.clear(); session.clear(); response = {};
    }
    void request(const QString& method, QVariantList arguments, QVariantMap options,
                 std::function<void(const QVariantMap&)> done) {
        if (method == "Start" && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("GNOME", Qt::CaseInsensitive))
            notice = QStringLiteral("Wayland clipboard: approve the system dialog. If GNOME disables Share, enable Allow Remote Interaction (no input devices are requested).");
        const auto token = QStringLiteral("deskport_") + QUuid::createUuid().toString(QUuid::Id128);
        auto sender = bus.baseService().mid(1); sender.replace('.', '_');
        pendingRequest = Path + "/request/" + sender + '/' + token;
        options["handle_token"] = token;
        bus.connect(Service, pendingRequest, "org.freedesktop.portal.Request", "Response", this, SLOT(responded(uint,QVariantMap)));
        response = std::move(done);
        arguments.append(options);
        const auto expected = pendingRequest;
        call(Desktop, method, arguments, [this, expected](const QDBusMessage& reply) {
            // All supported portals honor handle_token. Fail closed on a different
            // request path rather than lose an early Response race.
            if (reply.arguments().size() != 1 || qvariant_cast<QDBusObjectPath>(reply.arguments()[0]).path() != expected)
                fail(QStringLiteral("Wayland clipboard unavailable: portal request handle mismatch."));
        });
    }
    void initialize() {
        call("org.freedesktop.DBus.Properties", "Get", {Clipboard, "version"}, [this](const QDBusMessage& reply) {
            if (reply.arguments().isEmpty() || qvariant_cast<QDBusVariant>(reply.arguments()[0]).variant().toUInt() < 1) {
                fail(QStringLiteral("Wayland clipboard unavailable: neither data-control nor Clipboard portal is supported.")); return;
            }
            request("CreateSession", {}, {{"session_handle_token", QStringLiteral("deskport_") + QUuid::createUuid().toString(QUuid::Id128)}}, [this](const QVariantMap& result) {
                const auto handle = result.value("session_handle");
                session = handle.userType() == qMetaTypeId<QDBusObjectPath>() ? qvariant_cast<QDBusObjectPath>(handle).path() : handle.toString();
                if (!session.startsWith(Path + "/session/")) { fail(QStringLiteral("Wayland clipboard unavailable: invalid portal session.")); return; }
                bus.connect(Service, session, "org.freedesktop.portal.Session", "Closed", this, SLOT(closed(QVariantMap)));
                call(Clipboard, "RequestClipboard", {QVariant::fromValue(QDBusObjectPath(session)), QVariantMap{}}, [this](const QDBusMessage&) {
                    // Clipboard only: no keyboard/pointer, screen capture, EIS or
                    // persistence token. The system dialog remains authoritative.
                    request("SelectDevices", {QVariant::fromValue(QDBusObjectPath(session))}, {{"types", uint(0)}}, [this](const QVariantMap&) {
                        request("Start", {QVariant::fromValue(QDBusObjectPath(session)), QString()}, {}, [this](const QVariantMap& result) {
                            if (!result.value("clipboard_enabled").toBool() || !result.contains("devices") || result.value("devices").toUInt() != 0 || result.contains("streams")) {
                                fail(QStringLiteral("Wayland clipboard unavailable: portal did not grant clipboard-only access.")); return;
                            }
                            ready = true; permissionDeadline.stop(); notice.clear();
                            // A pending remote offer is published only after consent.
                            if (!publishedTypes.isEmpty()) setSelection();
                        });
                    });
                });
            });
        });
    }
    void setSelection() {
        const auto revision = sourceGeneration;
        call(Clipboard, "SetSelection", {QVariant::fromValue(QDBusObjectPath(session)), QVariantMap{{"mime_types", publishedTypes}}}, [this, revision](const QDBusMessage&) {
            if (revision == sourceGeneration) { owned = true; types = publishedTypes; }
        });
    }
    void transferDone(uint serial, bool success) {
        if (!ready) return;
        call(Clipboard, "SelectionWriteDone", {QVariant::fromValue(QDBusObjectPath(session)), serial, success}, [](const QDBusMessage&) {}, false);
    }
private slots:
    void responded(uint code, const QVariantMap& result) {
        bus.disconnect(Service, pendingRequest, "org.freedesktop.portal.Request", "Response", this, SLOT(responded(uint,QVariantMap)));
        pendingRequest.clear();
        auto done = std::move(response); response = {};
        if (code != 0) { fail(QStringLiteral("Wayland clipboard unavailable: portal permission denied or cancelled.")); return; }
        if (healthy && done) done(result);
    }
    void closed(const QVariantMap&) { fail(QStringLiteral("Wayland clipboard stopped: portal session was closed.")); }
    void ownerChanged(const QDBusObjectPath& handle, const QVariantMap& options) {
        if (!healthy || handle.path() != session) return;
        owned = options.value("session_is_owner").toBool();
        types = qdbus_cast<QStringList>(options.value("mime_types"));
        if (!owned && ready) { ++sourceGeneration; reader = {}; publishedTypes.clear(); }
        // Notifications emitted during Start establish the baseline without
        // reading payloads. Copies after its successful response are observable.
        if (ready && !owned) ++generation;
    }
    void transfer(const QDBusObjectPath& handle, const QString& mime, uint serial) {
        if (handle.path() != session || !ready) return;
        if (!owned || serving || !reader || !publishedTypes.contains(mime)) { transferDone(serial, false); return; }
        const auto revision = sourceGeneration;
        const auto provide = reader;
        call(Clipboard, "SelectionWrite", {QVariant::fromValue(handle), serial}, [this, revision, provide, mime, serial](const QDBusMessage& reply) {
            if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() != 1) { transferDone(serial, false); return; }
            const auto descriptor = qvariant_cast<QDBusUnixFileDescriptor>(reply.arguments()[0]);
            const int fd = fcntl(descriptor.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
            if (fd < 0 || !owned || revision != sourceGeneration || serving) { if (fd >= 0) ::close(fd); transferDone(serial, false); return; }
            QScopedValueRollback<bool> busy(serving, true);
            const auto bytes = provide(mime);
            new ClipboardPipe::Writer(fd, bytes, [this, revision] { return valid() && ready && owned && revision == sourceGeneration; }, this,
                                      [this, serial](bool success) { transferDone(serial, success); });
        }, false);
    }
public:
    PortalClipboard() : watcher(Service, bus, QDBusServiceWatcher::WatchForOwnerChange, this) {
        if (!bus.isConnected()) { fail(QStringLiteral("Wayland clipboard unavailable: session bus is disconnected.")); return; }
        connect(&watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString&, const QString& previous, const QString&) {
            if (previous.isEmpty()) return; // Allow ordinary D-Bus service activation.
            fail(QStringLiteral("Wayland clipboard stopped: portal service changed or disconnected."));
        });
        bus.connect(Service, Path, Clipboard, "SelectionOwnerChanged", this, SLOT(ownerChanged(QDBusObjectPath,QVariantMap)));
        bus.connect(Service, Path, Clipboard, "SelectionTransfer", this, SLOT(transfer(QDBusObjectPath,QString,uint)));
        connect(&permissionDeadline, &QTimer::timeout, this, [this] { fail(QStringLiteral("Wayland clipboard unavailable: portal permission timed out.")); });
        permissionDeadline.setSingleShot(true); permissionDeadline.start(60000);
        QTimer::singleShot(0, this, [this] { initialize(); });
    }
    ~PortalClipboard() override { close(); }
    bool valid() const override { return healthy && bus.isConnected(); }
    QString stateMessage() const override {
        return bus.isConnected() ? notice : QStringLiteral("Wayland clipboard stopped: session bus disconnected.");
    }
    void cancel() override { healthy = false; ready = false; ++generation; ++sourceGeneration; reader = {}; close(); }
    bool failedRead(const QByteArray& bytes) const override { return bytes.isNull(); }
    qint64 revision() override { return generation; }
    QStringList formats() override { return types; }
    QByteArray read(const QString& mime) override {
        if (!ready || !types.contains(mime) || reading || owned) return {};
        QScopedValueRollback<bool> busy(reading, true);
        const auto revision = generation;
        QEventLoop loop; QByteArray bytes;
        QObject lifetime; const QPointer<QObject> guard(&lifetime);
        call(Clipboard, "SelectionRead", {QVariant::fromValue(QDBusObjectPath(session)), mime}, [&, guard](const QDBusMessage& reply) {
            if (!guard) return;
            if (reply.type() != QDBusMessage::ErrorMessage && reply.arguments().size() == 1 && ready && generation == revision) {
                const auto descriptor = qvariant_cast<QDBusUnixFileDescriptor>(reply.arguments()[0]);
                const int fd = fcntl(descriptor.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
                if (fd >= 0) { bytes = ClipboardPipe::read(fd, [this, revision] { return valid() && ready && generation == revision; }); ::close(fd); }
            }
            loop.quit();
        }, false);
        // Transfer errors are per selection; revocation and service loss invalidate
        // the provider through their signals. Also bound nested waiting on
        // revocation/cancellation, not only on a successful FD reply.
        QTimer poll;
        connect(&poll, &QTimer::timeout, &loop, [&] { if (!ready || generation != revision) loop.quit(); });
        poll.start(20); loop.exec();
        return ready && generation == revision ? bytes : QByteArray();
    }
    void publish(const QStringList& formats, Reader provide, int = 1) override {
        if (!healthy) return;
        if (formats.isEmpty() && !owned) return;
        ++sourceGeneration; ++generation;
        reader = std::move(provide); publishedTypes = formats;
        if (ready) setSelection();
    }
};
}
std::unique_ptr<ClipboardNative> makePortalClipboardNative() { return std::unique_ptr<ClipboardNative>(new PortalClipboard); }
#include "portalnative.moc"
