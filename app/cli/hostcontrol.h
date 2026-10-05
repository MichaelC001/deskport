#pragma once

#include <QObject>
#include <QLocalServer>
#include <QPointer>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

class HostManager;
class PeerManager;

namespace DeskPortCli {

// Arguments include argv[0]. These paths require only QCoreApplication.
bool isControlCommand(const QStringList& arguments);
int runControlCommand(const QStringList& arguments);
const char* helpText();
QString socketName(const QString& directory = QString());

class ControlServer : public QObject {
public:
    explicit ControlServer(HostManager* host, PeerManager* peers,
                           QObject* parent = nullptr, const QString& directory = QString());
    ~ControlServer();
    // The caller must already own SingleInstance for this configuration. Only
    // that owner may remove a socket left behind by an earlier process.
    bool listen();
    QString errorString() const { return m_Error.isEmpty() ? m_Server.errorString() : m_Error; }
    void setBrowserInfo(std::function<QJsonObject()> info) { m_BrowserInfo = std::move(info); }
    void setBrowserPairings(std::function<QJsonArray()> list,
                            std::function<bool(const QString&)> revoke) {
        m_BrowserPairings = std::move(list); m_RevokeBrowser = std::move(revoke);
    }
private:
    void acceptConnections();
    void dispatch(QObject* request, const QStringList& arguments);
    HostManager* m_Host;
    PeerManager* m_Peers;
    QString m_Name;
    QString m_Error;
    QLocalServer m_Server;
    QPointer<QObject> m_Mutation;
    int m_Connections = 0;
    std::function<QJsonObject()> m_BrowserInfo;
    std::function<QJsonArray()> m_BrowserPairings;
    std::function<bool(const QString&)> m_RevokeBrowser;
};

}
