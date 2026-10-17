#pragma once
#include <QByteArray>
#include <QEventLoop>
#include <QSocketNotifier>
#include <QTimer>
#include <functional>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace ClipboardPipe {
using Current = std::function<bool()>;
// The caller owns fd. A null result denotes failure, including stale EOF.
inline QByteArray read(int fd, const Current& current) {
    if (fd < 0 || !current()) return {};
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    QEventLoop loop; QByteArray bytes(""); bool complete = false;
    QSocketNotifier input(fd, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &loop, [&] {
        char chunk[65536]; ssize_t n;
        while ((n = ::read(fd, chunk, sizeof(chunk))) > 0) {
            bytes.append(chunk, n);
            if (bytes.size() > 128 * 1024 * 1024 || !current()) { loop.quit(); return; }
        }
        if (n == 0) { complete = current(); loop.quit(); }
        else if (errno != EAGAIN && errno != EINTR) loop.quit();
    });
    QTimer cancelled, deadline;
    QObject::connect(&cancelled, &QTimer::timeout, &loop, [&] { if (!current()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    cancelled.start(20); deadline.setSingleShot(true); deadline.start(30000);
    loop.exec(); input.setEnabled(false);
    return complete && current() ? bytes : QByteArray();
}
// Own fd, report actual pipe completion (not just successful scheduling).
class Writer final : public QObject {
    int fd;
    QByteArray bytes;
    qsizetype offset = 0;
    QSocketNotifier output;
    QTimer deadline, cancelled;
    Current current;
    std::function<void(bool)> done;
    bool finished = false;
    void finish(bool success) {
        if (finished) return;
        finished = true; output.setEnabled(false); ::close(fd); fd = -1;
        if (done) done(success);
        deleteLater();
    }
public:
    Writer(int descriptor, QByteArray data, Current active, QObject* parent,
           std::function<void(bool)> completion = {})
        : QObject(parent), fd(descriptor), bytes(std::move(data)), output(fd, QSocketNotifier::Write),
          current(std::move(active)), done(std::move(completion)) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        connect(&output, &QSocketNotifier::activated, this, [this] {
            if (!current() || bytes.isNull()) { finish(false); return; }
            while (offset < bytes.size()) {
                const auto n = ::write(fd, bytes.constData() + offset, bytes.size() - offset);
                if (n > 0) offset += n;
                else if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
                else { finish(false); return; }
            }
            finish(current());
        });
        connect(&deadline, &QTimer::timeout, this, [this] { finish(false); });
        connect(&cancelled, &QTimer::timeout, this, [this] { if (!current()) finish(false); });
        deadline.setSingleShot(true); deadline.start(30000); cancelled.start(20);
    }
    ~Writer() override { if (fd >= 0) ::close(fd); }
};
}
