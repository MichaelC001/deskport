// SPDX-License-Identifier: GPL-3.0-or-later
// Lease an explicitly selected, inactive xf86-video-dummy output. XRandR cannot
// manufacture connectors; physical connectors and global X11 DPI are untouched.
#include "x11-display.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QRect>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <memory>
#include <poll.h>
#include <unistd.h>

namespace {
int xerror = 0;
int onError(::Display*, XErrorEvent* event) { xerror = event->error_code; return 0; }
void send(const QJsonObject& object) {
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); fflush(stdout);
}
using Resources = std::unique_ptr<XRRScreenResources, decltype(&XRRFreeScreenResources)>;
using Output = std::unique_ptr<XRROutputInfo, decltype(&XRRFreeOutputInfo)>;
using Crtc = std::unique_ptr<XRRCrtcInfo, decltype(&XRRFreeCrtcInfo)>;
class Lease {
    ::Display* d = nullptr;
    Window root = 0, owner = 0;
    Atom selection = 0;
    RROutput output = 0;
    RRCrtc crtc = 0;
    QString name = qEnvironmentVariable("DESKPORT_X11_OUTPUT"), journal, mainJournal, token, serverId;
    QJsonObject baseline;
    QList<RRMode> modes;
    bool active = false, pendingWarning = false;
    int width = 0, height = 0, originX = 0, lastFbW = 0, lastFbH = 0;
    Resources resources() {
        // Xlib caches screen millimeters. Refresh that cache from RandR events
        // before taking a later lease snapshot or preserving current screen DPI.
        XSync(d, False);
        while (XPending(d)) { XEvent event; XNextEvent(d, &event); XRRUpdateConfiguration(&event); }
        return Resources(XRRGetScreenResources(d, root), XRRFreeScreenResources);
    }
    Output info(XRRScreenResources* r, RROutput id) { return Output(XRRGetOutputInfo(d, r, id), XRRFreeOutputInfo); }
    Crtc crtcInfo(XRRScreenResources* r, RRCrtc id) { return Crtc(XRRGetCrtcInfo(d, r, id), XRRFreeCrtcInfo); }
    bool sync() { XSync(d, False); if (xerror) { error = "X server rejected the virtual output operation"; xerror = 0; return false; } return true; }
    QString modeName(XRRScreenResources* r, RRMode id) {
        for (int i = 0; i < r->nmode; ++i) if (r->modes[i].id == id) return QString::fromLatin1(r->modes[i].name, r->modes[i].nameLength);
        return {};
    }
    QJsonArray otherCrtcs(XRRScreenResources* r) {
        QJsonArray result;
        for (int i = 0; i < r->ncrtc; ++i) {
            auto c = crtcInfo(r, r->crtcs[i]);
            if (!c || !c->mode) continue;
            bool owned = false;
            QJsonArray outputs;
            for (int j = 0; j < c->noutput; ++j) { owned |= c->outputs[j] == output; outputs.append(double(c->outputs[j])); }
            if (!owned) result.append(QJsonObject{{"id", double(r->crtcs[i])}, {"mode", double(c->mode)}, {"x", c->x}, {"y", c->y},
                {"width", int(c->width)}, {"height", int(c->height)}, {"rotation", c->rotation}, {"outputs", outputs}, {"plain", plainCrtc(r, r->crtcs[i])}});
        }
        return result;
    }
    bool plainCrtc(XRRScreenResources* r, RRCrtc id) {
        XRRCrtcTransformAttributes* attributes = nullptr;
        if (!XRRGetCrtcTransform(d, id, &attributes) || !attributes) return false;
        bool plain = true;
        for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) {
            const XFixed expected = row == col ? 65536 : 0;
            plain &= attributes->currentTransform.matrix[row][col] == expected && attributes->pendingTransform.matrix[row][col] == expected;
        }
        XFree(attributes);
        auto panning = XRRGetPanning(d, r, id);
        if (!panning) return false;
        plain &= panning->width == 0 && panning->height == 0;
        XRRFreePanning(panning);
        return plain;
    }
    bool writeJournal() {
        QSaveFile file(journal);
        if (!file.open(QIODevice::WriteOnly)) { error = "Cannot journal the X11 virtual output lease"; return false; }
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        file.write(QJsonDocument(baseline).toJson(QJsonDocument::Compact));
        if (!file.commit()) { error = "Cannot commit the X11 recovery journal"; return false; }
        return true;
    }
    bool loadJournal(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) { error = "Cannot read the pending X11 recovery journal"; return false; }
        baseline = QJsonDocument::fromJson(file.readAll()).object(); token = baseline["token"].toString();
        if (baseline["server"].toString() != serverId || baseline["root"].toDouble() != root || baseline["output"].toDouble() != output ||
            !QRegularExpression("^DeskPort-[a-f0-9-]{36}$").match(token).hasMatch()) {
            error = "X11 recovery journal belongs to a different server; inspect it before retrying"; baseline = {}; return false;
        }
        lastFbW = baseline["lastWidth"].toInt(); lastFbH = baseline["lastHeight"].toInt();
        for (const auto& key : {"width", "height", "lastWidth", "lastHeight"}) {
            const int value = baseline[key].toInt();
            if (value < 64 || value > 32767) { error = "Invalid X11 framebuffer recovery dimensions"; baseline = {}; return false; }
        }
        for (const auto& key : {"mmWidth", "mmHeight"}) {
            const int value = baseline[key].toInt();
            if (value < 1 || value > 1000000) { error = "Invalid X11 framebuffer recovery density"; baseline = {}; return false; }
        }
        return true;
    }
    bool resourcesReleased() {
        auto r = resources(); if (!r) return false;
        auto o = info(r.get(), output); if (!o || o->crtc) return false;
        for (int i = 0; i < r->nmode; ++i) if (modeName(r.get(), r->modes[i].id).startsWith(token + "-")) return false;
        return true;
    }
    void clearBaseline() { baseline = {}; modes.clear(); active = false; crtc = 0; }
    void retryPending() {
        if (mainJournal.isEmpty()) return;
        pendingWarning = false;
        const QFileInfo file(mainJournal);
        const auto filter = file.completeBaseName() + ".pending-*.json";
        for (const auto& name : QDir(file.absolutePath()).entryList({filter}, QDir::Files, QDir::Name)) {
            journal = file.absolutePath() + "/" + name;
            if (!loadJournal(journal) || !restore()) {
                pendingWarning = true;
                clearBaseline(); // Keep the file; never overwrite the changed layout.
            }
        }
        journal = mainJournal; error.clear();
    }
    void size(int w, int h, int mmw, int mmh) { XRRSetScreenSize(d, root, w, h, std::max(1, mmw), std::max(1, mmh)); }
public:
    QString error;
    ~Lease() {
        if (d) { release(); if (owner) XDestroyWindow(d, owner); XCloseDisplay(d); }
    }
    bool open() {
        // DUMMY connectors are provided by xf86-video-dummy, not DRM/physical
        // outputs. A name alone is insufficient: require the full driver's
        // connector family, no EDID, no active CRTC, and an exclusive selection.
        if (!QRegularExpression("^DUMMY(?:[1-9]|1[0-5])$").match(name).hasMatch()) {
            error = "X11 adaptive display requires an explicitly selected inactive DUMMY1-DUMMY15 output (DESKPORT_X11_OUTPUT); physical outputs are unsupported"; return false;
        }
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") || qgetenv("XDG_SESSION_TYPE").toLower() == "wayland") { error = "XWayland output emulation is not a virtual display backend"; return false; }
        if (qEnvironmentVariableIsEmpty("DISPLAY")) { error = "The selected X11 backend requires DISPLAY; no physical fallback will be selected"; return false; }
        d = XOpenDisplay(nullptr);
        if (!d) { error = "Cannot open the selected X11 session"; return false; }
        XSetErrorHandler(onError);
        int major = 1, minor = 5;
        if (!XRRQueryVersion(d, &major, &minor) || major < 1 || (major == 1 && minor < 3)) { error = "XRandR 1.3 or newer is required"; return false; }
        root = DefaultRootWindow(d);
        XRRSelectInput(d, root, RRScreenChangeNotifyMask);
        auto r = resources();
        if (!r) { error = "Cannot enumerate XRandR resources"; return false; }
        bool dummy0 = false;
        for (int i = 0; i < r->noutput; ++i) {
            auto o = info(r.get(), r->outputs[i]); if (!o) continue;
            const auto n = QString::fromLatin1(o->name, o->nameLen);
            if (!QRegularExpression("^DUMMY(?:[0-9]|1[0-5])$").match(n).hasMatch()) { error = "Mixed or non-dummy XRandR drivers are unsupported"; return false; }
            dummy0 |= n == "DUMMY0";
            if (n == name) output = r->outputs[i];
        }
        if (!dummy0 || !output) { error = "The selected xf86-video-dummy connector is unavailable; no driver or Xorg configuration will be installed"; return false; }
        const auto atom = XInternAtom(d, "EDID", True);
        if (atom) {
            Atom type = None; int format = 0; unsigned long count = 0, after = 0; unsigned char* bytes = nullptr;
            const int status = XRRGetOutputProperty(d, output, atom, 0, 256, False, False, AnyPropertyType, &type, &format, &count, &after, &bytes);
            if (bytes) XFree(bytes);
            if (status != Success) { error = "Cannot verify the selected output EDID property"; return false; }
            if (count || after) { error = "Refusing an EDID-bearing output"; return false; }
        }
        selection = XInternAtom(d, ("_DESKPORT_X11_LEASE_" + QByteArray::number(output)).constData(), False);
        owner = XCreateSimpleWindow(d, root, 0, 0, 1, 1, 0, 0, 0);
        XGrabServer(d);
        // X resource IDs are reused after a server restart. An ephemeral root
        // cookie prevents a stale journal from matching a different X server.
        const Atom cookie = XInternAtom(d, "_DESKPORT_X11_SERVER_ID", False);
        Atom type = None; int format = 0; unsigned long count = 0, after = 0; unsigned char* bytes = nullptr;
        XGetWindowProperty(d, root, cookie, 0, 64, False, XA_STRING, &type, &format, &count, &after, &bytes);
        if (type == XA_STRING && format == 8 && count == 36 && !after) serverId = QString::fromLatin1(reinterpret_cast<char*>(bytes), int(count));
        if (bytes) XFree(bytes);
        if (!QRegularExpression("^[a-f0-9-]{36}$").match(serverId).hasMatch()) {
            serverId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const auto value = serverId.toLatin1();
            XChangeProperty(d, root, cookie, XA_STRING, 8, PropModeReplace, reinterpret_cast<const unsigned char*>(value.constData()), value.size());
        }
        const bool busy = XGetSelectionOwner(d, selection) != None;
        if (!busy) XSetSelectionOwner(d, selection, owner, CurrentTime);
        XUngrabServer(d); XSync(d, False);
        if (busy || XGetSelectionOwner(d, selection) != owner) { error = "The X11 virtual connector is already leased"; return false; }
        auto dir = qEnvironmentVariable("DESKPORT_DISPLAY_STATE_DIR");
        if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/DeskPort/x11";
        if (!QDir().mkpath(dir)) { error = "Cannot create the X11 recovery directory"; return false; }
        journal = mainJournal = dir + "/x11-" + serverId + "-" + name + ".json";
        if (QFile::exists(journal) && (!loadJournal(journal) || !release())) return false;
        retryPending();
        r = resources();
        if (!r) { error = "Cannot enumerate recovered X11 outputs"; return false; }
        auto o = info(r.get(), output);
        if (!o || o->crtc) { error = "The selected virtual connector is active; refusing to take over an existing workspace"; return false; }
        for (const auto& value : otherCrtcs(r.get())) if (!value.toObject()["plain"].toBool()) {
            error = "X11 independent workspaces require existing outputs without RandR transforms or panning"; return false;
        }
        return true;
    }
    QJsonObject identity() const {
        QJsonObject result{{"backend", "x11"}, {"outputName", name}, {"displayId", 1}, {"active", active},
            {"width", width}, {"height", height}, {"scale", 1}, {"leaseToken", token}};
        if (pendingWarning) result["layoutWarning"] = "X11 framebuffer recovery is pending; local layout changes are preserved";
        return result;
    }
    bool healthy() {
        if (!active) return true;
        if (XGetSelectionOwner(d, selection) != owner) return false;
        auto r = resources(); if (!r) return false;
        auto o = info(r.get(), output); if (!o || o->crtc != crtc) return false;
        auto c = crtcInfo(r.get(), crtc);
        if (!c || c->noutput != 1 || c->outputs[0] != output || c->width != unsigned(width) || c->height != unsigned(height) ||
            c->rotation != RR_Rotate_0 || !plainCrtc(r.get(), crtc) || c->x != originX || c->y != 0 || !modeName(r.get(), c->mode).startsWith(token + "-")) return false;
        const QRect owned(c->x, c->y, int(c->width), int(c->height));
        for (const auto& value : otherCrtcs(r.get())) {
            const auto other = value.toObject();
            if (!other["plain"].toBool()) return false;
            if (owned.intersects(QRect(other["x"].toInt(), other["y"].toInt(), other["width"].toInt(), other["height"].toInt()))) return false;
        }
        return true;
    }
    bool configure(int w, int h) {
        auto r = resources(); if (!r) { error = "Cannot enumerate XRandR resources"; return false; }
        if (!active) {
            auto o = info(r.get(), output);
            if (!o || o->crtc) { error = "The selected virtual connector is no longer idle"; return false; }
            for (int i = 0; i < o->ncrtc; ++i) { auto c = crtcInfo(r.get(), o->crtcs[i]); if (c && !c->mode && !c->noutput && c->rotation == RR_Rotate_0 && plainCrtc(r.get(), o->crtcs[i])) { crtc = o->crtcs[i]; break; } }
            if (!crtc) { error = "The driver has no free untransformed CRTC with disabled panning for an independent virtual workspace"; return false; }
            XWindowAttributes a; XGetWindowAttributes(d, root, &a);
            token = "DeskPort-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            baseline = {{"server", serverId}, {"root", double(root)}, {"output", double(output)}, {"token", token}, {"width", a.width}, {"height", a.height},
                {"mmWidth", DisplayWidthMM(d, DefaultScreen(d))}, {"mmHeight", DisplayHeightMM(d, DefaultScreen(d))},
                {"primary", double(XRRGetOutputPrimary(d, root))}, {"others", otherCrtcs(r.get())}};
        } else if (!healthy() || otherCrtcs(r.get()) != baseline["others"].toArray()) {
            error = "The X11 display topology changed during the virtual lease"; return false;
        }
        for (const auto& value : baseline["others"].toArray()) if (!value.toObject()["plain"].toBool()) {
            error = "X11 existing outputs use unsupported transforms or panning"; clearBaseline(); return false;
        }
        if (active && w == width && h == height) return true;
        int x = 0, fbH = h;
        for (const auto& value : baseline["others"].toArray()) {
            auto c = value.toObject(); x = std::max(x, c["x"].toInt() + c["width"].toInt()); fbH = std::max(fbH, c["y"].toInt() + c["height"].toInt());
        }
        const int fbW = x + w;
        int minW, minH, maxW, maxH;
        if (!XRRGetScreenSizeRange(d, root, &minW, &minH, &maxW, &maxH) || fbW > maxW || fbH > maxH) { error = "The XRandR framebuffer limit cannot fit this independent workspace"; return false; }
        lastFbW = std::max({fbW, minW, baseline["width"].toInt()});
        lastFbH = std::max({fbH, minH, baseline["height"].toInt()});
        baseline["lastWidth"] = lastFbW; baseline["lastHeight"] = lastFbH;
        if (!writeJournal()) return false; // before the first mutation
        QByteArray mode = (token + "-" + QString::number(w) + "x" + QString::number(h)).toLatin1();
        XRRModeInfo m{}; m.name = mode.data(); m.nameLength = mode.size(); m.width = w; m.height = h;
        // Dummy has no scanout timings. These bounded, non-interlaced timings
        // keep RandR's refresh close to 60 Hz without inventing physical modes.
        m.hSyncStart = w + 8; m.hSyncEnd = w + 16; m.hTotal = w + 32;
        m.vSyncStart = h + 1; m.vSyncEnd = h + 4; m.vTotal = h + 8;
        m.dotClock = static_cast<unsigned long>(m.hTotal) * m.vTotal * 60;
        xerror = 0;
        RRMode id = XRRCreateMode(d, root, &m);
        if (!sync() || !id) { error = "The driver rejected a dynamic virtual mode"; return false; }
        modes.append(id);
        XRRAddOutputMode(d, output, id);
        if (!sync()) return false;
        // Disable only our own CRTC before a shrinking framebuffer transaction.
        if (active && XRRSetCrtcConfig(d, r.get(), crtc, CurrentTime, 0, 0, None, RR_Rotate_0, nullptr, 0) != Success) { error = "Cannot resize the owned virtual CRTC"; return false; }
        // RandR root dimensions are global even for an extended connector.
        // Preserve screen DPI (to integer-millimeter rounding) as the root grows;
        // do not write Xft.dpi/XSETTINGS or pretend this is per-output UI scale.
        const int mmW = std::max(1, int(qRound(double(lastFbW) * baseline["mmWidth"].toInt() / baseline["width"].toInt())));
        const int mmH = std::max(1, int(qRound(double(lastFbH) * baseline["mmHeight"].toInt() / baseline["height"].toInt())));
        size(lastFbW, lastFbH, mmW, mmH);
        if (!sync()) return false;
        r = resources();
        if (!r || XRRSetCrtcConfig(d, r.get(), crtc, CurrentTime, x, 0, id, RR_Rotate_0, &output, 1) != Success || !sync()) {
            error = "The XRandR driver could not activate the requested independent mode"; return false;
        }
        active = true; width = w; height = h; originX = x;
        if (!healthy()) { error = "Observed X11 pixels or output identity differ from the request"; return false; }
        // Only one custom mode per active lease is needed; remove stale modes.
        for (auto old : modes) if (old != id) { XRRDeleteOutputMode(d, output, old); XRRDestroyMode(d, old); }
        modes = {id};
        return sync();
    }
    bool restore() {
        if (baseline.isEmpty()) return true;
        auto r = resources(); if (!r) { error = "X11 recovery cannot enumerate outputs"; return false; }
        auto o = info(r.get(), output);
        if (o && o->crtc) {
            auto c = crtcInfo(r.get(), o->crtc);
            if (!c || c->noutput != 1 || !modeName(r.get(), c->mode).startsWith(token + "-")) {
                error = "X11 recovery refused a connector changed by another owner"; return false;
            }
            if (XRRSetCrtcConfig(d, r.get(), o->crtc, CurrentTime, 0, 0, None, RR_Rotate_0, nullptr, 0) != Success) { error = "Cannot release the owned X11 output"; return false; }
        }
        r = resources();
        XWindowAttributes a; XGetWindowAttributes(d, root, &a);
        const bool unchanged = r && otherCrtcs(r.get()) == baseline["others"].toArray() && XRRGetOutputPrimary(d, root) == baseline["primary"].toDouble();
        const int expectedMmW = qRound(double(lastFbW) * baseline["mmWidth"].toInt() / std::max(1, baseline["width"].toInt()));
        const int expectedMmH = qRound(double(lastFbH) * baseline["mmHeight"].toInt() / std::max(1, baseline["height"].toInt()));
        const bool rootOwned = a.width == lastFbW && a.height == lastFbH &&
            DisplayWidthMM(d, DefaultScreen(d)) == expectedMmW && DisplayHeightMM(d, DefaultScreen(d)) == expectedMmH;
        if (unchanged && rootOwned)
            size(baseline["width"].toInt(), baseline["height"].toInt(), baseline["mmWidth"].toInt(), baseline["mmHeight"].toInt());
        if (r) for (int i = 0; i < r->nmode; ++i) if (modeName(r.get(), r->modes[i].id).startsWith(token + "-")) {
            XRRDeleteOutputMode(d, output, r->modes[i].id); XRRDestroyMode(d, r->modes[i].id);
        }
        if (!sync()) return false;
        r = resources(); o = r ? info(r.get(), output) : Output(nullptr, XRRFreeOutputInfo);
        if (!o || o->crtc) { error = "X11 virtual-output removal was not observed"; return false; }
        XGetWindowAttributes(d, root, &a);
        if (!unchanged || a.width != baseline["width"].toInt() || a.height != baseline["height"].toInt()) {
            error = "X11 layout changed or framebuffer restoration was not observed; connector released, recovery journal retained for inspection"; return false;
        }
        if (!QFile::remove(journal) && QFile::exists(journal)) { error = "Cannot retire the restored X11 journal"; return false; }
        clearBaseline();
        return true;
    }
    bool release() {
        if (!restore()) {
            // A failed root-size restoration must not indefinitely block a new
            // independent lease once its connector and modes are safely gone.
            if (!resourcesReleased() || baseline.isEmpty()) return false;
            const auto pending = mainJournal.left(mainJournal.size() - 5) + ".pending-" + token + ".json";
            if (!QFile::rename(journal, pending)) { error = "Cannot retain the pending X11 framebuffer journal"; return false; }
            clearBaseline();
        }
        retryPending();
        return true;
    }
};
}

int guardX11Display() {
    std::signal(SIGTERM, SIG_IGN); std::signal(SIGINT, SIG_IGN); std::signal(SIGPIPE, SIG_IGN);
    Lease lease;
    if (!lease.open()) { send({{"error", lease.error}}); return 1; }
    auto ready = lease.identity(); ready["ready"] = true; send(ready);
    QByteArray buffer;
    while (true) {
        pollfd fd{STDIN_FILENO, POLLIN, 0};
        int result = poll(&fd, 1, 1000);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 || !lease.healthy()) { send({{"error", "The owned X11 virtual output disappeared or changed"}}); return 1; }
        if (!result) continue;
        char bytes[4096]; auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return lease.release() ? 0 : 1;
        buffer.append(bytes, int(count)); if (buffer.size() > 8192) return 2;
        while (buffer.contains('\n')) {
            const int end = buffer.indexOf('\n');
            const auto request = QJsonDocument::fromJson(buffer.left(end)).object(); buffer.remove(0, end + 1);
            const int seq = request["seq"].toInt(), w = request["width"].toInt(), h = request["height"].toInt(), scale = request["scale"].toInt();
            QJsonObject reply{{"seq", seq}};
            if (!seq || w < 640 || w > 7680 || h < 360 || h > 4320 || w % 4 || h % 4 || (scale != 1 && scale != 2)) reply["error"] = "Invalid X11 workspace request";
            else if (request.contains("displayPolicy")) reply["error"] = "X11 supports independent extension only; topology policy is not advertised";
            else if (!request["session"].toBool(true)) {
                if (!lease.release()) { send({{"seq", seq}, {"error", lease.error}}); return 1; }
                reply = lease.identity(); reply["seq"] = seq;
                reply["active"] = false; reply["width"] = w; reply["height"] = h; reply["scale"] = 1;
            } else if (scale != 1) reply["error"] = "X11 has no supported per-output UI scaling; 2x is unavailable (global DPI and RandR image transforms are not changed)";
            else if (!lease.configure(w, h)) { send({{"seq", seq}, {"error", lease.error}}); return 1; }
            else { reply = lease.identity(); reply["seq"] = seq; }
            send(reply);
        }
    }
}

int runX11Display(int, int) {
    // The guardian performs every mutation, so death during an in-flight request
    // cannot race an earlier cleanup. A separate pipe owns its lifetime.
    QProcess guard;
    guard.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    bool finished = false;
    int exitCode = 1;
    const auto drain = [&] {
        const auto bytes = guard.readAllStandardOutput(); fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); fflush(stdout);
    };
    QObject::connect(&guard, &QProcess::readyReadStandardOutput, &guard, drain);
    QObject::connect(&guard, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &guard, [&](int code, QProcess::ExitStatus status) {
        drain(); finished = true; exitCode = status == QProcess::NormalExit && code == 0 ? 0 : 1;
        QCoreApplication::exit(exitCode);
    });
    guard.start(QCoreApplication::applicationFilePath(), {"--guard-x11"});
    if (!guard.waitForStarted(1000)) {
        QObject::disconnect(&guard, nullptr, &guard, nullptr);
        guard.kill(); guard.waitForFinished(1000);
        send({{"error", "Cannot start X11 display recovery"}}); return 1;
    }
    // A denied connector can exit during waitForStarted. Drain and inspect the
    // state before entering an event loop whose earlier exit would be ignored.
    drain();
    if (finished || guard.state() == QProcess::NotRunning) return exitCode;
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &input, [&] {
        char bytes[4096]; const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) return;
        if (count <= 0) { input.setEnabled(false); guard.closeWriteChannel(); }
        else guard.write(bytes, count);
    });
    const int result = QCoreApplication::exec();
    guard.closeWriteChannel();
    guard.waitForFinished(3000);
    drain();
    QObject::disconnect(&guard, nullptr, &guard, nullptr);
    return result;
}
