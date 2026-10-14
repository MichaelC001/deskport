// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "smartstream.h"
#include "inputactivity.h"

namespace deskport {
// A still picture converges over several duplicate encodes. After content
// stops changing, keep duplicates at the full cadence briefly instead of the
// one-fps idle floor; measured text took about six seconds to sharpen at 1 fps.
constexpr std::int64_t kRefineMs = 500;
inline int refineFrameRate(std::int64_t nowMs, std::int64_t lastChangeMs, int ceiling) {
    return lastChangeMs >= 0 && nowMs - lastChangeMs < kRefineMs ? std::max(1, ceiling) : 1;
}

// Capture adapters report changed content; lack of input never suppresses video.
// Keep pending content across denied slots so the final desktop update is sent.
class FrameCadence {
public:
    explicit FrameCadence(int fps, std::int64_t startMs = 0) : policy(fps, startMs) {}
    StreamPolicy policy;
    ActivityBoost boost;
    int floor(std::int64_t nowUs) {
        const int ceiling = policy.frameRate(nowUs / 1000);
        return std::max(activityFrameRate(boost, nowUs / 1000, ceiling),
                        refineFrameRate(nowUs / 1000, lastChange / 1000, ceiling));
    }
    bool admit(std::int64_t nowUs, bool changed, bool recovery) {
        if (changed) lastChange = nowUs;
        pending = pending || changed;
        const int fps = floor(nowUs);
        if (recovery || pending) {
            if (!policy.admit(nowUs, recovery)) return false;
        } else if (sent && nowUs - lastEncoded < 1000000 / fps) {
            return false;
        }
        lastEncoded = nowUs;
        pending = false;
        sent = true;
        return true;
    }
private:
    bool pending = false, sent = false;
    std::int64_t lastEncoded = 0, lastChange = -1000000000;
};
}
