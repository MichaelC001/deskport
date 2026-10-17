#include "native.h"
#include "ext-data-control.h"
#include "wlr-data-control.h"
#include "pipeio.h"
#include <QSet>
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
// Both protocols have the same selection/FD contract. Only the wire adapters differ.
#define DATA_CONTROL_TRAITS(Name, prefix, maximum) \
struct Name { \
    using Manager = prefix##_manager_v1; using Device = prefix##_device_v1; \
    using Offer = prefix##_offer_v1; using Source = prefix##_source_v1; \
    using DeviceListener = prefix##_device_v1_listener; using OfferListener = prefix##_offer_v1_listener; \
    using SourceListener = prefix##_source_v1_listener; \
    static const char* name() { return #prefix "_manager_v1"; } \
    static Manager* bind(wl_registry* r, uint32_t n, uint32_t v) { return static_cast<Manager*>(wl_registry_bind(r, n, &prefix##_manager_v1_interface, qMin(v, maximum))); } \
    static Device* device(Manager* m, wl_seat* s) { return prefix##_manager_v1_get_data_device(m, s); } \
    static Source* source(Manager* m) { return prefix##_manager_v1_create_data_source(m); } \
    static void listen(Device* p, const DeviceListener* l, void* d) { prefix##_device_v1_add_listener(p,l,d); } \
    static void listen(Offer* p, const OfferListener* l, void* d) { prefix##_offer_v1_add_listener(p,l,d); } \
    static void listen(Source* p, const SourceListener* l, void* d) { prefix##_source_v1_add_listener(p,l,d); } \
    static void destroy(Manager* p) { prefix##_manager_v1_destroy(p); } \
    static void destroy(Device* p) { prefix##_device_v1_destroy(p); } \
    static void destroy(Offer* p) { prefix##_offer_v1_destroy(p); } \
    static void destroy(Source* p) { prefix##_source_v1_destroy(p); } \
    static void offer(Source* s, const char* t) { prefix##_source_v1_offer(s,t); } \
    static void receive(Offer* o, const char* t, int fd) { prefix##_offer_v1_receive(o,t,fd); } \
    static void select(Device* d, Source* s) { prefix##_device_v1_set_selection(d,s); } \
};
DATA_CONTROL_TRAITS(Ext, ext_data_control, 1U)
DATA_CONTROL_TRAITS(Wlr, zwlr_data_control, 2U)
#undef DATA_CONTROL_TRAITS

template<class P>
class WaylandClipboard final : public QObject, public ClipboardNative {
    struct Offer { typename P::Offer* object; QStringList formats; };
    struct State { Reader reader; bool active = true; };
    struct Source { typename P::Source* object; std::shared_ptr<State> state; WaylandClipboard* owner; };
    QSet<Source*> sources;
    bool stopped = false;
    uint32_t managerName = 0, seatName = 0;
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    typename P::Manager* manager = nullptr;
    typename P::Device* device = nullptr;
    QHash<typename P::Offer*, Offer*> offers;
    Offer* selection = nullptr;
    Source* source = nullptr;
    qint64 generation = 0;
    bool healthy = false, reading = false;
    std::unique_ptr<QSocketNotifier> notifier;
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        auto self = static_cast<WaylandClipboard*>(data);
        if (!strcmp(interface, P::name()) && !self->manager) { self->manager = P::bind(registry, name, version); self->managerName = name; }
        if (!strcmp(interface, "wl_seat") && !self->seat) { self->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1)); self->seatName = name; }
    }
    static void removed(void* data, wl_registry*, uint32_t name) {
        auto self = static_cast<WaylandClipboard*>(data);
        if (name == self->managerName || name == self->seatName) self->cancel();
    }
    static void offerMime(void* data, typename P::Offer*, const char* mime) { static_cast<Offer*>(data)->formats.append(QString::fromUtf8(mime)); }
    static void offered(void* data, typename P::Device*, typename P::Offer* object) {
        auto self = static_cast<WaylandClipboard*>(data); auto offer = new Offer{object, {}};
        static const typename P::OfferListener listener{offerMime};
        P::listen(object, &listener, offer); self->offers.insert(object, offer);
    }
    static void selected(void* data, typename P::Device*, typename P::Offer* object) {
        auto self = static_cast<WaylandClipboard*>(data); self->selection = self->offers.value(object); ++self->generation;
        if (!self->reading) self->collect();
    }
    void collect() {
        for (auto it = offers.begin(); it != offers.end();) {
            if (it.value() == selection) { ++it; continue; }
            P::destroy(it.key()); delete it.value(); it = offers.erase(it);
        }
    }
    static void finished(void* data, typename P::Device*) { static_cast<WaylandClipboard*>(data)->cancel(); }
    static void primary(void* data, typename P::Device*, typename P::Offer* object) {
        auto self = static_cast<WaylandClipboard*>(data);
        if (object && self->offers.contains(object) && self->offers.value(object) != self->selection) {
            auto offer = self->offers.take(object); P::destroy(object); delete offer;
        }
    }
    static void send(void* data, typename P::Source*, const char* mime, int32_t fd) {
        auto source = static_cast<Source*>(data); const auto state = source->state;
        const auto type = QString::fromUtf8(mime); auto owner = source->owner;
        // Own the FD immediately, including cancellation before this callback runs.
        auto descriptor = std::shared_ptr<int>(new int(fd), [](int* p) { if (*p >= 0) ::close(*p); delete p; });
        QTimer::singleShot(0, owner, [owner, state, type, descriptor] {
            if (!state->active || !owner->valid()) return;
            const auto bytes = state->reader(type);
            if (!state->active || !owner->valid()) return;
            new ClipboardPipe::Writer(*descriptor, bytes, [owner, state] { return owner->valid() && state->active; }, owner);
            *descriptor = -1;
        });
    }
    static void cancelled(void* data, typename P::Source* object) {
        auto source = static_cast<Source*>(data);
        if (source->owner->source == source) source->owner->source = nullptr;
        source->state->active = false; source->owner->sources.remove(source);
        P::destroy(object); delete source;
    }
public:
    WaylandClipboard() {
        display = wl_display_connect(nullptr); if (!display) return;
        registry = wl_display_get_registry(display);
        static const wl_registry_listener registryListener{global, removed};
        wl_registry_add_listener(registry, &registryListener, this);
        if (wl_display_roundtrip(display) < 0 || !manager || !seat) return;
        device = P::device(manager, seat);
        static const typename P::DeviceListener listener{offered, selected, finished, primary};
        P::listen(device, &listener, this);
        if (wl_display_roundtrip(display) < 0) return;
        healthy = true;
        notifier.reset(new QSocketNotifier(wl_display_get_fd(display), QSocketNotifier::Read));
        connect(notifier.get(), &QSocketNotifier::activated, this, [this] {
            if (wl_display_dispatch(display) < 0) { cancel(); notifier->setEnabled(false); }
            wl_display_flush(display);
        });
    }
    ~WaylandClipboard() override {
        notifier.reset();
        cancel();
        for (auto s : sources) { P::destroy(s->object); delete s; }
        for (auto offer : offers) { P::destroy(offer->object); delete offer; }
        if (device) P::destroy(device);
        if (manager) P::destroy(manager);
        if (seat) wl_seat_destroy(seat);
        if (registry) wl_registry_destroy(registry);
        if (display) wl_display_disconnect(display);
    }
    bool valid() const override { return healthy && !stopped; }
    QString stateMessage() const override { return valid() ? QString() : QStringLiteral("Wayland clipboard unavailable: data-control protocol or seat missing, removed, or disconnected."); }
    void cancel() override { stopped = true; healthy = false; ++generation; for (auto s : sources) s->state->active = false; }
    bool failedRead(const QByteArray& bytes) const override { return bytes.isNull(); }
    qint64 revision() override { return generation; }
    QStringList formats() override { return selection ? selection->formats : QStringList(); }
    QByteArray read(const QString& mime) override {
        if (!healthy || !selection || !selection->formats.contains(mime) || reading) return {};
        int pipes[2]; if (pipe2(pipes, O_CLOEXEC) != 0) return {};
        reading = true; auto offer = selection;
        fcntl(pipes[0], F_SETFL, fcntl(pipes[0], F_GETFL) | O_NONBLOCK);
        P::receive(offer->object, mime.toUtf8().constData(), pipes[1]); ::close(pipes[1]); wl_display_flush(display);
        const auto revision = generation;
        const auto bytes = ClipboardPipe::read(pipes[0], [this, offer, revision] { return valid() && selection == offer && generation == revision; });
        ::close(pipes[0]); reading = false; collect(); return bytes;
    }
    void publish(const QStringList& formats, Reader reader, int = 1) override {
        if (!valid()) return;
        if (formats.isEmpty()) {
            // Release only our live source. Never clear a newer local owner.
            if (source && source->state->active) { source->state->active = false; P::select(device, nullptr); wl_display_flush(display); }
            return;
        }
        if (source) source->state->active = false;
        // The compositor cancels the previous source after the new selection.
        auto next = new Source{P::source(manager), std::make_shared<State>(State{std::move(reader), true}), this};
        static const typename P::SourceListener listener{send, cancelled};
        P::listen(next->object, &listener, next);
        for (const auto& mime : formats) P::offer(next->object, mime.toUtf8().constData());
        sources.insert(next); source = next; P::select(device, next->object); wl_display_flush(display);
    }
};
}
std::unique_ptr<ClipboardNative> makeWaylandClipboardNative(const QString& protocol) {
    if (protocol == "portal") return makePortalClipboardNative();
    std::unique_ptr<ClipboardNative> native;
    if (protocol != "wlr-data-control") native.reset(new WaylandClipboard<Ext>);
    if (native && (native->valid() || protocol == "ext-data-control-v1")) return native;
    native.reset(new WaylandClipboard<Wlr>);
    if (native->valid() || protocol == "wlr-data-control") return native;
    return makePortalClipboardNative();
}
