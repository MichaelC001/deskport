#pragma once
#include "smalltcp.h"
#include <QTcpServer>

namespace SmallTcp {
// A short-lived, loopback-only CONNECT endpoint for one fixed destination. It
// forwards opaque TLS bytes; QNetworkAccessManager retains TLS and HTTP parsing.
class Tunnel : public QTcpServer {
public:
    Tunnel(const QString& host, quint16 port) : m_Host(host), m_Port(port), m_Address(resolve(host)) {
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            auto local = nextPendingConnection();
            close(); // One request/connection per tunnel, never a general proxy.
            local->setReadBufferSize(65536);
            auto remote = new QTcpSocket(local);
            remote->setProxy(QNetworkProxy::NoProxy);
            remote->setReadBufferSize(65536);
            auto header = std::make_shared<QByteArray>();
            auto started = std::make_shared<bool>(false);
            auto pump = [](QTcpSocket* from, QTcpSocket* to) {
                if (to->state() != QAbstractSocket::ConnectedState) return;
                const auto room = 65536 - to->bytesToWrite();
                if (room > 0) to->write(from->read(room));
            };
            QObject::connect(local, &QTcpSocket::readyRead, local, [=] {
                if (*started) { pump(local, remote); return; }
                *header += local->readAll();
                if (header->size() > 8192) { local->abort(); return; }
                const int end = header->indexOf("\r\n\r\n");
                if (end < 0) return;
                const auto destination = header->split(' ').value(1);
                const auto suffix = ':' + QByteArray::number(m_Port);
                const auto numeric = m_Address.toString().toUtf8();
                // Qt's HTTP socket engine also emits unbracketed IPv6 literals.
                const bool matches = destination == m_Host.toUtf8() + suffix ||
                    destination == numeric + suffix || destination == '[' + numeric + ']' + suffix;
                if (!header->startsWith("CONNECT ") || !matches || end + 4 != header->size() ||
                    !prepare(*remote, m_Address)) { local->abort(); return; }
                *started = true;
                remote->connectToHost(m_Address, m_Port);
            });
            QObject::connect(remote, &QTcpSocket::connected, local, [local] { local->write("HTTP/1.1 200 Connection Established\r\n\r\n"); });
            auto drainRemote = [=] {
                pump(remote, local);
                if (*started && remote->state() == QAbstractSocket::UnconnectedState && remote->bytesAvailable() == 0)
                    local->disconnectFromHost();
            };
            QObject::connect(remote, &QTcpSocket::readyRead, local, drainRemote);
            QObject::connect(local, &QTcpSocket::bytesWritten, local, [=](qint64) { drainRemote(); });
            QObject::connect(remote, &QTcpSocket::bytesWritten, local, [=](qint64) { pump(local, remote); });
            QObject::connect(remote, &QTcpSocket::disconnected, local, drainRemote);
            QObject::connect(remote, &QTcpSocket::errorOccurred, local, [local](QAbstractSocket::SocketError error) {
                if (error != QAbstractSocket::RemoteHostClosedError) local->abort();
            });
            QObject::connect(local, &QTcpSocket::disconnected, local, &QObject::deleteLater);
        });
        if (!m_Address.isNull()) listen(QHostAddress::LocalHost, 0);
    }
    QNetworkProxy proxy() const {
        QNetworkProxy result(QNetworkProxy::HttpProxy, "127.0.0.1", serverPort());
        result.setCapabilities(QNetworkProxy::TunnelingCapability);
        return result;
    }
private:
    QString m_Host;
    quint16 m_Port;
    QHostAddress m_Address;
};
}
