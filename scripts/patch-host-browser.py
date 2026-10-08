#!/usr/bin/env python3
"""Add the authenticated browser transport to either pinned desktop host.

Only the encoded packet boundary is redirected. Native capture, encoding and
input remain shared; browser media uses private queues and never steals native
packets. Run after the session, lifecycle, smart-stream and video-pause overlays.
"""
import difflib
import re
from pathlib import Path
import shutil
import subprocess
import sys

args = [value for value in sys.argv[1:] if value != '--revert']
if not args:
    raise SystemExit('usage: patch-host-browser.py SOURCE [HOST_BROWSER_DIR] [--revert]')
root = Path(args[0]).resolve()
assets = Path(args[1]).resolve() if len(args) > 1 else Path(__file__).resolve().parents[1] / 'host/browser'
marker = root / 'deskport-browser.patch'
if marker.exists():
    subprocess.run(['git', 'apply', '--reverse', '--check', str(marker)], cwd=root, check=True)
    if '--revert' in sys.argv:
        subprocess.run(['git', 'apply', '--reverse', str(marker)], cwd=root, check=True)
        marker.unlink()
    else:
        for source in assets.glob('*'):
            if source.suffix in ('.h', '.cpp'):
                shutil.copy2(source, root / 'src/deskport/browser' / source.name)
    raise SystemExit(0)
if '--revert' in sys.argv:
    raise SystemExit(0)

original, changes = {}, {}
def edit(name, old, new, count=1):
    path = root / name
    source = changes.get(path, path.read_text())
    original.setdefault(path, source)
    if source.count(old) != count:
        raise SystemExit(f'Browser overlay expected {count} anchors in {name}: {old!r}')
    changes[path] = source.replace(old, new)

for name in ['video.cpp', 'audio.cpp', 'confighttp.cpp', 'rtsp.cpp', 'main.cpp']:
    path = root / 'src' / name
    source = path.read_text()
    original[path] = source
    changes[path] = '#include "deskport/browser/browser.h"\n' + source

# WebRTC guarantees constrained-baseline decoding, while Chromium builds need
# not expose High profile. Apply the profile to the real encoder, not just SDP.
video_header = (root / 'src/video.h').read_text()
anchor = re.search(r'^    int enableIntraRefresh;[^\n]*', video_header, re.MULTILINE)
if not anchor:
    raise SystemExit('Browser overlay expected the video configuration tail')
edit('src/video.h', anchor.group(), anchor.group() + '\n    bool deskport_browser_baseline = false;')
video_source = changes[root / 'src/video.cpp']
anchor = re.search(r'^          ctx->profile = .*H264.*;', video_source, re.MULTILINE)
if not anchor:
    # Newer host factors profile selection into a shared helper.
    anchor = re.search(r'^          ctx->profile = select_h264_profile.*;', video_source, re.MULTILINE)
if not anchor:
    raise SystemExit('Browser overlay expected the H.264 AVCodec profile selection')
edit('src/video.cpp', anchor.group(), anchor.group().replace('ctx->profile = ',
    'ctx->profile = config.deskport_browser_baseline ? AV_PROFILE_H264_CONSTRAINED_BASELINE : ') +
    '\n          if (config.deskport_browser_baseline) {'
    '\n            const int macroblocks = ((config.width + 15) / 16) * ((config.height + 15) / 16);'
    '\n            ctx->level = macroblocks <= 3600 ? 31 : macroblocks <= 8192 ? 41 : 51;'
    '\n          }')
anchor = '      auto handle_option = [&options, &config](const encoder_t::option_t &option) {\n'
edit('src/video.cpp', anchor, anchor + '''        if (config.deskport_browser_baseline && config.videoFormat == 0) {
          if (option.name == "profile") { av_dict_set(&options, "profile", "baseline", 0); return; }
          if (option.name == "coder") { av_dict_set(&options, "coder", "cavlc", 0); return; }
          if (option.name == "cavlc") { av_dict_set_int(&options, "cavlc", 1, 0); return; }
        }
''')
nvenc_source = (root / 'src/nvenc/nvenc_base.cpp').read_text()
anchor = re.search(r'^\s*enc_config.profileGUID = .*NV_ENC_H264_PROFILE_HIGH_GUID;', nvenc_source, re.MULTILINE)
if not anchor:
    raise SystemExit('Browser overlay expected the native NVENC H.264 profile')
edit('src/nvenc/nvenc_base.cpp', anchor.group(), anchor.group().replace('enc_config.profileGUID = ',
    'enc_config.profileGUID = client_config.deskport_browser_baseline ? NV_ENC_H264_PROFILE_BASELINE_GUID : ') +
    '\n          if (client_config.deskport_browser_baseline) {'
    '\n            const int macroblocks = ((client_config.width + 15) / 16) * ((client_config.height + 15) / 16);'
    '\n            enc_config.encodeCodecConfig.h264Config.level = macroblocks <= 3600 ? NV_ENC_LEVEL_H264_31 :'
    '\n              macroblocks <= 8192 ? NV_ENC_LEVEL_H264_41 : NV_ENC_LEVEL_H264_51;'
    '\n          }')
edit('src/nvenc/nvenc_base.cpp', 'config.h264_cavlc || !get_encoder_cap(',
     'client_config.deskport_browser_baseline || config.h264_cavlc || !get_encoder_cap(')

# Both capture modes have a channel token; the third global queue belongs to
# encoder probing and must keep its original, isolated synchronous behavior.
path = root / 'src/video.cpp'
anchor = 'mail::man->queue<packet_t>(mail::video_packets)'
if changes[path].count(anchor) != 3:
    raise SystemExit('Browser overlay expected async, sync and probe video queues')
changes[path] = changes[path].replace(anchor, 'deskport::browser::video_queue(channel_data)', 2)
edit('src/video.cpp', 'packets->raise(std::move(packet));',
     'deskport::browser::publish_video(packets, std::move(packet));', 2)
edit('src/audio.cpp', 'mail::man->queue<packet_t>(mail::audio_packets)',
     'deskport::browser::audio_queue(channel_data)')

handler = '''  // The browser HTTP origin terminates in the DeskPort supervisor. This
  // endpoint accepts only its certificate-pinned, authenticated loopback calls.
  void deskportBrowser(const resp_https_t &response, const req_https_t &request) {
    const auto authorization = request->header.find("Authorization");
    if (!request->remote_endpoint().address().is_loopback() ||
        authorization == request->header.end() || authorization->second.rfind("Basic ", 0) != 0 ||
        request->header.find("Origin") != request->header.end() ||
        request->header.find("Referer") != request->header.end() || !authenticate(response, request)) {
      bad_request(response, request, "Authenticated local management only");
      return;
    }
    nlohmann::json output {{"version", 1}, {"status", false}, {"code", "invalid-request"}};
    try {
      const auto body = request->content.string();
      if (body.size() <= 131072) output = deskport::browser::request(nlohmann::json::parse(body));
    } catch (const std::exception&) {}
    send_response(response, output);
  }

'''
edit('src/confighttp.cpp', '  void deskportSessions(const resp_https_t &response, const req_https_t &request) {',
     handler + '  void deskportSessions(const resp_https_t &response, const req_https_t &request) {')
edit('src/confighttp.cpp', '    server.resource["^/api/deskport/sessions$"]["POST"] = deskportSessions;',
     '    server.resource["^/api/deskport/sessions$"]["POST"] = deskportSessions;\n'
     '    server.resource["^/api/deskport/browser$"]["POST"] = deskportBrowser;')
# The supervisor owns display restoration. Do not let a native takeover enter
# between media stop and display restoration, even if it explicitly asks takeover.
edit('src/confighttp.cpp', '          if (cert.empty() || lease.empty() || lease.size() > 64) {',
     '''          if (deskport::browser::reserved()) {
            output["status"] = false; output["code"] = "busy";
          } else if (cert.empty() || lease.empty() || lease.size() > 64) {''')
edit('src/rtsp.cpp', '  void terminate_sessions() {',
     '  void terminate_sessions() {\n    deskport::browser::stop_all();')
edit('src/main.cpp', '  task_pool.stop();',
     '  deskport::browser::shutdown();\n  task_pool.stop();')

definitions = []
if 'alloc(safe::mail_t mail, std::string session_id)' in (root / 'src/input.h').read_text():
    definitions.append('DESKPORT_BROWSER_INPUT_IDENTITY')
if 'int framerateX100;' in (root / 'src/video.h').read_text():
    definitions.append('DESKPORT_BROWSER_FRAMERATE_X100')
if 'bool deskport_smart' in (root / 'src/video.h').read_text():
    definitions.append('DESKPORT_BROWSER_SMART')
path = root / 'cmake/targets/common.cmake'
original[path] = path.read_text()
changes[path] = original[path] + '''
# DeskPort's encoded-frame WebRTC transport. Required in browser-enabled builds.
find_package(LibDataChannel 0.24 CONFIG REQUIRED)
target_sources(sunshine PRIVATE
    "${CMAKE_SOURCE_DIR}/src/deskport/browser/browser.cpp"
    "${CMAKE_SOURCE_DIR}/src/deskport/browser/rtc-session.cpp")
target_link_libraries(sunshine LibDataChannel::LibDataChannel)
''' + ('target_compile_definitions(sunshine PRIVATE ' + ' '.join(definitions) + ')\n' if definitions else '')

patch = ''.join(''.join(difflib.unified_diff(original[path].splitlines(True), updated.splitlines(True),
    fromfile='a/' + str(path.relative_to(root)), tofile='b/' + str(path.relative_to(root))))
    for path, updated in changes.items())
marker.write_text(patch)
subprocess.run(['git', 'apply', '--check', str(marker)], cwd=root, check=True)
subprocess.run(['git', 'apply', str(marker)], cwd=root, check=True)
destination = root / 'src/deskport/browser'
destination.mkdir(parents=True, exist_ok=True)
for source in assets.glob('*'):
    if source.suffix in ('.h', '.cpp'):
        shutil.copy2(source, destination / source.name)
print('Applied direct encoded-frame DeskPort browser transport')
