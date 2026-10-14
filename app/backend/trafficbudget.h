// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "trafficaccount.h"
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

// Daily client traffic budget shared by every session of this application.
// Usage and limit are local settings; nothing leaves this computer.
class TrafficBudget : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(double limitGb READ limitGb WRITE setLimitGb NOTIFY changed)
    Q_PROPERTY(double usedBytes READ usedBytes NOTIFY changed)
    Q_PROPERTY(double fraction READ fraction NOTIFY changed)
    // 0 normal, 1 warning (70%), 2 critical (85%), 3 exceeded (100%).
    Q_PROPERTY(int level READ level NOTIFY changed)
public:
    static TrafficBudget& instance();
    explicit TrafficBudget(QObject* parent = nullptr, bool persistent = true);
    bool enabled() const { return m_Enabled; }
    void setEnabled(bool value);
    double limitGb() const { return m_LimitGb; }
    void setLimitGb(double value);
    double usedBytes() const { return double(m_Usage.used); }
    double fraction() const;
    int level() const;
    Q_INVOKABLE void reset();
    // Cumulative received + sent bytes of one session.
    void account(const QString& session, qint64 total);
    void forget(const QString& session) { m_Usage.forget(session.toStdString()); }
    // Overridable for tests; defaults to the local calendar date.
    std::function<QString()> today;
signals:
    void changed();
    // Emitted once per day when an enabled budget is used up.
    void exceeded();
private:
    void refreshDay();
    void save(bool force);
    void evaluate(int previousLevel);
    qint64 limitBytes() const { return qint64(m_LimitGb * 1e9); }
    DeskPortTraffic::DailyUsage m_Usage;
    bool m_Enabled = false, m_Persistent = true;
    double m_LimitGb = 5.0;
    QString m_ExceededDay;
    qint64 m_SavedAt = 0;
    QTimer m_DayTimer;
};
