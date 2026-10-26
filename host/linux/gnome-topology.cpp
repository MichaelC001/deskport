// SPDX-License-Identifier: GPL-3.0-or-later
#include "gnome-topology.h"
#include "gnome-display.h"
#include <QFile>
#include <QSaveFile>
#include <QLockFile>
#include <QJsonDocument>
#include <QThread>
#include <QElapsedTimer>
#include <cmath>
#include <cstdio>
#include <unistd.h>

namespace {
QDBusMessage invoke(const QString& method, const QList<QVariant>& args = {}) {
    auto message = QDBusMessage::createMethodCall("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig", "org.gnome.Mutter.DisplayConfig", method);
    message.setArguments(args);
    return QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
}
bool saveJournal(const QString& path, const QJsonObject& value) {
    QSaveFile file(path); const auto bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) &&
        file.write(bytes) == bytes.size() && file.commit();
}

QJsonArray specJson(const Spec& s) { return {std::get<0>(s.v), std::get<1>(s.v), std::get<2>(s.v), std::get<3>(s.v)}; }
QString name(const QJsonObject& monitor) { return monitor["spec"].toArray()[0].toString(); }
bool foreignVirtual(const QJsonObject& monitor) {
    const auto spec = monitor["spec"].toArray();
    return spec.size() == 4 && spec[1] == "MetaVendor" && spec[2] == "Virtual remote monitor";
}
QJsonObject monitor(const QJsonObject& layout, const QJsonArray& spec) {
    for (const auto& item : layout["monitors"].toArray()) if (item.toObject()["spec"].toArray() == spec) return item.toObject();
    return {};
}
QJsonArray groups(const QJsonObject& layout) { return layout["logical"].toArray(); }
QJsonArray members(const QJsonObject& group) { return group["monitors"].toArray(); }
QJsonObject currentMode(const QJsonObject& m) {
    for (const auto& mode : m["modes"].toArray()) if (mode.toObject()["current"].toBool()) return mode.toObject();
    return {};
}
QJsonObject member(const QJsonObject& m) { return {{"spec", m["spec"]}, {"mode", currentMode(m)}, {"properties", m["properties"]}}; }
// Mode IDs may change on hotplug; match the actual mode, never a connector alone.
QJsonObject matchingMode(const QJsonObject& m, const QJsonObject& wanted) {
    QJsonObject closest; double distance = .01;
    for (const auto& item : m["modes"].toArray()) {
        const auto mode = item.toObject();
        if (mode["width"] != wanted["width"] || mode["height"] != wanted["height"] ||
            mode["refresh-mode"] != wanted["refresh-mode"] || mode["interlaced"] != wanted["interlaced"]) continue;
        const double delta = std::abs(mode["refresh"].toDouble() - wanted["refresh"].toDouble());
        if (delta >= .01) continue;
        if (mode["id"] == wanted["id"]) return mode;
        if (delta < distance) { closest = mode; distance = delta; }
    }
    return closest;
}

bool sameGroups(QJsonArray expected, QJsonArray actual) {
    auto normalize = [](QJsonArray input) {
        QMap<QString, QByteArray> result;
        for (const auto& value : input) {
            auto group = value.toObject();
            QMap<QString, QJsonObject> sorted;
            for (const auto& item : members(group)) {
                auto m = item.toObject(); auto mode = m["mode"].toObject();
                mode.remove("id"); mode.remove("current"); mode.remove("scales");
                m["mode"] = mode;
                sorted.insert(QString::fromUtf8(QJsonDocument(m["spec"].toArray()).toJson(QJsonDocument::Compact)), m);
            }
            QJsonArray list; for (const auto& item : sorted) list.append(item);
            group["monitors"] = list;
            result.insert(sorted.firstKey(), QJsonDocument(group).toJson(QJsonDocument::Compact));
        }
        return result;
    };
    return normalize(expected) == normalize(actual);
}
}
void GnomeTopology::registerTypes() {
    qDBusRegisterMetaType<Spec>(); qDBusRegisterMetaType<QList<Spec>>(); qDBusRegisterMetaType<QList<double>>();
    qDBusRegisterMetaType<Mode>(); qDBusRegisterMetaType<QList<Mode>>(); qDBusRegisterMetaType<Monitor>(); qDBusRegisterMetaType<QList<Monitor>>();
    qDBusRegisterMetaType<Logical>(); qDBusRegisterMetaType<QList<Logical>>();
    qDBusRegisterMetaType<ConfigMonitor>(); qDBusRegisterMetaType<QList<ConfigMonitor>>();
    qDBusRegisterMetaType<ConfigLogical>(); qDBusRegisterMetaType<QList<ConfigLogical>>();
}
QJsonObject GnomeTopology::snapshot() {
    const auto response = invoke("GetCurrentState");
    if (response.type() == QDBusMessage::ErrorMessage || response.arguments().size() != 4) { error = "Mutter GetCurrentState failed: " + response.errorMessage(); return {}; }
    QJsonArray monitors, logical;
    for (const auto& item : qdbus_cast<QList<Monitor>>(response.arguments()[1])) {
        QJsonArray modes;
        for (const auto& mode : std::get<1>(item.v)) {
            QJsonArray scales; for (double scale : std::get<5>(mode.v)) scales.append(scale);
            modes.append(QJsonObject{{"id", std::get<0>(mode.v)}, {"width", std::get<1>(mode.v)}, {"height", std::get<2>(mode.v)},
                {"refresh", std::get<3>(mode.v)}, {"scales", scales}, {"current", std::get<6>(mode.v).value("is-current").toBool()},
                {"refresh-mode", std::get<6>(mode.v).value("refresh-rate-mode", QStringLiteral("fixed")).toString()},
                {"interlaced", std::get<6>(mode.v).value("is-interlaced").toBool()}});
        }
        const auto props = std::get<2>(item.v);
        QJsonObject preserved;
        if (props.contains("is-underscanning")) preserved["underscanning"] = props["is-underscanning"].toBool();
        if (props.contains("color-mode")) preserved["color-mode"] = props["color-mode"].toInt();
        if (props.contains("rgb-range")) preserved["rgb-range"] = props["rgb-range"].toInt();
        monitors.append(QJsonObject{{"spec", specJson(std::get<0>(item.v))}, {"modes", modes}, {"properties", preserved}, {"for-lease", props.value("is-for-lease").toBool()}});
    }
    QJsonObject layout{{"version", 1}, {"serial", double(response.arguments()[0].toUInt())}, {"monitors", monitors}};
    for (const auto& item : qdbus_cast<QList<Logical>>(response.arguments()[2])) {
        QJsonArray list;
        for (const auto& spec : std::get<5>(item.v)) {
            auto m = member(monitor(layout, specJson(spec)));
            if (m["mode"].toObject().isEmpty()) { error = "Cannot snapshot an active Mutter monitor mode"; return {}; }
            list.append(m);
        }
        if (list.isEmpty()) { error = "Mutter returned an empty logical monitor"; return {}; }
        logical.append(QJsonObject{{"x", std::get<0>(item.v)}, {"y", std::get<1>(item.v)}, {"scale", std::get<2>(item.v)},
            {"transform", int(std::get<3>(item.v))}, {"primary", std::get<4>(item.v)}, {"monitors", list}});
    }
    const auto props = qdbus_cast<QVariantMap>(response.arguments()[3]);
    layout["layout-mode"] = int(props.value("layout-mode", uint(1)).toUInt());
    layout["can-change-layout-mode"] = props.value("supports-changing-layout-mode").toBool();
    layout["logical"] = logical;
    return layout;
}
bool GnomeTopology::apply(const QJsonObject& layout) {
    auto current = snapshot(); if (current.isEmpty()) return false;
    if (sameGroups(groups(layout), groups(current)) && layout["layout-mode"] == current["layout-mode"]) return true;
    if (layout["layout-mode"] != current["layout-mode"] && !current["can-change-layout-mode"].toBool()) {
        error = "Mutter cannot restore the original layout coordinate mode"; return false;
    }
    QList<ConfigLogical> configuration;
    for (const auto& value : groups(layout)) {
        const auto group = value.toObject(); QList<ConfigMonitor> list;
        for (const auto& item : members(group)) {
            const auto m = item.toObject(); const auto physical = monitor(current, m["spec"].toArray());
            const auto mode = matchingMode(physical, m["mode"].toObject());
            if (mode.isEmpty() || !mode["scales"].toArray().contains(group["scale"])) { error = "Mutter monitor identity, mode or scale changed before apply"; return false; }
            auto properties = m["properties"].toObject().toVariantMap();
            for (const auto& key : {"color-mode", "rgb-range"})
                if (properties.contains(key)) properties[key] = uint(properties[key].toUInt());
            list.append({{name(physical), mode["id"].toString(), properties}});
        }
        if (list.isEmpty()) { error = "Refusing empty Mutter monitor group"; return false; }
        configuration.append({{group["x"].toInt(), group["y"].toInt(), group["scale"].toDouble(), uint(group["transform"].toInt()), group["primary"].toBool(), list}});
    }
    if (configuration.isEmpty()) { error = "Refusing to disable every Mutter output"; return false; }
    QVariantMap options;
    QList<Spec> leased;
    for (const auto& value : current["monitors"].toArray()) {
        const auto m = value.toObject(); if (!m["for-lease"].toBool()) continue;
        const auto s = m["spec"].toArray();
        leased.append({{s[0].toString(), s[1].toString(), s[2].toString(), s[3].toString()}});
    }
    // Preserve compositor/DRM leases belonging to other clients. Omission would
    // replace the global for-lease set when applying an otherwise local layout.
    if (!leased.isEmpty()) options["monitors-for-lease"] = QVariant::fromValue(leased);
    if (current["can-change-layout-mode"].toBool() && layout.contains("layout-mode")) options["layout-mode"] = uint(layout["layout-mode"].toInt());
    const QList<QVariant> args{uint(current["serial"].toDouble()), uint(0), QVariant::fromValue(configuration), options};
    auto result = invoke("ApplyMonitorsConfig", args);
    if (result.type() == QDBusMessage::ErrorMessage) { error = "Mutter rejected display configuration: " + result.errorMessage(); return false; }
    auto temporary = args; temporary[1] = uint(1);
    result = invoke("ApplyMonitorsConfig", temporary);
    if (result.type() == QDBusMessage::ErrorMessage) { error = "Mutter failed temporary display configuration: " + result.errorMessage(); return false; }
    const auto observed = snapshot();
    if (!sameGroups(groups(layout), groups(observed)) || layout["layout-mode"] != observed["layout-mode"]) { error = "Mutter display configuration acknowledgment did not match observed layout"; return false; }
    return true;
}
bool GnomeTopology::supported(const QJsonObject& baseline, int policy, int width, int height, int scale) {
    if (policy < 0 || policy > 2) { error = "Unsupported Mutter display policy"; return false; }
    if (policy != 2) for (const auto& value : baseline.value("admissionMonitors").isArray() ? baseline["admissionMonitors"].toArray() : baseline["monitors"].toArray()) {
        if (foreignVirtual(value.toObject())) {
            error = "Another client owns a Mutter virtual monitor; only extension is safe"; return false;
        }
    }
    if (policy != 0) return true;
    // Mutter clones share a mode size, transform and scale. Preserve physical
    // modes: no arbitrary scaling/letterboxing API exists on this interface.
    for (const auto& value : groups(baseline)) {
        const auto group = value.toObject();
        if (group["scale"].toDouble() != scale || group["transform"].toInt() != 0) {
            error = "Mutter mirroring requires the existing physical scale and rotation to match the workspace"; return false;
        }
        for (const auto& item : members(group)) {
            const auto mode = item.toObject()["mode"].toObject();
            if (mode["width"].toInt() != width || mode["height"].toInt() != height) {
                error = "Mutter cannot mirror arbitrary workspace pixels while preserving physical monitor modes; select extension or primary-only"; return false;
            }
        }
    }
    return true;
}
bool GnomeTopology::configure(const QJsonObject& baseline, const QString& owned, int policy, int width, int height, int scale) {
    auto current = snapshot(); if (current.isEmpty() || !supported(baseline, policy, width, height, scale)) return false;
    QJsonObject virtualMonitor;
    for (const auto& value : current["monitors"].toArray()) if (name(value.toObject()) == owned) virtualMonitor = value.toObject();
    if (virtualMonitor["spec"] != baseline["ownedSpec"]) {
        error = "The owned Mutter monitor identity disappeared or changed"; return false;
    }
    if (policy != 2) for (const auto& value : current["monitors"].toArray()) {
        const auto other = value.toObject();
        if (other["spec"] != baseline["ownedSpec"] && foreignVirtual(other)) {
            // A newly attached screen-cast output can make Mutter re-enable
            // physical outputs. Do not race its view creation with another
            // takeover; keep the verified workspace and report degradation.
            error = "Another client attached a Mutter virtual monitor; physical policy is suspended";
            return false;
        }
    }
    const auto mode = currentMode(virtualMonitor);
    if (mode["width"].toInt() != width || mode["height"].toInt() != height || !mode["scales"].toArray().contains(scale)) {
        error = "Mutter has not applied the requested owned pixels or cannot support its scale"; return false;
    }
    QJsonArray target, mirrors{member(virtualMonitor)};
    if (policy == 0) for (const auto& original : groups(baseline)) for (const auto& old : members(original.toObject()))
        if (!monitor(current, old.toObject()["spec"].toArray()).isEmpty()) mirrors.append(old);
    bool found = false;
    for (const auto& value : groups(current)) {
        auto group = value.toObject(); QJsonArray keep;
        for (const auto& item : members(group)) {
            const auto m = item.toObject();
            if (m["spec"] == virtualMonitor["spec"]) {
                if (policy == 2 && members(group).size() != 1) { error = "Owned extension unexpectedly belongs to a mirror group"; return false; }
                found = true; continue;
            }
            bool wasActive = false;
            for (const auto& original : groups(baseline)) for (const auto& old : members(original.toObject()))
                if (old.toObject()["spec"] == m["spec"]) wasActive = true;
            if (policy == 2 || !wasActive) keep.append(m); // Hotplugged outputs are never takeover targets.
        }
        if (!keep.isEmpty()) {
            if (policy != 2 && group["primary"].toBool()) { error = "An unowned output became primary; refusing to take it over"; return false; }
            group["monitors"] = keep; target.append(group);
        }
        if (policy == 2 && members(group).size() == 1 && members(group)[0].toObject()["spec"] == virtualMonitor["spec"]) {
            group["monitors"] = QJsonArray{member(virtualMonitor)}; group["scale"] = scale; target.append(group);
        }
    }
    if (!found) { error = "Owned Mutter output is not active"; return false; }
    if (policy != 2) target.prepend(QJsonObject{{"x", 0}, {"y", 0}, {"scale", scale}, {"transform", 0}, {"primary", true}, {"monitors", mirrors}});
    current["logical"] = target;
    return apply(current);
}
bool GnomeTopology::workspaceVerified(const QJsonObject& baseline, int width, int height, int scale) {
    const auto current = snapshot(); if (current.isEmpty()) return false;
    const auto owned = monitor(current, baseline["ownedSpec"].toArray());
    const auto mode = currentMode(owned);
    if (mode["width"].toInt() != width || mode["height"].toInt() != height) return false;
    for (const auto& value : groups(current)) {
        const auto group = value.toObject();
        for (const auto& item : members(group))
            if (item.toObject()["spec"] == baseline["ownedSpec"]) return group["scale"].toDouble() == scale;
    }
    return false;
}
bool GnomeTopology::restore(const QJsonObject& baseline, bool prepareRemoval) {
    if (prepareRemoval && baseline["policy"].toInt(2) == 2) return true;
    auto current = snapshot(); if (current.isEmpty()) return false;
    if (!prepareRemoval && sameGroups(groups(baseline), groups(current)) && baseline["layout-mode"] == current["layout-mode"]) return true;
    QJsonArray target;
    // Preserve new monitors in their current groups, remove only the lease's
    // owned virtual output, restore attached originals by complete identity.
    for (const auto& value : groups(current)) {
        auto group = value.toObject(); QJsonArray keep;
        for (const auto& item : members(group)) {
            const auto m = item.toObject();
            if (m["spec"] == baseline["ownedSpec"]) continue;
            const auto saved = monitor(baseline, m["spec"].toArray());
            if (foreignVirtual(m) || saved.isEmpty() || saved["for-lease"].toBool()) keep.append(m);
        }
        if (!keep.isEmpty()) { group["monitors"] = keep; group["primary"] = false; target.append(group); }
    }
    for (const auto& value : groups(baseline)) {
        auto group = value.toObject(); QJsonArray keep;
        for (const auto& item : members(group)) if (!foreignVirtual(item.toObject()) && !monitor(current, item.toObject()["spec"].toArray()).isEmpty()) keep.append(item);
        if (!keep.isEmpty()) { group["monitors"] = keep; target.append(group); }
    }
    // A detached primary must not leave a headless user without local control.
    bool primary = false; for (const auto& value : target) primary |= value.toObject()["primary"].toBool();
    if (!primary && !target.isEmpty()) { auto first = target[0].toObject(); first["primary"] = true; target[0] = first; }
    if (target.isEmpty()) {
        // With no attached original/new outputs, leave the owned headless
        // output to Mutter's connection lifetime, rather than applying all-off.
        return true;
    }
    if (prepareRemoval) {
        // Never disable a live RecordVirtual source through DisplayConfig.
        // Some Mutter versions block while tearing down its active view. First
        // restore physical control with the owned output still extended, then
        // let the owner's ScreenCast connection remove it before final replay.
        for (const auto& value : groups(current)) {
            auto ownedGroup = value.toObject(); QJsonArray own;
            for (const auto& item : members(ownedGroup))
                if (item.toObject()["spec"] == baseline["ownedSpec"]) own.append(item);
            if (own.isEmpty()) continue;
            int right = 0;
            for (const auto& value : target) {
                const auto group = value.toObject(); const auto mode = members(group)[0].toObject()["mode"].toObject();
                const int pixels = mode[(group["transform"].toInt() % 2) ? "height" : "width"].toInt();
                const double scale = current["layout-mode"].toInt(1) == 2 ? 1. : group["scale"].toDouble();
                right = qMax(right, group["x"].toInt() + int(std::round(pixels / scale)));
            }
            ownedGroup["monitors"] = own; ownedGroup["x"] = right; ownedGroup["y"] = 0; ownedGroup["primary"] = false;
            target.append(ownedGroup);
            break;
        }
    }
    current["logical"] = target;
    if (baseline.contains("layout-mode")) current["layout-mode"] = baseline["layout-mode"];
    return apply(current);
}
int restoreGnomeDisplay(const QString& journal) {
    GnomeTopology::registerTypes(); GnomeTopology topology;
    QLockFile lock(journal + ".lock");
    if (!lock.tryLock(0)) return 1;
    QJsonObject baseline;
    bool pending = false;
    auto recover = [&] { return baseline.isEmpty() || topology.restore(baseline); };
    QFile previous(journal);
    if (previous.exists()) {
        if (!previous.open(QIODevice::ReadOnly)) return 1;
        QJsonParseError parse; baseline = QJsonDocument::fromJson(previous.readAll(), &parse).object(); previous.close();
        if (parse.error != QJsonParseError::NoError || baseline["version"].toInt() != 1 || !baseline["monitors"].isArray() || !baseline["logical"].isArray()) return 1;
        const auto leaseOwner = baseline["leaseOwner"].toString();
        if (QDBusConnection::sessionBus().interface()->isServiceRegistered(leaseOwner).value()) {
            const auto current = topology.snapshot();
            if (!baseline.contains("ownedSpec") || current.isEmpty() || !monitor(current, baseline["ownedSpec"].toArray()).isEmpty()) return 1;
        }
        if (!recover()) {
            pending = true;
            if (!baseline.contains("recovery-policy")) baseline["recovery-policy"] = baseline["policy"];
            baseline["recovery-error"] = topology.error;
            baseline["recovery-attempt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            if (!saveJournal(journal, baseline)) return 1;
            fprintf(stderr, "Mutter previous layout recovery remains pending; retaining its original snapshot\n");
        } else { QFile::remove(journal); baseline = {}; }
    }
    auto reply = [](const char* message) { puts(message); fflush(stdout); };
    reply(pending ? "pending" : "ready");
    QByteArray buffer; char byte;
    while (true) {
        const auto count = read(STDIN_FILENO, &byte, 1);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        if (byte != '\n') { buffer.append(byte); if (buffer.size() > 1024 * 1024) return 2; continue; }
        if (buffer == "restore") { reply(baseline.isEmpty() || topology.restore(baseline, true) ? "restored" : "failed"); buffer.clear(); continue; }
        QJsonParseError parse; auto value = QJsonDocument::fromJson(buffer, &parse).object(); buffer.clear();
        if (parse.error != QJsonParseError::NoError || value["version"].toInt() != 1 || !value["monitors"].isArray() || !value["logical"].isArray()) return 2;
        if (!saveJournal(journal, value)) return 1;
        baseline = value; reply("armed");
    }
    // Observe destruction, rather than guessing how long the compositor needs.
    // A pre-removal replay does not disarm this post-removal replay.
    QElapsedTimer removal; removal.start();
    while (baseline.contains("ownedSpec")) {
        const auto current = topology.snapshot();
        if (!current.isEmpty() && monitor(current, baseline["ownedSpec"].toArray()).isEmpty()) break;
        if (removal.elapsed() >= 4000) {
            topology.restore(baseline, true); // Restore local control, retain uncertain recovery.
            fprintf(stderr, "Mutter owned output removal was not observed; journal retained\n");
            return 1;
        }
        QThread::msleep(50);
    }
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (recover()) { QFile::remove(journal); return 0; }
        QThread::msleep(250);
    }
    fprintf(stderr, "Mutter recovery journal retained: %s\n", qPrintable(topology.error));
    return 1;
}
