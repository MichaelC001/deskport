#include "smalltcp.h"
#include "smalltcptunnel.h"
#include "nvhttp.h"
#include <QtTest>
#include <QProcess>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
extern "C" const char* LiGetLaunchUrlQueryParameters() { return ""; }

class SmallTcpTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QProcess server;
    quint16 port = 0;
    QString log;
    QSslCertificate cert;
    void start(const QString& mode) {
        server.kill(); server.waitForFinished();
        log = directory.filePath(QUuid::createUuid().toString() + ".jsonl");
        server.start(qEnvironmentVariable("TEST_PYTHON"), {qEnvironmentVariable("TEST_FIXTURE"),
            qEnvironmentVariable("TEST_CERT"), qEnvironmentVariable("TEST_KEY"), mode, log});
        QVERIFY(server.waitForStarted()); QVERIFY(server.waitForReadyRead());
        port = server.readLine().trimmed().toUShort(); QVERIFY(port);
    }
    QList<QJsonObject> records() {
        QFile file(log); QList<QJsonObject> result; if (!file.open(QIODevice::ReadOnly)) return result;
        for (const auto& line : file.readAll().split('\n')) if (!line.isEmpty()) result << QJsonDocument::fromJson(line).object();
        return result;
    }
    int count(const QString& field) { int n = 0; for (const auto& entry : records()) n += entry.contains(field); return n; }
    void configure(QSslSocket& socket) {
        socket.setProxy(QNetworkProxy::NoProxy);
        connect(&socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), &socket,
            [&, this](const QList<QSslError>& errors) { if (socket.peerCertificate() == cert) socket.ignoreSslErrors(errors); });
    }
private slots:
    void initTestCase() {
        QFile file(qEnvironmentVariable("TEST_CERT")); QVERIFY(file.open(QIODevice::ReadOnly)); cert = QSslCertificate(file.readAll());
        QVERIFY(!cert.isNull());
    }
    void cleanup() { server.kill(); server.waitForFinished(); auto& c = SmallTcp::cache(); QMutexLocker lock(&c.mutex); c.entries.clear(); c.addresses.clear(); }
    void nativeBlocking() {
        if (!SmallTcp::supported()) QSKIP("Linux MSS support only");
        start("small"); QSslSocket socket; configure(socket);
        const bool connected = SmallTcp::connectBlocking(socket, "127.0.0.1", port, 300); qInfo() << records() << socket.errorString(); QVERIFY(connected); QCOMPARE(socket.peerCertificate(), cert);
        SmallTcp::accepted(socket, "127.0.0.1", port);
        QCOMPARE(count("mss"), 2); QVERIFY(records().last()["mss"].toInt() <= 900);
        QVERIFY(SmallTcp::cached(SmallTcp::pathKey("127.0.0.1", port)));
        socket.abort();
        QSslSocket second; configure(second); QVERIFY(SmallTcp::connectBlocking(second, "127.0.0.1", port, 300));
        QCOMPARE(count("mss"), 3);
    }
    void nativeAsync() {
        if (!SmallTcp::supported()) QSKIP("Linux MSS support only");
        start("small"); QSslSocket socket; configure(socket);
        SmallTcp::connectAsync(&socket, "127.0.0.1", port);
        QTest::qWait(3500); qInfo() << records() << socket.errorString(); QVERIFY(socket.isEncrypted());
        QCOMPARE(socket.peerCertificate(), cert); QCOMPARE(count("mss"), 2);
        QTest::qWait(1700); QCOMPARE(count("mss"), 2);
    }
    void cachedHostname() {
        if (!SmallTcp::supported()) QSKIP("Linux MSS support only");
        start("small"); SmallTcp::remember(SmallTcp::pathKey("localhost", port), QHostAddress("127.0.0.1"));
        QSslSocket socket; configure(socket);
        QVERIFY(SmallTcp::connectBlocking(socket, "localhost", port, 700));
        QCOMPARE(socket.peerCertificate(), cert); QCOMPARE(count("mss"), 1);
        QVERIFY(records().last()["mss"].toInt() <= 900);
    }
    void nativeIPv6() {
        if (!SmallTcp::supported()) QSKIP("Linux MSS support only");
        start("small6"); QSslSocket socket; configure(socket);
        QVERIFY(SmallTcp::connectBlocking(socket, "::1", port, 300));
        QCOMPARE(socket.peerCertificate(), cert); QCOMPARE(count("mss"), 2);
        QVERIFY(records().last()["mss"].toInt() <= 900);
    }
    void httpIPv6() {
        if (!SmallTcp::supported()) QSKIP("Linux MSS support only");
        start("small6"); NvHTTP http(NvAddress("::1", port), port, cert);
        QCOMPARE(http.openConnectionToString(http.m_BaseUrlHttps, "serverinfo", {}, 400).size(), 262144);
        QCOMPARE(count("mss"), 2); QCOMPARE(count("request"), 1);
    }
    void normalPathUsesOneConnection() {
        start("normal"); NvHTTP http(NvAddress("127.0.0.1", port), port, cert);
        QCOMPARE(http.openConnectionToString(http.m_BaseUrlHttps, "serverinfo", {}, 400).size(), 262144);
        QCOMPARE(count("mss"), 1); QCOMPARE(count("request"), 1);
        QVERIFY(!SmallTcp::cached(SmallTcp::pathKey("127.0.0.1", port)));
    }
    void tunnelPinFailure() {
        if (!SmallTcp::supported()) QSKIP("Linux MSS support only");
        start("small"); QFile other(qEnvironmentVariable("TEST_OTHER_CERT")); QVERIFY(other.open(QIODevice::ReadOnly));
        NvHTTP http(NvAddress("127.0.0.1", port), port, QSslCertificate(other.readAll()));
        bool rejected = false;
        try { http.openConnectionToString(http.m_BaseUrlHttps, "serverinfo", {}, 400); }
        catch (const GfeHttpResponseException&) { rejected = true; }
        QVERIFY(rejected); QCOMPARE(count("mss"), 2); QCOMPARE(count("request"), 0);
        QVERIFY(!SmallTcp::cached(SmallTcp::pathKey("127.0.0.1", port)));
    }
    void pinFailureDoesNotRetry() {
        start("normal"); QSslSocket socket; socket.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(!SmallTcp::connectBlocking(socket, "127.0.0.1", port, 300)); QCOMPARE(count("mss"), 1);
        QVERIFY(!SmallTcp::cached(SmallTcp::pathKey("127.0.0.1", port)));
    }
    void httpFallbackAndLargeBody() {
        if (!SmallTcp::supported()) QSKIP("Linux MSS support only");
        start("small"); NvHTTP http(NvAddress("127.0.0.1", port), port, cert);
        QCOMPARE(http.openConnectionToString(http.m_BaseUrlHttps, "serverinfo", {}, 400).size(), 262144);
        QCOMPARE(count("mss"), 2); QCOMPARE(count("request"), 1);
        QCOMPARE(http.openConnectionToString(http.m_BaseUrlHttps, "serverinfo", {}, 400).size(), 262144);
        QCOMPARE(count("mss"), 3); QCOMPARE(count("request"), 2);
    }
    void httpPinFailure() {
        start("normal"); NvHTTP http(NvAddress("127.0.0.1", port), port, QSslCertificate());
        // Use a different real certificate: an absent pin is a programmer error.
        QFile other(qEnvironmentVariable("TEST_OTHER_CERT")); QVERIFY(other.open(QIODevice::ReadOnly));
        http.setServerCert(QSslCertificate(other.readAll()));
        bool rejected = false;
        try { http.openConnectionToString(http.m_BaseUrlHttps, "serverinfo", {}, 400); }
        catch (const GfeHttpResponseException&) { rejected = true; }
        QVERIFY(rejected); QCOMPARE(count("mss"), 1); QCOMPARE(count("request"), 0);
    }
    void sideEffectNeverReplayed() {
        start("drop"); NvHTTP http(NvAddress("127.0.0.1", port), port, cert);
        bool timedOut = false;
        try { http.openConnectionToString(http.m_BaseUrlHttps, "launch", {}, 400); }
        catch (const QtNetworkReplyException&) { timedOut = true; }
        QVERIFY(timedOut); QCOMPARE(count("mss"), 1); QCOMPARE(count("request"), 1);
    }
    void boundedRetry() {
        start("blackhole"); NvHTTP http(NvAddress("127.0.0.1", port), port, cert);
        bool timedOut = false;
        try { http.openConnectionToString(http.m_BaseUrlHttps, "serverinfo", {}, 250); }
        catch (const QtNetworkReplyException&) { timedOut = true; }
        QVERIFY(timedOut); QCOMPARE(count("mss"), SmallTcp::supported() ? 2 : 1);
        QCOMPARE(count("request"), 0); QVERIFY(!SmallTcp::cached(SmallTcp::pathKey("127.0.0.1", port)));
    }
    void cacheExpiry() {
        if (!SmallTcp::supported()) QSKIP("Linux MSS support only");
        const auto key = SmallTcp::pathKey("127.0.0.1", 12345); SmallTcp::remember(key); QVERIFY(SmallTcp::cached(key));
        qint64 original;
        { auto& c = SmallTcp::cache(); QMutexLocker lock(&c.mutex); original = c.entries.value(key); }
        QTest::qWait(5); SmallTcp::remember(key);
        { auto& c = SmallTcp::cache(); QMutexLocker lock(&c.mutex);
          QCOMPARE(c.entries.value(key), original); c.entries[key] = c.clock.elapsed() - 120001; }
        QVERIFY(!SmallTcp::cached(key)); QVERIFY(!SmallTcp::cached(SmallTcp::pathKey("127.0.0.2", 12345)));
    }
};
QTEST_GUILESS_MAIN(SmallTcpTests)
#include "small-tcp.moc"
