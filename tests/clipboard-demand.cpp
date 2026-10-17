#include "clipboard/agent.h"
#include <QtTest>
#include <QFile>
#include <QUrl>
#include <QCryptographicHash>
#include <QProcess>
#include <QGuiApplication>
#include <QUuid>
#ifdef Q_OS_LINUX
#include "clipboard/linuxmime.h"
#endif

class MemoryClipboard : public ClipboardNative {
public:
    qint64 generation = 0;
    QStringList types;
    QHash<QString, QByteArray> bytes;
    Reader reader;
    int reads = 0;
    qint64 revision() override { return generation; }
    QStringList formats() override { return types; }
    QByteArray read(const QString& mime) override { ++reads; return reader ? reader(mime) : bytes[mime]; }
    void publish(const QStringList& f, Reader r, int = 1) override { types = f; reader = r; ++generation; }
    void copy(const QString& type, const QByteArray& data) { reader = {}; bytes.clear(); bytes[type] = data; types = {type}; ++generation; }
};
class DemandTest : public QObject {
    Q_OBJECT
private slots:
    void nativeWindowsOwnership() {
#ifdef Q_OS_WIN
        if (qEnvironmentVariable("DESKPORT_TEST_WINDOWS_CLIPBOARD") != "1")
            QSKIP("Requires the authorized isolated Windows clipboard fixture.");
        auto first = makeClipboardNative();
        auto second = makeClipboardNative();
        auto unused = makeClipboardNative();
        first->publish({"text/plain;charset=utf-8"}, [](const QString&) { return QByteArray("deskport-owner-a"); });
        second->publish({"text/plain;charset=utf-8"}, [](const QString&) { return QByteArray("deskport-owner-b"); });
        QCoreApplication::processEvents();
        first->publish({}, {});
        unused->publish({}, {});
        first.reset();
        QCoreApplication::processEvents();
        QCOMPARE(second->read("text/plain"), QByteArray("deskport-owner-b"));
        second->publish({}, {});
        QCoreApplication::processEvents();
        QVERIFY(second->read("text/plain").isEmpty());
#else
        QSKIP("Windows only");
#endif
    }
    void nativeMacProviders() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() != "cocoa") QSKIP("Named pasteboard test runs in the separate Cocoa pass.");
        const auto name = "io.github.keithxc.deskport.test." + QUuid::createUuid().toString();
        auto board = makeMacClipboardNative(name); int reads = 0;
        auto external = [&](const QString& mode, const QString& mime) {
            QProcess reader; reader.start(QCoreApplication::applicationFilePath(), {"--native-reader", name, mode, mime});
            QElapsedTimer elapsed; elapsed.start();
            while (reader.state() != QProcess::NotRunning && elapsed.elapsed() < 10000) QTest::qWait(5);
            if (reader.state() != QProcess::NotRunning) { reader.kill(); reader.waitForFinished(); return QByteArray("TIMEOUT"); }
            return reader.readAllStandardOutput();
        };
        const QByteArray image("synthetic image bytes");
        board->publish({"image/png"}, [&](const QString&) { ++reads; return image; });
        QVERIFY(external("formats", "").contains("image/png")); QCOMPARE(reads, 0);
        QCOMPARE(external("read", "image/png"), image); QCOMPARE(reads, 1);
        QTemporaryDir files;
        const auto urls = QUrl::fromLocalFile(files.path()+"/one").toEncoded()+"\r\n"+QUrl::fromLocalFile(files.path()+"/two").toEncoded()+"\r\n";
        board->publish({"text/uri-list"}, [&](const QString&) { ++reads; return urls; }, 2);
        QVERIFY(external("formats", "").contains("text/uri-list")); QCOMPARE(reads, 1);
        QCOMPARE(external("read", "text/uri-list"), urls); QCOMPARE(reads, 3);
#else
        QSKIP("macOS only");
#endif
    }
    void nativeWaylandProviders() {
#ifdef Q_OS_LINUX
        if (qEnvironmentVariable("DESKPORT_TEST_WAYLAND") != "1" || !qEnvironmentVariable("WAYLAND_DISPLAY").startsWith("deskport-test-")) QSKIP("Requires an isolated test compositor.");
        auto board = makeWaylandClipboardNative(); QVERIFY(board->valid()); int reads = 0;
        const QByteArray payload(1024 * 1024 + 13, 'p');
        board->publish({"image/png"}, [&](const QString&) { ++reads; return payload; });
        auto external = [&](const QString& mode, const QString& mime) {
            QProcess reader; reader.start(QCoreApplication::applicationFilePath(), {"--native-wayland-reader", mode, mime});
            QElapsedTimer elapsed; elapsed.start();
            while (reader.state() != QProcess::NotRunning && elapsed.elapsed() < 10000) QTest::qWait(5);
            if (reader.state() != QProcess::NotRunning) { reader.kill(); reader.waitForFinished(); return QByteArray("TIMEOUT"); }
            return reader.readAllStandardOutput();
        };
        QVERIFY(external("formats", "").contains("image/png")); QCOMPARE(reads, 0);
        QCOMPARE(external("read", "image/png"), payload); QCOMPARE(reads, 1);
        const QByteArray uris("file:///tmp/synthetic-one\r\nfile:///tmp/synthetic-two\r\n");
        board->publish({"text/uri-list"}, [&](const QString&) { ++reads; return uris; });
        QVERIFY(external("formats", "").contains("text/uri-list")); QCOMPARE(reads, 1);
        QCOMPARE(external("read", "text/uri-list"), uris); QCOMPARE(reads, 2);
#else
        QSKIP("Wayland only");
#endif
    }
    void nativeProtocolUnavailable() {
#ifdef Q_OS_LINUX
        const auto previous = qgetenv("WAYLAND_DISPLAY");
        qputenv("WAYLAND_DISPLAY", "deskport-test-nonexistent-clipboard");
        for (const auto& protocol : {"ext-data-control-v1", "wlr-data-control"}) {
            auto native = makeWaylandClipboardNative(protocol);
            QVERIFY(!native->valid()); QVERIFY(!native->stateMessage().isEmpty());
            QVERIFY(native->formats().isEmpty()); QVERIFY(native->read("image/png").isNull());
            int reads = 0;
            native->publish({"image/png"}, [&](const QString&) { ++reads; return QByteArray("unused"); });
            QCOMPARE(reads, 0); QVERIFY(!native->valid());
        }
        if (previous.isNull()) qunsetenv("WAYLAND_DISPLAY"); else qputenv("WAYLAND_DISPLAY", previous);
#else
        QSKIP("Wayland only");
#endif
    }
    void linuxFileManagerMime() {
#ifdef Q_OS_LINUX
        auto raw = new MemoryClipboard;
        LinuxClipboardMime native{std::unique_ptr<ClipboardNative>(raw)};
        raw->copy("x-special/gnome-copied-files", "copy\nfile:///synthetic/one\nfile:///synthetic/folder");
        QVERIFY(native.formats().contains("text/uri-list")); QCOMPARE(raw->reads, 0);
        QCOMPARE(native.read("text/uri-list"), QByteArray("file:///synthetic/one\nfile:///synthetic/folder"));
        raw->copy("x-special/gnome-copied-files", "cut\nfile:///synthetic/one");
        QCOMPARE(native.read("text/uri-list"), QByteArray("file:///synthetic/one"));
        raw->copy("x-special/gnome-copied-files", "invalid\nfile:///synthetic/one");
        QVERIFY(native.read("text/uri-list").isNull());
        int reads = 0;
        native.publish({"text/uri-list"}, [&](const QString&) { ++reads; return QByteArray("file:///synthetic/one\r\nfile:///synthetic/folder\r\n"); });
        QVERIFY(raw->formats().contains("x-special/gnome-copied-files")); QCOMPARE(reads, 0);
        QCOMPARE(raw->read("x-special/gnome-copied-files"), QByteArray("copy\nfile:///synthetic/one\nfile:///synthetic/folder"));
        native.publish({"text/uri-list"}, [](const QString&) { return QByteArray(); });
        QVERIFY(raw->read("x-special/gnome-copied-files").isNull());
#else
        QSKIP("Linux MIME adapter only");
#endif
    }
    void unsupportedMimeIsNotSuccessfulText() {
        auto native = new MemoryClipboard;
        QList<QJsonObject> messages;
        ClipboardAgent agent{std::unique_ptr<ClipboardNative>(native), [&](const QJsonObject& message) { messages.append(message); }};
        native->copy("application/synthetic-only", "must not be read"); agent.observe();
        QCOMPARE(native->reads, 0);
        QVERIFY(messages.size() >= 2);
        QCOMPARE(messages.first()["type"].toString(), QString("clipboard-v2-status"));
        QVERIFY(!messages.first()["message"].toString().isEmpty());
        QCOMPARE(messages.last()["kind"].toString(), QString());
        QVERIFY(!messages.last().contains("text"));
        messages.clear(); native->copy("text/plain", "supported"); agent.observe();
        // Simulate the authoritative acknowledgement before the queued copy is resent.
        agent.receive({{"type", "clipboard-v2-offer"}, {"kind", "ack"}, {"id", "synthetic-ack"}, {"rev", 1}});
        QVERIFY(!messages.isEmpty()); QCOMPARE(messages.first()["message"].toString(), QString());
    }
    void selectionChangesDuringObservation() {
        auto native = new MemoryClipboard; QList<QJsonObject> messages;
        ClipboardAgent agent{std::unique_ptr<ClipboardNative>(native), [&](const QJsonObject& m) { messages.append(m); }};
        native->copy("text/plain", "obsolete");
        native->reader = [&](const QString&) {
            QTimer::singleShot(0, &agent, [&] { native->bytes["text/plain"] = "latest"; ++native->generation; agent.observe(); });
            QTest::qWait(30); return QByteArray("obsolete");
        };
        agent.observe();
        for (const auto& message : messages) QVERIFY(message["type"] != "clipboard-v2-offer");
        native->reader = {}; messages.clear(); agent.observe();
        QVERIFY(!messages.isEmpty()); QCOMPARE(messages.last()["kind"].toString(), QString("text"));
        QCOMPARE(QByteArray::fromBase64(messages.last()["text"].toString().toLatin1()), QByteArray("latest"));
    }
    void manifestValidation() {
        auto file = [](QString p, qint64 size = 1, bool dir = false) { return QJsonObject{{"path", p}, {"size", QString::number(size)}, {"directory", dir}}; };
        QVERIFY(ClipboardV2::validManifest({file("folder", 0, true), file("folder/a.txt")}));
        for (const auto& path : {"../secret", "/etc/passwd", "a/../../x", "a\\b", "a//b", "C:foo", ".", "x."}) QVERIFY(!ClipboardV2::validManifest({file(path)}));
        QVERIFY(!ClipboardV2::validManifest({file("same"), file("SAME")}));
        QVERIFY(!ClipboardV2::validManifest({file("folder/a")}));
        QVERIFY(!ClipboardV2::validManifest({file("huge", ClipboardV2::MaxFiles + 1)}));
        QVERIFY(!ClipboardV2::validManifest({file("dir", 1, true)}));
    }
    void bidirectionalDemandAndFailure() {
        auto left = new MemoryClipboard; auto right = new MemoryClipboard;
        left->copy("text/plain", "old left"); right->copy("text/plain", "old right");
        ClipboardAgent* a = nullptr; ClipboardAgent* b = nullptr;
        int dataFrames = 0, offers = 0;
        auto deliver = [&](ClipboardAgent* target, QJsonObject message) {
            if (message["type"] == "clipboard-v2-status") return;
            if (message["type"] == "clipboard-v2-data") ++dataFrames;
            if (message["type"] == "clipboard-v2-offer") ++offers;
            QTimer::singleShot(0, target, [target, message] { target->receive(message); });
        };
        ClipboardAgent first(std::unique_ptr<ClipboardNative>(left), [&](const QJsonObject& m) { deliver(b, m); }, true); a = &first;
        ClipboardAgent second(std::unique_ptr<ClipboardNative>(right), [&](const QJsonObject& m) { deliver(a, m); }); b = &second;
        a->observe(); b->observe(); QCOMPARE(offers, 0);
        for (int i = 0; i < 100; ++i) {
            auto source = i % 2 ? right : left; auto destination = i % 2 ? left : right; auto agent = i % 2 ? b : a;
            const auto text = QString("复制 %1\nclipboard 😀").arg(i).toUtf8(); source->copy("text/plain", text); agent->observe();
            QTRY_VERIFY(destination->types.contains("application/x-deskport-clipboard"));
            QCOMPARE(destination->read("text/plain"), text);
        }
        left->copy("text/plain", QByteArray("")); a->observe();
        QTRY_VERIFY(right->types.contains("application/x-deskport-clipboard"));
        QTRY_VERIFY(right->read("text/plain").isEmpty()); QVERIFY(!right->read("text/plain").isNull());
        // Rapid client copies before the first acknowledgement preserve the latest.
        right->copy("text/plain", "first"); b->observe();
        right->copy("text/plain", "latest"); b->observe();
        QTRY_COMPARE(left->read("text/plain"), QByteArray("latest"));
        QTest::qWait(10);
        // Simultaneous copies resolve to the host revision without ping-pong.
        left->copy("text/plain", "host wins"); a->observe();
        right->copy("text/plain", "client concurrent"); b->observe();
        QTRY_COMPARE(right->read("text/plain"), QByteArray("host wins"));
        QTest::qWait(10); QCOMPARE(left->read("text/plain"), QByteArray("host wins"));
        QCOMPARE(dataFrames, 0);
        QByteArray image(3 * ClipboardV2::Chunk + 13, 'i'); image.replace(0, 8, QByteArray::fromHex("89504e470d0a1a0a"));
        left->copy("image/png", image); const int reads = left->reads; a->observe();
        QTRY_VERIFY(right->types.contains("image/png")); QCOMPARE(left->reads, reads); QCOMPARE(dataFrames, 0);
        QCOMPARE(right->read("image/png"), image); QCOMPARE(dataFrames, 4);
        QCOMPARE(right->read("image/png"), image); QCOMPARE(dataFrames, 4);
        // Copy files (including a directory and zero-byte file), then fetch only on URI read.
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QDir().mkpath(directory.path() + "/folder/empty");
        QFile file(directory.path() + "/folder/data.bin"); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(image), qint64(image.size())); file.close();
        QFile empty(directory.path() + "/zero"); QVERIFY(empty.open(QIODevice::WriteOnly)); empty.close();
        const auto uris = QUrl::fromLocalFile(directory.path()+"/folder").toEncoded()+"\r\n"+QUrl::fromLocalFile(directory.path()+"/zero").toEncoded()+"\r\n";
        left->copy("text/uri-list", uris); a->observe(); QTRY_VERIFY(right->types.contains("text/uri-list")); QCOMPARE(dataFrames, 4);
        const auto pasted = right->read("text/uri-list"); QVERIFY(!pasted.isEmpty());
        const auto urls = pasted.trimmed().split('\n'); QCOMPARE(urls.size(), 2);
        const auto folder = QUrl::fromEncoded(urls[0].trimmed()).toLocalFile();
        QVERIFY(QFileInfo(folder + "/empty").isDir()); QFile copy(folder + "/data.bin"); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(), image);
        QVERIFY(QFileInfo(QUrl::fromEncoded(urls[1].trimmed()).toLocalFile()).size() == 0);
        const int frames = dataFrames; QCOMPARE(right->read("text/uri-list"), pasted); QCOMPARE(dataFrames, frames);
        // A file changed after copy must not silently send different content.
        left->copy("text/uri-list", uris); a->observe(); QTest::qWait(20);
        QVERIFY(file.open(QIODevice::Append)); file.write("changed"); file.close(); QVERIFY(right->read("text/uri-list").isEmpty());
        left->copy("image/png", image); a->observe(); QTest::qWait(20);
        left->copy("text/plain", "new text"); QVERIFY(right->read("image/png").isEmpty());
        a->observe(); QTRY_VERIFY(right->types.contains("text/plain")); QCOMPARE(right->read("text/plain"), QByteArray("new text"));
        b->stop(); QVERIFY(right->read("text/plain").isEmpty());
    }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
#ifdef Q_OS_MACOS
    if (app.arguments().contains("--native-reader")) {
        const auto args = app.arguments(); auto native = makeMacClipboardNative(args.value(2));
        const auto bytes = args.value(3) == "formats" ? native->formats().join('\n').toUtf8() : native->read(args.value(4));
        fwrite(bytes.constData(), 1, bytes.size(), stdout); return 0;
    }
#endif
#ifdef Q_OS_LINUX
    if (app.arguments().contains("--native-wayland-reader") && qEnvironmentVariable("DESKPORT_TEST_WAYLAND") == "1") {
        const auto args = app.arguments(); auto native = makeWaylandClipboardNative(); if (!native->valid()) return 2;
        const auto bytes = args.value(2) == "formats" ? native->formats().join('\n').toUtf8() : native->read(args.value(3));
        fwrite(bytes.constData(), 1, bytes.size(), stdout); return 0;
    }
#endif
    DemandTest test; return QTest::qExec(&test, argc, argv);
}
#include "clipboard-demand.moc"
