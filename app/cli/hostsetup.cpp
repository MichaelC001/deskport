#include "hostsetup.h"
#include "backend/serviceconfig.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#ifdef Q_OS_LINUX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
#ifdef Q_OS_LINUX
const QString unitName = QStringLiteral("io.github.keithxc.DeskPort.service");
const QByteArray ownership = "# Managed by DeskPort CLI v1\n# Content-SHA256: ";

struct ProcessResult {
    bool finished = false;
    int code = -1;
    QByteArray output;
};

ProcessResult run(const QString& program, const QStringList& arguments,
                  const QProcessEnvironment& environment = QProcessEnvironment::systemEnvironment()) {
    ProcessResult result;
    if (program.isEmpty()) return result;
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(program, arguments);
    if (!process.waitForStarted(1500)) return result;
    if (!process.waitForFinished(4000)) {
        process.kill();
        process.waitForFinished(1000);
        return result;
    }
    result.finished = process.exitStatus() == QProcess::NormalExit;
    result.code = process.exitCode();
    result.output = process.readAllStandardOutput().left(65536);
    return result;
}

bool succeeded(const ProcessResult& result) { return result.finished && result.code == 0; }
#endif

int reportError(const QString& message, int code = 1) {
    QTextStream(stderr) << "deskport: " << message << '\n';
    return code;
}

void printHelp() {
    QTextStream(stdout) <<
        "Usage:\n"
        "  deskport doctor [--json]\n"
        "  deskport service install [--headless]\n"
        "  deskport service status [--json]\n"
        "  deskport service uninstall\n\n"
        "install writes a user unit and reloads systemd; it never enables or starts it.\n"
        "--headless runs the host API without a GUI. It does not create a desktop.\n"
        "See docs/CLI.md for SSH session setup and explicit activation commands.\n";
}

#ifdef Q_OS_LINUX
bool socketExists(const QString& path) {
    if (path.isEmpty()) return false;
    struct stat details {};
    return ::stat(QFile::encodeName(path).constData(), &details) == 0 && S_ISSOCK(details.st_mode);
}

QString resolvedRuntimePath(const QString& runtime, const QString& name) {
    if (name.isEmpty()) return {};
    return QFileInfo(name).isAbsolute() ? name : runtime.isEmpty() ? QString() : runtime + '/' + name;
}

QJsonObject doctor() {
    QJsonArray checks;
    bool blocked = false;
    bool degraded = false;
    const auto add = [&](const QString& id, const QString& status, const QString& message,
                         const QString& hint = QString()) {
        checks.append(QJsonObject{{"id", id}, {"status", status}, {"message", message}, {"hint", hint}});
        blocked |= status == "unsupported";
        degraded |= status == "degraded";
    };
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const QFileInfo runtimeInfo(runtime);
    const bool runtimeReady = !runtime.isEmpty() && runtimeInfo.isAbsolute() && runtimeInfo.isDir()
        && runtimeInfo.ownerId() == uint(::getuid()) && runtimeInfo.isReadable() && runtimeInfo.isWritable();
    add("runtime", runtimeReady ? "ok" : "degraded",
        runtimeReady ? "User runtime directory is available." : "XDG_RUNTIME_DIR is missing or inaccessible for this user.",
        runtimeReady ? QString() : "Use the desktop user's SSH login and its existing /run/user/$(id -u) directory.");
    const bool wayland = runtimeReady && socketExists(resolvedRuntimePath(runtime, qEnvironmentVariable("WAYLAND_DISPLAY")));
    const bool x11 = !qEnvironmentVariableIsEmpty("DISPLAY");
    add("session", wayland ? "ok" : x11 ? "degraded" : "unsupported",
        wayland ? "A Wayland display socket is available." : x11 ? "An X11 display is configured but has not been verified." : "No accessible graphical session was found.",
        wayland ? QString() : "SSH alone does not create pixels. Start a desktop/compositor as this user and import its session environment.");

    QProcessEnvironment busEnvironment = QProcessEnvironment::systemEnvironment();
    const bool busSocket = runtimeReady && socketExists(runtime + "/bus");
    if (busEnvironment.value("DBUS_SESSION_BUS_ADDRESS").isEmpty() && busSocket)
        busEnvironment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + runtime + "/bus");
    const QString dbusSend = QStandardPaths::findExecutable("dbus-send");
    const bool busConfigured = !busEnvironment.value("DBUS_SESSION_BUS_ADDRESS").isEmpty();
    const bool busReady = busConfigured && succeeded(run(dbusSend,
        {"--session", "--print-reply", "--reply-timeout=1500", "--dest=org.freedesktop.DBus",
         "/org/freedesktop/DBus", "org.freedesktop.DBus.Peer.Ping"}, busEnvironment));
    add("session_bus", busReady ? "ok" : "degraded",
        busReady ? "The session D-Bus responds." : "The session D-Bus could not be verified.",
        busReady ? QString() : "Install dbus-send and use the existing desktop user's session bus; do not create a separate root session.");
    const QString remote = qEnvironmentVariable("PIPEWIRE_REMOTE", "pipewire-0");
    const bool pipewire = runtimeReady && socketExists(resolvedRuntimePath(runtime, remote));
    add("pipewire", pipewire ? "ok" : "degraded",
        pipewire ? "A PipeWire socket is available; capture has not been exercised." : "No PipeWire socket is available.",
        pipewire ? QString() : "Start the desktop's PipeWire and session manager, then run doctor in the same user session.");

    const QString helpers = QCoreApplication::applicationDirPath() + "/../libexec/";
    const bool host = QFileInfo(helpers + "deskport-host").isExecutable();
    const bool display = QFileInfo(helpers + "deskport-display").isExecutable();
    add("host_helper", host ? "ok" : "unsupported",
        host ? "The packaged host helper is executable." : "The packaged host helper is missing or not executable.",
        host ? QString() : "Install a complete DeskPort host package, including libexec/deskport-host.");
    add("display_helper", display ? "ok" : "degraded",
        display ? "The packaged display helper is executable." : "The packaged display helper is missing or not executable.",
        display ? QString() : "Install libexec/deskport-display for virtual-output and compositor integration.");
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    const bool hyprland = desktop.contains("Hyprland", Qt::CaseInsensitive)
        || !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE");
    QString compositor = hyprland ? "hyprland" : desktop.contains("KDE", Qt::CaseInsensitive) ? "kde"
        : desktop.contains("GNOME", Qt::CaseInsensitive) ? "gnome" : "unknown";
    if (hyprland) {
        const QString hyprctl = QStandardPaths::findExecutable("hyprctl");
        const auto probe = run(hyprctl, {"-j", "monitors"});
        const auto monitors = QJsonDocument::fromJson(probe.output);
        const bool ready = succeeded(probe) && monitors.isArray() && !monitors.array().isEmpty();
        add("hyprland", ready ? "ok" : "degraded",
            ready ? "Hyprland IPC responds and has an active output." : "Hyprland was detected but its output IPC is unavailable.",
            ready ? QString() : "Install hyprctl and import WAYLAND_DISPLAY, XDG_CURRENT_DESKTOP and HYPRLAND_INSTANCE_SIGNATURE from the active session.");
        // Permission configuration varies by Hyprland version. Even when IPC
        // works, only a capture attempt can establish permission for the actual
        // host executable behind package wrappers; never disable enforcement.
        add("capture_permission", "degraded", "Hyprland screencopy permission has not been verified for the actual host executable.",
            "Authorize only the real host executable using this Hyprland version's permission configuration. Permission changes require a compositor restart; see docs/CLI.md.");
    } else {
        add("hyprland", "not_applicable", "Hyprland is not selected in this process environment.");
        add("capture_permission", "degraded", "Screen-capture authorization has not been exercised.",
            "Verify the capture backend in an actual session; portal backends may require an interactive desktop approval.");
    }
    const bool input = ::access("/dev/uinput", R_OK | W_OK) == 0;
    add("uinput", input ? "ok" : "degraded",
        input ? "This user can read and write /dev/uinput; no input was injected." : "This user cannot read and write /dev/uinput.",
        input ? QString() : "Use the package's udev rules and input access instructions, then start a new login session. Do not run DeskPort as root.");
    add("encoder", "degraded", "Encoder initialization and first-frame delivery require a real streaming test.",
        "A VPS needs a working capture source and a usable hardware or software encoder. CLI availability alone does not establish streaming support.");
    const QString systemctl = QStandardPaths::findExecutable("systemctl");
    const bool manager = succeeded(run(systemctl, {"--user", "show", "--property=Version", "--value"}, busEnvironment));
    add("user_service_manager", manager ? "ok" : "degraded",
        manager ? "The systemd user manager responds." : "The systemd user manager is unavailable.",
        manager ? QString() : "Use a systemd user login with the correct runtime directory and session bus before installing the service.");
    return {{"schemaVersion", 1}, {"command", "doctor"}, {"platform", "linux"},
            {"status", blocked ? "unsupported" : degraded ? "degraded" : "ready"},
            {"compositor", compositor}, {"checks", checks}};
}

QByteArray unitBody(QString executable, bool headless) {
    executable.replace("\\", "\\\\").replace("\"", "\\\"").replace("%", "%%").replace("$", "$$");
    QString body = "[Unit]\nDescription=DeskPort desktop sharing\n";
    if (!headless) body += "After=graphical-session.target\nPartOf=graphical-session.target\n";
    body += "StartLimitIntervalSec=0\n\n[Service]\nType=simple\nExecStart=\"" + executable + "\" "
        + (headless ? "host run" : "--background --share")
        + "\nRestart=on-failure\nRestartSec=10\nKillMode=mixed\nTimeoutStopSec=10\n\n[Install]\nWantedBy="
        + (headless ? "default.target\n" : "graphical-session.target\n");
    return body.toUtf8();
}

QByteArray ownedUnit(const QByteArray& body) {
    return ownership + QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex() + '\n' + body;
}

bool unmodifiedOwnedUnit(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return false;
    const QByteArray bytes = file.readAll();
    if (!bytes.startsWith(ownership)) return false;
    const int bodyAt = bytes.indexOf('\n', ownership.size());
    if (bodyAt < 0) return false;
    return ownedUnit(bytes.mid(bodyAt + 1)) == bytes;
}

bool hasDropIns(const QString& path) {
    const QFileInfo info(path + ".d");
    return info.isSymLink() || (info.exists() && (!info.isDir()
        || !QDir(info.absoluteFilePath()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()));
}

QString modificationBlock(const QString& path) {
    if (DeskPortService::storeManaged(path) || DeskPortService::storeManaged(DeskPortService::autostartPath())
        || QFileInfo(path).canonicalFilePath().startsWith("/nix/store/")
        || QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath().startsWith("/nix/store/"))
        return "This installation is managed by Nix. Change its declarative configuration instead.";
    const QFileInfo info(path);
    if (info.isSymLink()) return "Refusing to modify a symlinked user unit. Manage it through its existing configuration.";
    if (hasDropIns(path)) return "Refusing to modify a unit with manual drop-ins. Review its existing configuration first.";
    if (info.exists() && !unmodifiedOwnedUnit(path))
        return "Refusing to overwrite or remove an existing or edited unit. Only unmodified DeskPort CLI units are managed here.";
    return {};
}

int serviceCommand(const QStringList& args) {
    if (args.isEmpty()) { printHelp(); return 2; }
    const QString action = args.first();
    const bool headless = args.contains("--headless");
    const bool json = args.contains("--json");
    if (action != "install" && action != "uninstall" && action != "status") return reportError("Unknown service command.", 2);
    QStringList options = args.mid(1);
    if (action == "install") options.removeOne("--headless");
    if (action == "status") options.removeOne("--json");
    if (!options.isEmpty()) return reportError("Unsupported service option. Use deskport service --help.", 2);
    const QString systemctl = QStandardPaths::findExecutable("systemctl");
    if (systemctl.isEmpty()) return reportError("systemctl was not found; service management requires systemd.", 3);
    const QString path = DeskPortService::unitPath();
    if (action == "status") {
        const auto state = run(systemctl, {"--user", "show", unitName, "--property=LoadState", "--property=ActiveState",
                                         "--property=SubState", "--property=UnitFileState"});
        QJsonObject result{{"schemaVersion", 1}, {"command", "service status"}, {"unit", unitName},
                           {"status", succeeded(state) ? "ok" : "unavailable"},
                           {"managedByCli", unmodifiedOwnedUnit(path)}, {"storeManaged", DeskPortService::storeManaged(path)}};
        for (const auto& line : state.output.split('\n')) {
            const int at = line.indexOf('=');
            if (at < 0) continue;
            const QString key = QString::fromUtf8(line.left(at));
            if (key == "LoadState" || key == "ActiveState" || key == "SubState" || key == "UnitFileState")
                result.insert(key, QString::fromUtf8(line.mid(at + 1)));
        }
        QTextStream out(stdout);
        if (json) out << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
        else {
            out << unitName << ": " << result["status"].toString() << '\n';
            for (const QString& key : {QString("LoadState"), QString("ActiveState"), QString("SubState"), QString("UnitFileState")})
                if (result.contains(key)) out << key << ": " << result[key].toString() << '\n';
            if (!succeeded(state)) out << "Use the desktop user's runtime directory and session bus, then retry.\n";
        }
        return succeeded(state) ? 0 : 1;
    }
    const QString blocked = modificationBlock(path);
    if (!blocked.isEmpty()) return reportError(blocked);
    // Query only ownership metadata, never the running service environment or credentials.
    const auto fragment = run(systemctl, {"--user", "show", unitName, "--property=FragmentPath", "--value"});
    if (!succeeded(fragment)) return reportError("Cannot inspect the systemd user manager. Set up the user's SSH runtime/session bus and retry.");
    const QString loadedPath = QString::fromUtf8(fragment.output).trimmed();
    if (!loadedPath.isEmpty() && QDir::cleanPath(loadedPath) != QDir::cleanPath(path))
        return reportError("A unit with this name is supplied by another configuration. Refusing to shadow or remove it.");
    const auto dropIns = run(systemctl, {"--user", "show", unitName, "--property=DropInPaths", "--value"});
    if (!succeeded(dropIns) || !dropIns.output.trimmed().isEmpty())
        return reportError("The unit has external drop-ins or their ownership could not be checked. Review its configuration before changing it.");
    if (action == "uninstall") {
        if (!QFileInfo::exists(path)) { QTextStream(stdout) << "No DeskPort CLI user unit is installed.\n"; return 0; }
        const auto active = run(systemctl, {"--user", "show", unitName, "--property=ActiveState", "--value"});
        const auto enabled = run(systemctl, {"--user", "show", unitName, "--property=UnitFileState", "--value"});
        const QByteArray activeState = active.output.trimmed();
        const QByteArray enabledState = enabled.output.trimmed();
        if (!succeeded(active) || !succeeded(enabled) || (activeState != "inactive" && activeState != "failed")
            || (enabledState != "disabled" && enabledState != "static"))
            return reportError("Disable and stop the unit explicitly before uninstalling: systemctl --user disable --now " + unitName);
        if (!QFile::remove(path)) return reportError("Could not remove the owned user unit.");
    } else {
        QString executable = DeskPortService::persistentExecutable(QCoreApplication::applicationFilePath(), qEnvironmentVariable("APPIMAGE"));
        const QFileInfo executableInfo(executable);
        // Nix wrapQtAppsHook launches the real binary as .deskport-wrapped.
        // The sibling launcher restores Qt/plugin/library search paths after
        // login, where this process's inherited build environment is absent.
        if (executableInfo.fileName() == ".deskport-wrapped") {
            const QFileInfo launcher(executableInfo.absolutePath() + "/deskport");
            if (!launcher.isFile() || !launcher.isExecutable())
                return reportError("The packaged deskport launcher is missing; refusing to install a service for the internal wrapped binary.");
            executable = launcher.absoluteFilePath();
        }
        if (executable.contains('\n') || executable.contains('\r')) return reportError("The executable path contains a line break and cannot be used in a systemd unit.");
        const QByteArray desired = ownedUnit(unitBody(executable, headless));
        QFile previous(path);
        const bool unchanged = previous.open(QIODevice::ReadOnly) && previous.readAll() == desired;
        previous.close();
        if (!unchanged) {
            if (!QDir().mkpath(QFileInfo(path).absolutePath())) return reportError("Could not create the systemd user unit directory.");
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly) || output.write(desired) != desired.size() || !output.commit())
                return reportError("Could not write the user unit.");
        }
    }
    if (!succeeded(run(systemctl, {"--user", "daemon-reload"})))
        return reportError("The unit file changed, but systemd daemon-reload failed. Run systemctl --user daemon-reload after repairing the session bus.");
    QTextStream(stdout) << (action == "install" ? "Installed: " : "Removed: ") << path << '\n';
    if (action == "install") QTextStream(stdout) << "Review with: systemctl --user cat " << unitName
        << "\nActivate explicitly with: systemctl --user enable --now " << unitName
        << "\nNo service was enabled or started.\n";
    return 0;
}
#endif
}

bool DeskPortCli::isSetupCommand(const QStringList& arguments) {
    return arguments.size() > 1 && (arguments[1] == "doctor" || arguments[1] == "service");
}

int DeskPortCli::runSetupCommand(const QStringList& arguments) {
    if (!isSetupCommand(arguments)) return reportError("Unknown setup command.", 2);
    const QStringList args = arguments.mid(2);
    if (args == QStringList{"--help"} || args == QStringList{"-h"}) { printHelp(); return 0; }
    if (arguments[1] == "service" && args.size() == 2
        && (args[0] == "install" || args[0] == "uninstall" || args[0] == "status")
        && (args[1] == "--help" || args[1] == "-h")) { printHelp(); return 0; }
    if (arguments[1] == "doctor" && !args.isEmpty() && args != QStringList{"--json"})
        return reportError("Usage: deskport doctor [--json]", 2);
#ifdef Q_OS_LINUX
    if (arguments[1] == "service") return serviceCommand(args);
    const auto result = doctor();
    QTextStream out(stdout);
    if (args.contains("--json")) out << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    else {
        out << "DeskPort host prerequisites: " << result["status"].toString() << '\n';
        for (const auto& item : result["checks"].toArray()) {
            const auto check = item.toObject();
            out << '[' << check["status"].toString() << "] " << check["id"].toString() << ": " << check["message"].toString() << '\n';
            if (!check["hint"].toString().isEmpty()) out << "  " << check["hint"].toString() << '\n';
        }
    }
    return result["status"] == "unsupported" ? 3 : result["status"] == "degraded" ? 1 : 0;
#else
    if (arguments[1] == "doctor" && args.contains("--json"))
        QTextStream(stdout) << "{\"schemaVersion\":1,\"command\":\"doctor\",\"platform\":\"other\",\"status\":\"unsupported\",\"checks\":[]}\n";
    else QTextStream(stderr) << "This setup command currently supports Linux hosts only.\n";
    return 3;
#endif
}
