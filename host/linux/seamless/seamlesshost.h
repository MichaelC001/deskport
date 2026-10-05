// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QString>
#include <QStringList>

#include <QtWaylandCompositor/QWaylandCompositor>

#include <functional>
#include <memory>

class QImage;
class QRegion;
class QTimer;
class QWaylandBufferRef;
class QWaylandOutput;
class QWaylandSurface;
class QWaylandSurfaceGrabber;
class QWaylandView;
class QWaylandXdgShell;
class QWaylandXdgSurface;
class QWaylandXdgToplevel;
class QWindow;

struct SidecarOptions {
    QString socketName;
    int width = 1280;
    int height = 720;
    QString captureDir;
    QStringList childArgv;
    bool selfTest = false;
    bool testClient = false;
};

struct CapturedFrame {
    quint64 windowId = 0;
    quint64 sequence = 0;
    QSize size;
    QByteArray sha256;
};

class JsonEmitter final {
public:
    JsonEmitter();

    bool isOpen() const;
    bool writeEvent(QJsonObject event);
    void writeStderr(const QByteArray& bytes);

private:
    QFile m_Stdout;
    QFile m_Stderr;
};

class SeamlessCompositor final : public QWaylandCompositor {
public:
    SeamlessCompositor(const SidecarOptions& options, JsonEmitter& emitter,
                       QObject* parent = nullptr);
    ~SeamlessCompositor() override;

    bool start(QString* error);
    QString socketNameString() const;
    void requestClientClose();

    std::function<void(quint64)> windowCreated;
    std::function<void(const CapturedFrame&)> frameCaptured;
    std::function<void(quint64)> windowDestroyed;

protected:
    void grabSurface(QWaylandSurfaceGrabber* grabber,
                     const QWaylandBufferRef& buffer) override;

private:
    struct ActiveWindow;

    void handleToplevel(QWaylandXdgToplevel* toplevel,
                        QWaylandXdgSurface* xdgSurface);
    void handleDamage(quint64 windowId, const QRegion& damage);
    void completeCapture(quint64 windowId, QWaylandSurface* surface);
    void finishFrame(QWaylandSurface* surface);
    void handleWindowDestroyed(quint64 windowId, const QString& reason);
    void emitTitle(quint64 windowId);
    void emitAppId(quint64 windowId);
    bool saveCapture(const QImage& image, quint64 windowId, quint64 sequence,
                     QString* fileName, QString* error) const;

    SidecarOptions m_Options;
    JsonEmitter& m_Emitter;
    std::unique_ptr<QWindow> m_ParentWindow;
    QWaylandOutput* m_Output = nullptr;
    QWaylandXdgShell* m_XdgShell = nullptr;
    std::unique_ptr<ActiveWindow> m_Active;
    quint64 m_NextWindowId = 1;
};

class SidecarHost final : public QObject {
public:
    SidecarHost(const SidecarOptions& options, JsonEmitter& emitter,
                QObject* parent = nullptr);
    ~SidecarHost() override;

    bool start(QString* error);
    bool launchChild(const QStringList& argv, QString* error);
    void beginShutdown(const QString& reason, int exitCode);

    std::function<void(quint64)> windowCreated;
    std::function<void(const CapturedFrame&)> frameCaptured;
    std::function<void(quint64)> windowDestroyed;
    std::function<void(int, QProcess::ExitStatus)> childFinished;

private:
    void terminateOwnedChild(int signalNumber);
    bool childGroupAlive() const;
    void settleChildGroup();
    void exitWhenChildStops();
    QProcessEnvironment childEnvironment() const;

    SidecarOptions m_Options;
    JsonEmitter& m_Emitter;
    std::unique_ptr<SeamlessCompositor> m_Compositor;
    QProcess m_Child;
    std::unique_ptr<QTimer> m_KillTimer;
    qint64 m_ChildGroupId = 0;
    int m_KillChecks = 0;
    bool m_KillEscalated = false;
    bool m_ShuttingDown = false;
    int m_ExitCode = 0;
};

bool validateSidecarOptions(SidecarOptions* options, QString* error);
bool validateLaunchArguments(const QStringList& argv, QString* error);
QByteArray normalizedImageSha256(const QImage& image);
QByteArray knownPatternSha256(const QSize& size);
int runKnownPixelTestClient(const SidecarOptions& options);
