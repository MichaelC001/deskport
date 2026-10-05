#include "seamlesshostmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace {
constexpr int MaxControlLine = 32768;
constexpr int MinimumWidth = 64;
constexpr int MinimumHeight = 64;
constexpr int MaximumWidth = 7680;
constexpr int MaximumHeight = 4320;
constexpr int MaximumWindows = 64;

bool integerField(const QJsonObject& object, const QString& name, int minimum, int maximum,
                  int* result) {
    const auto value = object.value(name);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < minimum || number > maximum) return false;
    if (result) *result = int(number);
    return true;
}
}

SeamlessHostManager::SeamlessHostManager(QObject* parent) : QObject(parent) {
    m_Status = available() ? tr("Seamless host is off") :
        tr("The experimental Seamless host helper is missing");
    m_Process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_LINUX
    m_Process.setChildProcessModifier([this] {
        if (::prctl(PR_SET_PDEATHSIG, SIGTERM) != 0)
            m_Process.failChildProcessModifier("prctl(PR_SET_PDEATHSIG)", errno);
        if (::getppid() == 1)
            ::raise(SIGTERM);
    });
#endif
    connect(&m_Process, &QProcess::readyReadStandardOutput, this, &SeamlessHostManager::consumeOutput);
    connect(&m_Process, &QProcess::readyReadStandardError, this, [this] {
        const auto message = QString::fromUtf8(m_Process.readAllStandardError()).trimmed();
        if (!message.isEmpty()) qWarning().noquote() << "Seamless host:" << message.left(4096);
    });
    connect(&m_Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_Stopping) return;
        setStatus(tr("Seamless host failed: %1").arg(m_Process.errorString()));
        if (m_Process.state() == QProcess::NotRunning) emit stopped();
    });
    connect(&m_Process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exitStatus) {
        const bool expected = m_Stopping;
        m_KillTimer.stop();
        m_Ready = false;
        m_Windows.clear();
        m_SocketName.clear();
        m_Output.clear();
        m_Stopping = false;
        setStatus(expected ? tr("Seamless host is off") :
            tr("Seamless host stopped (%1, %2)").arg(code).arg(
                exitStatus == QProcess::NormalExit ? tr("normal exit") : tr("crashed")));
        emit stopped();
    });
    m_KillTimer.setSingleShot(true);
    m_KillTimer.setInterval(5000);
    connect(&m_KillTimer, &QTimer::timeout, this, [this] {
        // This QProcess is the exact child started by this manager. Never kill
        // by executable name or touch another DeskPort/compositor session.
        if (m_Process.state() != QProcess::NotRunning) m_Process.kill();
    });
}

SeamlessHostManager::~SeamlessHostManager() {
    if (m_Process.state() == QProcess::NotRunning) return;
    m_Process.disconnect(this);
    m_Process.terminate();
    // The sidecar gives its owned application group three seconds to exit and
    // then verifies the SIGKILL escalation. Do not kill the supervisor before
    // that group-cleanup window has completed.
    if (!m_Process.waitForFinished(5500)) {
        m_Process.kill();
        m_Process.waitForFinished(1000);
    }
}

QString SeamlessHostManager::binaryPath() {
    const auto override = qEnvironmentVariable("DESKPORT_SEAMLESS_HOST_BINARY");
    if (!override.isEmpty() && QDir::isAbsolutePath(override)) return QDir::cleanPath(override);
#ifdef Q_OS_LINUX
    return QDir::cleanPath(QCoreApplication::applicationDirPath() +
                           QStringLiteral("/../libexec/deskport-seamless-host"));
#else
    return {};
#endif
}

bool SeamlessHostManager::available() const {
    const QFileInfo helper(binaryPath());
    return helper.isFile() && helper.isExecutable();
}

bool SeamlessHostManager::running() const {
    return m_Process.state() != QProcess::NotRunning;
}

bool SeamlessHostManager::start(const QStringList& application, const QSize& size) {
    if (running() || m_Stopping) return false;
    if (!available()) {
        setStatus(tr("The experimental Seamless host helper is missing"));
        return false;
    }
    if (size.width() < MinimumWidth || size.height() < MinimumHeight ||
        size.width() > MaximumWidth || size.height() > MaximumHeight) {
        setStatus(tr("Unsupported Seamless workspace size"));
        return false;
    }
    for (const auto& argument : application) {
        if (argument.contains(QChar::Null)) {
            setStatus(tr("Seamless application arguments contain an invalid character"));
            return false;
        }
    }

    m_Output.clear();
    m_Ready = false;
    m_Stopping = false;
    m_Windows.clear();
    m_SocketName.clear();
    QStringList arguments{QStringLiteral("--width"), QString::number(size.width()),
                          QStringLiteral("--height"), QString::number(size.height())};
    if (!application.isEmpty()) {
        arguments.append(QStringLiteral("--"));
        arguments.append(application);
    }
    m_Process.setProgram(binaryPath());
    m_Process.setArguments(arguments);
    setStatus(tr("Starting experimental Seamless host"));
    m_Process.start();
    return true;
}

void SeamlessHostManager::stop() {
    if (m_Process.state() == QProcess::NotRunning) {
        finishStop();
        return;
    }
    if (m_Stopping) return;
    m_Stopping = true;
    m_Ready = false;
    setStatus(tr("Stopping Seamless host"));
    m_Process.terminate();
    m_KillTimer.start();
}

void SeamlessHostManager::consumeOutput() {
    m_Output += m_Process.readAllStandardOutput();
    if (m_Output.size() > MaxControlLine && !m_Output.contains('\n')) {
        setStatus(tr("Seamless host returned an oversized control message"));
        stop();
        return;
    }
    while (true) {
        const int newline = m_Output.indexOf('\n');
        if (newline < 0) break;
        const auto line = m_Output.left(newline);
        m_Output.remove(0, newline + 1);
        if (line.size() > MaxControlLine) {
            setStatus(tr("Seamless host returned an oversized control message"));
            stop();
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        const auto event = document.object();
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            event.value(QStringLiteral("version")).toInt() != 1 ||
            event.value(QStringLiteral("type")).toString().isEmpty()) {
            setStatus(tr("Seamless host returned an invalid control message"));
            stop();
            return;
        }
        const auto type = event.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ready")) {
            const auto socket = event.value(QStringLiteral("socket")).toString();
            static const QRegularExpression socketSyntax(
                QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$"));
            if (m_Ready || !m_Windows.isEmpty() || !socketSyntax.match(socket).hasMatch() ||
                !integerField(event, QStringLiteral("width"), MinimumWidth, MaximumWidth, nullptr) ||
                !integerField(event, QStringLiteral("height"), MinimumHeight, MaximumHeight, nullptr)) {
                setStatus(tr("Seamless host returned an invalid ready event"));
                stop();
                return;
            }
            m_SocketName = socket;
            m_Ready = true;
            setStatus(tr("Experimental Seamless host is ready"));
        } else if (type == QStringLiteral("window-create")) {
            int id = 0;
            const bool valid = integerField(event, QStringLiteral("id"), 1, 2147483647, &id) &&
                m_Ready &&
                integerField(event, QStringLiteral("width"), 16, MaximumWidth, nullptr) &&
                integerField(event, QStringLiteral("height"), 16, MaximumHeight, nullptr) &&
                event.value(QStringLiteral("title")).isString() &&
                event.value(QStringLiteral("appId")).isString() &&
                event.value(QStringLiteral("title")).toString().toUtf8().size() <= 1024 &&
                event.value(QStringLiteral("appId")).toString().toUtf8().size() <= 255 &&
                m_Windows.size() < MaximumWindows && !m_Windows.contains(id);
            if (!valid) {
                setStatus(tr("Seamless host returned an invalid window lifecycle event"));
                stop();
                return;
            }
            m_Windows.insert(id);
            emit changed();
        } else if (type == QStringLiteral("window-destroy")) {
            int id = 0;
            if (!m_Ready ||
                !integerField(event, QStringLiteral("id"), 1, 2147483647, &id) ||
                !m_Windows.remove(id)) {
                setStatus(tr("Seamless host returned an invalid window lifecycle event"));
                stop();
                return;
            }
            emit changed();
        } else if (type == QStringLiteral("error")) {
            setStatus(tr("Seamless host error: %1").arg(event.value(QStringLiteral("message")).toString()));
        }
        emit eventReceived(event);
    }
    if (m_Output.size() > MaxControlLine) {
        setStatus(tr("Seamless host returned an oversized control message"));
        stop();
    }
}

void SeamlessHostManager::setStatus(const QString& status) {
    if (m_Status == status) return;
    m_Status = status;
    emit changed();
}

void SeamlessHostManager::finishStop() {
    m_Ready = false;
    m_Windows.clear();
    m_SocketName.clear();
    setStatus(tr("Seamless host is off"));
    emit stopped();
}
