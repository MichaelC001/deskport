#pragma once
#include <QByteArray>

struct SDL_Window;

// Wayland compositors only raise a window for the action that the user took.
// A tray click belongs to the desktop shell, which hands its activation token
// to the clicked application. The SDL viewer window may live in another
// thread or a session worker process, so the token is handed over explicitly.
namespace WindowActivation {
// Keeps a token for the next recall of an SDL viewer window.
void setPending(const QByteArray& token);
// Raises a shown window with the pending token. Returns false without one.
bool activate(SDL_Window* window);
}
