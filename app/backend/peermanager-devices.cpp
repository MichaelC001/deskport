// PeerManager members for saved devices: endpoints, edits, revocation and removal.
// Moved unchanged from peermanager.cpp.
#include "sessionfailure.h"
#include "diagnostics.h"
#include "smalltcp.h"
#include "../../shared/deskport-core/include/deskport/protocol.h"
#include "peermanager.h"
#include "peermanager-link.h"
#include "sessiongraph.h"
#include <QSysInfo>
#include <QPointer>
#include "peerstore.h"
#include "pairinginvite.h"
#include "clipboardprotocol.h"
#include "clipboard/process.h"
#ifdef Q_OS_MACOS
#include "macclipboard.h"
#endif
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>
#include "nvaddress.h"
#include "localhostfilter.h"
#include <QSslSocket>
#include <QUrl>
#include <QSslError>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QHostInfo>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <functional>
#include <QDebug>
#include <QRegularExpression>
#include <QRandomGenerator>

using namespace PeerManagerDetail;

void PeerManager::refreshEndpoints() {
    if (!m_Healthy || busy() || m_RefreshLink || m_Peers.isEmpty()) return;
    const auto keys = m_Peers.keys();
    const auto fp = keys.at(m_RefreshCursor++ % keys.size());
    if (m_RefreshCursor >= keys.size()) m_RefreshCursor = 0;
    const auto peer = m_Peers.value(fp).toObject();
    const auto address = peer["address"].toString();
    const int port = peer["bindingPort"].toInt(48991);
    if (peer["role"].toString() == "client" || !peer["ready"].toBool() || !peer["granted"].toBool() ||
        address.isEmpty() || port < 1 || port > 65535) return;
    auto link = new Link(this); link->socket = new QSslSocket(link);
    link->endpointRefresh = true; link->expectedFingerprint = fp;
    link->expectedPeer = peer; m_RefreshLink = link;
    attach(link);
    SmallTcp::connectAsync(link->socket, address, quint16(port));
    QTimer::singleShot(5000, link, [this, link] {
        if (!link->ended) fail(link, tr("Endpoint refresh timed out"));
    });
}

void PeerManager::restoreHosts() {
    if (!m_Healthy) return;
    for (const auto& peer : m_Peers) if (peer.toObject()["role"].toString() != "client" &&
        peer.toObject()["ready"].toBool() && peer.toObject()["granted"].toBool()) emit peerBound(peer.toObject().toVariantMap());
}

bool PeerManager::editPeer(const QString& fp, const QString& nameValue,
                           const QString& addressValue, int hostPort, int bindingPort) {
    auto reject = [this](const QString& message) {
        m_Status = message; emit changed(); return false;
    };
    if (busy() || m_DisplayLink || m_ClipboardLink) return reject(tr("Finish the current connection before editing this device."));
    if (!m_Peers.contains(fp)) return reject(tr("This saved device no longer exists."));
    if (m_Peers[fp].toObject()["role"].toString() == "client") return reject(tr("Client-only devices have no host endpoint to edit."));
    const auto name = nameValue.trimmed();
    QString address = addressValue.trimmed();
    if (address.startsWith('[') && address.endsWith(']')) address = address.mid(1, address.size() - 2);
    if (name.isEmpty() || name.size() > 64 || name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")))
        return reject(tr("Enter a device name between 1 and 64 characters."));
    if (dnsName(address).isEmpty() && QHostAddress(address).isNull())
        return reject(tr("Enter a domain name or IP address without a port or URL path."));
    if (hostPort < 1024 || hostPort > 65514 || bindingPort < 1 || bindingPort > 65535)
        return reject(tr("Enter valid host and binding ports."));
    auto peer = m_Peers[fp].toObject();
    peer["name"] = name; peer["customName"] = true;
    peer["address"] = address;
    peer["hostPort"] = hostPort; peer["bindingPort"] = bindingPort;
    peer["requestedAddress"] = (address.contains(':') ? "[" + address + "]" : address) + ":" + QString::number(bindingPort);
    peer.remove("resolvedAddress");
    const auto old = m_Peers;
    m_Peers[fp] = peer;
    if (!save()) { m_Peers = old; return reject(tr("Could not save device information. Please retry.")); }
    if (peer["ready"].toBool()) emit peerBound(peer.toVariantMap());
    m_Status = tr("Device information saved. Reconnect to use the updated address.");
    emit changed(); return true;
}

void PeerManager::revoke(const QString& fp) {
    if (busy() || !m_Peers.contains(fp)) return;
    beginRevocation(fp);
}

void PeerManager::removeDevice(const QString& hostId) {
    if (hostId.isEmpty()) return;
    if (!m_Healthy) { emit deviceRemovalFinished(hostId, false); return; }
    if (busy()) {
        m_Status = tr("Finish the current connection before editing this device.");
        emit changed(); emit deviceRemovalFinished(hostId, false); return;
    }
    m_RemovingDevice = hostId;
    for (auto it = m_Peers.constBegin(); it != m_Peers.constEnd(); ++it)
        if (it.value().toObject()["hostId"].toString().compare(hostId, Qt::CaseInsensitive) == 0)
            m_RemovingFingerprints.append(it.key());
    emit changed();
    QTimer::singleShot(0, this, [this] { continueDeviceRemoval(); });
}

void PeerManager::continueDeviceRemoval(bool success) {
    if (m_RemovingDevice.isEmpty()) return;
    if (success && !m_RemovingFingerprints.isEmpty()) {
        beginRevocation(m_RemovingFingerprints.takeFirst()); return;
    }
    const auto hostId = m_RemovingDevice;
    m_RemovingDevice.clear(); m_RemovingFingerprints.clear();
    emit changed(); emit deviceRemovalFinished(hostId, success);
}

void PeerManager::beginRevocation(const QString& fp) {
    if (m_ClientOnly) {
        // Forget only this local record. The remote host remains the authority
        // for revocation; never provision or restart a nonexistent local host.
        const auto previous = m_Peers;
        m_Peers.remove(fp);
        const bool success = save();
        if (!success) {
            m_Peers = previous; m_Status = tr("Could not remove the saved binding; retry");
        } else m_Status = tr("Saved binding removed. Remove the device from Devices separately; revoke access on the host to deny this client's certificate.");
        emit changed(); continueDeviceRemoval(success); return;
    }
    if (m_SessionLink && m_SessionLink->fingerprint == fp) fail(m_SessionLink, tr("Device access removed"));
    if (m_ClipboardLink && m_ClipboardLink->fingerprint == fp) fail(m_ClipboardLink, tr("Device access removed"));
    if (m_DisplayLink && m_DisplayLink->fingerprint == fp) fail(m_DisplayLink, tr("Device access removed"));
    m_Revoking = fp; m_TrustInFlight = true;
    m_Host->updatePeerTrust(trustId(fp), QString(), QSslCertificate(), true);
    emit changed();
}
