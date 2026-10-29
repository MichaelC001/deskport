#pragma once
// Private to the peermanager*.cpp files: per-connection state and the
// endpoint and trust helpers they share.
#include "peermanager.h"
#include "clipboardprotocol.h"
#include "clipboard/process.h"
#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslSocket>
#include <QUrl>

namespace PeerManagerDetail {
inline QString fingerprint(const QSslCertificate& cert) { return QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex()); }
inline QString requestedHost(const QString& endpoint) {
    if (endpoint.isEmpty()) return {};
    const auto url = QUrl::fromUserInput("https://" + endpoint.trimmed());
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        url.path().size() > 1 || url.hasQuery() || url.hasFragment() ||
        url.port(48991) <= 0 || url.port(48991) > 65535) return {};
    return url.host();
}
inline QString trustId(const QString& fp) {
    return fp.mid(0,8)+"-"+fp.mid(8,4)+"-"+fp.mid(12,4)+"-"+fp.mid(16,4)+"-"+fp.mid(20,12);
}
inline QString dnsName(const QString& value) {
    const QString name = value.trimmed();
    static const QRegularExpression syntax(QStringLiteral(
        "^(?=.{1,253}$)[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*\\.?$"));
    return QHostAddress(name).isNull() && syntax.match(name).hasMatch() ? name : QString();
}
}

struct PeerManager::Link : QObject {
    using QObject::QObject;
    QSslSocket* socket = nullptr;
    QByteArray buffer;
    QJsonObject peer;
    QString transaction, fingerprint, requestedAddress;
    bool incoming = false, requested = false, accepted = false;
    bool endpointRefresh = false;
    QString expectedFingerprint;
    QJsonObject expectedPeer;
    int displayPolicy = -1;
    bool clipboardControl = false;
    ClipboardProcess* clipboardHelper = nullptr;
    QString clipboardText, clipboardEncoded;
    int clipboardMaxText = DeskPortClipboard::LegacyMaxText;
    bool clipboardSupported = false, clipboardIsText = false, clipboardDirty = true;
    qint64 clipboardNativeRevision = -1;
    int clipboardRevision = 0, clipboardSequence = 0;
    qint64 lastClipboardRequest = 0;
    bool sessionOptIn = false, sessionAdmitted = false;
    bool topologyPending = false, topologyQuery = false, topologyOptIn = false;
    QString sessionChallenge, sessionSnapshot, sessionLease;
    qint64 sessionDeadline = 0;
    quint64 sessionEpoch = 0;
    bool displayControl = false;
    bool caretUpdates = false;
    bool clientWindow = false;
    // Last full-screen state reported with display-resize; older clients do not
    // report it and keep the request available.
    bool clientFullScreen = true;
    bool videoPause = false, videoPending = false;
    int videoSequence = 0;
    QJsonObject videoResult;
    // Optional live bitrate ceiling (protocol/VIDEO_RATE.md).
    bool videoRate = false, ratePending = false;
    int rateSequence = 0;
    QJsonObject rateResult;
    bool lifecycle = false, recovering = false;
    int displaySequence = 0;
    qint64 lastDisplayRequest = 0;
    bool localReady = false, remoteReady = false, ended = false;
    bool hostReadyRequired = false, hostReadySent = false;
    int seamlessVersionAdvertised = 0;
};
