#include "multisessions.h"
#include "peermanager.h"
#include "sessiongraph.h"
#include "gui/hostlayout.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUuid>

namespace {
QByteArray frame(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n'; }
}
MultiSessions::MultiSessions(PeerManager* peers, QByteArray certificate, QByteArray key, QObject* parent)
    : QObject(parent), m_Certificate(certificate), m_Key(key), m_Peers(peers) {
    m_Identity = SessionGraph::identity(QSslCertificate(m_Certificate));
    m_Server.setSocketOptions(QLocalServer::UserAccessOption);
    m_Server.listen(QStringLiteral("deskport-sessions-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    connect(&m_Server, &QLocalServer::newConnection, this, &MultiSessions::accept);
}
MultiSessions::~MultiSessions() {
    // Normal application exit waits asynchronously for shutdown(). Children also
    // exit on IPC loss, including a shell crash; do not retain detached viewers.
    for (auto entry : m_Entries) {
        if (entry->process) entry->process->disconnect(this);
        if (entry->socket) { entry->socket->disconnect(this); entry->socket->abort(); }
        if (entry->reserved) SessionGraph::release(m_Identity, entry->token);
        delete entry;
    }
}
bool MultiSessions::busy() const {
    for (auto entry : m_Entries) if (entry->process && entry->process->state() != QProcess::NotRunning) return true;
    return false;
}
QVariantMap MultiSessions::selectedTraffic() const {
    const auto entry = m_Entries.value(m_Selected);
    return entry ? entry->traffic : QVariantMap{};
}
void MultiSessions::setDiagnosticsEnabled(bool enabled) {
    for (auto entry : m_Entries) send(entry, {{"command","diagnostics"},{"enabled",enabled}});
}
QStringList MultiSessions::ordered() const {
    // Devices the saved layout does not mention follow in connection order.
    return HostLayout::load().flattened(m_Order);
}
QVariantList MultiSessions::sessions() const {
    QVariantList rows;
    for (const auto& id : ordered()) if (auto entry = m_Entries.value(id)) rows.append(QVariantMap{{"id",entry->id},{"name",entry->name},{"state",entry->state},
        {"error",entry->error},{"selected",entry->id == m_Selected},{"visible",entry->id == m_Visible}});
    return rows;
}
QVariantMap MultiSessions::states() const {
    QVariantMap result;
    for (auto entry : m_Entries) result.insert(entry->id, entry->state);
    return result;
}
void MultiSessions::open(QString id, QString name, QString address, QString app) {
    if (m_Shutdown || id.isEmpty()) return;
    id = id.toLower();
    auto entry = m_Entries.value(id);
    if (entry && (entry->state == "starting" || (entry->process && entry->process->state() != QProcess::NotRunning))) { select(id); return; }
    if (!entry) { entry = new Entry; entry->id = id; m_Entries.insert(id, entry); }
    m_Order.removeAll(id); m_Order.append(id);
    entry->name = name; entry->address = address; entry->app = app;
    entry->error.clear(); entry->state = "starting"; entry->fullscreenPending = false; entry->exposed = false;
    entry->token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    select(id);
    emit devicesRequested(); emit changed();
    QTimer::singleShot(0, this, [this, id] { launch(id); });
}
void MultiSessions::launch(const QString& id) {
    auto entry = m_Entries.value(id);
    if (!entry || m_Shutdown || entry->state != "starting" || (entry->process && entry->process->state() != QProcess::NotRunning)) return;
    const auto peer = m_Peers->sessionPeer(id);
    const QSslCertificate cert(peer.value("clientCert").toString().toUtf8());
    const int port = peer.value("bindingPort").toInt();
    if (!m_Server.isListening() || !peer.value("ready").toBool() || !peer.value("granted").toBool() || cert.isNull() || port < 1 || port > 65535 || entry->address.isEmpty()) {
        ended(entry, tr("The device binding is unavailable. Refresh the device and try again.")); return;
    }
    entry->reserved = SessionGraph::reserve(m_Identity, {entry->token,entry->address,quint16(port),cert,m_Certificate,m_Key});
    if (!entry->reserved) { ended(entry, tr("This connection would create a loop.")); return; }
    auto process = new QProcess(this); entry->process = process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("DESKPORT_SESSION_ENDPOINT", m_Server.fullServerName());
    environment.insert("DESKPORT_SESSION_TOKEN", entry->token);
    process->setProcessEnvironment(environment);
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setArguments({"--session-worker", entry->id, entry->app});
    // Drain output without persisting potentially identifying native-library text.
    connect(process, &QProcess::readyReadStandardOutput, process, [process] { process->readAllStandardOutput(); });
    connect(process, &QProcess::readyReadStandardError, process, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this,entry,process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && entry->process == process) {
            entry->process=nullptr; process->deleteLater(); ended(entry, tr("The desktop worker could not start."));
        }
    });
    connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this,entry,process](int code,QProcess::ExitStatus status) {
        ended(entry, entry->state == "stopping" ? QString() : !entry->error.isEmpty() ? entry->error :
            (status == QProcess::NormalExit && code == 0 ? QString() : tr("The desktop worker stopped unexpectedly.")));
        entry->process = nullptr; process->deleteLater(); emit changed();
    });
    process->start();
    QTimer::singleShot(15000, process, [this,entry,process] {
        if (entry->process == process && !entry->socket && process->state() != QProcess::NotRunning) {
            entry->error = tr("The desktop worker did not respond."); process->kill();
        }
    });
}
void MultiSessions::accept() {
    while (auto socket = m_Server.nextPendingConnection()) {
        socket->setReadBufferSize(16385);
        auto bytes = std::make_shared<QByteArray>();
        auto owner = std::make_shared<Entry*>(nullptr);
        connect(socket, &QLocalSocket::readyRead, this, [this,socket,bytes,owner] {
            *bytes += socket->readAll();
            if (bytes->size() > 16384) { socket->abort(); return; }
            while (bytes->contains('\n')) {
                const auto end = bytes->indexOf('\n');
                const auto message = QJsonDocument::fromJson(bytes->left(end)).object(); bytes->remove(0,end+1);
                if (!*owner) {
                    for (auto entry : m_Entries) if (entry->token == message.value("token").toString() && !entry->socket && entry->process && entry->process->state() != QProcess::NotRunning) {
                        *owner = entry; entry->socket = socket; break;
                    }
                    if (!*owner) { socket->abort(); return; }
                    const auto source=m_Peers->sessionPeer((*owner)->id);
                    QJsonObject peer;
                    for (const auto& key : {"hostId","name","hostPort","hostCert","os"}) peer[key]=source.value(key);
                    peer["address"]=(*owner)->address;
                    send(*owner,{{"command","initialize"},{"peer",peer}});
                    if ((*owner)->state == "stopping") send(*owner,{{"command","disconnect"}});
                    present();
                } else if ((*owner)->socket == socket) receive(*owner, message);
                else { socket->abort(); return; }
            }
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(5000, socket, [socket,owner] { if (!*owner) socket->abort(); });
    }
}
void MultiSessions::send(Entry* entry, const QJsonObject& message) {
    if (entry && entry->socket && entry->socket->bytesToWrite() < 16384) entry->socket->write(frame(message));
}
void MultiSessions::receive(Entry* entry, const QJsonObject& message) {
    const auto type = message.value("type").toString();
    if (type == "traffic") {
        for (const auto& key : {"received", "sent"}) {
            const auto value=message.value(key).toDouble(-1);
            if (value >= 0 && value < 1e18) entry->traffic[key]=value;
        }
        emit trafficChanged(); return;
    }
    if (type == "hidden" && quint64(message.value("epoch").toDouble()) == entry->hideEpoch) {
        if (m_Hiding.remove(entry->id)) entry->exposed = false;
        if (m_Visible == entry->id) m_Visible.clear();
        present();
    } else if (type == "devices") { showDevices(); emit devicesRequested(); }
    else if (type == "next") {
        // Only the viewer that currently owns presentation may switch it.
        if (m_Wanted == entry->id || m_Visible == entry->id) selectNext();
        return;
    }
    else if (type == "ready") { present(); }
    else if (type == "connected") { entry->state = "connected"; entry->error.clear(); }
    else if (type == "shown" && m_Wanted == entry->id && m_Hiding.isEmpty()) {
        m_Visible = entry->id;
        if (entry->fullscreenPending) { entry->fullscreenPending = false; send(entry, {{"command","fullscreen"}}); }
        emit viewerShown();
    }
    else if (type == "error") { entry->error = message.value("message").toString().left(1024); entry->state = "error"; }
    else if (type == "connecting") { entry->state = "starting"; entry->error.clear(); }
    emit changed();
}
void MultiSessions::select(QString id) {
    id = id.toLower();
    if (!m_Entries.contains(id) || m_Shutdown) return;
    const auto selected=m_Entries.value(id);
    if (selected->state != "starting" && selected->state != "connected") { emit devicesRequested(); return; }
    m_Selected = id; m_Wanted = id;
    ++m_Epoch;
    // Only a viewer that may be presenting has to release input first; waiting
    // on hidden background viewers would add their latency to every switch.
    for (auto entry : m_Entries) if (entry->id != id && entry->socket && entry->exposed) {
        hide(entry);
    }
    present(); emit changed();
}
void MultiSessions::selectNext() {
    const auto order = ordered();
    if (m_Shutdown || order.isEmpty()) return;
    const auto current = order.indexOf(m_Wanted.isEmpty() ? m_Selected : m_Wanted);
    for (int step = 1; step <= order.size(); ++step) {
        const auto entry = m_Entries.value(order.at((qMax(current, 0) + step) % order.size()));
        if (entry && entry->state == "connected" && entry->id != m_Wanted) { select(entry->id); return; }
    }
}
bool MultiSessions::recall() {
    if (m_Shutdown) return false;
    const auto last = m_Entries.value(m_Selected);
    if (last && (last->state == "connected" || last->state == "starting")) { select(last->id); return true; }
    for (const auto& id : ordered()) if (m_Entries.value(id)->state == "connected") { select(id); return true; }
    return false;
}
void MultiSessions::hide(Entry* entry) {
    // Repeated selections must not keep postponing an unresponsive worker's
    // deadline. Only process exit or an acknowledgement releases input ownership.
    const bool pending = m_Hiding.contains(entry->id);
    m_Hiding.insert(entry->id); entry->hideEpoch = m_Epoch;
    send(entry, {{"command","hide"},{"epoch",double(m_Epoch)}});
    if (pending || !entry->process) return;
    auto process = entry->process;
    const auto request = ++entry->hideRequest;
    QTimer::singleShot(2000, process, [this, entry, process, request] {
        if (entry->process == process && entry->hideRequest == request && m_Hiding.contains(entry->id)) {
            entry->error = tr("The desktop worker did not respond.");
            process->kill();
        }
    });
}
void MultiSessions::present() {
    if (m_Shutdown || !m_Hiding.isEmpty() || m_Wanted.isEmpty()) return;
    auto entry = m_Entries.value(m_Wanted);
    if (entry && entry->socket && entry->state != "stopping" && entry->state != "error") {
        entry->exposed = true;
        send(entry, {{"command","show"}});
    }
}
void MultiSessions::showDevices() {
    bool exposed = false;
    for (auto entry : m_Entries) exposed |= entry->socket && entry->exposed;
    // Repeated Devices requests (tray, top bar and worker) are idempotent.
    if (m_Wanted.isEmpty() && !exposed) return;
    m_Wanted.clear(); ++m_Epoch;
    for (auto entry : m_Entries) if (entry->socket && entry->exposed) {
        hide(entry);
    }
    emit changed();
}
void MultiSessions::disconnectSession(QString id) {
    auto entry = m_Entries.value(id.toLower()); if (!entry) return;
    id = entry->id;
    const bool leavingViewer = m_Wanted == id || m_Visible == id;
    entry->state = "stopping";
    if (leavingViewer) showDevices();
    send(entry, {{"command","disconnect"}});
    if (entry->process) {
        auto process = entry->process;
        QTimer::singleShot(7000, process, [entry,process] { if (entry->process == process && process->state() != QProcess::NotRunning) process->kill(); });
    } else ended(entry, {});
    if (leavingViewer) emit devicesRequested();
    emit changed();
}
void MultiSessions::reconnect(QString id) { auto entry=m_Entries.value(id.toLower()); if (entry) send(entry,{{"command","reconnect"}}); }
void MultiSessions::fullscreen(QString id) {
    if (id.isEmpty()) id = m_Selected;
    auto entry = m_Entries.value(id.toLower());
    if (!entry || (entry->state != "starting" && entry->state != "connected")) return;
    if (m_Visible == entry->id && m_Wanted == entry->id && m_Hiding.isEmpty()) {
        send(entry, {{"command","fullscreen"}});
    } else {
        entry->fullscreenPending = true;
        select(entry->id);
    }
}
void MultiSessions::suspend() { for (auto entry : m_Entries) disconnectSession(entry->id); }
void MultiSessions::shutdown() { m_Shutdown=true; for (auto entry : m_Entries) disconnectSession(entry->id); }
void MultiSessions::ended(Entry* entry, const QString& error) {
    if (entry->reserved) { SessionGraph::release(m_Identity,entry->token); entry->reserved=false; }
    if (entry->socket) { entry->socket->abort(); entry->socket=nullptr; }
    entry->state=error.isEmpty() ? "disconnected" : "error"; entry->error=error;
    m_Hiding.remove(entry->id); entry->exposed=false;
    if (m_Visible==entry->id) { m_Visible.clear(); emit devicesRequested(); }
    if (m_Wanted==entry->id) { m_Wanted.clear(); emit devicesRequested(); }
    present(); emit changed();
}

