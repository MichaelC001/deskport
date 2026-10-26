#include "backend/shortcutrecorderinhibitor.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTest>
#include <qt_windows.h>

struct Key {
    int key;
    quint32 scan;
    Qt::KeyboardModifiers modifiers;
    bool down, repeat;
};
class InputWindow final : public QWindow {
public:
    using QWindow::QWindow;
    QList<Key> keys;
    int nativeKeys = 0;
    bool event(QEvent* event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto key = static_cast<QKeyEvent*>(event);
            keys.append({key->key(), key->nativeScanCode(), key->modifiers(),
                         event->type() == QEvent::KeyPress, key->isAutoRepeat()});
            return true;
        }
        return QWindow::event(event);
    }
    bool nativeEvent(const QByteArray&, void* message, qintptr*) override {
        auto msg = static_cast<MSG*>(message);
        if (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP ||
            msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP) ++nativeKeys;
        return false;
    }
};

class WindowsCaptureTest : public QObject {
    Q_OBJECT
    InputWindow m_Window, m_Other;
    ShortcutRecorderInhibitor m_Capture;
    HWND m_Previous = nullptr;
    QList<WORD> m_Held;

    bool inject(WORD scan, bool down, InputWindow& target) {
        // Never inject into a personal window, even if the user changes focus
        // during the fixture. All foreground windows belong to this process.
        if (GetForegroundWindow() != GetAncestor(reinterpret_cast<HWND>(target.winId()), GA_ROOT)) return false;
        INPUT input {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scan & 0xff;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | ((scan & 0x100) ? KEYEVENTF_EXTENDEDKEY : 0) |
                           (down ? 0 : KEYEVENTF_KEYUP);
        if (SendInput(1, &input, sizeof(input)) != 1) return false;
        if (down && !m_Held.contains(scan)) m_Held.append(scan);
        else if (!down) m_Held.removeAll(scan);
        QTest::qWait(35);
        return true;
    }
private slots:
    void initTestCase() {
        m_Previous = GetForegroundWindow();
        m_Window.setTitle("DeskPort isolated Windows keyboard regression");
        m_Window.resize(480, 240);
        m_Other.setTitle("DeskPort local keyboard control fixture");
        m_Other.resize(400, 200);
        m_Window.show(); m_Window.requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(m_Window.isActive(), 3000);
        m_Capture.setWindow(&m_Window);
    }
    void init() {
        m_Window.show(); m_Window.requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(m_Window.isActive(), 3000);
        m_Capture.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(m_Capture.isInhibited(), 3000);
        m_Window.keys.clear(); m_Window.nativeKeys = 0;
    }
    void cleanup() {
        // Release only this fixture's synthetic downs while its window is active.
        if (!m_Held.isEmpty()) {
            m_Window.show(); m_Window.requestActivate();
            QTest::qWait(100);
            for (WORD scan : QList<WORD>(m_Held)) QVERIFY(inject(scan, false, m_Window));
        }
        m_Capture.setEnabled(false);
        QVERIFY(!m_Capture.isInhibited());
    }
    void winSpace_data() {
        QTest::addColumn<int>("win");
        QTest::addColumn<bool>("releaseWinFirst");
        QTest::newRow("left") << 0x15b << false;
        QTest::newRow("right") << 0x15c << false;
        QTest::newRow("left-release-first") << 0x15b << true;
        QTest::newRow("right-release-first") << 0x15c << true;
    }
    void winSpace() {
        QFETCH(int, win); QFETCH(bool, releaseWinFirst);
        QVERIFY(inject(win, true, m_Window));
        QVERIFY(inject(0x39, true, m_Window));
        QVERIFY(inject(0x39, true, m_Window)); // Native repeat must remain a repeat.
        QVERIFY(inject(releaseWinFirst ? win : 0x39, false, m_Window));
        QVERIFY(inject(releaseWinFirst ? 0x39 : win, false, m_Window));
        QCOMPARE(m_Window.keys.size(), 5);
        QCOMPARE(m_Window.keys[0].key, int(Qt::Key_Meta));
        QCOMPARE(m_Window.keys[0].scan, quint32(win));
        QCOMPARE(m_Window.keys[1].key, int(Qt::Key_Space));
        QCOMPARE(m_Window.keys[1].scan, quint32(0x39));
        QVERIFY(m_Window.keys[1].modifiers & Qt::MetaModifier);
        QVERIFY(m_Window.keys[2].repeat);
        QVERIFY(!m_Window.keys[3].down && !m_Window.keys[4].down);
        QCOMPARE(m_Window.nativeKeys, 0); // OS must not also deliver the chord.
        QCOMPARE(GetForegroundWindow(), reinterpret_cast<HWND>(m_Window.winId()));
    }
    void bothWindowsKeysAndReleaseShortcut() {
        QVERIFY(inject(0x15b, true, m_Window));
        QVERIFY(inject(0x15c, true, m_Window));
        QVERIFY(inject(0x15b, false, m_Window));
        QVERIFY(inject(0x39, true, m_Window));
        QVERIFY(m_Window.keys.last().modifiers & Qt::MetaModifier);
        QVERIFY(inject(0x39, false, m_Window));
        QVERIFY(inject(0x15c, false, m_Window));
        QVERIFY(inject(0x1d, true, m_Window));
        QVERIFY(inject(0x38, true, m_Window));
        QVERIFY(inject(0x2a, true, m_Window));
        QVERIFY(inject(0x2c, true, m_Window));
        QCOMPARE(m_Window.keys.last().key, int(Qt::Key_Z));
        QCOMPARE(m_Window.keys.last().modifiers,
                 Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        for (WORD scan : {0x2c, 0x2a, 0x38, 0x1d}) QVERIFY(inject(scan, false, m_Window));
        QCOMPARE(m_Window.nativeKeys, 0);
    }
    void releaseAndFocusLossRestoreLocalInput() {
        m_Capture.setEnabled(false);
        QVERIFY(inject(0x39, true, m_Window));
        QVERIFY(inject(0x39, false, m_Window));
        QVERIFY(m_Window.nativeKeys >= 2);
        m_Capture.setEnabled(true);
        QTRY_VERIFY(m_Capture.isInhibited());
        m_Other.show(); m_Other.requestActivate();
        QTRY_VERIFY(m_Other.isActive());
        QTRY_VERIFY(!m_Capture.isInhibited());
        m_Other.nativeKeys = 0;
        QVERIFY(inject(0x39, true, m_Other));
        QVERIFY(inject(0x39, false, m_Other));
        QVERIFY(m_Other.nativeKeys >= 2);
        m_Window.requestActivate();
        QTRY_VERIFY(m_Window.isActive());
        QTRY_VERIFY(m_Capture.isInhibited());
        m_Window.hide();
        QTRY_VERIFY(!m_Capture.isInhibited());
    }
    void embeddedCanvasSurfaceCaptures() {
        m_Capture.setWindow(nullptr);
        InputWindow child(&m_Window);
        child.setFlags(Qt::FramelessWindowHint);
        child.resize(300, 180); child.show(); child.requestActivate();
        QTRY_VERIFY(child.isActive());
        m_Capture.setWindow(&child);
        QTRY_VERIFY(m_Capture.isInhibited());
        QVERIFY(inject(0x15c, true, child));
        QVERIFY(inject(0x39, true, child));
        QVERIFY(inject(0x39, false, child));
        QVERIFY(inject(0x15c, false, child));
        QCOMPARE(child.keys.size(), 4);
        QCOMPARE(child.keys[0].scan, quint32(0x15c));
        QVERIFY(child.keys[1].modifiers & Qt::MetaModifier);
        QCOMPARE(child.nativeKeys, 0);
        m_Capture.setWindow(&m_Window);
    }
    void focusLossWhileWinHeldDoesNotLatch() {
        QVERIFY(inject(0x15b, true, m_Window));
        m_Other.show(); m_Other.requestActivate();
        QTRY_VERIFY(m_Other.isActive());
        QTRY_VERIFY(!m_Capture.isInhibited());
        QVERIFY(inject(0x15b, false, m_Other));
        QCOMPARE(GetForegroundWindow(), reinterpret_cast<HWND>(m_Other.winId()));
        m_Window.requestActivate();
        QTRY_VERIFY(m_Window.isActive());
        QTRY_VERIFY(m_Capture.isInhibited());
        m_Window.keys.clear();
        QVERIFY(inject(0x39, true, m_Window));
        QVERIFY(inject(0x39, false, m_Window));
        QCOMPARE(m_Window.keys.size(), 2);
        QVERIFY(!(m_Window.keys[0].modifiers & Qt::MetaModifier));
    }
    void cleanupTestCase() {
        m_Capture.setWindow(nullptr);
        m_Window.close(); m_Other.close();
        if (m_Previous && IsWindow(m_Previous)) SetForegroundWindow(m_Previous);
    }
};
QTEST_MAIN(WindowsCaptureTest)
#include "windows-shortcut-capture.moc"
