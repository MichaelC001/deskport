// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace deskport {
// Policy is platform independent. Adapters must explicitly qualify a codec /
// driver path before opting in; an encoder name alone is not a capability.
struct RateControlDecision {
    bool boundedVbr;
    bool retryOriginalOnFailure;
};
inline RateControlDecision rateControl(bool smart, bool qualified, int configuredMode, int attempt) {
    // Vulkan enum: 2 = CBR. Preserve explicit CQP, VBR and driver-default modes.
    const bool use = smart && qualified && configuredMode == 2 && attempt == 0;
    return {use, use};
}

// Match recovery requests against output PTS, not the most recently submitted
// frame. Hardware codecs may return an older packet from their asynchronous queue.
class KeyframeRequests {
public:
    void submitted(std::int64_t pts, bool key) {
        if (!key) return;
        if (pending.size() == 64) pending.pop_front();
        pending.push_back(pts);
    }
    bool take(std::int64_t pts) {
        auto it = std::find(pending.begin(), pending.end(), pts);
        if (it == pending.end()) return false;
        pending.erase(it);
        return true;
    }
private:
    std::deque<std::int64_t> pending;
};

// Encoded elementary-stream bytes, before packet headers, encryption and FEC.
// Owned by one session and updated only by the video broadcast thread.
struct EncodedWindow {
    std::int64_t startMs = -1;
    std::uint64_t bytes = 0, packets = 0, keys = 0;
    std::size_t maxPacketBytes = 0;
    bool add(std::int64_t nowMs, std::size_t size, bool key) {
        if (startMs < 0) startMs = nowMs;
        bytes += size;
        ++packets;
        keys += key;
        maxPacketBytes = std::max(maxPacketBytes, size);
        return nowMs - startMs >= 5000;
    }
    double mbps(std::int64_t nowMs) const {
        return nowMs > startMs ? bytes * 8.0 / (nowMs - startMs) / 1000.0 : 0.0;
    }
    void reset(std::int64_t nowMs) { *this = {}; startMs = nowMs; }
};
}
