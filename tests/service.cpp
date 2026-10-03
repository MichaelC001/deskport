#include <QtTest>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QApplication>
#include "hostmanager.h"
#include "serviceconfig.h"

class ServiceTests : public QObject {
    Q_OBJECT
private slots:
    void appImageLoginUsesThePersistentDownload() {
        QTemporaryDir dir;
        const QString native = "/tmp/.mount_example/usr/bin/deskport";
        const QString image = dir.path() + "/DeskPort with spaces.AppImage";
        QFile file(image); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\nexit 0\n"); file.close();
        QCOMPARE(DeskPortService::persistentExecutable(native, image), native);
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QCOMPARE(DeskPortService::persistentExecutable(native, image), image);
        QCOMPARE(DeskPortService::persistentExecutable(native, QString()), native);
        QCOMPARE(DeskPortService::persistentExecutable(native, "relative.AppImage"), native);
        QCOMPARE(DeskPortService::persistentExecutable(native, dir.path()), native);
        QVERIFY(QFile::remove(image));
        QCOMPARE(DeskPortService::persistentExecutable(native, image), native);
    }
    void startupSupervisesTheRealProcess() {
        const auto plist = DeskPortService::launchAgent();
        QXmlStreamReader reader(plist); while (!reader.atEnd()) reader.readNext();
        QVERIFY(!reader.hasError());
        QVERIFY(plist.contains("/Applications/DeskPort.app/Contents/MacOS/DeskPort"));
        QVERIFY(!plist.contains("/usr/bin/open"));
        QVERIFY(plist.contains("SuccessfulExit</key><false/>"));
        QVERIFY(plist.contains("--background"));
        const auto unit = DeskPortService::systemdUnit("/tmp/path with spaces/percent%/deskport");
        QVERIFY(unit.contains("ExecStart=\"/tmp/path with spaces/percent%%/deskport\" --background"));
        QVERIFY(unit.contains("Restart=on-failure"));
        QVERIFY(unit.contains("KillMode=mixed"));
        QVERIFY(DeskPortService::desktopEntry().contains("systemctl --user start"));
    }
    void declarativeConfigurationKeepsOwnershipOfLoginStartup() {
        QTemporaryDir dir;
        const QString linked = dir.path() + "/linked.desktop", plain = dir.path() + "/plain.desktop";
        // 悬空也算数: home-manager 的链接在 store 回收后仍是它的地盘。
        QVERIFY(QFile::link("/nix/store/abc-deskport/share/applications/x.desktop", linked));
        QFile file(plain); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        QVERIFY(DeskPortService::storeManaged(linked));
        QVERIFY(!DeskPortService::storeManaged(plain));
        QVERIFY(!DeskPortService::storeManaged(dir.path() + "/missing.desktop"));
        QVERIFY(QFile::link(dir.path() + "/elsewhere", dir.path() + "/other.desktop"));
        QVERIFY(!DeskPortService::storeManaged(dir.path() + "/other.desktop"));
    }
    void guiStartupProtectsCliManualAndEditedUnits() {
        QTemporaryDir dir;
        const auto unit = dir.filePath("io.github.keithxc.DeskPort.service");
        const auto desktop = dir.filePath("io.github.keithxc.DeskPort.desktop");
        const auto write = [&](const QString& path, const QByteArray& bytes) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(!DeskPortService::startupManagedElsewhere(unit, desktop));
        auto generated = DeskPortService::systemdUnit("/old/install/deskport").toUtf8();
        QVERIFY(write(unit, generated));
        QVERIFY(DeskPortService::guiManagedUnit(unit));
        QVERIFY(!DeskPortService::startupManagedElsewhere(unit, desktop));
        const int bodyAt = generated.indexOf("[Unit]");
        auto legacy = generated.mid(bodyAt);
        QVERIFY(write(unit, legacy)); QVERIFY(DeskPortService::guiManagedUnit(unit));
        legacy.replace("KillMode=mixed", "KillMode=control-group");
        QVERIFY(write(unit, legacy)); QVERIFY(DeskPortService::guiManagedUnit(unit));
        auto manualArguments = legacy;
        manualArguments.replace("/old/install/deskport", "/old/install/deskport\" --custom \"/fake/deskport");
        QVERIFY(write(unit, manualArguments)); QVERIFY(!DeskPortService::guiManagedUnit(unit));
        QVERIFY(write(unit, generated.replace("/old/install", "/user/edited")));
        QVERIFY(DeskPortService::startupManagedElsewhere(unit, desktop));
        QVERIFY(write(unit, "# Managed by DeskPort CLI v1\n[Service]\nExecStart=/usr/bin/deskport host run\n"));
        QVERIFY(DeskPortService::startupManagedElsewhere(unit, desktop));
        QVERIFY(write(unit, "[Service]\nExecStart=/manual/deskport --background\n"));
        QVERIFY(DeskPortService::startupManagedElsewhere(unit, desktop));
        QVERIFY(write(unit, DeskPortService::systemdUnit("/usr/bin/deskport").toUtf8()));
        QVERIFY(write(desktop, "[Desktop Entry]\nType=Application\nExec=/manual/deskport\n"));
        QVERIFY(DeskPortService::startupManagedElsewhere(unit, desktop));
        QVERIFY(write(desktop, DeskPortService::desktopEntry().toUtf8()));
        QVERIFY(!DeskPortService::startupManagedElsewhere(unit, desktop));
        QVERIFY(QDir().mkpath(unit + ".d"));
        QVERIFY(write(unit + ".d/manual.conf", "[Service]\nEnvironment=EXAMPLE=1\n"));
        QVERIFY(DeskPortService::startupManagedElsewhere(unit, desktop));
    }
    void installedServiceDoesNotMeanEnabledStartup() {
        QTemporaryDir dir;
        const auto unit = dir.filePath("io.github.keithxc.DeskPort.service");
        const auto desktop = dir.filePath("io.github.keithxc.DeskPort.desktop");
        QFile file(unit); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Service]\nExecStart=/usr/bin/deskport host run\n"); file.close();
        QVERIFY(!DeskPortService::startupEnabled(unit, desktop, {dir.path()}));
        QVERIFY(QDir().mkpath(dir.filePath("default.target.wants")));
        QVERIFY(QFile::link(unit, dir.filePath("default.target.wants/io.github.keithxc.DeskPort.service")));
        QVERIFY(DeskPortService::startupEnabled(unit, desktop, {dir.path()}));
        QVERIFY(QFile::remove(dir.filePath("default.target.wants/io.github.keithxc.DeskPort.service")));
        QVERIFY(!DeskPortService::startupEnabled(unit, desktop, {dir.path()}));
        QFile entry(desktop); QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write(DeskPortService::desktopEntry().toUtf8()); entry.close();
        QVERIFY(DeskPortService::startupEnabled(unit, desktop, {dir.path()}));
        QVERIFY(entry.open(QIODevice::Append)); entry.write("Hidden=true\n"); entry.close();
        QVERIFY(!DeskPortService::startupEnabled(unit, desktop, {dir.path()}));
    }
    void guiConstructorPreservesInstalledCliService() {
#ifdef Q_OS_LINUX
        const auto fixture = qEnvironmentVariable("DESKPORT_SERVICE_TEST_CONFIG");
        QVERIFY2(!fixture.isEmpty() && QDir(fixture).isAbsolute(), "Run via test-host-lifecycle.py --service with an isolated profile");
        const auto unit = DeskPortService::unitPath();
        const auto desktop = DeskPortService::autostartPath();
        QVERIFY(unit.startsWith(fixture + '/')); QVERIFY(desktop.startsWith(fixture + '/'));
        QVERIFY(!QFileInfo::exists(unit)); QVERIFY(!QFileInfo::exists(desktop));
        QVERIFY(QDir().mkpath(QFileInfo(unit).absolutePath()));
        const QByteArray cliUnit = "# Managed by DeskPort CLI v1\n[Service]\nExecStart=/usr/bin/deskport host run\n";
        QFile file(unit); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(cliUnit); file.close();
        QSettings settings;
        settings.setValue("setup/completed", true);
        settings.setValue("host/startAtLogin", true);
        settings.setValue("host/sharingDisabled", true);
        settings.setValue("host/shareOnLaunch", false);
        {
            HostManager host;
            QVERIFY(host.loginStartManaged()); QVERIFY(!host.loginStart());
            QVERIFY(!host.running());
            QCOMPARE(DeskPortService::startupFile(unit), cliUnit);
            host.setLoginStart(true); host.setLoginStart(false);
            QCOMPARE(DeskPortService::startupFile(unit), cliUnit);
            QVERIFY(!QFileInfo::exists(desktop));
            const auto wants = QFileInfo(unit).absolutePath() + "/default.target.wants";
            QVERIFY(QDir().mkpath(wants));
            const auto enabled = wants + '/' + QFileInfo(unit).fileName();
            QVERIFY(QFile::link(unit, enabled)); QVERIFY(host.loginStart());
            QVERIFY(QFile::remove(enabled));
        }
        QVERIFY(QFile::remove(unit));
        settings.clear();
#endif
    }
    void isolatedTestsNeverRegisterSystemServices() {
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.setUnattended(true);
        QVERIFY(!host.unattendedEnabled());
        QVERIFY(!QFile::exists(dir.path() + "/unattended/enabled"));
        host.openUnattendedSettings(); // Must be a no-op in isolated tests.
        host.refreshUnattended();
        QVERIFY(!host.unattendedNeedsApproval());
    }
    void ordinaryQuitHidesUntilExplicitExit() {
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.setResident(true);
        QSignalSpy hide(&host, &HostManager::hideRequested), exit(&host, &HostManager::exitRequested);
        QEvent quit(QEvent::Quit);
        QCoreApplication::sendEvent(qApp, &quit);
        QCOMPARE(hide.size(), 1); QCOMPARE(exit.size(), 0);
        host.requestExit(); QCOMPARE(exit.size(), 1);
    }
    void crashedChildRecoversWithoutChangingIdentity() {
        qputenv("DESKPORT_TEST_MODE", "host-crash-once");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.path()+"/crashed-once"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(host.status().contains("Host stopped (7)"), 5000);
        QFile secret(dir.path()+"/control-secret"); QVERIFY(secret.open(QIODevice::ReadOnly));
        const auto before = secret.readAll(); secret.close();
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 12000);
        QVERIFY(secret.open(QIODevice::ReadOnly)); QCOMPARE(secret.readAll(), before);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QTest::qWait(5500); QVERIFY(!host.running());
    }
    void stopCancelsPendingRecovery() {
        qputenv("DESKPORT_TEST_MODE", "host-fail");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720);
        QTRY_VERIFY_WITH_TIMEOUT(host.status().contains("Host stopped (7)"), 5000);
        host.stop(); qputenv("DESKPORT_TEST_MODE", "normal");
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QTest::qWait(5500); QVERIFY(!host.running());
    }
};
QTEST_MAIN(ServiceTests)
#include "service.moc"
