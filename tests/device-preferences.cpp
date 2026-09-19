#include <QGuiApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QCryptographicHash>
#include <QtTest>
#include "settings/streamingpreferences.h"
namespace WMUtils { bool isRunningWayland() { return false; } }
class DevicePreferences : public QObject {
    Q_OBJECT
private slots:
    void adjustmentIsDeviceScopedAndDoesNotOverwriteOtherEdits() {
        QCoreApplication::setOrganizationName("DeskPortTest");
        QCoreApplication::setApplicationName("DevicePreferences");
        QTemporaryDir directory;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,directory.path());
        auto global=StreamingPreferences::get();
        QObject owner;
        auto a=global->snapshot("device-a",&owner);
        auto b=global->snapshot("device-b",&owner);
        QCOMPARE(a->desktopAdjustment,1.0); QCOMPARE(b->desktopAdjustment,1.0);
        a->remoteAudio=false; a->desktopAdjustment=0.5; a->save();
        QVERIFY(StreamingPreferences::saveDesktopAdjustment("device-a",1.5));
        QVERIFY(!StreamingPreferences::saveDesktopAdjustment("device-a",1.1));
        QVERIFY(!StreamingPreferences::saveDesktopAdjustment(QString(),1.2));
        a->reload(); b->reload(); global->reload();
        QCOMPARE(a->desktopAdjustment,1.5); QVERIFY(!a->remoteAudio);
        QCOMPARE(b->desktopAdjustment,1.0); QCOMPARE(global->desktopAdjustment,1.0);
        QVERIFY(StreamingPreferences::saveDesktopAdjustment("DEVICE-A",0.7));
        a->reload(); QCOMPARE(a->desktopAdjustment,0.7);
        QSettings raw;
        const auto prefix="devices/"+QString::fromLatin1(QCryptographicHash::hash("device-a",QCryptographicHash::Sha256).toHex())+"/";
        raw.setValue(prefix+"desktopAdjustment",1.1); raw.sync();
        a->reload(); QCOMPARE(a->desktopAdjustment,1.0);
    }
};
QTEST_MAIN(DevicePreferences)
#include "device-preferences.moc"
