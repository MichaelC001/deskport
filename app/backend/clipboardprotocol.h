#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace DeskPortClipboard {
constexpr int LegacyMaxText = 1024 * 1024;
constexpr int MaxText = 128 * 1024 * 1024;
// Base64 plus a bounded JSON envelope, including its newline.
constexpr int MaxFrame = ((MaxText + 2) / 3) * 4 + 4096;
inline int negotiatedLimit(const QJsonValue& value) {
    const int advertised = value.toInt(LegacyMaxText);
    return advertised > 0 ? qMin(advertised, MaxText) : LegacyMaxText;
}
inline bool encode(const QString& text, QString& encoded, int limit = MaxText) {
    const auto bytes = text.toUtf8();
    if (bytes.size() > qMin(limit, MaxText) || text.contains(QChar(0))) return false;
    encoded = QString::fromLatin1(bytes.toBase64());
    return true;
}
inline bool decode(const QJsonValue& value, QString& text, int limit = MaxText) {
    limit = qMin(limit, MaxText);
    if (!value.isString()) return false;
    const auto encoded = value.toString().toLatin1();
    if (encoded.size() > ((limit + 2) / 3) * 4) return false;
    const auto bytes = QByteArray::fromBase64(encoded);
    if (bytes.size() > limit || bytes.toBase64() != encoded || bytes.contains('\0')) return false;
    text = QString::fromUtf8(bytes);
    return text.toUtf8() == bytes;
}
}
