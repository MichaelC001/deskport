#include "globalshortcut.h"

#include <QGuiApplication>
#include <QKeySequence>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QSocketNotifier>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>

struct PortalShortcut { QString id; QVariantMap options; };
using PortalShortcuts = QList<PortalShortcut>;
Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(PortalShortcuts)
static QDBusArgument& operator<<(QDBusArgument& arg, const PortalShortcut& value) {
    arg.beginStructure(); arg << value.id << value.options; arg.endStructure(); return arg;
}
static const QDBusArgument& operator>>(const QDBusArgument& arg, PortalShortcut& value) {
    arg.beginStructure(); arg >> value.id >> value.options; arg.endStructure(); return arg;
}

namespace {
const QString Service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString Path = QStringLiteral("/org/freedesktop/portal/desktop");
const QString Interface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString ShortcutId = QStringLiteral("toggle-window");

class PortalGlobalShortcut final : public GlobalShortcutBackend {
    Q_OBJECT
public:
    explicit PortalGlobalShortcut(QObject* parent) : GlobalShortcutBackend(parent),
        m_Watcher(Service, m_Bus, QDBusServiceWatcher::WatchForOwnerChange, this) {
        qDBusRegisterMetaType<PortalShortcut>(); qDBusRegisterMetaType<PortalShortcuts>();
        connect(&m_Watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
                [this](const QString&, const QString& oldOwner, const QString& newOwner) {
            if (!m_Running || oldOwner.isEmpty()) return;
            close();
            if (availability) availability(false);
            if (!newOwner.isEmpty()) initialize();
            // A later appearance is handled by serviceRegistered below.
        });
        connect(&m_Watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
            if (m_Running && !m_Initializing && m_Session.isEmpty() && m_Request.isEmpty()) initialize();
        });
    }
    ~PortalGlobalShortcut() override { stop(); }
    bool start() override {
        if (m_Running) return false;
        m_Running = true;
        if (!m_Bus.isConnected()) { fail("The desktop session bus is unavailable."); return false; }
        m_Bus.connect(Service, Path, Interface, "Activated", this,
                      SLOT(pressed(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        m_Bus.connect(Service, Path, Interface, "Deactivated", this,
                      SLOT(released(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        m_Bus.connect(Service, Path, Interface, "ShortcutsChanged", this,
                      SLOT(shortcutsChanged(QDBusObjectPath,PortalShortcuts)));
        initialize();
        return false;
    }
    void stop() override {
        if (!m_Running) return;
        m_Running = false;
        close();
        m_Bus.disconnect(Service, Path, Interface, "Activated", this,
                         SLOT(pressed(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        m_Bus.disconnect(Service, Path, Interface, "Deactivated", this,
                         SLOT(released(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        m_Bus.disconnect(Service, Path, Interface, "ShortcutsChanged", this,
                         SLOT(shortcutsChanged(QDBusObjectPath,PortalShortcuts)));
    }
private:
    using Reply = std::function<void(const QDBusMessage&)>;
    void call(const QString& interface, const QString& method, const QVariantList& args, Reply done, bool optional = false) {
        auto message = QDBusMessage::createMethodCall(Service, Path, interface, method);
        message.setArguments(args);
        auto pending = new QDBusPendingCallWatcher(m_Bus.asyncCall(message, 5000), this);
        const auto generation = m_Generation;
        connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation, done, optional] {
            const auto reply = pending->reply(); pending->deleteLater();
            if (!m_Running || generation != m_Generation) return;
            if (reply.type() == QDBusMessage::ErrorMessage && !optional) {
                fail("The desktop GlobalShortcuts portal is unavailable (" + reply.errorName() + ")."); return;
            }
            done(reply);
        });
    }
    void disconnectRequest() {
        if (!m_Request.isEmpty()) m_Bus.disconnect(Service, m_Request, "org.freedesktop.portal.Request", "Response",
                                                   this, SLOT(response(uint,QVariantMap)));
    }
    void close() {
        ++m_Generation;
        disconnectRequest();
        if (!m_Session.isEmpty()) m_Bus.disconnect(Service, m_Session, "org.freedesktop.portal.Session", "Closed",
                                                  this, SLOT(closed()));
        for (const auto& item : {qMakePair(m_Request, QStringLiteral("org.freedesktop.portal.Request")),
                                 qMakePair(m_Session, QStringLiteral("org.freedesktop.portal.Session"))}) {
            if (item.first.isEmpty()) continue;
            auto message = QDBusMessage::createMethodCall(Service, item.first, item.second, "Close");
            message.setAutoStartService(false); m_Bus.asyncCall(message);
        }
        m_Request.clear(); m_Session.clear(); m_Response = {}; m_Ready = false; m_Down = false; m_Initializing = false;
    }
    void fail(const QString& reason) { close(); if (error) error(reason); }
    void request(const QString& method, QVariantList args, QVariantMap options,
                 std::function<void(const QVariantMap&)> done) {
        const auto token = "deskport_" + QUuid::createUuid().toString(QUuid::Id128);
        auto sender = m_Bus.baseService().mid(1); sender.replace('.', '_');
        m_Request = Path + "/request/" + sender + '/' + token;
        options["handle_token"] = token;
        m_Response = std::move(done);
        m_Bus.connect(Service, m_Request, "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap)));
        args.append(options);
        const auto expected = m_Request;
        call(Interface, method, args, [this, expected](const QDBusMessage& reply) {
            if (reply.arguments().size() != 1 || qvariant_cast<QDBusObjectPath>(reply.arguments()[0]).path() != expected)
                fail("The shortcut portal returned an unexpected request handle.");
        });
    }
    void initialize() {
        // Service activation can emit serviceRegistered while Registry's
        // reply is still pending. Keep exactly one initialization in flight.
        if (m_Initializing) return;
        m_Initializing = true;
        // Older portals lack Registry; sandboxed applications already have an
        // identity. A host app can identify itself on newer portals without
        // rewriting desktop configuration or asking for input-monitor access.
        call("org.freedesktop.host.portal.Registry", "Register", {QStringLiteral("io.github.keithxc.DeskPort"), QVariantMap{}},
             [this](const QDBusMessage&) {
            request("CreateSession", {}, {{"session_handle_token", "deskport_" + QUuid::createUuid().toString(QUuid::Id128)}},
                    [this](const QVariantMap& values) {
                m_Session = values.value("session_handle").toString();
                if (m_Session.isEmpty()) m_Session = qvariant_cast<QDBusObjectPath>(values.value("session_handle")).path();
                if (!m_Session.startsWith(Path + "/session/")) { fail("The shortcut portal returned an invalid session."); return; }
                m_Bus.connect(Service, m_Session, "org.freedesktop.portal.Session", "Closed", this, SLOT(closed()));
                // Reuse the action ID on every launch so the compositor retains
                // the user's approved binding. The requested default is LOGO+'.
                PortalShortcuts shortcuts{{ShortcutId, {{"description", QStringLiteral("Show or hide DeskPort")},
                                                        {"preferred_trigger", QStringLiteral("LOGO+apostrophe")}}}};
                request("BindShortcuts", {QVariant::fromValue(QDBusObjectPath(m_Session)), QVariant::fromValue(shortcuts), QString()}, {},
                        [this](const QVariantMap& values) { update(qdbus_cast<PortalShortcuts>(values.value("shortcuts"))); });
            });
        }, true);
    }
    void update(const PortalShortcuts& shortcuts) {
        m_Initializing = false;
        m_Down = false;
        if (availability) availability(false);
        m_Ready = false;
        fixedChord = false;
        for (const auto& shortcut : shortcuts) if (shortcut.id == ShortcutId &&
            !shortcut.options.value("trigger_description").toString().isEmpty()) {
            m_Ready = true;
            const auto description = shortcut.options.value("trigger_description").toString();
            fixedChord = QKeySequence::fromString(description, QKeySequence::NativeText) == QKeySequence(Qt::META | Qt::Key_Apostrophe);
        }
        if (!m_Ready) { m_Down = false; if (error) error("The desktop did not bind the DeskPort shortcut."); }
        else if (availability) availability(true);
    }
private slots:
    void response(uint code, const QVariantMap& values) {
        if (m_Request.isEmpty() || !m_Response) return;
        disconnectRequest(); m_Request.clear();
        const auto done = std::move(m_Response); m_Response = {};
        if (code != 0) { fail("Shortcut permission was cancelled or denied by the desktop."); return; }
        done(values);
    }
    void pressed(const QDBusObjectPath& session, const QString& id, qulonglong, const QVariantMap& options) {
        if (!m_Ready || session.path() != m_Session || id != ShortcutId || m_Down) return;
        m_Down = true;
        if (edge) edge(true, options.value("activation_token").toString().toUtf8());
    }
    void released(const QDBusObjectPath& session, const QString& id, qulonglong, const QVariantMap& options) {
        if (!m_Ready || session.path() != m_Session || id != ShortcutId || !m_Down) return;
        m_Down = false;
        if (edge) edge(false, options.value("activation_token").toString().toUtf8());
    }
    void shortcutsChanged(const QDBusObjectPath& session, const PortalShortcuts& shortcuts) {
        if (session.path() == m_Session) update(shortcuts);
    }
    void closed() { fail("The desktop revoked the shortcut session."); }
private:
    QDBusConnection m_Bus = QDBusConnection::sessionBus();
    QDBusServiceWatcher m_Watcher;
    QString m_Request, m_Session;
    std::function<void(const QVariantMap&)> m_Response;
    quint64 m_Generation = 0;
    bool m_Running = false, m_Ready = false, m_Down = false, m_Initializing = false;
};
}

#ifdef HAS_X11
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
namespace {
class X11GlobalShortcut final : public GlobalShortcutBackend {
public:
    explicit X11GlobalShortcut(QObject* parent) : GlobalShortcutBackend(parent) {}
    ~X11GlobalShortcut() override { stop(); }
    bool start() override {
        m_Display = XOpenDisplay(nullptr);
        if (!m_Display) { if (error) error("The X11 display is unavailable."); return false; }
        int opcode, errorBase, major = XkbMajorVersion, minor = XkbMinorVersion;
        if (!XkbQueryExtension(m_Display, &opcode, &m_XkbEvent, &errorBase, &major, &minor)) {
            stop(); if (error) error("The X11 keyboard extension is unavailable."); return false;
        }
        XkbSelectEvents(m_Display, XkbUseCoreKbd, XkbStateNotifyMask | XkbMapNotifyMask,
                        XkbStateNotifyMask | XkbMapNotifyMask);
        XkbSelectEventDetails(m_Display, XkbUseCoreKbd, XkbStateNotify, XkbGroupStateMask, XkbGroupStateMask);
        Bool supported; XkbSetDetectableAutoRepeat(m_Display, True, &supported);
        m_Notifier = new QSocketNotifier(ConnectionNumber(m_Display), QSocketNotifier::Read, this);
        connect(m_Notifier, &QSocketNotifier::activated, this, [this] { receive(); });
        return bind();
    }
    void stop() override {
        delete m_Notifier; m_Notifier = nullptr;
        if (m_Display) { XUngrabKey(m_Display, AnyKey, AnyModifier, DefaultRootWindow(m_Display)); XCloseDisplay(m_Display); }
        m_Display = nullptr; m_Down = false;
    }
private:
    bool bind() {
        if (availability) availability(false);
        XUngrabKey(m_Display, AnyKey, AnyModifier, DefaultRootWindow(m_Display));
        m_Down = false;
        m_Key = 0; m_Modifiers = Mod4Mask;
        XkbStateRec state {};
        if (XkbGetState(m_Display, XkbUseCoreKbd, &state) != Success) {
            if (error) error("The current X11 keyboard group is unavailable."); return false;
        }
        m_Group = state.group;
        int minimum, maximum; XDisplayKeycodes(m_Display, &minimum, &maximum);
        for (int code = minimum; code <= maximum && !m_Key; ++code)
            if (XkbKeycodeToKeysym(m_Display, code, m_Group, 0) == XK_apostrophe) m_Key = code;
        if (!m_Key) { if (error) error("This X11 layout has no apostrophe key without extra modifiers."); return false; }
        unsigned numLock = 0, super = 0;
        auto mapping = XGetModifierMapping(m_Display);
        if (mapping) {
            for (int mod = 0; mod < 8; ++mod) for (int key = 0; key < mapping->max_keypermod; ++key) {
                const auto code = mapping->modifiermap[mod * mapping->max_keypermod + key];
                if (!code) continue;
                const auto symbol = XkbKeycodeToKeysym(m_Display, code, 0, 0);
                if (symbol == XK_Num_Lock) numLock |= 1u << mod;
                if (symbol == XK_Super_L || symbol == XK_Super_R) super |= 1u << mod;
            }
            XFreeModifiermap(mapping);
        }
        if (super) m_Modifiers = (m_Modifiers & ~Mod4Mask) | super;
        // XGrabKey errors are asynchronous. Capture only this connection's
        // errors during its short synchronized registration transaction.
        s_GrabDisplay = m_Display; s_GrabError = 0;
        XSync(m_Display, False);
        s_PreviousError = XSetErrorHandler(&grabError);
        for (unsigned locks : {0u, unsigned(LockMask), numLock, numLock | LockMask})
            XGrabKey(m_Display, m_Key, m_Modifiers | locks, DefaultRootWindow(m_Display), False, GrabModeAsync, GrabModeAsync);
        XSync(m_Display, False);
        XSetErrorHandler(s_PreviousError); s_GrabDisplay = nullptr;
        if (s_GrabError) {
            XUngrabKey(m_Display, AnyKey, AnyModifier, DefaultRootWindow(m_Display)); XFlush(m_Display);
            if (error) error("Meta + apostrophe is already in use on this X11 desktop."); return false;
        }
        if (availability) availability(true);
        return true;
    }
    static int grabError(Display* display, XErrorEvent* event) {
        if (display == s_GrabDisplay) { s_GrabError = event->error_code; return 0; }
        return s_PreviousError ? s_PreviousError(display, event) : 0;
    }
    void receive() {
        while (m_Display && XPending(m_Display)) {
            XEvent event; XNextEvent(m_Display, &event);
            if (event.type == m_XkbEvent) {
                auto& xkb = reinterpret_cast<XkbEvent&>(event);
                if (xkb.any.xkb_type == XkbStateNotify && xkb.state.group != m_Group) bind();
                else if (xkb.any.xkb_type == XkbMapNotify) { XkbRefreshKeyboardMapping(&xkb.map); bind(); }
            } else if (event.type == MappingNotify) { XRefreshKeyboardMapping(&event.xmapping); bind(); }
            else if (event.type == KeyPress && event.xkey.keycode == m_Key && !m_Down) {
                m_Down = true; if (edge) edge(true, {});
            } else if (event.type == KeyRelease && event.xkey.keycode == m_Key && m_Down) {
                if (XPending(m_Display)) {
                    XEvent next; XPeekEvent(m_Display, &next);
                    if (next.type == KeyPress && next.xkey.keycode == event.xkey.keycode && next.xkey.time == event.xkey.time) continue;
                }
                m_Down = false; if (edge) edge(false, {});
            }
        }
    }
    Display* m_Display = nullptr;
    QSocketNotifier* m_Notifier = nullptr;
    unsigned m_Key = 0, m_Modifiers = 0;
    int m_XkbEvent = 0, m_Group = 0;
    bool m_Down = false;
    static Display* s_GrabDisplay;
    static int s_GrabError;
    static XErrorHandler s_PreviousError;
};
Display* X11GlobalShortcut::s_GrabDisplay = nullptr;
int X11GlobalShortcut::s_GrabError = 0;
XErrorHandler X11GlobalShortcut::s_PreviousError = nullptr;
}
#endif

#ifdef HAS_X11
// Xlib macros would otherwise corrupt Qt declarations in the generated moc.
#undef Bool
#undef None
#undef Status
#endif

GlobalShortcutBackend* createGlobalShortcutBackend(QObject* parent) {
#ifdef HAS_X11
    if (QGuiApplication::platformName() == "xcb") return new X11GlobalShortcut(parent);
#endif
    return new PortalGlobalShortcut(parent);
}

#include "globalshortcut_linux.moc"
