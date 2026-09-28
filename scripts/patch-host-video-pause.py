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
  inline std::chrono::steady_clock::time_point video_resume_time;
  inline unsigned long long video_frames_sent = 0, video_frames_suppressed = 0;
  inline void reset_video() {
    std::lock_guard lock {video_mutex};
    video_paused = false; video_needs_idr = false;
  }
  inline std::recursive_mutex mutex;''')
edit('rtsp.h', '#include <mutex>', '#include <mutex>\n#include <chrono>')
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
            output["status"] = true;
            output["paused"] = paused;
            output["framesSent"] = deskport_admission::video_frames_sent;
            output["framesSuppressed"] = deskport_admission::video_frames_suppressed;
          }
        } else if (action == "release") {''')
edit('confighttp.cpp', '            deskport_admission::certificate.clear(); deskport_admission::lease.clear();', '            deskport_admission::reset_video();\n            deskport_admission::certificate.clear(); deskport_admission::lease.clear();')
edit('confighttp.cpp', '            rtsp_stream::terminate_sessions();', '            rtsp_stream::terminate_sessions();\n            deskport_admission::reset_video();')
edit('stream.cpp', '#include "stream.h"', '#include "stream.h"\n#include "rtsp.h"')
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
      ++deskport_admission::video_frames_sent;
      auto lowseq = session->video.lowseq;''')
patch = ''.join(''.join(difflib.unified_diff(original[p].splitlines(True), s.splitlines(True),
    fromfile='a/' + str(p.relative_to(root)), tofile='b/' + str(p.relative_to(root)))) for p, s in changes.items())
for p, s in changes.items(): p.write_text(s)
marker.write_text(patch)
