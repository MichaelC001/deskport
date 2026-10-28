#!/usr/bin/env python3
"""Gate video transmission on the authenticated admission lease, on both pins."""
import difflib
import subprocess
import sys
from pathlib import Path
root = Path(sys.argv[1])
marker = root / 'deskport-video-pause.patch'
if marker.exists():
    subprocess.run(['git', 'apply', '--reverse', '--check', str(marker.resolve())], cwd=root, check=True)
    if '--revert' in sys.argv:
        subprocess.run(['git', 'apply', '--reverse', str(marker.resolve())], cwd=root, check=True)
        marker.unlink()
    raise SystemExit(0)
if '--revert' in sys.argv:
    raise SystemExit(0)
original, changes = {}, {}
def edit(name, anchor, replacement):
    path = root / 'src' / name
    source = changes.get(path, path.read_text())
    original.setdefault(path, source)
    if source.count(anchor) != 1:
        raise SystemExit(f'Expected one video pause anchor in {name}: {anchor!r}')
    changes[path] = source.replace(anchor, replacement, 1)
edit('rtsp.h', '  inline std::recursive_mutex mutex;', '''  // Separate from admission: terminating streams joins the sender while
  // holding admission. The sender must never acquire the admission mutex.
  inline std::mutex video_mutex;
  inline bool video_paused = false, video_needs_idr = false;
  // Encoders read this without the sender lock. While set they skip encoding:
  // unsent frames would still advance the frame index, and a jump past half of
  // the viewer's 16-bit frame window made it reject every resumed frame.
  inline std::atomic<bool> video_encode_paused {false};
  inline std::chrono::steady_clock::time_point video_resume_time;
  inline unsigned long long video_frames_sent = 0, video_frames_suppressed = 0;
  inline unsigned long long video_key_frames_sent = 0, video_last_frame = 0;
  inline void reset_video() {
    std::lock_guard lock {video_mutex};
    video_paused = false; video_needs_idr = false;
    video_encode_paused = false;
  }
  inline std::recursive_mutex mutex;''')
edit('rtsp.h', '#include <mutex>', '#include <mutex>\n#include <atomic>\n#include <chrono>')
edit('confighttp.cpp', '        if (action == "release") {', '''        if (action == "video") {
          if (deskport_admission::lease.empty() || input.value("lease", "") != deskport_admission::lease ||
              !input.contains("paused") || !input["paused"].is_boolean() || rtsp_stream::session_count() > 1) {
            output["status"] = false; output["code"] = "unauthorized";
          } else {
            std::lock_guard lock {deskport_admission::video_mutex};
            const bool paused = input["paused"].get<bool>();
            if (deskport_admission::video_paused && !paused) {
              deskport_admission::video_needs_idr = true;
              deskport_admission::video_resume_time = std::chrono::steady_clock::now();
            }
            deskport_admission::video_paused = paused;
            // After video_needs_idr: a resumed encoder's first frame is a key
            // frame encoded after video_resume_time, which the sender accepts.
            deskport_admission::video_encode_paused = paused;
            output["status"] = true;
            output["paused"] = paused;
            output["framesSent"] = deskport_admission::video_frames_sent;
            output["framesSuppressed"] = deskport_admission::video_frames_suppressed;
          }
        } else if (action == "release") {''')
edit('confighttp.cpp', '            deskport_admission::certificate.clear(); deskport_admission::lease.clear();', '            deskport_admission::reset_video();\n            deskport_admission::certificate.clear(); deskport_admission::lease.clear();')
edit('confighttp.cpp', '            rtsp_stream::terminate_sessions();', '            rtsp_stream::terminate_sessions();\n            deskport_admission::reset_video();')
edit('confighttp.cpp', '    output["reserved"] = !deskport_admission::lease.empty();', '''    output["reserved"] = !deskport_admission::lease.empty();
    // Read-only telemetry under the same authenticated loopback API. This
    // observes the sender without sending another state-changing command.
    {
      std::lock_guard lock {deskport_admission::video_mutex};
      output["video"] = {{"paused", deskport_admission::video_paused},
                         {"framesSent", deskport_admission::video_frames_sent},
                         {"framesSuppressed", deskport_admission::video_frames_suppressed},
                         {"keyFramesSent", deskport_admission::video_key_frames_sent},
                         {"lastFrame", deskport_admission::video_last_frame}};
    }''')
edit('stream.cpp', '#include "stream.h"', '#include "stream.h"\n#include "rtsp.h"')
edit('video.cpp', '#include "video.h"', '#include "video.h"\n#include "rtsp.h"')
# Asynchronous encoder: keep converting captured images so the retained picture
# is current, but do not encode while paused; resume with a key frame.
edit('video.cpp', '    deskport::SaverGate deskport_saver_gate;', '''    deskport::SaverGate deskport_saver_gate;
    bool deskport_encode_paused = false;''')
edit('video.cpp', '''      last_encoded = std::chrono::steady_clock::now();
      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {''', '''      if (deskport_admission::video_encode_paused) {
        deskport_encode_paused = true;
        std::this_thread::sleep_for(10ms);
        continue;
      }
      if (deskport_encode_paused) {
        deskport_encode_paused = false;
        session->request_idr_frame();
      }
      last_encoded = std::chrono::steady_clock::now();
      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {''')
# Synchronous encoders (Linux capture loop): the same per session.
edit('video.cpp', '    int deskport_last_floor = 0;', '    int deskport_last_floor = 0;\n    bool deskport_encode_paused = false;')
edit('video.cpp', '''          if (encode(ctx->frame_nr++, *pos->session, ctx->packets, ctx->channel_data, frame_timestamp)) {''', '''          if (deskport_admission::video_encode_paused) {
            pos->deskport_encode_paused = true;
            ++pos;
            continue;
          }
          if (pos->deskport_encode_paused) {
            pos->deskport_encode_paused = false;
            pos->session->request_idr_frame();
          }
          if (encode(ctx->frame_nr++, *pos->session, ctx->packets, ctx->channel_data, frame_timestamp)) {''')
edit('video.h', '    void *channel_data = nullptr;', '    const std::chrono::steady_clock::time_point deskport_encoded_at = std::chrono::steady_clock::now();\n    void *channel_data = nullptr;')
edit('stream.cpp', '      auto lowseq = session->video.lowseq;', '''      // Keep the lock through the complete frame send. A successful pause
      // acknowledgment therefore fences all video UDP sends (in-flight network
      // packets can still arrive). Audio and control retain their own threads.
      std::unique_lock video_lock {deskport_admission::video_mutex};
      if (deskport_admission::video_paused) {
        ++deskport_admission::video_frames_suppressed;
        continue;
      }
      if (deskport_admission::video_needs_idr) {
        if (!packet->is_idr() ||
            packet->deskport_encoded_at < deskport_admission::video_resume_time) {
          session->video.idr_events->raise(true);
          ++deskport_admission::video_frames_suppressed;
          continue;
        }
        deskport_admission::video_needs_idr = false;
      }
      auto lowseq = session->video.lowseq;''')
edit('stream.cpp', '        session->video.lowseq = lowseq;', '''        session->video.lowseq = lowseq;
        ++deskport_admission::video_frames_sent;
        if (packet->is_idr()) ++deskport_admission::video_key_frames_sent;
        deskport_admission::video_last_frame = packet->frame_index();''')
patch = ''.join(''.join(difflib.unified_diff(original[p].splitlines(True), s.splitlines(True),
    fromfile='a/' + str(p.relative_to(root)), tofile='b/' + str(p.relative_to(root)))) for p, s in changes.items())
for p, s in changes.items(): p.write_text(s)
marker.write_text(patch)
