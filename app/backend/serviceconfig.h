#pragma once
#include <QString>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace DeskPortService {
inline QString persistentExecutable(const QString& native, const QString& appImage) {
    const QFileInfo portable(appImage);
    // AppImage mount paths disappear on exit; login must launch the original file.
    return !appImage.isEmpty() && portable.isAbsolute() && portable.isFile() && portable.isExecutable()
        ? portable.absoluteFilePath() : native;
}
#ifdef Q_OS_LINUX
inline QString autostartPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/io.github.keithxc.DeskPort.desktop";
}
inline QString unitPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/systemd/user/io.github.keithxc.DeskPort.service";
}
#endif
// 声明式系统配置 (Nix home-manager) 把自启文件链接进 /nix/store。那份配置才是
// 事实源: 我们既不能覆盖也不能删除, 否则下次 switch 会把改动悄悄还原回去。
inline bool storeManaged(const QString& path) {
    const QFileInfo info(path);
    return info.isSymLink() && info.symLinkTarget().startsWith("/nix/store/");
}
inline QString launchAgent() {
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\"><dict>"
        "<key>Label</key><string>io.github.keithxc.DeskPort</string>"
        "<key>ProgramArguments</key><array><string>/Applications/DeskPort.app/Contents/MacOS/DeskPort</string>"
        "<string>--background</string></array>"
        "<key>RunAtLoad</key><true/>"
        "<key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>"
        "<key>ThrottleInterval</key><integer>10</integer>"
        "<key>LimitLoadToSessionType</key><string>Aqua</string>"
        "<key>ProcessType</key><string>Interactive</string></dict></plist>");
}
inline QString systemdUnit(QString executable) {
    executable.replace("\\", "\\\\").replace("\"", "\\\"").replace("%", "%%").replace("$", "$$");
    const auto body = QStringLiteral("[Unit]\nDescription=DeskPort desktop sharing\nAfter=graphical-session.target\n"
        "PartOf=graphical-session.target\nStartLimitIntervalSec=0\n\n[Service]\nType=simple\n"
        "ExecStart=\"%1\" --background\nRestart=on-failure\nRestartSec=10\n"
        "KillMode=mixed\nTimeoutStopSec=10\n\n[Install]\nWantedBy=graphical-session.target\n").arg(executable);
    return "# Managed by DeskPort GUI v1\n# Content-SHA256: " +
        QString::fromLatin1(QCryptographicHash::hash(body.toUtf8(), QCryptographicHash::Sha256).toHex()) + '\n' + body;
}
inline QString desktopEntry() {
    return QStringLiteral("[Desktop Entry]\nType=Application\nName=DeskPort\n"
        "Exec=systemctl --user start io.github.keithxc.DeskPort.service\nTerminal=false\n");
}

inline QByteArray startupFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() <= 65536 ? file.readAll() : QByteArray();
}

inline bool guiManagedUnit(const QString& path) {
    if (QFileInfo(path).isSymLink()) return false;
    const auto bytes = startupFile(path);
    const QByteArray header = "# Managed by DeskPort GUI v1\n# Content-SHA256: ";
    if (bytes.startsWith(header)) {
        const int bodyAt = bytes.indexOf('\n', header.size());
        return bodyAt >= 0 && bytes.mid(header.size(), bodyAt - header.size()) ==
            QCryptographicHash::hash(bytes.mid(bodyAt + 1), QCryptographicHash::Sha256).toHex();
    }
    // Older GUI versions emitted an exact, unmarked template. Permit their
    // refresh across installed executable paths, but preserve any other edits.
    const QByteArray marker = "DESKPORT_EXECUTABLE_PLACEHOLDER";
    QByteArray expected = systemdUnit(QString::fromLatin1(marker)).toUtf8();
    expected = expected.mid(expected.indexOf('\n', header.size()) + 1);
    const int at = expected.indexOf(marker);
    for (const auto& mode : {QByteArray("mixed"), QByteArray("control-group")}) {
        QByteArray suffix = expected.mid(at + marker.size());
        suffix.replace("KillMode=mixed", "KillMode=" + mode);
        if (!bytes.startsWith(expected.left(at)) || !bytes.endsWith(suffix)) continue;
        const auto command = bytes.mid(at, bytes.size() - at - suffix.size());
        bool quotedPath = true;
        for (int i = 0; i < command.size(); ++i) {
            if (command[i] == '\\') { if (++i == command.size()) quotedPath = false; }
            else if (command[i] == '"') quotedPath = false;
        }
        if (quotedPath && !command.isEmpty() && !command.contains('\n') && !command.contains('\r') &&
            (command.endsWith("/deskport") || command.endsWith("/DeskPort") || command.endsWith(".AppImage"))) return true;
    }
    return false;
}

inline bool startupManagedElsewhere(const QString& unit, const QString& desktop) {
    for (const auto& path : {unit, desktop}) {
        const QFileInfo info(path);
        if (info.isSymLink() || QFileInfo(info.absolutePath()).canonicalFilePath().startsWith("/nix/store/")) return true;
    }
    const QFileInfo dropIns(unit + ".d");
    if (dropIns.isSymLink() || (dropIns.exists() && (!dropIns.isDir() ||
        !QDir(dropIns.absoluteFilePath()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()))) return true;
    return (QFileInfo::exists(unit) && !guiManagedUnit(unit)) ||
        (QFileInfo::exists(desktop) && startupFile(desktop) != desktopEntry().toUtf8());
}

inline bool startupEnabled(const QString& unit, const QString& desktop, const QStringList& searchDirectories) {
    // A service file is only installed configuration. Login activation requires
    // an enabled desktop entry or a target dependency, including Nix symlinks.
    if (QFileInfo(desktop).isFile()) {
        QSettings entry(desktop, QSettings::IniFormat);
        entry.beginGroup("Desktop Entry");
        if (!entry.value("Hidden", false).toBool() && entry.value("X-GNOME-Autostart-enabled", true).toBool() &&
            entry.value("Type").toString() == "Application" && !entry.value("Exec").toString().isEmpty()) return true;
    }
    const auto name = QFileInfo(unit).fileName();
    for (const auto& directory : searchDirectories) {
        const QDir root(directory);
        for (const auto& dependency : root.entryList({"*.wants", "*.requires"}, QDir::Dirs | QDir::NoDotAndDotDot))
            if (QFileInfo(root.filePath(dependency + '/' + name)).isFile()) return true;
    }
    return false;
}

#ifdef Q_OS_LINUX
inline bool startupEnabled() {
    return startupEnabled(unitPath(), autostartPath(), {
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/systemd/user",
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/systemd/user",
        "/etc/systemd/user", "/run/systemd/user", "/usr/local/lib/systemd/user", "/usr/lib/systemd/user"});
}
#endif
}
