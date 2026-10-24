#include <QGuiApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QCryptographicHash>
#include <QtTest>
#include "settings/streamingpreferences.h"
#include "backend/hostports.h"
namespace WMUtils { bool isRunningWayland() { return false; } }
class DevicePreferences : public QObject {
    Q_OBJECT
private slots:
    void publicPortFamilyHasNoFallbackToAnotherHost() {
        for (int port = 0; port <= 65536; ++port) {
            const bool expected = port >= 48989 && port <= 50889 && (port - 48989) % 100 == 0;
            QCOMPARE(DeskPortNetwork::isPrivateBase(port), expected);
        }
    }
    void adjustmentIsDeviceScopedAndDoesNotOverwriteOtherEdits() {
        QCoreApplication::setOrganizationName("DeskPortTest");
        QCoreApplication::setApplicationName("DevicePreferences");
        QTemporaryDir directory;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,directory.path());
        auto global=StreamingPreferences::get();
        QCOMPARE(global->desktopAdjustmentChoices().size(), 10);
        QCOMPARE(global->desktopAdjustmentLabels().size(), 10);
        QCOMPARE(global->desktopAdjustmentChoices().at(5).toDouble(), 1.0);
        for (int i = 0; i < global->desktopAdjustmentChoices().size(); ++i) {
            const auto value = global->desktopAdjustmentChoices().at(i).toDouble();
            QVERIFY(StreamingPreferences::validDesktopAdjustment(value));
            QCOMPARE(global->desktopAdjustmentLabels().at(i).toDouble(), value);
        }
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
    void localTextModeDefaultsToPhysicalAndStaysDeviceScoped() {
        auto global = StreamingPreferences::get();
        QObject owner;
        auto a = global->snapshot("ime-a", &owner);
        auto b = global->snapshot("ime-b", &owner);
        QVERIFY(!a->localTextInput); QVERIFY(!b->localTextInput);
        a->localTextInput = true; a->save();
        a->reload(); b->reload(); global->reload();
        QVERIFY(a->localTextInput); QVERIFY(!b->localTextInput); QVERIFY(!global->localTextInput);
    }
    void globalWindowShortcutDefaultsOnAndCannotBeOverriddenByDevice() {
        auto global = StreamingPreferences::get();
        QVERIFY(global->globalShortcutEnabled);
        QSignalSpy changed(global, &StreamingPreferences::globalShortcutEnabledChanged);
        QVERIFY(global->setProperty("globalShortcutEnabled", false));
        QCOMPARE(changed.count(), 1);
        global->save();
        global->reload();
        QVERIFY(!global->globalShortcutEnabled);
        QObject owner;
        auto device = global->snapshot("shortcut-device", &owner);
        QVERIFY(!device->globalShortcutEnabled);
        device->globalShortcutEnabled = true;
        device->save();
        global->reload();
        QVERIFY(!global->globalShortcutEnabled);
        QVERIFY(global->setProperty("globalShortcutEnabled", true));
        global->save();
        global->reload(); device->reload();
        QVERIFY(global->globalShortcutEnabled);
        QVERIFY(device->globalShortcutEnabled);
    }
    void customShortcutIsValidatedPersistedAndLocalOnly() {
        auto global = StreamingPreferences::get();
        QCOMPARE(global->globalShortcutSequence, QStringLiteral("Meta+'"));
        QSignalSpy changed(global, &StreamingPreferences::globalShortcutSequenceChanged);
        QVERIFY(global->setProperty("globalShortcutSequence", "Ctrl+Alt+K"));
        QCOMPARE(changed.count(), 1);
        global->save(); global->reload();
        QCOMPARE(global->globalShortcutSequence, QStringLiteral("Ctrl+Alt+K"));
        QObject owner;
        auto device = global->snapshot("custom-shortcut-device", &owner);
        device->globalShortcutSequence = "Meta+F8"; device->save();
        global->reload();
        QCOMPARE(global->globalShortcutSequence, QStringLiteral("Ctrl+Alt+K"));
        QSettings raw;
        raw.setValue("ui/globalShortcutSequence", "Shift+A"); raw.sync();
        global->reload();
        QCOMPARE(global->globalShortcutSequence, QStringLiteral("Meta+'"));
        QVERIFY(global->shortcutFromKey(Qt::Key_K, Qt::NoModifier).isEmpty());
        QVERIFY(!global->shortcutDisplay("Ctrl+Alt+K").isEmpty());
    }
    void uiLanguageChangesPreserveDevicePolicyAndSavedIds() {
        auto preferences = StreamingPreferences::get();
        const auto previousLanguage = preferences->language;
        const auto previousDirection = QGuiApplication::layoutDirection();
        const auto resolution = QSize(preferences->width, preferences->height);
        const auto policy = preferences->displayPolicy;
        const auto input = preferences->remoteInput;
        QCOMPARE(int(StreamingPreferences::LANG_KO), 11);
        QCOMPARE(int(StreamingPreferences::LANG_PT), 18);
        QCOMPARE(preferences->sharedLanguages().size(), 22);
        preferences->language = StreamingPreferences::LANG_AR;
        QVERIFY(preferences->retranslate());
        QCOMPARE(QCoreApplication::translate("SettingsHome", "Language"), QString::fromUtf8("اللغة"));
        QCOMPARE(preferences->manualLanguage(), QString("ar"));
        QCOMPARE(QGuiApplication::layoutDirection(), Qt::RightToLeft);
        preferences->language = StreamingPreferences::LANG_BO;
        QVERIFY(preferences->retranslate());
        QCOMPARE(QCoreApplication::translate("SettingsHome", "Language"), QString::fromUtf8("སྐད་ཡིག"));
        QCOMPARE(QGuiApplication::layoutDirection(), Qt::LeftToRight);
        preferences->language = StreamingPreferences::LANG_PT_BR;
        QVERIFY(preferences->retranslate());
        QCOMPARE(QCoreApplication::translate("SettingsHome", "Language"), QString("Idioma"));
        QCOMPARE(QSize(preferences->width, preferences->height), resolution);
        QCOMPARE(preferences->displayPolicy, policy);
        QCOMPARE(preferences->remoteInput, input);
        // A wire token is never a UI lookup key; translators must return it intact.
        QCOMPARE(QCoreApplication::translate("SettingsHome", "video-rate"), QString("video-rate"));
        preferences->language = previousLanguage;
        QVERIFY(preferences->retranslate());
        QGuiApplication::setLayoutDirection(previousDirection);
    }
};
QTEST_MAIN(DevicePreferences)
#include "device-preferences.moc"
