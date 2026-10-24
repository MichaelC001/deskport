#pragma once
#include <SDL.h>

#include <QJsonObject>
#include <QSize>

// Authenticated shell/worker IPC supplies presentation state. SDL's real
// window remains hidden and provides only the decoder's platform context.
namespace CanvasBridge {
void configure(const QJsonObject& state);
bool enabled();
void state(const QJsonObject& state);
void requested(bool visible);
bool requested();
void registerWindow(SDL_Window* window);
bool owns(SDL_Window* window);
void synchronize(SDL_Window* window);
Uint32 flags(SDL_Window* window);
Uint32 mouseState(int* x, int* y);
QSize pixels();
qreal scale();
bool fullscreen();
void input(const QJsonObject& event);
void inputEnabled(bool enabled);
void capture(bool grab, bool cursorHidden);
void label(const QString& text);
QJsonObject description();
}
