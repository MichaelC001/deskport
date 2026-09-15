#pragma once
#include "native.h"
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QTemporaryDir>
#include <QTimer>
#include <QEventLoop>
#include <memory>

namespace ClipboardV2 {
constexpr int Chunk = 256 * 1024;
constexpr qint64 MaxImage = 128LL * 1024 * 1024;
constexpr qint64 MaxFiles = 16LL * 1024 * 1024 * 1024;
constexpr int MaxEntries = 4096;
constexpr int Frame = 180 * 1024 * 1024; // existing 128 MiB UTF-8 text compatibility
bool safeRelativePath(const QString& path);
bool validManifest(const QJsonArray& files);
}
class ClipboardAgent : public QObject {
public:
    using Sender = std::function<void(const QJsonObject&)>;
    ClipboardAgent(std::unique_ptr<ClipboardNative> native, Sender sender, bool authoritative = false);
    ~ClipboardAgent() override;
    void receive(const QJsonObject& message);
    void observe();
    void stop();
    bool valid() const { return m_Native->valid(); }
private:
    void applyOffer(const QJsonObject& message);
    QJsonObject request(QJsonObject message);
    QByteArray materialize(const QString& id, const QString& mime);
    QJsonObject serve(const QJsonObject& message);
    bool snapshotFiles(const QByteArray& urls);
    void status(const QString& text);
    std::unique_ptr<ClipboardNative> m_Native;
    Sender m_Send;
    QTimer m_Timer;
    qint64 m_Seen = -1, m_LocalRevision = -1;
    QString m_LocalId, m_RemoteId, m_Kind, m_ImageMime;
    QByteArray m_Image;
    QJsonArray m_Files;
    QStringList m_Paths;
    struct FileStamp { quint64 device, inode; };
    QList<FileStamp> m_FileStamps;
    QHash<QString, QByteArray> m_Cache;
    QHash<QString, QJsonObject> m_Replies;
    std::unique_ptr<QTemporaryDir> m_Temporary;
    bool m_Stopped = false, m_Busy = false, m_Serving = false;
    QEventLoop* m_WaitLoop = nullptr;
    int m_Request = 0;
    bool m_Authoritative = false, m_OfferInFlight = false, m_QueuedOffer = false;
    int m_Revision = 0;
    QJsonObject m_LastOffer, m_PendingOffer;
    qint64 m_CachedBytes = 0;
};
int runClipboardHelper(int argc, char** argv);
