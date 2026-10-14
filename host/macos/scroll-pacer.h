// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>

namespace deskport {
// VideoToolbox's motion search was measured to lose a vertical text scroll
// between 88 and 96 px per frame: quality collapsed from 51 to ~20 dB PSNR at
// any bitrate, while the same distance spread over four frames stayed at 51 dB
// using a sixth of the bits. Pace injected scroll so one captured frame moves
// at most kMaxScrollStep points (80 px even on a 2x backing capture).
constexpr int kMaxScrollStep = 40;
constexpr int kScrollStepMs = 16;

struct ScrollStep {
    int vertical = 0, horizontal = 0;
    bool empty() const { return !vertical && !horizontal; }
};

// Accumulates scroll distance and releases it in bounded steps. Total distance
// and direction are preserved; opposite input cancels pending distance.
class ScrollPacer {
public:
    void add(int vertical, int horizontal) { pendingV += vertical; pendingH += horizontal; }
    bool pending() const { return pendingV || pendingH; }
    ScrollStep take() {
        const ScrollStep step {std::clamp(pendingV, -kMaxScrollStep, kMaxScrollStep),
                               std::clamp(pendingH, -kMaxScrollStep, kMaxScrollStep)};
        pendingV -= step.vertical;
        pendingH -= step.horizontal;
        return step;
    }
    void clear() { pendingV = pendingH = 0; }
private:
    int pendingV = 0, pendingH = 0;
};
}
