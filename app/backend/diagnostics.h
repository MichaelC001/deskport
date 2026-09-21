#pragma once
#include <QObject>
#include <QMutex>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QUrl>

// The only application-owned diagnostic file sink. Raw messages never reach disk.
class Diagnostics : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString bundlePath READ bundlePath NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit Diagnostics(QObject* parent = nullptr, const QString& directory = QString());
    static Diagnostics& instance();
    bool enabled() const;
    void startMaintenance();
    void setEnabled(bool enabled);
    void ingest(const QString& source, const QByteArray& bytes);
    void record(const QString& source, const QString& message);
    void connection(int phase, int error, const QString& state);
    QString bundlePath() const { return m_Bundle; }
    QString status() const { return m_Status; }
    Q_INVOKABLE bool feedback();
    Q_INVOKABLE void showBundle();
    Q_INVOKABLE void clear();
    QString createBundle();
    static QUrl issueUrl();
    static QJsonObject project(const QString& message);
    static QJsonObject validate(const QJsonObject& object);
    static constexpr qint64 FileLimit = 1024 * 1024;
signals:
    void changed();
private:
    void write(const QString& source, QJsonObject event);
    void prune();
    QString m_Directory, m_Run, m_Bundle, m_Status;
    bool m_Enabled;
    bool m_MaintenanceStarted = false;
    mutable QMutex m_Mutex;
    QElapsedTimer m_Time;
    QHash<QString, QByteArray> m_Pending;
};
