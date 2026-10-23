#pragma once
#include <QByteArray>

struct SDL_Window;
class QWindow;

// Wayland compositors only raise a window for the action that the user took.
// A tray click belongs to the desktop shell, which hands its activation token
// to the clicked application. The SDL viewer window may live in another
// thread or a session worker process, so the token is handed over explicitly.
namespace WindowActivation {
// Keeps the user action for the next recall of an SDL viewer window. A
// tokenless global shortcut may remap an unfocused native Wayland window.
// Automatic recovery and ordinary presentation must not grant this fallback.
void setPending(const QByteArray& token, bool shortcut = false);
// Consumes the pending action and shows/raises the same SDL window.
void recall(SDL_Window* window, bool userRequested = false);
// Call only for a global shortcut, immediately before showing a Qt window.
// Some portals omit activation tokens; remapping lets the compositor apply its
// normal show policy instead of requesting activation with an old input serial.
void prepareShortcutRecall(QWindow* window, const QByteArray& token);
#ifdef HAS_X11
// X11 passive keyboard grabs can temporarily clear Qt's cached active window
// even though the server's input focus has not moved. Query it for the shortcut.
bool shortcutWindowIsActive(QWindow* window);
#endif
}
