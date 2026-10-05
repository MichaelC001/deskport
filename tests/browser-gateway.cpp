#include "browsergateway.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTextStream>
#include <unistd.h>

static void report(const QJsonObject& object) {
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    (void)!::write(STDOUT_FILENO, bytes.constData(), size_t(bytes.size()));
}
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("DeskPort Browser Isolated Test");
    bool sharing = true;
    QString owner;
    int starts = 0, stops = 0;
    bool deferStart = false;
    std::function<void(QJsonObject)> deferred;
    QJsonObject last;
    BrowserGateway::Options options;
    options.stateDirectory = QString::fromLocal8Bit(argv[1]);
    options.listenAddresses = {QHostAddress::LocalHost};
    if (qEnvironmentVariableIsSet("DESKPORT_TEST_PARTIAL_BIND")) options.listenAddresses.append(QHostAddress::LocalHost);
    if (qEnvironmentVariableIsSet("DESKPORT_TEST_ALLOWED_HOSTS"))
        options.allowedHosts = qEnvironmentVariable("DESKPORT_TEST_ALLOWED_HOSTS").split(',');
    options.port = argc > 2 ? quint16(QString::fromLocal8Bit(argv[2]).toInt()) : 0;
    options.mediaIdleSeconds = 2;
    if (argc > 4) { options.certificatePath = QString::fromLocal8Bit(argv[3]); options.privateKeyPath = QString::fromLocal8Bit(argv[4]); }
    BrowserGateway::Hooks hooks;
    hooks.state = [&] { return QJsonObject{{"sharing", sharing}, {"hostName", "Isolated host"},
        {"mediaAvailable", true}, {"busy", !owner.isEmpty()}, {"privateSecret", "must-never-be-served"}}; };
    hooks.request = [&](const QJsonObject& body, QObject*, std::function<void(QJsonObject)> completion) {
        const auto action = body.value("action").toString(), id = body.value("id").toString();
        last = body;
        if (action == "start") {
            if (!owner.isEmpty()) { completion({{"status", false}, {"code", "busy"}}); return; }
            owner = id; ++starts;
            if (deferStart) deferred = completion;
            else completion({{"status", true}, {"type", "offer"}, {"sdp", "v=0\r\n"}, {"id", id}, {"lease", "internal"}});
        } else if (action == "stop") {
            if (owner == id) { owner.clear(); ++stops; }
            completion({{"status", true}});
        } else if (owner != id) completion({{"status", false}, {"code", "no-session"}});
        else completion({{"status", true}, {"state", "connected"}});
    };
    BrowserGateway gateway(hooks, options);
    if (!gateway.start()) { report({{"error", gateway.errorString()}, {"active", gateway.active()},
        {"port", gateway.port()}, {"urlCount", gateway.urls().size()}}); return 2; }
    report({{"port", gateway.port()}, {"code", gateway.accessCode()}, {"urls", QJsonArray::fromStringList(gateway.urls())}});
    QByteArray input;
    QSocketNotifier notifier(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &application, [&] {
        char bytes[4096]; const auto size = ::read(STDIN_FILENO, bytes, sizeof(bytes));
        if (size <= 0) { application.quit(); return; }
        input.append(bytes, int(size));
        int end;
        while ((end = input.indexOf('\n')) >= 0) {
            const auto command = input.left(end); input.remove(0, end + 1);
            if (command == "quit") { application.quit(); return; }
            if (command == "off") { sharing = false; gateway.hostStateChanged(); }
            if (command == "on") sharing = true;
            if (command == "reset") gateway.resetAccessCode();
            bool revoked = false;
            if (command.startsWith("revoke ")) revoked = gateway.revokeBrowser(QString::fromUtf8(command.mid(7)));
            if (command == "defer") deferStart = true;
            if (command == "complete" && deferred) {
                auto completion = deferred; deferred = {}; deferStart = false;
                completion({{"status", true}, {"type", "offer"}, {"sdp", "v=0\r\n"}});
            }
            report({{"starts", starts}, {"stops", stops}, {"active", !owner.isEmpty()},
                {"code", gateway.accessCode()}, {"last", last}, {"pairings", gateway.pairedBrowsers()},
                {"revoked", revoked}, {"error", gateway.errorString()}});
        }
    });
    return application.exec();
}
