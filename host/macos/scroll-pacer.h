// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>

namespace deskport {
// VideoToolbox's motion search was measured to lose a vertical text scroll
// between 88 and 96 px per encoded frame, at 2560x1440 and 5104x2720 alike:
// quality collapsed from ~50 to ~20 dB PSNR at any bitrate, while the same
// distance spread over more frames kept ~50 dB with far fewer bits.
// Steps are bounded in captured pixels: a 2x display doubles every point, and
// a 5K encode can sample only every other 60 Hz frame, so 40 px per 16 ms keeps
// one encoded frame at or under ~80 px.
constexpr int kMaxScrollStepPixels = 40;
constexpr int kScrollStepMs = 16;
inline int scrollStepPoints(double backingScale) {
    return std::max(4, static_cast<int>(kMaxScrollStepPixels / std::max(1.0, backingScale)));
}

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
    ScrollStep take(int maxStep) {
        maxStep = std::max(1, maxStep);
        const ScrollStep step {std::clamp(pendingV, -maxStep, maxStep),
                               std::clamp(pendingH, -maxStep, maxStep)};
        pendingV -= step.vertical;
        pendingH -= step.horizontal;
        return step;
    }
    void clear() { pendingV = pendingH = 0; }
private:
    int pendingV = 0, pendingH = 0;
};
}
