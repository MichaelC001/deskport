#pragma once
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

// Opt-in operator fixture. No identity is created or copied by this helper.
// main validates the marker before any settings, services or workers start.
namespace InputTestProfile {
inline QString root() { return qEnvironmentVariable("DESKPORT_INPUT_TEST_PROFILE"); }
inline bool valid() {
    const auto path = root();
    return QDir::isAbsolutePath(path) && QFileInfo(path).isDir() &&
        QFileInfo(path + "/.deskport-input-test").isFile();
}
inline QString data() {
    return root().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                            : root() + "/data";
}
}
