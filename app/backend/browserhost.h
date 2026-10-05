#pragma once

#include "browsergateway.h"
#include <QJsonObject>
#include <QTimer>
#include <QVector>
#include <QVariantList>
#include <functional>

class HostManager;

// Coordinates a browser transport with the same host/display admission boundary
// as native sessions. The HTTPS gateway never receives host management secrets.
class BrowserHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accessCode READ accessCode NOTIFY changed)
    Q_PROPERTY(QStringList urls READ urls NOTIFY changed)
    Q_PROPERTY(QString qrSource READ qrSource NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList pairedBrowsers READ pairedBrowsers NOTIFY changed)
public:
    explicit BrowserHost(HostManager* host, QObject* parent = nullptr,
                         const QString& directory = QString());
    ~BrowserHost();
    bool start();
    void stop();
    QString accessCode() const;
    QStringList urls() const;
    QString qrSource() const;
    QString errorString() const;
    bool listening() const;
    bool busy() const { return !m_Id.isEmpty() || m_Operation; }
    QJsonObject localInfo() const;
    QVariantList pairedBrowsers() const;
    Q_INVOKABLE bool revokeBrowser(const QString& id);
signals:
    void changed();
private:
    friend class BrowserHostTests;
    using Completion = std::function<void(QJsonObject)>;
    QJsonObject state() const;
    void request(const QJsonObject& body, QObject* context, Completion completion);
    void begin(const QJsonObject& body, Completion completion);
    void beginMedia(QJsonObject body, quint64 epoch, Completion completion);
    void end(Completion completion = {});
    void poll();
    HostManager* m_Host;
    BrowserGateway m_Gateway;
    QString m_Id;
    quint64 m_Epoch = 0;
    bool m_Operation = false, m_Ending = false, m_Polling = false;
    bool m_Cancelled = false;
    bool m_DisplayOwned = false, m_Listening = false;
    int m_Sequence = 1000000;
    QTimer m_Poll;
    QVector<Completion> m_EndWaiters;
};
