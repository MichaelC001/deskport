#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QSize>
#include <QTimer>

class SeamlessHostManager : public QObject {
    Q_OBJECT
public:
    explicit SeamlessHostManager(QObject* parent = nullptr);
    ~SeamlessHostManager() override;

    static QString binaryPath();
    bool available() const;
    bool running() const;
    bool ready() const { return m_Ready; }
    QString status() const { return m_Status; }
    QString socketName() const { return m_SocketName; }
    int windowCount() const { return m_Windows.size(); }
    qint64 processId() const { return m_Process.processId(); }

    bool start(const QStringList& application = {}, const QSize& size = QSize(1280, 720));
    void stop();

signals:
    void changed();
    void eventReceived(const QJsonObject& event);
    void stopped();

private:
    void consumeOutput();
    void setStatus(const QString& status);
    void finishStop();

    QProcess m_Process;
    QTimer m_KillTimer;
    QByteArray m_Output;
    QString m_Status;
    QString m_SocketName;
    bool m_Ready = false;
    bool m_Stopping = false;
    QSet<int> m_Windows;
};
