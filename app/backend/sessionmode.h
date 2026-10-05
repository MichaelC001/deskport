#pragma once

#include <QString>

namespace DeskPortSession {

enum class Mode {
    Desktop,
    Seamless,
};

inline QString name(Mode mode) {
    return mode == Mode::Seamless ? QStringLiteral("seamless") : QStringLiteral("desktop");
}

inline bool parse(const QString& value, Mode* mode) {
    if (value == QStringLiteral("desktop")) {
        if (mode) *mode = Mode::Desktop;
        return true;
    }
    if (value == QStringLiteral("seamless")) {
        if (mode) *mode = Mode::Seamless;
        return true;
    }
    return false;
}

}
