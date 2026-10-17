#include "clipboard/agent.h"
#include <QImage>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QBuffer>
#include "clipboard/linuxmime.h"
#include <QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QUrl>
#include <signal.h>

class MemoryClipboard final : public ClipboardNative {
public:
    qint64 generation = 0;
    QStringList types;
    QHash<QString, QByteArray> bytes;
    Reader provider;
    qint64 revision() override { return generation; }
    QStringList formats() override { return types; }
    QByteArray read(const QString& mime) override { return provider ? provider(mime) : bytes.value(mime); }
    void publish(const QStringList& formats, Reader reader, int = 1) override { types = formats; provider = std::move(reader); ++generation; }
    void copy(const QString& mime, const QByteArray& data) { provider = {}; types = {mime}; bytes.clear(); bytes[mime] = data; ++generation; }
};
class NativeTest : public QObject {
    Q_OBJECT
    QString protocol;
public:
    explicit NativeTest(QString backend) : protocol(std::move(backend)) {}
private slots:
    void twoNativeConnections() {
        if (qEnvironmentVariable("DESKPORT_TEST_SYNTHETIC_SESSION") != "1") QSKIP("Owned synthetic compositor/VM required");
        LinuxClipboardMime left(makeWaylandClipboardNative(protocol));
        std::unique_ptr<ClipboardNative> rightProvider(new LinuxClipboardMime(makeWaylandClipboardNative(protocol)));
        auto& right = *rightProvider;
        QTRY_VERIFY_WITH_TIMEOUT(!left.valid() || left.stateMessage().isEmpty(), 90000);
        QTRY_VERIFY_WITH_TIMEOUT(!right.valid() || right.stateMessage().isEmpty(), 90000);
        QVERIFY2(left.valid(), qPrintable(left.stateMessage())); QVERIFY2(right.valid(), qPrintable(right.stateMessage()));
        int reads = 0;
        const QByteArray image(1024 * 1024 + 13, 'p');
        const auto originalRevision = right.revision();
        left.publish({"image/png", "application/x-deskport-clipboard"}, [&](const QString& mime) { ++reads; return mime == "image/png" ? image : QByteArray("synthetic-image"); });
        QCOMPARE(reads, 0); // Publishing itself does not invoke the provider.
        QTRY_VERIFY(right.revision() != originalRevision && right.formats().contains("image/png"));
        const auto externalReads = reads; // A desktop history manager may request data.
        QVERIFY(right.read("application/not-offered").isNull()); QCOMPARE(reads, externalReads);
        QCOMPARE(right.read("image/png"), image);
        qInfo() << "Synthetic source reads requested by native consumers:" << reads;
        for (int i = 0; i < 100; ++i) {
            auto source = i % 2 ? &left : &right; auto target = i % 2 ? &right : &left;
            const auto text = QString("Synthetic 中文 %1 😀").arg(i).toUtf8(); const auto revision = target->revision();
            source->publish({"text/plain;charset=utf-8"}, [text](const QString&) { return text; });
            QTRY_VERIFY(target->revision() != revision && target->formats().contains("text/plain;charset=utf-8"));
            QCOMPARE(target->read("text/plain;charset=utf-8"), text);
        }
        const QByteArray urls("file:///synthetic/one\r\nfile:///synthetic/folder\r\n");
        const auto beforeUris = reads;
        left.publish({"text/uri-list"}, [&](const QString&) { ++reads; return urls; }); QCOMPARE(reads, beforeUris);
        QTRY_VERIFY(right.formats().contains("x-special/gnome-copied-files"));
        QCOMPARE(right.read("x-special/gnome-copied-files"), QByteArray("copy\nfile:///synthetic/one\nfile:///synthetic/folder"));
        const auto revision = right.revision();
        left.publish({"text/uri-list"}, [](const QString&) { return QByteArray(); });
        QTRY_VERIFY(right.revision() != revision);
        QVERIFY(right.read("text/uri-list").isEmpty()); // Failed materialization never returns a usable URL.
        right.publish({"text/plain"}, [](const QString&) { return QByteArray("new owner"); });
        QTRY_VERIFY(left.formats().contains("text/plain"));
        left.publish({}, {}); // Releasing the old remote offer preserves right's newer owner.
        QTest::qWait(100); QCOMPARE(left.read("text/plain"), QByteArray("new owner"));
        // Exercise production v2 agents over queued synthetic peer frames with
        // the actual compositor provider on one endpoint and a deterministic peer.
        auto peer = new MemoryClipboard;
        ClipboardAgent* localAgent = nullptr; ClipboardAgent* remoteAgent = nullptr;
        auto relay = [&](ClipboardAgent* target, const QJsonObject& message) {
            if (message["type"] == "clipboard-v2-status") return;
            QTimer::singleShot(0, target, [target, message] { target->receive(message); });
        };
        ClipboardAgent remote(std::unique_ptr<ClipboardNative>(peer), [&](const QJsonObject& m) { relay(localAgent, m); }, true);
        remoteAgent = &remote;
        ClipboardAgent local(std::move(rightProvider), [&](const QJsonObject& m) { relay(remoteAgent, m); });
        localAgent = &local;
        QImage picture(32, 32, QImage::Format_RGBA8888); picture.fill(QColor(30, 120, 210));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(picture.save(&buffer, "PNG"));
        left.publish({"image/png"}, [png](const QString&) { return png; });
        QTRY_VERIFY(peer->types.contains("image/png")); QCOMPARE(peer->read("image/png"), png);
        peer->copy("image/png", png); remote.observe();
        QTRY_VERIFY(left.formats().contains("application/x-deskport-clipboard"));
        QCOMPARE(left.read("image/png"), png);

        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(QDir().mkpath(directory.path() + "/folder/empty"));
        const QByteArray fileBytes(ClipboardV2::Chunk + 17, 'f');
        QFile source(directory.path() + "/folder/payload.bin"); QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(fileBytes), qint64(fileBytes.size())); source.close();
        QFile zero(directory.path() + "/zero"); QVERIFY(zero.open(QIODevice::WriteOnly)); zero.close();
        const auto sourceUrls = QUrl::fromLocalFile(directory.path()+"/folder").toEncoded()+"\r\n"+QUrl::fromLocalFile(directory.path()+"/zero").toEncoded()+"\r\n";
        auto checkFiles = [&](const QByteArray& urls) {
            const auto roots = urls.trimmed().split('\n');
            if (roots.size() != 2) return false;
            const auto folder = QUrl::fromEncoded(roots[0].trimmed()).toLocalFile();
            QFile file(folder + "/payload.bin");
            return QFileInfo(folder+"/empty").isDir() && file.open(QIODevice::ReadOnly) && file.readAll() == fileBytes &&
                   QFileInfo(QUrl::fromEncoded(roots[1].trimmed()).toLocalFile()).isFile() &&
                   QFileInfo(QUrl::fromEncoded(roots[1].trimmed()).toLocalFile()).size() == 0;
        };
        left.publish({"text/uri-list"}, [sourceUrls](const QString&) { return sourceUrls; });
        QTRY_VERIFY(peer->types.contains("text/uri-list")); QVERIFY(checkFiles(peer->read("text/uri-list")));
        peer->copy("text/uri-list", sourceUrls); remote.observe();
        QTRY_VERIFY(left.formats().contains("application/x-deskport-clipboard"));
        const auto copied = left.read("x-special/gnome-copied-files"); QVERIFY(copied.startsWith("copy\n"));
        QVERIFY(checkFiles(copied.mid(5)));
        // Change the source after metadata was offered; no partial URL may escape.
        peer->copy("text/uri-list", sourceUrls); remote.observe();
        QVERIFY(source.open(QIODevice::Append)); source.write("changed"); source.close();
        QTest::qWait(100);
        QVERIFY(left.read("text/uri-list").isEmpty());
        local.stop(); remote.stop(); left.cancel(); QVERIFY(!left.valid()); QVERIFY(!right.valid());
    }
};
int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN); QCoreApplication app(argc, argv);
    if (app.arguments().contains("--probe-protocols")) {
        if (qEnvironmentVariable("DESKPORT_TEST_SYNTHETIC_SESSION") != "1") return 64;
        QJsonObject capabilities;
        for (const auto& protocol : {"ext-data-control-v1", "wlr-data-control"}) {
            auto native = makeWaylandClipboardNative(protocol); capabilities[protocol] = native->valid();
        }
        const auto bytes = QJsonDocument(capabilities).toJson(QJsonDocument::Compact);
        fwrite(bytes.constData(), 1, bytes.size(), stdout); fputc('\n', stdout); return 0;
    }
    const auto protocol = qEnvironmentVariable("DESKPORT_TEST_WAYLAND_PROTOCOL");
    NativeTest test(protocol); return QTest::qExec(&test, argc, argv);
}
#include "waylandclipboard-native.moc"
