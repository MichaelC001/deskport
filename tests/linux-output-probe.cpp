// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QElapsedTimer>
#include <poll.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstring>
#include <map>
#include <wayland-client.h>
#include "../host/linux/kde-output-device-v2.h"
#include "../host/linux/kde-output-management-v2.h"
static kde_output_management_v2* management = nullptr;
struct Output { wl_output* proxy; QJsonObject state; bool removed = false; };
struct Device {
    kde_output_device_v2* proxy;
    kde_output_device_mode_v2* current = nullptr;
    std::map<kde_output_device_mode_v2*, QJsonObject> modes;
    QJsonObject state;
};
static int modeEvent(const void*, void* target, uint32_t, const wl_message* message, wl_argument* args) {
    auto device = static_cast<Device*>(wl_proxy_get_user_data(static_cast<wl_proxy*>(target)));
    auto& mode = device->modes[static_cast<kde_output_device_mode_v2*>(target)];
    if (!strcmp(message->name, "size")) { mode["width"] = args[0].i; mode["height"] = args[1].i; }
    if (!strcmp(message->name, "refresh")) mode["refresh"] = args[0].i;
    return 0;
}
static int deviceEvent(const void*, void* target, uint32_t, const wl_message* message, wl_argument* args) {
    auto device = static_cast<Device*>(wl_proxy_get_user_data(static_cast<wl_proxy*>(target)));
    auto& s = device->state;
    const QByteArray event(message->name);
    if (event == "name" || event == "uuid" || event == "replication_source") s[event] = QString::fromUtf8(args[0].s);
    else if (event == "priority" || event == "enabled") s[event] = int(args[0].u);
    else if (event == "scale") s["scale"] = wl_fixed_to_double(args[0].f);
    else if (event == "geometry") { s["x"] = args[0].i; s["y"] = args[1].i; s["transform"] = args[7].i; }
    else if (event == "current_mode") device->current = reinterpret_cast<kde_output_device_mode_v2*>(args[0].o);
    else if (event == "mode") {
        auto mode = reinterpret_cast<kde_output_device_mode_v2*>(args[0].o);
        device->modes.emplace(mode, QJsonObject{});
        wl_proxy_add_dispatcher(reinterpret_cast<wl_proxy*>(mode), modeEvent, nullptr, device);
    }
    return 0;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto display = wl_display_connect(nullptr);
    if (!display) return 1;
    if (app.arguments().contains("--devices") || (app.arguments().contains("--disable-last") || app.arguments().contains("--enable-last"))) {
        std::map<uint32_t, Device> devices;
        auto registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
            [](void* p, wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
                if (!strcmp(interface, "kde_output_management_v2")) {
                    management = static_cast<kde_output_management_v2*>(wl_registry_bind(registry, id, &kde_output_management_v2_interface, std::min(version, 18u))); return;
                }
                if (strcmp(interface, "kde_output_device_v2") || version < 18) return;
                auto& device = (*static_cast<std::map<uint32_t, Device>*>(p))[id];
                device.proxy = static_cast<kde_output_device_v2*>(wl_registry_bind(registry, id, &kde_output_device_v2_interface, 18));
                wl_proxy_add_dispatcher(reinterpret_cast<wl_proxy*>(device.proxy), deviceEvent, nullptr, &device);
            }, [](void*, wl_registry*, uint32_t) {}
        };
        wl_registry_add_listener(registry, &listener, &devices);
        for (int n = 0; n < 3; ++n) if (wl_display_roundtrip(display) < 0) return 1;
        if ((app.arguments().contains("--disable-last") || app.arguments().contains("--enable-last"))) {
            if (!management || devices.size() < 2) return 2;
            int applied = 0;
            auto config = kde_output_management_v2_create_configuration(management);
            static const kde_output_configuration_v2_listener listener = {
                [](void* p, kde_output_configuration_v2*) { *static_cast<int*>(p) = 1; },
                [](void* p, kde_output_configuration_v2*) { *static_cast<int*>(p) = -1; },
                [](void*, kde_output_configuration_v2*, const char* reason) { fprintf(stderr, "Fixture configuration failed: %s\n", reason); }
            };
            kde_output_configuration_v2_add_listener(config, &listener, &applied);
            kde_output_configuration_v2_enable(config, devices.rbegin()->second.proxy, app.arguments().contains("--enable-last") ? 1 : 0);
            // Exercise restoration of a rotated fractional-scale panel as well.
            auto first = devices.begin()->second.proxy;
            kde_output_configuration_v2_transform(config, first, 3);
            kde_output_configuration_v2_scale(config, first, wl_fixed_from_double(1.75));
            kde_output_configuration_v2_position(config, first, 100, 100);
            kde_output_configuration_v2_apply(config);
            QElapsedTimer timer; timer.start();
            while (!applied && timer.elapsed() < 3000) {
                if (wl_display_dispatch_pending(display) < 0) return 1;
                wl_display_flush(display);
                pollfd fd{wl_display_get_fd(display), POLLIN, 0};
                if (poll(&fd, 1, 100) > 0 && wl_display_dispatch(display) < 0) return 1;
            }
            wl_display_disconnect(display);
            return applied == 1 ? 0 : 1;
        }
        QJsonArray result;
        for (auto& entry : devices) {
            auto& device = entry.second;
            const auto mode = device.modes[device.current];
            for (auto it = mode.begin(); it != mode.end(); ++it) device.state[it.key()] = it.value();
            result.append(device.state);
        }
        wl_display_disconnect(display);
        const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
        fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
        return 0;
    }
    std::map<uint32_t, Output> outputs;
    auto registry = wl_display_get_registry(display);
    static const wl_output_listener outputListener = {
        [](void* p, wl_output*, int32_t x, int32_t y, int32_t, int32_t, int32_t, const char*, const char*, int32_t transform) { auto& s = static_cast<Output*>(p)->state; s["x"] = x; s["y"] = y; s["transform"] = transform; },
        [](void* p, wl_output*, uint32_t flags, int32_t w, int32_t h, int32_t refresh) { if (flags & WL_OUTPUT_MODE_CURRENT) { auto& s = static_cast<Output*>(p)->state; s["width"] = w; s["height"] = h; s["refresh"] = refresh; } },
        [](void*, wl_output*) {},
        [](void* p, wl_output*, int32_t scale) { static_cast<Output*>(p)->state["scale"] = scale; },
        [](void* p, wl_output*, const char* name) { static_cast<Output*>(p)->state["name"] = name; },
        [](void*, wl_output*, const char*) {}
    };
    static const wl_registry_listener registryListener = {
        [](void* p, wl_registry* registry, uint32_t id, const char* name, uint32_t version) {
            if (strcmp(name, "wl_output") || version < 4) return;
            auto& out = (*static_cast<std::map<uint32_t, Output>*>(p))[id];
            out.proxy = static_cast<wl_output*>(wl_registry_bind(registry, id, &wl_output_interface, 4));
            out.state["id"] = int(id);
            wl_output_add_listener(out.proxy, &outputListener, &out);
        }, [](void* p, wl_registry*, uint32_t id) {
            auto& outputs = *static_cast<std::map<uint32_t, Output>*>(p);
            if (auto it = outputs.find(id); it != outputs.end()) it->second.removed = true;
        }
    };
    wl_registry_add_listener(registry, &registryListener, &outputs);
    if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0) return 1;
    QJsonArray result;
    for (const auto& out : outputs) { if (!out.second.removed) result.append(out.second.state); wl_output_destroy(out.second.proxy); }
    wl_registry_destroy(registry); wl_display_disconnect(display);
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    return 0;
}
