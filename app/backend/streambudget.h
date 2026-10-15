// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace DeskPortStream {
// Resolution-aware desktop budget: 15 Mbps for 2560x1440 at 60 fps, scaled by
// pixels and frame rate, 5-80 Mbps. Slow links are adapted live by the host.
inline int automaticBitrate(int width, int height, int fps, bool yuv444) {
    const auto pixels = std::int64_t(std::max(1, width)) * std::max(1, height);
    const auto budget = pixels * std::min(120, std::max(1, fps)) * 15000 / (2560LL * 1440 * 60);
    return int(std::clamp<std::int64_t>(budget * (yuv444 ? 2 : 1), 5000, 80000));
}
// Data saver: a quarter of the automatic budget, 3-12 Mbps. Resolution is unchanged.
inline int dataSaverBitrate(int automatic) { return std::clamp(automatic / 4, 3000, 12000); }

// Automatic streaming ignores the saved manual bitrate; manual streaming keeps
// it. Data saver caps either one.
inline int sessionBitrate(bool automatic, bool dataSaver, int manualKbps, int width, int height, int fps, bool yuv444) {
    const int budget = automaticBitrate(width, height, fps, yuv444);
    const int chosen = automatic ? budget : std::max(1, manualKbps);
    return dataSaver ? std::min(chosen, dataSaverBitrate(budget)) : chosen;
}
}
