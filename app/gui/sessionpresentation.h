#pragma once
#include <QObject>
#include <QPointer>
#include <QJsonObject>
#include <QWindow>
#include <memory>
#include "streaming/video/queuedframesource.h"
#include "streaming/video/ffmpeg.h"

// Presentation lifetime is independent of the network worker. Only the selected
// session owns the shared child canvas; switching joins its decoder first.
class SessionPresentation : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    explicit SessionPresentation(QObject* parent = nullptr);
    ~SessionPresentation() override;
    bool ready() const { return m_Ready; }
    bool active() const { return bool(m_Decoder); }
    bool configure(const QJsonObject& config);
    void frame(QByteArray bytes);
    void status(const QJsonObject& status);
    void attach(QWindow* canvas);
    void detach();
    void resized();
    QJsonObject capabilities() const;
signals:
    void readyChanged();
    void activeChanged();
    void idrRequested();
    void failed(QString error);
private:
    bool start();
    void stop();
    void readyFrame(quint64 generation);
    QPointer<QWindow> m_Canvas;
    QJsonObject m_Config;
    QJsonObject m_Status;
    SDL_Window* m_Probe = nullptr;
    Overlay::OverlayManager m_Overlays;
    std::unique_ptr<QueuedVideoSource> m_Source;
    std::unique_ptr<FFmpegVideoDecoder> m_Decoder;
    quint64 m_Generation = 0, m_PresentationGeneration = 0;
    bool m_Ready = false, m_SdlInitialized = false;
};
