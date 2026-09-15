#include "native.h"
#include "ext-data-control.h"
#include <QSocketNotifier>
#include <QTimer>
#include <QEventLoop>
#include <QHash>
#include <QPointer>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace {
// Pipe I/O is event driven. A slow paste target must not block clipboard RPCs.
class PipeWriter : public QObject {
    int fd;
    QByteArray bytes;
    qsizetype offset = 0;
    QSocketNotifier notifier;
    QTimer deadline;
public:
    PipeWriter(int descriptor, QByteArray data, QObject* parent) : QObject(parent), fd(descriptor), bytes(std::move(data)), notifier(fd, QSocketNotifier::Write) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        connect(&notifier, &QSocketNotifier::activated, this, [this] {
            while (offset < bytes.size()) {
                const auto n = ::write(fd, bytes.constData() + offset, bytes.size() - offset);
                if (n > 0) offset += n;
                else if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
                else { notifier.setEnabled(false); deleteLater(); return; }
            }
            notifier.setEnabled(false); deleteLater();
        });
        deadline.setSingleShot(true); connect(&deadline, &QTimer::timeout, this, &QObject::deleteLater); deadline.start(30000);
    }
    ~PipeWriter() override { ::close(fd); }
};
class WaylandClipboard final : public QObject, public ClipboardNative {
    struct Offer { ext_data_control_offer_v1* object; QStringList formats; };
    struct Source { ext_data_control_source_v1* object; Reader reader; WaylandClipboard* owner; };
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    ext_data_control_manager_v1* manager = nullptr;
    ext_data_control_device_v1* device = nullptr;
    QHash<ext_data_control_offer_v1*, Offer*> offers;
    Offer* selection = nullptr;
    Source* source = nullptr;
    qint64 generation = 0;
    bool healthy = false, reading = false;
    std::unique_ptr<QSocketNotifier> notifier;
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        auto self = static_cast<WaylandClipboard*>(data);
        if (!strcmp(interface, "ext_data_control_manager_v1")) self->manager = static_cast<ext_data_control_manager_v1*>(wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
        if (!strcmp(interface, "wl_seat") && !self->seat) self->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, qMin(version, 1U)));
    }
    static void removed(void*, wl_registry*, uint32_t) {}
    static void offerMime(void* data, ext_data_control_offer_v1*, const char* mime) { static_cast<Offer*>(data)->formats.append(QString::fromUtf8(mime)); }
    static void offered(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* object) {
        auto self = static_cast<WaylandClipboard*>(data); auto offer = new Offer{object, {}};
        static const ext_data_control_offer_v1_listener listener{offerMime};
        ext_data_control_offer_v1_add_listener(object, &listener, offer); self->offers.insert(object, offer);
    }
    static void selected(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* object) {
        auto self = static_cast<WaylandClipboard*>(data); self->selection = self->offers.value(object); ++self->generation;
        if (!self->reading) self->collect();
    }
    void collect() {
        for (auto it = offers.begin(); it != offers.end();) {
            if (it.value() == selection) { ++it; continue; }
            ext_data_control_offer_v1_destroy(it.key()); delete it.value(); it = offers.erase(it);
        }
    }
    static void finished(void* data, ext_data_control_device_v1*) { static_cast<WaylandClipboard*>(data)->healthy = false; }
    static void primary(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* object) {
        auto self = static_cast<WaylandClipboard*>(data);
        if (object && self->offers.contains(object) && self->offers.value(object) != self->selection) {
            auto offer = self->offers.take(object); ext_data_control_offer_v1_destroy(object); delete offer;
        }
    }
    static void send(void* data, ext_data_control_source_v1*, const char* mime, int32_t fd) {
        auto source = static_cast<Source*>(data); const auto reader = source->reader; const auto type = QString::fromUtf8(mime);
        auto owner = source->owner;
        // Leave the Wayland dispatch callback before waiting for a remote response.
        QTimer::singleShot(0, owner, [owner, reader, type, fd] { new PipeWriter(fd, reader(type), owner); });
    }
    static void cancelled(void* data, ext_data_control_source_v1* object) {
        auto source = static_cast<Source*>(data);
        if (source->owner->source == source) source->owner->source = nullptr;
        ext_data_control_source_v1_destroy(object); delete source;
    }
public:
    WaylandClipboard() {
        display = wl_display_connect(nullptr); if (!display) return;
        registry = wl_display_get_registry(display);
        static const wl_registry_listener registryListener{global, removed};
        wl_registry_add_listener(registry, &registryListener, this);
        if (wl_display_roundtrip(display) < 0 || !manager || !seat) return;
        device = ext_data_control_manager_v1_get_data_device(manager, seat);
        static const ext_data_control_device_v1_listener listener{offered, selected, finished, primary};
        ext_data_control_device_v1_add_listener(device, &listener, this);
        if (wl_display_roundtrip(display) < 0) return;
        healthy = true;
        notifier.reset(new QSocketNotifier(wl_display_get_fd(display), QSocketNotifier::Read));
        connect(notifier.get(), &QSocketNotifier::activated, this, [this] {
            if (wl_display_dispatch(display) < 0) { healthy = false; notifier->setEnabled(false); }
            wl_display_flush(display);
        });
    }
    ~WaylandClipboard() override {
        notifier.reset();
        if (source) { ext_data_control_source_v1_destroy(source->object); delete source; }
        for (auto offer : offers) { ext_data_control_offer_v1_destroy(offer->object); delete offer; }
        if (device) ext_data_control_device_v1_destroy(device);
        if (manager) ext_data_control_manager_v1_destroy(manager);
        if (seat) wl_seat_destroy(seat);
        if (registry) wl_registry_destroy(registry);
        if (display) wl_display_disconnect(display);
    }
    bool valid() const override { return healthy; }
    qint64 revision() override { return generation; }
    QStringList formats() override { return selection ? selection->formats : QStringList(); }
    QByteArray read(const QString& mime) override {
        if (!healthy || !selection || !selection->formats.contains(mime) || reading) return {};
        int pipes[2]; if (pipe(pipes) != 0) return {};
        reading = true; auto offer = selection;
        fcntl(pipes[0], F_SETFL, fcntl(pipes[0], F_GETFL) | O_NONBLOCK);
        ext_data_control_offer_v1_receive(offer->object, mime.toUtf8().constData(), pipes[1]); ::close(pipes[1]); wl_display_flush(display);
        QEventLoop loop; QByteArray bytes; bool complete = false;
        QSocketNotifier input(pipes[0], QSocketNotifier::Read);
        connect(&input, &QSocketNotifier::activated, &loop, [&] {
            char chunk[65536]; ssize_t n;
            while ((n = ::read(pipes[0], chunk, sizeof(chunk))) > 0) {
                bytes.append(chunk, n); if (bytes.size() > 128 * 1024 * 1024) { bytes.clear(); loop.quit(); return; }
            }
            if (n == 0) { complete = true; loop.quit(); }
            else if (errno != EAGAIN && errno != EINTR) loop.quit();
        });
        QTimer::singleShot(30000, &loop, &QEventLoop::quit); loop.exec(); input.setEnabled(false); ::close(pipes[0]);
        reading = false; collect(); return complete ? bytes : QByteArray();
    }
    void publish(const QStringList& formats, Reader reader, int = 1) override {
        if (!healthy) return;
        // The compositor cancels the previous source after the new selection.
        auto next = new Source{ext_data_control_manager_v1_create_data_source(manager), std::move(reader), this};
        static const ext_data_control_source_v1_listener listener{send, cancelled};
        ext_data_control_source_v1_add_listener(next->object, &listener, next);
        for (const auto& mime : formats) ext_data_control_source_v1_offer(next->object, mime.toUtf8().constData());
        source = next; ext_data_control_device_v1_set_selection(device, next->object); wl_display_flush(display);
    }
};
}
std::unique_ptr<ClipboardNative> makeWaylandClipboardNative() { return std::unique_ptr<ClipboardNative>(new WaylandClipboard); }
