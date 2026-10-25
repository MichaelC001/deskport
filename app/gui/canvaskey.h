#pragma once
#include <SDL.h>
#include <QKeyEvent>
#include <QGuiApplication>

namespace CanvasKey {
// PC set-1 positions shared by Win32 and the first evdev key codes. This is
// physical placement, deliberately independent of the active keyboard layout.
inline SDL_Scancode pc(unsigned code) {
    static const SDL_Scancode positions[] = {
        SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_1, SDL_SCANCODE_2,
        SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
        SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB,
        SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y,
        SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
        SDL_SCANCODE_RETURN, SDL_SCANCODE_LCTRL, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
        SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON,
        SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_BACKSLASH,
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B, SDL_SCANCODE_N, SDL_SCANCODE_M,
        SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_KP_MULTIPLY,
        SDL_SCANCODE_LALT, SDL_SCANCODE_SPACE, SDL_SCANCODE_CAPSLOCK,
        SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5,
        SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10,
        SDL_SCANCODE_NUMLOCKCLEAR, SDL_SCANCODE_SCROLLLOCK, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9,
        SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_PLUS,
        SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_PERIOD,
        SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_NONUSBACKSLASH, SDL_SCANCODE_F11, SDL_SCANCODE_F12
    };
    return code < SDL_arraysize(positions) ? positions[code] : SDL_SCANCODE_UNKNOWN;
}
inline SDL_Scancode windows(unsigned code) {
    if (!(code & 0x100)) return pc(code);
    switch (code & 0xff) {
    case 0x1c: return SDL_SCANCODE_KP_ENTER; case 0x1d: return SDL_SCANCODE_RCTRL;
    case 0x35: return SDL_SCANCODE_KP_DIVIDE; case 0x37: return SDL_SCANCODE_PRINTSCREEN;
    case 0x38: return SDL_SCANCODE_RALT; case 0x47: return SDL_SCANCODE_HOME;
    case 0x48: return SDL_SCANCODE_UP; case 0x49: return SDL_SCANCODE_PAGEUP;
    case 0x4b: return SDL_SCANCODE_LEFT; case 0x4d: return SDL_SCANCODE_RIGHT;
    case 0x4f: return SDL_SCANCODE_END; case 0x50: return SDL_SCANCODE_DOWN;
    case 0x51: return SDL_SCANCODE_PAGEDOWN; case 0x52: return SDL_SCANCODE_INSERT;
    case 0x53: return SDL_SCANCODE_DELETE; case 0x5b: return SDL_SCANCODE_LGUI;
    case 0x5c: return SDL_SCANCODE_RGUI; case 0x5d: return SDL_SCANCODE_APPLICATION;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}
inline SDL_Scancode evdev(unsigned code) {
    if (code <= 88) return pc(code);
    if (code >= 183 && code <= 194) return SDL_Scancode(SDL_SCANCODE_F13 + code - 183);
    switch (code) {
    case 96: return SDL_SCANCODE_KP_ENTER; case 97: return SDL_SCANCODE_RCTRL;
    case 98: return SDL_SCANCODE_KP_DIVIDE; case 99: return SDL_SCANCODE_PRINTSCREEN;
    case 100: return SDL_SCANCODE_RALT; case 102: return SDL_SCANCODE_HOME;
    case 103: return SDL_SCANCODE_UP; case 104: return SDL_SCANCODE_PAGEUP;
    case 105: return SDL_SCANCODE_LEFT; case 106: return SDL_SCANCODE_RIGHT;
    case 107: return SDL_SCANCODE_END; case 108: return SDL_SCANCODE_DOWN;
    case 109: return SDL_SCANCODE_PAGEDOWN; case 110: return SDL_SCANCODE_INSERT;
    case 111: return SDL_SCANCODE_DELETE; case 119: return SDL_SCANCODE_PAUSE;
    case 125: return SDL_SCANCODE_LGUI; case 126: return SDL_SCANCODE_RGUI;
    case 127: return SDL_SCANCODE_APPLICATION;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}
inline SDL_Scancode mac(unsigned key) {
    static const SDL_Scancode letters[] = { SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
        SDL_SCANCODE_H, SDL_SCANCODE_G, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V,
        SDL_SCANCODE_NONUSBACKSLASH, SDL_SCANCODE_B, SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E,
        SDL_SCANCODE_R, SDL_SCANCODE_Y, SDL_SCANCODE_T, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
        SDL_SCANCODE_4, SDL_SCANCODE_6, SDL_SCANCODE_5, SDL_SCANCODE_EQUALS, SDL_SCANCODE_9,
        SDL_SCANCODE_7, SDL_SCANCODE_MINUS, SDL_SCANCODE_8, SDL_SCANCODE_0, SDL_SCANCODE_RIGHTBRACKET,
        SDL_SCANCODE_O, SDL_SCANCODE_U, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_I, SDL_SCANCODE_P,
        SDL_SCANCODE_RETURN, SDL_SCANCODE_L, SDL_SCANCODE_J, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_K,
        SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_COMMA, SDL_SCANCODE_SLASH,
        SDL_SCANCODE_N, SDL_SCANCODE_M, SDL_SCANCODE_PERIOD, SDL_SCANCODE_TAB, SDL_SCANCODE_SPACE,
        SDL_SCANCODE_GRAVE, SDL_SCANCODE_BACKSPACE };
    if (key < SDL_arraysize(letters)) return letters[key];
    switch (key) {
    case 53: return SDL_SCANCODE_ESCAPE; case 54: return SDL_SCANCODE_RGUI; case 55: return SDL_SCANCODE_LGUI;
    case 56: return SDL_SCANCODE_LSHIFT; case 57: return SDL_SCANCODE_CAPSLOCK; case 58: return SDL_SCANCODE_LALT;
    case 59: return SDL_SCANCODE_LCTRL; case 60: return SDL_SCANCODE_RSHIFT; case 61: return SDL_SCANCODE_RALT;
    case 62: return SDL_SCANCODE_RCTRL; case 65: return SDL_SCANCODE_KP_PERIOD; case 67: return SDL_SCANCODE_KP_MULTIPLY;
    case 69: return SDL_SCANCODE_KP_PLUS; case 71: return SDL_SCANCODE_NUMLOCKCLEAR; case 75: return SDL_SCANCODE_KP_DIVIDE;
    case 76: return SDL_SCANCODE_KP_ENTER; case 78: return SDL_SCANCODE_KP_MINUS; case 81: return SDL_SCANCODE_KP_EQUALS;
    case 82: return SDL_SCANCODE_KP_0; case 83: return SDL_SCANCODE_KP_1; case 84: return SDL_SCANCODE_KP_2;
    case 85: return SDL_SCANCODE_KP_3; case 86: return SDL_SCANCODE_KP_4; case 87: return SDL_SCANCODE_KP_5;
    case 88: return SDL_SCANCODE_KP_6; case 89: return SDL_SCANCODE_KP_7; case 91: return SDL_SCANCODE_KP_8;
    case 92: return SDL_SCANCODE_KP_9;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}
inline SDL_Scancode physical(const QKeyEvent& event, SDL_Keycode symbol) {
    SDL_Scancode scan = SDL_SCANCODE_UNKNOWN;
#ifdef Q_OS_WIN
    scan = windows(event.nativeScanCode());
#elif defined(Q_OS_MACOS)
    // Qt's flags-change events can omit the native key code. Zero is also
    // Carbon's physical A key, so a modifier must not be decoded as A.
    const bool modifier = symbol >= SDLK_LCTRL && symbol <= SDLK_RGUI;
    if (event.nativeVirtualKey() || !modifier) scan = mac(event.nativeVirtualKey());
#elif defined(Q_OS_LINUX)
    if ((QGuiApplication::platformName().startsWith("wayland") || QGuiApplication::platformName() == "xcb") && event.nativeScanCode() >= 8)
        scan = evdev(event.nativeScanCode() - 8);
#endif
    return scan == SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeFromKey(symbol) : scan;
}
}
