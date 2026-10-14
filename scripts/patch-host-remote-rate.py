#!/usr/bin/env python3
"""Remote-path rate control, pacing and contiguous frame numbering.

Applies to both pinned host revisions after the smart-stream and encoder-policy
overlays. Live encoder bitrate is capability-gated: only FFmpeg VideoToolbox
encoders implement it; other backends keep their negotiated configuration.
"""
from pathlib import Path
import sys
root = Path(sys.argv[1])

def patch(name, old, new):
    path = root / name
    source = path.read_text()
    if new in source:
        return
    if source.count(old) != 1:
        raise SystemExit(f'Remote rate anchor mismatch: {name}: {old[:80]!r}')
    path.write_text(source.replace(old, new))

for name in ('video.cpp', 'stream.cpp'):
    patch('src/' + name, '#include "deskport/common/encoderpolicy.h"',
          '#include "deskport/common/encoderpolicy.h"\n#include "deskport/common/ratecontrol.h"')

# Live bitrate hook. The default reports "unsupported" so the policy can log it.
patch('src/video.h', '    virtual void request_normal_frame() = 0;', '''    virtual void request_normal_frame() = 0;

    virtual bool deskport_set_bitrate(int kbps, bool constrained) {
      return false;
    }''')

patch('src/video.cpp', '#include "deskport/common/ratecontrol.h"', '''#include "deskport/common/ratecontrol.h"
#ifdef __APPLE__
  #include "deskport/macos/vtbitrate.h"
#endif
#include <mutex>

namespace {
  // Survives the stop/resume used by adaptive resize; one host process.
  std::mutex deskport_rate_memory_mutex;
  deskport::RateMemory deskport_rate_memory;
  std::int64_t deskport_wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
}''')

patch('src/video.cpp', '    deskport::KeyframeRequests deskport_keys;', '''    deskport::KeyframeRequests deskport_keys;
    std::int64_t deskport_last_pts = -1;
    std::int64_t deskport_pts_shift = 0;

    bool deskport_set_bitrate(int kbps, bool constrained) override {
#ifdef __APPLE__
      auto *vt = deskport::vt::session(avcodec_ctx.get());
      // QP 32 measurably shrinks content-change frames on a congested link.
      return deskport::vt::applyRate(vt, std::int64_t(kbps) * 1000) &&
             deskport::vt::applyQualityFloor(vt, constrained ? 32 : 0);
#else
      return false;
#endif
    }''')

# Rate-controlled encoders may drop a frame. A gap in frame numbers makes the
# client treat it as network loss and request an IDR, so number only the frames
# actually produced. Recovery matching and frame timestamps use the original PTS.
patch('src/video.cpp', '''      if (av_packet && av_packet->pts == frame_nr) {
        packet->frame_timestamp = frame_timestamp;
      }
''', '''      if (av_packet && av_packet->pts == frame_nr) {
        packet->frame_timestamp = frame_timestamp;
      }
      if (av_packet) {
        if (session.deskport_last_pts >= 0 && av_packet->pts > session.deskport_last_pts + 1) {
          session.deskport_pts_shift += av_packet->pts - session.deskport_last_pts - 1;
        }
        session.deskport_last_pts = av_packet->pts;
        av_packet->pts -= session.deskport_pts_shift;
      }
''')

patch('src/video.cpp', '      if (smart && congestion->pop(0ms)) policy.loss(now_us / 1000);', '''      const bool deskport_lost = smart && congestion->pop(0ms);
      if (deskport_lost) policy.loss(now_us / 1000);
      // Remote sessions receive RTT samples from the control thread; LAN
      // sessions never do, so they keep the negotiated bitrate.
      auto rtt = deskport_rtt->pop(0ms);
      if (smart && rtt) {
        if (!deskport_rate) {
          deskport_rate.emplace(deskport_ceiling_kbps, now_us / 1000);
          int resume = 0;
          {
            std::lock_guard lock {deskport_rate_memory_mutex};
            resume = deskport_rate_memory.resumeFor(deskport_ceiling_kbps, deskport_wall_ms());
          }
          deskport_live = session->deskport_set_bitrate(deskport_ceiling_kbps, false);
          if (deskport_live && resume) deskport_rate->resume(resume);
          BOOST_LOG(info) << "DeskPort remote rate control: ceiling_kbps=" << deskport_ceiling_kbps
                          << ", floor_kbps=" << deskport_rate->floorKbps()
                          << ", resume_kbps=" << resume
                          << ", live_encoder=" << (deskport_live ? "yes" : "no");
          deskport_applied_kbps = deskport_ceiling_kbps;
          mail->event<int>("deskport_rate_kbps")->raise(deskport_ceiling_kbps);
        }
        deskport_rate->rtt(now_us / 1000, *rtt);
        deskport_last_rtt = *rtt;
      }
      if (deskport_rate) {
        if (deskport_lost) deskport_rate->loss(now_us / 1000);
        const int kbps = deskport_rate->targetKbps(now_us / 1000);
        if (deskport_live && kbps != deskport_applied_kbps) {
          if (!session->deskport_set_bitrate(kbps, deskport_rate->constrained())) {
            BOOST_LOG(warning) << "DeskPort remote rate control: encoder rejected " << kbps << " kbps";
            deskport_live = false;
          } else {
            BOOST_LOG(info) << "DeskPort remote rate: target_kbps=" << kbps << ", rtt_ms=" << deskport_last_rtt
                            << ", baseline_ms=" << deskport_rate->baselineMs()
                            << ", quality_floor=" << (deskport_rate->constrained() ? "on" : "off");
            deskport_applied_kbps = kbps;
            mail->event<int>("deskport_rate_kbps")->raise(kbps);
            std::lock_guard lock {deskport_rate_memory_mutex};
            deskport_rate_memory.store(deskport_ceiling_kbps, kbps, deskport_wall_ms());
          }
        }
      }''')

patch('src/video.cpp', '''    deskport::StreamPolicy policy(config.framerate);
    auto congestion = mail->event<int>("deskport_congestion");''', '''    deskport::StreamPolicy policy(config.framerate);
    auto congestion = mail->event<int>("deskport_congestion");
    auto deskport_rtt = mail->event<int>("deskport_rtt");
    std::optional<deskport::BitrateController> deskport_rate;
    const int deskport_ceiling_kbps = config::video.max_bitrate > 0 ? std::min(config.bitrate, config::video.max_bitrate) : config.bitrate;
    int deskport_applied_kbps = deskport_ceiling_kbps;
    int deskport_last_rtt = 0;
    bool deskport_live = false;''')

# Per-session transport state.
patch('src/stream.cpp', '''    deskport::EncodedWindow deskport_encoded;''', '''    deskport::EncodedWindow deskport_encoded;
    std::atomic<int> deskport_remote {-1};
    int deskport_pace_kbps = 0;
    std::chrono::steady_clock::time_point deskport_rtt_sample {};''')

# Sample the existing ENet control RTT for remote sessions only.
patch('src/stream.cpp', '''          if (!session->control.peer) {
            has_session_awaiting_peer = true;
          } else {''', '''          if (session->control.peer && session->deskport_remote.load(std::memory_order_relaxed) == 1 &&
              now - session->deskport_rtt_sample >= 500ms) {
            session->deskport_rtt_sample = now;
            session->mail->event<int>("deskport_rtt")->raise((int) session->control.peer->roundTripTime);
          }

          if (!session->control.peer) {
            has_session_awaiting_peer = true;
          } else {''')

# Remote sessions pace at a multiple of the live target instead of ~800 Mbps.
patch('src/stream.cpp', '''        size_t ratecontrol_packets_in_1ms = std::giga::num * 80 / 100 / 1000 / blocksize / 8;''', '''        size_t ratecontrol_packets_in_1ms = std::giga::num * 80 / 100 / 1000 / blocksize / 8;
        if (session->deskport_remote.load(std::memory_order_relaxed) < 0) {
          const auto address = session->video.peer.address();
          bool remote = false;
          if (address.is_v4()) {
            const auto bytes = address.to_v4().to_bytes();
            remote = deskport::remotePeer(bytes.data(), (int) bytes.size());
          } else if (address.is_v6()) {
            const auto bytes = address.to_v6().to_bytes();
            remote = deskport::remotePeer(bytes.data(), (int) bytes.size());
          }
          session->deskport_remote.store(remote ? 1 : 0, std::memory_order_relaxed);
          BOOST_LOG(info) << "DeskPort network path: " << (remote ? "remote" : "local");
        }
        const bool deskport_paced = session->config.monitor.deskport_smart &&
            session->deskport_remote.load(std::memory_order_relaxed) == 1;
        if (deskport_paced) {
          if (auto kbps = session->mail->event<int>("deskport_rate_kbps")->pop(0ms)) session->deskport_pace_kbps = *kbps;
          ratecontrol_packets_in_1ms = deskport::remotePacketsPerMs(
              session->deskport_pace_kbps > 0 ? session->deskport_pace_kbps : session->config.monitor.bitrate, blocksize);
        }''')
patch('src/stream.cpp', '''        send_batch_size = std::min<size_t>(64, send_batch_size);''', '''        send_batch_size = std::min<size_t>(64, send_batch_size);
        if (deskport_paced) send_batch_size = std::min(send_batch_size, ratecontrol_packets_in_1ms);''')

