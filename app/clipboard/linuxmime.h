#pragma once
#include "native.h"

// Linux file managers additionally use the Nautilus copy format. Keep its
// native serialization here; v2 still carries the shared file-copy contract.
class LinuxClipboardMime final : public ClipboardNative {
    std::unique_ptr<ClipboardNative> native;
    static QString uri() { return QStringLiteral("text/uri-list"); }
    static QString gnome() { return QStringLiteral("x-special/gnome-copied-files"); }
public:
    explicit LinuxClipboardMime(std::unique_ptr<ClipboardNative> provider) : native(std::move(provider)) {}
    bool valid() const override { return native->valid(); }
    QString stateMessage() const override { return native->stateMessage(); }
    bool failedRead(const QByteArray& bytes) const override { return native->failedRead(bytes); }
    void cancel() override { native->cancel(); }
    qint64 revision() override { return native->revision(); }
    QStringList formats() override {
        auto types = native->formats();
        if (types.contains(gnome()) && !types.contains(uri())) types.append(uri());
        return types;
    }
    QByteArray read(const QString& mime) override {
        if (mime != uri() || native->formats().contains(uri())) return native->read(mime);
        if (!native->formats().contains(gnome())) return {};
        const auto data = native->read(gnome()); const auto newline = data.indexOf('\n');
        if (newline < 0 || (data.left(newline) != "copy" && data.left(newline) != "cut")) return {};
        // A source cut remains a copy across devices; never delete local files.
        return data.mid(newline + 1);
    }
    void publish(const QStringList& formats, Reader reader, int items = 1) override {
        auto types = formats;
        if (types.contains(uri()) && !types.contains(gnome())) types.append(gnome());
        native->publish(types, [reader](const QString& mime) {
            if (mime != gnome()) return reader ? reader(mime) : QByteArray();
            auto urls = reader ? reader(uri()) : QByteArray();
            if (urls.isEmpty()) return QByteArray();
            urls.replace("\r\n", "\n");
            while (urls.endsWith('\n')) urls.chop(1);
            return QByteArray("copy\n") + urls;
        }, items);
    }
};
