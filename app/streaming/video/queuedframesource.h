#pragma once

#include "encodedframe.h"
#include "framesource.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

// One source per stream generation, one decode consumer. Backpressure discards
// the whole dependent chain and waits for an IDR instead of decoding corrupt
// inter frames or growing a latency queue. Callbacks run without the queue lock.
class QueuedVideoSource final : public VideoFrameSource {
public:
    explicit QueuedVideoSource(quint64 generation, std::function<void()> requestIdr)
        : m_Generation(generation), m_RequestIdr(std::move(requestIdr)) {}

    bool push(std::unique_ptr<EncodedVideoFrame> frame) {
        bool request = false;
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (!frame || frame->generation != m_Generation || m_Stopped) return false;
            if (m_Frames.size() >= 3 || frame->unit.fullLength > MaxQueuedBytes - m_Bytes) {
                m_Frames.clear(); m_Bytes = 0;
                request = !m_NeedsIdr;
                m_NeedsIdr = true;
            }
            if (!m_NeedsIdr || frame->unit.frameType == FRAME_TYPE_IDR) {
                m_NeedsIdr = false;
                m_Bytes += frame->unit.fullLength;
                m_Frames.push_back(std::move(frame));
                accepted = true;
            }
        }
        m_Ready.notify_one();
        if (request) requestIdr();
        return accepted;
    }
    bool wait(VIDEO_FRAME_HANDLE* handle, PDECODE_UNIT* unit) override {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Ready.wait(lock, [this] { return m_Stopped || m_Wake || !m_Frames.empty(); });
        if (m_Stopped || m_Wake) { m_Wake = false; return false; }
        return take(handle, unit);
    }
    bool poll(VIDEO_FRAME_HANDLE* handle, PDECODE_UNIT* unit) override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return !m_Stopped && take(handle, unit);
    }
    void complete(VIDEO_FRAME_HANDLE handle, int status) override {
        delete static_cast<EncodedVideoFrame*>(handle);
        if (status != DR_OK) {
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Frames.clear(); m_Bytes = 0; m_NeedsIdr = true;
            }
            requestIdr();
        }
    }
    void wake() override {
        { std::lock_guard<std::mutex> lock(m_Mutex); m_Wake = true; }
        m_Ready.notify_one();
    }
    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stopped = true; m_Frames.clear(); m_Bytes = 0;
        }
        m_Ready.notify_one();
    }
    void requestIdr() override { if (m_RequestIdr) m_RequestIdr(); }
    void updateMetadata(bool active, const SS_HDR_METADATA* hdr, uint32_t latency, uint32_t variance) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_HdrActive = active; m_HasMetadata = hdr != nullptr;
        if (hdr) m_Hdr = *hdr;
        m_Rtt = latency; m_Variance = variance;
    }
    bool hdrMetadata(PSS_HDR_METADATA metadata) override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_HasMetadata) *metadata = m_Hdr;
        return m_HasMetadata;
    }
    bool hdrActive() override {
        std::lock_guard<std::mutex> lock(m_Mutex); return m_HdrActive;
    }
    bool rtt(uint32_t* latency, uint32_t* variance) override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        *latency = m_Rtt; *variance = m_Variance; return m_Rtt != 0;
    }
private:
    bool take(VIDEO_FRAME_HANDLE* handle, PDECODE_UNIT* unit) {
        if (m_Frames.empty()) return false;
        auto frame = m_Frames.front().release(); m_Frames.pop_front();
        m_Bytes -= frame->unit.fullLength;
        *handle = frame; *unit = &frame->unit;
        return true;
    }
    static constexpr int MaxQueuedBytes = 64 * 1024 * 1024;
    const quint64 m_Generation;
    const std::function<void()> m_RequestIdr;
    std::mutex m_Mutex;
    std::condition_variable m_Ready;
    std::deque<std::unique_ptr<EncodedVideoFrame>> m_Frames;
    int m_Bytes = 0;
    bool m_Wake = false, m_Stopped = false, m_NeedsIdr = true;
    bool m_HdrActive = false, m_HasMetadata = false;
    SS_HDR_METADATA m_Hdr{};
    uint32_t m_Rtt = 0, m_Variance = 0;
};
