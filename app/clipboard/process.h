#pragma once
#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

// Own one native clipboard helper for the authenticated session. No clipboard
// content goes to logs or command-line arguments; pipes carry bounded frames.
class ClipboardProcess : public QProcess {
public:
    explicit ClipboardProcess(QObject* parent = nullptr) : QProcess(parent) {
        setProgram(QCoreApplication::applicationFilePath());
        setArguments({"--clipboard-helper"});
        setProcessChannelMode(QProcess::SeparateChannels);
        setStandardErrorFile(QProcess::nullDevice());
    }
    ~ClipboardProcess() override {
        closeWriteChannel();
        if (state() != QProcess::NotRunning && !waitForFinished(1500)) { kill(); waitForFinished(1500); }
    }
    bool put(const QJsonObject& message) {
        if (bytesToWrite() > 180 * 1024 * 1024) return false;
        return write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n') >= 0;
    }
    bool drain(const std::function<void(const QJsonObject&)>& receive) {
        buffer += readAllStandardOutput();
        if (buffer.size() > 180 * 1024 * 1024) return false;
        int end;
        while ((end = buffer.indexOf('\n')) >= 0) {
            QJsonParseError error; const auto doc = QJsonDocument::fromJson(buffer.left(end), &error);
            buffer.remove(0, end + 1);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
            receive(doc.object());
        }
        return true;
    }
private:
    QByteArray buffer;
};
