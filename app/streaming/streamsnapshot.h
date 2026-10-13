#pragma once
#include <QImage>
#include <QVector>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>
#include <atomic>
#include <algorithm>

// A small, blurred picture of the recent stream. A reconnect shows it behind its
// progress indicator instead of switching abruptly to a flat background.
class StreamSnapshot {
public:
    static constexpr quint32 IntervalMs = 3000;
    // Asks the renderer for a newer picture, e.g. when a resize begins.
    static void request() { requested().store(true); }
    // Whether a frame rendered at now should be captured.
    static bool due(quint32 now) {
        return requested().exchange(false) || !lastCapture().load() || quint32(now - lastCapture().load()) >= IntervalMs;
    }
    static void captured(quint32 now) { lastCapture().store(now ? now : 1); }
    static void store(QImage image) {
        if (image.isNull()) return;
        blur(image);
        QMutexLocker lock(&mutex()); picture() = std::move(image);
    }
    static QImage take() {
        QMutexLocker lock(&mutex()); QImage image = std::move(picture()); picture() = QImage();
        lastCapture().store(0);
        return image;
    }
    static void clear() { take(); }
    // Three box passes approximate a Gaussian blur on the small picture.
    static void blur(QImage& image) {
        image = image.convertToFormat(QImage::Format_RGB32);
        const int radius = std::max(2, image.width() / 48);
        for (int pass = 0; pass < 3; ++pass) {
            boxBlur(image, radius, true);
            boxBlur(image, radius, false);
        }
    }
private:
    static void boxBlur(QImage& image, int radius, bool horizontal) {
        const int w = image.width(), h = image.height();
        const int lines = horizontal ? h : w, length = horizontal ? w : h;
        QVector<QRgb> source(length);
        for (int line = 0; line < lines; ++line) {
            auto at = [&](int i) -> QRgb& {
                return reinterpret_cast<QRgb*>(image.scanLine(horizontal ? line : i))[horizontal ? i : line];
            };
            for (int i = 0; i < length; ++i) source[i] = at(i);
            int r = 0, g = 0, b = 0;
            const int span = 2 * radius + 1;
            for (int i = -radius; i <= radius; ++i) {
                const QRgb p = source[std::clamp(i, 0, length - 1)];
                r += qRed(p); g += qGreen(p); b += qBlue(p);
            }
            for (int i = 0; i < length; ++i) {
                at(i) = qRgb(r / span, g / span, b / span);
                const QRgb out = source[std::clamp(i - radius, 0, length - 1)];
                const QRgb in = source[std::clamp(i + radius + 1, 0, length - 1)];
                r += qRed(in) - qRed(out); g += qGreen(in) - qGreen(out); b += qBlue(in) - qBlue(out);
            }
        }
    }
    static QMutex& mutex() { static QMutex value; return value; }
    static QImage& picture() { static QImage value; return value; }
    static std::atomic<bool>& requested() { static std::atomic<bool> value{false}; return value; }
    static std::atomic<quint32>& lastCapture() { static std::atomic<quint32> value{0}; return value; }
};
