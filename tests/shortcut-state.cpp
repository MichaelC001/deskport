#include "streaming/shortcutstate.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

using ShortcutState::KeyChord;
using ShortcutState::KeyResult;

static SDL_KeyboardEvent key(Uint32 type, SDL_Keycode symbol = SDLK_QUOTE,
                            SDL_Keymod modifiers = KMOD_LGUI,
                            SDL_Scancode scan = SDL_SCANCODE_APOSTROPHE, bool repeat = false) {
    SDL_KeyboardEvent event{};
    event.type = type;
    event.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.repeat = repeat;
    event.keysym.sym = symbol;
    event.keysym.mod = modifiers;
    event.keysym.scancode = scan;
    return event;
}

int main() {
    KeyChord chord;
    ShortcutState::setEnabled(false);
    assert(chord.handle(key(SDL_KEYDOWN)) == KeyResult::Pass);
    assert(chord.handle(key(SDL_KEYUP)) == KeyResult::Pass);
    ShortcutState::setEnabled(true);

    assert(chord.handle(key(SDL_KEYDOWN)) == KeyResult::Pressed);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTE, KMOD_LGUI, SDL_SCANCODE_APOSTROPHE, true)) == KeyResult::Consumed);
    assert(chord.handle(key(SDL_KEYUP)) == KeyResult::Released);
    assert(chord.handle(key(SDL_KEYUP)) == KeyResult::Pass);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTE, KMOD_LGUI, SDL_SCANCODE_APOSTROPHE, true)) == KeyResult::Pass);

    // Both physical Meta keys count as one modifier; release only when neither remains down.
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTE, KMOD_GUI)) == KeyResult::Pressed);
    assert(chord.handle(key(SDL_KEYUP, SDLK_LGUI, KMOD_RGUI, SDL_SCANCODE_LGUI)) == KeyResult::Pass);
    assert(chord.handle(key(SDL_KEYUP, SDLK_RGUI, KMOD_NONE, SDL_SCANCODE_RGUI)) == KeyResult::Released);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTE, KMOD_NONE, SDL_SCANCODE_APOSTROPHE, true)) == KeyResult::Consumed);
    assert(chord.handle(key(SDL_KEYUP, SDLK_QUOTE, KMOD_NONE)) == KeyResult::Consumed);
    assert(chord.handle(key(SDL_KEYUP, SDLK_QUOTE, KMOD_NONE)) == KeyResult::Pass);

    for (auto modifier : {KMOD_NONE, KMOD_CTRL, KMOD_ALT, KMOD_SHIFT, KMOD_MODE}) {
        const auto mask = modifier == KMOD_NONE ? KMOD_NONE : SDL_Keymod(KMOD_LGUI | modifier);
        assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTE, mask)) == KeyResult::Pass);
    }
    // SDL key symbols may remain unshifted: never accept an extra Shift modifier.
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTEDBL, SDL_Keymod(KMOD_LGUI | KMOD_SHIFT))) == KeyResult::Pass);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTE, SDL_Keymod(KMOD_LGUI | KMOD_SHIFT), SDL_SCANCODE_4)) == KeyResult::Pass);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTE, KMOD_LGUI, SDL_SCANCODE_4)) == KeyResult::Pressed);
    assert(chord.handle(key(SDL_KEYUP, SDLK_QUOTEDBL, KMOD_LGUI, SDL_SCANCODE_4)) == KeyResult::Released);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_a, KMOD_LGUI)) == KeyResult::Pass);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_UNKNOWN, SDL_Keymod(KMOD_LGUI | KMOD_SHIFT))) == KeyResult::Pass);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_UNKNOWN)) == KeyResult::Pressed);
    assert(chord.handle(key(SDL_KEYUP, SDLK_UNKNOWN)) == KeyResult::Released);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_QUOTE, SDL_Keymod(KMOD_RGUI | KMOD_CAPS | KMOD_NUM))) == KeyResult::Pressed);
    assert(chord.handle(key(SDL_KEYUP)) == KeyResult::Released);

    // A revoked registration consumes an already-held key without executing an action.
    assert(chord.handle(key(SDL_KEYDOWN)) == KeyResult::Pressed);
    ShortcutState::setEnabled(false);
    assert(chord.handle(key(SDL_KEYUP)) == KeyResult::Consumed);
    ShortcutState::setEnabled(true);
    assert(chord.handle(key(SDL_KEYDOWN)) == KeyResult::Pressed);
    chord.reset(); // Focus loss cancels a partial chord.
    assert(chord.handle(key(SDL_KEYUP)) == KeyResult::Pass);
    ShortcutState::configure(true, "Ctrl+Alt+K");
    assert(chord.handle(key(SDL_KEYDOWN)) == KeyResult::Pass); // Default no longer matches.
    const auto custom = SDL_Keymod(KMOD_LCTRL | KMOD_RALT);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_k, custom, SDL_SCANCODE_K)) == KeyResult::Pressed);
    assert(chord.handle(key(SDL_KEYUP, SDLK_k, custom, SDL_SCANCODE_K)) == KeyResult::Released);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_k, custom, SDL_SCANCODE_K)) == KeyResult::Pressed);
    ShortcutState::configure(true, "Meta+F8");
    assert(chord.handle(key(SDL_KEYUP, SDLK_k, custom, SDL_SCANCODE_K)) == KeyResult::Consumed);
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_F8, KMOD_GUI, SDL_SCANCODE_F8)) == KeyResult::Pressed);
    ShortcutState::setEnabled(false); ShortcutState::setEnabled(true);
    assert(chord.handle(key(SDL_KEYUP, SDLK_F8, KMOD_GUI, SDL_SCANCODE_F8)) == KeyResult::Consumed);
    ShortcutState::configure(true, "A");
    assert(!ShortcutState::enabled());
    assert(chord.handle(key(SDL_KEYDOWN, SDLK_a, KMOD_NONE, SDL_SCANCODE_A)) == KeyResult::Pass);
    ShortcutState::setEnabled(false);
    puts("PASS: default/custom shortcut fallback, configuration changes, held keys, layout symbols and registration/focus loss");
}
