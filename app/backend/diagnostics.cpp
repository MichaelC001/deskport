#include "diagnostics.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QDateTime>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QUuid>
#include <QRegularExpression>
#include <QDataStream>
#include <QTimer>

namespace {
const QStringList sources {"client", "host", "display"};
const QStringList stages {"decoder-setup", "continuation", "mode-request", "mode-ready", "mode-failed", "observed", "stop-begin", "viewer-geometry-ready", "probe-begin", "probe-end", "stop-end", "resume-request", "resume-response", "initialize-begin", "input-init-begin", "input-init-end", "first-render-submit"};
const QStringList events {"started", "stopped", "failed", "timeout", "connection", "display", "encoder", "decoder", "input", "capture", "recovery", "permission", "resize", "enabled"};
QStringList names() {
    QStringList result;
    for (const auto& source : sources) for (int i = 0; i < 3; ++i)
        result << source + QString("-%1.jsonl").arg(i);
    return result;
}
QString platform() {
#ifdef Q_OS_MACOS
    return "macOS";
#elif defined(Q_OS_WIN)
    return "Windows";
#else
    return "Linux";
#endif
}
// ZIP store method: small bounded text files, no external archiver or directory traversal.
quint32 crc32(const QByteArray& data) {
    quint32 crc = 0xffffffff;
    for (unsigned char c : data) { crc ^= c; for (int i=0; i<8; ++i) crc = (crc >> 1) ^ (0xedb88320 & (0u - (crc & 1))); }
    return ~crc;
}
QByteArray zip(const QMap<QString, QByteArray>& files) {
    QByteArray out, central;
    QDataStream local(&out,QIODevice::WriteOnly), cd(&central,QIODevice::WriteOnly);
    local.setByteOrder(QDataStream::LittleEndian); cd.setByteOrder(QDataStream::LittleEndian);
    for (auto it=files.begin(); it!=files.end(); ++it) {
        const QByteArray name=it.key().toUtf8(), data=it.value();
        const quint32 offset=out.size(), crc=crc32(data), size=data.size();
        local << quint32(0x04034b50) << quint16(20) << quint16(0) << quint16(0) << quint16(0) << quint16(33) << crc << size << size << quint16(name.size()) << quint16(0);
        local.writeRawData(name.data(),name.size()); local.writeRawData(data.data(),data.size());
        cd << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0) << quint16(0) << quint16(0) << quint16(33) << crc << size << size << quint16(name.size()) << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << offset;
        cd.writeRawData(name.data(),name.size());
    }
    const quint32 offset=out.size();
    local.writeRawData(central.data(),central.size());
    local << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(files.size()) << quint16(files.size()) << quint32(central.size()) << offset << quint16(0);
    return out;
}
}
Diagnostics& Diagnostics::instance() { static Diagnostics value; return value; }
Diagnostics::Diagnostics(QObject* parent, const QString& directory) : QObject(parent),
    m_Directory(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/diagnostics" : directory),
    m_Run(QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-')),
    m_Enabled(directory.isEmpty() && QSettings().value("diagnostics/enabled",false).toBool()) {
    m_Time.start();
    prune();
    startMaintenance();
}
void Diagnostics::startMaintenance() {
    if (m_MaintenanceStarted || !QCoreApplication::instance()) return;
    m_MaintenanceStarted=true;
    auto timer=new QTimer(this);
    timer->setInterval(60*60*1000);
    connect(timer,&QTimer::timeout,this,[this] { QMutexLocker lock(&m_Mutex); prune(); });
    timer->start();
}
bool Diagnostics::enabled() const { QMutexLocker lock(&m_Mutex); return m_Enabled; }
void Diagnostics::setEnabled(bool value) {
    { QMutexLocker lock(&m_Mutex); m_Enabled=value; m_Pending.clear();
      QSettings().setValue("diagnostics/enabled",value); }
    if (value) record("client","diagnostics enabled");
    emit changed();
}
// Fail closed: retain fixed event classes and tightly matched numeric timing records,
// never arbitrary log text. Unknown formats lose detail instead of leaking identities.
QJsonObject Diagnostics::project(const QString& message) {
    if (message.size()>16384) return {};
    const auto lower=message.toLower();
    // Do not fingerprint or persist private payloads, even when mixed with an error.
    for (const auto& word : {"clipboard", "keycode", "keystroke", "keydown", "keyup", "key text", "text input", "password", "token", "secret", "private key", "rikey"})
        if (lower.contains(QLatin1String(word))) return {};
    static const QRegularExpression resize("DeskPort resize stage=([a-z-]+) tick_ms=([0-9]{1,10}) width=([0-9]{1,5}) height=([0-9]{1,5})\\s*$");
    auto match=resize.match(message);
    if (match.hasMatch() && stages.contains(match.captured(1))) {
        return {{"event","resize"},{"stage",match.captured(1)}, {"tick_ms",match.captured(2).toDouble()},
            {"width",match.captured(3).toInt()},{"height",match.captured(4).toInt()}};
    }
    const QList<QPair<QString,QString>> classes {
        {"diagnostics enabled","enabled"},{"permission","permission"},{"timeout","timeout"},
        {"failed","failed"},{"error","failed"},{"recovery","recovery"},{"stopped","stopped"},
        {"started","started"},{"connect","connection"},{"encoder","encoder"},
        {"decoder","decoder"},{"capture","capture"},{"display","display"},{"input-init","input"}};
    for (const auto& entry : classes) if (lower.contains(entry.first)) return {{"event",entry.second}};
    return {};
}
QJsonObject Diagnostics::validate(const QJsonObject& o) {
    if (!events.contains(o.value("event").toString()) || !sources.contains(o.value("source").toString())) return {};
    static const QRegularExpression id("^[a-f0-9]{32}$");
    if (!id.match(o.value("run").toString()).hasMatch()) return {};
    QJsonObject result {{"event",o.value("event")},{"source",o.value("source")},{"run",o.value("run")}};
    for (const auto& key : {"elapsed_ms","tick_ms","width","height"}) {
        auto v=o.value(QLatin1String(key));
        if (v.isDouble() && v.toDouble()>=0 && v.toDouble()<=4294967295.0 && v.toDouble()==double(quint64(v.toDouble()))) result[QLatin1String(key)]=v;
    }
    for (const auto& key : {"phase","error"}) {
        auto v=o.value(QLatin1String(key));
        if (v.isDouble() && v.toDouble()>=-2147483648.0 && v.toDouble()<=2147483647.0 && v.toDouble()==double(v.toInt())) result[QLatin1String(key)]=v;
    }
    if (QStringList{"starting","failed","terminated"}.contains(o.value("state").toString())) result["state"]=o.value("state");
    if (!result.contains("elapsed_ms")) return {};
    if (stages.contains(o.value("stage").toString())) result["stage"]=o.value("stage");
    return result;
}
void Diagnostics::ingest(const QString& source, const QByteArray& bytes) {
    QMutexLocker lock(&m_Mutex);
    if (!m_Enabled || !sources.contains(source)) return;
    auto& pending=m_Pending[source];
    // Bound partial lines; discard oversized records through the next newline.
    for (char c : bytes) {
        if (c=='\n') { if (pending.size()<=16384) write(source,project(QString::fromUtf8(pending))); pending.clear(); }
        else if (pending.size()<=16384) pending.append(c);
    }
}
void Diagnostics::record(const QString& source, const QString& message) {
    QMutexLocker lock(&m_Mutex);
    if (m_Enabled && sources.contains(source)) write(source,project(message));
}
void Diagnostics::connection(int phase, int error, const QString& state) {
    QMutexLocker lock(&m_Mutex);
    if (m_Enabled && phase>=0 && phase<=32 && QStringList{"starting","failed","terminated"}.contains(state))
        write("client",{{"event","connection"},{"phase",phase},{"error",error},{"state",state}});
}
void Diagnostics::prune() {
    if (QFileInfo(m_Directory).isSymLink()) return;
    const auto cutoff=QDateTime::currentDateTimeUtc().addDays(-7);
    for (const auto& name : names() + QStringList{"DeskPort-diagnostics.zip"}) {
        QFileInfo info(m_Directory+"/"+name);
        if (info.isSymLink() || (info.exists() && info.lastModified()<cutoff)) QFile::remove(info.filePath());
    }
}
void Diagnostics::write(const QString& source, QJsonObject event) {
    if (event.isEmpty() || QFileInfo(m_Directory).isSymLink()) return;
    if (!QDir().mkpath(m_Directory)) return;
    QFile::setPermissions(m_Directory,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    prune();
    event["source"]=source; event["run"]=m_Run; event["elapsed_ms"]=double(m_Time.elapsed());
    const auto line=QJsonDocument(validate(event)).toJson(QJsonDocument::Compact)+'\n';
    const auto base=m_Directory+"/"+source;
    const auto path=base+"-0.jsonl";
    if (QFileInfo(path).size()+line.size()>FileLimit) {
        QFile::remove(base+"-2.jsonl"); QFile::rename(base+"-1.jsonl",base+"-2.jsonl"); QFile::rename(path,base+"-1.jsonl");
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly|QIODevice::Append)) {
        file.setPermissions(QFile::ReadOwner|QFile::WriteOwner); file.write(line);
    }
}
QString Diagnostics::createBundle() {
    QMutexLocker lock(&m_Mutex);
    m_Status.clear(); m_Bundle.clear(); prune();
    if (QFileInfo(m_Directory).isSymLink()) { m_Status=tr("Cannot use the diagnostics directory."); return {}; }
    QMap<QString,QByteArray> files;
    for (const auto& name : names()) {
        QFileInfo info(m_Directory+"/"+name);
        if (!info.isFile() || info.isSymLink() || info.size()>FileLimit) continue;
        QFile file(info.filePath()); if (!file.open(QIODevice::ReadOnly)) continue;
        QByteArray data;
        const auto snapshot=file.read(FileLimit);
        auto lines=snapshot.split('\n');
        // The final fragment is incomplete (or empty after a trailing newline).
        lines.removeLast();
        for (const auto& line : lines) {
            if (line.size()>16384) continue;
            auto safe=validate(QJsonDocument::fromJson(line).object());
            if (!safe.isEmpty()) data+=QJsonDocument(safe).toJson(QJsonDocument::Compact)+'\n';
        }
        if (!data.isEmpty()) files.insert(name,data);
    }
    // Build version is compile-time product metadata, not host/environment inventory.
    QString version=QCoreApplication::applicationVersion();
    if (version.size() > 64 || !QRegularExpression("^[0-9]+(?:\\.[0-9]+){1,3}(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?\\z").match(version).hasMatch()) version="development";
    files["manifest.json"]=QJsonDocument(QJsonObject{{"schema",1},{"version",version},{"platform",platform()},
        {"diagnostics_enabled",m_Enabled},{"log_files",files.size()}}).toJson();
    files["README.txt"]="DeskPort diagnostics: event classes, relative timing, resize dimensions and random run IDs only. Unknown messages are omitted. No configuration, pairing, keys, clipboard, key text, screen or raw error text is collected. Review before attaching to a PUBLIC GitHub issue. Nothing has been uploaded.\n";
    if (!QDir().mkpath(m_Directory)) { m_Status=tr("Cannot create the diagnostics directory."); return {}; }
    QFile::setPermissions(m_Directory,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    // One bounded export, replaced only by an explicit subsequent export.
    const QString target=m_Directory+"/DeskPort-diagnostics.zip";
    if (QFileInfo(target).isSymLink()) { m_Status=tr("Cannot replace the diagnostics archive."); return {}; }
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly)) { m_Status=tr("Cannot write the diagnostics archive."); return {}; }
    output.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    const auto archive=zip(files);
    if (output.write(archive)!=archive.size() || !output.commit()) { m_Status=tr("Cannot save the diagnostics archive."); return {}; }
    m_Bundle=target;
    m_Status=files.size()==2 ? tr("Archive contains version information only. Enable logs and reproduce the problem to collect events.") : tr("Archive ready. Review it, then drag the ZIP into your public GitHub issue. Nothing is uploaded automatically.");
    return target;
}
QUrl Diagnostics::issueUrl() {
    QUrl url("https://github.com/keithxc/deskport/issues/new"); QUrlQuery query;
    query.addQueryItem("title","[Feedback] ");
    query.addQueryItem("body","## What happened?\n\n## Steps to reproduce\n\n## Expected behavior\n\n## Diagnostics\nReview the locally generated DeskPort-diagnostics.zip, then drag it here to attach. This issue and its attachments are PUBLIC. Do not include credentials or private content. DeskPort does not upload or attach files automatically.\n");
    url.setQuery(query); return url;
}
bool Diagnostics::feedback() {
    const bool ready=!createBundle().isEmpty();
    if (ready && !QDesktopServices::openUrl(issueUrl())) m_Status+=tr(" Could not open the browser. Open github.com/keithxc/deskport/issues/new manually.");
    emit changed(); return ready;
}
void Diagnostics::showBundle() { if (!m_Bundle.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_Directory)); }
void Diagnostics::clear() {
    { QMutexLocker lock(&m_Mutex);
      if (!QFileInfo(m_Directory).isSymLink()) { for (const auto& name : names()) QFile::remove(m_Directory+"/"+name); QFile::remove(m_Directory+"/DeskPort-diagnostics.zip"); }
      m_Bundle.clear(); m_Pending.clear(); m_Status=tr("Saved diagnostics cleared."); }
    emit changed();
}
