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

bool layoutKey(TISInputSourceRef source, const ShortcutBinding& binding, UInt32& key) {
    if (!source) return false;
    const auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    if (!data) return false;
    const auto layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
    // Command can select a different layout (for example, Dvorak-QWERTY).
    // Both base and Command layers must name the selected symbol, matching SDL's
    // unmodified key symbol without silently requiring another modifier.
    for (UInt16 candidate = 0; candidate < 128; ++candidate) {
        bool matches = true;
        for (UInt32 layer : {UInt32(0), UInt32(cmdKey >> 8)}) {
            if (layer && !(binding.modifiers & Qt::MetaModifier)) continue;
            UInt32 deadKey = 0;
            UniChar output[4] {};
            UniCharCount length = 0;
            const OSStatus status = UCKeyTranslate(layout, candidate, kUCKeyActionDown,
                layer, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask,
                &deadKey, 4, &length, output);
            if (status != noErr || length != 1 || output[0] != UniChar(binding.character())) { matches = false; break; }
        }
        if (matches) {
            key = candidate;
            return true;
        }
    }
    return false;
}

bool currentKey(const ShortcutBinding& binding, UInt32& key, UInt32& modifiers) {
    modifiers = 0;
    if (binding.modifiers & Qt::MetaModifier) modifiers |= cmdKey;
    if (binding.modifiers & Qt::ControlModifier) modifiers |= controlKey;
    if (binding.modifiers & Qt::AltModifier) modifiers |= optionKey;
    if (binding.modifiers & Qt::ShiftModifier) modifiers |= shiftKey;
    if (binding.key >= Qt::Key_F1 && binding.key <= Qt::Key_F20) {
        const UInt32 functionKeys[] {kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6,
                                     kVK_F7, kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12,
                                     kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18,
                                     kVK_F19, kVK_F20};
        key = functionKeys[binding.key - Qt::Key_F1];
        return true;
    }
    switch (binding.key) {
    case Qt::Key_Space: key = kVK_Space; return true;
    case Qt::Key_Tab: key = kVK_Tab; return true;
    case Qt::Key_Backspace: key = kVK_Delete; return true;
    case Qt::Key_Delete: key = kVK_ForwardDelete; return true;
    case Qt::Key_Home: key = kVK_Home; return true;
    case Qt::Key_End: key = kVK_End; return true;
    case Qt::Key_PageUp: key = kVK_PageUp; return true;
    case Qt::Key_PageDown: key = kVK_PageDown; return true;
    case Qt::Key_Left: key = kVK_LeftArrow; return true;
    case Qt::Key_Right: key = kVK_RightArrow; return true;
    case Qt::Key_Up: key = kVK_UpArrow; return true;
    case Qt::Key_Down: key = kVK_DownArrow; return true;
    case Qt::Key_Escape: key = kVK_Escape; return true;
    case Qt::Key_Return: key = kVK_Return; return true;
    }
    auto source = TISCopyCurrentKeyboardLayoutInputSource();
    const bool hasLayout = source && TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    const bool found = layoutKey(source, binding, key);
    if (source) CFRelease(source);
    if (found || hasLayout) return found;
    // Input methods can omit Unicode layout data. Use their underlying ASCII
    // layout without changing the user's selected input method.
    source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    const bool fallback = layoutKey(source, binding, key);
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
        const bool mapped = currentKey(binding, key, modifiers);
        if (mapped && m_HotKey && key == m_Key && modifiers == m_Modifiers) return true;
        // Invalidate queued events even when mapping or registration will fail.
        ++m_RegistrationId;
        if (m_HotKey) UnregisterEventHotKey(m_HotKey);
        m_HotKey = nullptr;
        if (!mapped) {
            if (error) error(shortcutText("This keyboard layout cannot register the selected shortcut without extra modifiers."));
            return false;
        }
        const EventHotKeyID id {HotKeySignature, m_RegistrationId};
        const OSStatus status = RegisterEventHotKey(key, modifiers, id, GetApplicationEventTarget(),
            kEventHotKeyExclusive, &m_HotKey);
        if (status != noErr) {
            m_HotKey = nullptr;
            if (error) error(shortcutText("The selected shortcut is unavailable or already in use (macOS error %1).").arg(status));
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
