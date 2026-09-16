// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstring>
#include <map>
#include <wayland-client.h>
struct Output { wl_output* proxy; QJsonObject state; };
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto display = wl_display_connect(nullptr);
    if (!display) return 1;
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
        }, [](void*, wl_registry*, uint32_t) {}
    };
    wl_registry_add_listener(registry, &registryListener, &outputs);
    if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0) return 1;
    QJsonArray result;
    for (const auto& out : outputs) { result.append(out.second.state); wl_output_destroy(out.second.proxy); }
    wl_registry_destroy(registry); wl_display_disconnect(display);
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    return 0;
}
