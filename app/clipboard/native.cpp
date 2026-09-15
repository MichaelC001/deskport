#include "native.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QVariant>

namespace {
class LazyMime final : public QMimeData {
public:
    QStringList types;
    ClipboardNative::Reader reader;
    QStringList formats() const override { return types; }
    bool hasFormat(const QString& type) const override { return types.contains(type); }
protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QVariant retrieveData(const QString& mime, QMetaType) const override { return reader(mime); }
#else
    QVariant retrieveData(const QString& mime, QVariant::Type) const override { return reader(mime); }
#endif
};
class QtClipboard final : public ClipboardNative {
    qint64 generation = 0;
    QMetaObject::Connection connection;
public:
    QtClipboard() { connection = QObject::connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, [this] { ++generation; }); }
    ~QtClipboard() override { QObject::disconnect(connection); }
    qint64 revision() override { return generation; }
    QStringList formats() override { auto m = QGuiApplication::clipboard()->mimeData(); return m ? m->formats() : QStringList(); }
    QByteArray read(const QString& mime) override { auto m = QGuiApplication::clipboard()->mimeData(); return m ? m->data(mime) : QByteArray(); }
    void publish(const QStringList& formats, Reader reader, int = 1) override {
        auto m = new LazyMime; m->types = formats; m->reader = std::move(reader);
        QGuiApplication::clipboard()->setMimeData(m);
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
