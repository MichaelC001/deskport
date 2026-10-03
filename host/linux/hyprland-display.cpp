// SPDX-License-Identifier: GPL-3.0-or-later
// Own only a uniquely named headless output. Physical monitor rules are untouched.
#include "hyprland-display.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <memory>
#include <unistd.h>

namespace {
void send(const QJsonObject& object) {
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); fflush(stdout);
}

bool validName(const QString& name) {
    return QRegularExpression(QStringLiteral("^DeskPort-[a-f0-9-]{36}$")).match(name).hasMatch();
}

class Control {
public:
    QString executable = QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
    QString error;
    bool command(const QStringList& args, QByteArray* output = nullptr) {
        if (executable.isEmpty()) { error = "hyprctl is missing; install Hyprland before sharing"; return false; }
        QProcess process;
        process.start(executable, args);
        if (!process.waitForStarted(1000) || !process.waitForFinished(1500)) {
            process.kill(); process.waitForFinished(500);
            error = "Hyprland control timed out; check the selected graphical session";
            return false;
        }
        const auto bytes = process.readAllStandardOutput().trimmed();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            error = "Hyprland control failed: " + QString::fromUtf8(process.readAllStandardError().left(512));
            return false;
        }
        if (output) { *output = bytes; return true; }
        if (bytes != "ok") { error = "Hyprland rejected the request: " + QString::fromUtf8(bytes.left(512)); return false; }
        return true;
    }
    bool monitors(QJsonArray& result) {
        QByteArray bytes;
        if (!command({"-j", "monitors", "all"}, &bytes)) return false;
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error != QJsonParseError::NoError || !document.isArray()) {
            error = "Hyprland returned invalid monitor information"; return false;
        }
        result = document.array(); return true;
    }
    bool monitor(const QString& name, QJsonObject& result) {
        QJsonArray values;
        if (!monitors(values)) return false;
        result = {};
        for (const auto& value : values) {
            const auto candidate = value.toObject();
            if (candidate["name"].toString() == name) { result = candidate; break; }
        }
        return true;
    }
};

class Display {
    Control control;
    std::unique_ptr<QProcess> recovery;
    bool active = false;
    int width = 0, height = 0, scale = 0;
public:
    const QString name = "DeskPort-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    ~Display() { release(); }
    QString error;
    bool available() {
        QJsonArray monitors;
        if (!control.monitors(monitors)) { error = control.error; return false; }
        return true;
    }
    bool observe(int w, int h, int s) {
        QJsonObject value;
        if (!control.monitor(name, value)) { error = control.error; return false; }
        if (value.isEmpty() || value["disabled"].toBool() || value["width"].toInt() != w || value["height"].toInt() != h ||
            value["scale"].toDouble() != s || value["transform"].toInt() != 0 ||
            (!value["mirrorOf"].toString().isEmpty() && value["mirrorOf"].toString() != "none")) {
            error = "Hyprland owned output disappeared or its pixels, scale or layout did not match"; return false;
        }
        return true;
    }
    bool healthy() { return !active || observe(width, height, scale); }
    bool release() {
        active = false;
        if (!recovery) return true;
        recovery->closeWriteChannel();
        if (!recovery->waitForFinished(3000)) {
            // Leave the independent guard alive to finish removal after our exit.
            error = "Hyprland output cleanup did not complete promptly";
            recovery.release(); return false;
        }
        const bool ok = recovery->exitStatus() == QProcess::NormalExit && recovery->exitCode() == 0;
        recovery.reset();
        if (!ok) error = "Hyprland output cleanup failed; inspect the compositor session";
        return ok;
    }
    bool configure(int w, int h, int s) {
        if (!active) {
            QJsonObject existing;
            if (!control.monitor(name, existing)) { error = control.error; return false; }
            if (!existing.isEmpty()) { error = "Refusing to replace an existing Hyprland output"; return false; }
            // Pipe ownership, not process IDs, makes EOF/SIGKILL cleanup reliable.
            recovery = std::make_unique<QProcess>();
            recovery->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            recovery->start(QCoreApplication::applicationFilePath(), {"--restore-hyprland", name});
            if (!recovery->waitForStarted(1000) || !recovery->waitForReadyRead(4000) || recovery->readLine().trimmed() != "ready") {
                error = "Cannot start the Hyprland output cleanup guard"; return false;
            }
        } else if (w == width && h == height && s == scale) return observe(w, h, s);

        const auto mode = QString("%1x%2@60").arg(w).arg(h);
        const auto lua = QString("hl.monitor({ output = \"%1\", mode = \"%2\", position = \"auto\", scale = %3, transform = 0, mirror = \"\", disabled = false })").arg(name, mode).arg(s);
        // Omarchy 4 uses Lua; older Hyprland versions retain the keyword API.
        if (!control.command({"eval", lua})) {
            const bool legacy = control.error.contains("unknown", Qt::CaseInsensitive) ||
                control.error.contains("eval is only supported with the lua config manager");
            if (!legacy ||
                !control.command({"keyword", "monitor", QString("%1,%2,auto,%3,transform,0").arg(name, mode).arg(s)})) {
                error = control.error; return false;
            }
        }
        QElapsedTimer timer; timer.start();
        do {
            if (observe(w, h, s)) { width = w; height = h; scale = s; active = true; error.clear(); return true; }
            QThread::msleep(40);
        } while (timer.elapsed() < 1800);
        return false;
    }
    QJsonObject identity(int w, int h, int s) const {
        return {{"displayId", 1}, {"outputName", name}, {"backend", "hyprland"},
            {"width", w}, {"height", h}, {"scale", s}, {"active", true}};
    }
};
}

int restoreHyprlandDisplay(const QString& name) {
    if (!validName(name)) return 2;
    // A session-wide TERM must not kill restoration together with its owner.
    // EOF still ends the guard; the service's final SIGKILL remains bounded.
    std::signal(SIGTERM, SIG_IGN);
    std::signal(SIGINT, SIG_IGN);
    // The parent can die while output creation is in flight, before reading
    // our ready acknowledgment. Still consume EOF and remove the owned output.
    std::signal(SIGPIPE, SIG_IGN);
    Control control;
    QJsonObject monitor;
    if (!control.monitor(name, monitor) || !monitor.isEmpty()) return 1;
    // The guard creates the output itself, so parent death during creation cannot
    // race an early cleanup against a still-running compositor request.
    const bool created = control.command({"output", "create", "headless", name});
    if (!created) {
        if (control.monitor(name, monitor) && !monitor.isEmpty()) control.command({"output", "remove", name});
        return 1;
    }
    fputs("ready\n", stdout); fflush(stdout);
    char bytes[64];
    while (true) {
        const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
    }
    if (!control.monitor(name, monitor)) return 1;
    if (monitor.isEmpty()) return 0;
    if (!control.command({"output", "remove", name})) return 1;
    QElapsedTimer timer; timer.start();
    do {
        if (!control.monitor(name, monitor)) return 1;
        if (monitor.isEmpty()) return 0;
        QThread::msleep(40);
    } while (timer.elapsed() < 1800);
    return 1;
}

int runHyprlandDisplay(int width, int height) {
    Display display;
    if (!display.available()) { send({{"error", display.error}}); return 1; }
    if (qEnvironmentVariableIntValue("DESKPORT_DISPLAY_ON_DEMAND") == 1)
        send({{"ready", true}, {"active", false}, {"backend", "hyprland"}, {"outputName", display.name}});
    else {
        if (!display.configure(width, height, 1)) { send({{"error", display.error}}); return 1; }
        send(display.identity(width, height, 1));
    }
    QByteArray buffer;
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &input, [&] {
        input.setEnabled(false);
        char bytes[4096]; const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) { input.setEnabled(true); return; }
        if (count <= 0) { QCoreApplication::quit(); return; }
        buffer.append(bytes, int(count));
        if (buffer.size() > 8192) { QCoreApplication::exit(2); return; }
        while (buffer.contains('\n')) {
            const auto end = buffer.indexOf('\n');
            const auto request = QJsonDocument::fromJson(buffer.left(end)).object(); buffer.remove(0, end + 1);
            const int seq = request["seq"].toInt(), w = request["width"].toInt(), h = request["height"].toInt(), s = request["scale"].toInt();
            QJsonObject response{{"seq", seq}};
            if (!seq || w < 640 || w > 7680 || h < 360 || h > 4320 || w % 4 || h % 4 || (s != 1 && s != 2) ||
                (request.contains("displayPolicy") && request["displayPolicy"].toInt(-1) != 0)) {
                response["error"] = "Invalid or unsupported Hyprland virtual output request";
            } else if (!request["session"].toBool(true)) {
                if (!display.release()) { response["error"] = display.error; send(response); QCoreApplication::exit(1); return; }
                response["active"] = false; response["width"] = w; response["height"] = h; response["scale"] = s;
            } else {
                if (!display.configure(w, h, s)) { response["error"] = display.error; send(response); QCoreApplication::exit(1); return; }
                response = display.identity(w, h, s); response["seq"] = seq;
            }
            send(response);
        }
        input.setEnabled(true);
    });
    QTimer health;
    health.setInterval(2000);
    QObject::connect(&health, &QTimer::timeout, &health, [&] {
        if (!display.healthy()) { send({{"error", display.error}}); QCoreApplication::exit(1); }
    });
    health.start();
    return QCoreApplication::exec();
}
