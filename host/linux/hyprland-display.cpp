// SPDX-License-Identifier: GPL-3.0-or-later
// Session-scoped Hyprland topology. Only the guardian mutates compositor state.
#include "hyprland-display.h"
#include "../../shared/deskport-core/include/deskport/protocol.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QLockFile>
#include <QSaveFile>
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
#include <functional>
#include <unistd.h>
#include <sys/prctl.h>

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
        const auto owner = getpid();
        process.setChildProcessModifier([owner] {
            // A killed guardian must not leave a delayed hyprctl child that
            // mutates outputs after a replacement guardian has recovered them.
            if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != owner) _exit(1);
        });
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
    bool workspaces(QJsonArray& result) {
        QByteArray bytes;
        if (!command({"-j", "workspaces"}, &bytes)) return false;
        const auto document = QJsonDocument::fromJson(bytes);
        if (!document.isArray()) { error = "Hyprland returned invalid workspace information"; return false; }
        result = document.array(); return true;
    }
    bool moveWorkspace(int id, const QString& destination) {
        // Lua's dispatch command accepts a closure, not legacy dispatcher text.
        const auto lua = QString("hl.dispatch(hl.dsp.workspace.move({ workspace = \"%1\", monitor = \"%2\" }))").arg(id).arg(destination);
        if (command({"eval", lua})) return true;
        const bool legacy = error.contains("unknown", Qt::CaseInsensitive) ||
            error.contains("eval is only supported with the lua config manager");
        return legacy && command({"dispatch", "moveworkspacetomonitor", QString("%1 %2").arg(id).arg(destination)});
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

// JSON monitor IDs are ephemeral; resolve mirrorOf IDs to output names before
// journaling. Serial/description also prevent restoring onto a replaced device.
QString mirror(const QJsonObject& monitor, const QJsonArray& monitors) {
    const auto source = monitor["mirrorOf"].toString();
    if (source.isEmpty() || source == "none") return {};
    for (const auto& value : monitors) {
        const auto candidate = value.toObject();
        if (QString::number(candidate["id"].toInt(-1)) == source || candidate["name"].toString() == source)
            return candidate["name"].toString();
    }
    return QStringLiteral("?");
}
bool safeOutput(const QString& name) {
    return QRegularExpression(QStringLiteral("^[A-Za-z0-9_.:-]{1,128}$")).match(name).hasMatch();
}
bool safeDescription(const QString& value) {
    if (value.isEmpty() || value.size() > 512) return false;
    for (const auto ch : value)
        if (ch.unicode() < 32 || ch == '"' || ch == '\\' || ch == ',' || ch == '%') return false;
    return true;
}
QString selector(const QJsonObject& monitor, const QJsonArray& monitors) {
    const auto name = monitor["name"].toString();
    // Software backend names cannot collide with kernel DRM connector names.
    if (QRegularExpression(QStringLiteral("^(HEADLESS|WAYLAND)-[0-9]+$")).match(name).hasMatch() || validName(name)) return name;
    const auto description = monitor["description"].toString(), serial = monitor["serial"].toString();
    const auto expected = QString("%1 %2 %3").arg(monitor["make"].toString(), monitor["model"].toString(), serial).remove(',').trimmed();
    if (!safeDescription(description) || monitor["make"].toString().isEmpty() || monitor["model"].toString().isEmpty() ||
        description != expected || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_.:-]{1,128}$")).match(serial).hasMatch() ||
        QRegularExpression(QStringLiteral("^(0x)?0+$|^(unknown|none|n/a)$"), QRegularExpression::CaseInsensitiveOption).match(serial).hasMatch()) return {};
    for (const auto& value : monitors) {
        const auto other = value.toObject();
        if (other["name"] != monitor["name"] && other["description"].toString() == description) return {};
    }
    // Hyprland matches desc: by prefix. The full DRM description appends
    // " (connector)"; include that boundary so serial 123 cannot match 1234.
    return "desc:" + description + " (";
}
bool sameDevice(const QJsonObject& a, const QJsonObject& b) {
    return (a["selector"].toString().startsWith("desc:") || a["name"] == b["name"]) &&
        a["serial"] == b["serial"] && a["description"] == b["description"];
}
bool retainedWorkspace(const QJsonObject& workspace) {
    // Empty transient workspaces can be destroyed and their IDs reused by
    // Hyprland during monitor migration. They are not a stable recovery target.
    return workspace["windows"].toInt() > 0 || workspace["ispersistent"].toBool();
}
QString number(double value) { return QString::number(value, 'f', 5); }

class Lease {
    Control control;
    QString journalPath;
    QJsonObject journal;
    int policy = -1;
    bool validJournal = true;
public:
    QString name;
    bool layoutWarning = false, pendingRecovery = false;
    QString error, recoveryReason;
    explicit Lease(const QString& output, const QString& path) : journalPath(path), name(output) {}
    bool capable(const QJsonArray& monitors) const {
        for (const auto& value : monitors) {
            const auto m = value.toObject();
            if (!safeOutput(m["name"].toString()) || !m.contains("disabled") || !m.contains("mirrorOf")) return false;
            // Monitor rules replace unexposed HDR/ICC/VRR settings as a unit.
            // Limit topology leases to ordinary SDR; extension needs no physical
            // rule replacement and remains available on advanced color setups.
            if (!m["disabled"].toBool() && selector(m, monitors).isEmpty()) return false;
            if (!m["disabled"].toBool() &&
                (!m["colorManagementPreset"].isString() || m["colorManagementPreset"].toString().toLower() != "srgb" ||
                 !m["currentFormat"].isString() || !m["currentFormat"].toString().contains("8888") || !m["vrr"].isBool() || m["vrr"].toBool())) return false;
            if (!m["disabled"].toBool() && (m["width"].toInt() <= 0 || m["height"].toInt() <= 0 ||
                m["scale"].toDouble() <= 0 || m["refreshRate"].toDouble() <= 0 ||
                !m.contains("x") || !m.contains("y") || mirror(m, monitors) == "?")) return false;
        }
        return true;
    }
    bool available(bool& policies) {
        if (!validJournal) return false;
        QJsonArray monitors;
        if (!control.monitors(monitors)) { error = control.error; return false; }
        policies = capable(monitors); return true;
    }
    bool save() {
        QSaveFile file(journalPath);
        if (!file.open(QIODevice::WriteOnly)) { error = "Cannot persist Hyprland recovery journal"; return false; }
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        const auto bytes = QJsonDocument(journal).toJson(QJsonDocument::Compact);
        if (file.write(bytes) != bytes.size() || !file.commit()) { error = "Cannot commit Hyprland recovery journal"; return false; }
        return true;
    }
    bool rawRule(const QJsonObject& m, const QString& source = {}, bool disabled = false) {
        const auto output = m["selector"].toString(m["name"].toString());
        const bool validSelector = output == m["name"].toString() ? safeOutput(output) :
            output == "desc:" + m["description"].toString() + " (" && safeDescription(m["description"].toString());
        if (!validSelector || (!source.isEmpty() && !safeOutput(source))) { error = "Unsafe Hyprland output identity"; return false; }
        const auto mode = QString("%1x%2@%3").arg(m["width"].toInt()).arg(m["height"].toInt()).arg(number(m["refreshRate"].toDouble(60)));
        const auto position = m.contains("x") ? QString("%1x%2").arg(m["x"].toInt()).arg(m["y"].toInt()) : QStringLiteral("auto");
        const auto scale = number(m["scale"].toDouble());
        const auto lua = QString("hl.monitor({ output = \"%1\", mode = \"%2\", position = \"%3\", scale = %4, transform = %5, mirror = \"%6\", disabled = %7%8 })")
            .arg(output, mode, position, scale).arg(m["transform"].toInt()).arg(source, disabled ? "true" : "false")
            .arg(m.contains("colorManagementPreset") ? ", cm = \"srgb\"" : "");
        if (control.command({"eval", lua})) return true;
        const bool legacy = control.error.contains("unknown", Qt::CaseInsensitive) ||
            control.error.contains("eval is only supported with the lua config manager");
        auto text = disabled ? output + ",disable" : QString("%1,%2,%3,%4,transform,%5").arg(output, mode, position, scale).arg(m["transform"].toInt());
        if (!disabled && !source.isEmpty()) text += ",mirror," + source;
        if (!disabled && m.contains("colorManagementPreset")) text += ",cm,srgb";
        if (legacy && control.command({"keyword", "monitor", text})) return true;
        error = control.error; return false;
    }
    bool rule(const QJsonObject& m, const QString& source = {}, bool disabled = false) {
        QJsonArray now;
        if (!control.monitors(now)) { error = control.error; return false; }
        const auto matches = [&](const QJsonObject& current) {
            return m["selector"].toString().startsWith("desc:") ? sameDevice(m, current) : current["name"] == m["name"];
        };
        for (const auto& value : now) {
            const auto current = value.toObject();
            if (!matches(current)) continue;
            const auto previous = mirror(current, now);
            if (previous.isEmpty() || previous == source) break;
            // Clear a prior replication relationship before assigning another
            // source. This also avoids stale mirror lists in older Hyprland.
            if (!rawRule(m) || !waitFor([&] {
                QJsonArray observed;
                if (!control.monitors(observed)) return false;
                for (const auto& item : observed)
                    if (matches(item.toObject())) return mirror(item.toObject(), observed).isEmpty();
                return true; // Detached originals are restored by their rule.
            })) return false;
            if (source.isEmpty() && !disabled) return true;
            break;
        }
        return rawRule(m, source, disabled);
    }
    bool owned(int w, int h, int s) {
        QJsonObject m;
        if (!control.monitor(name, m)) { error = control.error; return false; }
        if (m.isEmpty() || m["disabled"].toBool() || m["width"].toInt() != w || m["height"].toInt() != h ||
            m["scale"].toDouble() != s || m["transform"].toInt() != 0 ||
            (!m["mirrorOf"].toString().isEmpty() && m["mirrorOf"].toString() != "none")) {
            error = "Hyprland owned output disappeared or its pixels, scale or layout did not match"; return false;
        }
        return true;
    }
    bool topology(bool restoring) {
        QJsonArray now;
        if (!control.monitors(now)) { error = control.error; return false; }
        for (const auto& value : journal["targets"].toArray()) {
            const auto before = value.toObject();
            for (const auto& item : now) {
                const auto current = item.toObject();
                if (!sameDevice(before, current)) {
                    if (current["name"] == before["name"] && !before["selector"].toString().startsWith("desc:")) {
                        error = "Hyprland virtual output identity changed; recovery retained"; return false;
                    }
                    continue;
                }
                if (!restoring && policy == DP_DISPLAY_PRIMARY_ONLY) {
                    if (!current["disabled"].toBool()) return false;
                } else if (!restoring) {
                    if (current["disabled"].toBool() || mirror(current, now) != name) return false;
                } else {
                    const auto originalSource = before["source"].toString();
                    const auto source = workspaceTarget(originalSource, now);
                    if ((!originalSource.isEmpty() && source.isEmpty()) || current["disabled"].toBool() || mirror(current, now) != source) return false;
                    for (const auto* key : {"width", "height", "x", "y", "transform"})
                        if (current[key].toInt() != before[key].toInt()) return false;
                    if (qAbs(current["scale"].toDouble() - before["scale"].toDouble()) > .01 ||
                        qAbs(current["refreshRate"].toDouble() - before["refreshRate"].toDouble()) > .1) return false;
                }
            }
        }
        return true;
    }
    bool waitFor(const std::function<bool()>& check) {
        QElapsedTimer timer; timer.start();
        do { if (check()) return true; QThread::msleep(40); } while (timer.elapsed() < 1800);
        if (error.isEmpty()) error = "Hyprland did not confirm the requested output topology";
        return false;
    }
    QString workspaceTarget(const QString& original, const QJsonArray& now) const {
        for (const auto& value : journal["targets"].toArray()) {
            const auto target = value.toObject();
            if (target["name"].toString() != original) continue;
            for (const auto& item : now) {
                const auto current = item.toObject();
                if (sameDevice(target, current) && safeOutput(current["name"].toString())) return current["name"].toString();
            }
        }
        return {};
    }
    bool workspacesRestored() {
        if (journal["workspaces"].toArray().isEmpty()) return true;
        QJsonArray now, spaces;
        if (!control.monitors(now) || !control.workspaces(spaces)) return false;
        for (const auto& value : journal["workspaces"].toArray()) {
            const auto before = value.toObject();
            const auto destination = workspaceTarget(before["monitor"].toString(), now);
            if (destination.isEmpty()) continue;
            for (const auto& current : spaces)
                if (current.toObject()["id"] == before["id"] && retainedWorkspace(current.toObject()) && current.toObject()["monitor"].toString() != destination) return false;
        }
        return true;
    }
    bool journalShape() const {
        if (journal["version"].toInt() != 1 || !validName(journal["output"].toString()) ||
            !journal["targets"].isArray() || !journal["snapshot"].isArray() || !journal["workspaces"].isArray()) return false;
        for (const auto& value : journal["targets"].toArray()) {
            const auto m = value.toObject();
            if (!safeOutput(m["name"].toString()) || m["name"] == journal["output"] ||
                m["width"].toInt() <= 0 || m["height"].toInt() <= 0 || m["scale"].toDouble() <= 0 ||
                m["refreshRate"].toDouble() <= 0 || !m.contains("x") || !m.contains("y") ||
                !m["selector"].isString() || (m["selector"] != m["name"] && (m["selector"].toString() != "desc:" + m["description"].toString() + " (" || !safeDescription(m["description"].toString()))) ||
                !m["source"].isString() || (!m["source"].toString().isEmpty() && !safeOutput(m["source"].toString()))) return false;
        }
        for (const auto& value : journal["workspaces"].toArray()) {
            const auto w = value.toObject();
            if (w["id"].toInt() <= 0 || !safeOutput(w["monitor"].toString())) return false;
        }
        return true;
    }
    bool restore() {
        if (journal.isEmpty() && QFile::exists(journalPath)) {
            QFile file(journalPath);
            if (!file.open(QIODevice::ReadOnly)) { validJournal = false; error = "Cannot read Hyprland recovery journal"; return false; }
            journal = QJsonDocument::fromJson(file.readAll()).object();
            if (!journalShape()) {
                validJournal = false; error = "Invalid Hyprland recovery journal; refusing display changes"; return false;
            }
        }
        if (journal.isEmpty()) return true;
        name = journal["output"].toString();
        // Restore sources before mirrors. Restore even detached rules, so a
        // later hotplug cannot inherit our temporary disable/mirror rule.
        QJsonArray now;
        if (!control.monitors(now)) { error = control.error; return false; }
        bool ok = true;
        for (int mirrors = 0; mirrors < 2; ++mirrors) {
            for (const auto& value : journal["targets"].toArray()) {
                const auto before = value.toObject();
                if (before["source"].toString().isEmpty() != (mirrors == 0)) continue;
                bool replaced = false;
                for (const auto& item : now) {
                    const auto current = item.toObject();
                    if (current["name"] == before["name"] && !sameDevice(before, current) && !before["selector"].toString().startsWith("desc:")) replaced = true;
                }
                if (replaced) { ok = false; error = "Hyprland output identity changed; recovery retained"; continue; }
                const auto originalSource = before["source"].toString();
                const auto source = workspaceTarget(originalSource, now);
                if (!originalSource.isEmpty() && source.isEmpty()) ok = false;
                if (!rule(before, source)) ok = false;
            }
        }
        if (!waitFor([&] { return topology(true); })) ok = false;
        // Disabling/mirroring migrates normal workspaces to the source output.
        // Return surviving workspaces to their original, still attached display.
        // Hyprland may change focus as a side effect; no input is synthesized.
        QJsonArray spaces;
        if (!journal["workspaces"].toArray().isEmpty() && !control.workspaces(spaces)) { error = control.error; ok = false; }
        for (const auto& value : journal["workspaces"].toArray()) {
            const auto before = value.toObject();
            const auto destination = workspaceTarget(before["monitor"].toString(), now);
            if (destination.isEmpty()) continue;
            for (const auto& current : spaces) {
                const auto space = current.toObject();
                if (space["id"] != before["id"] || !retainedWorkspace(space) || space["monitor"].toString() == destination) continue;
                if (!control.moveWorkspace(before["id"].toInt(), destination)) { error = control.error; ok = false; }
            }
        }
        if (ok && !waitFor([&] { return workspacesRestored(); })) ok = false;
        // Never strand local outputs behind a removed mirror source when the
        // first restore attempt fails. Retry the retained journal on startup.
        if (!ok) return false;
        const auto output = journal["output"].toString();
        QJsonObject m;
        if (!control.monitor(output, m)) { error = control.error; return false; }
        if (!m.isEmpty() && !control.command({"output", "remove", output})) { error = control.error; return false; }
        if (!waitFor([&] { return control.monitor(output, m) && m.isEmpty() && topology(true) && workspacesRestored(); })) return false;
        if (!QFile::remove(journalPath)) { error = "Cannot clear verified Hyprland recovery journal"; return false; }
        journal = {}; policy = -1; pendingRecovery = false; recoveryReason.clear(); error.clear(); return true;
    }
    bool configure(int w, int h, int s, int requested) {
        error.clear(); layoutWarning = pendingRecovery;
        const bool continuing = policy >= 0;
        if (policy >= 0 && requested != policy) { error = "Cannot change Hyprland display policy within a lease"; return false; }
        if (journal.isEmpty()) {
            QJsonArray monitors;
            if (!control.monitors(monitors)) { error = control.error; return false; }
            if (requested != DP_DISPLAY_EXTEND && !capable(monitors)) { error = "Hyprland cannot safely snapshot this topology; only extension is supported"; return false; }
            QJsonArray targets;
            for (const auto& value : monitors) {
                auto m = value.toObject();
                if (m["name"].toString() == name) { error = "Refusing to replace an existing Hyprland output"; return false; }
                if (requested != DP_DISPLAY_EXTEND && !m["disabled"].toBool()) { m["source"] = mirror(m, monitors); m["selector"] = selector(m, monitors); targets.append(m); }
            }
            QJsonArray spaces, savedSpaces;
            if (!targets.isEmpty()) {
                if (!control.workspaces(spaces)) { error = control.error; return false; }
                for (const auto& value : spaces) {
                    const auto space = value.toObject();
                    if (space["id"].toInt() <= 0 || !retainedWorkspace(space)) continue;
                    for (const auto& target : targets)
                        if (target.toObject()["name"] == space["monitor"]) savedSpaces.append(QJsonObject{{"id", space["id"]}, {"monitor", space["monitor"]}});
                }
            }
            journal = {{"version", 1}, {"output", name}, {"targets", targets}, {"snapshot", monitors}, {"workspaces", savedSpaces}};
            // Snapshot is committed before the guard's first compositor mutation.
            if (!save()) { journal = {}; return false; }
            policy = requested;
        }
        if (policy < 0) {
            // Pending recovery retains the original snapshot across a new lease.
            // It must not prevent an independently verified remote workspace.
            bool supported = false;
            if (!available(supported) || (requested != DP_DISPLAY_EXTEND && !supported)) { error = "Hyprland topology policy is unavailable while recovery is pending"; return false; }
            policy = requested; layoutWarning = true;
        }
        QJsonObject existing;
        if (!control.monitor(name, existing)) { error = control.error; return false; }
        if (existing.isEmpty()) {
            if (continuing) { error = "Hyprland owned output disappeared"; return false; }
            if (!control.command({"output", "create", "headless", name})) { error = control.error; return false; }
        }
        const QJsonObject mode{{"name", name}, {"width", w}, {"height", h}, {"scale", s}, {"transform", 0}, {"refreshRate", 60}};
        if (!owned(w, h, s) && (!rule(mode) || !waitFor([&] { return owned(w, h, s); }))) return false;
        error.clear();
        // A target attached after the snapshot is deliberately excluded. Reapply
        // the original targets after resize or same-device hotplug, never resnapshot.
        QJsonArray now;
        if (!control.monitors(now)) { error = control.error; return false; }
        if (policy != DP_DISPLAY_EXTEND && !topology(false)) {
            bool applied = true;
            // Break existing mirror relationships before changing their roots.
            for (int mirrored = 1; mirrored >= 0; --mirrored) {
                for (const auto& value : journal["targets"].toArray()) {
                    const auto before = value.toObject();
                    for (const auto& item : now) {
                        const auto current = item.toObject();
                        if (!sameDevice(before, current) || int(!mirror(current, now).isEmpty()) != mirrored) continue;
                        if (!rule(before, policy == DP_DISPLAY_PRIMARY_MIRROR ? name : QString(), policy == DP_DISPLAY_PRIMARY_ONLY)) applied = false;
                    }
                }
            }
            if (!waitFor([&] { return topology(false); }) || !applied) layoutWarning = true;
        }
        if (layoutWarning) {
            pendingRecovery = true;
            QJsonArray observed;
            control.monitors(observed);
            journal["lastFailure"] = QJsonObject{{"policy", requested}, {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                {"reason", error.isEmpty() ? (recoveryReason.isEmpty() ? QStringLiteral("Local layout recovery remains pending") : recoveryReason) : error}, {"observed", observed}};
            if (!save()) return false;
        }
        if (!owned(w, h, s)) return false;
        error.clear(); return true;
    }
};

class Display {
    std::unique_ptr<QProcess> recovery;
    int width = 0, height = 0, scale = 0, policy = -1;
    bool active = false;
    bool start() {
        recovery = std::make_unique<QProcess>();
        recovery->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        // Keep recovery outside the owner's process group. EOF, rather than a
        // session-wide signal, determines when this guardian restores displays.
        recovery->setChildProcessModifier([] { setsid(); });
        recovery->start(QCoreApplication::applicationFilePath(), {"--restore-hyprland", name});
        QJsonObject ready;
        if (!recovery->waitForStarted(1000) || !receive(ready) || !ready["ready"].toBool()) {
            if (error.isEmpty()) error = "Cannot start the Hyprland topology cleanup guard";
            return false;
        }
        policies = ready["displayPolicy"].toBool();
        if (validName(ready["outputName"].toString())) name = ready["outputName"].toString();
        return true;
    }
    bool receive(QJsonObject& object) {
        QElapsedTimer timer; timer.start();
        while (!recovery->canReadLine() && timer.elapsed() < 12000 && recovery->state() != QProcess::NotRunning)
            recovery->waitForReadyRead(100);
        if (!recovery->canReadLine()) { error = "Hyprland topology guard stopped or timed out"; return false; }
        object = QJsonDocument::fromJson(recovery->readLine()).object();
        if (object.contains("error")) { error = object["error"].toString(); return false; }
        return !object.isEmpty();
    }
public:
    QString name = "DeskPort-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool policies = false, layoutWarning = false;
    QString error;
    ~Display() { release(); }
    bool available() { return start(); }
    bool healthy() {
        if (!active && !recovery) return true;
        if (!recovery || recovery->state() != QProcess::Running) { error = "Hyprland topology guard stopped"; return false; }
        if (!active) return true;
        return configure(width, height, scale, policy);
    }
    bool release() {
        active = false; policy = -1;
        if (!recovery) return true;
        recovery->closeWriteChannel();
        if (recovery->state() != QProcess::NotRunning && !recovery->waitForFinished(12000)) {
            error = "Hyprland topology cleanup is still pending";
            // The guardian still owns the lock and journal; leave it to finish.
            recovery.release(); return false;
        }
        bool ok = recovery->exitStatus() == QProcess::NormalExit && recovery->exitCode() == 0;
        recovery.reset();
        if (!ok) {
            // A killed guardian leaves a durable journal. A new guardian replays
            // it before admitting any new output, including after parent death.
            if (start()) {
                recovery->closeWriteChannel();
                ok = recovery->waitForFinished(12000) && recovery->exitStatus() == QProcess::NormalExit && recovery->exitCode() == 0;
                if (recovery->state() == QProcess::NotRunning) recovery.reset();
            }
        }
        if (!ok) error = "Hyprland topology recovery is pending; journal retained";
        return ok;
    }
    bool configure(int w, int h, int s, int p) {
        if (active && p != policy) { error = "Cannot change Hyprland display policy within a lease"; return false; }
        if (!recovery && !start()) return false;
        recovery->write(QJsonDocument(QJsonObject{{"width", w}, {"height", h}, {"scale", s}, {"displayPolicy", p}}).toJson(QJsonDocument::Compact) + '\n');
        QJsonObject response;
        if (!receive(response)) return false;
        layoutWarning = response["layoutWarning"].toBool();
        width = w; height = h; scale = s; policy = p; active = true; error.clear(); return true;
    }
    QJsonObject identity(int w, int h, int s) const {
        QJsonObject result{{"displayId", 1}, {"outputName", name}, {"backend", "hyprland"}, {"displayPolicy", policies},
            {"width", w}, {"height", h}, {"scale", s}, {"active", true}};
        if (layoutWarning || !policies) result["layoutWarning"] = true;
        return result;
    }
};
}

int restoreHyprlandDisplay(const QString& name) {
    if (!validName(name)) return 2;
    std::signal(SIGTERM, SIG_IGN); std::signal(SIGINT, SIG_IGN); std::signal(SIGPIPE, SIG_IGN);
    const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const auto signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (runtime.isEmpty() || signature.isEmpty()) { send({{"error", "Hyprland requires a private runtime directory and instance signature"}}); return 1; }
    const auto directory = runtime + "/deskport-hyprland/" + QString::fromLatin1(QCryptographicHash::hash(signature.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (!QDir().mkpath(directory)) return 1;
    QFile::setPermissions(runtime + "/deskport-hyprland", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QLockFile lock(directory + "/lease.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(3000)) { send({{"error", "Another DeskPort Hyprland topology guardian owns this compositor"}}); return 1; }
    Lease lease(name, directory + "/recovery.json");
    bool policies = false;
    lease.pendingRecovery = !lease.restore(); // Keep a verified workspace available during local repair.
    if (lease.pendingRecovery) lease.recoveryReason = lease.error;
    if (!lease.available(policies)) { send({{"error", lease.error}}); return 1; }
    send({{"ready", true}, {"displayPolicy", policies}, {"outputName", lease.name}});
    QByteArray buffer;
    char bytes[4096];
    bool ok = true;
    while (true) {
        const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        buffer.append(bytes, int(count));
        if (buffer.size() > 8192) { ok = false; break; }
        while (buffer.contains('\n')) {
            const auto end = buffer.indexOf('\n');
            const auto request = QJsonDocument::fromJson(buffer.left(end)).object(); buffer.remove(0, end + 1);
            const int w = request["width"].toInt(), h = request["height"].toInt(), s = request["scale"].toInt(), p = request["displayPolicy"].toInt(-1);
            if (w < 640 || w > 7680 || h < 360 || h > 4320 || w % 4 || h % 4 || (s != 1 && s != 2) || !DPDisplayPolicyValid(p)) {
                send({{"error", "Invalid Hyprland topology guard request"}}); ok = false; break;
            }
            if (!lease.configure(w, h, s, p)) { send({{"error", lease.error}}); ok = false; break; }
            send({{"ok", true}, {"layoutWarning", lease.layoutWarning}});
        }
        if (!ok) break;
    }
    bool restored = false;
    for (int attempt = 0; attempt < 3 && !restored; ++attempt) {
        restored = lease.restore();
        if (!restored) QThread::msleep(200);
    }
    if (!restored) fprintf(stderr, "Hyprland recovery pending: %s\n", qPrintable(lease.error));
    return restored && ok ? 0 : 1;
}

int runHyprlandDisplay(int width, int height) {
    Display display;
    if (!display.available()) { send({{"error", display.error}}); return 1; }
    if (qEnvironmentVariableIntValue("DESKPORT_DISPLAY_ON_DEMAND") == 1)
        send({{"ready", true}, {"active", false}, {"backend", "hyprland"}, {"displayPolicy", display.policies}, {"outputName", display.name}});
    else {
        if (!display.configure(width, height, 1, DP_DISPLAY_EXTEND)) { send({{"error", display.error}}); return 1; }
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
            const int policy = request.contains("displayPolicy") ? request["displayPolicy"].toInt(-1) : DP_DISPLAY_PRIMARY_MIRROR;
            QJsonObject response{{"seq", seq}};
            if (!seq || w < 640 || w > 7680 || h < 360 || h > 4320 || w % 4 || h % 4 || (s != 1 && s != 2) || !DPDisplayPolicyValid(policy) ||
                (!display.policies && policy != DP_DISPLAY_EXTEND)) {
                response["error"] = "Invalid or unsupported Hyprland virtual output request";
            } else if (!request["session"].toBool(true)) {
                if (!display.release()) { response["error"] = display.error; send(response); QCoreApplication::exit(1); return; }
                response["active"] = false; response["width"] = w; response["height"] = h; response["scale"] = s;
            } else {
                if (!display.configure(w, h, s, policy)) { response["error"] = display.error; send(response); QCoreApplication::exit(1); return; }
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
    const int result = QCoreApplication::exec();
    if (!display.release()) { send({{"error", display.error}}); return 1; }
    return result;
}
