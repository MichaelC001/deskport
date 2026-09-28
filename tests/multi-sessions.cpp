#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include "multisessions.h"
#include "peermanager.h"
#include "peerstore.h"
#include "sessiongraph.h"
static QByteArray credential(const char* name) { QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }

// Real child processes and local sockets; synthetic media never contacts a host
// or touches physical input. Slow hide acknowledgements test ownership ordering.
static int worker(QApplication& app) {
    QLocalSocket socket;
    QByteArray buffer;
    int fullscreenRequests = 0;
    const QString id=app.arguments().value(2);
    const auto mode=app.arguments().value(3);
    auto send=[&](QJsonObject o) { socket.write(QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n'); };
    QObject::connect(&socket,&QLocalSocket::connected,&app,[&] {
        send({{"token",qEnvironmentVariable("DESKPORT_SESSION_TOKEN")}});
        send({{"type","ready"}}); send({{"type","connected"}});
    });
    QObject::connect(&socket,&QLocalSocket::readyRead,&app,[&] {
        buffer+=socket.readAll();
        while (buffer.contains('\n')) {
            auto end=buffer.indexOf('\n'); auto o=QJsonDocument::fromJson(buffer.left(end)).object(); buffer.remove(0,end+1);
            const auto action=o.value("command").toString();
            if(action=="show") send({{"type","shown"}});
            else if(action=="fullscreen") send({{"type","traffic"},{"received",0},{"sent",++fullscreenRequests}});
            else if(action=="hide") {
                if (mode=="hung") continue;
                QTimer::singleShot(mode=="slow" ? 300 : 0,&app,[&,o] { send({{"type","hidden"},{"epoch",o.value("epoch")}}); });
            } else if(action=="disconnect") QTimer::singleShot(0,&app,&QCoreApplication::quit);
            else if(action=="reconnect" && mode=="crash") QCoreApplication::exit(7);
        }
    });
    QObject::connect(&socket,&QLocalSocket::disconnected,&app,&QCoreApplication::quit);
    socket.connectToServer(qEnvironmentVariable("DESKPORT_SESSION_ENDPOINT"));
    QTimer::singleShot(15000,&app,&QCoreApplication::quit);
    return app.exec();
}
class MultiSessionsTest : public QObject {
    Q_OBJECT
private slots:
    void concurrentWorkersAndInputHandoff() {
        QTemporaryDir dir;
        auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A");
        auto remote=credential("TEST_CERT_B");
        QJsonObject records;
        for(auto id : {"a","b","c"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(remote)},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        const auto identity=SessionGraph::identity(QSslCertificate(cert));
        auto row=[&](QString id) { for(const auto& item:manager.sessions()) if(item.toMap().value("id")==id)return item.toMap(); return QVariantMap{}; };
        QSignalSpy shown(&manager,&MultiSessions::viewerShown);
        manager.open("a","First","127.0.0.1","slow");
        QTRY_COMPARE(row("a").value("state").toString(),QString("connected"));
        QTRY_VERIFY(row("a").value("visible").toBool());
        manager.open("b","Second","127.0.0.1","crash");
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;}); heartbeat.start();
        QTest::qWait(100);
        QVERIFY(!row("b").value("visible").toBool()); // A has not released input yet.
        QVERIFY(ticks>=5);
        QTRY_VERIFY(row("b").value("visible").toBool());
        QCOMPARE(row("a").value("state").toString(),QString("connected"));
        QCOMPARE(SessionGraph::branches(identity).first.size(),2);
        manager.fullscreen("a");
        QTRY_VERIFY(row("a").value("visible").toBool());
        QTRY_COMPARE(manager.selectedTraffic().value("sent").toInt(),1);
        // Rapid switching supersedes older hide acknowledgements.
        for(int i=0;i<12;++i) { manager.select(i%2 ? "a":"b"); QTest::qWait(5); }
        manager.select("b"); QTRY_VERIFY(row("b").value("visible").toBool());
        QVERIFY(!row("a").value("visible").toBool());
        manager.showDevices();
        QTRY_VERIFY(!row("b").value("visible").toBool());
        QCOMPARE(row("a").value("state").toString(),QString("connected"));
        QCOMPARE(row("b").value("state").toString(),QString("connected"));
        // One crash cannot tear down the other connection or its graph edge.
        manager.reconnect("b");
        QTRY_COMPARE(row("b").value("state").toString(),QString("error"));
        QCOMPARE(row("a").value("state").toString(),QString("connected"));
        QCOMPARE(SessionGraph::branches(identity).first.size(),1);
        manager.select("a"); QTRY_VERIFY(row("a").value("visible").toBool());
        manager.open("c","Third","127.0.0.1");
        QTRY_COMPARE(row("c").value("state").toString(),QString("connected"));
        QTRY_VERIFY(row("c").value("visible").toBool());
        QSignalSpy devices(&manager,&MultiSessions::devicesRequested);
        manager.disconnectSession("a");
        QTRY_COMPARE(row("a").value("state").toString(),QString("disconnected"));
        QCOMPARE(devices.count(),0);
        QVERIFY(row("c").value("visible").toBool());
        QCOMPARE(row("c").value("state").toString(),QString("connected"));
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
        QCOMPARE(SessionGraph::branches(identity).first.size(),0);
    }
    void unresponsiveWorkerDoesNotBlockShellOrNextViewer() {
        QTemporaryDir dir;
        auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A");
        auto remote=credential("TEST_CERT_B");
        QJsonObject records;
        for(auto id : {"a","b"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(remote)},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        auto visible=[&](QString id) { for (auto item:manager.sessions()) if(item.toMap().value("id")==id)return item.toMap().value("visible").toBool(); return false; };
        manager.open("a","Hung viewer","127.0.0.1","hung");
        QTRY_VERIFY(visible("a"));
        manager.open("b","Healthy viewer","127.0.0.1");
        int ticks=0; qint64 longestTick=0;
        QElapsedTimer elapsed; elapsed.start();
        QTimer heartbeat; heartbeat.setInterval(10);
        connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks; longestTick=qMax(longestTick,elapsed.restart());}); heartbeat.start();
        for (int i=0;i<15;++i) { manager.select("b"); QTest::qWait(100); }
        QVERIFY(ticks>=75);
        QVERIFY2(longestTick<100,qPrintable(QString("UI heartbeat gap: %1 ms").arg(longestTick)));
        qInfo("UI heartbeat longest gap during stalled handoff: %lld ms", static_cast<long long>(longestTick));
        QVERIFY(!visible("b"));
        QTRY_COMPARE_WITH_TIMEOUT(manager.states().value("a").toString(),QString("error"),1500);
        QTRY_VERIFY(visible("b"));
        QCOMPARE(manager.states().value("b").toString(),QString("connected"));
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void cancellationBeforeLaunch() {
        QTemporaryDir dir; HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        manager.open("a","Fixture","127.0.0.1"); manager.disconnectSession("a");
        QTest::qWait(100); QVERIFY(!manager.busy());
        QCOMPARE(manager.sessions().first().toMap().value("state").toString(),QString("disconnected"));
    }
};
int main(int argc,char**argv) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("DeskPortTests"); QCoreApplication::setApplicationName("MultiSessions");
    if(app.arguments().value(1)=="--session-worker")return worker(app);
    MultiSessionsTest test; return QTest::qExec(&test,argc,argv);
}
#include "multi-sessions.moc"
