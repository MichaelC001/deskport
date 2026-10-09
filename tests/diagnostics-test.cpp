#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QDesktopServices>
#include <QUrlQuery>
#include "diagnostics.h"
#include "sessionfailure.h"

class DiagnosticsTest : public QObject {
    Q_OBJECT
public:
    QUrl opened;
public slots:
    void captureUrl(const QUrl& url) { opened=url; }
private slots:
    void initTestCase() {
        QCoreApplication::setApplicationVersion(qEnvironmentVariable("TEST_DESKPORT_VERSION", "development"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("TEST_DIAGNOSTICS_ROOT"));
    }
    void preparationErrorsAndStructuredPrivacy() {
        for (const auto code : {"cancelled", "unauthorized", "busy", "stale", "cycle", "unavailable", "timeout", "display-failed", "capture-encoder-failed"}) {
            const QString value(code);
            QVERIFY(!DeskPortSessionFailure::message(value).isEmpty());
            if (value != "unauthorized") QVERIFY(!DeskPortSessionFailure::message(value).contains("denied"));
            auto event = Diagnostics::project("DeskPort failure stage=session-prepare code=" + value);
            QCOMPARE(event["code"].toString(), value);
            event["source"] = "client"; event["run"] = QString(32, 'a'); event["elapsed_ms"] = 1;
            event["message"] = "PRIVATE_SECRET";
            const auto safe = Diagnostics::validate(event);
            QCOMPARE(safe["code"].toString(), value); QVERIFY(!safe.contains("message"));
            event["code"] = "private.example.net"; QVERIFY(Diagnostics::validate(event).isEmpty());
        }
        QVERIFY(!DeskPortSessionFailure::message("unknown-private-host").contains("unknown-private-host"));
        QCOMPARE(Diagnostics::project("[Info]: Found H.264 encoder: h264_vaapi [vaapi]")["backend"].toString(), QString("vaapi"));
        QVERIFY(!Diagnostics::project("Found H.264 encoder: secret [private-host]").contains("backend"));
        auto runtime = Diagnostics::project("DeskPort runtime kind=capture backend=kwin");
        runtime["source"] = "host"; runtime["run"] = QString(32, 'a'); runtime["elapsed_ms"] = 1;
        QCOMPARE(Diagnostics::validate(runtime)["backend"].toString(), QString("kwin"));
        runtime["backend"] = "/private/config"; QVERIFY(Diagnostics::validate(runtime).isEmpty());
        QVERIFY(!Diagnostics::project("DeskPort failure stage=session-prepare code=private-host").contains("code"));
        QCOMPARE(Diagnostics::project("Video failed to find working encoder: /private/path")["code"].toString(), QString("encoder-unavailable"));
        QTemporaryDir dir; Diagnostics logs(nullptr, dir.path()); logs.setEnabled(true);
        logs.record("client", "DeskPort failure stage=session-prepare code=display-failed");
        logs.record("host", "DeskPort runtime kind=capture backend=kwin");
        QFile archive(logs.createBundle()); QVERIFY(archive.open(QIODevice::ReadOnly));
        const auto bytes = archive.readAll(); QVERIFY(bytes.contains("display-failed")); QVERIFY(bytes.contains("kwin"));
    }
    void defaultAndSavedPreference() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings settings;
        settings.remove("diagnostics/enabled");
        { Diagnostics logs; QVERIFY(logs.enabled()); }
        settings.setValue("diagnostics/enabled", false);
        { Diagnostics logs; QVERIFY(!logs.enabled()); }
        settings.setValue("diagnostics/enabled", true);
        { Diagnostics logs; QVERIFY(logs.enabled()); }
        settings.remove("diagnostics/enabled");
        QStandardPaths::setTestModeEnabled(false);
    }
    void manifestVersion_data() {
        QTest::addColumn<QString>("version");
        QTest::addColumn<QString>("expected");
        for (auto version : {"0.4.6", "0.4.6-D", "0.4.6-preview.2"})
            QTest::newRow(version) << QString(version) << QString(version);
        for (auto version : {"private.example.net", "0.4.6-D\n", "0.4.6-/Users/alice", "0.4.6-"})
            QTest::newRow(version) << QString(version) << QString("development");
    }
    void manifestVersion() {
        QFETCH(QString, version); QFETCH(QString, expected);
        QCoreApplication::setApplicationVersion(version);
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path());
        QFile archive(logs.createBundle()); QVERIFY(archive.open(QIODevice::ReadOnly));
        QVERIFY(archive.readAll().contains("\"version\": \"" + expected.toUtf8() + "\""));
        QCoreApplication::setApplicationVersion(qEnvironmentVariable("TEST_DESKPORT_VERSION", "development"));
    }
    void privacy_data() {
        QTest::addColumn<QString>("secret");
        for (auto s : {"192.168.10.42", "2001:db8::abcd", "fe80::1234%en0", "::ffff:192.0.2.9", "[2001:db8::1]:443", "private.example.net", "my-host", "办公室电脑", "https://user:password@example.net/api?access_token=abc#key=123", "token=abcd", "Authorization: Bearer abcdef", "-----BEGIN PRIVATE KEY-----", "/Users/alice/Documents/private.txt", "/home/bob/.ssh/id_ed25519", "C:\\Users\\Jane\\private.txt", "clipboard: private sentence", "keycode=65", "text input: secret words"})
            QTest::newRow(s) << QString::fromUtf8(s);
    }
    void privacy() {
        QFETCH(QString,secret);
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path()); logs.setEnabled(true);
        logs.record("client","Connection failed: "+secret);
        logs.ingest("host",("Encoder failed: "+secret+"\n").toUtf8());
        logs.ingest("display",("display error: "+secret+"\n").toUtf8());
        const QString path=logs.createBundle(); QVERIFY(!path.isEmpty()); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes=file.readAll(); QVERIFY(!bytes.contains(secret.toUtf8()));
        for (auto name : QDir(dir.path()).entryList({"*.jsonl"})) {
            QFile log(dir.filePath(name)); QVERIFY(log.open(QIODevice::ReadOnly)); QVERIFY(!log.readAll().contains(secret.toUtf8()));
        }
    }
    void defaultOffToggleAndPartialLines() {
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path()); QVERIFY(!logs.enabled());
        logs.record("client","Connection failed: example.net"); logs.ingest("host","Host started\n");
        QVERIFY(QDir(dir.path()).entryList({"*.jsonl"}).isEmpty());
        logs.setEnabled(true); logs.ingest("host","Connection failed: 192.168.");
        logs.setEnabled(false); logs.ingest("host","1.2\n");
        logs.setEnabled(true); logs.ingest("host","Encoder started\n");
        QFile file(dir.filePath("host-0.jsonl")); QVERIFY(file.open(QIODevice::ReadOnly)); const auto data=file.readAll();
        QVERIFY(data.contains("started")); QVERIFY(!data.contains("failed"));
        logs.setEnabled(false); const auto size=file.size(); logs.ingest("host","Connection failed\n"); QCOMPARE(file.size(),size);
    }
    void structuredTimingAndValidation() {
        const auto projected=Diagnostics::project("00:01 - SDL Info: DeskPort resize stage=input-init-end tick_ms=1234 width=1920 height=1080\n");
        QCOMPARE(projected.value("stage").toString(),QString("input-init-end"));
        QCOMPARE(projected.value("width").toInt(),1920);
        QJsonObject object=projected; object["source"]="client"; object["run"]=QString(32,'a'); object["elapsed_ms"]=100;
        object["private"]="192.0.2.1"; object["token"]="secret";
        auto validated=Diagnostics::validate(object); QVERIFY(!validated.contains("private")); QVERIFY(!validated.contains("token"));
        object["run"]="my-host"; QVERIFY(Diagnostics::validate(object).isEmpty());
        object["run"]=QString(32,'a'); object["source"]="private.example"; QVERIFY(Diagnostics::validate(object).isEmpty());
    }
    void navigationTimingKeepsOnlyFixedStages() {
        for (const auto& stage : {"devices-request", "devices-ready", "devices-frame", "event-loop-delay"}) {
            auto record = Diagnostics::project(QString("DeskPort navigation stage=%1 duration_ms=875").arg(stage));
            QCOMPARE(record.value("event").toString(), QString("navigation"));
            QCOMPARE(record.value("duration_ms").toInt(), 875);
            record["source"] = "client"; record["run"] = QString(32, 'a'); record["elapsed_ms"] = 100;
            QCOMPARE(Diagnostics::validate(record).value("stage").toString(), QString(stage));
        }
        QVERIFY(Diagnostics::project("DeskPort navigation stage=private-host duration_ms=875").isEmpty());
    }
    void rotationRetentionAndExportAllowlist() {
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path()); logs.setEnabled(true);
        for (int i=0;i<28000;++i) logs.record("host","Connection failed");
        QVERIFY(QFile::exists(dir.filePath("host-2.jsonl")));
        for (auto name:QDir(dir.path()).entryList({"*.jsonl"})) QVERIFY(QFileInfo(dir.filePath(name)).size()<=Diagnostics::FileLimit);
        QFile secret(dir.filePath("state.json")); QVERIFY(secret.open(QIODevice::WriteOnly)); secret.write("PAIRING_SECRET"); secret.close();
        QFile linkTarget(dir.filePath("credentials")); QVERIFY(linkTarget.open(QIODevice::WriteOnly)); linkTarget.write("KEY_SECRET"); linkTarget.close();
        QVERIFY(QFile::link(linkTarget.fileName(),dir.filePath("display-0.jsonl")));
        QFile old(dir.filePath("display-1.jsonl")); QVERIFY(old.open(QIODevice::WriteOnly)); old.write("OLD_SECRET");
        QVERIFY(old.flush());
        QVERIFY(old.setFileTime(QDateTime::currentDateTimeUtc().addDays(-8),QFileDevice::FileModificationTime)); old.close();
        // Valid records with injected extra keys are reserialized from allowed fields.
        QFile altered(dir.filePath("client-0.jsonl")); QVERIFY(altered.open(QIODevice::WriteOnly|QIODevice::Append));
        altered.write("{\"source\":\"client\",\"run\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"event\":\"failed\",\"elapsed_ms\":5,\"message\":\"INJECTED_SECRET\"}\n"); altered.close();
        const QString viewer=dir.filePath("viewers/"+QString(32,'b'));
        QVERIFY(QDir().mkpath(viewer));
        QVERIFY(QFile::copy(dir.filePath("client-0.jsonl"),viewer+"/client-0.jsonl"));
        QFile privateFile(viewer+"/private.json"); QVERIFY(privateFile.open(QIODevice::WriteOnly)); privateFile.write("WORKER_SECRET"); privateFile.close();
        const auto path=logs.createBundle(); QVERIFY(!path.isEmpty()); QFile archive(path); QVERIFY(archive.open(QIODevice::ReadOnly)); const auto bytes=archive.readAll();
        for (auto secretValue:{"PAIRING_SECRET","KEY_SECRET","OLD_SECRET","INJECTED_SECRET","state.json","credentials","WORKER_SECRET","private.json"}) QVERIFY(!bytes.contains(secretValue));
        QVERIFY(!QFile::exists(old.fileName()));
        const auto root=qEnvironmentVariable("TEST_DIAGNOSTICS_ROOT"); QFile::remove(root+"/verified.zip"); QVERIFY(QFile::copy(path,root+"/verified.zip"));
        QVERIFY(bytes.contains("viewer-0-client-0.jsonl"));
        logs.clear(); QVERIFY(!QFile::exists(viewer+"/client-0.jsonl")); QVERIFY(QFile::exists(privateFile.fileName())); QVERIFY(!QFile::exists(path)); QVERIFY(QFile::exists(secret.fileName()));
    }
    void connectionCodesAndExportFailure() {
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.filePath("logs")); logs.setEnabled(true);
        logs.connection(3,-10060,"failed"); logs.connection(3,0,"starting");
        QFile file(dir.filePath("logs/client-0.jsonl")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines=file.readAll().trimmed().split('\n'); QVERIFY(lines.size()>=3);
        auto failed=QJsonDocument::fromJson(lines[lines.size()-2]).object();
        auto starting=QJsonDocument::fromJson(lines.last()).object();
        QCOMPARE(failed.value("error").toInt(),-10060); QCOMPARE(failed.value("phase").toInt(),3);
        QCOMPARE(failed.value("run"),starting.value("run"));
        QVERIFY(starting.value("elapsed_ms").toDouble()>=failed.value("elapsed_ms").toDouble());
        QFile blocker(dir.filePath("blocked")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        Diagnostics broken(nullptr,blocker.fileName()+"/logs"); QVERIFY(broken.createBundle().isEmpty()); QVERIFY(!broken.status().isEmpty());
        Diagnostics unknown(nullptr,dir.filePath("unknown")); unknown.setEnabled(true);
        unknown.ingest("host",QByteArray(20000,'x')+"connection failed\n");
        QVERIFY(!QFile::exists(dir.filePath("unknown/host-0.jsonl")));
    }
    void feedbackOpensOnlyStaticPublicUrl() {
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path());
        QDesktopServices::setUrlHandler("https",this,"captureUrl"); QVERIFY(logs.feedback());
        QCOMPARE(opened.host(),QString("github.com")); QCOMPARE(opened.path(),QString("/keithxc/deskport/issues/new"));
        QVERIFY(!opened.toString().contains(dir.path())); QVERIFY(!opened.toString().contains(".jsonl"));
        QVERIFY(QUrlQuery(opened).queryItemValue("body").contains("PUBLIC"));
        QVERIFY(logs.status().contains("version information only"));
        QDesktopServices::unsetUrlHandler("https");
    }
};
QTEST_MAIN(DiagnosticsTest)
#include "diagnostics-test.moc"
