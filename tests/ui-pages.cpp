#include "diagnostics.h"
#include <QDesktopServices>
#include <cmath>
#include <QtTest>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTranslator>
#include <functional>
#include <QAbstractListModel>
#include <QDir>
#include "peermanager.h"
#include "peerstore.h"
#include "singleinstance.h"
#include "../shared/deskport-core/portable/include/deskport/catalog.h"

static QQuickItem* findVisual(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) return item;
    for (auto child : item->childItems()) if (auto found = findVisual(child, name)) return found;
    return nullptr;
}
static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
class TestSession : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_PROPERTY(QString hostId READ hostId CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    QString hostId() const { return "device-a"; }
    QString hostName() const { return "Studio"; }
    Q_INVOKABLE QVariantMap traffic() const { return {{"received",862000000.0},{"sent",18000000.0}}; }
    Q_PROPERTY(bool viewerReady READ viewerReady NOTIFY viewerReadyChanged)
    bool nativeReady = false, viewerRequested = true;
    bool viewerReady() const { return nativeReady; }
    Q_INVOKABLE void setViewerRequested(bool visible) { viewerRequested = visible; }
    void makeViewerReady() { nativeReady = true; emit viewerReadyChanged(); }
    int delay = 0; bool cancelled = false;
    Q_INVOKABLE int retryDelay() const { return delay; }
    Q_INVOKABLE void cancelRecovery() { cancelled = true; }
    int executions = 0;
    QQuickWindow* receivedWindow = nullptr;
    TestSession* next = nullptr;
    std::function<void()> duringExec;
    Q_INVOKABLE bool adaptiveRestartPending() const { return next != nullptr; }
    Q_INVOKABLE TestSession* adaptiveContinuation() { auto value = next; next = nullptr; return value; }
    Q_INVOKABLE void exec(QQuickWindow* window) { receivedWindow = window; ++executions; if (duringExec) duringExec(); }
signals:
    void viewerReadyChanged();
    void stageStarting(QString stage);
    void stageFailed(QString stage, int error, QString ports);
    void connectionStarted();
    void displayLaunchError(QString text);
    void displayLaunchWarning(QString text);
    void quitStarting();
    void sessionFinished(int result);
    void readyForDeletion();
};
class TestComputers : public QAbstractListModel {
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;
    Q_INVOKABLE void initialize(QObject*) {}
    Q_INVOKABLE void refreshFavorites() {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 2; }
    QHash<int,QByteArray> roleNames() const override {
        return {{Qt::UserRole,"name"},{Qt::UserRole+1,"hostId"},{Qt::UserRole+2,"online"},
        {Qt::UserRole+3,"paired"},{Qt::UserRole+4,"statusUnknown"},{Qt::UserRole+5,"address"},
        {Qt::UserRole+6,"favorite"},{Qt::UserRole+7,"sourceIndex"},{Qt::UserRole+8,"details"},
        {Qt::UserRole+11,"operatingSystem"},{Qt::UserRole+9,"serverSupported"},{Qt::UserRole+10,"wakeable"}};
    }
    QVariant data(const QModelIndex& index,int role) const override {
        const bool first = index.row() == 0;
        switch(role-Qt::UserRole) {
        case 0: return first ? "Studio" : "Travel laptop";
        case 1: return first ? "device-a" : "device-b";
        case 2: case 3: case 9: return true;
        case 4: case 10: return false;
        case 5: return "example.invalid";
        case 6: return first;
        case 7: return first ? 1 : 0;
        case 8: return "Synthetic device details";
        case 11: return first ? "macOS" : "NixOS";
        default: return {};
        }
    }
signals:
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result,QString ports);
};
static QString desktopTestState;
static TestSession* desktopTestSession;
static int desktopCreateCalls;
class TestDesktopApps : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void initialize(QObject*, int, bool) {}
    Q_INVOKABLE QVariantMap desktopTarget() const {
        return {{"state", desktopTestState}, {"index", 0}, {"name", "Desktop"}, {"resume", false}};
    }
    Q_INVOKABLE TestSession* createSessionForApp(int) { ++desktopCreateCalls; return desktopTestSession; }
signals:
    void computerLost();
};
class TestPreferences : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_PROPERTY(QVariantList desktopAdjustmentChoices READ adjustmentChoices CONSTANT)
    Q_PROPERTY(QStringList desktopAdjustmentLabels READ adjustmentLabels CONSTANT)
    QVariantList adjustmentChoices() const {
        QVariantList values;
        for (double value : dp_catalog_tuning_values) values.append(value);
        return values;
    }
    QStringList adjustmentLabels() const {
        QStringList labels;
        for (const char *label : dp_catalog_tuning_labels) labels.append(QString::fromLatin1(label));
        return labels;
    }
    enum Language { LANG_AUTO, LANG_EN, LANG_FR, LANG_ZH_CN, LANG_DE, LANG_NB_NO, LANG_RU, LANG_ES, LANG_JA, LANG_VI, LANG_TH, LANG_KO, LANG_HU, LANG_NL, LANG_SV, LANG_TR, LANG_UK, LANG_ZH_TW, LANG_PT, LANG_PT_BR, LANG_EL, LANG_IT, LANG_HI, LANG_PL, LANG_CS, LANG_HE, LANG_CKB, LANG_LT, LANG_ET };
    Q_ENUM(Language)
};
class PreviewHost : public HostManager {
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ previewName CONSTANT)
    Q_PROPERTY(QVariantList permissions READ previewPermissions NOTIFY permissionsChanged)
    Q_PROPERTY(QString readiness READ previewReadiness NOTIFY permissionsChanged)
public:
    using HostManager::HostManager;
    QString previewName() const { return "This computer"; }
    QString previewReadiness() const { return "attention"; }
    QVariantList previewPermissions() const {
        return {QVariantMap{{"key","screen"},{"title","Screen recording"},{"purpose","Synthetic screen permission"},{"state","allowed"}},
                QVariantMap{{"key","input"},{"title","Keyboard and pointer"},{"purpose","Synthetic input permission"},{"state","needsSetup"}}};
    }
};
class UiPages : public QObject {
    Q_OBJECT
    QUrl feedbackUrl;
public slots:
    void captureFeedback(const QUrl& url) { feedbackUrl=url; }
private slots:
    void diagnosticsControls() {
        QTemporaryDir dir;
        Diagnostics logs(nullptr,dir.path()+"/diagnostics");
        QQmlEngine engine;
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        engine.rootContext()->setContextProperty("diagnostics",&logs);
        QQmlComponent component(&engine,QUrl::fromLocalFile(gui+"/SettingsHome.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
        auto item=qobject_cast<QQuickItem*>(page.data()); QVERIFY(item);
        QQuickWindow window; window.resize(660,900);
        item->setParentItem(window.contentItem()); item->setSize(QSizeF(660,900));
        window.show(); QTest::qWait(100);
        auto toggle=page->findChild<QQuickItem*>("diagnosticsSwitch"); QVERIFY(toggle);
        QVERIFY(!toggle->property("checked").toBool());
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2)).toPoint());
        QTRY_VERIFY(logs.enabled());
        auto button=page->findChild<QObject*>("feedbackButton"); QVERIFY(button);
        auto notice=page->findChild<QObject*>("diagnosticsPublicNotice"); QVERIFY(notice);
        QVERIFY(notice->property("text").toString().contains("public"));
        QDesktopServices::setUrlHandler("https",this,"captureFeedback");
        QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        QVERIFY(QFile::exists(logs.bundlePath()));
        QCOMPARE(feedbackUrl,Diagnostics::issueUrl());
        QVERIFY(!feedbackUrl.toString().contains(dir.path()));
        QDesktopServices::unsetUrlHandler("https");
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2)).toPoint());
        QTRY_VERIFY(!logs.enabled());
        const QString shots=qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS");
        if (!shots.isEmpty()) { QDir().mkpath(shots); QTest::qWait(100); QVERIFY(window.grabWindow().save(shots+"/diagnostics.png")); }
    }

    void repeatedLaunchActivatesOnlyTheOwner() {
        QTemporaryDir directory;
        int activations = 0;
        {
            SingleInstance owner;
            owner.activate = [&] { ++activations; };
            QVERIFY(owner.start(directory.path()));
            for (int i = 0; i < 3; ++i) {
                SingleInstance duplicate;
                QVERIFY(!duplicate.start(directory.path()));
                QVERIFY(duplicate.delivered);
                QTRY_COMPARE(activations, i + 1);
            }
            SingleInstance background;
            QVERIFY(!background.start(directory.path(), false));
            QVERIFY(background.delivered);
            QTest::qWait(50);
            QCOMPARE(activations, 3);
        }
        SingleInstance restarted;
        QVERIFY(restarted.start(directory.path()));
    }
    void initTestCase() {
        qmlRegisterType<TestComputers>("ComputerModel",1,0,"ComputerModel");
        qmlRegisterSingletonType<QObject>("AutoUpdateChecker",1,0,"AutoUpdateChecker",+[](QQmlEngine*,QJSEngine*) -> QObject* { return new QObject; });
        qmlRegisterType<TestSession>("Session",1,0,"Session");
        qmlRegisterType<TestDesktopApps>("AppModel",1,0,"AppModel");
        qmlRegisterSingletonType<QObject>("SdlGamepadKeyNavigation",1,0,"SdlGamepadKeyNavigation",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property int enables: 0; function enable() { enables++ } function disable() {} function getConnectedGamepads() { return 0 } }",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("ComputerManager",1,0,"ComputerManager",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { signal quitAppCompleted(var error); signal computerAddCompleted(bool success, bool blocked); function startPolling() {} function stopPollingAsync() {} }",QUrl()); return c.create();
        });
        qmlRegisterType<TestPreferences>("TestPreferences",1,0,"TestPreferences");
        qmlRegisterSingletonType<TestPreferences>("StreamingPreferences",1,0,"StreamingPreferences",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine);
            c.setData(R"(import QtQuick 2.9
import TestPreferences 1.0
TestPreferences {
 property int displayPolicy: 0
 property double desktopAdjustment: 1.0
 property string deviceId: ""; property bool remoteAudio: true; property bool remoteInput: true
 property int uiAccent: 1; property bool showTraffic: true; property int uiTheme: 0; property bool compactDevices: true; property int uiDisplayMode: 0
 property int language: 1; property int retranslations: 0
 function retranslate() { retranslations++; return true }
 property int width: 2048; property int height: 1152; property int fps: 75; property int bitrateKbps: 125000
 property int windowMode: 2; property int captureSysKeysMode: 1; property int saves: 0
 property bool smartStreaming: true; property bool framePacing: false; property bool showPerformanceOverlay: false
 property bool sharedClipboard: false; property bool showLocalCursor: true; property bool adaptiveResolution: true; property bool enableVsync: true; property bool absoluteMouseMode: true; property bool reverseScrollDirection: false
 property bool muteOnFocusLoss: true; property bool playAudioOnHost: false; property bool enableMdns: true; property bool keepAwake: true
 function forDevice(id) { deviceId = id; return this }
 function save() { saves++ }
})",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("SystemProperties",1,0,"SystemProperties",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property bool systemDark: false; property color systemAccent: \"#3269d7\"; property bool hasBrowser: false; property bool hasDesktopEnvironment: true; property bool isWow64: false; property bool hasHardwareAcceleration: true; property bool isRunningXWayland: false; property string unmappedGamepads: \"\"; property string friendlyNativeArchName: \"test\"; property string versionString: \"test\" }",QUrl()); return c.create();
        });
    }
    void continuationDuringNestedEventLoop() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession first, next;
        QQmlEngine::setObjectOwnership(&first, QQmlEngine::CppOwnership);
        QQmlEngine::setObjectOwnership(&next, QQmlEngine::CppOwnership);
        engine.rootContext()->setContextProperty("testSession", &first);
        first.duringExec = [&] {
            first.next = &next;
            emit first.sessionFinished(0);
            QTimer::singleShot(0, &first, [&] { emit first.readyForDeletion(); });
            QTest::qWait(100);
        };
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
import SdlGamepadKeyNavigation 1.0
ApplicationWindow {
 id: window; width: 800; height: 600
 property int navigationEnables: SdlGamepadKeyNavigation.enables
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/nested-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTRY_COMPARE(next.executions,1);
        QCOMPARE(root->property("navigationEnables").toInt(), 0);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void continuationAfterDeferredCleanup() {
        // Real sessions return from exec() first; readyForDeletion arrives
        // later from the cleanup worker. The continuation must still start.
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession first, next;
        QQmlEngine::setObjectOwnership(&first, QQmlEngine::CppOwnership);
        QQmlEngine::setObjectOwnership(&next, QQmlEngine::CppOwnership);
        engine.rootContext()->setContextProperty("testSession", &first);
        first.duringExec = [&] {
            first.next = &next;
            emit first.sessionFinished(0);
            QTimer::singleShot(50, &first, [&] { emit first.readyForDeletion(); });
        };
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
import SdlGamepadKeyNavigation 1.0
ApplicationWindow {
 id: window; width: 800; height: 600
 property int navigationEnables: SdlGamepadKeyNavigation.enables
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/deferred-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTRY_COMPARE(next.executions,1);
        QCOMPARE(root->property("navigationEnables").toInt(), 0);
        QCOMPARE(next.receivedWindow, qobject_cast<QQuickWindow*>(root.data()));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void streamAndQuitActivateWithoutLegacyToolbar() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession session;
        engine.rootContext()->setContextProperty("testSession", &session);
        // No toolBar in this context: exercise activation, not just page creation.
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 property int quits: 0
 property bool navigationVisible: !stackView.currentItem || stackView.currentItem.hidesNavigation !== true
 property alias currentPage: stackView.currentItem
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function startStream() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), { session: testSession, appName: "Test" }, StackView.Immediate) }
 function startQuit() { stackView.push(Qt.resolvedUrl("QuitSegue.qml"), { appName: "Test", quitRunningAppFn: function() { window.quits++ } }, StackView.Immediate) }
 function back() { stackView.pop(StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/test-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"startStream"));
        QTRY_COMPARE(session.executions,1);
        QVERIFY(!root->property("navigationVisible").toBool());
        emit session.stageStarting("Handshake");
        auto page=root->property("currentPage").value<QObject*>(); QVERIFY(page);
        QCOMPARE(page->property("stageText").toString(),QString("Starting Handshake..."));
        TestSession continuation;
        QQmlEngine::setObjectOwnership(&continuation, QQmlEngine::CppOwnership);
        session.next = &continuation;
        emit session.sessionFinished(0);
        QCOMPARE(root->property("currentPage").value<QObject*>(), page);
        emit session.readyForDeletion();
        QTRY_COMPARE(continuation.executions,1);
        QVERIFY(!root->property("navigationVisible").toBool());
        QVERIFY(root->property("currentPage").value<QObject*>() != page);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
        QVERIFY(root->property("navigationVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"startQuit"));
        QTRY_COMPARE(root->property("quits").toInt(),1);
        QVERIFY(!root->property("navigationVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
        QVERIFY(root->property("navigationVisible").toBool());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void reconnectDelayCanBeCancelled() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession session; session.delay = 10000;
        engine.rootContext()->setContextProperty("testSession", &session);
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 property alias depth: stackView.depth
 property alias currentPage: stackView.currentItem
 QtObject { id: streamSegueErrorDialog; property string text: ""; property bool quitAfter: false; function open() {} }
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
 function showDevices() { stackView.push(controlPage, StackView.Immediate) }
 Component { id: controlPage; Item { property bool controlCenterForActiveSession: true } }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/control-center-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTest::qWait(100);
        QCOMPARE(session.executions,0);
        auto cancel=root->findChild<QObject*>("cancelReconnect"); QVERIFY(cancel);
        QVERIFY(QMetaObject::invokeMethod(cancel,"clicked"));
        QVERIFY(session.cancelled);
        QTRY_COMPARE(session.executions,1); // Normal cleanup owner runs exactly once.
        emit session.sessionFinished(0);
        emit session.readyForDeletion();
        QTest::qWait(150);
        QCOMPARE(session.executions,1);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void controlCenterDoesNotReplaceActiveSession() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession session;
        engine.rootContext()->setContextProperty("testSession", &session);
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 property alias depth: stackView.depth
 property alias currentPage: stackView.currentItem
 QtObject { id: streamSegueErrorDialog; property string text: ""; property bool quitAfter: false; function open() {} }
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
 function showDevices() { stackView.push(controlPage, StackView.Immediate) }
 Component { id: controlPage; Item { property bool controlCenterForActiveSession: true } }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/control-center-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTRY_COMPARE(session.executions,1);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevices"));
        QCOMPARE(session.receivedWindow, qobject_cast<QQuickWindow*>(root.data()));
        root->setProperty("visible", true);
        emit session.connectionStarted();
        QVERIFY(root->property("visible").toBool());
        QCOMPARE(session.executions, 1);
        QCOMPARE(root->property("depth").toInt(),3);
        QVERIFY(root->property("currentPage").value<QObject*>()->property("controlCenterForActiveSession").toBool());
        emit session.sessionFinished(0);
        QTRY_COMPARE(root->property("depth").toInt(),1);
        QTest::qWait(50);
        engine.collectGarbage();
        emit session.readyForDeletion();
        QTest::qWait(20);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void desktopShortcut_data() {
        QTest::addColumn<QString>("state");
        for (const auto& value : {"ready", "waiting", "background", "missing", "busy", "cancel"}) QTest::newRow(value) << QString(value);
    }
    void desktopShortcut() {
        QFETCH(QString, state);
        desktopTestState = (state == "cancel" || state == "background") ? "waiting" : state; desktopCreateCalls = 0;
        QQmlEngine engine;
        TestSession session; desktopTestSession = &session;
        QQmlEngine::setObjectOwnership(&session, QQmlEngine::CppOwnership);
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 property alias currentPage: stackView.currentItem
 property alias depth: stackView.depth
 QtObject { id: streamSegueErrorDialog; property string text: ""; property bool quitAfter: false; function open() {} }
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("DesktopSegue.qml"), {computerIndex: 0}, StackView.Immediate) }
 function showDevices() { stackView.push(controlPage, StackView.Immediate) }
 Component { id: controlPage; Item { property bool controlCenterForActiveSession: true } }
 function back() { stackView.pop(StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/desktop-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        if (state == "cancel") {
            QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
            desktopTestState = "ready";
            QTest::qWait(300);
            QCOMPARE(desktopCreateCalls,0);
            QCOMPARE(root->property("depth").toInt(),1);
            return;
        }
        if (state == "background") QVERIFY(QMetaObject::invokeMethod(root.data(), "showDevices"));
        if (state == "waiting" || state == "background") {
            QTest::qWait(250);
            QCOMPARE(desktopCreateCalls,0);
            desktopTestState = "ready";
        } else if (state != "ready") {
            QCOMPARE(desktopCreateCalls,0);
            auto page=root->property("currentPage").value<QObject*>(); QVERIFY(page);
            QTRY_VERIFY(!page->property("errorText").toString().isEmpty());
            QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
            QCOMPARE(root->property("depth").toInt(),1);
            return;
        }
        QTRY_COMPARE(session.executions,1);
        QCOMPARE(desktopCreateCalls,1);
        QCOMPARE(root->property("depth").toInt(), state == "background" ? 3 : 2);
        emit session.sessionFinished(0);
        QTRY_COMPARE(root->property("depth").toInt(),1);
        QTest::qWait(250);
        QCOMPARE(desktopCreateCalls,1);
    }
    void pagesLoadWithoutChangingAccessOrSettings() {
        QTemporaryDir dir;
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/peers",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
        engine.rootContext()->setContextProperty("hostManager",&host);
        engine.rootContext()->setContextProperty("peerManager",&peers);
        QQuickWindow window;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        QQmlComponent prefsAccess(&engine);
        prefsAccess.setData("import QtQuick 2.9; import StreamingPreferences 1.0; QtObject { property var prefs: StreamingPreferences }",QUrl());
        QScopedPointer<QObject> access(prefsAccess.create()); QVERIFY(access);
        auto prefs=access->property("prefs").value<QObject*>(); QVERIFY(prefs);
        for(const auto& name: {"SetupView","BindView","HostView","SettingsHome"}) {
            QQmlComponent component(&engine,QUrl::fromLocalFile(gui+"/"+name+".qml"));
            QVERIFY2(component.isReady(),qPrintable(component.errorString()));
            QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
            auto item=qobject_cast<QQuickItem*>(page.data()); QVERIFY(item);
            item->setParentItem(window.contentItem()); item->setWidth(780); item->setHeight(620);
            QTest::qWait(30);
            QVERIFY(page->property("contentHeight").toReal() > 0);
            QVERIFY(peers.peers().isEmpty()); QVERIFY(!peers.busy()); QVERIFY(!host.running());
            QCOMPARE(prefs->property("width").toInt(),2048);
            QCOMPARE(prefs->property("fps").toInt(),75);
            QCOMPARE(prefs->property("bitrateKbps").toInt(),125000);
            QCOMPARE(prefs->property("saves").toInt(),0);
            if(QString(name)=="SettingsHome") {
                QVERIFY(!page->findChild<QObject*>("resolutionChoice"));
                QVERIFY(!page->findChild<QObject*>("settingsSections"));
                auto themeChoice=page->findChild<QObject*>("themeChoice"); QVERIFY(themeChoice);
                QVERIFY(QMetaObject::invokeMethod(themeChoice,"activated",Q_ARG(int,2)));
                QCOMPARE(prefs->property("uiTheme").toInt(),2);
                auto accent=page->findChild<QObject*>("accentChoice"); QVERIFY(accent);
                QVERIFY(QMetaObject::invokeMethod(accent,"activated",Q_ARG(int,3)));
                QCOMPARE(prefs->property("uiAccent").toInt(),3);
                auto traffic=page->findChild<QObject*>("showTrafficSwitch"); QVERIFY(traffic);
                traffic->setProperty("checked",false);
                QVERIFY(QMetaObject::invokeMethod(traffic,"clicked"));
                QVERIFY(!prefs->property("showTraffic").toBool());
                QCOMPARE(prefs->property("bitrateKbps").toInt(),125000);
                QObject* languages=page->findChild<QObject*>("languageChoice"); QVERIFY(languages);
                QVERIFY(QMetaObject::invokeMethod(languages,"activated",Q_ARG(int,2)));
                QCOMPARE(prefs->property("language").toInt(),3);
                QCOMPARE(prefs->property("retranslations").toInt(),1);

            }
        }
        const auto bad=warnings.filter(QRegularExpression("ReferenceError|TypeError|binding loop|Binding loop|Cannot assign|Unable to assign|Missing parent|Component is not ready"));
        QVERIFY2(bad.isEmpty(),qPrintable(bad.join('\n')));
    }
    void themeFollowsSystemAndAllowsIndependentOverrides() {
        QQmlEngine engine;
        QQmlComponent component(&engine,QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(component.create()); QVERIFY(theme);
        theme->setProperty("systemDark",true); QVERIFY(theme->property("dark").toBool());
        theme->setProperty("mode",1); QVERIFY(!theme->property("dark").toBool());
        theme->setProperty("mode",2); theme->setProperty("systemDark",false); QVERIFY(theme->property("dark").toBool());
        theme->setProperty("mode",0); QVERIFY(!theme->property("dark").toBool());
        theme->setProperty("systemAccent",QColor("#8055bf"));
        QCOMPARE(theme->property("baseAccent").value<QColor>(),QColor("#8055bf"));
        theme->setProperty("accentMode",1);
        auto fixed=theme->property("baseAccent");
        theme->setProperty("systemAccent",QColor("#23754f")); QCOMPARE(theme->property("baseAccent"),fixed);
        for(int mode=1;mode<=2;++mode) for(int accent=0;accent<=4;++accent) {
            theme->setProperty("mode",mode); theme->setProperty("accentMode",accent);
            auto color=theme->property("accent").value<QColor>();
            auto text=theme->property("accentText").value<QColor>();
            auto luminance=[](QColor c) {
                auto f=[](double v){ return v<=0.04045 ? v/12.92 : std::pow((v+0.055)/1.055,2.4); };
                return 0.2126*f(c.redF())+0.7152*f(c.greenF())+0.0722*f(c.blueF());
            };
            double a=luminance(color),b=luminance(text);
            QVERIFY((qMax(a,b)+0.05)/(qMin(a,b)+0.05)>=4.5);
        }
    }
    void savedDeviceAddressCanBeEditedFromSettings() {
        QTemporaryDir dir;
        const auto path = dir.path() + "/peers";
        QVERIFY(QDir().mkpath(path));
        QVERIFY(PeerStore::write(path + "/peers.json", {{"version", 1}, {"peers", QJsonObject{
            {"saved", QJsonObject{{"hostId", "device-a"}, {"name", "Studio"}, {"address", "192.0.2.1"},
                {"hostPort", 48989}, {"bindingPort", 48991}, {"hostCert", "pinned"}, {"ready", true}}}}}}));
        HostManager host(nullptr, dir.path() + "/host");
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), path, 0, QHostAddress::LocalHost);
        QQmlEngine engine;
        const auto gui = qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine, QUrl::fromLocalFile(gui + "/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui", theme.data());
        engine.rootContext()->setContextProperty("peerManager", &peers);
        QQmlComponent component(&engine);
        component.setData("import QtQuick 2.9; import StreamingPreferences 1.0; DeviceSettings { preferences: StreamingPreferences; deviceName: \"Studio\"; deviceId: \"device-a\" }", QUrl::fromLocalFile(gui + "/address-test.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page, qPrintable(component.errorString()));
        QQuickWindow window; window.resize(800, 720);
        auto item = qobject_cast<QQuickItem*>(page.data()); QVERIFY(item);
        item->setParentItem(window.contentItem()); item->setSize({800, 720});
        auto open = page->findChild<QObject*>("changeDeviceAddress"); QVERIFY(open);
        QVERIFY(QMetaObject::invokeMethod(open, "clicked"));
        auto address = page->findChild<QObject*>("editPeerAddress"); QVERIFY(address);
        QCOMPARE(address->property("text").toString(), QString("192.0.2.1"));
        auto save = page->findChild<QObject*>("savePeerAddress"); QVERIFY(save);
        const auto original = PeerStore::read(path + "/peers.json");
        address->setProperty("text", "https://bad.example/path");
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(PeerStore::read(path + "/peers.json"), original);
        address->setProperty("text", "desktop.example");
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        const auto peer = peers.peers().first().toMap();
        QCOMPARE(peer["address"].toString(), QString("desktop.example"));
        QCOMPARE(peer["hostCert"].toString(), QString("pinned"));
        QCOMPARE(peer["hostId"].toString(), QString("device-a"));
        QCOMPARE(peer["hostPort"].toInt(), 48989);
        auto label = page->findChild<QObject*>("savedDeviceAddress"); QVERIFY(label);
        QCOMPARE(label->property("text").toString(), QString("desktop.example"));
        QVERIFY(QMetaObject::invokeMethod(open, "clicked"));
        QCOMPARE(address->property("text").toString(), QString("desktop.example"));
    }
    void deviceSettingsAreSeparate() {
        QTemporaryDir directory;
        PreviewHost host(nullptr,directory.path()+"/host");
        PeerManager manager(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),directory.path()+"/binding",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("peerManager",&manager);
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        QQmlComponent accessComponent(&engine);
        accessComponent.setData("import QtQuick 2.9; import StreamingPreferences 1.0; QtObject { property var prefs: StreamingPreferences }",QUrl());
        QScopedPointer<QObject> access(accessComponent.create());
        auto prefs=access->property("prefs").value<QObject*>(); QVERIFY(prefs);
        prefs->setProperty("deviceId","synthetic-device");
        engine.rootContext()->setContextProperty("testPrefs",prefs);
        QQmlComponent component(&engine);
        component.setData("import QtQuick 2.9; DeviceSettings { preferences: testPrefs; deviceName: \"Studio\" }",QUrl::fromLocalFile(gui+"/device-test.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
        QVERIFY(!page->findChild<QObject*>("themeChoice"));
        auto mode=page->findChild<QObject*>("devicePictureMode"); QVERIFY(mode);
        QCOMPARE(prefs->property("saves").toInt(),0);
        QVERIFY(QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,3)));
        QCOMPARE(prefs->property("fps").toInt(),30);
        QCOMPARE(prefs->property("bitrateKbps").toInt(),5000);
        QVERIFY(!prefs->property("smartStreaming").toBool());
        QVERIFY(QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,0)));
        QVERIFY(prefs->property("smartStreaming").toBool());
        QCOMPARE(prefs->property("uiTheme").toInt(),0);
        auto policy=page->findChild<QObject*>("deviceDisplayPolicy"); QVERIFY(policy);
        QCOMPARE(policy->property("currentIndex").toInt(),0);
        policy->setProperty("currentIndex",2);
        QVERIFY(QMetaObject::invokeMethod(policy,"activated",Q_ARG(int,2)));
        QCOMPARE(prefs->property("displayPolicy").toInt(),2);
        auto adjustment=page->findChild<QObject*>("deviceDesktopAdjustment"); QVERIFY(adjustment);
        QCOMPARE(adjustment->property("currentIndex").toInt(),5);
        QVERIFY(QMetaObject::invokeMethod(adjustment,"activated",Q_ARG(int,0)));
        QCOMPARE(prefs->property("desktopAdjustment").toDouble(),0.5);
        QVERIFY(QMetaObject::invokeMethod(adjustment,"activated",Q_ARG(int,9)));
        QCOMPARE(prefs->property("desktopAdjustment").toDouble(),1.5);
        QVERIFY(page->findChild<QObject*>("changeDeviceAddress"));
        QVERIFY(page->findChild<QObject*>("deviceAdvancedButton"));
    }
    void navigationAndDeviceIdentity_data() {
        QTest::addColumn<QString>("outcome");
        for (auto name : {"streaming", "failure", "cancel"}) QTest::newRow(name) << QString(name);
    }
    void navigationAndDeviceIdentity() {
        QFETCH(QString, outcome);
        QTemporaryDir directory;
        PreviewHost host(nullptr,directory.path()+"/host");
        PeerManager peers(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),directory.path()+"/peers",0,QHostAddress::LocalHost);
        TestSession session;
        QQmlEngine::setObjectOwnership(&session,QQmlEngine::CppOwnership);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
        engine.rootContext()->setContextProperty("hostManager", &host);
        engine.rootContext()->setContextProperty("peerManager", &peers);
        engine.rootContext()->setContextProperty("initialView", QString("qrc:/gui/PcView.qml"));
        engine.rootContext()->setContextProperty("startInBackground", true);
        engine.rootContext()->setContextProperty("startSharingPage", false);
        engine.rootContext()->setContextProperty("testSession", &session);
        QFile source(qEnvironmentVariable("TEST_GUI_DIR")+"/main.qml"); QVERIFY(source.open(QIODevice::ReadOnly));
        auto qml=source.readAll();
        qml.insert(qml.lastIndexOf('}'), R"(
 function testStart() { stackView.push("qrc:/gui/StreamSegue.qml", {session:testSession, appName:"Desktop"}, StackView.Immediate) }
 function testSettings() { showDevices(); navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
 function testSharing() { showDevices(); navigateTo("qrc:/gui/HostView.qml", "HostView") }
 function testGrid() { return stackView.currentItem }
 function testDevice() { stackView.push("qrc:/gui/DeviceSettings.qml", {preferences: StreamingPreferences.forDevice("device-a"), deviceName: "Studio"}, StackView.Immediate) }
 function testCards() { StreamingPreferences.compactDevices = false }
 function testSameSettings() { navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
 property alias testDepth: stackView.depth
 property alias testCurrentPage: stackView.currentItem
)");
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        QQmlComponent component(&engine); component.setData(qml,QUrl("qrc:/gui/main.qml"));
        QScopedPointer<QObject> root(component.create()); QVERIFY2(root,qPrintable(component.errorString()));
        auto window=qobject_cast<QQuickWindow*>(root.data()); QVERIFY(window);
        window->resize(800,620); window->show(); QTest::qWait(100);
        QCOMPARE(root->property("testDepth").toInt(),2);
        // Discard only initial setup navigation, which is scheduled once.
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevices"));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"testStart"));
        QTRY_COMPARE(session.executions,1);
        QCOMPARE(root->property("activeHostId").toString(),QString("device-a"));
        // Hold transport/window creation pending: returning must keep progress
        // visible even after the connection callback (which is not window-ready).
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
        QVERIFY(!session.viewerRequested);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"prepareViewerRecall"));
        QVERIFY(window->isVisible());
        QVERIFY(session.viewerRequested);
        emit session.connectionStarted();
        QVERIFY(window->isVisible());
        QTest::qWait(250);
        QVERIFY(window->isVisible());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
        if (outcome != "streaming") {
            if (outcome == "cancel") {
                QVERIFY(QMetaObject::invokeMethod(root.data(),"prepareViewerRecall"));
                auto cancel = root->findChild<QObject*>("cancelReconnect"); QVERIFY(cancel);
                QVERIFY(QMetaObject::invokeMethod(cancel,"clicked"));
                QVERIFY(session.cancelled);
            } else emit session.stageFailed("Synthetic delayed host", -1, "");
            emit session.sessionFinished(0);
            QTRY_COMPARE(root->property("testDepth").toInt(),1);
            QVERIFY(window->isVisible());
            emit session.readyForDeletion();
            QTest::qWait(50);
            session.makeViewerReady(); // Late callback cannot hide Devices.
            QVERIFY(window->isVisible());
            QCOMPARE(session.executions,1);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            return;
        }
        session.makeViewerReady();
        QVERIFY(window->isVisible()); // Late readiness must not steal Devices.
        QVERIFY(QMetaObject::invokeMethod(root.data(),"prepareViewerRecall"));
        QVERIFY(!window->isVisible());
        int recalls=0;
        connect(&host,&HostManager::viewerRecallRequested,this,[&]{recalls++;});
        for(int i=0;i<50;++i) {
            QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
            QCOMPARE(root->property("testDepth").toInt(),3);
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSettings"));
            QCOMPARE(root->property("testDepth").toInt(),4);
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSameSettings"));
            QCOMPARE(root->property("testDepth").toInt(),4);
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSharing"));
            QCOMPARE(root->property("testDepth").toInt(),4);
            QVERIFY(QMetaObject::invokeMethod(root.data(),"prepareViewerRecall"));
            QCOMPARE(root->property("testDepth").toInt(),2);
            QVERIFY(!window->isVisible());
            QTest::qWait(20);
        }
        QCOMPARE(session.executions,1);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
        QTest::qWait(150);
        auto grid=root->property("testCurrentPage").value<QObject*>(); QVERIFY(grid);
        QCOMPARE(grid->property("count").toInt(),2);
        auto gridItem=qobject_cast<QQuickItem*>(grid); QVERIFY(gridItem);
        auto a=findVisual(gridItem,"device-device-a");
        auto b=findVisual(gridItem,"device-device-b");
        QVERIFY(a); QVERIFY(b);
        auto cardB=b->property("contentItem").value<QObject*>(); QVERIFY(cardB);
        auto cardA=a->property("contentItem").value<QObject*>(); QVERIFY(cardA);
        QVERIFY(QMetaObject::invokeMethod(cardB,"activateRequested"));
        QCOMPARE(recalls,0);
        QVERIFY(QMetaObject::invokeMethod(cardA,"activateRequested"));
        QCOMPARE(recalls,1);
        auto header=findVisual(gridItem,"deviceHeader"); QVERIFY(header);
        auto first=qobject_cast<QQuickItem*>(a); QVERIFY(first);
        QVERIFY(first->mapToScene(QPointF()).y() >= header->mapToScene(QPointF(0,header->height())).y());
        // Capture only synthetic pages, never the user's running application.
        const QString shots=qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS");
        if(!shots.isEmpty()) {
            QDir().mkpath(shots);
            // Dismiss the synthetic details dialog before screenshots.
            auto details=grid->findChild<QObject*>("deviceDetails"); QVERIFY(details);
            QVERIFY(QMetaObject::invokeMethod(details,"close"));
            window->resize(800,620); QTest::qWait(400);
            QVERIFY(window->grabWindow().save(shots+"/devices.png"));
            auto buttons=a->findChildren<QObject*>();
            for(auto button : buttons) if(button->property("text").toString()=="Return to desktop" && button->property("highlighted").isValid()) {
                auto content=button->property("contentItem").value<QObject*>(); QVERIFY(content);
                QCOMPARE(content->property("color").value<QColor>(), QColor("#ffffff"));
            }
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSharing"));
            window->resize(640,620); QTest::qWait(400);
            QVERIFY(window->grabWindow().save(shots+"/sharing-narrow.png"));
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSettings"));
            QTest::qWait(400); QVERIFY(window->grabWindow().save(shots+"/settings-narrow.png"));
            auto settings=root->property("testCurrentPage").value<QObject*>(); QVERIFY(settings);
            auto choice=settings->findChild<QObject*>("themeChoice"); QVERIFY(choice);
            QVERIFY(QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,2)));
            QTest::qWait(100); QVERIFY(window->grabWindow().save(shots+"/settings-dark.png"));
            auto accent=settings->findChild<QObject*>("accentChoice"); QVERIFY(accent);
            QVERIFY(QMetaObject::invokeMethod(accent,"activated",Q_ARG(int,3)));
            QTest::qWait(100); QVERIFY(window->grabWindow().save(shots+"/appearance-dark.png"));
            QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
            window->resize(1120,760); QTest::qWait(100);
            QVERIFY(window->grabWindow().save(shots+"/devices-dark.png"));
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testCards"));
            QTest::qWait(100);
            auto current=root->property("testCurrentPage").value<QObject*>(); QVERIFY(current);
            QVERIFY(!current->property("compact").toBool());
            QVERIFY(window->grabWindow().save(shots+"/cards-dark.png"));
            window->resize(640,620); QTest::qWait(100);
            QVERIFY(!current->property("compact").toBool());
            QTranslator chinese;
            QVERIFY(chinese.load(qEnvironmentVariable("TEST_GUI_DIR")+"/../languages/qml_zh_CN.qm"));
            QVERIFY(QCoreApplication::installTranslator(&chinese)); engine.retranslate();
            QTest::qWait(100); QVERIFY(window->grabWindow().save(shots+"/devices-chinese-narrow.png"));
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testDevice"));
            QTest::qWait(150); QVERIFY(window->grabWindow().save(shots+"/device-settings-chinese.png"));
            auto devicePage=root->property("testCurrentPage").value<QObject*>(); QVERIFY(devicePage);
            auto advanced=devicePage->findChild<QObject*>("deviceAdvancedButton"); QVERIFY(advanced);
            QVERIFY(QMetaObject::invokeMethod(advanced,"clicked"));
            QTest::qWait(200); QVERIFY(window->grabWindow().save(shots+"/device-advanced-chinese.png"));

            QCoreApplication::removeTranslator(&chinese); engine.retranslate();
        }
        emit session.sessionFinished(0);
        QTRY_COMPARE(root->property("testDepth").toInt(),1);
        QVERIFY(root->property("activeHostId").toString().isEmpty());
        emit session.readyForDeletion(); QTest::qWait(50);
        const auto bad=warnings.filter(QRegularExpression("ReferenceError|TypeError|binding loop|Binding loop|Cannot assign|Unable to assign|Missing parent|Component is not ready"));
        QVERIFY2(bad.isEmpty(),qPrintable(bad.join('\n')));
    }
    void commonLanguagesRetranslateSettings() {
        QTemporaryDir directory;
        HostManager host(nullptr, directory.path());
        PeerManager peers(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),directory.path()+"/peers",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
        engine.rootContext()->setContextProperty("hostManager", &host);
        engine.rootContext()->setContextProperty("peerManager", &peers);
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        QQmlComponent component(&engine,QUrl::fromLocalFile(gui+"/SettingsHome.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
        const QString english=page->property("heading").toString();
        QCOMPARE(english,QString("Make DeskPort your own."));
        auto themeChoice=page->findChild<QObject*>("themeChoice"); QVERIFY(themeChoice);
        QVERIFY(!page->findChild<QObject*>("windowModeChoice"));
        for (const auto& language : {"zh_CN","zh_TW","ja","ko","de","fr","es"}) {
            QTranslator translator;
            QVERIFY(translator.load(gui+"/../languages/qml_"+language+".qm"));
            QVERIFY(QCoreApplication::installTranslator(&translator));
            engine.retranslate();
            QVERIFY(page->property("heading").toString()!=english);
            QCOMPARE(themeChoice->property("currentIndex").toInt(),0);
            QVERIFY(!translator.translate("SettingsHome","Follow system").isEmpty());
            QVERIFY(!translator.translate("SettingsHome","Enable diagnostic logs").isEmpty());
            QVERIFY(!translator.translate("SettingsHome","Match the client window resolution").isEmpty());
            QVERIFY(!translator.translate("HostView","Built-in virtual display").isEmpty());
            QVERIFY(!translator.translate("HostView","Active · %1 × %2 pixels").isEmpty());
            QVERIFY(!translator.translate("BindingApproval","Allow & bind").isEmpty());
            QCoreApplication::removeTranslator(&translator);
            engine.retranslate();
            QCOMPARE(page->property("heading").toString(),english);
        }
    }
};
QTEST_MAIN(UiPages)
#include "ui-pages.moc"
