#include "transitionwindow.h"
#include <QImage>
#include <QPainter>
#include <QFontMetricsF>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

TransitionWindow::TransitionWindow(SDL_Window* window, const QString& text, QImage backdrop)
    : m_Window(window), m_Backdrop(std::move(backdrop)) {
    // Keep SDL video alive after the old session releases its reference.
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_SetWindowGrab(window, SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    m_Label = text;
    if (qstrcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        m_Wayland = std::make_unique<WaylandLoading>(window);
    }
#ifdef Q_OS_WIN32
    // A stream's flip-model swap chain keeps its last frame on screen over GDI
    // output, so a software renderer would leave the old picture frozen there.
    // Its own Direct3D 11 swap chain replaces that frame; the stream's renderer
    // recreates one after this renderer is destroyed.
    for (int i = 0; !m_Wayland && !m_Renderer && i < SDL_GetNumRenderDrivers(); ++i) {
        SDL_RendererInfo info;
        if (SDL_GetRenderDriverInfo(i, &info) == 0 && qstrcmp(info.name, "direct3d11") == 0)
            m_Renderer = SDL_CreateRenderer(window, i, SDL_RENDERER_ACCELERATED);
    }
#endif
    if (!m_Wayland && !m_Renderer) m_Renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!rendering()) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Loading renderer unavailable: %s", SDL_GetError());
    pump();
}
void TransitionWindow::clearRenderer() {
    if (m_Renderer) SDL_DestroyRenderer(m_Renderer);
    m_Renderer = nullptr;
    m_Wayland.reset();
#if SDL_VERSION_ATLEAST(2, 28, 0)
    if (m_Window && SDL_HasWindowSurface(m_Window)) SDL_DestroyWindowSurface(m_Window);
#endif
}
TransitionWindow::~TransitionWindow() {
    clearRenderer();
    if (m_Window) SDL_DestroyWindow(m_Window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
SDL_Window* TransitionWindow::takeWindow() {
    SDL_FlushEvents(SDL_KEYDOWN, SDL_MOUSEWHEEL);
    clearRenderer();
    auto window = m_Window; m_Window = nullptr;
    return window;
}
void TransitionWindow::pump() {
    if (!m_Window || m_Pumping) return;
    m_Pumping = true;
    // Cocoa event pumping can dispatch Qt work that pumps this window again or
    // hands its native window to the next stream; stop if that happened.
    SDL_PumpEvents();
    m_Pumping = false;
    if (!m_Window) return;
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_USEREVENT) > 0)
        if (event.user.code == DeskPortRecallWindow) { recallDesktopWindow(m_Window, true); m_PresentationShown=true; }
        else if (event.user.code == DeskPortRestoreViewer) m_PresentationShown=true;
        else if (event.user.code == DeskPortEndSession) { WindowActivation::setPending({}); m_Cancelled = true; }
        else if (event.user.code == DeskPortHideWindow || event.user.code == DeskPortShowDevices) SDL_HideWindow(m_Window);
        else if (event.user.code == DeskPortToggleWindow || event.user.code == DeskPortShortcutToggleWindow) {
            if (desktopWindowInFront(m_Window, event.user.code == DeskPortToggleWindow)) { WindowActivation::setPending({}); SDL_HideWindow(m_Window); } else recallDesktopWindow(m_Window, true);
        }
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_WINDOWEVENT, SDL_WINDOWEVENT) > 0)
        if (event.window.windowID == SDL_GetWindowID(m_Window)) {
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) SDL_HideWindow(m_Window);
            else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { noteDesktopWindowFocusLost(); m_ShortcutChord.reset(); }
            else if (event.window.event == SDL_WINDOWEVENT_HIDDEN || event.window.event == SDL_WINDOWEVENT_MINIMIZED) m_ShortcutChord.reset();
        }
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) SDL_HideWindow(m_Window);
    // Never replay keystrokes or clicks made while the stream was unavailable.
    // Only local shortcuts and an open question consume them.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_KEYDOWN, SDL_MOUSEWHEEL) > 0) {
        if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) && desktopWindowVisible(m_Window) && event.key.windowID == SDL_GetWindowID(m_Window)) {
            const auto shortcut = m_ShortcutChord.handle(event.key);
            if (shortcut != ShortcutState::KeyResult::Pass) {
                if (shortcut == ShortcutState::KeyResult::Released) m_ShortcutRequested = true;
                continue;
            }
        }
        if (event.type == SDL_KEYDOWN && m_Asking && m_Answer < 0 &&
            (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_RETURN)) m_Answer = 0;
        else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) m_Cancelled = true;
        else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && m_Asking && m_Answer < 0 &&
                 event.button.windowID == SDL_GetWindowID(m_Window)) {
            int pixels, points, height;
#if SDL_VERSION_ATLEAST(2, 26, 0)
            SDL_GetWindowSizeInPixels(m_Window, &pixels, &height);
#else
            SDL_GetWindowSize(m_Window, &pixels, &height);
#endif
            SDL_GetWindowSize(m_Window, &points, &height);
            const double scale = points > 0 ? double(pixels) / points : 1.0;
            const QPointF at(event.button.x * scale, event.button.y * scale);
            if (m_AcceptButton.contains(at)) m_Answer = 1;
            else if (m_CancelButton.contains(at)) m_Answer = 0;
        }
    }
    if (m_Cancelled) { SDL_HideWindow(m_Window); return; }
    SDL_FlushEvents(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERBUTTONUP);
    SDL_FlushEvents(SDL_FINGERDOWN, SDL_MULTIGESTURE);
    if (!rendering() || (SDL_GetWindowFlags(m_Window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED))) return;
    int w, h;
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_GetWindowSizeInPixels(m_Window, &w, &h);
#else
    SDL_GetWindowSize(m_Window, &w, &h);
#endif
    // Input/window events above must always be drained, but the 80 ms spinner
    // does not need a full CPU image repaint for every 20 ms progress callback.
    const auto now = SDL_GetTicks();
    if (m_PresentedFrames && m_Frame.size() == QSize(w, h) && now - m_LastPaint < 80) return;
    m_LastPaint = now;
    if (m_Frame.size() != QSize(w, h)) m_Frame = QImage(w, h, QImage::Format_ARGB32);
    m_Frame.fill(QColor(24, 27, 33));
    QPainter painter(&m_Frame); painter.setRenderHint(QPainter::Antialiasing);
    if (!m_Backdrop.isNull()) {
        // Fill the window with the blurred previous picture, cropping rather
        // than stretching it when the window has a new aspect ratio, and dim it
        // so the progress indicator stays readable.
        const QSizeF fitted = QSizeF(w, h).scaled(m_Backdrop.size(), Qt::KeepAspectRatio);
        const QRectF source((m_Backdrop.width() - fitted.width()) / 2, (m_Backdrop.height() - fitted.height()) / 2,
                            fitted.width(), fitted.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(0, 0, w, h), m_Backdrop, source);
        painter.fillRect(QRect(0, 0, w, h), QColor(16, 18, 22, 90));
    }
    if (m_Asking) {
        drawQuestion(painter, w, h, m_QuestionScale);
    } else {
        const int phase = (SDL_GetTicks() / 80) % 12;
        for (int i = 0; i < 12; ++i) {
            const double angle = i * 6.283185307 / 12;
            const int light = 55 + ((i - phase + 12) % 12) * 16;
            painter.setPen(Qt::NoPen); painter.setBrush(QColor(int(light * 0.65), int(light * 0.85), light));
            painter.drawEllipse(QPointF(w / 2 + std::cos(angle) * 22, h / 2 - 28 + std::sin(angle) * 22), 3, 3);
        }
        QFont font; font.setPixelSize(20); painter.setFont(font); painter.setPen(QColor(230, 233, 238));
        painter.drawText(QRect(0, h / 2 + 16, w, 48), Qt::AlignHCenter | Qt::AlignTop, m_Label);
    }
    painter.end();
    if (m_Wayland) { if (m_Wayland->present(m_Frame)) ++m_PresentedFrames; return; }
    auto surface = SDL_CreateRGBSurfaceWithFormatFrom(m_Frame.bits(), w, h, 32, m_Frame.bytesPerLine(), SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return;
    auto texture = SDL_CreateTextureFromSurface(m_Renderer, surface); SDL_FreeSurface(surface);
    if (!texture) return;
    SDL_RenderCopy(m_Renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(m_Renderer); SDL_DestroyTexture(texture); ++m_PresentedFrames;
}

void TransitionWindow::ask(const QString& title, const QString& text, const QString& cancel, const QString& accept, double scale) {
    m_QuestionTitle = title; m_QuestionText = text; m_CancelText = cancel; m_AcceptText = accept;
    m_QuestionScale = std::clamp(scale, 1.0, 4.0);
    m_Asking = true; m_Answer = -1; m_PresentedFrames = 0;
    m_CancelButton = m_AcceptButton = QRectF();
}
int TransitionWindow::takeAnswer() {
    if (m_Answer < 0) return -1;
    const int answer = m_Answer;
    dismiss();
    return answer;
}
// The app's dark palette: card, raised button, line, text, muted text, accent.
void TransitionWindow::drawQuestion(QPainter& painter, int width, int height, double scale) {
    QFont titleFont; titleFont.setPixelSize(int(19 * scale)); titleFont.setBold(true);
    QFont bodyFont; bodyFont.setPixelSize(int(15 * scale));
    QFont buttonFont; buttonFont.setPixelSize(int(15 * scale)); buttonFont.setBold(true);
    const double pad = 24 * scale, gap = 10 * scale, buttonHeight = 40 * scale;
    const double cardWidth = std::min(width - 32 * scale, 460 * scale);
    const double inner = cardWidth - 2 * pad;
    const QRectF titleBounds = QFontMetricsF(titleFont).boundingRect(QRectF(0, 0, inner, 1e6), Qt::TextWordWrap, m_QuestionTitle);
    const QRectF textBounds = QFontMetricsF(bodyFont).boundingRect(QRectF(0, 0, inner, 1e6), Qt::TextWordWrap, m_QuestionText);
    const double cardHeight = pad + titleBounds.height() + gap + textBounds.height() + 2.2 * gap + buttonHeight + pad;
    const QRectF card((width - cardWidth) / 2, (height - cardHeight) / 2, cardWidth, cardHeight);
    painter.setPen(QPen(QColor(0x2f, 0x3b, 0x4d), scale)); painter.setBrush(QColor(0x1c, 0x25, 0x33));
    painter.drawRoundedRect(card, 14 * scale, 14 * scale);
    double y = card.top() + pad;
    painter.setFont(titleFont); painter.setPen(QColor(0xf1, 0xf4, 0xf3));
    painter.drawText(QRectF(card.left() + pad, y, inner, titleBounds.height()), Qt::TextWordWrap, m_QuestionTitle);
    y += titleBounds.height() + gap;
    painter.setFont(bodyFont); painter.setPen(QColor(0xa4, 0xb1, 0xb5));
    painter.drawText(QRectF(card.left() + pad, y, inner, textBounds.height()), Qt::TextWordWrap, m_QuestionText);
    const QFontMetricsF buttonMetrics(buttonFont);
    const double acceptWidth = std::max(110 * scale, buttonMetrics.horizontalAdvance(m_AcceptText) + 32 * scale);
    const double cancelWidth = std::max(90 * scale, buttonMetrics.horizontalAdvance(m_CancelText) + 32 * scale);
    const double buttonTop = card.bottom() - pad - buttonHeight;
    m_AcceptButton = QRectF(card.right() - pad - acceptWidth, buttonTop, acceptWidth, buttonHeight);
    m_CancelButton = QRectF(m_AcceptButton.left() - gap - cancelWidth, buttonTop, cancelWidth, buttonHeight);
    painter.setFont(buttonFont);
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(0x25, 0x30, 0x42));
    painter.drawRoundedRect(m_CancelButton, 9 * scale, 9 * scale);
    painter.setPen(QColor(0xf1, 0xf4, 0xf3)); painter.drawText(m_CancelButton, Qt::AlignCenter, m_CancelText);
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(0x32, 0x69, 0xd7));
    painter.drawRoundedRect(m_AcceptButton, 9 * scale, 9 * scale);
    painter.setPen(QColor(0xff, 0xff, 0xff)); painter.drawText(m_AcceptButton, Qt::AlignCenter, m_AcceptText);
}
