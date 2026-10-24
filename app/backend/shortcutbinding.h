#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

// Local desktop shortcut configuration, shared by the native registration and
// SDL adapters. Meta always means Command/Super/Windows, even on macOS where
// Qt swaps Control and Meta for its application-menu shortcut conventions.
struct ShortcutBinding {
    int key = 0;
    Qt::KeyboardModifiers modifiers{};

    static QList<int> supportedKeys() {
        static const QList<int> keys = [] {
            QList<int> result;
            for (int key = Qt::Key_A; key <= Qt::Key_Z; ++key) result << key;
            for (int key = Qt::Key_0; key <= Qt::Key_9; ++key) result << key;
            // Carbon defines physical virtual keys only through F20. Do not
            // offer F21-F24 on macOS and then fail to register them later.
#ifdef Q_OS_MACOS
            constexpr int lastFunctionKey = Qt::Key_F20;
#else
            constexpr int lastFunctionKey = Qt::Key_F24;
#endif
            for (int key = Qt::Key_F1; key <= lastFunctionKey; ++key) result << key;
            result << Qt::Key_Space << Qt::Key_Apostrophe << Qt::Key_Minus << Qt::Key_Equal
                   << Qt::Key_BracketLeft << Qt::Key_BracketRight << Qt::Key_Semicolon
                   << Qt::Key_Comma << Qt::Key_Period << Qt::Key_Slash << Qt::Key_Backslash
                   << Qt::Key_QuoteLeft << Qt::Key_Tab << Qt::Key_Backspace << Qt::Key_Delete;
#ifndef Q_OS_MACOS
            // macOS's similarly positioned Help key has different semantics.
            result << Qt::Key_Insert;
#endif
            result << Qt::Key_Home << Qt::Key_End << Qt::Key_PageUp << Qt::Key_PageDown
                   << Qt::Key_Left << Qt::Key_Right << Qt::Key_Up << Qt::Key_Down
                   << Qt::Key_Escape << Qt::Key_Return;
            return result;
        }();
        return keys;
    }
    bool valid() const {
        const auto allowed = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
        return !(modifiers & ~allowed) &&
            (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
            supportedKeys().contains(key);
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
        // Qt reports Shift+Tab as Backtab; this is a named key alias, not a
        // keyboard-layout-dependent conversion of a shifted printable symbol.
        if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; modifiers |= Qt::ShiftModifier; }
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
        if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde) return char(key);
        return 0;
    }
    QString portalKey() const {
        if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
        switch (key) {
        case Qt::Key_Space: return QStringLiteral("space");
        case Qt::Key_Apostrophe: return QStringLiteral("apostrophe");
        case Qt::Key_Minus: return QStringLiteral("minus");
        case Qt::Key_Equal: return QStringLiteral("equal");
        case Qt::Key_BracketLeft: return QStringLiteral("bracketleft");
        case Qt::Key_BracketRight: return QStringLiteral("bracketright");
        case Qt::Key_Semicolon: return QStringLiteral("semicolon");
        case Qt::Key_Comma: return QStringLiteral("comma");
        case Qt::Key_Period: return QStringLiteral("period");
        case Qt::Key_Slash: return QStringLiteral("slash");
        case Qt::Key_Backslash: return QStringLiteral("backslash");
        case Qt::Key_QuoteLeft: return QStringLiteral("grave");
        case Qt::Key_Tab: return QStringLiteral("Tab");
        case Qt::Key_Backspace: return QStringLiteral("BackSpace");
        case Qt::Key_Delete: return QStringLiteral("Delete");
        case Qt::Key_Insert: return QStringLiteral("Insert");
        case Qt::Key_Home: return QStringLiteral("Home");
        case Qt::Key_End: return QStringLiteral("End");
        case Qt::Key_PageUp: return QStringLiteral("Prior");
        case Qt::Key_PageDown: return QStringLiteral("Next");
        case Qt::Key_Left: return QStringLiteral("Left");
        case Qt::Key_Right: return QStringLiteral("Right");
        case Qt::Key_Up: return QStringLiteral("Up");
        case Qt::Key_Down: return QStringLiteral("Down");
        case Qt::Key_Escape: return QStringLiteral("Escape");
        case Qt::Key_Return: return QStringLiteral("Return");
        default: return character() ? QString(QChar::fromLatin1(character())) : QString();
        }
    }
    QString portalTrigger() const {
        QStringList parts;
        if (modifiers & Qt::ControlModifier) parts << QStringLiteral("CTRL");
        if (modifiers & Qt::AltModifier) parts << QStringLiteral("ALT");
        if (modifiers & Qt::ShiftModifier) parts << QStringLiteral("SHIFT");
        if (modifiers & Qt::MetaModifier) parts << QStringLiteral("LOGO");
        parts << portalKey();
        return valid() ? parts.join('+') : QString();
    }
};
