#pragma once
#include <cstdint>

// Watches the first frame after a background video pause ends. The host
// answers a resume with a new key frame, so a silent stream means the frames
// are lost or rejected. Ask once more for a key frame, then reconnect, rather
// than leaving a frozen picture until the user reconnects by hand.
class ResumeWatchdog {
public:
    enum class Action { None, RequestKeyFrame, Reconnect };
    static constexpr int64_t KeyFrameMs = 3000, ReconnectMs = 8000;
    // Called on every pass with the desired pause state and the number of
    // frames handed to the decoder so far.
    Action update(bool paused, uint64_t frames, int64_t now) {
        if (paused) { m_WasPaused = true; m_Active = false; return Action::None; }
        if (m_WasPaused) {
            m_WasPaused = false; m_Active = true; m_KeyRequested = false;
            m_Since = now; m_Frames = frames;
            return Action::None;
        }
        if (!m_Active) return Action::None;
        if (frames != m_Frames) { m_Active = false; return Action::None; }
        if (now - m_Since >= ReconnectMs) { m_Active = false; return Action::Reconnect; }
        if (now - m_Since >= KeyFrameMs && !m_KeyRequested) { m_KeyRequested = true; return Action::RequestKeyFrame; }
        return Action::None;
    }
private:
    bool m_WasPaused = false, m_Active = false, m_KeyRequested = false;
    int64_t m_Since = 0;
    uint64_t m_Frames = 0;
};
