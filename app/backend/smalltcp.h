#pragma once

#include <QSslSocket>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QSignalBlocker>
#include <QDebug>
#include <memory>
#ifdef Q_OS_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

// Only control TCP sockets use this policy. TLS and application messages remain
// owned by their existing callers. Never retry after encryption/application I/O.
namespace SmallTcp {
inline bool supported() {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}
inline QString pathKey(const QString& host, quint16 port) {
    if (!supported()) return {};
    QStringList addresses;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        for (const auto& address : iface.addressEntries())
            addresses << iface.name() + ":" + address.ip().toString();
    }
    addresses.sort();
    return host.toLower() + ':' + QString::number(port) + '/' + addresses.join(',');
}
struct Cache {
    QMutex mutex;
    QElapsedTimer clock;
    QHash<QString, qint64> entries;
    QHash<QString, QHostAddress> addresses;
    Cache() { clock.start(); }
};
inline Cache& cache() { static Cache value; return value; }
inline bool cached(const QString& key) {
    if (!supported()) return false;
    auto& c = cache(); QMutexLocker lock(&c.mutex);
    return c.entries.contains(key) && c.clock.elapsed() - c.entries.value(key) < 120000;
}
inline QHostAddress cachedAddress(const QString& key) {
    auto& c = cache(); QMutexLocker lock(&c.mutex); return c.addresses.value(key);
}
inline void remember(const QString& key, const QHostAddress& address = {}) {
    auto& c = cache(); QMutexLocker lock(&c.mutex);
    // Fixed expiry: polling must not keep compatibility enabled forever.
    if (c.entries.contains(key) && c.clock.elapsed() - c.entries.value(key) < 120000) return;
    if (c.entries.size() >= 64) { c.entries.clear(); c.addresses.clear(); }
    c.entries.insert(key, c.clock.elapsed());
    if (!address.isNull()) c.addresses.insert(key, address);
}
inline void forget(const QString& key) {
    auto& c = cache(); QMutexLocker lock(&c.mutex); c.entries.remove(key); c.addresses.remove(key);
}
inline bool prepare(QAbstractSocket& socket, const QHostAddress& address) {
#ifdef Q_OS_LINUX
    const auto any = address.protocol() == QAbstractSocket::IPv6Protocol ? QHostAddress::AnyIPv6 :
        address.protocol() == QAbstractSocket::IPv4Protocol ? QHostAddress::AnyIPv4 : QHostAddress::Any;
    if (!socket.bind(QHostAddress(any), 0)) return false;
    const int mss = 900;
    if (::setsockopt(socket.socketDescriptor(), IPPROTO_TCP, TCP_MAXSEG, &mss, sizeof(mss)) == 0) return true;
    socket.abort();
#else
    Q_UNUSED(socket); Q_UNUSED(address);
#endif
    return false;
}
inline QHostAddress resolve(const QString& host) {
    QHostAddress address(host);
    if (!address.isNull()) return address;
    QHostInfo result;
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    const int lookup = QHostInfo::lookupHost(host, &loop, [&](const QHostInfo& info) { result = info; loop.quit(); });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(2000); loop.exec(QEventLoop::ExcludeUserInputEvents);
    QHostInfo::abortHostLookup(lookup);
    for (const auto& candidate : result.addresses())
        if (candidate.protocol() == QAbstractSocket::IPv4Protocol) return candidate;
    return result.addresses().value(0);
}
inline bool startSmall(QSslSocket& socket, const QString& host, quint16 port, QHostAddress address = {}) {
    if (address.isNull()) address = QHostAddress(host);
    if (address.isNull() || !prepare(socket, address)) return false;
    socket.connectToHostEncrypted(address.toString(), port, host);
    return true;
}

inline bool connectBlocking(QSslSocket& socket, const QString& host, quint16 port, int timeout) {
    const auto key = pathKey(host, port);
    bool small = cached(key);
    bool sslError = false;
    const auto observation = QObject::connect(&socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),
        &socket, [&](const QList<QSslError>&) { sslError = true; }, Qt::DirectConnection);
    if (small) {
        if (!startSmall(socket, host, port, cachedAddress(key))) { QObject::disconnect(observation); forget(key); return false; }
    } else socket.connectToHostEncrypted(host, port);
    QElapsedTimer deadline; deadline.start();
    bool ok = socket.waitForEncrypted(timeout);
    if (!ok && !small && supported() && !sslError && (socket.error() == QAbstractSocket::SocketTimeoutError ||
            (deadline.elapsed() >= timeout && socket.state() == QAbstractSocket::ConnectedState))) {
        const auto address = socket.peerAddress();
        { QSignalBlocker blocker(&socket); socket.abort(); }
        small = true;
        qInfo() << "Retrying TLS handshake with reduced TCP segments";
        ok = startSmall(socket, host, port, address) && socket.waitForEncrypted(timeout);
    }
    QObject::disconnect(observation);
    if (!ok) forget(key);
    // Caller validates the expected certificate before recording success.
    socket.setProperty("deskportSmallTcp", small && ok);
    return ok;
}
inline void accepted(QSslSocket& socket, const QString& host, quint16 port) {
    if (socket.property("deskportSmallTcp").toBool()) remember(pathKey(host, port), socket.peerAddress());
}
inline void connectAsync(QSslSocket* socket, const QString& host, quint16 port) {
    const auto key = pathKey(host, port);
    auto timer = new QTimer(socket); timer->setSingleShot(true);
    auto sslError = std::make_shared<bool>(false);
    QObject::connect(socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), timer,
        [sslError](const QList<QSslError>&) { *sslError = true; });
    QObject::connect(socket, &QSslSocket::encrypted, timer, &QTimer::stop);
    QObject::connect(socket, &QSslSocket::errorOccurred, timer, [timer, key](QAbstractSocket::SocketError) { timer->stop(); forget(key); });
    if (cached(key)) {
        socket->setProperty("deskportSmallTcp", true);
        if (startSmall(*socket, host, port, cachedAddress(key))) return;
        forget(key);
        socket->setProperty("deskportSmallTcp", false);
    }
    QObject::connect(timer, &QTimer::timeout, socket, [socket, host, port, sslError] {
        if (*sslError || socket->isEncrypted() || socket->state() != QAbstractSocket::ConnectedState || socket->peerAddress().isNull()) return;
        const auto address = socket->peerAddress();
        { QSignalBlocker blocker(socket); socket->abort(); }
        socket->setProperty("deskportSmallTcp", true);
        qInfo() << "Retrying TLS handshake with reduced TCP segments";
        if (!startSmall(*socket, host, port, address)) {
            socket->setProperty("deskportSmallTcp", false);
            socket->connectToHostEncrypted(host, port);
        }
    });
    socket->connectToHostEncrypted(host, port);
    if (supported()) timer->start(1500);
}
}
