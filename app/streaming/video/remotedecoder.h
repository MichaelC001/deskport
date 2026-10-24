#pragma once
#include "decoder.h"
#include <QByteArray>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// The network worker forwards encoded units. It owns no visible video surface.
class RemoteVideoDecoder final : public IVideoDecoder {
public:
    ~RemoteVideoDecoder() override;
    bool initialize(PDECODER_PARAMETERS params) override;
    bool isHardwareAccelerated() override { return m_Hardware; }
    bool isAlwaysFullScreen() override { return false; }
    bool isHdrSupported() override { return m_Hdr; }
    int getDecoderCapabilities() override { return CAPABILITY_DIRECT_SUBMIT; }
    int getDecoderColorspace() override { return m_Colorspace; }
    int getDecoderColorRange() override { return m_ColorRange; }
    QSize getDecoderMaxResolution() override { return {}; }
    int submitDecodeUnit(PDECODE_UNIT unit) override;
    void renderFrameOnMainThread() override {}
    void setHdrMode(bool) override {}
    bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO) override { return true; }
private:
    void run(QByteArray config);
    std::mutex m_Mutex;
    std::condition_variable m_Ready;
    std::deque<QByteArray> m_Queue;
    std::thread m_Thread;
    bool m_Stopped = false, m_Initialized = false, m_Failed = false, m_NeedsIdr = true;
    bool m_Hardware = false, m_Hdr = false;
    int m_Colorspace = COLORSPACE_REC_601, m_ColorRange = COLOR_RANGE_LIMITED;
    int m_Bytes = 0;
    quint64 m_Generation = 0;
};
