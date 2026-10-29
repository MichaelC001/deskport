#!/usr/bin/env python3
"""Apply common smart streaming to both pinned host source revisions."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
def patch(name, old, new):
    p = root / name
    source = p.read_text()
    if new in source: return
    if source.count(old) != 1:
        raise SystemExit(f'Smart stream anchor mismatch: {name}: {old[:80]!r}')
    p.write_text(source.replace(old, new))

patch('src/video.cpp', '''#include "globals.h"
''', '''#include "globals.h"
#include "deskport/common/smartstream.h"
#include <cstdlib>
''')

patch('src/video.cpp', '''    // set max frame time based on client-requested target framerate.
    double minimum_fps_target = (config::video.minimum_fps_target > 0.0) ? config::video.minimum_fps_target : (config.framerate / 2);
''', '''    const bool smart = std::getenv("DESKPORT_SMART_STREAMING") &&
        std::string_view(std::getenv("DESKPORT_SMART_STREAMING")) == "1";
    deskport::StreamPolicy policy(config.framerate);
    auto congestion = mail->event<int>("deskport_congestion");
    const auto smart_start = std::chrono::steady_clock::now();
    int last_target = config.framerate;
    auto last_encoded = smart_start;

    // set max frame time based on client-requested target framerate.
    double minimum_fps_target = (config::video.minimum_fps_target > 0.0) ? config::video.minimum_fps_target : (config.framerate / 2);
    if (smart) minimum_fps_target = std::min(1.0, double(config.framerate));
''')

patch('src/video.cpp', '''      bool requested_idr_frame = false;
''', '''      bool requested_idr_frame = false;
      const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - smart_start).count();
      if (smart && congestion->pop(0ms)) policy.loss(now_us / 1000);
      if (smart && policy.frameRate(now_us / 1000) != last_target) {
        last_target = policy.frameRate(now_us / 1000);
        BOOST_LOG(info) << "DeskPort congestion frame-rate ceiling: " << last_target << " fps";
      }
''')

patch('src/video.cpp', '''        if (auto img = images->pop(max_frametime)) {
''', '''        // Leave the latest image in the mailbox while rate-limited. Consuming
        // and dropping it would lose a final text/cursor update on an idle screen.
        if (smart && images->peek()) {
          const auto captured_us = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - smart_start).count();
          if (!policy.admit(captured_us, requested_idr_frame)) {
            std::this_thread::sleep_for(2ms);
            continue;
          }
        }
        if (auto img = images->pop(smart ? std::chrono::duration<double, std::milli>(10ms) : max_frametime)) {
''')

patch('src/video.cpp', '''        } else if (!images->running()) {
          break;
''', '''        } else if (!images->running()) {
          break;
        } else if (smart && !requested_idr_frame && !shutdown_event->peek() && !reinit_event.peek() &&
                   std::chrono::steady_clock::now() - last_encoded < max_frametime) {
          continue; // Poll recovery/shutdown promptly without encoding duplicates.
''')

patch('src/video.cpp', '''      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {
''', '''      last_encoded = std::chrono::steady_clock::now();
      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {
''')

patch('src/stream.cpp', '''#include "sync.h"
''', '''#include "sync.h"
#include "deskport/common/smartstream.h"
''')

patch('src/stream.cpp', '''    server->map(packetTypes[IDX_LOSS_STATS], [&](session_t *session, const std::string_view &payload) {
''', '''    // Existing Sunshine FEC feedback is scoped to the authenticated stream.
    // Only final, unrecoverable blocks count; repaired loss must not lower FPS.
    server->map(0x5502, [](session_t *session, const std::string_view &payload) {
      if (deskport::unrecoverableFec(reinterpret_cast<const unsigned char *>(payload.data()), payload.size())) {
        session->mail->event<int>("deskport_congestion")->raise(1);
      }
    });

    server->map(packetTypes[IDX_LOSS_STATS], [&](session_t *session, const std::string_view &payload) {
''')
