#pragma once
#include <QObject>
#include <QVariantList>
#include <QProcess>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QMap>
#include <QSet>
#include <functional>
class PeerManager;
class Session;
class ComputerManager;

// The shell owns presentation and process lifetime; a worker owns exactly one
// legacy media context. No network/decoder operation runs in the shell process.
class MultiSessions : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QVariantMap states READ states NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(QVariantMap selectedTraffic READ selectedTraffic NOTIFY trafficChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit MultiSessions(PeerManager* peers, QByteArray certificate, QByteArray key, QObject* parent = nullptr);
    ~MultiSessions();
    QVariantList sessions() const;
    QVariantMap states() const;
    QString selectedId() const { return m_Selected; }
    bool busy() const;
    QVariantMap selectedTraffic() const;
    void setDiagnosticsEnabled(bool enabled);
    Q_INVOKABLE void open(QString id, QString name, QString address, QString app = QStringLiteral("Desktop"));
    Q_INVOKABLE void select(QString id);
    Q_INVOKABLE void showDevices();
    Q_INVOKABLE void disconnectSession(QString id);
    Q_INVOKABLE void reconnect(QString id);
    Q_INVOKABLE void fullscreen(QString id = QString());
    void shutdown();
    void suspend();
signals:
    void changed();
    void trafficChanged();
    void devicesRequested();
    void viewerShown();
private:
    struct Entry {
        QString id, name, address, app, token, state = QStringLiteral("starting"), error;
        QProcess* process = nullptr;
        QPointer<QLocalSocket> socket;
        QVariantMap traffic;
        bool reserved = false;
        bool fullscreenPending = false;
        quint64 hideEpoch = 0, hideRequest = 0;
    };
    void launch(const QString& id);
    void accept();
    void send(Entry* entry, const QJsonObject& message);
    void receive(Entry* entry, const QJsonObject& message);
    void present();
    void hide(Entry* entry);
    void ended(Entry* entry, const QString& error);
    QByteArray m_Certificate, m_Key;
    PeerManager* m_Peers;
    QLocalServer m_Server;
    QMap<QString, Entry*> m_Entries;
    QString m_Selected, m_Wanted, m_Visible, m_Identity;
    QSet<QString> m_Hiding;
    quint64 m_Epoch = 0;
    bool m_Shutdown = false;
};

