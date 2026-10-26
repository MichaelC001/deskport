// SPDX-License-Identifier: GPL-3.0-or-later
// Mutter's connection-owned virtual monitor, sized by PipeWire negotiation.
#include <QCoreApplication>
#include <QtDBus>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QThread>
#include <QSize>
#include <QSet>
#include <atomic>
#include <memory>
#include <cstdio>
#include <tuple>
#include <unistd.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include "gnome-display.h"

#include "gnome-topology.h"
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QScopedValueRollback>
#include <QCryptographicHash>
class GnomeDisplay : public QObject {
    Q_OBJECT
    QDBusConnection bus = QDBusConnection::sessionBus();
    QString session, streamPath, connector, error;
    uint node = 0;
    bool closed = false, shuttingDown = false;
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_stream* stream = nullptr;
    pw_registry* registry = nullptr;
    spa_hook registryListener{};
    std::atomic<quint64> serial{0};
    spa_hook listener{};
    std::atomic<int> actualWidth{0}, actualHeight{0};
    std::atomic<bool> pwFailed{false}, frame{false};
    QSet<QByteArray> original;
    QJsonObject baseline;
    std::unique_ptr<QProcess> recovery = std::make_unique<QProcess>();
    GnomeTopology topology;
    int leasePolicy = -1, requestedWidth = 0, requestedHeight = 0, requestedScale = 1;
    bool admitted = false, configuring = false, monitorCheckQueued = false;
    QString layoutWarning, recoveryWarning, journalPath;
    void pipeWireLost() {
        pwFailed = true;
        QMetaObject::invokeMethod(this, [this] { if (!shuttingDown) QCoreApplication::exit(1); }, Qt::QueuedConnection);
    }
    void noteLayout(const QString& reason) {
        if (layoutWarning == reason) return;
        layoutWarning = reason;
        if (!reason.isEmpty()) {
            const auto directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/DeskPort";
            QDir().mkpath(directory);
            const auto path = directory + "/display-layout-events.jsonl";
            QFile file(path);
            if (file.size() > 1024 * 1024) { QFile::remove(path + ".previous"); QFile::rename(path, path + ".previous"); }
            const QJsonObject event{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"backend", "mutter"},
                {"reason", reason}, {"policy", leasePolicy}, {"original", baseline}, {"observed", topology.snapshot()}};
            if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
                file.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
            }
            fprintf(stderr, "DeskPort Mutter local layout: %s; original and observed layouts recorded\n", qPrintable(reason));
        }
        emit layoutDegraded(reason);
    }
    QDBusMessage call(const QString& path, const QString& interface, const QString& method, const QList<QVariant>& args = {}) {
        const QString service = interface.startsWith("org.gnome.Mutter.DisplayConfig") ? "org.gnome.Mutter.DisplayConfig" : "org.gnome.Mutter.ScreenCast";
        auto message = QDBusMessage::createMethodCall(service, path, interface, method);
        message.setArguments(args);
        const auto result = bus.call(message, QDBus::Block, 3000);
        if (result.type() == QDBusMessage::ErrorMessage) error = result.errorMessage();
        return result;
    }
    bool wait(const std::function<bool()>& done, int timeout = 3000) {
        QElapsedTimer timer; timer.start();
        while (!done() && !closed && !pwFailed && timer.elapsed() < timeout) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
        }
        if (done() && !closed && !pwFailed) return true;
        if (error.isEmpty()) error = "Mutter/PipeWire virtual display negotiation failed or timed out";
        return false;
    }
    bool setScale(int scale, int width, int height) {
        QJsonObject current;
        if (connector.isEmpty()) {
            // PipeWire format/frame delivery and DisplayConfig publication are
            // independent. Also, Mutter reuses Meta-N connectors between leases:
            // compare the complete spec (including its per-stream serial).
            bool ambiguous = false;
            const bool identified = wait([&] {
                current = topology.snapshot();
                if (current.isEmpty()) return false;
                QJsonArray candidates;
                for (const auto& value : current["monitors"].toArray()) {
                    const auto spec = value.toObject()["spec"].toArray();
                    if (spec.size() == 4 && !original.contains(QJsonDocument(spec).toJson(QJsonDocument::Compact)) &&
                        spec[1] == "MetaVendor" && spec[2] == "Virtual remote monitor") candidates.append(spec);
                }
                if (candidates.size() > 1) { ambiguous = true; return true; }
                if (candidates.size() != 1) return false;
                baseline["ownedSpec"] = candidates[0];
                connector = candidates[0].toArray()[0].toString();
                return true;
            }, 2000);
            if (!identified || ambiguous) { error = "Cannot uniquely identify the Mutter virtual output"; return false; }
            if (!guardCommand(QJsonDocument(baseline).toJson(QJsonDocument::Compact), "armed")) { error = "Cannot persist owned Mutter output identity"; return false; }
        }
        const bool configured = topology.configure(baseline, connector, leasePolicy, width, height, scale);
        if (!configured) {
            const auto reason = topology.error;
            // Initial unsupported policies fail. A late compositor layout
            // failure may retain an already admitted, independently verified
            // workspace, with the existing local-layout degradation warning.
            if (!admitted || !topology.workspaceVerified(baseline, width, height, scale)) { error = reason; return false; }
            noteLayout(reason);
        } else noteLayout(recoveryWarning);
        if (closed || pwFailed) { error = "Mutter virtual session was closed"; return false; }
        admitted = true; requestedWidth = width; requestedHeight = height; requestedScale = scale;
        return true;
    }
    bool guardCommand(const QByteArray& command, const QByteArray& expected) {
        recovery->write(command + '\n');
        if (!recovery->waitForBytesWritten(1000)) return false;
        if (!recovery->canReadLine() && !recovery->waitForReadyRead(3000)) return false;
        return recovery->readLine().trimmed() == expected;
    }
    const spa_pod* format(spa_pod_builder& b, int width, int height) {
        const spa_rectangle size{uint32_t(width), uint32_t(height)};
        const spa_fraction rate{0, 1};
        return static_cast<const spa_pod*>(spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBx),
            SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&size), SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&rate)));
    }
public:
    ~GnomeDisplay() override {
        shuttingDown = true;
        if (recovery && recovery->state() == QProcess::Running) {
            // Re-enable physical outputs atomically before destroying the only
            // active virtual output. EOF repeats recovery after Mutter reflows.
            if (!guardCommand("restore", "restored")) fprintf(stderr, "Mutter pre-removal recovery remains pending\n");
        }
        if ((!recovery || recovery->state() != QProcess::Running) && !baseline.isEmpty())
            topology.restore(baseline, true);
        if (loop) pw_thread_loop_stop(loop);
        if (stream) pw_stream_destroy(stream);
        if (registry) pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
        if (core) pw_core_disconnect(core);
        if (context) pw_context_destroy(context);
        if (loop) pw_thread_loop_destroy(loop);
        if (!session.isEmpty() && !closed) call(session, "org.gnome.Mutter.ScreenCast.Session", "Stop");
        if (recovery && recovery->state() == QProcess::Running) {
            recovery->closeWriteChannel();
            if (!recovery->waitForFinished(15000)) {
                // QProcess must not kill the independent guardian on destruction.
                recovery.release();
                fprintf(stderr, "Mutter recovery did not finish within its deadline; journal retained\n");
            }
        }
    }
    bool start(int width, int height, int scale = 1, int policy = 2) {
        QScopedValueRollback<bool> transition(configuring, true);
        GnomeTopology::registerTypes();
        leasePolicy = policy;
        // A guard owns the per-session lock and replays any previous journal
        // before we take the fresh admission snapshot.
        if (!bus.isConnected() || !bus.interface()) { error = "A live GNOME session bus is required"; return false; }
        auto permission = QDBusMessage::createMethodCall("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig", "org.freedesktop.DBus.Properties", "Get");
        permission.setArguments({QStringLiteral("org.gnome.Mutter.DisplayConfig"), QStringLiteral("ApplyMonitorsConfigAllowed")});
        QDBusReply<QDBusVariant> allowed = bus.call(permission, QDBus::Block, 2000);
        if (!allowed.isValid() || !allowed.value().variant().toBool()) { error = "Mutter display configuration is unavailable or prohibited by session policy"; return false; }
        const auto owner = bus.interface()->serviceOwner("org.gnome.Mutter.DisplayConfig").value();
        if (owner.isEmpty()) { error = "Mutter display configuration is unavailable"; return false; }
        const auto scope = QCryptographicHash::hash((qgetenv("DBUS_SESSION_BUS_ADDRESS") + owner.toUtf8()), QCryptographicHash::Sha256).toHex().left(24);
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/DeskPort";
        if (!QDir().mkpath(directory)) { error = "Cannot create Mutter recovery directory"; return false; }
        recovery->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        QObject::connect(recovery.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { if (!shuttingDown) QCoreApplication::exit(1); });
        journalPath = directory + "/mutter-" + scope + ".json";
        recovery->start(QCoreApplication::applicationFilePath(), {"--restore-mutter", journalPath});
        if (!recovery->waitForStarted(1000) || (!recovery->canReadLine() && !recovery->waitForReadyRead(3000))) {
            error = "Mutter recovery is unavailable or another display lease owns this session"; return false;
        }
        const auto status = recovery->readLine().trimmed();
        if (status != "ready" && status != "pending") { error = "Mutter recovery guardian did not become ready"; return false; }
        const auto current = topology.snapshot();
        if (current.isEmpty()) { error = topology.error; return false; }
        baseline = current;
        if (status == "pending") {
            QFile previous(journalPath);
            if (!previous.open(QIODevice::ReadOnly)) { error = "Cannot retain pending Mutter recovery snapshot"; return false; }
            baseline = QJsonDocument::fromJson(previous.readAll()).object();
            if (baseline["version"].toInt() != 1 || !baseline["monitors"].isArray() || !baseline["logical"].isArray()) {
                error = "Invalid pending Mutter recovery snapshot"; return false;
            }
            recoveryWarning = baseline["recovery-error"].toString();
            if (recoveryWarning.isEmpty()) recoveryWarning = "Previous Mutter layout restoration remains pending";
            baseline["admission"] = current;
        }
        baseline["admissionMonitors"] = current["monitors"];
        baseline["policy"] = policy;
        baseline["leaseOwner"] = bus.baseService();
        if (!topology.supported(baseline, policy, width, height, scale)) { error = topology.error; return false; }
        for (const auto& value : current["monitors"].toArray())
            original.insert(QJsonDocument(value.toObject()["spec"].toArray()).toJson(QJsonDocument::Compact));
        if (!guardCommand(QJsonDocument(baseline).toJson(QJsonDocument::Compact), "armed")) { error = "Cannot persist Mutter recovery snapshot"; return false; }
        bus.connect("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig", "org.gnome.Mutter.DisplayConfig", "MonitorsChanged", this, SLOT(onMonitorsChanged()));
        auto result = call("/org/gnome/Mutter/ScreenCast", "org.gnome.Mutter.ScreenCast", "CreateSession", {QVariantMap{}});
        if (result.type() == QDBusMessage::ErrorMessage) return false;
        session = qdbus_cast<QDBusObjectPath>(result.arguments().value(0)).path();
        if (session.isEmpty()) return false;
        bus.connect("org.gnome.Mutter.ScreenCast", session, "org.gnome.Mutter.ScreenCast.Session", "Closed", this, SLOT(onClosed()));
        result = call(session, "org.gnome.Mutter.ScreenCast.Session", "RecordVirtual", {QVariantMap{{"cursor-mode", uint(1)}}});
        if (result.type() == QDBusMessage::ErrorMessage) return false;
        streamPath = qdbus_cast<QDBusObjectPath>(result.arguments().value(0)).path();
        bus.connect("org.gnome.Mutter.ScreenCast", streamPath, "org.gnome.Mutter.ScreenCast.Stream", "PipeWireStreamAdded", this, SLOT(onNode(uint)));
        result = call(session, "org.gnome.Mutter.ScreenCast.Session", "Start");
        if (result.type() == QDBusMessage::ErrorMessage || !wait([&] { return node != 0; })) return false;
        pw_init(nullptr, nullptr);
        loop = pw_thread_loop_new("deskport-virtual", nullptr);
        if (!loop) return false;
        context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
        if (!context) return false;
        core = pw_context_connect(context, nullptr, 0);
        if (!core) return false;
        registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
        static const pw_registry_events registryEvents = [] {
            pw_registry_events e{}; e.version = PW_VERSION_REGISTRY_EVENTS;
            e.global = [](void* p, uint32_t id, uint32_t, const char* type, uint32_t, const spa_dict* props) {
                auto self = static_cast<GnomeDisplay*>(p);
                if (id == self->node && !strcmp(type, PW_TYPE_INTERFACE_Node)) {
                    const char* value = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL);
                    if (value) self->serial = QByteArray(value).toULongLong();
                }
            };
            e.global_remove = [](void* p, uint32_t id) { auto self = static_cast<GnomeDisplay*>(p); if (id == self->node) self->pipeWireLost(); };
            return e;
        }();
        pw_registry_add_listener(registry, &registryListener, &registryEvents, this);
        stream = pw_stream_new(core, "DeskPort virtual display sizing", pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
        if (!stream) return false;
        static const pw_stream_events events = [] {
            pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS;
            e.state_changed = [](void* p, pw_stream_state, pw_stream_state state, const char* message) { fprintf(stderr, "Mutter PipeWire state %d: %s\n", int(state), message ? message : ""); if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) static_cast<GnomeDisplay*>(p)->pipeWireLost(); };
            e.param_changed = [](void* p, uint32_t id, const spa_pod* param) {
                if (id != SPA_PARAM_Format || !param) return;
                spa_video_info_raw info{};
                if (spa_format_video_raw_parse(param, &info) < 0) return;
                auto self = static_cast<GnomeDisplay*>(p); self->actualWidth = int(info.size.width); self->actualHeight = int(info.size.height);
            };
            e.process = [](void* p) { auto self = static_cast<GnomeDisplay*>(p); while (auto b = pw_stream_dequeue_buffer(self->stream)) { self->frame = true; pw_stream_queue_buffer(self->stream, b); } };
            return e;
        }();
        pw_stream_add_listener(stream, &listener, &events, this);
        uint8_t bytes[1024]; spa_pod_builder builder = SPA_POD_BUILDER_INIT(bytes, sizeof(bytes));
        auto param = format(builder, width, height);
        if (pw_stream_connect(stream, PW_DIRECTION_INPUT, node, pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), &param, 1) < 0 || pw_thread_loop_start(loop) < 0) return false;
        if (!wait([&] { return actualWidth == width && actualHeight == height && frame && serial != 0; })) return false;
        return setScale(scale, width, height);
    }
    bool resize(int width, int height, int scale, int policy) {
        QScopedValueRollback<bool> transition(configuring, true);
        error.clear();
        if (!recovery || recovery->state() != QProcess::Running) { error = "Mutter recovery guardian was lost"; return false; }
        if (policy != leasePolicy) { error = "Cannot change display policy within a lease"; return false; }
        if (!topology.supported(baseline, policy, width, height, scale)) { error = topology.error; return false; }
        if (closed || pwFailed) { error = "Mutter virtual session was closed"; return false; }
        if (actualWidth != width || actualHeight != height) {
            uint8_t bytes[1024]; spa_pod_builder builder = SPA_POD_BUILDER_INIT(bytes, sizeof(bytes));
            auto param = format(builder, width, height);
            pw_thread_loop_lock(loop); frame = false;
            const int result = pw_stream_update_params(stream, &param, 1);
            pw_thread_loop_unlock(loop);
            if (result < 0 || !wait([&] { return actualWidth == width && actualHeight == height && frame && serial != 0; })) return false;
        }
        return setScale(scale, width, height);
    }
    QJsonObject identity() const { return {{"displayId", 1}, {"outputName", connector}, {"pipewireNode", double(node)}, {"pipewireSerial", QString::number(serial.load())}, {"backend", "mutter"}, {"displayPolicy", 1}}; }
    QString recoveryJournal() const { return journalPath; }
    QString localLayoutWarning() const { return layoutWarning; }
    QString lastError() const { return error.isEmpty() ? "Mutter virtual display initialization failed" : error; }
    bool healthy() const { return !closed && !pwFailed; }
signals:
    void layoutDegraded(const QString& reason);
private slots:
    void onMonitorsChanged() {
        if (shuttingDown || !admitted || monitorCheckQueued) return;
        monitorCheckQueued = true;
        QTimer::singleShot(50, this, [this] {
            monitorCheckQueued = false;
            if (shuttingDown || !admitted) return;
            if (configuring) { onMonitorsChanged(); return; }
            if (!resize(requestedWidth, requestedHeight, requestedScale, leasePolicy)) {
                fprintf(stderr, "Mutter workspace became invalid after topology change: %s\n", qPrintable(lastError()));
                QCoreApplication::exit(1);
            }
        });
    }
    void onNode(uint id) { fprintf(stderr, "Mutter node: %u\n", id); node = id; }
    void onClosed() { closed = true; if (!shuttingDown) QCoreApplication::exit(1); }
};

int runGnomeDisplay(int width, int height) {
    auto send = [](const QJsonObject& o) { auto bytes = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n'; fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); fflush(stdout); };
    std::unique_ptr<GnomeDisplay> display;
    if (qEnvironmentVariableIntValue("DESKPORT_DISPLAY_ON_DEMAND") == 1)
        send({{"ready",true},{"active",false},{"gnome",true},{"displayPolicy",1},{"outputName","DeskPort-pending"}});
    else {
        display = std::make_unique<GnomeDisplay>();
        if (!display->start(width, height)) { send({{"error", display->lastError()}}); return 1; }
        auto initial = display->identity(); initial["width"] = width; initial["height"] = height; initial["scale"] = 1; send(initial);
    }
    QByteArray buffer;
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    const auto watch = [&] {
        QObject::connect(display.get(), &GnomeDisplay::layoutDegraded, &input, [&](const QString& reason) {
            send({{"layoutWarning", reason}});
        });
    };
    if (display) watch();
    QObject::connect(&input, &QSocketNotifier::activated, &input, [&] {
        input.setEnabled(false);
        char bytes[4096]; const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count <= 0) { QCoreApplication::quit(); return; }
        buffer.append(bytes, int(count));
        if (buffer.size() > 8192) { QCoreApplication::exit(2); return; }
        while (buffer.contains('\n')) {
            const int end = buffer.indexOf('\n'); const auto request = QJsonDocument::fromJson(buffer.left(end)).object(); buffer.remove(0, end + 1);
            const int policy = request.contains("displayPolicy") ? request["displayPolicy"].toInt(-1) : 0;
            const int seq = request["seq"].toInt(), w = request["width"].toInt(), h = request["height"].toInt(), scale = request["scale"].toInt();
            QJsonObject response{{"seq", seq}};
            if (policy < 0 || policy > 2 || !seq || w < 640 || w > 7680 || h < 360 || h > 4320 || w % 4 || h % 4 || (scale != 1 && scale != 2)) response["error"] = "Invalid virtual output request";
            else if (!request["session"].toBool(true)) {
                const auto journal = display ? display->recoveryJournal() : QString();
                display.reset();
                if (!journal.isEmpty() && QFile::exists(journal)) response["layoutWarning"] = "Mutter local display restoration remains pending";
                response["width"] = w; response["height"] = h; response["scale"] = scale; response["active"] = false;
            } else {
                bool recreated = false;
                if (!display) {
                    display = std::make_unique<GnomeDisplay>();
                    watch();
                    if (!display->start(w, h, scale, policy)) { response["error"] = display->lastError(); send(response); QCoreApplication::exit(1); return; }
                    recreated = true;
                }
                if (!display->resize(w, h, scale, policy)) { response["error"] = display->lastError(); send(response); QCoreApplication::exit(1); return; }
                if (recreated) { response = display->identity(); response["seq"] = seq; }
                response["width"] = w; response["height"] = h; response["scale"] = scale; response["active"] = true;
                if (!display->localLayoutWarning().isEmpty()) response["layoutWarning"] = display->localLayoutWarning();
            }
            send(response);
        }
        input.setEnabled(true);
    });
    return QCoreApplication::exec();
}
#include "gnome-display.moc"
