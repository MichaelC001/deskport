#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include "backend/autoupdatechecker.h"
class UpdateCheckerTest : public QObject {
    Q_OBJECT
private slots:
    void releases() {
        QJsonObject release{{"tag_name", "v0.4.6"}, {"draft", false}, {"prerelease", false},
            {"html_url", "https://github.com/keithxc/deskport/releases/tag/v0.4.6"},
            {"body", "Fixes <script> are plain text"}, {"published_at", "2026-09-21T00:00:00Z"}};
        QString v,n,d,u;
        auto check = [&](QString current) { return AutoUpdateChecker::evaluate(QJsonDocument(release).toJson(),current,v,n,d,u); };
        QCOMPARE(check("0.4.5"), QString("available"));
        QCOMPARE(v, QString("0.4.6")); QCOMPARE(n, release["body"].toString());
        QCOMPARE(check("0.4.6"), QString("current"));
        QCOMPARE(check("0.4.6-dev"), QString("current"));
        QCOMPARE(check("0.5.0"), QString("current"));
        release["prerelease"] = true; QCOMPARE(check("0.4.5"), QString("error")); QVERIFY(u.isEmpty());
        release["prerelease"] = false; release["draft"] = true; QCOMPARE(check("0.4.5"), QString("error"));
        release["draft"] = false; release["html_url"] = "https://example.com/update"; QCOMPARE(check("0.4.5"), QString("error"));
        release["html_url"] = "https://github.com/keithxc/deskport/releases/tag/v0.4.6-preview.1";
        release["tag_name"] = "v0.4.6-preview.1"; QCOMPARE(check("0.4.5"), QString("error"));
        QCOMPARE(AutoUpdateChecker::evaluate("[]","0.4.5",v,n,d,u), QString("error"));
        QCOMPARE(AutoUpdateChecker::evaluate("not JSON","0.4.5",v,n,d,u), QString("error"));
    }
};
QTEST_GUILESS_MAIN(UpdateCheckerTest)
#include "update-checker.moc"
