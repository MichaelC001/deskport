#include "hostdaemon.h"
#include "hostcontrol.h"
#include "backend/hostmanager.h"
#include "backend/browserhost.h"
#include "backend/peermanager.h"
#include "backend/identitymanager.h"
#include "backend/singleinstance.h"
#include "path.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QSocketNotifier>
#include <QTimer>
#ifdef Q_OS_UNIX
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
namespace {
int signalPipe[2] = {-1, -1};
void signalHandler(int) { const char value = 1; (void)!write(signalPipe[1], &value, 1); }
}
#endif

int DeskPortCli::runHostDaemon(int& argc, char** argv) {
    // The host uses Qt widgets internally, but it never needs a viewer or a
    // tray. Preserve WAYLAND_DISPLAY for the native capture child process.
    const bool hadPlatform = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
    const auto childPlatform = qgetenv("QT_QPA_PLATFORM");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // The supervisor's platform is fixed now. Clipboard helpers must still
    // connect to the user's actual Wayland/X11 session instead of inheriting
    // our offscreen override.
    if (hadPlatform) qputenv("QT_QPA_PLATFORM", childPlatform);
    else qunsetenv("QT_QPA_PLATFORM");
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription("Run the DeskPort host in the foreground, without a window.\n"
        "A desktop compositor, capture backend, audio and encoder must already be available.\n"
        "Use this with a user service; manage it over SSH with deskport status/devices/sharing.");
    parser.addHelpOption();
    parser.addPositionalArgument("host", "Local host commands");
    parser.addPositionalArgument("run", "Run the foreground host");
    parser.addOption({"no-share", "Start the management endpoint with sharing disabled."});
    parser.addOption({"no-web-server", "Disable the browser HTTPS listener for this run."});
    if (!parser.parse(app.arguments()) || parser.positionalArguments() != QStringList({"host", "run"})) {
        fprintf(stderr, "%s\n%s", qPrintable(parser.errorText()), qPrintable(parser.helpText()));
        return 2;
    }
    if (parser.isSet("help")) { fputs(qPrintable(parser.helpText()), stdout); return 0; }
#ifndef Q_OS_LINUX
    fputs("The foreground host is currently supported on Linux. Use the desktop app on this platform.\n", stderr);
    return 2;
#else
    SingleInstance instance;
    if (!instance.start(QString(), false)) {
        fputs("A DeskPort instance already owns this configuration, or its lock is unavailable. Use deskport status.\n", stderr);
        return 3;
    }
    HostManager host(nullptr, QString(), false);
    auto identity = IdentityManager::get();
    PeerManager peers(&host, identity->getCertificate(), identity->getPrivateKey());
    ControlServer control(&host, &peers);
    BrowserHost browserHost(&host);
    if (!parser.isSet("no-web-server")) browserHost.start();
    control.setBrowserInfo([&browserHost] { return browserHost.localInfo(); });
    control.setBrowserPairings([&browserHost] { return QJsonArray::fromVariantList(browserHost.pairedBrowsers()); },
                              [&browserHost](const QString& id) { return browserHost.revokeBrowser(id); });
    if (!control.listen()) {
        fprintf(stderr, "Cannot open the local management endpoint: %s\n", qPrintable(control.errorString()));
        return 3;
    }
    QObject::connect(&host, &HostManager::changed, &app, [&] {
        fprintf(stderr, "%s\n", qPrintable(host.status()));
    });
    bool stopping = false;
    QTimer shutdown;
    shutdown.setInterval(50);
    QObject::connect(&shutdown, &QTimer::timeout, &app, [&] {
        if (!host.running()) app.quit();
    });
    const auto stop = [&] {
        if (stopping) return;
        stopping = true;
        host.requestExit();
        shutdown.start();
    };
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, signalPipe) != 0) {
        fputs("Cannot install host shutdown signal handling.\n", stderr);
        return 3;
    }
    QSocketNotifier notifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&] {
        char buffer[32]; (void)!read(signalPipe[0], buffer, sizeof(buffer)); stop();
    });
    struct sigaction action {};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    if (!parser.isSet("no-share"))
        QTimer::singleShot(0, &host, [&] { host.start(host.sharingWidth(), host.sharingHeight()); });
    fputs("DeskPort host management is ready. Use deskport status to check sharing readiness.\n", stderr);
    const int result = app.exec();
    notifier.setEnabled(false);
    close(signalPipe[0]); close(signalPipe[1]);
    signalPipe[0] = signalPipe[1] = -1;
    return result;
#endif
}
