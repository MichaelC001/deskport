#pragma once
#include <SDL.h>
#include <QString>
#include <QRectF>
#include <QImage>
class QPainter;
#include "windowactivation.h"
#include "waylandloading.h"
#include "shortcutstate.h"

constexpr int DeskPortRecallWindow = 105;
constexpr int DeskPortEndSession = 106;
constexpr int DeskPortHideWindow = 107;
constexpr int DeskPortShowDevices = 108;
constexpr int DeskPortToggleWindow = 109;
constexpr int DeskPortReconnect = 110;
constexpr int DeskPortFullscreen = 111;
constexpr int DeskPortTransportEnded = 112;
// Resumes presentation state for background recovery without showing,
// raising or focusing the viewer window.
constexpr int DeskPortRestoreViewer = 113;
// Shortcut callbacks retain focus, so they do not use the tray's focus grace.
constexpr int DeskPortShortcutToggleWindow = 114;
inline bool desktopWindowVisible(SDL_Window* window) {
    return window && !(SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED));
}
inline void recallDesktopWindow(SDL_Window* window) {
    if (!window) return;
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(window);
    SDL_ShowWindow(window);
    // Without the shell's token, Wayland compositors only flash the window.
    if (!WindowActivation::activate(window)) SDL_RaiseWindow(window);
}
// Clicking a panel icon can take keyboard focus from the viewer just before
// the click is reported, so the moment the viewer last lost focus is kept.
inline Uint32& desktopWindowFocusLostAt() { static Uint32 ticks = 0; return ticks; }
inline void noteDesktopWindowFocusLost() { desktopWindowFocusLostAt() = SDL_GetTicks(); }
// A tray click hides the viewer only when it is the window in front; a
// visible window behind other windows is brought forward instead.
inline bool desktopWindowInFront(SDL_Window* window, bool trayClick = true) {
    if (!desktopWindowVisible(window)) return false;
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) return true;
    const Uint32 lost = desktopWindowFocusLostAt();
    return trayClick && lost && SDL_GetTicks() - lost < 600;
}

// Owns the existing native window between streaming sessions. All calls are made
// by the current SDL event owner; ownership moves only after its thread exits.
class TransitionWindow {
public:
    // backdrop is a blurred picture of the previous stream, if one exists.
    TransitionWindow(SDL_Window* window, const QString& text, QImage backdrop = {});
    ~TransitionWindow();
    void pump();
    bool takePresentationShown() { const bool shown=m_PresentationShown; m_PresentationShown=false; return shown; }
    bool takeShortcutRequested() { const bool requested=m_ShortcutRequested; m_ShortcutRequested=false; return requested; }
    SDL_Window* window() const { return m_Window; }
    bool cancelled() const { return m_Cancelled; }
    SDL_Window* takeWindow();
    bool rendering() const { return bool(m_Renderer) || (m_Wayland && m_Wayland->valid()); }
    const QImage& frame() const { return m_Frame; }
    const QImage& backdrop() const { return m_Backdrop; }
    const QString& label() const { return m_Label; }
    unsigned presentedFrames() const { return m_PresentedFrames; }
    // Asks a two-choice question inside this window, styled like the app, in
    // place of a native message box. takeAnswer() returns -1 until the user
    // chooses: 0 for cancel (also Escape or Return), 1 for accept.
    // scale is the desktop's UI scale, which SDL may not report on Wayland.
    void ask(const QString& title, const QString& text, const QString& cancel, const QString& accept, double scale);
    int takeAnswer();
    // Withdraws an unanswered question and shows the connecting state again.
    void dismiss() { m_Asking = false; m_Answer = -1; m_PresentedFrames = 0; }
private:
    void clearRenderer();
    void drawQuestion(QPainter& painter, int width, int height, double scale);
    SDL_Window* m_Window;
    SDL_Renderer* m_Renderer = nullptr;
    std::unique_ptr<WaylandLoading> m_Wayland;
    QString m_Label;
    QImage m_Frame, m_Backdrop;
    bool m_Cancelled = false;
    bool m_PresentationShown = false;
    bool m_ShortcutRequested = false;
    ShortcutState::KeyChord m_ShortcutChord;
    bool m_Pumping = false;
    unsigned m_PresentedFrames = 0;
    Uint32 m_LastPaint = 0;
    bool m_Asking = false;
    int m_Answer = -1;
    QString m_QuestionTitle, m_QuestionText, m_CancelText, m_AcceptText;
    double m_QuestionScale = 1.0;
    // Button bounds in window pixels, from the last painted question.
    QRectF m_CancelButton, m_AcceptButton;
};
