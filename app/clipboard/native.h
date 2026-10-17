#pragma once
#include <QByteArray>
#include <QStringList>
#include <functional>
#include <memory>

// All methods run on the clipboard helper's GUI thread, never the media thread.
class ClipboardNative {
public:
    using Reader = std::function<QByteArray(const QString&)>;
    virtual ~ClipboardNative() = default;
    virtual qint64 revision() = 0;
    virtual QStringList formats() = 0;
    virtual QByteArray read(const QString& mime) = 0;
    // Linux FD backends distinguish a failed null read from successful empty EOF.
    // Existing platform providers retain their original empty-result semantics.
    virtual bool failedRead(const QByteArray&) const { return false; }
    virtual void publish(const QStringList& formats, Reader reader, int items = 1) = 0;
    virtual bool valid() const { return true; }
    virtual QString stateMessage() const { return {}; }
    // Cancel nested reads before helper shutdown; native providers keep callbacks alive.
    virtual void cancel() {}
};
std::unique_ptr<ClipboardNative> makeClipboardNative();
#ifdef Q_OS_MACOS
std::unique_ptr<ClipboardNative> makeMacClipboardNative(const QString& board = {});
#endif
#ifdef Q_OS_LINUX
std::unique_ptr<ClipboardNative> makeWaylandClipboardNative(const QString& protocol = {});
std::unique_ptr<ClipboardNative> makePortalClipboardNative();
#endif
