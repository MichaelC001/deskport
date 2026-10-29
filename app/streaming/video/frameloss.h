#pragma once
#include <cstdint>

// Called at the network worker boundary, before any local presentation queue.
// A cumulative count survives local frame drops and is scoped to one decoder.
class FrameLossCounter {
public:
    void observe(uint32_t frame) {
        if (m_Seen) {
            const uint32_t distance = frame - m_Last;
            // Ignore duplicate, stale or reordered frames rather than allowing
            // unsigned wrap to create an enormous false loss count.
            if (distance > 1 && distance < 0x80000000u) m_Lost += distance - 1;
            if (distance && distance < 0x80000000u) m_Last = frame;
        } else { m_Seen = true; m_Last = frame; }
    }
    // After a local rejection the stream skips frames until an IDR. That gap
    // was caused locally, so the next observed frame becomes a new baseline.
    void resynchronize() { m_Seen = false; }
    uint64_t lost() const { return m_Lost; }
private:
    uint32_t m_Last = 0;
    uint64_t m_Lost = 0;
    bool m_Seen = false;
};
