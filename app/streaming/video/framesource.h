#pragma once

#include <Limelight.h>

// A decoder owns no protocol state. The source outlives the decoder, including
// its decode thread. Handles remain valid until complete() is called once.
class VideoFrameSource {
public:
    virtual ~VideoFrameSource() = default;
    virtual bool wait(VIDEO_FRAME_HANDLE* handle, PDECODE_UNIT* unit) = 0;
    virtual bool poll(VIDEO_FRAME_HANDLE* handle, PDECODE_UNIT* unit) = 0;
    virtual void complete(VIDEO_FRAME_HANDLE handle, int status) = 0;
    // Must also interrupt a wait entered immediately after this call.
    virtual void wake() = 0;
    virtual void requestIdr() = 0;
    virtual bool hdrMetadata(PSS_HDR_METADATA metadata) = 0;
    virtual bool hdrActive() = 0;
    virtual bool rtt(uint32_t* latency, uint32_t* variance) = 0;
};

// Compatibility adapter for the single-session command-line worker.
class MoonlightFrameSource final : public VideoFrameSource {
public:
    bool wait(VIDEO_FRAME_HANDLE* handle, PDECODE_UNIT* unit) override { return LiWaitForNextVideoFrame(handle, unit); }
    bool poll(VIDEO_FRAME_HANDLE* handle, PDECODE_UNIT* unit) override { return LiPollNextVideoFrame(handle, unit); }
    void complete(VIDEO_FRAME_HANDLE handle, int status) override { LiCompleteVideoFrame(handle, status); }
    void wake() override { LiWakeWaitForVideoFrame(); }
    void requestIdr() override { LiRequestIdrFrame(); }
    bool hdrMetadata(PSS_HDR_METADATA metadata) override { return LiGetHdrMetadata(metadata); }
    bool hdrActive() override { return LiGetCurrentHostDisplayHdrMode(); }
    bool rtt(uint32_t* latency, uint32_t* variance) override { return LiGetEstimatedRttInfo(latency, variance); }
};
