#!/usr/bin/env python3
"""Apply bounded per-session controls to pinned Sunshine sources (Mac/Linux).

The authenticated launch/resume request owns the settings; never mutate global
capture/input config. Missing fields retain the previous host behavior.
"""
import difflib
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
marker = root / 'deskport-session-settings.patch'
if '--revert' in sys.argv:
    if marker.exists():
        subprocess.run(['git', 'apply', '--reverse', '--check', str(marker.resolve())], cwd=root, check=True)
        subprocess.run(['git', 'apply', '--reverse', str(marker.resolve())], cwd=root, check=True)
        marker.unlink()
    raise SystemExit(0)
original = {}
changes = {}

def edit(name, transform):
    path = root / 'src' / name
    source = changes.get(path, path.read_text())
    original.setdefault(path, source)
    updated = transform(source)
    if updated == source:
        raise SystemExit(f'Session patch anchor missing: {name}')
    changes[path] = updated

def append_config(source, field):
    start = source.index('  struct config_t {')
    end = source.index('\n  };', start)
    return source[:end] + '\n    ' + field + source[end:]

def once(source, anchor, replacement):
    if source.count(anchor) != 1:
        raise SystemExit(f'Expected one session patch anchor: {anchor!r}')
    return source.replace(anchor, replacement, 1)

# Idempotent after a complete successful application; partial edits fail closed.
if marker.exists():
    subprocess.run(['git', 'apply', '--reverse', '--check', str(marker.resolve())], cwd=root, check=True)
    raise SystemExit(0)

edit('rtsp.h', lambda s: once(s, '    bool host_audio;', '''    bool deskport_audio = true;
    bool deskport_input = true;
    bool deskport_smart = true;
    bool host_audio;'''))
edit('nvhttp.cpp', lambda s: once(once(s,
    '    launch_session->host_audio = host_audio;', '''    launch_session->host_audio = host_audio;
    // Only paired clients reach launch/resume. Parse explicit booleans strictly.
    launch_session->deskport_audio = get_arg(args, "deskportAudio", "1") == "1";
    launch_session->deskport_input = get_arg(args, "deskportInput", "1") == "1";
    launch_session->deskport_smart = get_arg(args, "deskportSmart", "1") == "1";'''),
    '    tree.put("root.MaxLumaPixelsHEVC",', '    tree.put("root.DeskPortSessionSettings", "1");\n    tree.put("root.MaxLumaPixelsHEVC",'))
# Advertise the launcher's OS label so saved hosts gain icons without rebinding.
edit('nvhttp.cpp', lambda s: '#include <cstdlib>\n' + once(s,
    '    tree.put("root.hostname", config::nvhttp.sunshine_name);', '''    tree.put("root.hostname", config::nvhttp.sunshine_name);
    if (const char* os = std::getenv("DESKPORT_HOST_OS")) tree.put("root.DeskPortOS", os);'''))
edit('audio.h', lambda s: append_config(s, 'bool deskport_audio = true;'))
edit('audio.cpp', lambda s: once(s, 'if (!config::audio.stream)', 'if (!config::audio.stream || !config.deskport_audio)'))
edit('stream.h', lambda s: append_config(s, 'bool deskport_input = true;'))
edit('rtsp.cpp', lambda s: once(s, '    config.audio.flags[audio::config_t::HOST_AUDIO] = session.host_audio;', '''    config.audio.flags[audio::config_t::HOST_AUDIO] = session.host_audio;
    config.audio.deskport_audio = session.deskport_audio;
    config.deskport_input = session.deskport_input;'''))
def input_gate(s):
    anchor = 'input::passthrough(session->input, std::move(plaintext));'
    if s.count(anchor) != 2:
        raise SystemExit('Expected both encrypted input dispatch paths')
    return s.replace(anchor, 'if (session->config.deskport_input) ' + anchor)
edit('stream.cpp', input_gate)
# The macOS smart-stream overlay is optional. Use the session's selection when
# present; Linux's stock encoder continues to use its established pacing.
video = root / 'src/video.cpp'
if 'const bool smart = std::getenv("DESKPORT_SMART_STREAMING")' in video.read_text():
    edit('video.h', lambda s: append_config(s, 'bool deskport_smart = true;'))
    edit('video.cpp', lambda s: once(s, '''const bool smart = std::getenv("DESKPORT_SMART_STREAMING") &&
        std::string_view(std::getenv("DESKPORT_SMART_STREAMING")) == "1";''', 'const bool smart = config.deskport_smart;'))
    edit('rtsp.cpp', lambda s: once(s, '    config.deskport_input = session.deskport_input;', '    config.deskport_input = session.deskport_input;\n    config.monitor.deskport_smart = session.deskport_smart;'))
patch = ''.join(''.join(difflib.unified_diff(original[path].splitlines(True), updated.splitlines(True),
    fromfile='a/' + str(path.relative_to(root)), tofile='b/' + str(path.relative_to(root))))
    for path, updated in changes.items())
marker.write_text(patch)
subprocess.run(['git', 'apply', '--check', str(marker.resolve())], cwd=root, check=True)
subprocess.run(['git', 'apply', str(marker.resolve())], cwd=root, check=True)
