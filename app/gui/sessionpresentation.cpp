#include "sessionpresentation.h"
#include <QThread>

SessionPresentation::SessionPresentation(QObject* parent) : QObject(parent) {}
SessionPresentation::~SessionPresentation() { stop(); }
void SessionPresentation::stop() {
    ++m_PresentationGeneration;
    if (m_Source) m_Source->stop();
    m_Decoder.reset(); // joins decoding, pacing and rendering before surface teardown
    m_Source.reset();
    if (m_Probe) { SDL_DestroyWindow(m_Probe); m_Probe = nullptr; }
    if (m_SdlInitialized) { SDL_QuitSubSystem(SDL_INIT_VIDEO); m_SdlInitialized = false; }
    if (m_Ready) { m_Ready = false; emit readyChanged(); }
}
bool SessionPresentation::configure(const QJsonObject& config) {
    const auto generation = config.value("generation").toDouble();
    const int width = config.value("width").toInt(), height = config.value("height").toInt();
    const int fps = config.value("fps").toInt(), format = config.value("format").toInt();
    if (generation < 1 || generation > 9007199254740991.0 || quint64(generation) <= m_Generation ||
        width < 1 || width > 16384 || height < 1 || height > 16384 || fps < 1 || fps > 1000 ||
        !(format & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265 | VIDEO_FORMAT_MASK_AV1))) return false;
    stop(); m_Config = config; m_Generation = quint64(generation);
    return !m_Canvas || start();
}
void SessionPresentation::attach(QWindow* canvas) {
    if (m_Canvas == canvas) return;
    stop(); m_Canvas = canvas;
    if (m_Canvas && !m_Config.isEmpty() && !start()) emit failed(tr("Unable to initialize the desktop canvas."));
}
void SessionPresentation::detach() { stop(); m_Canvas = nullptr; }
void SessionPresentation::resized() {
    if (!m_Canvas || m_Config.isEmpty()) return;
    stop();
    if (!start()) emit failed(tr("Unable to resize the desktop canvas."));
}
bool SessionPresentation::start() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_Canvas || m_Canvas->width() < 1 || m_Canvas->height() < 1) return false;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return false;
    m_SdlInitialized = true;
    auto flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef Q_OS_LINUX
    flags |= SDL_WINDOW_VULKAN;
#endif
    // Supplies platform capability queries only; it is never shown or focused.
    m_Probe = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              m_Canvas->width(), m_Canvas->height(), flags);
    if (!m_Probe) { stop(); return false; }
    const auto generation = ++m_PresentationGeneration;
    m_Source = std::make_unique<QueuedVideoSource>(m_Generation, [this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation == m_PresentationGeneration) emit idrRequested();
        }, Qt::QueuedConnection);
    });
    DECODER_PARAMETERS params{};
    params.window = m_Probe; params.presentationWindow = m_Canvas;
    params.width = m_Config.value("width").toInt(); params.height = m_Config.value("height").toInt();
    params.frameRate = m_Config.value("fps").toInt(); params.videoFormat = m_Config.value("format").toInt();
    params.enableVsync = m_Config.value("vsync").toBool();
    // The visible renderer's swapchain paces presentation. A hidden SDL
    // surface must never be used as the Wayland frame callback source.
    params.enableFramePacing = false;
    params.vds = StreamingPreferences::VideoDecoderSelection(m_Config.value("decoder").toInt());
    params.presentationCallbacks.frameReady = [this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation == m_PresentationGeneration && m_Decoder) m_Decoder->renderFrameOnMainThread();
        }, Qt::QueuedConnection);
    };
    params.presentationCallbacks.firstFrame = [this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] { readyFrame(generation); }, Qt::QueuedConnection);
    };
    params.presentationCallbacks.resetRequired = [this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation == m_PresentationGeneration) resized();
        }, Qt::QueuedConnection);
    };
    m_Decoder = std::make_unique<FFmpegVideoDecoder>(false, m_Source.get(), &m_Overlays);
    if (!m_Decoder->initialize(&params)) { stop(); return false; }
    emit idrRequested();
    return true;
}
void SessionPresentation::readyFrame(quint64 generation) {
    if (generation != m_PresentationGeneration || !m_Decoder || m_Ready) return;
    m_Ready = true; emit readyChanged();
}
void SessionPresentation::frame(QByteArray bytes) {
    if (!m_Source) return;
    auto frame = EncodedVideoFrame::decode(bytes, LiGetMillis());
    if (!frame) { emit failed(tr("Invalid local video frame.")); return; }
    m_Source->push(std::move(frame));
}
QJsonObject SessionPresentation::capabilities() const {
    return {{"ok",true}, {"hardware",m_Decoder && m_Decoder->isHardwareAccelerated()},
        {"hdr",m_Decoder && m_Decoder->isHdrSupported()},
        {"colorspace",m_Decoder ? m_Decoder->getDecoderColorspace() : COLORSPACE_REC_601},
        {"range",m_Decoder ? m_Decoder->getDecoderColorRange() : COLOR_RANGE_LIMITED}};
}
