#include "hostcontrol.h"
#include "backend/hostmanager.h"
#include "backend/peermanager.h"
#include "backend/pairinginvite.h"
#include "terminalqr.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QDateTime>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace DeskPortCli {
namespace {
constexpr int MaxRequest = 8192;
constexpr int MaxResponse = 1024 * 1024;
constexpr int OperationTimeout = 65000;
constexpr int ClientTimeout = OperationTimeout + 5000;

bool privateEndpoint(const QString& name, bool create) {
#ifdef Q_OS_UNIX
    const auto directory = QFile::encodeName(QFileInfo(name).absolutePath());
    if (create && ::mkdir(directory.constData(), 0700) != 0 && errno != EEXIST) return false;
    struct stat state {};
    // macOS does not enforce Unix socket file permissions. The containing
    // directory therefore provides the access boundary on every Unix platform.
    // lstat rejects symlinks, including ones pre-created by another /tmp user.
    return ::lstat(directory.constData(), &state) == 0 && S_ISDIR(state.st_mode) &&
        state.st_uid == ::getuid() && (state.st_mode & 0777) == 0700;
#else
    Q_UNUSED(name); Q_UNUSED(create);
    return true;
#endif
}

QJsonObject failure(const QString& code, const QString& error) {
    return {{"version", 1}, {"ok", false}, {"code", code}, {"error", error}};
}
QJsonObject success(const QJsonObject& data = {}) {
    return {{"version", 1}, {"ok", true}, {"data", data}};
}

QString validate(const QStringList& args) {
    if (args == QStringList{"status"} || args == QStringList{"config", "get"} ||
        args == QStringList{"sharing", "start"} || args == QStringList{"sharing", "stop"} ||
        args == QStringList{"devices", "list"} || args == QStringList{"devices", "pending"} ||
        args == QStringList{"devices", "revoke-invite"} || args == QStringList{"web", "info"} ||
        args == QStringList{"web", "list"}) return {};
    if (args.size() == 3 && args[0] == "web" && args[1] == "remove" &&
        !args[2].isEmpty() && args[2].size() <= 128) return {};
    if (args.size() == 4 && args[0] == "devices" && args[1] == "invite" && args[2] == "--address") {
        if (!PairingInvite::entry(args[3], 48991).isEmpty()) return {};
        return QStringLiteral("Use --address HOST[:PORT] or [IPv6]:PORT with a reachable connection entry.");
    }
    if (args.size() == 3 && args[0] == "devices" &&
        (args[1] == "approve" || args[1] == "reject" || args[1] == "remove") &&
        !args[2].isEmpty() && args[2].size() <= 128) return {};
    if (args.size() == 4 && args[0] == "config" && args[1] == "set") {
        if (args[2] == "name") {
            if (args[3].trimmed().isEmpty() || args[3].size() > 64)
                return QStringLiteral("Device name must contain 1 to 64 characters.");
            for (const QChar ch : args[3])
                if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
                    return QStringLiteral("Device name must not contain control characters.");
            return {};
        }
        if (args[2] == "port") {
            bool numeric = false;
            const int port = args[3].toInt(&numeric);
            if (numeric && port >= 1024 && port <= 65535) return {};
            return QStringLiteral("Connection port must be an integer between 1024 and 65535.");
        }
    }
    return QStringLiteral("Unknown command or arguments. Run deskport --help for CLI usage.");
}

// One command per connection. An in-flight operation outlives a disconnected
// client so another client cannot race a partially completed mutation.
class Request : public QObject {
public:
    Request(QLocalSocket* client, QObject* parent) : QObject(parent), socket(client) {
        timeout.setParent(this);
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, this, [this] {
            finish(failure("timeout", "Command timed out; inspect status before retrying."));
        });
        timeout.start(3000);
    }
    void finish(const QJsonObject& result) {
        if (finished) return;
        finished = true;
        timeout.stop();
        if (socket && socket->state() == QLocalSocket::ConnectedState) {
            auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
            if (bytes.size() > MaxResponse)
                bytes = QJsonDocument(failure("too-large", "Response exceeded the local control limit.")).toJson(QJsonDocument::Compact) + '\n';
            socket->write(bytes);
            socket->disconnectFromServer();
            QTimer::singleShot(2000, socket, &QObject::deleteLater);
        }
        deleteLater();
    }
    QPointer<QLocalSocket> socket;
    QTimer timeout;
    bool received = false;
    bool finished = false;
};

QJsonObject device(const QVariantMap& peer) {
    // Never serialize peer metadata wholesale: it contains certificates.
    QJsonObject result;
    for (const char* key : {"name", "hostId", "fingerprint", "role", "address", "bindingPort", "ready", "granted"}) {
        const auto found = peer.constFind(QString::fromLatin1(key));
        if (found != peer.constEnd()) result[QString::fromLatin1(key)] = QJsonValue::fromVariant(*found);
    }
    result["id"] = peer.value("hostId").toString().isEmpty() ? peer.value("fingerprint").toString() : peer.value("hostId").toString();
    return result;
}
QJsonObject config(HostManager* host, PeerManager* peers) {
    return {{"name", host->deviceName()}, {"port", peers->port()}};
}
QJsonObject status(HostManager* host, PeerManager* peers) {
    return {{"name", host->deviceName()}, {"running", host->running()}, {"ready", host->canPair()},
            {"changing", host->changing()}, {"available", host->available()}, {"status", host->status()},
            {"readiness", host->readiness()}, {"displayWarning", host->displayWarning()},
            {"bindingStatus", peers->status()},
            {"port", peers->port()}, {"hostPort", host->basePort()},
            {"pendingRequestId", peers->requestId()}, {"devices", peers->peers().size()}};
}
QString printable(QString value) {
    for (int i = 0; i < value.size(); ++i)
        if (value[i].unicode() < 0x20 || value[i].unicode() == 0x7f) value[i] = ' ';
    return value;
}
void printHuman(const QJsonObject& result) {
    QTextStream output(stdout);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    output.setCodec("UTF-8");
#endif
    const auto data = result["data"].toObject();
    if (data.contains("accessCode") && data.contains("urls")) {
        output << "Browser access: " << (data["enabled"].toBool() ? "enabled" : "unavailable") << '\n';
        const auto urls = data["urls"].toArray();
        if (!urls.isEmpty()) output << terminalQr(urls.first().toString());
        for (const auto& url : urls) output << printable(url.toString()) << '\n';
        output << "Access code: " << printable(data["accessCode"].toString()) << '\n';
        output << "Enter this code in the browser. It stays valid across restarts.\n";
        if (!data["error"].toString().isEmpty()) output << printable(data["error"].toString()) << '\n';
        return;
    }
    if (data.contains("browsers")) {
        const auto browsers = data["browsers"].toArray();
        if (browsers.isEmpty()) output << "No paired browsers.\n";
        for (const auto& value : browsers) {
            const auto row = value.toObject();
            output << printable(row["id"].toString()) << '\t' << printable(row["name"].toString()) << '\n';
        }
        return;
    }
    if (data.contains("uri")) {
        int columns = 0;
        const auto qr = terminalQr(data["uri"].toString(), &columns);
        int terminalWidth = 0;
#ifdef Q_OS_UNIX
        struct winsize size {};
        if (::isatty(STDOUT_FILENO) && ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) terminalWidth = size.ws_col;
#endif
        output << "QR requires " << columns << " terminal columns; keep all rows unwrapped.\n";
        if (terminalWidth > 0 && terminalWidth < columns)
            output << "Terminal is too narrow. Widen it and create a new invitation, or use the link below.\n";
        else output << qr;
        output << data["uri"].toString() << '\n';
        output << "Expires: " << QDateTime::fromSecsSinceEpoch(qint64(data["expiresAt"].toDouble())).toUTC().toString(Qt::ISODate) << '\n';
        output << "Scan with DeskPort, check the host, then confirm. This grants one client access.\n";
        return;
    }
    if (data.contains("devices") && data["devices"].isArray()) {
        const auto devices = data["devices"].toArray();
        if (devices.isEmpty()) output << "No saved devices.\n";
        for (const auto& value : devices) {
            const auto row = value.toObject();
            output << printable(row["id"].toString()) << '\t' << printable(row["name"].toString())
                   << '\t' << (row["ready"].toBool() ? "ready" : "incomplete") << '\n';
        }
    } else {
        for (auto it = data.begin(); it != data.end(); ++it) {
            const auto value = it.value();
            output << it.key() << ": " << printable(value.isBool() ? (value.toBool() ? "true" : "false") : value.toVariant().toString()) << '\n';
        }
        if (data.isEmpty()) output << "Done.\n";
    }
}
int printResult(const QJsonObject& result, bool json, int errorCode = 1) {
    if (json) QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    else if (!result["ok"].toBool()) QTextStream(stderr) << printable(result["error"].toString()) << '\n';
    else printHuman(result);
    return result["ok"].toBool() ? 0 : errorCode;
}
}

const char* helpText() {
    return "Local SSH control (the DeskPort GUI or daemon must already be running):\n"
           "  deskport status [--json]\n"
           "  deskport sharing start|stop [--json]\n"
           "  deskport web info [--json]\n"
           "  deskport web list [--json]\n"
           "  deskport web remove BROWSER_ID [--json]\n"
           "  deskport devices list|pending [--json]\n"
           "  deskport devices invite --address HOST[:PORT] [--json]\n"
           "  deskport devices revoke-invite [--json]\n"
           "  deskport devices approve|reject REQUEST_ID [--json]\n"
           "  deskport devices remove DEVICE_ID [--json]\n"
           "  deskport config get [--json]\n"
           "  deskport config set name NAME [--json]\n"
           "  deskport config set port PORT [--json]\n"
           "Use the same OS user and configuration as the running instance.\n"
           "Approve/reject requires the exact request ID shown by devices pending.\n"
           "An invitation expires after five minutes and is consumed only on client confirmation.\n"
           "The connection port accepts 1024..65535; stop sharing before renaming.\n";
}
bool isControlCommand(const QStringList& arguments) {
    return arguments.size() > 1 && QStringList{"status", "sharing", "devices", "config", "web"}.contains(arguments[1]);
}
QString socketName(const QString& directory) {
    const auto path = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : directory;
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(QDir(path).absolutePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
#ifdef Q_OS_UNIX
    // Keep below sockaddr_un's macOS limit even for very long config paths.
    // /tmp is stable across GUI and SSH sessions with different TMPDIR values.
    return QString("/tmp/deskport-control-%1-%2/control.sock").arg(qulonglong(::getuid())).arg(hash);
#else
    return "deskport-control-" + hash;
#endif
}
int runControlCommand(const QStringList& arguments) {
    auto args = arguments.mid(1);
    const bool json = args.removeAll("--json") > 0;
    if (args.contains("--help") || args.contains("-h") || args == QStringList{"help"}) {
        QTextStream(stdout) << helpText();
        return 0;
    }
    const auto error = validate(args);
    if (!error.isEmpty()) return printResult(failure("usage", error), json, 2);
    QJsonArray values;
    for (const auto& value : args) values.append(value);
    const auto frame = QJsonDocument(QJsonObject{{"version", 1}, {"args", values}}).toJson(QJsonDocument::Compact) + '\n';
    if (frame.size() > MaxRequest) return printResult(failure("usage", "Command exceeds the local control limit."), json, 2);
    QLocalSocket socket;
    socket.setReadBufferSize(MaxResponse + 1);
    const auto name = socketName();
    if (!privateEndpoint(name, false))
        return printResult(failure("unavailable", "DeskPort has no secure local endpoint for this user and configuration. Start the GUI or configured daemon first."), json);
    socket.connectToServer(name);
    if (!socket.waitForConnected(2000))
        return printResult(failure("unavailable", "DeskPort is not running for this user and configuration. Start the GUI or configured daemon first."), json);
    if (socket.write(frame) != frame.size() || (socket.bytesToWrite() && !socket.waitForBytesWritten(2000)))
        return printResult(failure("transport", "Could not send the command to DeskPort."), json);
    QElapsedTimer deadline;
    deadline.start();
    while (!socket.canReadLine() && socket.bytesAvailable() <= MaxResponse && deadline.elapsed() < ClientTimeout) {
        if (socket.state() == QLocalSocket::UnconnectedState) break;
        socket.waitForReadyRead(qMin(1000, ClientTimeout - int(deadline.elapsed())));
    }
    if (socket.bytesAvailable() > MaxResponse)
        return printResult(failure("protocol", "DeskPort returned an oversized response."), json);
    if (!socket.canReadLine())
        return printResult(failure("timeout", "DeskPort did not complete the command; inspect status before retrying."), json);
    QJsonParseError parseError;
    const auto result = QJsonDocument::fromJson(socket.readLine(MaxResponse + 1), &parseError).object();
    if (parseError.error != QJsonParseError::NoError || result["version"].toInt() != 1 || !result["ok"].isBool())
        return printResult(failure("protocol", "DeskPort returned an invalid control response."), json);
    return printResult(result, json);
}

ControlServer::ControlServer(HostManager* host, PeerManager* peers, QObject* parent, const QString& directory)
    : QObject(parent), m_Host(host), m_Peers(peers), m_Name(socketName(directory)) {
    m_Server.setSocketOptions(QLocalServer::UserAccessOption);
    m_Server.setMaxPendingConnections(16);
    connect(&m_Server, &QLocalServer::newConnection, this, [this] { acceptConnections(); });
}
ControlServer::~ControlServer() {
#ifdef Q_OS_UNIX
    if (m_Server.isListening()) {
        m_Server.close();
        QDir().rmdir(QFileInfo(m_Name).absolutePath());
    }
#endif
}
bool ControlServer::listen() {
    if (m_Server.isListening()) return true;
    if (!privateEndpoint(m_Name, true)) {
        m_Error = "The local control directory must be owned by this user, private (0700), and not a symlink.";
        return false;
    }
    m_Error.clear();
    QLocalServer::removeServer(m_Name);
    return m_Server.listen(m_Name);
}
void ControlServer::acceptConnections() {
    while (auto socket = m_Server.nextPendingConnection()) {
        if (m_Connections >= 16) { socket->abort(); socket->deleteLater(); continue; }
        ++m_Connections;
        auto request = new Request(socket, this);
        connect(request, &QObject::destroyed, this, [this] { --m_Connections; });
        socket->setReadBufferSize(MaxRequest + 1);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        const auto receive = [this, request] {
            auto socket = request->socket.data();
            if (!socket || request->received || request->finished) return;
            if (socket->bytesAvailable() > MaxRequest) {
                request->finish(failure("too-large", "Request exceeded the local control limit.")); return;
            }
            if (!socket->canReadLine()) return;
            request->received = true;
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(socket->readLine(MaxRequest + 1), &error);
            const auto object = document.object();
            if (error.error != QJsonParseError::NoError || !document.isObject() || object["version"].toInt() != 1 || !object["args"].isArray()) {
                request->finish(failure("protocol", "Expected a version 1 local control request.")); return;
            }
            const auto values = object["args"].toArray();
            if (values.size() > 4) { request->finish(failure("usage", "Too many arguments.")); return; }
            QStringList arguments;
            for (const auto& value : values) {
                if (!value.isString()) { request->finish(failure("protocol", "Arguments must be strings.")); return; }
                arguments.append(value.toString());
            }
            const auto invalid = validate(arguments);
            if (!invalid.isEmpty()) { request->finish(failure("usage", invalid)); return; }
            request->timeout.start(OperationTimeout);
            dispatch(request, arguments);
        };
        connect(socket, &QLocalSocket::readyRead, request, receive);
        receive();
    }
}
void ControlServer::dispatch(QObject* context, const QStringList& args) {
    auto request = static_cast<Request*>(context);
    if (args == QStringList{"web", "info"}) {
        request->finish(m_BrowserInfo ? success(m_BrowserInfo()) : failure("unavailable", "Browser access is unavailable in this instance."));
        return;
    }
    if (args == QStringList{"web", "list"}) {
        request->finish(m_BrowserPairings ? success({{"browsers", m_BrowserPairings()}}) :
            failure("unavailable", "Browser pairing management is unavailable in this instance."));
        return;
    }
    if (args[0] == "status") { request->finish(success(status(m_Host, m_Peers))); return; }
    if (args == QStringList{"config", "get"}) { request->finish(success(config(m_Host, m_Peers))); return; }
    if (args == QStringList{"devices", "list"}) {
        QJsonArray devices;
        for (const auto& peer : m_Peers->peers()) devices.append(device(peer.toMap()));
        request->finish(success({{"devices", devices}})); return;
    }
    if (args == QStringList{"devices", "pending"}) {
        request->finish(success({{"requestId", m_Peers->requestId()}, {"description", m_Peers->pendingName()},
                                 {"clientOnly", m_Peers->pendingClientOnly()}})); return;
    }
    if (m_Mutation) { request->finish(failure("busy", "Another local control operation is still in progress.")); return; }
    m_Mutation = request;
    if (args.size() == 3 && args[0] == "web" && args[1] == "remove") {
        if (!m_BrowserPairings || !m_RevokeBrowser) {
            request->finish(failure("unavailable", "Browser pairing management is unavailable in this instance.")); return;
        }
        bool found = false;
        for (const auto& browser : m_BrowserPairings())
            if (browser.toObject()["id"].toString() == args[2]) { found = true; break; }
        if (!found) { request->finish(failure("not-found", "No paired browser matches that exact ID. Run web list again.")); return; }
        request->finish(m_RevokeBrowser(args[2]) ? success({{"removed", args[2]}}) :
            failure("remove-failed", "Cannot save the browser pairing removal. Check the local browser settings directory and retry."));
        return;
    }
    if (args == QStringList{"devices", "revoke-invite"}) {
        request->finish(success({{"revoked", m_Peers->revokeInvitation()}})); return;
    }
    if (args.size() == 4 && args[0] == "devices" && args[1] == "invite") {
        const auto invitation = m_Peers->createInvitation(args[3]);
        request->finish(invitation.isEmpty() ? failure("invite-failed", m_Peers->status()) : success(invitation)); return;
    }
    if (args[0] == "sharing") {
        if (m_Host->changing() || m_Peers->busy()) { request->finish(failure("busy", "Host or device access is changing; retry after it completes.")); return; }
        const bool start = args[1] == "start";
        if (start && m_Host->canPair()) { request->finish(success(status(m_Host, m_Peers))); return; }
        if (start && !m_Host->available()) { request->finish(failure("unavailable", "The bundled sharing host is unavailable.")); return; }
        const auto completed = [this, request, start] {
            if (request->finished || m_Host->changing()) return;
            if (start && m_Host->canPair()) request->finish(success(status(m_Host, m_Peers)));
            else if (!m_Host->running()) request->finish(start ? failure("start-failed", m_Host->status()) : success(status(m_Host, m_Peers)));
        };
        connect(m_Host, &HostManager::changed, request, completed);
        if (start) m_Host->start(QSettings().value("host/width", 2560).toInt(), QSettings().value("host/height", 1440).toInt());
        else m_Host->stop();
        completed(); return;
    }
    if (args[0] == "config") {
        if (m_Host->changing() || m_Peers->busy()) { request->finish(failure("busy", "Host or device access is changing; retry after it completes.")); return; }
        const bool ok = args[2] == "name" ? m_Host->setDeviceName(args[3]) : m_Peers->setConnectionPort(args[3].toInt());
        request->finish(ok ? success(config(m_Host, m_Peers)) : failure("config-failed", args[2] == "name" ? m_Host->status() : m_Peers->status())); return;
    }
    if (args[1] == "approve" || args[1] == "reject") {
        const auto id = args[2];
        if (m_Peers->requestId().isEmpty() || m_Peers->requestId() != id) {
            request->finish(failure("stale-request", "No pending request matches that exact ID. Run devices pending again.")); return;
        }
        const bool approve = args[1] == "approve";
        connect(m_Peers, &PeerManager::bindingFinished, request, [this, request, id, approve](const QString& transaction, bool ok) {
            if (transaction != id) return;
            request->finish(ok || !approve ? success({{"requestId", id}, {"approved", approve}}) : failure("approval-failed", m_Peers->status()));
        });
        if (approve) m_Peers->approve(id); else m_Peers->reject(id);
        return;
    }
    const auto id = args[2];
    if (m_Peers->busy() || m_Host->changing()) { request->finish(failure("busy", "Host or device access is changing; retry after it completes.")); return; }
    QString fingerprint, hostId;
    for (const auto& value : m_Peers->peers()) {
        const auto peer = value.toMap();
        if (peer["hostId"].toString().compare(id, Qt::CaseInsensitive) == 0 || peer["fingerprint"].toString() == id) {
            fingerprint = peer["fingerprint"].toString(); hostId = peer["hostId"].toString(); break;
        }
    }
    if (fingerprint.isEmpty()) { request->finish(failure("not-found", "No saved device matches that ID.")); return; }
    if (!hostId.isEmpty()) {
        connect(m_Peers, &PeerManager::deviceRemovalFinished, request, [this, request, hostId](const QString& removed, bool ok) {
            if (removed != hostId) return;
            request->finish(ok ? success({{"removed", hostId}}) : failure("remove-failed", m_Peers->status()));
        });
        m_Peers->removeDevice(hostId);
    } else {
        const auto completed = [this, request, fingerprint] {
            if (request->finished || m_Peers->busy()) return;
            for (const auto& peer : m_Peers->peers()) {
                if (peer.toMap()["fingerprint"].toString() == fingerprint) {
                    request->finish(failure("remove-failed", m_Peers->status())); return;
                }
            }
            request->finish(success({{"removed", fingerprint}}));
        };
        connect(m_Peers, &PeerManager::changed, request, completed);
        m_Peers->revoke(fingerprint);
        completed();
    }
}
}
