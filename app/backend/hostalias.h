#pragma once

#include <QRegularExpression>
#include <QSettings>
#include <QVariantMap>

// Local display aliases keyed by host UUID. They stay on this device and are
// independent of the name a host reports or a binding stores, so polling and
// re-binding never overwrite them.
namespace HostAlias {
inline QString normalize(const QString& value)
{
    QString text = value.simplified();
    text.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
    return text.left(64);
}

inline QString get(const QString& hostId)
{
    if (hostId.isEmpty()) return {};
    return normalize(QSettings().value(QStringLiteral("ui/aliases")).toMap().value(hostId.toLower()).toString());
}

inline void set(const QString& hostId, const QString& value)
{
    if (hostId.isEmpty()) return;
    QSettings settings;
    auto aliases = settings.value(QStringLiteral("ui/aliases")).toMap();
    const QString alias = normalize(value);
    if (alias.isEmpty()) aliases.remove(hostId.toLower());
    else aliases.insert(hostId.toLower(), alias);
    if (aliases.isEmpty()) settings.remove(QStringLiteral("ui/aliases"));
    else settings.setValue(QStringLiteral("ui/aliases"), aliases);
}

inline QString displayName(const QString& hostId, const QString& name)
{
    const QString alias = get(hostId);
    return alias.isEmpty() ? name : alias;
}
}
