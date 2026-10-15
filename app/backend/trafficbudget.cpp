// SPDX-License-Identifier: GPL-3.0-or-later
#include "trafficbudget.h"
#include <QDate>
#include <QDateTime>
#include <QSettings>
#include <QtGlobal>

namespace {
const auto kEnabled = QStringLiteral("trafficBudget/enabled");
const auto kLimit = QStringLiteral("trafficBudget/limitGb");
const auto kDay = QStringLiteral("trafficBudget/day");
const auto kUsed = QStringLiteral("trafficBudget/used");
const auto kExceeded = QStringLiteral("trafficBudget/exceededDay");
}

TrafficBudget& TrafficBudget::instance() {
    static TrafficBudget budget;
    return budget;
}

TrafficBudget::TrafficBudget(QObject* parent, bool persistent) : QObject(parent), m_Persistent(persistent) {
    today = [] { return QDate::currentDate().toString(Qt::ISODate); };
    if (m_Persistent) {
        QSettings settings;
        m_Enabled = settings.value(kEnabled, false).toBool();
        m_LimitGb = qBound(0.1, settings.value(kLimit, 5.0).toDouble(), 10000.0);
        m_Usage.day = settings.value(kDay).toString().toStdString();
        m_Usage.used = qMax<qint64>(0, settings.value(kUsed, 0).toLongLong());
        m_ExceededDay = settings.value(kExceeded).toString();
    }
    refreshDay();
    // A day can end without any traffic; check the date once a minute.
    m_DayTimer.setInterval(60000);
    connect(&m_DayTimer, &QTimer::timeout, this, &TrafficBudget::refreshDay);
    m_DayTimer.start();
}

void TrafficBudget::setEnabled(bool value) {
    if (value == m_Enabled) return;
    const int previous = level();
    m_Enabled = value;
    save(true);
    emit changed();
    evaluate(previous);
}

void TrafficBudget::setLimitGb(double value) {
    value = qBound(0.1, value, 10000.0);
    if (qFuzzyCompare(value, m_LimitGb)) return;
    const int previous = level();
    m_LimitGb = value;
    save(true);
    emit changed();
    evaluate(previous);
}

double TrafficBudget::fraction() const {
    return limitBytes() > 0 ? double(m_Usage.used) / double(limitBytes()) : 0.0;
}

int TrafficBudget::level() const {
    return m_Enabled ? int(DeskPortTraffic::level(m_Usage.used, limitBytes())) : 0;
}

void TrafficBudget::reset() {
    m_Usage.reset();
    // A manual reset also allows data saver to be switched on again today.
    m_ExceededDay.clear();
    save(true);
    emit changed();
}

void TrafficBudget::account(const QString& session, qint64 total) {
    const int previous = level();
    const auto day = m_Usage.day;
    if (!m_Usage.account(today().toStdString(), session.toStdString(), total) && day == m_Usage.day) return;
    save(day != m_Usage.day || level() != previous);
    emit changed();
    evaluate(previous);
}

void TrafficBudget::refreshDay() {
    const auto day = m_Usage.day;
    m_Usage.rollover(today().toStdString());
    if (day != m_Usage.day) { save(true); emit changed(); }
}

void TrafficBudget::evaluate(int previousLevel) {
    Q_UNUSED(previousLevel);
    if (level() != int(DeskPortTraffic::Level::Exceeded)) return;
    const auto day = QString::fromStdString(m_Usage.day);
    if (m_ExceededDay == day) return;
    m_ExceededDay = day;
    save(true);
    emit exceeded();
}

void TrafficBudget::save(bool force) {
    if (!m_Persistent) return;
    // Counters change every second during a session; write at most every 10 s.
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - m_SavedAt < 10000) return;
    m_SavedAt = now;
    QSettings settings;
    settings.setValue(kEnabled, m_Enabled);
    settings.setValue(kLimit, m_LimitGb);
    settings.setValue(kDay, QString::fromStdString(m_Usage.day));
    settings.setValue(kUsed, qint64(m_Usage.used));
    settings.setValue(kExceeded, m_ExceededDay);
}
