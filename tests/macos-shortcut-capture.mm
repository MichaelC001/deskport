#include "backend/shortcutrecorderinhibitor.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTest>
#import <AppKit/AppKit.h>
#include <Carbon/Carbon.h>

class InputWindow final : public QWindow {
public:
    using QWindow::QWindow;
    struct Key { int key; quint32 native; Qt::KeyboardModifiers mods; bool down; };
    QList<Key> keys;
    bool event(QEvent* event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto key = static_cast<QKeyEvent*>(event);
            keys.append({key->key(), key->nativeVirtualKey(), key->modifiers(),
                         event->type() == QEvent::KeyPress});
            return true;
        }
        return QWindow::event(event);
    }
};

class MacCaptureTest : public QObject {
    Q_OBJECT
    InputWindow m_Window, m_Other;
    ShortcutRecorderInhibitor m_Capture;
    NSRunningApplication* m_Previous = nil;
    QString inputSource() {
        auto source = TISCopyCurrentKeyboardInputSource();
        auto id = static_cast<CFStringRef>(TISGetInputSourceProperty(source, kTISPropertyInputSourceID));
        const QString result = QString::fromCFString(id);
        CFRelease(source);
        return result;
    }
    bool inject(CGKeyCode code, bool down, CGEventFlags flags, QWindow& target) {
        auto view = reinterpret_cast<NSView*>(target.winId());
        if (![NSApp isActive] || ![[view window] isKeyWindow]) return false;
        auto event = CGEventCreateKeyboardEvent(nullptr, code, down);
        CGEventSetFlags(event, flags);
        if (code == kVK_Command || code == kVK_RightCommand)
            CGEventSetType(event, kCGEventFlagsChanged);
        CGEventPost(kCGHIDEventTap, event); CFRelease(event);
        QTest::qWait(150);
        return true;
    }
private slots:
    void initTestCase() {
        m_Previous = [[NSWorkspace sharedWorkspace] frontmostApplication];
        [m_Previous retain];
        m_Window.setTitle("DeskPort isolated macOS keyboard regression");
        m_Window.resize(480, 240); m_Other.resize(400, 200);
        m_Window.show();
        [NSApp activateIgnoringOtherApps:YES];
        m_Window.requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(m_Window.isActive(), 3000);
        m_Capture.setWindow(&m_Window);
    }
    void init() {
        m_Window.show(); m_Window.requestActivate();
        QTRY_VERIFY(m_Window.isActive());
        m_Capture.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(m_Capture.isInhibited(), 3000);
        m_Window.keys.clear();
    }
    void cleanup() { m_Capture.setEnabled(false); QVERIFY(!m_Capture.isInhibited()); }
    void commandSpace_data() {
        QTest::addColumn<int>("command");
        QTest::newRow("left") << int(kVK_Command);
        QTest::newRow("right") << int(kVK_RightCommand);
    }
    void commandSpace() {
        QFETCH(int, command);
        const auto before = inputSource();
        QVERIFY(inject(command, true, kCGEventFlagMaskCommand, m_Window));
        QVERIFY(inject(kVK_Space, true, kCGEventFlagMaskCommand, m_Window));
        QVERIFY(inject(kVK_Space, false, kCGEventFlagMaskCommand, m_Window));
        QVERIFY(inject(command, false, 0, m_Window));
        QList<InputWindow::Key> spaces;
        for (const auto& key : m_Window.keys) if (key.key == Qt::Key_Space) spaces.append(key);
        QCOMPARE(spaces.size(), 2);
        QVERIFY(spaces[0].down && !spaces[1].down);
        QCOMPARE(spaces[0].native, quint32(kVK_Space));
        QVERIFY(spaces[0].mods & Qt::ControlModifier); // Physical Command on Cocoa.
        QCOMPARE(inputSource(), before);
        QVERIFY(m_Window.isActive());
    }
    void embeddedSurfaceAndRelease() {
        m_Capture.setWindow(nullptr);
        InputWindow child(&m_Window);
        child.setFlags(Qt::FramelessWindowHint); child.resize(300, 180);
        child.show(); child.requestActivate();
        QTRY_VERIFY(child.isActive());
        m_Capture.setWindow(&child);
        QTRY_VERIFY(m_Capture.isInhibited());
        QVERIFY(inject(kVK_Command, true, kCGEventFlagMaskCommand, child));
        QVERIFY(inject(kVK_Space, true, kCGEventFlagMaskCommand, child));
        QVERIFY(inject(kVK_Space, false, kCGEventFlagMaskCommand, child));
        QVERIFY(inject(kVK_Command, false, 0, child));
        QList<InputWindow::Key> spaces;
        for (const auto& key : child.keys) if (key.key == Qt::Key_Space) spaces.append(key);
        QCOMPARE(spaces.size(), 2);
        QVERIFY(spaces[0].down && !spaces[1].down);
        QVERIFY(spaces[0].mods & Qt::ControlModifier);
        m_Capture.setWindow(&m_Window);
        m_Other.show(); m_Other.requestActivate();
        QTRY_VERIFY(m_Other.isActive());
        QTRY_VERIFY(!m_Capture.isInhibited());
        m_Other.keys.clear();
        QVERIFY(inject(kVK_Space, true, 0, m_Other));
        QVERIFY(inject(kVK_Space, false, 0, m_Other));
        QTRY_COMPARE(m_Other.keys.size(), 2);
        m_Window.requestActivate();
        QTRY_VERIFY(m_Window.isActive());
        QTRY_VERIFY(m_Capture.isInhibited());
        m_Window.hide();
        QTRY_VERIFY(!m_Capture.isInhibited());
    }
    void cleanupTestCase() {
        m_Capture.setWindow(nullptr); m_Window.close(); m_Other.close();
        [m_Previous activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        [m_Previous release];
    }
};
QTEST_MAIN(MacCaptureTest)
#include "macos-shortcut-capture.moc"
