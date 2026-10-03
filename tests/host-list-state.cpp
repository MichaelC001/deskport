#include "backend/hostliststate.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DeskPortRemovalTest");
    QCoreApplication::setApplicationName("Isolated");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    if (argc == 3) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, argv[2]);
        const QString action = argv[1];
        if (action == "remove") {
            QSettings().setValue("hosts/fixture", "stale saved host");
            check(HostListState::setRemoved("Device-A", true), "durable removal");
            // Simulate an abrupt exit before host-list asynchronous flushing.
            std::_Exit(0);
        }
        if (action == "restart") {
            check(!HostListState::admit("device-a"), "startup/endpoint refresh must not recreate an offline removed host");
            check(!HostListState::admit("DEVICE-A"), "mDNS must honor the same removal");
            check(QSettings().value("hosts/fixture").isValid(), "stale snapshot intentionally still exists");
            check(HostListState::admit("device-b"), "another host is unaffected");
        } else if (action == "readd") {
            check(HostListState::admit("device-a", true), "explicit re-add clears removal");
        } else if (action == "restarted-again") {
            check(HostListState::admit("device-a"), "explicit re-add survives a second restart");
        }
        return 0;
    }
    QTemporaryDir temporary;
    check(temporary.isValid(), "isolated settings directory");
    for (const auto& action : {"remove", "restart", "restart", "readd", "restarted-again"})
        check(QProcess::execute(QCoreApplication::applicationFilePath(), {action, temporary.path()}) == 0, action);
    std::puts("PASS: removal survives abrupt exit and repeated process restarts; explicit re-add persists");
}
