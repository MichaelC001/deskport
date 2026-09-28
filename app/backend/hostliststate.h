#pragma once

#include <QSettings>
#include <QCryptographicHash>

// Keep only an irreversible ID digest to prevent automatic rediscovery after
// device records and binding credentials have been deleted.
namespace HostListState {
inline QString key(const QString& id) {
    return QStringLiteral("ui/removedHosts/") + QString::fromLatin1(QCryptographicHash::hash(id.toLower().toUtf8(), QCryptographicHash::Sha256).toHex());
}
inline bool removed(const QString& id) {
    return !id.isEmpty() && QSettings().value(key(id), false).toBool();
}
inline bool setRemoved(const QString& id, bool value) {
    if (id.isEmpty()) return false;
    QSettings settings;
    if (value) settings.setValue(key(id), true);
    else settings.remove(key(id));
    settings.sync();
    return settings.status() == QSettings::NoError;
}
inline bool admit(const QString& id, bool explicitAdd = false) {
    if (!removed(id)) return true;
    return explicitAdd && setRemoved(id, false);
}
}
