#pragma once

#include <QDBusUnixFileDescriptor>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <atomic>

// Holds a logind delay inhibitor so an active stream can release remote input
// and stop its transport before the client suspends or hibernates.
class SleepMonitor : public QObject
{
    Q_OBJECT
public:
    explicit SleepMonitor(QObject* parent = nullptr);
    // Allow a pending sleep to continue. Reacquired after resume.
    void release();

signals:
    void sleeping();
    void resumed();

private slots:
    void prepareForSleep(bool start);

private:
    void acquire();
    QDBusUnixFileDescriptor m_Inhibitor;
};

// Follows logind's LockedHint of the user's graphical session, which the
// desktop's screen locker sets while the screen is locked. DeskPort runs as a
// user service outside any session, so the session comes from the user's
// Display property rather than "auto".
class ScreenLockMonitor : public QObject
{
    Q_OBJECT
public:
    explicit ScreenLockMonitor(QObject* parent = nullptr);
    static bool locked() { return s_Locked.load(); }

private slots:
    void propertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated);

private:
    void refresh();
    QString m_Session;
    static std::atomic<bool> s_Locked;
};
