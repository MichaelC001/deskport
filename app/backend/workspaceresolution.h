#pragma once
#include <QSize>
#include <QtGlobal>
#include <cmath>

namespace DeskPortDisplay {
constexpr int MaxWidth = 7680;
constexpr int MaxHeight = 4320;
struct Workspace {
    QSize pixels;
    int scale = 1;
};
// Recover compositor scale when XWayland exposes pixels while Qt exposes the
// logical output rectangle. Reject mismatched outputs/orientations.
inline double scaleForOutput(QSize pixels, QSize logical, double fallback) {
    if (pixels.isEmpty() || logical.isEmpty()) return fallback;
    const double x = double(pixels.width()) / logical.width();
    const double y = double(pixels.height()) / logical.height();
    return x >= 0.5 && x <= 8.0 && std::abs(x - y) < 0.01 ? x : fallback;
}
// Match the client's logical desktop with a native 1x/2x Mac backing store.
// Fractional clients receive supersampled frames, never a smaller desktop raster
// enlarged by the viewer. Above 2x, retain drawable resolution rather than
// undersampling text. The size cap remains the explicit exception.
inline Workspace forClient(QSize drawablePixels, double clientScale) {
    if (drawablePixels.isEmpty() || !std::isfinite(clientScale) || clientScale < 0.5 || clientScale > 8.0) return {};
    const int scale = clientScale > 1.0 ? 2 : 1;
    const double width = drawablePixels.width();
    const double height = drawablePixels.height();
    // WindowServer advertises compact modes which it then rejects. Keep a usable
    // minimum logical desktop, preserving aspect ratio, and bound backing-store memory.
    const double minimum = qMax(qMax(1.0, scale / clientScale),
                                qMax(960.0 * scale / width, 540.0 * scale / height));
    const double maximum = qMin(double(MaxWidth) / width, double(MaxHeight) / height);
    const double factor = qMin(minimum, maximum);
    return {QSize(qMin(MaxWidth, int(std::ceil(width * factor / 4)) * 4),
                  qMin(MaxHeight, int(std::ceil(height * factor / 4)) * 4)), scale};
}
}
