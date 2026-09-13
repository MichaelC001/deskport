#pragma once
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <memory>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>

class FlatpakActivationObject : public QDBusVirtualObject {
public:
    std::function<void()> activate;
    QString introspect(const QString&) const override {
        return QStringLiteral("<interface name=\"io.github.keithxc.DeskPort.Instance\">"
                              "<method name=\"Activate\"/><method name=\"Ping\"/></interface>");
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override {
        if (message.interface() != "io.github.keithxc.DeskPort.Instance" ||
            (message.member() != "Activate" && message.member() != "Ping")) return false;
        if (message.member() == "Activate" && activate) activate();
        connection.send(message.createReply());
        return true;
    }
};
#endif

// One GUI owner per user configuration, including across installed versions.
// The lock owns stale-socket cleanup; a second process never removes a live
// server and never opens another GUI when activation delivery fails.
class SingleInstance {
public:
    ~SingleInstance() {
#ifdef Q_OS_LINUX
        if (flatpakObjectRegistered) QDBusConnection::sessionBus().unregisterObject("/io/github/keithxc/DeskPort");
        if (flatpakOwner) QDBusConnection::sessionBus().unregisterService("io.github.keithxc.DeskPort");
#endif
    }
    std::function<void()> activate;
    bool delivered = false;
    bool start(const QString& directory = QString(), bool requestActivation = true) {
#ifdef Q_OS_LINUX
        if (qEnvironmentVariable("FLATPAK_ID") == "io.github.keithxc.DeskPort") {
            // Sandbox PID reuse breaks stale-file-lock detection after a crash.
            // The session bus releases ownership on process death and is shared
            // between Flatpak invocations, unlike their private /tmp sockets.
            auto bus = QDBusConnection::sessionBus();
            if (!bus.isConnected()) return false;
            flatpakActivation = std::make_unique<FlatpakActivationObject>();
            flatpakActivation->activate = [this] { if (activate) activate(); };
            flatpakObjectRegistered = bus.registerVirtualObject("/io/github/keithxc/DeskPort", flatpakActivation.get());
            if (flatpakObjectRegistered && bus.registerService("io.github.keithxc.DeskPort")) {
                flatpakOwner = true;
                return true;
            }
            const auto message = QDBusMessage::createMethodCall("io.github.keithxc.DeskPort",
                "/io/github/keithxc/DeskPort", "io.github.keithxc.DeskPort.Instance",
                requestActivation ? "Activate" : "Ping");
            delivered = bus.call(message, QDBus::Block, 1500).type() == QDBusMessage::ReplyMessage;
            return false;
        }
#endif
        const auto path = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : directory;
        if (!QDir().mkpath(path)) return false;
        const auto name = "deskport-" + QString::fromLatin1(QCryptographicHash::hash(QDir(path).absolutePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
        lock = std::make_unique<QLockFile>(path + "/gui.lock");
        lock->setStaleLockTime(0);
        if (!lock->tryLock()) {
            for (int attempt = 0; attempt < 30; ++attempt) {
                QLocalSocket socket;
                socket.connectToServer(name);
                if (socket.waitForConnected(50)) {
                    socket.write(requestActivation ? "activate\n" : "ping\n");
                    delivered = socket.waitForBytesWritten(500);
                    return false;
                }
                QThread::msleep(50);
            }
            return false;
        }
        QLocalServer::removeServer(name);
        server.setSocketOptions(QLocalServer::UserAccessOption);
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (auto socket = server.nextPendingConnection()) {
                socket->setReadBufferSize(64);
                const auto receive = [this, socket] {
                    if (!socket->canReadLine()) return;
                    if (socket->readLine(64) == "activate\n" && activate) activate();
                    socket->disconnectFromServer();
                };
                QObject::connect(socket, &QLocalSocket::readyRead, socket, receive);
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                QTimer::singleShot(2000, socket, &QObject::deleteLater);
                receive();
            }
        });
        return server.listen(name);
    }
private:
#ifdef Q_OS_LINUX
    bool flatpakOwner = false;
    bool flatpakObjectRegistered = false;
    std::unique_ptr<FlatpakActivationObject> flatpakActivation;
#endif
    std::unique_ptr<QLockFile> lock;
    QLocalServer server;
};
