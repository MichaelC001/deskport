#include "globalshortcut.h"

#ifdef Q_OS_MACOS
#include <QCoreApplication>
#include <QTimer>
#include <Carbon/Carbon.h>

namespace {
constexpr OSType HotKeySignature = 0x44505254; // DPRT

QString shortcutText(const char* text) {
    return QCoreApplication::translate("GlobalShortcut", text);
}

bool apostropheKey(TISInputSourceRef source, UInt32& key, UInt32& modifiers) {
    if (!source) return false;
    const auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    if (!data) return false;
    const auto layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
    // Command can select a different layout (for example, Dvorak-QWERTY).
    // Both base and Command layers must name apostrophe, matching SDL's
    // unmodified key symbol without silently requiring another modifier.
    for (UInt16 candidate = 0; candidate < 128; ++candidate) {
        bool matches = true;
        for (UInt32 layer : {UInt32(0), UInt32(cmdKey >> 8)}) {
            UInt32 deadKey = 0;
            UniChar output[4] {};
            UniCharCount length = 0;
            const OSStatus status = UCKeyTranslate(layout, candidate, kUCKeyActionDown,
                layer, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask,
                &deadKey, 4, &length, output);
            if (status != noErr || length != 1 || output[0] != '\'') { matches = false; break; }
        }
        if (matches) {
            key = candidate;
            modifiers = cmdKey;
            return true;
        }
    }
    return false;
}

bool currentApostropheKey(UInt32& key, UInt32& modifiers) {
    auto source = TISCopyCurrentKeyboardLayoutInputSource();
    const bool hasLayout = source && TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    const bool found = apostropheKey(source, key, modifiers);
    if (source) CFRelease(source);
    if (found || hasLayout) return found;
    // Input methods can omit Unicode layout data. Use their underlying ASCII
    // layout without changing the user's selected input method.
    source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    const bool fallback = apostropheKey(source, key, modifiers);
    if (source) CFRelease(source);
    return fallback;
}

class MacGlobalShortcut final : public GlobalShortcutBackend {
public:
    explicit MacGlobalShortcut(QObject* parent) : GlobalShortcutBackend(parent) {
        m_ReleaseTimer.setInterval(15);
        connect(&m_ReleaseTimer, &QTimer::timeout, this, [this] { checkRelease(); });
    }
    ~MacGlobalShortcut() override { stop(); }

    bool start() override {
        if (m_Running) return m_HotKey != nullptr;
        const EventTypeSpec events[] {{kEventClassKeyboard, kEventHotKeyPressed},
                                      {kEventClassKeyboard, kEventHotKeyReleased}};
        const OSStatus status = InstallEventHandler(GetApplicationEventTarget(), hotKeyEvent,
            2, events, this, &m_Handler);
        if (status != noErr) {
            if (error) error(shortcutText("Cannot listen for the window shortcut (macOS error %1).").arg(status));
            return false;
        }
        m_Running = true;
        const bool registered = rebind();
        // A layout that cannot produce the fixed symbol can be replaced later.
        // Observe that change even when the initial registration failed.
        CFNotificationCenterAddObserver(CFNotificationCenterGetDistributedCenter(), this,
            layoutChanged, kTISNotifySelectedKeyboardInputSourceChanged, nullptr,
            CFNotificationSuspensionBehaviorDeliverImmediately);
        return registered;
    }

    void stop() override {
        m_Running = false;
        CFNotificationCenterRemoveObserver(CFNotificationCenterGetDistributedCenter(), this,
            kTISNotifySelectedKeyboardInputSourceChanged, nullptr);
        m_ReleaseTimer.stop();
        m_Pressed = m_ReleaseSeen = m_RebindPending = false;
        if (m_HotKey) UnregisterEventHotKey(m_HotKey);
        m_HotKey = nullptr;
        if (m_Handler) RemoveEventHandler(m_Handler);
        m_Handler = nullptr;
        // Stopping or rebinding is cancellation, never a synthetic key release.
    }

private:
    bool rebind() {
        if (!m_Running) return false;
        if (m_Pressed) { m_RebindPending = true; return true; }
        UInt32 key = 0, modifiers = 0;
        const bool mapped = currentApostropheKey(key, modifiers);
        if (mapped && m_HotKey && key == m_Key && modifiers == m_Modifiers) return true;
        // Invalidate queued events even when mapping or registration will fail.
        ++m_RegistrationId;
        if (m_HotKey) UnregisterEventHotKey(m_HotKey);
        m_HotKey = nullptr;
        if (!mapped) {
            if (error) error(shortcutText("This keyboard layout cannot register Command + apostrophe without extra modifiers."));
            return false;
        }
        const EventHotKeyID id {HotKeySignature, m_RegistrationId};
        const OSStatus status = RegisterEventHotKey(key, modifiers, id, GetApplicationEventTarget(),
            kEventHotKeyExclusive, &m_HotKey);
        if (status != noErr) {
            m_HotKey = nullptr;
            if (error) error(shortcutText("Command + apostrophe is unavailable or already in use (macOS error %1).").arg(status));
            return false;
        }
        m_Key = key;
        m_Modifiers = modifiers;
        if (availability) availability(true);
        return true;
    }

    static void layoutChanged(CFNotificationCenterRef, void* observer, CFStringRef, const void*, CFDictionaryRef) {
        auto self = static_cast<MacGlobalShortcut*>(observer);
        QTimer::singleShot(0, self, [self] { if (self->m_Running) self->rebind(); });
    }

    static OSStatus hotKeyEvent(EventHandlerCallRef, EventRef event, void* context) {
        auto self = static_cast<MacGlobalShortcut*>(context);
        EventHotKeyID id {};
        if (!self->m_Running || !self->m_HotKey || GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                nullptr, sizeof(id), nullptr, &id) != noErr ||
            id.signature != HotKeySignature || id.id != self->m_RegistrationId) return eventNotHandledErr;
        if (GetEventKind(event) == kEventHotKeyPressed) {
            if (!self->m_Pressed) {
                self->m_Pressed = true;
                self->m_ReleaseSeen = false;
                if (self->edge) self->edge(true, {});
            }
        } else if (GetEventKind(event) == kEventHotKeyReleased && self->m_Pressed) {
            self->m_ReleaseSeen = true;
            self->m_ReleaseTimer.start();
            self->checkRelease();
        }
        return noErr;
    }

    void checkRelease() {
        if (!m_Pressed || !m_ReleaseSeen) return;
        // Carbon provides the registered chord's release edge. Poll only until
        // its physical key and modifiers are up; no event tap or input monitor
        // is installed, and recalling a viewer cannot inherit held Command.
        if (GetCurrentKeyModifiers() & (cmdKey | shiftKey | optionKey | controlKey)) return;
        KeyMap keys {};
        GetKeys(keys);
        if (reinterpret_cast<const UInt8*>(keys)[m_Key / 8] & (1u << (m_Key % 8))) return;
        m_ReleaseTimer.stop();
        m_Pressed = m_ReleaseSeen = false;
        if (m_RebindPending) { m_RebindPending = false; if (!rebind()) return; }
        if (edge) edge(false, {});
    }

    QTimer m_ReleaseTimer;
    EventHotKeyRef m_HotKey = nullptr;
    EventHandlerRef m_Handler = nullptr;
    UInt32 m_Key = 0, m_Modifiers = 0, m_RegistrationId = 0;
    bool m_Running = false, m_Pressed = false, m_ReleaseSeen = false, m_RebindPending = false;
};
}

GlobalShortcutBackend* createGlobalShortcutBackend(QObject* parent) {
    return new MacGlobalShortcut(parent);
}
#endif
