#include "remotedecoder.h"
#include "encodedframe.h"
#include "streaming/canvasbridge.h"
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QtEndian>
#include <atomic>

namespace {
std::atomic<quint64> generation{0};
bool writePacket(QLocalSocket& socket, const QByteArray& bytes) {
    const quint32 length = qToBigEndian(quint32(bytes.size()));
    if (socket.write(reinterpret_cast<const char*>(&length), sizeof(length)) != sizeof(length) ||
        socket.write(bytes) != bytes.size()) return false;
    // Bound shutdown latency even when the shell stops consuming frames.
    QElapsedTimer deadline;
    deadline.start();
    while (socket.bytesToWrite()) {
        const auto remaining = 1000 - deadline.elapsed();
        if (remaining <= 0 || !socket.waitForBytesWritten(int(remaining))) return false;
    }
    return true;
}
}
RemoteVideoDecoder::~RemoteVideoDecoder() {
    { std::lock_guard<std::mutex> lock(m_Mutex); m_Stopped = true; }
    m_Ready.notify_one();
    if (m_Thread.joinable()) m_Thread.join();
}
bool RemoteVideoDecoder::initialize(PDECODER_PARAMETERS params) {
    m_Generation = ++generation;
    const QJsonObject config{{"generation",double(m_Generation)}, {"format",params->videoFormat},
        {"width",params->width}, {"height",params->height}, {"fps",params->frameRate},
        {"vsync",params->enableVsync}, {"pacing",params->enableFramePacing}, {"decoder",int(params->vds)}};
    m_Thread = std::thread(&RemoteVideoDecoder::run, this, QJsonDocument(config).toJson(QJsonDocument::Compact));
    std::unique_lock<std::mutex> lock(m_Mutex);
    return m_Ready.wait_for(lock, std::chrono::seconds(15), [this] { return m_Initialized || m_Failed; }) && m_Initialized;
}
int RemoteVideoDecoder::submitDecodeUnit(PDECODE_UNIT unit) {
    auto bytes = EncodedVideoFrame::encode(*unit, m_Generation);
    if (bytes.isEmpty()) return DR_NEED_IDR;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Stopped || m_Failed) return DR_NEED_IDR;
        if (m_Queue.size() >= 3 || bytes.size() > 64 * 1024 * 1024 - m_Bytes) {
            m_Queue.clear(); m_Bytes = 0; m_NeedsIdr = true;
        }
        if (m_NeedsIdr && unit->frameType != FRAME_TYPE_IDR) return DR_NEED_IDR;
        m_NeedsIdr = false;
        m_Bytes += bytes.size(); m_Queue.push_back(std::move(bytes));
    }
    m_Ready.notify_one();
    return DR_OK;
}
void RemoteVideoDecoder::run(QByteArray config) {
    const auto fail = [this] {
        bool reset;
        { std::lock_guard<std::mutex> lock(m_Mutex); m_Failed = true; reset = m_Initialized && !m_Stopped; }
        m_Ready.notify_one();
        if (reset) { SDL_Event event{}; event.type = SDL_RENDER_DEVICE_RESET; SDL_PushEvent(&event); }
    };
    QLocalSocket socket;
    socket.setReadBufferSize(16384);
    const auto transport = CanvasBridge::videoTransport();
    if (transport.first.isEmpty() || transport.second.isEmpty()) { fail(); return; }
    socket.connectToServer(transport.first);
    if (!socket.waitForConnected(5000)) { fail(); return; }
    const auto auth = QJsonDocument(QJsonObject{{"token",transport.second}}).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(auth) != auth.size() || !writePacket(socket, config)) { fail(); return; }
    QByteArray reply;
    QElapsedTimer handshake;
    handshake.start();
    while (!reply.contains('\n') && reply.size() < 16384) {
        const auto remaining = 5000 - handshake.elapsed();
        if (remaining <= 0 || (!socket.bytesAvailable() && !socket.waitForReadyRead(int(remaining)))) { fail(); return; }
        reply += socket.readAll();
    }
    const auto result = QJsonDocument::fromJson(reply.trimmed()).object();
    if (!result.value("ok").toBool()) { fail(); return; }
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Hardware = result.value("hardware").toBool(); m_Hdr = result.value("hdr").toBool();
        m_Colorspace = result.value("colorspace").toInt(COLORSPACE_REC_601);
        m_ColorRange = result.value("range").toInt(COLOR_RANGE_LIMITED);
        m_Initialized = true;
    }
    m_Ready.notify_one();
    for (;;) {
        QByteArray bytes;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Ready.wait(lock, [this] { return m_Stopped || !m_Queue.empty(); });
            if (m_Stopped) return;
            bytes = std::move(m_Queue.front()); m_Queue.pop_front(); m_Bytes -= bytes.size();
        }
        if (!writePacket(socket, bytes)) { fail(); return; }
    }
}
