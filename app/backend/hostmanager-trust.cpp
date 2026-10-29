// HostManager members for the host identity and peer trust updates.
// Moved unchanged from hostmanager.cpp.
#include "diagnostics.h"
#include <QSysInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#endif
#include "hostmanager.h"
#include "hostcaretmonitor.h"
#include <QScreen>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif
#include "workspaceresolution.h"
#include "peerstore.h"
#include "serviceconfig.h"
#include <QElapsedTimer>
#include <algorithm>
#include <limits>
#include <QSslError>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QNetworkProxy>
#include <QApplication>
#include <QPalette>
#include <QWindow>
#include <QMenu>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QUuid>
#include <QTimer>
#include <QThread>
#include <QMessageBox>
#include <QPushButton>
#include <QDateTime>
#include "../../host/macos/recovery-policy.h"
#include <QSettings>
#include <QPointer>
#include <QActionGroup>
#include <QRegularExpression>
#include <QHostInfo>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include "macpermissions.h"
#include "macdock.h"
#include "macunattended.h"
#endif

bool HostManager::prepareIdentity(const QByteArray& certificate, const QByteArray& key) {
    if (running() || !QDir().mkpath(m_Directory + "/credentials")) return false;
    QLockFile lock(m_Directory + "/instance.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return false;
    QFile::setPermissions(m_Directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (QFile::exists(m_Directory + "/credentials/cert.pem") != QFile::exists(m_Directory + "/credentials/key.pem")) return false;
    for (const auto& entry : {qMakePair(QString("cert.pem"), certificate), qMakePair(QString("key.pem"), key)}) {
        const QString path = m_Directory + "/credentials/" + entry.first;
        if (QFile::exists(path)) continue;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        if (file.write(entry.second) != entry.second.size() || !file.commit()) return false;
    }
    bool ok;
    auto state = PeerStore::read(m_Directory + "/state.json", &ok);
    if (!ok) return false;
    auto root = state["root"].toObject();
    if (root["uniqueid"].toString().isEmpty()) {
        root["uniqueid"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
        root["named_devices"] = QJsonArray();
        state["root"] = root;
        if (!PeerStore::write(m_Directory + "/state.json", state)) return false;
    }
    return !identity()["hostCert"].toString().isEmpty();
}

QJsonObject HostManager::identity() const {
    const auto certs = QSslCertificate::fromPath(m_Directory + "/credentials/cert.pem");
    return {{"hostId", PeerStore::read(m_Directory + "/state.json")["root"].toObject()["uniqueid"]},
            {"hostPort", m_BasePort}, {"hostCert", certs.isEmpty() ? QString() : QString::fromUtf8(certs.first().toPem())}};
}

void HostManager::sendTrustUpdate(const QJsonObject& body, qint64 deadline) {
    if (!deadline) deadline = QDateTime::currentMSecsSinceEpoch() + 3000;
    const auto generation = m_Generation;
    managementRequest(QStringLiteral("trust"), body, this, [this, body, deadline, generation](QJsonObject result) {
        // QProcess::started precedes the HTTPS listener becoming ready. Retry
        // only unavailable transport, never a helper's explicit rejection.
        if (result["code"].toString() == "unavailable" && generation == m_Generation &&
            canPair() && QDateTime::currentMSecsSinceEpoch() < deadline) {
            QTimer::singleShot(100, this, [this, body, deadline, generation] {
                if (generation == m_Generation) sendTrustUpdate(body, deadline);
                else { m_TrustBusy = false; emit trustUpdated(false); }
            });
            return;
        }
        m_TrustBusy = false;
        emit trustUpdated(result["status"].toBool());
    });
}

void HostManager::updatePeerTrust(const QString& id, const QString& name, const QSslCertificate& certificate, bool remove) {
    if (m_TrustBusy) { emit trustUpdated(false); return; }
    m_TrustBusy = true;
    if (running() && !canPair()) {
        // Startup/shutdown is asynchronous. Wait for a usable helper without
        // blocking the caller or stopping any process to edit its state file.
        auto timer = new QTimer(this);
        auto elapsed = std::make_shared<QElapsedTimer>(); elapsed->start();
        connect(timer, &QTimer::timeout, this, [=] {
            if (running() && !canPair() && elapsed->elapsed() < 10000) return;
            timer->stop(); timer->deleteLater();
            m_TrustBusy = false;
            if (running() && !canPair()) emit trustUpdated(false);
            else updatePeerTrust(id, name, certificate, remove);
        });
        timer->start(50);
        return;
    }
    if (running()) {
        // Mutate authorization in the running helper. Never restart the host
        // to edit trust, including when an older helper rejects the request.
        sendTrustUpdate({{"uuid", id}, {"name", name},
            {"cert", QString::fromUtf8(certificate.toPem())}, {"remove", remove}});
        return;
    }
    // File I/O runs off the UI thread. Starting the helper is deferred until
    // the mutation completes, and the instance lock excludes other owners.
    const auto directory = m_Directory;
    auto success = std::make_shared<bool>(false);
    auto worker = QThread::create([directory, id, name, certificate, remove, success] {
        QLockFile lock(directory + "/instance.lock"); lock.setStaleLockTime(0);
        if (lock.tryLock(0)) *success = PeerStore::trust(directory + "/state.json", id, name, certificate, remove);
    });
    connect(worker, &QThread::finished, this, [this, success] {
        m_TrustBusy = false;
        emit trustUpdated(*success);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
