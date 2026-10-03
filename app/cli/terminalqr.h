#pragma once

#include <QString>
#include "../../third_party/qrcodegen/qrcodegen.hpp"

namespace DeskPortCli {
// Two vertical modules per cell, explicit colors and a four-module quiet zone.
inline QString terminalQr(const QString& text, int* columns = nullptr) {
    const auto utf8 = text.toUtf8();
    const auto qr = qrcodegen::QrCode::encodeText(utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    QString result;
    const int border = 4, size = qr.getSize();
    if (columns) *columns = size + border * 2;
    for (int y = -border; y < size + border; y += 2) {
        result += QStringLiteral("\x1b[30;107m");
        for (int x = -border; x < size + border; ++x) {
            const bool top = qr.getModule(x, y), bottom = qr.getModule(x, y + 1);
            result += top ? (bottom ? QChar(0x2588) : QChar(0x2580)) : (bottom ? QChar(0x2584) : QChar(' '));
        }
        result += QStringLiteral("\x1b[0m\n");
    }
    return result;
}
}
