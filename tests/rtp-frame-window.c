// Frame-index window of the RTP video queue across a long host video pause.
// A paused host may skip many frame indexes; the next complete frame must still
// be delivered. Links the real RtpVideoQueue.c with stubbed collaborators.
#include "Limelight-internal.h"
#include <stdio.h>
#include <stdlib.h>

// The common-c headers can disable assert(); fail explicitly instead.
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

// Collaborators of RtpVideoQueue.c outside the unit under test.
STREAM_CONFIGURATION StreamConfig;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
SERVER_INFORMATION ServerInfo;
int AppVersionQuad[4] = {7, 1, 431, 0};
static int delivered, lost;
static uint32_t lastLost;
void queueRtpPacket(PRTPV_QUEUE_ENTRY entry) { (void)entry; delivered++; }
void notifyFrameLost(unsigned int frameNumber, bool speculative) { (void)speculative; lost++; lastLost = frameNumber; }
void connectionSawFrame(uint32_t frameIndex) { (void)frameIndex; }
void connectionSendFrameFecStatus(PSS_FRAME_FEC_STATUS fecStatus) { (void)fecStatus; }
uint64_t PltGetMillis(void) { return 0; }

static uint16_t sequence;
// Adds a complete single-packet frame without parity; returns the queue status.
static int sendFrame(PRTP_VIDEO_QUEUE queue, uint32_t frameIndex) {
    const int length = sizeof(RTP_PACKET) + 4 + sizeof(NV_VIDEO_PACKET) + 16;
    char* buffer = calloc(1, length + sizeof(RTPV_QUEUE_ENTRY));
    PRTP_PACKET packet = (PRTP_PACKET)buffer;
    packet->header = FLAG_EXTENSION;
    packet->sequenceNumber = sequence++;
    PNV_VIDEO_PACKET nv = (PNV_VIDEO_PACKET)(buffer + sizeof(RTP_PACKET) + 4);
    nv->frameIndex = LE32(frameIndex);
    nv->flags = FLAG_SOF | FLAG_EOF | FLAG_CONTAINS_PIC_DATA;
    nv->fecInfo = LE32(1u << 22); // one data shard, no parity
    const int status = RtpvAddPacket(queue, packet, length, (PRTPV_QUEUE_ENTRY)&buffer[length]);
    // Delivered entries are owned by the stubbed depacketizer and leak here.
    if (status != RTPF_RET_QUEUED) free(buffer);
    return status;
}

int main(void) {
    const uint32_t jumps[] = {1, 34033, 32768, 65535, 70000, 0x7fffffffu};
    for (size_t i = 0; i < sizeof(jumps) / sizeof(jumps[0]); i++) {
        RTP_VIDEO_QUEUE queue;
        RtpvInitializeQueue(&queue);
        delivered = lost = 0;
        uint32_t frame = 1;
        for (; frame <= 5; frame++) CHECK(sendFrame(&queue, frame) == RTPF_RET_QUEUED);
        CHECK(delivered == 5 && lost == 0);
        // The host resumes after skipping jumps[i] - 1 frame indexes.
        frame += jumps[i] - 1;
        CHECK(sendFrame(&queue, frame) == RTPF_RET_QUEUED);
        CHECK(delivered == 6);
        CHECK(jumps[i] == 1 ? lost == 0 : lost == 1 && lastLost == frame - 1);
        CHECK(sendFrame(&queue, frame + 1) == RTPF_RET_QUEUED && delivered == 7);
        // A frame that really is older is still rejected.
        CHECK(sendFrame(&queue, frame - 1) == RTPF_RET_REJECTED && delivered == 7);
        RtpvCleanupQueue(&queue);
    }
    puts("PASS: RTP video queue accepts long frame-index jumps");
    return 0;
}
