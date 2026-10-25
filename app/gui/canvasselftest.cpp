#include "sessioncanvas.h"
#include "streaming/video/ffmpeg.h"
#include "streaming/video/encodedframe.h"
#include <QApplication>
#include <QQuickWindow>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <cstdio>
#ifdef Q_OS_MACOS
#include "backend/mactitlebar.h"
#endif

// Explicit isolated renderer exercise: no settings, credentials, network,
// clipboard, host service, global shortcut or remote input is initialized.
int runCanvasSelfTest(int argc, char** argv) {
    SDL_SetMainReady();
    QCoreApplication::setApplicationName("DeskPortCanvasTest");
    QApplication app(argc, argv);
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND")) QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
#endif
    QQuickWindow window;
#ifdef Q_OS_MACOS
    window.setFlags(Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
    MacTitleBar titleBar;
    titleBar.attach(&window);
#endif
    window.setTitle("DeskPort isolated canvas test");
    window.setColor(QColor("#101216"));
    window.resize(960, 640);
    SessionPresentation first, second;
    auto canvas = new SessionCanvas(window.contentItem());
    const auto layout = [&] { canvas->setSize(window.size()); };
    QObject::connect(&window, &QWindow::widthChanged, &app, layout);
    QObject::connect(&window, &QWindow::heightChanged, &app, layout);
    layout();
    window.show();
    const WId original = window.winId();
    int result = 1, stage = 0, frameNumber = 1, renderedStages = 0;
    quint64 generation = 1;
    SessionPresentation* selected = &first;
    QElapsedTimer elapsed, stageTime;
    elapsed.start(); stageTime.start();
    const auto configure = [&] {
        return selected->configure({{"generation",double(generation)}, {"format",VIDEO_FORMAT_H264},
            {"width",1280}, {"height",720}, {"fps",60}, {"vsync",false},
            {"decoder",int(StreamingPreferences::VDS_AUTO)}});
    };
    const auto fail = [&](QString error) {
        fprintf(stderr, "Canvas self-test failed: %s\n", qPrintable(error));
        app.quit();
    };
    QObject::connect(&first, &SessionPresentation::failed, &app, fail);
    QObject::connect(&second, &SessionPresentation::failed, &app, fail);
    QTimer feed;
    QObject::connect(&feed, &QTimer::timeout, &app, [&] {
        if (elapsed.elapsed() > 60000) {
            fail(QString("stage %1 timed out: ready=%2 state=%3 size=%4x%5")
                .arg(stage).arg(selected->ready()).arg(int(window.windowState())).arg(window.width()).arg(window.height()));
            return;
        }
        if (window.winId() != original) { fail("top-level native window changed"); return; }
        int visibleRoots = 0;
        for (auto candidate : QGuiApplication::topLevelWindows()) if (candidate->isVisible()) ++visibleRoots;
        if (visibleRoots > 1) { fail("more than one visible top-level window"); return; }
        // SDL only owns hidden capability surfaces in this process.
        for (Uint32 id = 1; id <= 512; ++id) if (auto probe = SDL_GetWindowFromID(id)) {
            if (!(SDL_GetWindowFlags(probe) & SDL_WINDOW_HIDDEN)) { fail("visible SDL viewer window"); return; }
        }
        auto bytes = FFmpegVideoDecoder::sampleFrame();
        LENTRY entry{nullptr, bytes.data(), int(bytes.size()), BUFFER_TYPE_PICDATA};
        DECODE_UNIT unit{};
        unit.frameNumber = frameNumber++; unit.frameType = FRAME_TYPE_IDR;
        unit.bufferList = &entry; unit.fullLength = entry.length;
        unit.receiveTimeMs = unit.enqueueTimeMs = LiGetMillis();
        unit.presentationTimeMs = elapsed.elapsed();
        selected->frame(EncodedVideoFrame::encode(unit, generation));
        if (!selected->ready() || stageTime.elapsed() < 500) return;
        if (stage >= 10) {
            const auto expected = stage == 10 || stage == 14 ? Qt::WindowFullScreen :
                                  stage == 12 ? Qt::WindowMaximized : Qt::WindowNoState;
            if (window.windowState() != expected) return;
#ifdef Q_OS_MACOS
            // Qt can report FullScreen even when AppKit refused the transition.
            // Exercise the production title-bar policy and the native result.
            if (titleBar.isFullscreenTransitioning()) return;
            if (titleBar.isNativeFullscreen() != (expected == Qt::WindowFullScreen)) return;
#endif
        }
        ++renderedStages;
        fprintf(stderr, "Canvas self-test stage %d completed\n", stage);
        if (++stage == 16) {
            const auto capabilities = selected->capabilities();
            canvas->setPresentation(nullptr);
            if (first.active() || second.active()) { fail("decoder survived detach"); return; }
            printf("PASS: one persistent native root, 16 decoded presentation stages, resize, session switch, live hide/minimize/recall, native fullscreen/maximize/restore and decoder teardown; %s\n",
                   QJsonDocument(capabilities).toJson(QJsonDocument::Compact).constData());
            result = 0; app.quit(); return;
        }
        feed.stop();
        if (stage >= 10) {
            // Keep the live decoder attached across real compositor transitions.
            if (stage == 10 || stage == 14) window.showFullScreen();
            else if (stage == 12) window.showMaximized();
            else { window.showNormal(); window.resize(800, 560); }
            stageTime.restart();
            feed.start(50);
            return;
        }
        // Exercise an occluded live renderer before joining it, including the
        // compositor's asynchronous unmap notifications.
        if (stage % 3 == 0) window.hide();
        else if (stage % 3 == 1) window.showMinimized();
        QTimer::singleShot(250, &app, [&] {
            canvas->setPresentation(nullptr);
            window.showNormal();
            window.resize(stage % 2 ? QSize(800, 520) : QSize(1040, 700));
            selected = stage % 2 ? &second : &first;
            ++generation;
            if (!configure()) { fail("configuration rejected"); return; }
            if (stage == 2) {
                // The QML Loader destroys Devices' previous canvas and binds
                // presentation before anchors size the replacement on recall.
                delete canvas;
                canvas = new SessionCanvas(window.contentItem());
                canvas->setPresentation(selected);
                layout();
            }
            canvas->setPresentation(selected);
            stageTime.restart();
            feed.start(50);
        });
    });
    QTimer::singleShot(0, &app, [&] {
        if (!configure()) { fail("initial configuration rejected"); return; }
        canvas->setPresentation(selected);
        feed.start(50);
    });
    app.exec();
    feed.stop();
    canvas->setPresentation(nullptr);
    delete canvas;
    window.close();
    Q_UNUSED(renderedStages);
    return result;
}
