#pragma once
#include <QObject>
#include <QVariantList>
#include <QProcess>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QElapsedTimer>
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
    // Sessions whose requested desktop enlargement is capped by the host.
    Q_PROPERTY(QStringList tuningLimited READ tuningLimited NOTIFY changed)
public:
    explicit MultiSessions(PeerManager* peers, QByteArray certificate, QByteArray key, QObject* parent = nullptr);
    ~MultiSessions();
    QVariantList sessions() const;
    QVariantMap states() const;
    QString selectedId() const { return m_Selected; }
    bool busy() const;
    QVariantMap selectedTraffic() const;
    QStringList tuningLimited() const;
    void setDiagnosticsEnabled(bool enabled);
    void setShortcutEnabled(bool enabled);
    Q_INVOKABLE void open(QString id, QString name, QString address, QString app = QStringLiteral("Desktop"));
    Q_INVOKABLE void select(QString id);
    Q_INVOKABLE void showDevices();
    Q_INVOKABLE void disconnectSession(QString id);
    Q_INVOKABLE void reconnect(QString id);
    Q_INVOKABLE void setDataSaverAll(bool on);
    // Applies a saved desktop tuning value to that desktop's running stream.
    Q_INVOKABLE void setDesktopAdjustment(QString id, double value);
    Q_INVOKABLE void fullscreen(QString id = QString());
    // Cycles the viewer to the next connected desktop, in connection order.
    Q_INVOKABLE void selectNext();
    // Shows the last viewed desktop, else the first connected one. False when
    // no desktop can be shown.
    Q_INVOKABLE bool recall();
    // The shown viewer is the window in front. Plasma takes focus from it just
    // before a tray click arrives, so focus lost a moment ago still counts.
    bool viewerInFront(bool trayClick = true) const;
    // The desktop shell's activation token for the next raised viewer.
    void setActivationToken(const QByteArray& token) { m_ActivationToken = token; }
    void shutdown();
    void suspend();
signals:
    void changed();
    void trafficChanged();
    // Cumulative received + sent bytes of one live session, and its end.
    void sessionTraffic(QString id, double total);
    void sessionClosed(QString id);
    void devicesRequested();
    void viewerShown();
    void shortcutRequested();
private:
    struct Entry {
        QString id, name, address, app, token, state = QStringLiteral("starting"), error;
        QProcess* process = nullptr;
        QPointer<QLocalSocket> socket;
        QVariantMap traffic;
        bool reserved = false;
        bool fullscreenPending = false;
        bool tuningLimited = false;
        // The viewer has been on screen before, so later shows are recovery.
        bool presented = false;
        // False only after the worker acknowledged hiding at the current epoch.
        // Workers start hidden; a hidden worker never owns input.
        bool exposed = false;
        // Keyboard focus of the worker's viewer window, for the tray toggle.
        bool focused = false;
        QElapsedTimer focusLost;
        quint64 hideEpoch = 0, hideRequest = 0;
    };
    // Session IDs in the device list's order (groups flattened in place).
    QStringList ordered() const;
    void launch(const QString& id);
    void accept();
    void send(Entry* entry, const QJsonObject& message);
    void receive(Entry* entry, const QJsonObject& message);
    // Raising is for user actions; background recovery presents quietly.
    void present(bool raise = true);
    void hide(Entry* entry);
    void ended(Entry* entry, const QString& error);
    QByteArray m_Certificate, m_Key;
    PeerManager* m_Peers;
    QLocalServer m_Server;
    QMap<QString, Entry*> m_Entries;
    QStringList m_Order;
    QString m_Selected, m_Wanted, m_Visible, m_Identity;
    QSet<QString> m_Hiding;
    QByteArray m_ActivationToken;
    quint64 m_Epoch = 0;
    bool m_Shutdown = false;
    bool m_ShortcutEnabled = false;
};

