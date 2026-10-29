#!/usr/bin/env python3
"""Shared encoder policy and telemetry; explicit, qualified backend adapters."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
def patch(name, old, new):
    path = root / name
    source = path.read_text()
    if new in source:
        return
    if source.count(old) != 1:
        raise SystemExit(f'Encoder policy anchor mismatch: {name}: {old[:80]!r}')
    path.write_text(source.replace(old, new))

# Upgrade an incremental build tree made before driver qualification was added.
video_path = root / 'src/video.cpp'
video_source = video_path.read_text()
video_source = video_source.replace('encoder.name == "vulkan" && (video_format.name',
    'encoder.name == "vulkan" && encode_device->deskport_bounded_vbr(ctx.get()) && (video_format.name')
video_path.write_text(video_source)

for name in ('video.cpp', 'stream.cpp'):
    patch('src/' + name, '#include "deskport/common/smartstream.h"',
          '#include "deskport/common/smartstream.h"\n#include "deskport/common/encoderpolicy.h"')

patch('src/video.cpp', '''      if (auto status = avcodec_open2(ctx.get(), codec, &options)) {''', '''      // Apply after the device adapter: it may otherwise restore a minimum rate.
      // H.264/HEVC Vulkan were qualified with the bundled FFmpeg and RADV.
      // AV1 and other backends retain their existing driver-specific policy.
      deskport::RateControlDecision deskport_rate {false, false};
#if defined(__linux__) || defined(linux) || defined(__linux) || defined(__FreeBSD__)
      deskport_rate = deskport::rateControl(config.deskport_smart,
          encoder.name == "vulkan" && encode_device->deskport_bounded_vbr(ctx.get()) && (video_format.name == "h264_vulkan" || video_format.name == "hevc_vulkan"),
          config::video.vk.rc_mode, retries);
      if (deskport_rate.boundedVbr) {
        ctx->rc_min_rate = 0;
        av_dict_set_int(&options, "rc_mode", 4, 0);
      }
#endif
      if (auto status = avcodec_open2(ctx.get(), codec, &options)) {''')
patch('src/video.cpp', '''        if (!video_format.fallback_options.empty() && retries == 0) {''', '''        if ((!video_format.fallback_options.empty() || deskport_rate.retryOriginalOnFailure) && retries == 0) {''')
patch('src/video.cpp', '''      // Successfully opened the codec
      break;''', '''      if (deskport_rate.boundedVbr) {
        BOOST_LOG(info) << "DeskPort rate control: bounded VBR, codec=" << video_format.name
                        << ", target_bps=" << ctx->bit_rate << ", max_bps=" << ctx->rc_max_rate
                        << ", min_bps=" << ctx->rc_min_rate << ", vbv_bits=" << ctx->rc_buffer_size;
      } else {
        BOOST_LOG(info) << "DeskPort rate control: backend configuration, codec=" << video_format.name
                        << ", attempt=" << retries;
      }
      // Successfully opened the codec
      break;''')
patch('src/stream.cpp', '''    config_t config;''', '''    config_t config;
    deskport::EncodedWindow deskport_encoded;''')
patch('src/stream.cpp', '''      auto session = (session_t *) packet->channel_data;
      auto lowseq = session->video.lowseq;''', '''      auto session = (session_t *) packet->channel_data;
      const auto deskport_now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      auto &encoded = session->deskport_encoded;
      if (encoded.add(deskport_now_ms, packet->data_size(), packet->is_idr())) {
        BOOST_LOG(info) << "DeskPort encoded video: Mbps=" << encoded.mbps(deskport_now_ms)
                        << ", packets=" << encoded.packets << ", keyframes=" << encoded.keys
                        << ", max_packet_bytes=" << encoded.maxPacketBytes
                        << " (before transport/FEC)";
        encoded.reset(deskport_now_ms);
      }
      auto lowseq = session->video.lowseq;''')

patch('src/video.cpp', '    avcodec_encode_session_t() = default;',
      '    avcodec_encode_session_t() = default;\n    deskport::KeyframeRequests deskport_keys;')
patch('src/video.cpp', '    while (ret >= 0) {',
      '    session.deskport_keys.submitted(frame->pts, frame->flags & AV_FRAME_FLAG_KEY);\n\n    while (ret >= 0) {')
# Both pinned revisions and the previous macOS diagnostic overlay are accepted.
p = root / 'src/video.cpp'
s = p.read_text()
if 'session.deskport_keys.take(av_packet->pts)' not in s:
    import re
    pattern = r'      if \(\(frame->flags & AV_FRAME_FLAG_KEY\) && !\(av_packet->flags & AV_PKT_FLAG_KEY\)\) \{.*?\n      \}'
    replacement = '''      if (session.deskport_keys.take(av_packet->pts) && !(av_packet->flags & AV_PKT_FLAG_KEY)) {
        BOOST_LOG(error) << "Encoder did not produce IDR frame when requested! Returned PTS="sv
                         << av_packet->pts << ", encoder="sv << ctx->codec->name;
      }'''
    s, count = re.subn(pattern, replacement, s, flags=re.S)
    if count != 1: raise SystemExit('IDR diagnostic anchor mismatch')
    p.write_text(s)

patch('src/platform/common.h', '    virtual void init_codec_options(AVCodecContext *ctx, AVDictionary **options) {};',
      '    virtual void init_codec_options(AVCodecContext *ctx, AVDictionary **options) {};\n'
      '    virtual bool deskport_bounded_vbr(AVCodecContext *ctx) { return false; }')
patch('src/platform/linux/vulkan_encode.cpp', '    void init_codec_options(AVCodecContext *ctx, AVDictionary **options) override {', '''    bool deskport_bounded_vbr(AVCodecContext *ctx) override {
      if (!ctx->hw_frames_ctx) return false;
      auto *frames = (AVHWFramesContext *) ctx->hw_frames_ctx->data;
      auto *device = (AVHWDeviceContext *) frames->device_ref->data;
      auto *vk = (AVVulkanDeviceContext *) device->hwctx;
      VkPhysicalDeviceDriverProperties driver {};
      driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
      VkPhysicalDeviceProperties2 properties {};
      properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      properties.pNext = &driver;
      vkGetPhysicalDeviceProperties2(vk->phys_dev, &properties);
      // Other vendors/drivers retain their defaults until separately qualified.
      return properties.properties.vendorID == 0x1002 && driver.driverID == VK_DRIVER_ID_MESA_RADV;
    }

    void init_codec_options(AVCodecContext *ctx, AVDictionary **options) override {''')
