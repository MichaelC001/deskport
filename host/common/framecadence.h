// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "smartstream.h"
#include "inputactivity.h"

namespace deskport {
// Capture adapters report changed content; lack of input never suppresses video.
// Keep pending content across denied slots so the final desktop update is sent.
class FrameCadence {
public:
    explicit FrameCadence(int fps, std::int64_t startMs = 0) : policy(fps, startMs) {}
    StreamPolicy policy;
    ActivityBoost boost;
    int floor(std::int64_t nowUs) {
        return activityFrameRate(boost, nowUs / 1000, policy.frameRate(nowUs / 1000));
    }
    bool admit(std::int64_t nowUs, bool changed, bool recovery) {
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
    std::int64_t lastEncoded = 0;
};
}
