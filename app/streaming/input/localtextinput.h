#pragma once

#include <SDL.h>
#include <QByteArray>
#include <QSet>
#include <QString>
#include <functional>
#include <utility>

// Client-only IME adapter. Preedit never crosses the network. No clipboard access.
// All methods run on the SDL event thread, as do the native SDL IME calls.
class LocalTextInput {
public:
    using Commit = std::function<void(const QByteArray&)>;
    using Preedit = std::function<void(const QString&, int, int)>;
    LocalTextInput(bool enabled, Commit commit, Preedit preedit)
        : m_Enabled(enabled), m_Commit(std::move(commit)), m_Preedit(std::move(preedit)) {}
    ~LocalTextInput() { setActive(false); }

    bool enabled() const { return m_Enabled; }
    bool active() const { return m_Active; }
    bool composing() const { return m_Composing; }
    void setActive(bool active) {
        active = active && m_Enabled;
        if (active == m_Active) return;
        if (active) {
            discardQueuedText();
            SDL_SetHint("SDL_IME_INTERNAL_EDITING", "0");
            SDL_SetHint("SDL_IME_SHOW_UI", "1");
            SDL_SetHint("SDL_IME_SUPPORT_EXTENDED_TEXT", "1");
            // A stable local composition area, NOT a remote caret estimate.
            SDL_Rect anchor {0, 0, 1, 48};
            SDL_SetTextInputRect(&anchor);
            m_Started = SDL_GetTicks();
            m_Armed = false;
            m_Active = true;
            SDL_StartTextInput();
        } else {
            m_Active = false; // Native cancellation must never commit remotely.
#if SDL_VERSION_ATLEAST(2, 0, 22)
            SDL_ClearComposition();
#endif
            SDL_StopTextInput();
            discardQueuedText();
            m_Composing = false;
            m_Command = false;
            m_Consumed.clear();
            m_Preedit({}, 0, 0);
        }
    }
    void cancel() {
        const bool restart = m_Active;
        setActive(false);
        if (restart) setActive(true);
    }
    void editing(Uint32 window, Uint32 timestamp, const char* text, int start, int length, Uint32 ownWindow) {
        if (!accept(window, timestamp, ownWindow)) return;
        const auto value = QString::fromUtf8(text ? text : "").left(4096);
        m_Composing = !value.isEmpty();
        if (m_Composing) { m_Command = false; m_Armed = true; }
        m_Preedit(value, qMax(0, start), qMax(0, length));
    }
    void text(Uint32 window, Uint32 timestamp, const char* text, Uint32 ownWindow) {
        if (!accept(window, timestamp, ownWindow) || !text || m_Command || !m_Armed) return;
        const QByteArray value(text);
        // SDL commits are UTF-8; reject malformed/control input defensively.
        // Return/Tab are physical controls when there is no composition.
        if (value.isEmpty() || value.size() > 4096 || QString::fromUtf8(value).toUtf8() != value) return;
        for (const auto c : value) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return;
        m_Composing = false;
        m_Preedit({}, 0, 0);
        m_Commit(value); // Each SDL commit fragment is delivered exactly once.
    }
    bool consumeKey(const SDL_KeyboardEvent& event) {
        if (!m_Active) return false;
        const auto scan = event.keysym.scancode;
        if (event.type == SDL_KEYUP) {
            if (scan >= SDL_SCANCODE_LCTRL && scan <= SDL_SCANCODE_RGUI) m_Command = false;
            return m_Consumed.remove(scan) != 0;
        }
        const auto mod = event.keysym.mod;
        const bool modifier = scan >= SDL_SCANCODE_LCTRL && scan <= SDL_SCANCODE_RGUI;
        const bool languageSwitch = scan == SDL_SCANCODE_SPACE && (mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI));
        const bool command = (mod & (KMOD_CTRL | KMOD_GUI)) && !(mod & KMOD_RALT) && !languageSwitch;
        // Text, dead keys, Option/AltGr and every key used by the candidate UI
        // belong to the local IME. Commands and non-text editing keys can pass.
        const bool printable = (event.keysym.sym >= 0x20 && event.keysym.sym <= 0x10ffff) || (scan >= SDL_SCANCODE_A && scan <= SDL_SCANCODE_0) ||
            (scan >= SDL_SCANCODE_SPACE && scan <= SDL_SCANCODE_SLASH) ||
            (scan >= SDL_SCANCODE_KP_DIVIDE && scan <= SDL_SCANCODE_KP_PERIOD && scan != SDL_SCANCODE_KP_ENTER) ||
            scan == SDL_SCANCODE_NONUSBACKSLASH || scan == SDL_SCANCODE_KP_EQUALS ||
            scan == SDL_SCANCODE_KP_COMMA || scan == SDL_SCANCODE_KP_EQUALSAS400 ||
            (scan >= SDL_SCANCODE_INTERNATIONAL1 && scan <= SDL_SCANCODE_LANG9);
        const bool consume = modifier || scan == SDL_SCANCODE_CAPSLOCK || scan == SDL_SCANCODE_NUMLOCKCLEAR || m_Composing || languageSwitch || (printable && !command);
        if (!modifier) {
            m_Command = command && !m_Composing;
            if (printable && !command) m_Armed = true;
        }
        if (consume) m_Consumed.insert(scan);
        return consume;
    }
private:
    bool accept(Uint32 window, Uint32 timestamp, Uint32 ownWindow) const {
        return m_Active && window == ownWindow && SDL_TICKS_PASSED(timestamp, m_Started);
    }
    static void discardQueuedText() {
        SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT);
#if SDL_VERSION_ATLEAST(2, 0, 22)
        SDL_Event event;
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_TEXTEDITING_EXT, SDL_TEXTEDITING_EXT) == 1)
            SDL_free(event.editExt.text);
#endif
    }
    bool m_Enabled = false, m_Active = false, m_Composing = false, m_Command = false, m_Armed = false;
    Uint32 m_Started = 0;
    QSet<int> m_Consumed;
    Commit m_Commit;
    Preedit m_Preedit;
};
