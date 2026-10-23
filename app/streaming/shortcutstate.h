#pragma once

#include <SDL.h>
#include <atomic>

// Native registration is owned by the shell. Workers enable their local
// fallback only after the shell confirms that the shortcut is registered.
namespace ShortcutState {
inline std::atomic_bool& registrationEnabled() {
    static std::atomic_bool enabled{false};
    return enabled;
}
inline bool enabled() { return registrationEnabled().load(std::memory_order_relaxed); }
inline void setEnabled(bool enabled) { registrationEnabled().store(enabled, std::memory_order_relaxed); }

enum class KeyResult { Pass, Pressed, Consumed, Released };

// A local SDL fallback for GUI + apostrophe. Trigger when the chord is released,
// and consume the terminating key's release even if GUI was released first.
class KeyChord {
public:
    KeyResult handle(const SDL_KeyboardEvent& event, bool registered = enabled()) {
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
            (event.keysym.sym == SDLK_LGUI || event.keysym.sym == SDLK_RGUI) && !(event.keysym.mod & KMOD_GUI)) {
            m_Released = true;
            return registered ? KeyResult::Released : KeyResult::Consumed;
        }
        if (!registered || m_Down || event.type != SDL_KEYDOWN || event.repeat) return KeyResult::Pass;
        const auto modifiers = event.keysym.mod;
        if (!(modifiers & KMOD_GUI) || (modifiers & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT | KMOD_MODE)))
            return KeyResult::Pass;
        // A known layout symbol wins over the US physical position. In
        // particular, do not steal a non-US key that produces another symbol.
        if (event.keysym.sym != SDLK_QUOTE &&
            !(event.keysym.sym == SDLK_UNKNOWN && event.keysym.scancode == SDL_SCANCODE_APOSTROPHE))
            return KeyResult::Pass;
        m_Down = true;
        m_Released = false;
        m_Scan = event.keysym.scancode;
        m_Symbol = event.keysym.sym;
        return KeyResult::Pressed;
    }
    void reset() { m_Down = false; m_Released = false; m_Scan = SDL_SCANCODE_UNKNOWN; m_Symbol = SDLK_UNKNOWN; }
private:
    bool m_Down = false;
    bool m_Released = false;
    SDL_Scancode m_Scan = SDL_SCANCODE_UNKNOWN;
    SDL_Keycode m_Symbol = SDLK_UNKNOWN;
};
}
