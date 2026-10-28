#include "app/streaming/resumewatchdog.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
using Action = ResumeWatchdog::Action;

int main() {
    {   // Never paused: nothing to watch, however long no frame arrives.
        ResumeWatchdog w;
        for (int64_t t = 0; t <= 20000; t += 100) assert(w.update(false, 0, t) == Action::None);
    }
    {   // A frame after resume ends the watch.
        ResumeWatchdog w;
        assert(w.update(true, 10, 0) == Action::None);
        assert(w.update(false, 10, 1000) == Action::None);
        assert(w.update(false, 11, 1200) == Action::None);
        for (int64_t t = 1200; t <= 20000; t += 100) assert(w.update(false, 11, t) == Action::None);
    }
    {   // No frame: one key frame request at 3 s, reconnect at 8 s, then quiet.
        ResumeWatchdog w;
        assert(w.update(true, 10, 0) == Action::None);
        assert(w.update(false, 10, 1000) == Action::None);
        int keys = 0, reconnects = 0; int64_t keyAt = 0, reconnectAt = 0;
        for (int64_t t = 1000; t <= 20000; t += 100) {
            const auto a = w.update(false, 10, t);
            if (a == Action::RequestKeyFrame) { ++keys; keyAt = t; }
            if (a == Action::Reconnect) { ++reconnects; reconnectAt = t; }
        }
        assert(keys == 1 && keyAt == 1000 + ResumeWatchdog::KeyFrameMs);
        assert(reconnects == 1 && reconnectAt == 1000 + ResumeWatchdog::ReconnectMs);
    }
    {   // The key frame request recovers the stream: no reconnect.
        ResumeWatchdog w;
        w.update(true, 10, 0); w.update(false, 10, 0);
        assert(w.update(false, 10, 3000) == Action::RequestKeyFrame);
        assert(w.update(false, 12, 3300) == Action::None);
        for (int64_t t = 3300; t <= 20000; t += 100) assert(w.update(false, 12, t) == Action::None);
    }
    {   // Hiding again before a frame cancels the watch; the next resume restarts it.
        ResumeWatchdog w;
        w.update(true, 10, 0); w.update(false, 10, 0);
        assert(w.update(false, 10, 2000) == Action::None);
        assert(w.update(true, 10, 2500) == Action::None);
        for (int64_t t = 2500; t <= 20000; t += 100) assert(w.update(true, 10, t) == Action::None);
        assert(w.update(false, 10, 30000) == Action::None);
        assert(w.update(false, 10, 32999) == Action::None);
        assert(w.update(false, 10, 33000) == Action::RequestKeyFrame);
    }
    std::cout << "PASS: video resume watchdog\n";
}
