#include "streaming/windowactivation.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QThread>
#include <QWindow>
#include <cstdio>
#include <stdexcept>
#include <X11/Xlib.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void pump(int milliseconds) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
}
Window inputFocus(Display* display) {
    Window focus = None;
    int revert = RevertToNone;
    require(XGetInputFocus(display, &focus, &revert) != 0, "XGetInputFocus failed");
    return focus;
}
struct Cleanup {
    Display* display = nullptr;
    QProcess blank;
    ~Cleanup() {
        if (display) {
            XUngrabKeyboard(display, CurrentTime);
            XSync(display, False);
            XCloseDisplay(display);
        }
        if (blank.state() != QProcess::NotRunning) {
            blank.terminate();
            if (!blank.waitForFinished(2000)) {
                blank.kill();
                blank.waitForFinished(2000);
            }
        }
    }
};
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    require(qEnvironmentVariable("DESKPORT_TEST_PRIVATE_X11") == "1", "Private Xvfb marker required");
    require(QGuiApplication::platformName() == "xcb", "XCB backend required");
    QWindow window;
    window.setTitle(QStringLiteral("DeskPort private X11 activation fixture"));
    window.resize(400, 300);
    window.show();
    if (app.arguments().contains(QStringLiteral("--blank"))) {
        std::printf("%llu\n", static_cast<unsigned long long>(window.winId()));
        std::fflush(stdout);
        return app.exec();
    }

    Cleanup cleanup;
    QJsonArray checks;
    try {
        cleanup.display = XOpenDisplay(nullptr);
        require(cleanup.display, "Private X display unavailable");
        cleanup.blank.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--blank")});
        require(cleanup.blank.waitForStarted(5000) && cleanup.blank.waitForReadyRead(5000), "Blank fixture did not start");
        bool valid = false;
        const auto blank = static_cast<Window>(cleanup.blank.readLine().trimmed().toULongLong(&valid));
        require(valid && blank != None, "Blank fixture window ID missing");
        pump(250);
        const auto target = static_cast<Window>(window.winId());
        const auto focus = [&](Window id) {
            XSetInputFocus(cleanup.display, id, RevertToPointerRoot, CurrentTime);
            XSync(cleanup.display, False);
            pump(250);
            require(inputFocus(cleanup.display) == id, "Native focus did not reach owned window");
        };
        const auto check = [&](const char* stage, bool qt, bool native, bool helper) {
            const bool qtActive = window.isActive();
            const bool nativeActive = inputFocus(cleanup.display) == target;
            const bool helperActive = WindowActivation::shortcutWindowIsActive(&window);
            checks.append(QJsonObject{{"stage", QString::fromLatin1(stage)}, {"qt_active", qtActive},
                                      {"native_active", nativeActive}, {"helper_active", helperActive}});
            require(qtActive == qt && nativeActive == native && helperActive == helper, stage);
        };
        const auto grab = [&] {
            require(XGrabKeyboard(cleanup.display, DefaultRootWindow(cleanup.display), False,
                                  GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess, "Private keyboard grab failed");
            XSync(cleanup.display, False);
            // Qt's XCB focus-loss timer expires after 100 ms. No input is sent.
            pump(250);
        };
        const auto ungrab = [&] {
            XUngrabKeyboard(cleanup.display, CurrentTime);
            XSync(cleanup.display, False);
            pump(250);
        };
        require(!WindowActivation::shortcutWindowIsActive(nullptr), "Null window must be inactive");
        focus(target);
        check("foreground", true, true, true);
        grab();
        check("foreground-long-grab", false, true, true);
        ungrab();
        check("foreground-ungrab", true, true, true);
        focus(blank);
        check("background", false, false, false);
        grab();
        check("background-long-grab", false, false, false);
        ungrab();
        check("background-ungrab", false, false, false);
        focus(target);
        check("foreground-restored", true, true, true);
        std::puts(QJsonDocument(QJsonObject{{"status", "passed"}, {"input_injected", false},
                                           {"checks", checks}}).toJson(QJsonDocument::Compact).constData());
        return 0;
    } catch (const std::exception& error) {
        std::puts(QJsonDocument(QJsonObject{{"status", "failed"}, {"error", QString::fromUtf8(error.what())},
                                           {"checks", checks}}).toJson(QJsonDocument::Compact).constData());
        return 1;
    }
}
