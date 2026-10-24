#pragma once

#include <SDL.h>
#include <atomic>
#include "../backend/shortcutbinding.h"

// Native registration is owned by the shell. Workers enable their local
// fallback only after the shell confirms that the shortcut is registered.
namespace ShortcutState {
inline std::atomic<quint64>& configuration() {
    static std::atomic<quint64> value{quint64(SDLK_QUOTE) | (quint64(8) << 32)};
    return value;
}
constexpr quint64 Enabled = quint64(1) << 36;
constexpr quint64 BindingMask = (quint64(1) << 37) - 1;
inline bool enabled() { return configuration().load(std::memory_order_relaxed) & Enabled; }
inline unsigned modifiers(SDL_Keymod value) {
    return ((value & KMOD_CTRL) ? 1u : 0u) | ((value & KMOD_ALT) ? 2u : 0u) |
        ((value & KMOD_SHIFT) ? 4u : 0u) | ((value & KMOD_GUI) ? 8u : 0u);
}
inline void store(quint64 binding) {
    auto old = configuration().load(std::memory_order_relaxed);
    do {
        if ((old & BindingMask) == binding) return;
        const auto next = binding | (((old >> 37) + 1) << 37);
        if (configuration().compare_exchange_weak(old, next, std::memory_order_relaxed)) return;
    } while (true);
}
inline void setEnabled(bool enabled) {
    const auto value = configuration().load(std::memory_order_relaxed) & BindingMask;
    store(enabled ? value | Enabled : value & ~Enabled);
}
inline void configure(bool enabled, const QString& sequence) {
    const auto binding = ShortcutBinding::parse(sequence);
    SDL_Keycode key = SDLK_UNKNOWN;
    if (binding.key >= Qt::Key_F1 && binding.key <= Qt::Key_F12)
        key = SDLK_F1 + binding.key - Qt::Key_F1;
    else if (binding.valid()) key = binding.character();
    unsigned mask = 0;
    if (binding.modifiers & Qt::ControlModifier) mask |= 1;
    if (binding.modifiers & Qt::AltModifier) mask |= 2;
    if (binding.modifiers & Qt::ShiftModifier) mask |= 4;
    if (binding.modifiers & Qt::MetaModifier) mask |= 8;
    store(quint64(quint32(key)) | (quint64(mask) << 32) |
          (enabled && binding.valid() ? Enabled : quint64(0)));
}

enum class KeyResult { Pass, Pressed, Consumed, Released };

// A local SDL fallback for the configured chord. Trigger when it is released,
// and consume the terminating key's release even if GUI was released first.
class KeyChord {
public:
    KeyResult handle(const SDL_KeyboardEvent& event, bool registered = enabled()) {
        const auto config = configuration().load(std::memory_order_relaxed);
        // A preference change invalidates a held chord, even if the user turns
        // it off and on again before the SDL event loop receives the release.
        if (m_Down && config != m_Configuration) m_Released = true;
        const unsigned wanted = (config >> 32) & 15;
        const bool sameKey = m_Down &&
            (m_Scan != SDL_SCANCODE_UNKNOWN ? event.keysym.scancode == m_Scan : event.keysym.sym == m_Symbol);
        if (sameKey) {
            if (event.type == SDL_KEYUP) {
                const bool activate = !m_Released && registered;
                reset();
                return activate ? KeyResult::Released : KeyResult::Consumed;
            }
            return KeyResult::Consumed;
        }
        if (m_Down && !m_Released && event.type == SDL_KEYUP &&
            (event.keysym.sym == SDLK_LGUI || event.keysym.sym == SDLK_RGUI ||
             event.keysym.sym == SDLK_LCTRL || event.keysym.sym == SDLK_RCTRL ||
             event.keysym.sym == SDLK_LALT || event.keysym.sym == SDLK_RALT ||
             event.keysym.sym == SDLK_LSHIFT || event.keysym.sym == SDLK_RSHIFT) &&
            !(modifiers(SDL_Keymod(event.keysym.mod)) & wanted)) {
            m_Released = true;
            return registered ? KeyResult::Released : KeyResult::Consumed;
        }
        if (!registered || m_Down || event.type != SDL_KEYDOWN || event.repeat) return KeyResult::Pass;
        const auto modifiers = event.keysym.mod;
        if ((modifiers & KMOD_MODE) || ShortcutState::modifiers(SDL_Keymod(modifiers)) != wanted)
            return KeyResult::Pass;
        // A known layout symbol wins over the US physical position. In
        // particular, do not steal a non-US key that produces another symbol.
        const auto primary = SDL_Keycode(quint32(config));
        if (event.keysym.sym != primary &&
            !(primary == SDLK_QUOTE && event.keysym.sym == SDLK_UNKNOWN && event.keysym.scancode == SDL_SCANCODE_APOSTROPHE))
            return KeyResult::Pass;
        m_Down = true;
        m_Released = false;
        m_Scan = event.keysym.scancode;
        m_Symbol = event.keysym.sym;
        m_Configuration = config;
        return KeyResult::Pressed;
    }
    void reset() { m_Down = false; m_Released = false; m_Scan = SDL_SCANCODE_UNKNOWN; m_Symbol = SDLK_UNKNOWN; }
private:
    bool m_Down = false;
    bool m_Released = false;
    SDL_Scancode m_Scan = SDL_SCANCODE_UNKNOWN;
    SDL_Keycode m_Symbol = SDLK_UNKNOWN;
    quint64 m_Configuration = 0;
};
}
