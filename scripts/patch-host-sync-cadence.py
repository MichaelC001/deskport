#!/usr/bin/env python3
"""Use the same session policy in the synchronous (including VAAPI) encoder."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
p = root / 'src/video.cpp'
s = p.read_text()
if '#include "deskport/common/framecadence.h"' in s:
    raise SystemExit(0)
def patch(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit(f'Sync cadence anchor mismatch: {old[:80]!r}')
    s = s.replace(old, new)
patch('#include "deskport/common/inputactivity.h"', '#include "deskport/common/inputactivity.h"\n#include "deskport/common/framecadence.h"')
patch('  struct sync_session_ctx_t {', '''  struct sync_session_ctx_t {
    safe::mail_t deskport_mail;''')
patch('      ref->encode_session_ctx_queue.raise(sync_session_ctx_t {', '''      ref->encode_session_ctx_queue.raise(sync_session_ctx_t {
        mail,''')
patch('  struct sync_session_t {', '''  struct sync_session_t {
    std::optional<deskport::FrameCadence> deskport_cadence;
    std::optional<std::chrono::steady_clock::time_point> deskport_timestamp;
    int deskport_last_floor = 0;
    int deskport_last_ceiling = 0;''')
patch('    encode_session.session = std::move(session);', '''    encode_session.session = std::move(session);
    encode_session.deskport_cadence.emplace(ctx.config.framerate);''')
patch('          if (ctx->idr_events->peek()) {', '''          const bool deskport_recovery = ctx->idr_events->peek();
          if (deskport_recovery) {''')
old='''          std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
          if (img) {
            frame_timestamp = img->frame_timestamp;
          }
'''
patch(old, '''          std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
          if (frame_captured && img) {
            pos->deskport_timestamp = img->frame_timestamp;
          }
          if (ctx->config.deskport_smart) {
            auto &cadence = *pos->deskport_cadence;
            const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (ctx->deskport_mail->event<bool>("deskport_congestion")->pop(0ms))
              cadence.policy.loss(now_us / 1000);
            if (auto boost = ctx->deskport_mail->event<deskport::ActivityBoost>("deskport_activity")->pop(0ms))
              cadence.boost = *boost;
            const int floor = cadence.floor(now_us);
            const int ceiling = cadence.policy.frameRate(now_us / 1000);
            if (floor != pos->deskport_last_floor || ceiling != pos->deskport_last_ceiling) {
              BOOST_LOG(info) << "DeskPort input cadence: " << floor << " fps; congestion ceiling: " << ceiling;
              pos->deskport_last_floor = floor;
              pos->deskport_last_ceiling = ceiling;
            }
            if (!cadence.admit(now_us, frame_captured, deskport_recovery)) {
              ++pos;
              continue;
            }
            frame_timestamp = pos->deskport_timestamp;
            pos->deskport_timestamp.reset();
          } else if (img) {
            frame_timestamp = img->frame_timestamp;
          }
''')
p.write_text(s)
