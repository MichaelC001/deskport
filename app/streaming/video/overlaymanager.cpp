#include "overlaymanager.h"
#include "path.h"
#include "streaming/canvasbridge.h"
#include <QImage>
#include <QPainter>
#include <QTextLayout>
#include <QTextCharFormat>

using namespace Overlay;

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf"))
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    m_Overlays[OverlayType::OverlayDebug].color = {0xD0, 0xD0, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xCC, 0x00, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 36;

    // While TTF will usually not be initialized here, it is valid for that not to
    // be the case, since Session destruction is deferred and could overlap with
    // the lifetime of a new Session object.
    //SDL_assert(TTF_WasInit() == 0);

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    strncpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    m_Overlays[type].text[getOverlayMaxTextLength() - 1] = '\0';

    setOverlayTextUpdated(type);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    m_Renderer = renderer;
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    if (CanvasBridge::enabled()) {
        CanvasBridge::overlay(int(type), m_Overlays[type].enabled, QString::fromUtf8(m_Overlays[type].text));
        return;
    }
    if (m_Renderer == nullptr) {
        return;
    }

    // Construct the required font to render the overlay
    if (m_Overlays[type].font == nullptr) {
        if (m_FontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // m_FontData must stay around until the font is closed
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }

    if (m_Overlays[type].enabled) {
        // The _Wrapped variant is required for line breaks to work
        SDL_Surface* surface = TTF_RenderText_Blended_Wrapped(m_Overlays[type].font,
                                                              m_Overlays[type].text,
                                                              m_Overlays[type].color,
                                                              1024);
        SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, surface);
    }

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);
}

void OverlayManager::updateTextInput(const QString& text, int start, int length)
{
    auto& overlay = m_Overlays[OverlayTextInput];
    overlay.enabled = !text.isEmpty();
    SDL_Surface* surface = nullptr;
    if (overlay.enabled) {
        // Qt's platform font fallback supplies CJK glyphs; the diagnostic TTF
        // only contains Latin glyphs. Never put preedit into diagnostic logs.
        QImage image(960, 44, QImage::Format_ARGB32);
        image.fill(QColor(24, 26, 30, 240));
        QPainter painter(&image);
        QFont font; font.setPixelSize(22);
        painter.setFont(font);
        painter.setPen(Qt::white);
        const auto codepoints = text.toUcs4();
        start = qBound(0, start, int(codepoints.size()));
        length = qBound(0, length, int(codepoints.size()) - start);
        int prefix = 0, selected = 0;
        for (int i = 0; i < start; ++i) prefix += codepoints[i] > 0xffff ? 2 : 1;
        for (int i = start; i < start + length; ++i) selected += codepoints[i] > 0xffff ? 2 : 1;
        QTextLayout layout(text, font);
        QTextCharFormat underline; underline.setFontUnderline(true);
        QTextCharFormat selection = underline;
        selection.setBackground(QColor(60, 90, 145));
        layout.setFormats({{0, int(text.size()), underline}, {int(prefix), int(selected), selection}});
        layout.beginLayout();
        auto line = layout.createLine();
        line.setLineWidth(944);
        layout.endLayout();
        layout.draw(&painter, QPointF(8, 6));
        const auto cursor = qMin(qreal(950), line.cursorToX(prefix) + 8);
        painter.drawLine(QPointF(cursor, 6), QPointF(cursor, 34));
        painter.end();
        surface = SDL_CreateRGBSurfaceWithFormat(0, image.width(), image.height(), 32, SDL_PIXELFORMAT_ARGB8888);
        if (surface) {
            SDL_LockSurface(surface);
            for (int y = 0; y < image.height(); ++y)
                memcpy(static_cast<char*>(surface->pixels) + y * surface->pitch, image.constScanLine(y), image.bytesPerLine());
            SDL_UnlockSurface(surface);
        }
    }
    auto old = static_cast<SDL_Surface*>(SDL_AtomicSetPtr(reinterpret_cast<void**>(&overlay.surface), surface));
    if (old) SDL_FreeSurface(old);
    if (m_Renderer) m_Renderer->notifyOverlayUpdated(OverlayTextInput);
}
