#include <QtTest>

#include "seamlesshostmanager.h"

#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <csignal>
#endif

class SeamlessHostManagerTest : public QObject {
    Q_OBJECT
private:
    static QString helper(const QTemporaryDir& directory, const QByteArray& body) {
        const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty()) return {};
        const auto path = directory.path() + QStringLiteral("/fake-seamless-host");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write("#!" + QFile::encodeName(python) + "\n" + body);
        file.close();
        if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return {};
        return path;
    }

private slots:
    void missingHelperFailsClosed() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(directory.path() + "/missing"));
        SeamlessHostManager manager;
        QVERIFY(!manager.available());
        QVERIFY(!manager.start());
        QVERIFY(!manager.running());
        QVERIFY(!manager.ready());
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }

    void lifecycleAndExactChildTeardown() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, R"PY(
import json, signal, sys, time
running = True
def stop(_signum, _frame):
    global running
    print(json.dumps({"version": 1, "type": "window-destroy", "id": 1}), flush=True)
    running = False
signal.signal(signal.SIGTERM, stop)
print(json.dumps({"version": 1, "type": "ready", "socket": "deskport-test-1", "width": 1280, "height": 720}), flush=True)
print(json.dumps({"version": 1, "type": "window-create", "id": 1, "width": 320, "height": 200, "title": "Terminal", "appId": "org.example.Terminal"}), flush=True)
while running:
    time.sleep(0.02)
)PY");
        QVERIFY(!path.isEmpty());
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(path));
        SeamlessHostManager manager;
        QSignalSpy events(&manager, &SeamlessHostManager::eventReceived);
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.available());
        QVERIFY(manager.start({QStringLiteral("terminal"), QStringLiteral("--safe-argument")}));
        QTRY_VERIFY_WITH_TIMEOUT(manager.ready(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.windowCount(), 1, 5000);
        const auto pid = manager.processId();
        QVERIFY(pid > 0);
        manager.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 7000);
        QTRY_VERIFY_WITH_TIMEOUT(!stopped.isEmpty(), 7000);
        QCOMPARE(manager.windowCount(), 0);
        QVERIFY(events.size() >= 2);
#ifdef Q_OS_UNIX
        errno = 0;
        QCOMPARE(::kill(pid_t(pid), 0), -1);
        QCOMPARE(errno, ESRCH);
#endif
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }

    void lifecycleBeforeReadyStopsHelper() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, R"PY(
import json, time
print(json.dumps({"version": 1, "type": "window-create", "id": 1, "width": 320, "height": 200, "title": "bad", "appId": "bad"}), flush=True)
time.sleep(30)
)PY");
        QVERIFY(!path.isEmpty());
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(path));
        SeamlessHostManager manager;
        QStringList statuses;
        connect(&manager, &SeamlessHostManager::changed, &manager,
                [&] { statuses.append(manager.status()); });
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 8000);
        QVERIFY(std::any_of(statuses.cbegin(), statuses.cend(), [](const QString& status) {
            return status.contains(QStringLiteral("lifecycle"), Qt::CaseInsensitive);
        }));
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }

    void oversizedControlLineStopsHelper() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, R"PY(
import sys, time
sys.stdout.write("x" * 33000)
sys.stdout.flush()
time.sleep(30)
)PY");
        QVERIFY(!path.isEmpty());
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(path));
        SeamlessHostManager manager;
        QStringList statuses;
        connect(&manager, &SeamlessHostManager::changed, &manager,
                [&] { statuses.append(manager.status()); });
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 8000);
        QVERIFY(std::any_of(statuses.cbegin(), statuses.cend(), [](const QString& status) {
            return status.contains(QStringLiteral("oversized"), Qt::CaseInsensitive);
        }));
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }
};

QTEST_GUILESS_MAIN(SeamlessHostManagerTest)
#include "seamless-host-manager.moc"
