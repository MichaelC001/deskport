// SPDX-License-Identifier: GPL-3.0-or-later
// Run this with the AppImage's loader and Qt libraries, not the builder's Qt.
#include <QBackingStore>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QPainter>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <unistd.h>
#include <wayland-client.h>

struct RegistryWatch {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    std::set<uint32_t> outputs;
    int minimum = 0;
    int changes = 0;
    bool initialized = false;
    void changed() {
        ++changes;
        if (initialized) minimum = std::min(minimum, int(outputs.size()));
    }
    bool start() {
        display = wl_display_connect(nullptr);
        if (!display) return false;
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
            [](void* data, wl_registry*, uint32_t id, const char* interface, uint32_t) {
                if (strcmp(interface, "wl_output")) return;
                auto self = static_cast<RegistryWatch*>(data);
                self->outputs.insert(id);
                self->changed();
            },
            [](void* data, wl_registry*, uint32_t id) {
                auto self = static_cast<RegistryWatch*>(data);
                if (self->outputs.erase(id)) self->changed();
            }
        };
        wl_registry_add_listener(registry, &listener, this);
        if (wl_display_roundtrip(display) < 0) return false;
        minimum = int(outputs.size());
        initialized = true;
        return true;
    }
    ~RegistryWatch() {
        if (registry) wl_registry_destroy(registry);
        if (display) wl_display_disconnect(display);
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    RegistryWatch watch;
    if (!watch.start()) return 2;
    QSocketNotifier wayland(wl_display_get_fd(watch.display), QSocketNotifier::Read);
    QObject::connect(&wayland, &QSocketNotifier::activated, &app, [&] {
        if (wl_display_dispatch(watch.display) < 0) app.exit(3);
    });
    // The old bundled Qt Wayland plugin could retain a removed QScreen here.
    // Keep both surfaces alive over every virtual-primary/disconnect cycle.
    QOffscreenSurface offscreen;
    offscreen.setScreen(QGuiApplication::primaryScreen());
    offscreen.create();
    QWindow window;
    window.setSurfaceType(QSurface::RasterSurface);
    window.setTitle("DeskPort isolated AppImage screen regression");
    window.resize(480, 270);
    QBackingStore backing(&window);
    window.show();
    int screenEvents = 0;
    int frames = 0;
    QObject::connect(&app, &QGuiApplication::screenAdded, &app, [&](QScreen*) { ++screenEvents; });
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app, [&](QScreen*) { ++screenEvents; });
    QTimer paint;
    QObject::connect(&paint, &QTimer::timeout, &app, [&] {
        if (!window.isExposed()) return;
        const QRect area(QPoint(), window.size());
        backing.resize(window.size());
        backing.beginPaint(area);
        QPainter painter(backing.paintDevice());
        painter.fillRect(area, QColor(25, 37, 55));
        painter.setPen(Qt::white);
        painter.drawText(area, Qt::AlignCenter, QStringLiteral("Isolated Qt Wayland frame %1").arg(++frames));
        painter.end();
        backing.endPaint();
        backing.flush(area);
    });
    paint.start(50);
    auto snapshot = [&] {
        QJsonArray screens;
        for (auto screen : QGuiApplication::screens()) {
            screens.append(QJsonObject{{"name", screen->name()},
                                       {"width", screen->geometry().width()},
                                       {"height", screen->geometry().height()}});
        }
        const auto bytes = QJsonDocument(QJsonObject{
            {"ready", true}, {"qtVersion", qVersion()},
            {"platform", QGuiApplication::platformName()}, {"screens", screens},
            {"screenEvents", screenEvents}, {"activeWlOutputs", int(watch.outputs.size())},
            {"minActiveWlOutputs", watch.minimum}, {"registryChanges", watch.changes},
            {"offscreenValid", offscreen.isValid()}, {"windowExposed", window.isExposed()},
            {"frames", frames}}).toJson(QJsonDocument::Compact);
        fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
        fputc('\n', stdout);
        fflush(stdout);
    };
    QByteArray input;
    QSocketNotifier commands(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&commands, &QSocketNotifier::activated, &app, [&] {
        char bytes[512];
        const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count <= 0) { app.quit(); return; }
        input.append(bytes, int(count));
        int newline;
        while ((newline = input.indexOf('\n')) >= 0) {
            const auto command = input.left(newline);
            input.remove(0, newline + 1);
            if (command == "quit") app.quit();
            else if (command == "snapshot") snapshot();
        }
    });
    QTimer::singleShot(300, &app, snapshot);
    return app.exec();
}
