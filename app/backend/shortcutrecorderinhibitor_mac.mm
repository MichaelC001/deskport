#include "shortcutrecorderinhibitor.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <array>

namespace {
int qtKey(NSEvent* event) {
    NSString* text = [event charactersIgnoringModifiers];
    if (![text length]) return Qt::Key_unknown;
    const unichar character = [text characterAtIndex:0];
    if (character >= NSF1FunctionKey && character <= NSF35FunctionKey)
        return Qt::Key_F1 + character - NSF1FunctionKey;
    switch (character) {
    case '\r': return Qt::Key_Return;
    case '\t': return Qt::Key_Tab;
    case 0x19: return Qt::Key_Backtab;
    case 0x1b: return Qt::Key_Escape;
    case 0x7f: return Qt::Key_Backspace;
    case NSDeleteFunctionKey: return Qt::Key_Delete;
    case NSInsertFunctionKey: return Qt::Key_Insert;
    case NSHomeFunctionKey: return Qt::Key_Home;
    case NSEndFunctionKey: return Qt::Key_End;
    case NSPageUpFunctionKey: return Qt::Key_PageUp;
    case NSPageDownFunctionKey: return Qt::Key_PageDown;
    case NSLeftArrowFunctionKey: return Qt::Key_Left;
    case NSRightArrowFunctionKey: return Qt::Key_Right;
    case NSUpArrowFunctionKey: return Qt::Key_Up;
    case NSDownArrowFunctionKey: return Qt::Key_Down;
    default: return QChar(character).toUpper().unicode();
    }
}

class MacShortcutInhibition final : public QObject, public ShortcutRecorderInhibitionBackend {
public:
    ~MacShortcutInhibition() override { stop(); }
    bool start(QWindow* window) override {
        if (m_Tap) return m_Window == window;
        if (QGuiApplication::platformName() != "cocoa" || !window || !window->handle()) return false;
        m_Window = window;
        if (!foreground()) { m_Window = nullptr; return false; }
        // A default (non-listening) tap requires the operator's existing
        // Accessibility permission. Never prompt or modify TCC here. Modifiers
        // keep their native Cocoa flagsChanged path, including left/right keys.
        const CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
        m_Tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                                kCGEventTapOptionDefault, mask, eventTap, this);
        if (!m_Tap) { m_Window = nullptr; return false; }
        m_Source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_Tap, 0);
        if (!m_Source) { stop(); return false; }
        CFRunLoopAddSource(CFRunLoopGetMain(), m_Source, kCFRunLoopCommonModes);
        return true;
    }
    void stop() override {
        ++m_Generation;
        if (m_Source) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), m_Source, kCFRunLoopCommonModes);
            CFRelease(m_Source); m_Source = nullptr;
        }
        if (m_Tap) { CFMachPortInvalidate(m_Tap); CFRelease(m_Tap); m_Tap = nullptr; }
        m_Window = nullptr;
        m_Down.fill(false);
    }
    bool isActive() const override { return m_Tap && CGEventTapIsEnabled(m_Tap) && foreground(); }
private:
    bool foreground() const {
        if (!m_Window || !m_Window->isVisible() || !m_Window->handle() || ![NSApp isActive]) return false;
        NSView* view = reinterpret_cast<NSView*>(m_Window->winId());
        return view && [[view window] isKeyWindow];
    }
    static CGEventRef eventTap(CGEventTapProxy, CGEventType type, CGEventRef event, void* context) {
        auto self = static_cast<MacShortcutInhibition*>(context);
        // A revoked/timeout-disabled tap stays revoked until normal focus or
        // capture lifecycle releases it. Do not force-enable OS-disabled taps.
        if ((type != kCGEventKeyDown && type != kCGEventKeyUp) || !self->isActive()) return event;
        const auto code = quint32(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
        if (code >= self->m_Down.size()) return event;
        const bool down = type == kCGEventKeyDown;
        const bool repeat = down && self->m_Down[code];
        self->m_Down[code] = down;
        const auto flags = CGEventGetFlags(event);
        Qt::KeyboardModifiers mods;
        // Qt's Cocoa convention swaps physical Control and Command; the canvas
        // normalizes these once through ShortcutBinding::eventModifiers.
        if (flags & kCGEventFlagMaskCommand) mods |= Qt::ControlModifier;
        if (flags & kCGEventFlagMaskControl) mods |= Qt::MetaModifier;
        if (flags & kCGEventFlagMaskAlternate) mods |= Qt::AltModifier;
        if (flags & kCGEventFlagMaskShift) mods |= Qt::ShiftModifier;
        const int symbol = qtKey([NSEvent eventWithCGEvent:event]);
        const auto generation = self->m_Generation;
        QMetaObject::invokeMethod(self, [self, generation, code, symbol, mods, down, repeat] {
            if (generation != self->m_Generation || !self->isActive()) return;
            QKeyEvent key(down ? QEvent::KeyPress : QEvent::KeyRelease, symbol, mods,
                          0, code, 0, QString(), repeat);
            QCoreApplication::sendEvent(self->m_Window, &key);
        }, Qt::QueuedConnection);
        return nullptr;
    }
    QPointer<QWindow> m_Window;
    CFMachPortRef m_Tap = nullptr;
    CFRunLoopSourceRef m_Source = nullptr;
    std::array<bool, 128> m_Down {};
    quint64 m_Generation = 0;
};
}

std::unique_ptr<ShortcutRecorderInhibitionBackend> createShortcutRecorderInhibitionBackend() {
    return std::make_unique<MacShortcutInhibition>();
}
