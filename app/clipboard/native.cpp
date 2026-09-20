#include "native.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QVariant>
#ifdef Q_OS_WIN
#include <QBuffer>
#include <QImage>
#include "windowsnative.h"
#endif

namespace {
class LazyMime final : public QMimeData {
public:
    QStringList types;
    ClipboardNative::Reader reader;
    QStringList formats() const override { return types; }
    bool hasFormat(const QString& type) const override { return types.contains(type); }
protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QVariant retrieveData(const QString& mime, QMetaType) const override {
#ifdef Q_OS_WIN
        if (mime == "application/x-qt-image" && types.contains("image/png"))
            return QImage::fromData(reader("image/png"), "PNG");
#endif
        return reader(mime);
    }
#else
    QVariant retrieveData(const QString& mime, QVariant::Type) const override {
#ifdef Q_OS_WIN
        if (mime == "application/x-qt-image" && types.contains("image/png"))
            return QImage::fromData(reader("image/png"), "PNG");
#endif
        return reader(mime);
    }
#endif
};
class QtClipboard final : public ClipboardNative {
#ifdef Q_OS_WIN
    DeskPortWindowsClipboard::Data* owned = nullptr;
#endif
    qint64 generation = 0;
    QMetaObject::Connection connection;
public:
    QtClipboard() { connection = QObject::connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, [this] { ++generation; }); }
    ~QtClipboard() override {
        QObject::disconnect(connection);
#ifdef Q_OS_WIN
        if (owned) {
            owned->revoke();
            if (OleIsCurrentClipboard(owned) == S_OK) OleSetClipboard(nullptr);
            owned->Release();
        }
#endif
    }
    qint64 revision() override { return generation; }
    QStringList formats() override {
        auto m = QGuiApplication::clipboard()->mimeData();
        auto formats = m ? m->formats() : QStringList();
#ifdef Q_OS_WIN
        if (IsClipboardFormatAvailable(DeskPortWindowsClipboard::markerFormat()))
            formats.append(QStringLiteral("application/x-deskport-clipboard"));
        if (m && m->hasImage() && !formats.contains("image/png")) formats.append("image/png");
#endif
        return formats;
    }
    QByteArray read(const QString& mime) override {
        auto m = QGuiApplication::clipboard()->mimeData();
#ifdef Q_OS_WIN
        if (m && mime == "image/png" && !m->hasFormat(mime) && m->hasImage()) {
            const auto image = qvariant_cast<QImage>(m->imageData());
            if (image.isNull() || qint64(image.width()) * image.height() > 32LL * 1024 * 1024) return {};
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
            return image.save(&buffer, "PNG") ? bytes : QByteArray();
        }
#endif
        return m ? m->data(mime) : QByteArray();
    }
    void publish(const QStringList& formats, Reader reader, int = 1) override {
#ifdef Q_OS_WIN
        // Releasing an old remote offer must not erase a newer clipboard owner.
        if (formats.isEmpty()) {
            if (!owned) return;
            owned->revoke();
            if (OleIsCurrentClipboard(owned) == S_OK) OleSetClipboard(nullptr);
            owned->Release();
            owned = nullptr;
            ++generation;
            return;
        }
        auto next = formats.isEmpty() ? nullptr : new DeskPortWindowsClipboard::Data(formats, std::move(reader));
        const auto result = OleSetClipboard(next);
        if (SUCCEEDED(result)) {
            if (owned) { owned->revoke(); owned->Release(); }
            owned = next;
            ++generation;
        } else if (next) next->Release();
#else
        auto m = new LazyMime; m->types = formats; m->reader = std::move(reader);
        QGuiApplication::clipboard()->setMimeData(m);
#endif
    }
};
}
std::unique_ptr<ClipboardNative> makeClipboardNative() {
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa") return makeMacClipboardNative();
#endif
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && QGuiApplication::platformName() != "offscreen")
        return makeWaylandClipboardNative();
#endif
    return std::unique_ptr<ClipboardNative>(new QtClipboard);
}
