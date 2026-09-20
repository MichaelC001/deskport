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

template<class... T> struct Tuple { std::tuple<T...> v; };
template<class... T> QDBusArgument& operator<<(QDBusArgument& a, const Tuple<T...>& t) {
    a.beginStructure(); std::apply([&](const auto&... v) { ((a << v), ...); }, t.v); a.endStructure(); return a;
}
template<class... T> const QDBusArgument& operator>>(const QDBusArgument& a, Tuple<T...>& t) {
    a.beginStructure(); std::apply([&](auto&... v) { ((a >> v), ...); }, t.v); a.endStructure(); return a;
}
using Spec = Tuple<QString, QString, QString, QString>;
using Mode = Tuple<QString, int, int, double, double, QList<double>, QVariantMap>;
using Monitor = Tuple<Spec, QList<Mode>, QVariantMap>;
using Logical = Tuple<int, int, double, uint, bool, QList<Spec>, QVariantMap>;
using ConfigMonitor = Tuple<QString, QString, QVariantMap>;
using ConfigLogical = Tuple<int, int, double, uint, bool, QList<ConfigMonitor>>;
Q_DECLARE_METATYPE(Spec)
Q_DECLARE_METATYPE(Mode)
Q_DECLARE_METATYPE(Monitor)
Q_DECLARE_METATYPE(Logical)
Q_DECLARE_METATYPE(ConfigMonitor)
Q_DECLARE_METATYPE(ConfigLogical)

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
    QSet<QString> original;
    QDBusMessage call(const QString& path, const QString& interface, const QString& method, const QList<QVariant>& args = {}) {
        const QString service = interface.startsWith("org.gnome.Mutter.DisplayConfig") ? "org.gnome.Mutter.DisplayConfig" : "org.gnome.Mutter.ScreenCast";
        auto message = QDBusMessage::createMethodCall(service, path, interface, method);
        message.setArguments(args);
        const auto result = bus.call(message, QDBus::Block, 3000);
        if (result.type() == QDBusMessage::ErrorMessage) error = result.errorMessage();
        return result;
    }
    QDBusMessage state() { return call("/org/gnome/Mutter/DisplayConfig", "org.gnome.Mutter.DisplayConfig", "GetCurrentState"); }
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
        const auto current = state();
        if (current.type() == QDBusMessage::ErrorMessage || current.arguments().size() != 4) return false;
        const auto monitors = qdbus_cast<QList<Monitor>>(current.arguments()[1]);
        const auto logical = qdbus_cast<QList<Logical>>(current.arguments()[2]);
        if (connector.isEmpty()) {
            QStringList candidates;
            for (const auto& m : monitors) {
                const auto& spec = std::get<0>(m.v).v;
                if (!original.contains(std::get<0>(spec)) && std::get<1>(spec) == "MetaVendor" && std::get<2>(spec) == "Virtual remote monitor") candidates.append(std::get<0>(spec));
            }
            if (candidates.size() != 1) { error = "Cannot uniquely identify the Mutter virtual output"; return false; }
            connector = candidates.first();
        }
        QList<ConfigLogical> configuration;
        bool found = false, changed = false;
        for (const auto& l : logical) {
            QList<ConfigMonitor> group;
            double newScale = std::get<2>(l.v);
            for (const auto& spec : std::get<5>(l.v)) {
                const auto name = std::get<0>(spec.v);
                QString currentMode;
                for (const auto& m : monitors) if (std::get<0>(std::get<0>(m.v).v) == name) {
                    for (const auto& mode : std::get<1>(m.v)) if (std::get<6>(mode.v).value("is-current").toBool()) {
                        currentMode = std::get<0>(mode.v);
                        if (name == connector) {
                            if (std::get<1>(mode.v) != width || std::get<2>(mode.v) != height || !std::get<5>(mode.v).contains(double(scale))) {
                                error = "Mutter has not applied the requested pixels or cannot support the requested scale"; return false;
                            }
                        }
                    }
                }
                if (currentMode.isEmpty()) { error = "Cannot preserve an existing Mutter output mode"; return false; }
                group.append({{name, currentMode, {}}});
                if (name == connector) {
                    if (std::get<5>(l.v).size() != 1) { error = "The owned virtual monitor unexpectedly belongs to a mirror group"; return false; }
                    found = true; changed = newScale != scale; newScale = scale;
                }
            }
            configuration.append({{std::get<0>(l.v), std::get<1>(l.v), newScale, std::get<3>(l.v), std::get<4>(l.v), group}});
        }
        if (!found) { error = "Mutter virtual output is not active"; return false; }
        if (!changed) return true;
        const auto properties = qdbus_cast<QVariantMap>(current.arguments()[3]);
        QVariantMap options;
        if (properties.contains("layout-mode")) options["layout-mode"] = properties["layout-mode"];
        const auto result = call("/org/gnome/Mutter/DisplayConfig", "org.gnome.Mutter.DisplayConfig", "ApplyMonitorsConfig",
            {current.arguments()[0], uint(1), QVariant::fromValue(configuration), options});
        if (result.type() == QDBusMessage::ErrorMessage) return false;
        const auto verified = state();
        if (verified.arguments().size() != 4) return false;
        for (const auto& l : qdbus_cast<QList<Logical>>(verified.arguments()[2]))
            for (const auto& s : std::get<5>(l.v))
                if (std::get<0>(s.v) == connector && std::get<2>(l.v) == scale) return true;
        error = "Mutter scale acknowledgment did not match"; return false;
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
        if (loop) pw_thread_loop_stop(loop);
        if (stream) pw_stream_destroy(stream);
        if (registry) pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
        if (core) pw_core_disconnect(core);
        if (context) pw_context_destroy(context);
        if (loop) pw_thread_loop_destroy(loop);
        if (!session.isEmpty() && !closed) call(session, "org.gnome.Mutter.ScreenCast.Session", "Stop");
    }
    bool start(int width, int height) {
        qDBusRegisterMetaType<Spec>(); qDBusRegisterMetaType<QList<Spec>>(); qDBusRegisterMetaType<QList<double>>();
        qDBusRegisterMetaType<Mode>(); qDBusRegisterMetaType<QList<Mode>>(); qDBusRegisterMetaType<Monitor>(); qDBusRegisterMetaType<QList<Monitor>>();
        qDBusRegisterMetaType<Logical>(); qDBusRegisterMetaType<QList<Logical>>();
        qDBusRegisterMetaType<ConfigMonitor>(); qDBusRegisterMetaType<QList<ConfigMonitor>>();
        qDBusRegisterMetaType<ConfigLogical>(); qDBusRegisterMetaType<QList<ConfigLogical>>();
        const auto before = state();
        if (before.arguments().size() != 4) { error = "Mutter display configuration is unavailable"; return false; }
        for (const auto& monitor : qdbus_cast<QList<Monitor>>(before.arguments()[1])) original.insert(std::get<0>(std::get<0>(monitor.v).v));
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
            e.global_remove = [](void* p, uint32_t id) { auto self = static_cast<GnomeDisplay*>(p); if (id == self->node) self->pwFailed = true; };
            return e;
        }();
        pw_registry_add_listener(registry, &registryListener, &registryEvents, this);
        stream = pw_stream_new(core, "DeskPort virtual display sizing", pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
        if (!stream) return false;
        static const pw_stream_events events = [] {
            pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS;
            e.state_changed = [](void* p, pw_stream_state, pw_stream_state state, const char* message) { fprintf(stderr, "Mutter PipeWire state %d: %s\n", int(state), message ? message : ""); if (state == PW_STREAM_STATE_ERROR) static_cast<GnomeDisplay*>(p)->pwFailed = true; };
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
        return setScale(1, width, height);
    }
    bool resize(int width, int height, int scale) {
        error.clear();
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
    QJsonObject identity() const { return {{"displayId", 1}, {"outputName", connector}, {"pipewireNode", double(node)}, {"pipewireSerial", QString::number(serial.load())}, {"backend", "mutter"}}; }
    QString lastError() const { return error.isEmpty() ? "Mutter virtual display initialization failed" : error; }
    bool healthy() const { return !closed && !pwFailed; }
private slots:
    void onNode(uint id) { fprintf(stderr, "Mutter node: %u\n", id); node = id; }
    void onClosed() { closed = true; if (!shuttingDown) QCoreApplication::exit(1); }
};

int runGnomeDisplay(int width, int height) {
    auto send = [](const QJsonObject& o) { auto bytes = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n'; fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); fflush(stdout); };
    std::unique_ptr<GnomeDisplay> display;
    if (qEnvironmentVariableIntValue("DESKPORT_DISPLAY_ON_DEMAND") == 1)
        send({{"ready",true},{"active",false},{"gnome",true},{"outputName","DeskPort-pending"}});
    else {
        display = std::make_unique<GnomeDisplay>();
        if (!display->start(width, height)) { send({{"error", display->lastError()}}); return 1; }
        auto initial = display->identity(); initial["width"] = width; initial["height"] = height; initial["scale"] = 1; send(initial);
    }
    QByteArray buffer;
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &input, [&] {
        input.setEnabled(false);
        char bytes[4096]; const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count <= 0) { QCoreApplication::quit(); return; }
        buffer.append(bytes, int(count));
        if (buffer.size() > 8192) { QCoreApplication::exit(2); return; }
        while (buffer.contains('\n')) {
            const int end = buffer.indexOf('\n'); const auto request = QJsonDocument::fromJson(buffer.left(end)).object(); buffer.remove(0, end + 1);
            const int seq = request["seq"].toInt(), w = request["width"].toInt(), h = request["height"].toInt(), scale = request["scale"].toInt();
            QJsonObject response{{"seq", seq}};
            if (!seq || w < 640 || w > 7680 || h < 360 || h > 4320 || w % 4 || h % 4 || (scale != 1 && scale != 2)) response["error"] = "Invalid virtual output request";
            else if (!request["session"].toBool(true)) {
                display.reset();
                response["width"] = w; response["height"] = h; response["scale"] = scale; response["active"] = false;
            } else {
                bool recreated = false;
                if (!display) {
                    display = std::make_unique<GnomeDisplay>();
                    if (!display->start(w, h)) { response["error"] = display->lastError(); send(response); QCoreApplication::exit(1); return; }
                    recreated = true;
                }
                if (!display->resize(w, h, scale)) { response["error"] = display->lastError(); send(response); QCoreApplication::exit(1); return; }
                if (recreated) { response = display->identity(); response["seq"] = seq; }
                response["width"] = w; response["height"] = h; response["scale"] = scale; response["active"] = true;
            }
            send(response);
        }
        input.setEnabled(true);
    });
    return QCoreApplication::exec();
}
#include "gnome-display.moc"
