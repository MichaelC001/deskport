#include <QSysInfo>
#include <QtTest>
#include <future>
#include "adaptivedisplay.h"
#include "workspaceresolution.h"
#include <QTemporaryDir>
#include <QHostInfo>
#include <QSignalSpy>
#include <QSslSocket>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include "peermanager.h"
#include "peerstore.h"
#include "qmlcachekey.h"

static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
class PeerBinding : public QObject {
    Q_OBJECT
private slots:
    void sharedDisplayContract_data() {
        QTest::addColumn<QJsonObject>("message");
        QTest::addColumn<bool>("accepted");
        QFile f(qEnvironmentVariable("TEST_CORE_DISPLAY_CASES"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto cases=QJsonDocument::fromJson(f.readAll()).object()["requests"].toArray();
        QVERIFY(!cases.isEmpty());
        for (const auto& value : cases) {
            const auto item=value.toObject();
            QTest::newRow(qPrintable(item["name"].toString())) << item["message"].toObject() << item["accepted"].toBool();
        }
    }
    void sharedDisplayContract() {
        QFETCH(QJsonObject, message); QFETCH(bool, accepted);
        QTemporaryDir dir;
        const auto cert=credential("TEST_CERT_A");
        const auto fp=QString::fromLatin1(QSslCertificate(cert).digest(QCryptographicHash::Sha256).toHex());
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json", {{"version",1},{"peers",QJsonObject{
            {fp,QJsonObject{{"ready",true},{"granted",true}}}}}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager server(&host,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/binding",0,QHostAddress::LocalHost);
        host.start(2560,1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(),5000);
        QSslSocket socket;
        socket.setLocalCertificate(QSslCertificate(cert));
        socket.setPrivateKey(QSslKey(credential("TEST_KEY_A"),QSsl::Rsa));
        connect(&socket,qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),&socket,[&](const QList<QSslError>& errors){socket.ignoreSslErrors(errors);});
        socket.connectToHostEncrypted("127.0.0.1",quint16(server.port()));
        QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
        QCOMPARE(QJsonDocument::fromJson(socket.readLine()).object()["type"].toString(),QString("hello"));
        socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n');
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
        const auto reply=QJsonDocument::fromJson(socket.readLine()).object();
        QCOMPARE(reply["type"].toString(),QString("display-result"));
        QCOMPARE(reply["seq"].toInt(),message["seq"].toInt());
        QCOMPARE(!reply.contains("error"),accepted);
        if (accepted) {
            QCOMPARE(reply["width"].toInt(),message["width"].toInt());
            QCOMPARE(reply["height"].toInt(),message["height"].toInt());
            socket.write("{\"type\":\"display-ping\"}\n");
            QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
            QCOMPARE(QJsonDocument::fromJson(socket.readLine()).object()["type"].toString(),QString("display-pong"));
            auto changed=message;
            changed["seq"]=2;
            changed["displayPolicy"]=(message.value("displayPolicy").toInt()+1)%3;
            socket.write(QJsonDocument(changed).toJson(QJsonDocument::Compact)+'\n');
            QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
            QVERIFY(QJsonDocument::fromJson(socket.readLine()).object().contains("error"));
            message["seq"]=3;
            socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n');
            QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
            QVERIFY(!QJsonDocument::fromJson(socket.readLine()).object().contains("error"));
        }
        socket.abort(); host.stop();
    }
    void clientOnlyBinding_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("approved") << 0;
        QTest::newRow("declined") << 1;
        QTest::newRow("disconnect-before-approval") << 2;
        QTest::newRow("invalid-host-claim") << 3;
        QTest::newRow("ack-before-approval") << 4;
    }
    void clientOnlyBinding() {
        QFETCH(int, mode);
        QTemporaryDir dir;
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager manager(&host,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/binding",0,QHostAddress::LocalHost);
        QSignalSpy imported(&manager,&PeerManager::peerBound);
        QSslSocket socket;
        socket.setLocalCertificate(QSslCertificate(credential("TEST_CERT_A")));
        socket.setPrivateKey(QSslKey(credential("TEST_KEY_A"),QSsl::Rsa));
        connect(&socket,qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),&socket,[&](const QList<QSslError>& e){socket.ignoreSslErrors(e);});
        socket.connectToHostEncrypted("127.0.0.1",quint16(manager.port()));
        QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
        const auto tx=QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto send=[&](QJsonObject msg){socket.write(QJsonDocument(msg).toJson(QJsonDocument::Compact)+'\n');};
        QJsonObject meta{{"version",1},{"clientBinding",1},{"role","client"},{"name","Test tablet"},{"ready",true},{"granted",true}};
        if(mode==3) meta["hostPort"]=48989;
        send({{"type","request"},{"tx",tx},{"meta",meta}});
        if(mode!=3) {
            QTRY_COMPARE(manager.requestId(),tx);
            QVERIFY(manager.pendingClientOnly());
            QVERIFY(manager.peers().isEmpty());
            if(mode==1) manager.reject(tx);
            else if(mode==2) socket.abort();
            else if(mode==4) send({{"type","client-ready"},{"tx",tx}});
            else {
                manager.approve(tx);
                QTRY_VERIFY(!manager.peers().isEmpty() && manager.peers().first().toMap()["granted"].toBool());
                QVERIFY(!manager.peers().first().toMap()["ready"].toBool());
                send({{"type","client-ready"},{"tx",tx}});
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);
        QCOMPARE(imported.size(),0);
        const auto devices=PeerStore::read(dir.path()+"/host/state.json")["root"].toObject()["named_devices"].toArray();
        QCOMPARE(devices.size(),mode==0 ? 1 : 0);
        if(mode==0) {
            const auto peer=manager.peers().first().toMap();
            QVERIFY(peer["ready"].toBool()); QVERIFY(!peer.contains("hostPort"));
            manager.restoreHosts(); manager.refreshEndpoints();
            QCOMPARE(imported.size(),0); QVERIFY(!manager.busy());
            QVERIFY(!manager.editPeer(peer["fingerprint"].toString(),"Tablet","127.0.0.1",48989,48991));
            manager.revoke(peer["fingerprint"].toString());
            QTRY_VERIFY(!manager.busy()); QVERIFY(manager.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/host/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
        } else QVERIFY(manager.peers().isEmpty());
        host.stop();
    }

    void automaticEndpointRefresh_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("changed-port") << 0;
        QTest::newRow("wrong-stream-identity") << 1;
        QTest::newRow("wrong-tls-pin") << 2;
        QTest::newRow("revoked-at-server") << 3;
        QTest::newRow("wrong-stream-certificate") << 4;
        QTest::newRow("concurrent-local-edit") << 5;
        QTest::newRow("changed-entry-port") << 6;
    }
    void automaticEndpointRefresh() {
        QFETCH(int, failure);
        QTemporaryDir dir;
        const auto aCert = credential("TEST_CERT_A"), bCert = credential("TEST_CERT_B");
        auto digest = [](const QByteArray& cert) {
            return QString::fromLatin1(QSslCertificate(cert).digest(QCryptographicHash::Sha256).toHex());
        };
        QDir().mkpath(dir.path()+"/bb"); QDir().mkpath(dir.path()+"/ab");
        QVERIFY(PeerStore::write(dir.path()+"/bb/peers.json", {{"version",1}, {"peers",QJsonObject{
            {digest(aCert),QJsonObject{{"ready",true},{"granted",failure != 3}}}}}}));
        HostManager bh(nullptr,dir.path()+"/bh");
        PeerManager b(&bh,bCert,credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        auto peer=bh.identity();
        const int actualPort=bh.basePort();
        peer["hostPort"]=actualPort+100;
        peer["bindingPort"]=b.port(); peer["address"]="127.0.0.1";
        peer["name"]="My saved desktop"; peer["customName"]=true;
        peer["ready"]=true; peer["granted"]=true;
        peer["requestedAddress"]=QString("127.0.0.1:%1").arg(b.port());
        peer["clientCert"]=QString::fromUtf8(bCert);
        if (failure==1) peer["hostId"]=QUuid::createUuid().toString();
        if (failure==4) peer["hostCert"]=QString::fromUtf8(credential("TEST_CERT_C"));
        const auto fp=digest(failure==2 ? credential("TEST_CERT_C") : bCert);
        const QJsonObject before{{"version",1},{"peers",QJsonObject{{fp,peer}}}};
        QVERIFY(PeerStore::write(dir.path()+"/ab/peers.json",before));
        HostManager ah(nullptr,dir.path()+"/ah");
        PeerManager a(&ah,aCert,credential("TEST_KEY_A"),dir.path()+"/ab",0,QHostAddress::LocalHost);
        QSignalSpy updated(&a,&PeerManager::peerBound), approval(&b,&PeerManager::incomingRequest);
        const auto status=a.status();
        if (failure==6) {
            QTcpServer reservation; QVERIFY(reservation.listen(QHostAddress::LocalHost,0));
            const int next=reservation.serverPort(); reservation.close();
            QVERIFY(b.setConnectionPort(next));
        }
        a.refreshEndpoints();
        QVERIFY(!a.busy());
        QJsonObject edited;
        if (failure==5) {
            QVERIFY(a.editPeer(fp,"Updated locally","127.0.0.1",actualPort+200,b.port()));
            updated.clear();
            edited=PeerStore::read(dir.path()+"/ab/peers.json");
        }
        if (!failure || failure==6) {
            QTRY_COMPARE_WITH_TIMEOUT(updated.size(),1,5000);
            const auto after=PeerStore::read(dir.path()+"/ab/peers.json")["peers"].toObject()[fp].toObject();
            auto expected=peer; expected["hostPort"]=actualPort;
            expected["resolvedAddress"]="127.0.0.1";
            if (failure==6) {
                expected["bindingPort"]=b.port();
                expected["requestedAddress"]=QString("127.0.0.1:%1").arg(b.port());
            }
            QCOMPARE(after,expected);
            QTest::qWait(100); a.refreshEndpoints(); QTest::qWait(200);
            QCOMPARE(updated.size(),1);
        } else {
            QTest::qWait(600);
            QCOMPARE(updated.size(),0);
            QCOMPARE(PeerStore::read(dir.path()+"/ab/peers.json"),failure==5 ? edited : before);
        }
        QCOMPARE(approval.size(),0);
        if (failure!=5) QCOMPARE(a.status(),status);
        QTRY_VERIFY(!b.busy());
        QVERIFY(!ah.running()); QVERIFY(!bh.running());
    }

    void connectionPortKeepsOldListenerAndRejectsOccupiedPort() {
        QTemporaryDir dir;
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager manager(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/binding",0,QHostAddress::LocalHost);
        const int original=manager.port();
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost,0));
        QVERIFY(!manager.setConnectionPort(occupied.serverPort()));
        QVERIFY(!manager.setConnectionPort(0)); QCOMPARE(manager.port(),original);
        const int next=occupied.serverPort(); occupied.close();
        QVERIFY(manager.setConnectionPort(next)); QCOMPARE(manager.port(),next);
        QTcpServer oldProbe; QVERIFY(!oldProbe.listen(QHostAddress::LocalHost,original));
        QVERIFY(manager.setConnectionPort(original)); QCOMPARE(manager.port(),original);
        QTcpServer newProbe; QVERIFY(!newProbe.listen(QHostAddress::LocalHost,next));
    }

    void editedEndpointSurvivesRestartAndRejectsInvalidInput() {
        QTemporaryDir dir;
        const QString path = dir.path()+"/binding";
        QVERIFY(QDir().mkpath(path));
        const auto cert = credential("TEST_CERT_A"), key = credential("TEST_KEY_A");
        QVERIFY(PeerStore::write(path+"/peers.json", {{"version", 1}, {"peers", QJsonObject{
            {"saved", QJsonObject{{"address", "192.0.2.1"}, {"name", "old"}, {"ready", true},
                {"hostCert", "pinned"}, {"clientCert", "client"}, {"hostId", "stable"}}}}}}));
        {
            HostManager host(nullptr, dir.path()+"/host");
            PeerManager manager(&host,cert,key,path,0,QHostAddress::LocalHost);
            QSignalSpy updated(&manager,&PeerManager::peerBound);
            QVERIFY(manager.editPeer("saved", "My desktop", "desktop.example", 48989, 48991));
            QCOMPARE(updated.size(), 1);
            const auto saved = PeerStore::read(path+"/peers.json");
            QVERIFY(!manager.editPeer("saved", "bad", "https://host/path", 48989, 48991));
            QVERIFY(!manager.editPeer("saved", "bad", "host", 65535, 48991));
            QVERIFY(!manager.editPeer("missing", "bad", "host", 48989, 48991));
            QCOMPARE(PeerStore::read(path+"/peers.json"), saved);
        }
        HostManager host(nullptr, dir.path()+"/host");
        PeerManager manager(&host,cert,key,path,0,QHostAddress::LocalHost);
        const auto peer = manager.peers().first().toMap();
        QCOMPARE(peer["address"].toString(), QString("desktop.example"));
        QCOMPARE(peer["name"].toString(), QString("My desktop"));
        QCOMPARE(peer["hostCert"].toString(), QString("pinned"));
        QVERIFY(manager.editPeer("saved", "IPv6 desktop", "2001:db8::1", 48989, 48991));
        QCOMPARE(manager.peers().first().toMap()["requestedAddress"].toString(), QString("[2001:db8::1]:48991"));
    }
    void fractionalOutputScale() {
        QCOMPARE(DeskPortDisplay::scaleForOutput({2880, 1620}, {1920, 1080}, 2.0), 1.5);
        QCOMPARE(DeskPortDisplay::scaleForOutput({3840, 2160}, {1920, 1080}, 1.0), 2.0);
        QCOMPARE(DeskPortDisplay::scaleForOutput({1920, 1080}, {1920, 1080}, 2.0), 1.0);
        QCOMPARE(DeskPortDisplay::scaleForOutput({}, {1920, 1080}, 1.5), 1.5);
        QCOMPARE(DeskPortDisplay::scaleForOutput({1080, 1920}, {1920, 1080}, 1.5), 1.5);
    }

    void adaptiveDisplayRequiresPinnedApprovedExclusiveController() {
        QTemporaryDir dir;
        const auto aCert = credential("TEST_CERT_A"), bCert = credential("TEST_CERT_B"), cCert = credential("TEST_CERT_C");
        const auto fp = QString::fromLatin1(QSslCertificate(aCert).digest(QCryptographicHash::Sha256).toHex());
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json", {{"version", 1}, {"peers", QJsonObject{
            {fp, QJsonObject{{"ready", true}, {"granted", true}}}}}}));
        HostManager host(nullptr, dir.path()+"/host");
        PeerManager server(&host, bCert, credential("TEST_KEY_B"), dir.path()+"/binding", 0, QHostAddress::LocalHost);
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy resized(&host, &HostManager::displayResized), approval(&server, &PeerManager::incomingRequest);
        auto resize = [this](AdaptiveDisplay& channel, QSize size) {
            std::atomic<int> frames {0};
            auto result = std::async(std::launch::async, [&] { return channel.resize(size, 2, [&] { ++frames; }); });
            QElapsedTimer timer; timer.start();
            while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && timer.elapsed() < 11000) QTest::qWait(10);
            const bool connected = result.get();
            if (connected && frames == 0) return false; // Waiting must keep the loading UI alive.
            return connected;
        };
        {
            AdaptiveDisplay wrongPin("127.0.0.1", server.port(), QSslCertificate(cCert), aCert, credential("TEST_KEY_A"));
            QVERIFY(!resize(wrongPin, QSize(1920, 1080)));
        }
        QTRY_VERIFY(!server.busy());
        {
            AdaptiveDisplay unknown("127.0.0.1", server.port(), QSslCertificate(bCert), cCert, credential("TEST_KEY_C"));
            QVERIFY(!resize(unknown, QSize(1920, 1080)));
        }
        QTRY_VERIFY(!server.busy());
        QCOMPARE(resized.size(), 0); QCOMPARE(approval.size(), 0);
        {
            AdaptiveDisplay channel("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
            QVERIFY(resize(channel, QSize(1920, 1080)));
            QCOMPARE(resized.size(), 1); QVERIFY(host.running()); QVERIFY(!server.busy());
            // Let the worker enter its long condition wait before submitting again.
            QTest::qWait(150);
            QElapsedTimer wakeLatency; wakeLatency.start();
            QTcpServer newEntry; QVERIFY(newEntry.listen(QHostAddress::LocalHost,0));
            const int nextEntry=newEntry.serverPort(); newEntry.close();
            QVERIFY(server.setConnectionPort(nextEntry));
            // The existing authenticated display-control channel survives a port change.
            QVERIFY(resize(channel, QSize(2560, 1440)));
            QCOMPARE(resized.size(), 2); QVERIFY(host.running());
            QVERIFY(wakeLatency.elapsed() < 2000); // Must wake on work, not the 5 s heartbeat.
            // Geometry is opt-in. Legacy desktop leases must still receive a
            // display-pong as their next message, never unsolicited caret data.
            emit host.caretChanged({{"valid",true},{"x",0.25},{"y",0.75}});
            QTest::qWait(5200); // Idle heartbeats must preserve the display lease.

            {
                AdaptiveDisplay competing("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
                QVERIFY(!resize(competing, QSize(2560, 1440)));
            }
            QTRY_VERIFY(!server.busy());
            QVERIFY(resize(channel, QSize(1600, 1000)));
            QCOMPARE(resized.size(), 3);
            QVERIFY(!host.resizeDisplay(99999, 1000, 2, 99));
            QVERIFY(!host.resizeDisplay(1600, 1000, 9, 99));
            QCOMPARE(approval.size(), 0);
        }
        // A released controller cannot prevent the next connection taking over.
        QTest::qWait(100);
        AdaptiveDisplay next("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
        QVERIFY(resize(next, QSize(2560, 1440)));
        int clientReplies = 0;
        for (const auto& reply : resized) if (reply[0].toInt() > 0) ++clientReplies;
        QCOMPARE(clientReplies, 4);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void workspaceUsesClientSystemScale() {
        const auto fractional = DeskPortDisplay::forClient(QSize(2880, 1620), 1.5);
        QCOMPARE(fractional.pixels, QSize(3840, 2160)); QCOMPARE(fractional.scale, 2);
        QCOMPARE(DeskPortDisplay::forClient(QSize(2868, 1500), 1.5).pixels, QSize(3824, 2000));
        QCOMPARE(DeskPortDisplay::forClient(QSize(3828, 2040), 1.5).pixels, QSize(5104, 2720));
        const auto retina = DeskPortDisplay::forClient(QSize(2880, 1800), 2.0);
        QCOMPARE(retina.pixels, QSize(2880, 1800)); QCOMPARE(retina.scale, 2);
        const auto standard = DeskPortDisplay::forClient(QSize(1920, 1080), 1.0);
        QCOMPARE(standard.pixels, QSize(1920, 1080)); QCOMPARE(standard.scale, 1);
        const auto slight = DeskPortDisplay::forClient(QSize(2400, 1350), 1.25);
        QCOMPARE(slight.pixels, QSize(3840, 2160)); QCOMPARE(slight.scale, 2);
        // The 2x logical desktop keeps the 960x540 minimum.
        QCOMPARE(DeskPortDisplay::forClient(QSize(800, 450), 2.0).pixels, QSize(1920, 1080));
        QCOMPARE(DeskPortDisplay::forClient(QSize(8000, 4500), 2.0).pixels, QSize(7680, 4320));
        QVERIFY(!DeskPortDisplay::forClient(QSize(), 1.5).pixels.isValid());
        QVERIFY(!DeskPortDisplay::forClient(QSize(1920,1080), 0).pixels.isValid());
    }
    void fractionalWorkspacePreservesDetailAndUiSize() {
        // Compare the visible UI scale after fitting the stream to the window.
        // Merely asserting a chosen resolution would miss the old oversized UI.
        for (double scale : {1.0, 1.25, 1.5, 1.75, 2.0}) {
            for (QSize drawable : {QSize(2868, 1500), QSize(2400, 1600), QSize(1920, 2400)}) {
                const auto workspace = DeskPortDisplay::forClient(drawable, scale);
                QVERIFY(workspace.pixels.width() >= drawable.width());
                QVERIFY(workspace.pixels.height() >= drawable.height());
                const double visibleScale = double(workspace.scale) * drawable.width() / workspace.pixels.width();
                QVERIFY(std::abs(visibleScale - scale) < 0.005);
                QCOMPARE(workspace.pixels.width() % 4, 0);
                QCOMPARE(workspace.pixels.height() % 4, 0);
            }
        }
        const auto dense = DeskPortDisplay::forClient(QSize(3840, 2160), 3.0);
        QCOMPARE(dense.pixels, QSize(3840, 2160)); // No low-resolution upscaling above 2x.
        const auto cappedPortrait = DeskPortDisplay::forClient(QSize(1620, 2880), 1.25);
        QCOMPARE(cappedPortrait.pixels, QSize(2432, 4320));
    }
    void adaptiveSizeBounds() {
        QCOMPARE(AdaptiveDisplay::boundedSize(QSize(15360, 8640)), QSize(7680, 4320));
        QCOMPARE(AdaptiveDisplay::boundedSize(QSize(1001, 777)), QSize(1000, 776));
        QCOMPARE(AdaptiveDisplay::boundedSize(QSize(100, 100)), QSize(640, 360));
        QVERIFY(!AdaptiveDisplay::boundedSize(QSize(0, 0)).isValid());
    }
    void qmlCacheChangesWithContentEvenWhenTimestampsMatch() {
        QTemporaryDir dir;
        QFile file(dir.path()+"/main.qml");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("old UI"); file.close();
        const auto timestamp = QFileInfo(file).lastModified();
        const auto before = qmlCacheKey(dir.path());
        QCOMPARE(before,qmlCacheKey(dir.path()));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("new UI");
        QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime)); file.close();
        QVERIFY(before != qmlCacheKey(dir.path()));
        const auto after = qmlCacheKey(dir.path());
        QFile child(dir.path()+"/Approval.qml");
        QVERIFY(child.open(QIODevice::WriteOnly)); child.write("Dialog {}"); child.close();
        QVERIFY(after != qmlCacheKey(dir.path()));
        QVERIFY(child.remove()); QCOMPARE(after,qmlCacheKey(dir.path()));
    }
    void actualApprovalDialogOpensAndClosesWithRequest() {
        QTemporaryDir dir;
        HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
        PeerManager a(&ah,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/ab",0,QHostAddress::LocalHost);
        PeerManager b(&bh,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        QQuickWindow window;
        QQmlComponent component(&engine,QUrl::fromLocalFile(qEnvironmentVariable("TEST_BINDING_QML")));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QScopedPointer<QObject> dialog(component.createWithInitialProperties({
            {"manager",QVariant::fromValue(&b)}, {"appWindow",QVariant::fromValue(&window)},
            {"parent",QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(dialog,qPrintable(component.errorString()));
        a.request(QString("127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(),5000);
        QCOMPARE(dialog->property("transaction").toString(),b.requestId());
        QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
        a.cancel();
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    void trustPreservesExistingClientsAndRejectsBrokenState() {
        QTemporaryDir dir; const QString file = dir.path()+"/state.json";
        const QSslCertificate cert(credential("TEST_CERT_A"));
        QVERIFY(!cert.isNull());
        QVERIFY(PeerStore::write(file, {{"extra", 42}, {"root", QJsonObject{{"uniqueid", "stable"},
            {"named_devices", QJsonArray{QJsonObject{{"uuid", "other"}, {"name", "existing"}}}}}}}));
        QVERIFY(PeerStore::trust(file, "new", "new device", cert));
        auto state = PeerStore::read(file);
        QCOMPARE(state["extra"].toInt(), 42);
        QCOMPARE(state["root"].toObject()["uniqueid"].toString(), QString("stable"));
        QCOMPARE(state["root"].toObject()["named_devices"].toArray().size(), 2);
        QVERIFY(PeerStore::trust(file, "legacy-replacement", "same certificate", cert));
        QCOMPARE(PeerStore::read(file)["root"].toObject()["named_devices"].toArray().size(), 2);
        QVERIFY(PeerStore::trust(file, "new", "renamed", cert));
        QCOMPARE(PeerStore::read(file)["root"].toObject()["named_devices"].toArray().size(), 2);
        QVERIFY(PeerStore::trust(file, "new", "", QSslCertificate(), true));
        QCOMPARE(PeerStore::read(file)["root"].toObject()["named_devices"].toArray().size(), 1);
        QFile broken(file); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("{broken"); broken.close();
        QVERIFY(!PeerStore::trust(file, "new", "new", cert));
        QVERIFY(broken.open(QIODevice::ReadOnly)); QCOMPARE(broken.readAll(), QByteArray("{broken"));
    }
    void mutualBindingWaitsForOneApprovalAndSurvivesRestart() {
        QTemporaryDir dir;
        const auto aCert = credential("TEST_CERT_A"), bCert = credential("TEST_CERT_B");
        const auto aKey = credential("TEST_KEY_A"), bKey = credential("TEST_KEY_B");
        QString aId, bId;
        {
            HostManager ah(nullptr, dir.path()+"/ah"), bh(nullptr, dir.path()+"/bh");
            PeerManager a(&ah,aCert,aKey,dir.path()+"/ab",0,QHostAddress::LocalHost);
            PeerManager b(&bh,bCert,bKey,dir.path()+"/bb",0,QHostAddress::LocalHost);
            QVERIFY(a.port() > 0); QVERIFY(b.port() > 0);
            aId=ah.identity()["hostId"].toString(); bId=bh.identity()["hostId"].toString();
            QSignalSpy incoming(&b,&PeerManager::incomingRequest), aDone(&a,&PeerManager::peerBound), bDone(&b,&PeerManager::peerBound);
            a.request(QString("localhost:%1").arg(b.port()));
            QTRY_COMPARE_WITH_TIMEOUT(incoming.size(), 1, 5000);
            QTRY_VERIFY(a.status().contains("Request received"));
            QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
            b.approve("wrong-transaction"); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
            b.approve(b.requestId());
            QTRY_COMPARE_WITH_TIMEOUT(aDone.size(),1,7000);
            QTRY_COMPARE_WITH_TIMEOUT(bDone.size(),1,7000);
            QCOMPARE(a.peers().first().toMap()["os"].toString(), QSysInfo::prettyProductName());
            QCOMPARE(a.peers().first().toMap()["address"].toString(), QString("localhost"));
            QCOMPARE(b.peers().first().toMap()["address"].toString(), QHostInfo::localHostName());
            QVERIFY(a.peers().first().toMap()["ready"].toBool()); QVERIFY(b.peers().first().toMap()["ready"].toBool());
            auto clients=PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray();
            QCOMPARE(clients.size(),1); QCOMPARE(QSslCertificate(clients[0].toObject()["cert"].toString().toUtf8()),QSslCertificate(bCert));
            QFile metadata(dir.path()+"/ab/peers.json"); QVERIFY(metadata.open(QIODevice::ReadOnly)); QVERIFY(!metadata.readAll().contains("PRIVATE KEY"));
            auto legacy = PeerStore::read(dir.path()+"/ab/peers.json");
            auto peers = legacy["peers"].toObject();
            auto it = peers.begin(); auto peer = it.value().toObject();
            peer["address"] = "127.0.0.1"; peer.remove("resolvedAddress");
            it.value() = peer; legacy["peers"] = peers;
            QVERIFY(PeerStore::write(dir.path()+"/ab/peers.json", legacy));
        }
        {
            HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
            PeerManager a(&ah,aCert,aKey,dir.path()+"/ab",0,QHostAddress::LocalHost);
            PeerManager b(&bh,bCert,bKey,dir.path()+"/bb",0,QHostAddress::LocalHost);
            QCOMPARE(ah.identity()["hostId"].toString(),aId); QCOMPARE(bh.identity()["hostId"].toString(),bId);
            QSignalSpy restored(&a,&PeerManager::peerBound); a.restoreHosts(); QCOMPARE(restored.size(),1);
            QCOMPARE(restored.first().first().toMap()["address"].toString(), QString("localhost"));
            HostManager ch(nullptr,dir.path()+"/ch");
            PeerManager c(&ch,credential("TEST_CERT_C"),credential("TEST_KEY_C"),dir.path()+"/cb",quint16(a.peers().first().toMap()["bindingPort"].toInt()),QHostAddress::LocalHost);
            QSignalSpy substituted(&c,&PeerManager::incomingRequest);
            a.request(QString("localhost:%1").arg(c.port()));
            QTRY_VERIFY_WITH_TIMEOUT(!a.busy(),5000);
            QCOMPARE(substituted.size(),0); QVERIFY(c.peers().isEmpty());
            QVERIFY(a.status().contains("different device key"));
            a.revoke(a.peers().first().toMap()["fingerprint"].toString());
            QTRY_VERIFY_WITH_TIMEOUT(!a.busy(),5000); QVERIFY(a.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
        }
    }
    void invalidOversizedAndReplayedMessagesCannotGrant() {
        QTemporaryDir dir;
        HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
        QVERIFY(ah.prepareIdentity(credential("TEST_CERT_A"),credential("TEST_KEY_A")));
        PeerManager b(&bh,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        for (int mode = 0; mode < 3; ++mode) {
            QSslSocket socket;
            socket.setLocalCertificate(QSslCertificate(credential("TEST_CERT_A")));
            socket.setPrivateKey(QSslKey(credential("TEST_KEY_A"), QSsl::Rsa));
            connect(&socket,qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),&socket,[&socket](const QList<QSslError>& e){ socket.ignoreSslErrors(e); });
            socket.connectToHostEncrypted("127.0.0.1",quint16(b.port()));
            QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
            if (mode == 0) socket.write("{not-json}\n");
            if (mode == 1) socket.write(QByteArray(40000,'x'));
            if (mode == 2) {
                auto meta=ah.identity(); meta["version"]=1; meta["bindingPort"]=48991; meta["name"]="Test A";
                const auto request=QJsonDocument(QJsonObject{{"type","request"},{"tx",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"meta",meta}}).toJson(QJsonDocument::Compact)+ '\n';
                socket.write(request); QTRY_VERIFY(!b.requestId().isEmpty());
                socket.write(request);
            }
            QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
            QVERIFY(b.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/bh/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
        }
    }
    void declinedOrDisconnectedRequestsNeverGrantAccess() {
        QTemporaryDir dir;
        HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
        PeerManager a(&ah,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/ab",0,QHostAddress::LocalHost);
        PeerManager b(&bh,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        a.request(QString("localhost:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(),5000);
        b.reject(b.requestId());
        QTRY_VERIFY(!a.busy()); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
        a.request(QString("localhost:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(),5000);
        const QString stale=b.requestId(); a.cancel(); QTRY_VERIFY(!b.busy());
        b.approve(stale); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
    }
};
QTEST_MAIN(PeerBinding)
#include "peer-binding.moc"
