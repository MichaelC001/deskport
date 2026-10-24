#pragma once

#include <QKeySequence>
#include <QString>
#include <QStringList>

// Local desktop shortcut configuration, shared by the native registration and
// SDL adapters. Meta always means Command/Super/Windows, even on macOS where
// Qt swaps Control and Meta for its application-menu shortcut conventions.
struct ShortcutBinding {
    int key = 0;
    Qt::KeyboardModifiers modifiers{};

    bool valid() const {
        const auto allowed = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
        return !(modifiers & ~allowed) &&
            (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
            ((key >= Qt::Key_A && key <= Qt::Key_Z) ||
             (key >= Qt::Key_0 && key <= Qt::Key_9) ||
             (key >= Qt::Key_F1 && key <= Qt::Key_F12) ||
             key == Qt::Key_Space || key == Qt::Key_Apostrophe);
    }
    static QString defaultSequence() { return QStringLiteral("Meta+'"); }
    static ShortcutBinding parse(const QString& value) {
        const auto sequence = QKeySequence::fromString(value.trimmed(), QKeySequence::PortableText);
        if (sequence.count() != 1) return {};
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const int combined = sequence[0].toCombined();
#else
        const int combined = sequence[0];
#endif
        ShortcutBinding binding{combined & ~int(Qt::KeyboardModifierMask),
            Qt::KeyboardModifiers(combined & int(Qt::KeyboardModifierMask))};
        return binding.valid() ? binding : ShortcutBinding{};
    }
    QString portable() const {
        return valid() ? QKeySequence(int(modifiers) | key).toString(QKeySequence::PortableText) : QString();
    }
    static Qt::KeyboardModifiers eventModifiers(Qt::KeyboardModifiers value) {
#ifdef Q_OS_MACOS
        const bool control = value.testFlag(Qt::ControlModifier);
        const bool meta = value.testFlag(Qt::MetaModifier);
        value &= ~(Qt::ControlModifier | Qt::MetaModifier);
        if (control) value |= Qt::MetaModifier;
        if (meta) value |= Qt::ControlModifier;
#endif
        return value;
    }
    static QString fromEvent(int key, int modifiers) {
        return ShortcutBinding{key, eventModifiers(Qt::KeyboardModifiers(modifiers))}.portable();
    }
    static QString display(const QString& value) {
        const auto binding = parse(value);
        return binding.valid() ? QKeySequence(int(eventModifiers(binding.modifiers)) | binding.key)
            .toString(QKeySequence::NativeText) : QString();
    }
    // The unmodified layout symbol, matching SDL key symbols. Function keys are
    // handled explicitly by each platform adapter.
    char character() const {
        if (key >= Qt::Key_A && key <= Qt::Key_Z) return char('a' + key - Qt::Key_A);
        if (key >= Qt::Key_0 && key <= Qt::Key_9) return char('0' + key - Qt::Key_0);
        if (key == Qt::Key_Space) return ' ';
        if (key == Qt::Key_Apostrophe) return '\'';
        return 0;
    }
    QString portalTrigger() const {
        QStringList parts;
        if (modifiers & Qt::ControlModifier) parts << QStringLiteral("CTRL");
        if (modifiers & Qt::AltModifier) parts << QStringLiteral("ALT");
        if (modifiers & Qt::ShiftModifier) parts << QStringLiteral("SHIFT");
        if (modifiers & Qt::MetaModifier) parts << QStringLiteral("LOGO");
        parts << (key >= Qt::Key_F1 && key <= Qt::Key_F12 ? QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1) :
                  key == Qt::Key_Apostrophe ? QStringLiteral("apostrophe") :
                  key == Qt::Key_Space ? QStringLiteral("space") : QString(QChar::fromLatin1(character())));
        return valid() ? parts.join('+') : QString();
    }
};
