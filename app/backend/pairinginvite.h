#pragma once

#include <QHostAddress>
#include <QRegularExpression>
#include <QString>

namespace PairingInvite {
// An explicitly supplied connection entry, never a guessed public/NAT address.
inline QString entry(const QString& value, int defaultPort) {
    if (value.isEmpty() || value.size() > 320 || value != value.trimmed()) return {};
    static const QRegularExpression syntax(QStringLiteral("^(?:\\[([^\\]]+)\\]|([^:\\s/?#@%]+))(?::([0-9]{1,5}))?$"));
    const auto match = syntax.match(value);
    if (!match.hasMatch()) return {};
    QString host = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    const QHostAddress numeric(host);
    if (!match.captured(1).isEmpty()) {
        if (numeric.protocol() != QAbstractSocket::IPv6Protocol || host.contains('%')) return {};
        host = '[' + numeric.toString() + ']';
    } else if (!numeric.isNull()) {
        if (numeric.protocol() != QAbstractSocket::IPv4Protocol) return {};
        host = numeric.toString();
    } else {
        // A failed numeric IPv4 parse must not become a DNS name: the mobile
        // entry parsers reject it, so emitting an invitation would be unusable.
        static const QRegularExpression numericHost(QStringLiteral("^[0-9.]+$"));
        if (numericHost.match(host).hasMatch()) return {};
        static const QRegularExpression dns(QStringLiteral("^(?=.{1,253}$)[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*\\.?$"));
        if (!dns.match(host).hasMatch()) return {};
        host = host.toLower();
    }
    const int port = match.captured(3).isEmpty() ? defaultPort : match.captured(3).toInt();
    if (port < 1 || port > 65535) return {};
    return host + ':' + QString::number(port);
}
}
