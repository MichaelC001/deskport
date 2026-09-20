#include <QtTest>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include "hostmanager.h"
#include "nvaddress.h"
#include "localhostfilter.h"
#include "peerstore.h"

class HostLifecycle : public QObject {
    Q_OBJECT
private slots:
    void init() { qputenv("DESKPORT_TEST_MODE", "normal"); }
    void reconnectWaitsForPendingRestore() {
        qputenv("DESKPORT_TEST_MODE", "restore-slow");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy result(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1280, 720, 1, -1));
        QVERIFY(host.resizeDisplay(1920, 1080, 2, 7, 1));
        QVERIFY(!host.resizeDisplay(1920, 1080, 2, 8, 1));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 2, 4000);
        QCOMPARE(result[0][0].toInt(), -1);
        QCOMPARE(result[1][0].toInt(), 7);
        QVERIFY(result[1][3].toString().isEmpty());
        QCOMPARE(result[1][1].toInt(), 1920);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void reconnectAfterFailedRestore() {
        qputenv("DESKPORT_TEST_MODE", "restore-reject");
        QTemporaryDir dir; HostManager host(nullptr, dir.path()); host.start(2560,1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(),5000);
        QSignalSpy result(&host,&HostManager::displayResized);
        QVERIFY(host.resizeDisplay(2560,1440,1,-1));
        QVERIFY(host.resizeDisplay(1920,1888,2,7));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(),2,4000);
        QVERIFY(!result[0][3].toString().isEmpty());
        QCOMPARE(result[1][0].toInt(),7);
        QVERIFY(result[1][3].toString().isEmpty());
        QCOMPARE(result[1][1].toInt(),1920); QCOMPARE(result[1][2].toInt(),1888);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(),5000);
    }
    void disconnectedQueuedControllerNeverTakesOver() {
        qputenv("DESKPORT_TEST_MODE", "restore-slow");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy result(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1280, 720, 1, -1));
        QVERIFY(host.resizeDisplay(1920, 1080, 2, 7, 1));
        host.restoreDisplay();
        QTest::qWait(2000);
        for (const auto& reply : result) QVERIFY(reply[0].toInt() < 0);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void resizeFailuresDoNotStopSharing_data() {
        QTest::addColumn<QByteArray>("mode");
        QTest::newRow("rejected") << QByteArray("resize-reject");
        QTest::newRow("timed-out") << QByteArray("resize-timeout");
    }
    void resizeFailuresDoNotStopSharing() {
        QFETCH(QByteArray, mode);
        qputenv("DESKPORT_TEST_MODE", mode);
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy result(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1920, 1080, 2, 7));
        QVERIFY(!host.resizeDisplay(1600, 1000, 2, 8));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 6500);
        QCOMPARE(result.first().first().toInt(), 7);
        QVERIFY(!result.first().last().toString().isEmpty());
        QVERIFY(host.running()); QVERIFY(!host.changing());
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void ownedLinuxCaptureTracksModeAndCleansUp() {
#ifndef Q_OS_LINUX
        QSKIP("Linux capture descriptor");
#else
        qputenv("DESKPORT_TEST_MODE", "gnome-display");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        const auto path = dir.path() + "/virtual-display.json";
        const auto initial = PeerStore::read(path);
        QCOMPARE(initial["node"].toInt(), 42);
        QCOMPARE(initial["serial"].toString(), QString("142"));
        QCOMPARE(initial["output"].toString(), QString("Meta-1"));
        QSignalSpy results(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1668, 2388, 2, 7));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 5000);
        const auto resized = PeerStore::read(path);
        QCOMPARE(resized["node"].toInt(), 43);
        QCOMPARE(resized["serial"].toString(), QString("143"));
        QCOMPARE(resized["output"].toString(), QString("Meta-2"));
        QCOMPARE(resized["width"].toInt(), 1668);
        QCOMPARE(resized["height"].toInt(), 2388);
        QCOMPARE(resized["scale"].toInt(), 2);
        QFile config(dir.path() + "/sunshine.conf"); QVERIFY(config.open(QIODevice::ReadOnly));
        const auto text = config.readAll();
        QVERIFY(text.contains("capture = portal\n"));
        QVERIFY(text.contains("output_name = Meta-1\n")); // GNOME capture reads the live descriptor.
        host.restoreDisplay();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 4500);
        QCOMPARE(host.displayWidth(), 0);
        QVERIFY(host.adaptiveDisplayAvailable());
        QVERIFY(host.resizeDisplay(1920, 1080, 1, 8));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 5000);
        QCOMPARE(PeerStore::read(path)["node"].toInt(), 43);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(!QFile::exists(path));
#endif
    }
    void idleLinuxStartupWaitsForAdmission() {
#ifndef Q_OS_LINUX
        QSKIP("Linux on-demand lifecycle");
#else
        qputenv("DESKPORT_TEST_MODE", "gnome-idle");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.running(), 5000);
        const auto path = dir.path() + "/virtual-display.json";
        QVERIFY(!QFile::exists(path));
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QCOMPARE(host.displayWidth(), 0);
        QFile config(dir.path() + "/sunshine.conf"); QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("capture = portal\n"));
        QVERIFY(host.resizeDisplay(1920, 1080, 1, 7));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 5000);
        QCOMPARE(PeerStore::read(path)["output"].toString(), QString("Meta-2"));
        host.restoreDisplay();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 5000);
        QCOMPARE(host.displayWidth(), 0);
        QVERIFY(host.running()); QVERIFY(host.adaptiveDisplayAvailable());
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
#endif
    }
    void localHostsAreFilteredWithoutMatchingNamesOrSubnets() {
        const QList<QHostAddress> local{QHostAddress("192.0.2.10"), QHostAddress("2001:db8::10")};
        for (const QString& value : {"127.0.0.1", "127.0.0.2", "::1", "192.0.2.10", "::ffff:192.0.2.10", "2001:db8::10"})
            QVERIFY(DeskPortNetwork::isLocalHostAddress(value, local));
        for (const QString& value : {"", "0.0.0.0", "::", "DeskPort", "192.0.2.11", "2001:db8::11"})
            QVERIFY(!DeskPortNetwork::isLocalHostAddress(value, local));
    }
    void manualAddressesKeepServicesSeparate() {
        QCOMPARE(NvAddress::fromUserInput("203.0.113.10").port(), quint16(48989));
        QCOMPARE(NvAddress::fromUserInput("203.0.113.10:47989").port(), quint16(47989));
        QCOMPARE(NvAddress::fromUserInput("203.0.113.10:49089").port(), quint16(49089));
        QCOMPARE(NvAddress::fromUserInput("[2001:db8::1]:49089").port(), quint16(49089));
        QVERIFY(NvAddress::fromUserInput("203.0.113.10:0").isNull());
    }
    void separateFromNativePortFamily() {
        QVERIFY(!DeskPortNetwork::isPrivateBase(47989));
        QVERIFY(!DeskPortNetwork::isPrivateBase(65535));
        QVERIFY(!DeskPortNetwork::isPrivateBase(DeskPortNetwork::DefaultBasePort + DeskPortNetwork::PortChoices * DeskPortNetwork::PortStep));
        for (int i = 0; i < DeskPortNetwork::PortChoices; ++i) {
            const int base = DeskPortNetwork::DefaultBasePort + i * DeskPortNetwork::PortStep;
            QVERIFY(DeskPortNetwork::isPrivateBase(base));
            QVERIFY(!DeskPortNetwork::isPrivateBase(base - 5));
            QVERIFY(!DeskPortNetwork::isPrivateBase(base + 1));
            QVERIFY(base - 5 > 48010);
            QVERIFY(base + 21 <= 65535);
        }
    }
    void occupiedFamilyMemberIsNotReused_data() {
        QTest::addColumn<bool>("tcp");
        QTest::addColumn<int>("offset");
        for (int offset : DeskPortNetwork::tcpOffsets())
            QTest::newRow(qPrintable(QString("tcp-%1").arg(offset))) << true << offset;
        for (int offset : DeskPortNetwork::udpOffsets())
            QTest::newRow(qPrintable(QString("udp-%1").arg(offset))) << false << offset;
    }
    void occupiedFamilyMemberIsNotReused() {
        QFETCH(bool, tcp);
        QFETCH(int, offset);
        HostPortReservation ports;
        const int original = ports.reserve(DeskPortNetwork::DefaultBasePort, QHostAddress::LocalHost);
        QVERIFY(original != 0);
        ports.release();
        QTcpServer foreignTcp;
        QUdpSocket foreignUdp;
        if (tcp) QVERIFY(foreignTcp.listen(QHostAddress::LocalHost, original + offset));
        else QVERIFY(foreignUdp.bind(QHostAddress::LocalHost, original + offset, QAbstractSocket::DontShareAddress));
        const int selected = ports.reserve(original, QHostAddress::LocalHost);
        QVERIFY(selected != 0);
        QVERIFY(selected != original);
        if (tcp) QVERIFY(foreignTcp.isListening());
        else QCOMPARE(foreignUdp.localPort(), quint16(original + offset));
        ports.release();
        foreignTcp.close(); foreignUdp.close();
        QCOMPARE(ports.reserve(original, QHostAddress::LocalHost), original);
    }
    void exhaustedPortsDoNotLaunchComponents() {
        std::vector<std::unique_ptr<HostPortReservation>> occupied;
        for (int i = 0; i < DeskPortNetwork::PortChoices; ++i) {
            std::unique_ptr<HostPortReservation> ports(new HostPortReservation);
            if (!ports->reserve(DeskPortNetwork::DefaultBasePort, QHostAddress::LocalHost)) break;
            occupied.push_back(std::move(ports));
        }
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 1000);
        QVERIFY(host.status().contains("No free DeskPort port group"));
        QVERIFY(!QFile::exists(state.path() + "/sunshine.conf"));
        QVERIFY(!QFile::exists(state.path() + "/display.log"));
        occupied.clear();
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
    }
    void anotherInstanceCannotChangeActiveHost() {
        QTemporaryDir state;
        HostManager first(nullptr, state.path());
        first.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(first.canPair(), 5000);
        QFile lock(state.path() + "/instance.lock");
        QVERIFY(lock.open(QIODevice::ReadWrite));
        QVERIFY(lock.setFileTime(QDateTime::currentDateTime().addSecs(-120), QFileDevice::FileModificationTime));
        lock.close();
        HostManager second(nullptr, state.path());
        second.start(3840, 2160);
        QVERIFY(!second.running());
        QVERIFY(second.status().contains("Another DeskPort instance"));
        QVERIFY(first.canPair());
        first.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!first.running(), 5000);
        second.start(3840, 2160);
        QTRY_VERIFY_WITH_TIMEOUT(second.canPair(), 5000);
        second.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!second.running(), 5000);
    }
    void chosenPortReachesHostConfiguration() {
        HostPortReservation foreign;
        const int occupied = foreign.reserve(DeskPortNetwork::DefaultBasePort, QHostAddress::LocalHost);
        QVERIFY(occupied != 0);
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        QVERIFY(host.basePort() != occupied);
        QFile config(state.path() + "/sunshine.conf");
        QVERIFY(config.open(QIODevice::ReadOnly));
        const auto contents = config.readAll();
        QVERIFY(contents.contains(QString("port = %1\n").arg(host.basePort()).toUtf8()));
        // Audio availability is host-wide; each client selects whether to receive it.
        QVERIFY(contents.contains("stream_audio = enabled\n"));
        QVERIFY(contents.contains("upnp = disabled\n"));
        QVERIFY(contents.contains("system_tray = disabled\n"));
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
    }
    void startupAndStopStayResponsive() {
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        QVERIFY(host.available());
        QElapsedTimer elapsed; elapsed.start();
        host.start(2560, 1440);
        QVERIFY(elapsed.elapsed() < 200);
        QVERIFY(host.running());
        QVERIFY(!host.canPair());
        int ticks = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
        heartbeat.start(10);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        QVERIFY(ticks >= 10); // Authentication runs for 400 ms in the fake host.
        elapsed.restart(); host.stop();
        QVERIFY(elapsed.elapsed() < 200);
        QVERIFY(!host.canPair());
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QCOMPARE(host.status(), QString("Sharing is off"));
    }
    void authenticationFailureCleansUp() {
        qputenv("DESKPORT_TEST_MODE", "auth-fail");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(host.status().contains("authentication"));
        QVERIFY(!host.canPair());
    }
    void hostFailureAllowsRetry() {
        qputenv("DESKPORT_TEST_MODE", "host-fail");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(host.status().contains("Host stopped (7)"));
        qputenv("DESKPORT_TEST_MODE", "normal");
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
    }
    void earlyDisplayExitCleansUp() {
        qputenv("DESKPORT_TEST_MODE", "display-fail");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(host.status().contains("Virtual display stopped"));
    }
    void displayFailureStopsRunningHost() {
        qputenv("DESKPORT_TEST_MODE", "display-late-fail");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(host.status().contains("Virtual display stopped"));
        QVERIFY(!host.canPair());
    }
    void authenticationTimeoutCleansUp() {
        qputenv("DESKPORT_TEST_MODE", "auth-slow");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 9000);
        QVERIFY(host.status().contains("authentication timed out"));
        QVERIFY(!QFile::exists(state.path() + "/host-started"));
    }
    void stopDuringAuthenticationNeverStartsHost() {
        qputenv("DESKPORT_TEST_MODE", "auth-slow");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(state.path() + "/auth-started"), 3000);
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QTest::qWait(600);
        QVERIFY(!QFile::exists(state.path() + "/host-started"));
        QVERIFY(!host.canPair());
    }
    void unresponsiveHostIsKilledWithoutBlockingUi() {
        qputenv("DESKPORT_TEST_MODE", "stubborn");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(state.path() + "/host-started"), 5000);
        int ticks = 0;
        qint64 longestGap = 0;
        QElapsedTimer pulse; pulse.start();
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        connect(&heartbeat, &QTimer::timeout, [&] {
            ++ticks; longestGap = qMax(longestGap, pulse.restart());
        });
        heartbeat.start(10);
        QElapsedTimer elapsed; elapsed.start(); host.stop();
        QVERIFY(elapsed.elapsed() < 200);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 6000);
        // Hosted runners may coalesce timers. Measure responsiveness, not the
        // number of 10 ms callbacks delivered by a particular machine.
        QVERIFY(elapsed.elapsed() >= 2000); // Exercise the forced-stop grace period.
        QVERIFY(ticks >= 3);
        QVERIFY2(longestGap < 1000, qPrintable(QString::number(longestGap)));
        QCOMPARE(host.status(), QString("Sharing is off"));
    }
};
QTEST_MAIN(HostLifecycle)
#include "host-lifecycle.moc"
