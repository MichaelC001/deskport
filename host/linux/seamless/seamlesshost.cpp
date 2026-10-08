// SPDX-License-Identifier: GPL-3.0-or-later
#include "seamlesshost.h"

#include <QBackingStore>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QExposeEvent>
#include <QFileDevice>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegion>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTemporaryFile>
#include <QTimer>
#include <QWindow>

#include <QtWaylandCompositor/QWaylandBufferRef>
#include <QtWaylandCompositor/QWaylandOutput>
#include <QtWaylandCompositor/QWaylandOutputMode>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QtWaylandCompositor/QWaylandSurfaceGrabber>
#include <QtWaylandCompositor/QWaylandView>
#include <QtWaylandCompositor/QWaylandXdgShell>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <limits>
#include <utility>

#include <sys/types.h>
#include <unistd.h>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace {
constexpr qsizetype MaxJsonLine = 32768;
constexpr int MaxMetadataText = 1024;
constexpr int MaxAppIdText = 255;
constexpr int MaxDamageRectangles = 128;
constexpr int MinimumDimension = 64;
constexpr int MaximumWidth = 7680;
constexpr int MaximumHeight = 4320;
constexpr qint64 MaximumPixels = 32LL * 1024LL * 1024LL;
constexpr int ChildTerminationTimeoutMs = 3000;

QString boundedText(const QString& value, int maximumBytes = MaxMetadataText)
{
    QString result = value.left(maximumBytes);
    while (result.toUtf8().size() > maximumBytes)
        result.chop(1);
    return result;
}

QJsonArray damageRectangles(const QRegion& damage, bool* truncated)
{
    QJsonArray result;
    int count = 0;
    for (const QRect& rectangle : damage) {
        if (count == MaxDamageRectangles) {
            *truncated = true;
            break;
        }
        result.append(QJsonObject{
            {QStringLiteral("x"), rectangle.x()},
            {QStringLiteral("y"), rectangle.y()},
            {QStringLiteral("width"), rectangle.width()},
            {QStringLiteral("height"), rectangle.height()},
        });
        ++count;
    }
    return result;
}

QString bufferTypeName(const QWaylandBufferRef& buffer)
{
    switch (buffer.bufferType()) {
    case QWaylandBufferRef::BufferType_SharedMemory:
        return QStringLiteral("wl_shm");
    case QWaylandBufferRef::BufferType_Egl:
        return QStringLiteral("egl");
    case QWaylandBufferRef::BufferType_Null:
        return QStringLiteral("none");
    }
    return QStringLiteral("unknown");
}

QString grabErrorName(QWaylandSurfaceGrabber::Error error)
{
    switch (error) {
    case QWaylandSurfaceGrabber::InvalidSurface:
        return QStringLiteral("invalid-surface");
    case QWaylandSurfaceGrabber::NoBufferAttached:
        return QStringLiteral("no-buffer-attached");
    case QWaylandSurfaceGrabber::UnknownBufferType:
        return QStringLiteral("unsupported-buffer-type");
    case QWaylandSurfaceGrabber::RendererNotReady:
        return QStringLiteral("renderer-not-ready");
    }
    return QStringLiteral("unknown-grab-error");
}

bool validateDirectory(const QString& path, bool privateRuntime, QString* error)
{
    const QFileInfo info(path);
    const QString absolute = QDir::cleanPath(info.absoluteFilePath());
    if (!info.isAbsolute() || absolute != path) {
        *error = QStringLiteral("Directory must be an absolute, normalized path: %1").arg(path);
        return false;
    }
    if (!info.exists() || !info.isDir() || info.isSymLink()) {
        *error = QStringLiteral("Directory does not exist or is not a real directory: %1").arg(path);
        return false;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || canonical != absolute) {
        *error = QStringLiteral("Directory or one of its ancestors resolves through a symlink: %1")
                     .arg(path);
        return false;
    }
    if (info.ownerId() != static_cast<uint>(::geteuid())) {
        *error = QStringLiteral("Directory is not owned by the current user: %1").arg(path);
        return false;
    }
    const QFileDevice::Permissions permissions = info.permissions();
    if (!(permissions & QFileDevice::WriteOwner) || !(permissions & QFileDevice::ExeOwner)) {
        *error = QStringLiteral("Directory is not writable and searchable by its owner: %1")
                     .arg(path);
        return false;
    }
    const QFileDevice::Permissions groupOrOtherWrite =
        QFileDevice::WriteGroup | QFileDevice::WriteOther;
    if (permissions & groupOrOtherWrite) {
        *error = QStringLiteral("Directory is writable by another user: %1").arg(path);
        return false;
    }
    if (privateRuntime) {
        const QFileDevice::Permissions anyGroupOrOther =
            QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        if (permissions & anyGroupOrOther) {
            *error = QStringLiteral("XDG_RUNTIME_DIR must have mode 0700: %1").arg(path);
            return false;
        }
    }
    return true;
}

void paintKnownPattern(QPainter* painter, const QSize& size)
{
    const int halfWidth = size.width() / 2;
    const int halfHeight = size.height() / 2;
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    painter->fillRect(QRect(QPoint(0, 0), size), QColor(17, 34, 51, 255));
    painter->fillRect(QRect(0, 0, halfWidth, halfHeight), QColor(222, 48, 64, 255));
    painter->fillRect(QRect(halfWidth, 0, size.width() - halfWidth, halfHeight),
                      QColor(30, 190, 105, 255));
    painter->fillRect(QRect(0, halfHeight, halfWidth, size.height() - halfHeight),
                      QColor(42, 91, 220, 255));
    painter->fillRect(QRect(halfWidth, halfHeight, size.width() - halfWidth,
                            size.height() - halfHeight),
                      QColor(245, 201, 48, 255));
    const QRect marker(qMax(0, size.width() / 3), qMax(0, size.height() / 3),
                       qMax(1, size.width() / 7), qMax(1, size.height() / 7));
    painter->fillRect(marker, QColor(151, 71, 255, 255));
}

class KnownPixelWindow final : public QWindow {
public:
    explicit KnownPixelWindow(const QSize& size)
        : m_BackingStore(this)
    {
        setFlags(Qt::FramelessWindowHint);
        setTitle(QStringLiteral("DeskPort Seamless known-pixel test"));
        resize(size);
    }

    void render()
    {
        if (!isExposed() || size().isEmpty())
            return;
        m_BackingStore.resize(size());
        const QRegion region(QRect(QPoint(0, 0), size()));
        m_BackingStore.beginPaint(region);
        QPainter painter(m_BackingStore.paintDevice());
        paintKnownPattern(&painter, size());
        painter.end();
        m_BackingStore.endPaint();
        m_BackingStore.flush(region, this);
        m_Painted = true;
    }

    bool painted() const { return m_Painted; }

protected:
    void exposeEvent(QExposeEvent*) override { render(); }
    void resizeEvent(QResizeEvent*) override { render(); }

private:
    QBackingStore m_BackingStore;
    bool m_Painted = false;
};

QProcessEnvironment sanitizedEnvironment()
{
    const QProcessEnvironment source = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment result;
    static const char* const allowed[] = {
        "DBUS_SESSION_BUS_ADDRESS", "FONTCONFIG_FILE", "FONTCONFIG_PATH", "HOME",
        "LANG", "LC_ALL", "LC_CTYPE", "LD_LIBRARY_PATH", "LOGNAME",
        "NIX_LD_LIBRARY_PATH", "PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH",
        "QT_PLUGIN_PATH", "SHELL", "TERM", "USER", "XDG_CACHE_HOME",
        "XDG_CONFIG_DIRS", "XDG_CONFIG_HOME", "XDG_DATA_DIRS", "XDG_DATA_HOME",
        "XDG_RUNTIME_DIR", "XKB_CONFIG_ROOT",
    };
    for (const char* name : allowed) {
        const QString key = QString::fromLatin1(name);
        if (source.contains(key))
            result.insert(key, source.value(key));
    }
    return result;
}
} // namespace

struct SeamlessCompositor::ActiveWindow {
    quint64 id = 0;
    QPointer<QWaylandSurface> surface;
    QPointer<QWaylandXdgToplevel> toplevel;
    QWaylandView* view = nullptr;
    quint64 nextFrameSequence = 1;
    bool captureInFlight = false;
    QRegion pendingDamage;
};

JsonEmitter::JsonEmitter()
{
    const bool stdoutOpened = m_Stdout.open(stdout,
        QIODevice::WriteOnly | QIODevice::Unbuffered, QFileDevice::DontCloseHandle);
    const bool stderrOpened = m_Stderr.open(stderr,
        QIODevice::WriteOnly | QIODevice::Unbuffered, QFileDevice::DontCloseHandle);
    Q_UNUSED(stdoutOpened);
    Q_UNUSED(stderrOpened);
}

bool JsonEmitter::isOpen() const
{
    return m_Stdout.isOpen();
}

bool JsonEmitter::writeEvent(QJsonObject event)
{
    if (!m_Stdout.isOpen())
        return false;
    event.insert(QStringLiteral("version"), 1);
    QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (line.size() + 1 > MaxJsonLine) {
        line = QJsonDocument(QJsonObject{
            {QStringLiteral("version"), 1},
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("event-too-large")},
            {QStringLiteral("message"), QStringLiteral("A sidecar event exceeded the 32 KiB limit")},
        }).toJson(QJsonDocument::Compact);
    }
    line.append('\n');
    return m_Stdout.write(line) == line.size() && m_Stdout.flush();
}

void JsonEmitter::writeStderr(const QByteArray& bytes)
{
    if (m_Stderr.isOpen()) {
        m_Stderr.write(bytes);
        m_Stderr.flush();
    }
}

SeamlessCompositor::SeamlessCompositor(const SidecarOptions& options,
                                       JsonEmitter& emitter, QObject* parent)
    : QWaylandCompositor(parent), m_Options(options), m_Emitter(emitter),
      m_ParentWindow(std::make_unique<QWindow>())
{
    m_ParentWindow->setSurfaceType(QSurface::RasterSurface);
    m_ParentWindow->setFlags(Qt::FramelessWindowHint);
    m_ParentWindow->resize(m_Options.width, m_Options.height);
}

SeamlessCompositor::~SeamlessCompositor()
{
    if (m_XdgShell)
        QObject::disconnect(m_XdgShell, nullptr, this, nullptr);
    if (m_Active) {
        if (m_Active->surface)
            QObject::disconnect(m_Active->surface, nullptr, this, nullptr);
        if (m_Active->toplevel)
            QObject::disconnect(m_Active->toplevel, nullptr, this, nullptr);
        if (m_Active->view)
            QObject::disconnect(m_Active->view, nullptr, this, nullptr);
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("window-destroy")},
            {QStringLiteral("id"), static_cast<qint64>(m_Active->id)},
            {QStringLiteral("reason"), QStringLiteral("compositor-shutdown")},
        });
        delete m_Active->view;
        m_Active.reset();
    }
    delete m_XdgShell;
    m_XdgShell = nullptr;
    delete m_Output;
    m_Output = nullptr;
    m_ParentWindow.reset();
}

bool SeamlessCompositor::start(QString* error)
{
    setSocketName(m_Options.socketName.toLocal8Bit());
    setUseHardwareIntegrationExtension(false);
    m_ParentWindow->create();

    m_Output = new QWaylandOutput(this, m_ParentWindow.get());
    m_Output->setSizeFollowsWindow(false);
    const QWaylandOutputMode outputMode(QSize(m_Options.width, m_Options.height), 60000);
    m_Output->addMode(outputMode, true);

    QWaylandCompositor::create();
    if (!isCreated()) {
        *error = QStringLiteral("QtWaylandCompositor did not initialize");
        return false;
    }
    m_Output->setCurrentMode(outputMode);

    m_XdgShell = new QWaylandXdgShell(this);
    connect(m_XdgShell, &QWaylandXdgShell::toplevelCreated, this,
            [this](QWaylandXdgToplevel* toplevel, QWaylandXdgSurface* surface) {
                handleToplevel(toplevel, surface);
            });
    connect(m_XdgShell, &QWaylandXdgShell::popupCreated, this,
            [this](QWaylandXdgPopup* popup, QWaylandXdgSurface*) {
                m_Emitter.writeEvent(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("popup-rejected")},
                    {QStringLiteral("reason"), QStringLiteral("m2-single-toplevel-only")},
                });
                popup->sendPopupDone();
            });
    return true;
}

QString SeamlessCompositor::socketNameString() const
{
    return QString::fromLocal8Bit(socketName());
}

void SeamlessCompositor::requestClientClose()
{
    if (m_Active && m_Active->toplevel)
        m_Active->toplevel->sendClose();
}

void SeamlessCompositor::grabSurface(QWaylandSurfaceGrabber* grabber,
                                     const QWaylandBufferRef& buffer)
{
    // M2 is intentionally wl_shm-only. In particular, do not let a current GL
    // context turn an EGL/dmabuf client into an accidental GPU dependency.
    if (!buffer.isSharedMemory()) {
        emit grabber->failed(QWaylandSurfaceGrabber::UnknownBufferType);
        return;
    }
    QWaylandCompositor::grabSurface(grabber, buffer);
}

void SeamlessCompositor::handleToplevel(QWaylandXdgToplevel* toplevel,
                                        QWaylandXdgSurface* xdgSurface)
{
    QWaylandSurface* surface = xdgSurface ? xdgSurface->surface() : nullptr;
    if (!surface) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("invalid-xdg-surface")},
            {QStringLiteral("message"), QStringLiteral("An XDG top-level had no Wayland surface")},
        });
        return;
    }
    if (m_Active) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("window-rejected")},
            {QStringLiteral("reason"), QStringLiteral("m2-single-toplevel-limit")},
        });
        toplevel->sendClose();
        const QPointer<QWaylandSurface> rejected(surface);
        QTimer::singleShot(0, this, [this, rejected] {
            if (rejected)
                destroyClientForSurface(rejected);
        });
        return;
    }
    if (m_NextWindowId > static_cast<quint64>(std::numeric_limits<qint32>::max())) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("window-id-exhausted")},
            {QStringLiteral("message"), QStringLiteral("The monotonic window ID space is exhausted")},
        });
        destroyClientForSurface(surface);
        return;
    }

    auto active = std::make_unique<ActiveWindow>();
    active->id = m_NextWindowId++;
    active->surface = surface;
    active->toplevel = toplevel;
    active->view = new QWaylandView(nullptr, this);
    active->view->setOutput(m_Output);
    active->view->setSurface(surface);
    active->view->setPrimary();
    const quint64 windowId = active->id;
    m_Active = std::move(active);

    connect(toplevel, &QWaylandXdgToplevel::titleChanged, this,
            [this, windowId] { emitTitle(windowId); });
    connect(toplevel, &QWaylandXdgToplevel::appIdChanged, this,
            [this, windowId] { emitAppId(windowId); });
    connect(surface, &QWaylandSurface::damaged, this,
            [this, windowId](const QRegion& damage) { handleDamage(windowId, damage); });
    connect(m_Active->view, &QWaylandView::surfaceDestroyed, this,
            [this, windowId] { handleWindowDestroyed(windowId, QStringLiteral("surface-destroyed")); });

    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-create")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("title"), boundedText(toplevel->title())},
        {QStringLiteral("appId"), boundedText(toplevel->appId(), MaxAppIdText)},
        {QStringLiteral("width"), m_Options.width},
        {QStringLiteral("height"), m_Options.height},
    });
    if (windowCreated)
        windowCreated(windowId);

    const QList<QWaylandXdgToplevel::State> states{
        QWaylandXdgToplevel::ActivatedState,
    };
    toplevel->sendConfigure(QSize(m_Options.width, m_Options.height), states);
}

void SeamlessCompositor::handleDamage(quint64 windowId, const QRegion& damage)
{
    if (!m_Active || m_Active->id != windowId || !m_Active->surface || !m_Active->view)
        return;

    // QWaylandSurfaceGrabber is asynchronous. Keep at most one readback alive
    // per surface and collapse a burst of commits to one bounded follow-up
    // capture. This keeps frame sequences ordered and prevents a fast client
    // from allocating an unbounded number of full-size QImages.
    if (m_Active->captureInFlight) {
        if (!damage.isEmpty())
            m_Active->pendingDamage |= damage.boundingRect();
        return;
    }
    m_Active->captureInFlight = true;

    QWaylandSurface* surface = m_Active->surface;
    m_Output->frameStarted();
    const bool advanced = m_Active->view->advance();
    const QWaylandBufferRef buffer = m_Active->view->currentBuffer();

    if (damage.isEmpty()) {
        completeCapture(windowId, surface);
        return;
    }
    const quint64 sequence = m_Active->nextFrameSequence++;
    if (!advanced || !buffer.hasBuffer()) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
            {QStringLiteral("id"), static_cast<qint64>(windowId)},
            {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
            {QStringLiteral("error"), QStringLiteral("view-buffer-not-ready")},
        });
        completeCapture(windowId, surface);
        return;
    }
    const QSize bufferSize = buffer.size();
    if (bufferSize.width() <= 0 || bufferSize.height() <= 0 ||
        bufferSize.width() > m_Options.width || bufferSize.height() > m_Options.height ||
        static_cast<qint64>(bufferSize.width()) * bufferSize.height() > MaximumPixels) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
            {QStringLiteral("id"), static_cast<qint64>(windowId)},
            {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
            {QStringLiteral("error"), QStringLiteral("unsafe-buffer-size")},
            {QStringLiteral("width"), bufferSize.width()},
            {QStringLiteral("height"), bufferSize.height()},
        });
        completeCapture(windowId, surface);
        return;
    }

    const QRegion boundedDamage = damage.intersected(QRect(QPoint(0, 0), bufferSize));
    bool damageTruncated = false;
    const QJsonArray rectangles = damageRectangles(boundedDamage, &damageTruncated);
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-damage")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
        {QStringLiteral("bufferType"), bufferTypeName(buffer)},
        {QStringLiteral("rectangles"), rectangles},
        {QStringLiteral("rectanglesTruncated"), damageTruncated},
    });

    auto* grabber = new QWaylandSurfaceGrabber(surface, this);
    connect(grabber, &QWaylandSurfaceGrabber::success, this,
            [this, grabber, surface = QPointer<QWaylandSurface>(surface), windowId, sequence,
             rectangles, damageTruncated](const QImage& grabbed) {
                if (!m_Active || m_Active->id != windowId ||
                    m_Active->surface != surface) {
                    grabber->deleteLater();
                    return;
                }
                if (grabbed.width() <= 0 || grabbed.height() <= 0 ||
                    grabbed.width() > m_Options.width || grabbed.height() > m_Options.height ||
                    static_cast<qint64>(grabbed.width()) * grabbed.height() > MaximumPixels) {
                    m_Emitter.writeEvent(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
                        {QStringLiteral("id"), static_cast<qint64>(windowId)},
                        {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                        {QStringLiteral("error"), QStringLiteral("unsafe-grab-size")},
                    });
                } else {
                    const QImage normalized = grabbed.convertToFormat(
                        QImage::Format_RGBA8888_Premultiplied);
                    const QByteArray digest = normalizedImageSha256(normalized);
                    QString captureFile;
                    QString captureError;
                    if (!m_Options.captureDir.isEmpty() &&
                        !saveCapture(normalized, windowId, sequence, &captureFile, &captureError)) {
                        m_Emitter.writeEvent(QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("capture-file-error")},
                            {QStringLiteral("id"), static_cast<qint64>(windowId)},
                            {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                            {QStringLiteral("message"), boundedText(captureError)},
                        });
                    }
                    QJsonObject metadata{
                        {QStringLiteral("type"), QStringLiteral("frame-metadata")},
                        {QStringLiteral("id"), static_cast<qint64>(windowId)},
                        {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                        {QStringLiteral("width"), normalized.width()},
                        {QStringLiteral("height"), normalized.height()},
                        {QStringLiteral("pixelFormat"), QStringLiteral("rgba8888-premultiplied")},
                        {QStringLiteral("sha256"), QString::fromLatin1(digest)},
                        {QStringLiteral("damage"), rectangles},
                        {QStringLiteral("damageTruncated"), damageTruncated},
                    };
                    if (!captureFile.isEmpty())
                        metadata.insert(QStringLiteral("captureFile"), captureFile);
                    m_Emitter.writeEvent(metadata);
                    if (frameCaptured) {
                        frameCaptured(CapturedFrame{
                            windowId,
                            sequence,
                            normalized.size(),
                            digest,
                        });
                    }
                }
                if (surface)
                    completeCapture(windowId, surface);
                grabber->deleteLater();
            });
    connect(grabber, &QWaylandSurfaceGrabber::failed, this,
            [this, grabber, surface = QPointer<QWaylandSurface>(surface), windowId, sequence,
             bufferType = bufferTypeName(buffer)](QWaylandSurfaceGrabber::Error error) {
                if (!m_Active || m_Active->id != windowId ||
                    m_Active->surface != surface) {
                    grabber->deleteLater();
                    return;
                }
                m_Emitter.writeEvent(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
                    {QStringLiteral("id"), static_cast<qint64>(windowId)},
                    {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                    {QStringLiteral("bufferType"), bufferType},
                    {QStringLiteral("error"), grabErrorName(error)},
                });
                if (surface)
                    completeCapture(windowId, surface);
                grabber->deleteLater();
            });
    grabber->grab();
}

void SeamlessCompositor::completeCapture(quint64 windowId, QWaylandSurface* surface)
{
    if (!m_Active || m_Active->id != windowId || m_Active->surface != surface)
        return;
    m_Active->captureInFlight = false;
    finishFrame(surface);
    const QRegion pending = std::exchange(m_Active->pendingDamage, QRegion());
    if (!pending.isEmpty()) {
        const QPointer<QWaylandSurface> expected(surface);
        QTimer::singleShot(0, this, [this, windowId, expected, pending] {
            if (expected && m_Active && m_Active->id == windowId &&
                m_Active->surface == expected) {
                handleDamage(windowId, pending);
            }
        });
    }
}

void SeamlessCompositor::finishFrame(QWaylandSurface*)
{
    if (m_Output)
        m_Output->sendFrameCallbacks();
}

void SeamlessCompositor::handleWindowDestroyed(quint64 windowId, const QString& reason)
{
    if (!m_Active || m_Active->id != windowId)
        return;
    QWaylandView* view = m_Active->view;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-destroy")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("reason"), reason},
    });
    m_Active.reset();
    if (view)
        view->deleteLater();
    if (windowDestroyed)
        windowDestroyed(windowId);
}

void SeamlessCompositor::emitTitle(quint64 windowId)
{
    if (!m_Active || m_Active->id != windowId || !m_Active->toplevel)
        return;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-title")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("title"), boundedText(m_Active->toplevel->title())},
    });
}

void SeamlessCompositor::emitAppId(quint64 windowId)
{
    if (!m_Active || m_Active->id != windowId || !m_Active->toplevel)
        return;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-app-id")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("appId"), boundedText(m_Active->toplevel->appId(), MaxAppIdText)},
    });
}

bool SeamlessCompositor::saveCapture(const QImage& image, quint64 windowId, quint64 sequence,
                                     QString* fileName, QString* error) const
{
    const QString pattern = QDir(m_Options.captureDir).filePath(
        QStringLiteral("deskport-window-%1-frame-%2-XXXXXX.png").arg(windowId).arg(sequence));
    QTemporaryFile output(pattern);
    output.setAutoRemove(true);
    if (!output.open()) {
        *error = output.errorString();
        return false;
    }
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!image.save(&output, "PNG") || !output.flush()) {
        *error = output.errorString().isEmpty() ? QStringLiteral("PNG encoding failed")
                                                : output.errorString();
        return false;
    }
    *fileName = QFileInfo(output.fileName()).fileName();
    output.setAutoRemove(false);
    output.close();
    return true;
}

SidecarHost::SidecarHost(const SidecarOptions& options, JsonEmitter& emitter, QObject* parent)
    : QObject(parent), m_Options(options), m_Emitter(emitter),
      m_Compositor(std::make_unique<SeamlessCompositor>(options, emitter)),
      m_KillTimer(std::make_unique<QTimer>())
{
    m_Compositor->windowCreated = [this](quint64 id) {
        if (windowCreated)
            windowCreated(id);
    };
    m_Compositor->frameCaptured = [this](const CapturedFrame& frame) {
        if (frameCaptured)
            frameCaptured(frame);
    };
    m_Compositor->windowDestroyed = [this](quint64 id) {
        if (windowDestroyed)
            windowDestroyed(id);
    };

    m_KillTimer->setSingleShot(true);
    m_KillTimer->setInterval(ChildTerminationTimeoutMs);
    connect(m_KillTimer.get(), &QTimer::timeout, this, [this] {
        if (!childGroupAlive()) {
            m_ChildGroupId = 0;
            exitWhenChildStops();
            return;
        }
        m_KillEscalated = true;
        terminateOwnedChild(SIGKILL);
        if (++m_KillChecks < 20) {
            m_KillTimer->setInterval(50);
            m_KillTimer->start();
        } else {
            m_Emitter.writeEvent(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("error")},
                {QStringLiteral("code"), QStringLiteral("child-group-cleanup-timeout")},
                {QStringLiteral("message"), QStringLiteral("The owned application process group did not disappear after SIGKILL")},
            });
            exitWhenChildStops();
        }
    });
    connect(&m_Child, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                m_Emitter.writeEvent(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("child-exit")},
                    {QStringLiteral("exitCode"), code},
                    {QStringLiteral("exitStatus"), status == QProcess::NormalExit
                                                          ? QStringLiteral("normal")
                                                          : QStringLiteral("crashed")},
                });
                if (childFinished)
                    childFinished(code, status);
                if (!childGroupAlive()) {
                    m_ChildGroupId = 0;
                    m_KillTimer->stop();
                }
                if (m_ShuttingDown)
                    settleChildGroup();
                else if (!m_Options.selfTest)
                    beginShutdown(QStringLiteral("child-exited"),
                                  status == QProcess::NormalExit ? code : 1);
            });
    connect(&m_Child, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("child-start-failed")},
            {QStringLiteral("message"), boundedText(m_Child.errorString())},
        });
        if (childFinished)
            childFinished(-1, QProcess::CrashExit);
        beginShutdown(QStringLiteral("child-start-failed"), 1);
    });
}

SidecarHost::~SidecarHost()
{
    if (m_Child.state() != QProcess::NotRunning || childGroupAlive()) {
        terminateOwnedChild(SIGTERM);
        if (m_Child.state() != QProcess::NotRunning &&
            !m_Child.waitForFinished(ChildTerminationTimeoutMs)) {
            terminateOwnedChild(SIGKILL);
            m_Child.waitForFinished(1000);
        }
        if (childGroupAlive())
            terminateOwnedChild(SIGKILL);
    }
}

bool SidecarHost::start(QString* error)
{
    if (!m_Compositor->start(error))
        return false;
    if (!m_Options.childArgv.isEmpty() && !launchChild(m_Options.childArgv, error))
        return false;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("ready")},
        {QStringLiteral("socket"), m_Compositor->socketNameString()},
        {QStringLiteral("width"), m_Options.width},
        {QStringLiteral("height"), m_Options.height},
        {QStringLiteral("capture"), QStringLiteral("wl_shm-only")},
        {QStringLiteral("networkStreaming"), false},
    });
    return true;
}

bool SidecarHost::launchChild(const QStringList& argv, QString* error)
{
    if (m_Child.state() != QProcess::NotRunning) {
        *error = QStringLiteral("The sidecar already owns a child process");
        return false;
    }
    if (!validateLaunchArguments(argv, error))
        return false;

    m_Child.setProgram(argv.first());
    m_Child.setArguments(argv.mid(1));
    m_Child.setProcessEnvironment(childEnvironment());
    m_Child.setStandardOutputFile(QStringLiteral("/dev/stderr"), QIODevice::Append);
    m_Child.setStandardErrorFile(QStringLiteral("/dev/stderr"), QIODevice::Append);
#ifdef Q_OS_LINUX
    m_Child.setChildProcessModifier([this] {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        const auto fail = [this](const char* call) { m_Child.failChildProcessModifier(call, errno); };
#else
        // Qt 6.4 (Ubuntu 24.04 packages) has no failure channel; the child exits
        // before exec and QProcess reports that it did not start.
        const auto fail = [](const char*) { ::_exit(127); };
#endif
        if (::setpgid(0, 0) != 0)
            fail("setpgid");
        if (::prctl(PR_SET_PDEATHSIG, SIGTERM) != 0)
            fail("prctl(PR_SET_PDEATHSIG)");
        if (::getppid() == 1)
            ::raise(SIGTERM);
    });
#endif
    m_Child.start();
    if (!m_Child.waitForStarted(5000)) {
        *error = m_Child.errorString();
        return false;
    }
    m_ChildGroupId = m_Child.processId();
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("child-start")},
        {QStringLiteral("pid"), static_cast<qint64>(m_Child.processId())},
        {QStringLiteral("program"), boundedText(argv.first())},
    });
    return true;
}

void SidecarHost::beginShutdown(const QString& reason, int exitCode)
{
    if (m_ShuttingDown)
        return;
    m_ShuttingDown = true;
    m_ExitCode = exitCode;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("stopping")},
        {QStringLiteral("reason"), boundedText(reason)},
    });
    m_Compositor->requestClientClose();
    if (m_Child.state() != QProcess::NotRunning || childGroupAlive()) {
        m_KillChecks = 0;
        m_KillEscalated = false;
        terminateOwnedChild(SIGTERM);
        m_KillTimer->setInterval(ChildTerminationTimeoutMs);
        m_KillTimer->start();
        return;
    }
    exitWhenChildStops();
}

void SidecarHost::terminateOwnedChild(int signalNumber)
{
    const qint64 processId = m_ChildGroupId;
    if (processId <= 1)
        return;
    if (::kill(-static_cast<pid_t>(processId), signalNumber) != 0 && errno != ESRCH) {
        if (m_Child.state() != QProcess::NotRunning && signalNumber == SIGKILL)
            m_Child.kill();
        else if (m_Child.state() != QProcess::NotRunning)
            m_Child.terminate();
    }
}

bool SidecarHost::childGroupAlive() const
{
    if (m_ChildGroupId <= 1)
        return false;
    errno = 0;
    return ::kill(-static_cast<pid_t>(m_ChildGroupId), 0) == 0 || errno == EPERM;
}

void SidecarHost::settleChildGroup()
{
    if (!childGroupAlive()) {
        m_ChildGroupId = 0;
        m_KillTimer->stop();
        exitWhenChildStops();
        return;
    }
    if (!m_KillTimer->isActive()) {
        if (!m_KillEscalated)
            terminateOwnedChild(SIGTERM);
        m_KillTimer->setInterval(m_KillEscalated ? 50 : ChildTerminationTimeoutMs);
        m_KillTimer->start();
    }
}

void SidecarHost::exitWhenChildStops()
{
    QTimer::singleShot(0, QCoreApplication::instance(), [this] {
        QCoreApplication::exit(m_ExitCode);
    });
}

QProcessEnvironment SidecarHost::childEnvironment() const
{
    QProcessEnvironment environment = sanitizedEnvironment();
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), m_Compositor->socketNameString());
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));
    environment.insert(QStringLiteral("QT_WAYLAND_DISABLE_WINDOWDECORATION"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GDK_BACKEND"), QStringLiteral("wayland"));
    environment.insert(QStringLiteral("SDL_VIDEODRIVER"), QStringLiteral("wayland"));
    environment.insert(QStringLiteral("MOZ_ENABLE_WAYLAND"), QStringLiteral("1"));
    environment.insert(QStringLiteral("XDG_SESSION_TYPE"), QStringLiteral("wayland"));
    return environment;
}

bool validateSidecarOptions(SidecarOptions* options, QString* error)
{
    if (options->width < MinimumDimension || options->height < MinimumDimension ||
        options->width > MaximumWidth || options->height > MaximumHeight ||
        static_cast<qint64>(options->width) * options->height > MaximumPixels) {
        *error = QStringLiteral("Width must be 64..7680 and height 64..4320 with at most 33554432 pixels");
        return false;
    }
    if (options->testClient)
        return options->childArgv.isEmpty();

    const QString runtimeDir = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (runtimeDir.isEmpty()) {
        *error = QStringLiteral("XDG_RUNTIME_DIR is required");
        return false;
    }
    if (!validateDirectory(runtimeDir, true, error))
        return false;

    if (options->socketName.isEmpty()) {
        options->socketName = QStringLiteral("deskport-seamless-%1")
                                  .arg(QCoreApplication::applicationPid());
    }
    static const QRegularExpression socketSyntax(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$"));
    if (!socketSyntax.match(options->socketName).hasMatch()) {
        *error = QStringLiteral("Socket name must match [A-Za-z0-9][A-Za-z0-9_.-]{0,63}");
        return false;
    }
    const QByteArray socketPath = QDir(runtimeDir).filePath(options->socketName).toLocal8Bit();
    if (socketPath.size() >= 104) {
        *error = QStringLiteral("The Wayland socket path is too long");
        return false;
    }
    if (QFileInfo::exists(QString::fromLocal8Bit(socketPath))) {
        *error = QStringLiteral("The requested Wayland socket already exists");
        return false;
    }

    if (!options->captureDir.isEmpty() &&
        !validateDirectory(options->captureDir, false, error)) {
        return false;
    }
    if (options->selfTest && !options->childArgv.isEmpty()) {
        *error = QStringLiteral("--self-test cannot be combined with an application after --");
        return false;
    }
    return options->childArgv.isEmpty() || validateLaunchArguments(options->childArgv, error);
}

bool validateLaunchArguments(const QStringList& argv, QString* error)
{
    if (argv.isEmpty()) {
        *error = QStringLiteral("An application is required after --");
        return false;
    }
    if (argv.size() > 256) {
        *error = QStringLiteral("At most 256 application arguments are allowed");
        return false;
    }
    qsizetype total = 0;
    for (const QString& argument : argv) {
        if (argument.contains(QChar::Null) || argument.size() > 16384) {
            *error = QStringLiteral("An application argument is invalid or too long");
            return false;
        }
        total += argument.size();
        if (total > 131072) {
            *error = QStringLiteral("Application arguments exceed the 128 KiB limit");
            return false;
        }
    }
    const QString program = argv.first();
    if (program.isEmpty()) {
        *error = QStringLiteral("The application program is empty");
        return false;
    }
    if (program.contains(QLatin1Char('/'))) {
        const QFileInfo executable(program);
        if (!executable.isAbsolute() || !executable.isFile() || !executable.isExecutable()) {
            *error = QStringLiteral("An application path containing / must name an absolute executable");
            return false;
        }
    } else {
        static const QRegularExpression bareProgram(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.+:-]{0,255}$"));
        if (!bareProgram.match(program).hasMatch()) {
            *error = QStringLiteral("The application program name contains unsafe characters");
            return false;
        }
    }
    return true;
}

QByteArray normalizedImageSha256(const QImage& image)
{
    const QImage normalized = image.format() == QImage::Format_RGBA8888_Premultiplied
        ? image
        : image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    const qsizetype rowBytes = static_cast<qsizetype>(normalized.width()) * 4;
    for (int row = 0; row < normalized.height(); ++row) {
        digest.addData(QByteArrayView(
            reinterpret_cast<const char*>(normalized.constScanLine(row)), rowBytes));
    }
    return digest.result().toHex();
}

QByteArray knownPatternSha256(const QSize& size)
{
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    QPainter painter(&image);
    paintKnownPattern(&painter, size);
    painter.end();
    return normalizedImageSha256(image);
}

int runKnownPixelTestClient(const SidecarOptions& options)
{
    QGuiApplication::setApplicationName(QStringLiteral("deskport-seamless-test-client"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.deskport.SeamlessSelfTest"));
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!app)
        return 2;
    app->setQuitOnLastWindowClosed(false);

    KnownPixelWindow window(QSize(options.width, options.height));
    window.show();
    QTimer::singleShot(100, &window, [&window] { window.render(); });
    QTimer::singleShot(1500, app, [&window, app] {
        const bool painted = window.painted();
        window.close();
        app->exit(painted ? 0 : 1);
    });
    return app->exec();
}
