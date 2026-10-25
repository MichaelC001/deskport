#include "sessionpresentation.h"
#include <QThread>
#include "streaming/video/queuedframesource.h"
#include "streaming/video/ffmpeg.h"
#include "streaming/video/videostatus.h"

struct SessionPresentation::Impl {
    QPointer<QWindow> m_Canvas;
    QJsonObject m_Config;
    QJsonObject m_Status;
    SDL_Window* m_Probe = nullptr;
    Overlay::OverlayManager m_Overlays;
    std::unique_ptr<QueuedVideoSource> m_Source;
    std::unique_ptr<FFmpegVideoDecoder> m_Decoder;
    quint64 m_Generation = 0, m_PresentationGeneration = 0;
    bool m_Ready = false, m_SdlInitialized = false;
    bool m_HasFrames = false;
    int m_ResetAttempts = 0;
};
SessionPresentation::SessionPresentation(QObject* parent) : QObject(parent), m_Impl(std::make_unique<Impl>()) {}
bool SessionPresentation::ready() const { return m_Impl->m_Ready; }
bool SessionPresentation::active() const { return bool(m_Impl->m_Decoder); }
bool SessionPresentation::hasFrames() const { return m_Impl->m_HasFrames; }
void SessionPresentation::preedit(const QString& text) { m_Impl->m_Overlays.updateTextInput(text, 0, text.size()); }
SessionPresentation::~SessionPresentation() { stop(); }
void SessionPresentation::stop() {
    ++m_Impl->m_PresentationGeneration;
    if (m_Impl->m_Source) m_Impl->m_Source->stop();
    const bool wasActive = bool(m_Impl->m_Decoder);
    m_Impl->m_HasFrames = false;
    m_Impl->m_Decoder.reset(); // joins decoding, pacing and rendering before surface teardown
    m_Impl->m_Source.reset();
    if (m_Impl->m_Probe) { SDL_DestroyWindow(m_Impl->m_Probe); m_Impl->m_Probe = nullptr; }
    if (m_Impl->m_SdlInitialized) { SDL_QuitSubSystem(SDL_INIT_VIDEO); m_Impl->m_SdlInitialized = false; }
    if (m_Impl->m_Ready) { m_Impl->m_Ready = false; emit readyChanged(); }
    if (wasActive) emit activeChanged();
}
bool SessionPresentation::configure(const QJsonObject& config) {
    const auto generation = config.value("generation").toDouble();
    const int width = config.value("width").toInt(), height = config.value("height").toInt();
    const int fps = config.value("fps").toInt(), format = config.value("format").toInt();
    if (generation < 1 || generation > 9007199254740991.0 || quint64(generation) <= m_Impl->m_Generation ||
        width < 1 || width > 16384 || height < 1 || height > 16384 || fps < 1 || fps > 1000 ||
        !(format & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265 | VIDEO_FORMAT_MASK_AV1))) return false;
    stop(); m_Impl->m_Config = config; m_Impl->m_Generation = quint64(generation); m_Impl->m_ResetAttempts = 0;
    return !m_Impl->m_Canvas || m_Impl->m_Canvas->width() < 1 || m_Impl->m_Canvas->height() < 1 || start();
}
void SessionPresentation::attach(QWindow* canvas) {
    if (m_Impl->m_Canvas == canvas) return;
    stop(); m_Impl->m_Canvas = canvas;
    // QML creates a fresh canvas before its anchors have assigned a size when
    // returning from Devices. Its geometry notification starts the decoder.
    if (m_Impl->m_Canvas && m_Impl->m_Canvas->width() > 0 && m_Impl->m_Canvas->height() > 0 &&
        !m_Impl->m_Config.isEmpty() && !start()) emit failed(tr("Unable to initialize the desktop canvas."));
}
void SessionPresentation::detach() { stop(); m_Impl->m_Canvas = nullptr; }
void SessionPresentation::resized() {
    if (!m_Impl->m_Canvas || m_Impl->m_Config.isEmpty() ||
        m_Impl->m_Canvas->width() < 1 || m_Impl->m_Canvas->height() < 1) return;
    stop();
    if (!start()) emit failed(tr("Unable to resize the desktop canvas."));
}
bool SessionPresentation::start() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_Impl->m_Canvas || m_Impl->m_Canvas->width() < 1 || m_Impl->m_Canvas->height() < 1) return false;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return false;
    m_Impl->m_SdlInitialized = true;
    auto flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef Q_OS_LINUX
    flags |= SDL_WINDOW_VULKAN;
#endif
    // Supplies platform capability queries only; it is never shown or focused.
    m_Impl->m_Probe = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              m_Impl->m_Canvas->width(), m_Impl->m_Canvas->height(), flags);
    if (!m_Impl->m_Probe) { stop(); return false; }
    const auto generation = ++m_Impl->m_PresentationGeneration;
    m_Impl->m_Source = std::make_unique<QueuedVideoSource>(m_Impl->m_Generation, [this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation == m_Impl->m_PresentationGeneration) emit idrRequested();
        }, Qt::QueuedConnection);
    });
    DECODER_PARAMETERS params{};
    params.window = m_Impl->m_Probe; params.presentationWindow = m_Impl->m_Canvas;
    params.width = m_Impl->m_Config.value("width").toInt(); params.height = m_Impl->m_Config.value("height").toInt();
    params.frameRate = m_Impl->m_Config.value("fps").toInt(); params.videoFormat = m_Impl->m_Config.value("format").toInt();
    params.enableVsync = m_Impl->m_Config.value("vsync").toBool();
    // The visible renderer's swapchain paces presentation. A hidden SDL
    // surface must never be used as the Wayland frame callback source.
    params.enableFramePacing = false;
    params.vds = StreamingPreferences::VideoDecoderSelection(m_Impl->m_Config.value("decoder").toInt());
    params.presentationCallbacks.frameReady = [this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation == m_Impl->m_PresentationGeneration && m_Impl->m_Decoder) m_Impl->m_Decoder->renderFrameOnMainThread();
        }, Qt::QueuedConnection);
    };
    params.presentationCallbacks.firstFrame = [this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] { readyFrame(generation); }, Qt::QueuedConnection);
    };
    params.presentationCallbacks.resetRequired = [this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation != m_Impl->m_PresentationGeneration) return;
            if (++m_Impl->m_ResetAttempts > 3) {
                stop(); emit failed(tr("The desktop canvas could not recover its graphics device."));
            } else resized();
        }, Qt::QueuedConnection);
    };
    m_Impl->m_Decoder = std::make_unique<FFmpegVideoDecoder>(false, m_Impl->m_Source.get(), &m_Impl->m_Overlays);
    status(m_Impl->m_Status);
    if (!m_Impl->m_Decoder->initialize(&params)) { stop(); return false; }
    emit activeChanged();
    emit idrRequested();
    return true;
}
void SessionPresentation::readyFrame(quint64 generation) {
    if (generation != m_Impl->m_PresentationGeneration || !m_Impl->m_Decoder || m_Impl->m_Ready) return;
    m_Impl->m_Ready = true; emit readyChanged();
}
void SessionPresentation::frame(QByteArray bytes) {
    if (!m_Impl->m_Source) return;
    auto frame = EncodedVideoFrame::decode(bytes, LiGetMillis());
    if (!frame) { emit failed(tr("Invalid local video frame.")); return; }
    if (frame->generation != m_Impl->m_Generation) return;
    if (!m_Impl->m_HasFrames) {
        // Keep connection controls visible until encoded video actually arrives.
        m_Impl->m_HasFrames = true; emit activeChanged();
    }
    m_Impl->m_Source->push(std::move(frame));
}
void SessionPresentation::status(const QJsonObject& value) {
    m_Impl->m_Status = value;
    if (!m_Impl->m_Source) return;
    SS_HDR_METADATA hdr{};
    const bool hasMetadata = VideoStatus::mastering(value, hdr);
    const auto bounded = [&](const char* key) {
        const auto number = value.value(key).toDouble();
        return number >= 0 && number <= UINT32_MAX ? uint32_t(number) : 0;
    };
    m_Impl->m_Source->updateMetadata(value.value("hdr").toBool(), hasMetadata ? &hdr : nullptr,
                             bounded("rtt"), bounded("variance"));
}
QJsonObject SessionPresentation::capabilities() const {
    return {{"ok",true}, {"hardware",m_Impl->m_Decoder && m_Impl->m_Decoder->isHardwareAccelerated()},
        {"hdr",m_Impl->m_Decoder && m_Impl->m_Decoder->isHdrSupported()},
        {"colorspace",m_Impl->m_Decoder ? m_Impl->m_Decoder->getDecoderColorspace() : COLORSPACE_REC_601},
        {"range",m_Impl->m_Decoder ? m_Impl->m_Decoder->getDecoderColorRange() : COLOR_RANGE_LIMITED}};
}
void SessionPresentation::overlays(const QJsonObject& value) {
    for (int i = 0; i < Overlay::OverlayTextInput; ++i) {
        const auto state = value.value(QString::number(i)).toObject();
        const auto type = Overlay::OverlayType(i);
        const bool enabled = state.value("enabled").toBool();
        m_Impl->m_Overlays.setOverlayState(type, enabled);
        // The shell's decoder produces its own performance text.
        if (enabled && type != Overlay::OverlayDebug) {
            const auto text = state.value("text").toString().left(512).toUtf8();
            if (text != m_Impl->m_Overlays.getOverlayText(type)) m_Impl->m_Overlays.updateOverlayText(type, text.constData());
        }
    }
}
