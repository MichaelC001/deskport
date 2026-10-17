// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtDBus>
#include <QJsonArray>
#include <QJsonObject>
#include <tuple>
template<class... T> struct Tuple { std::tuple<T...> v; };
template<class... T> QDBusArgument& operator<<(QDBusArgument& a, const Tuple<T...>& t) {
    a.beginStructure(); std::apply([&](const auto&... v) { ((a << v), ...); }, t.v); a.endStructure(); return a;
}
template<class... T> const QDBusArgument& operator>>(const QDBusArgument& a, Tuple<T...>& t) {
    a.beginStructure(); std::apply([&](auto&... v) { ((a >> v), ...); }, t.v); a.endStructure(); return a;
}
using Spec = Tuple<QString, QString, QString, QString>;
using Mode = Tuple<QString, int, int, double, double, QList<double>, QVariantMap>;
using Monitor = Tuple<Spec, QList<Mode>, QVariantMap>;
using Logical = Tuple<int, int, double, uint, bool, QList<Spec>, QVariantMap>;
using ConfigMonitor = Tuple<QString, QString, QVariantMap>;
using ConfigLogical = Tuple<int, int, double, uint, bool, QList<ConfigMonitor>>;
Q_DECLARE_METATYPE(Spec)
Q_DECLARE_METATYPE(Mode)
Q_DECLARE_METATYPE(Monitor)
Q_DECLARE_METATYPE(Logical)
Q_DECLARE_METATYPE(ConfigMonitor)
Q_DECLARE_METATYPE(ConfigLogical)


// Native adapter for the core SESSION_DISPLAY contract, not a second policy enum.
class GnomeTopology {
public:
    QString error;
    QJsonObject snapshot();
    bool apply(const QJsonObject& layout);
    bool restore(const QJsonObject& baseline, bool prepareRemoval = false);
    bool workspaceVerified(const QJsonObject& baseline, int width, int height, int scale);
    bool supported(const QJsonObject& baseline, int policy, int width, int height, int scale);
    bool configure(const QJsonObject& baseline, const QString& owned, int policy, int width, int height, int scale);
    static void registerTypes();
};
