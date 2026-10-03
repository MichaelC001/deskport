#!/usr/bin/env python3
"""Real Hyprland lifecycle inside virtual KWin; no personal desktop access.

Needs Hyprland, hyprctl, kwin_wayland, dbus-run-session and the locked Qt devShell.
Uses a private D-Bus bus and XDG directories. DRM seat access is disabled for
this nested test. Set DESKPORT_TEST_HOST to include actual WLR/software encoder
probing and refusal to capture a lost output. It never injects live input.
"""
import argparse
import json
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--helper')
parser.add_argument('--lua', action='store_true', help='Use the current Lua compositor configuration')
parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]


def until(check, message, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = check()
        if value: return value
        time.sleep(.05)
    raise AssertionError(message)


def receive(process):
    assert select.select([process.stdout], [], [], 8)[0], 'helper response timeout'
    value = process.stdout.readline()
    assert value, f'helper exited {process.poll()}'
    return json.loads(value)


def request(process, seq, width=1280, height=720, scale=1, **extra):
    process.stdin.write(json.dumps(dict(seq=seq, width=width, height=height, scale=scale, **extra)) + '\n')
    process.stdin.flush()
    response = receive(process)
    assert response.get('seq') == seq and 'error' not in response, response
    return response


if not args.inside:
    # Hyprland's instance signatures are long; UNIX sockets need a short root.
    with tempfile.TemporaryDirectory(prefix='dphy-', dir='/tmp') as tmp:
        work = Path(tmp)
        helper = str(Path(args.helper).resolve()) if args.helper else str(work / 'helper')
        if not args.helper:
            subprocess.run([sys.executable, str(root / 'scripts/test-hyprland-display.py'), '--build-only', helper], check=True)
        for name in ['runtime', 'config', 'data', 'cache']:
            (work / name).mkdir(mode=0o700)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(work / 'runtime'), XDG_CONFIG_HOME=str(work / 'config'),
                   XDG_DATA_HOME=str(work / 'data'), XDG_CACHE_HOME=str(work / 'cache'),
                   DESKPORT_TEST_WORK=str(work), XDG_CURRENT_DESKTOP='Hyprland',
                   XDG_SESSION_TYPE='wayland', XDG_SESSION_DESKTOP='Hyprland',
                   AQ_DRM_DEVICES='/dev/null', LIBSEAT_BACKEND='seatd', SEATD_SOCK=str(work / 'no-seat'),
                   HYPRLAND_NO_RT='1', HYPRLAND_NO_SD_NOTIFY='1', HYPRLAND_NO_CRASHREPORT='1',
                   QT_QPA_PLATFORM='offscreen', DESKPORT_DISPLAY_ON_DEMAND='1')
        for key in ['DISPLAY', 'WAYLAND_DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE', 'DBUS_SESSION_BUS_ADDRESS']:
            env.pop(key, None)
        bus_config = work / 'session-bus.conf'
        bus_config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
        result = subprocess.run(['dbus-run-session', '--config-file=' + str(bus_config), '--', sys.executable,
                                 str(Path(__file__).resolve()), '--inside', '--helper', helper] + (['--lua'] if args.lua else []), env=env)
        if result.returncode:
            for name in ['kwin.log', 'hyprland.log', 'helper.log']:
                file = work / name
                if file.exists(): print(f'{name}:\n' + file.read_text(errors='replace')[-12000:], file=sys.stderr)
        raise SystemExit(result.returncode)

work = Path(os.environ['DESKPORT_TEST_WORK'])
assert str(work / 'runtime') == os.environ['XDG_RUNTIME_DIR']
env = dict(os.environ)
children = []
try:
    kwin_log = open(work / 'kwin.log', 'w')
    kwin_binary = shutil.which('kwin_wayland', path=os.pathsep.join(p for p in env['PATH'].split(os.pathsep) if '/wrappers/' not in p))
    assert kwin_binary, 'kwin_wayland is required as an isolated parent compositor'
    kwin = subprocess.Popen([kwin_binary, '--virtual', '--output-count', '1', '--socket', 'deskport-test-parent',
                             '--width', '1280', '--height', '720', '--no-lockscreen', '--no-global-shortcuts', '--no-kactivities'],
                            env=env, stdout=kwin_log, stderr=subprocess.STDOUT)
    children.append(kwin)
    until(lambda: (work / 'runtime/deskport-test-parent').exists(), 'isolated KWin did not start')
    env['WAYLAND_DISPLAY'] = 'deskport-test-parent'
    config = work / ('config/hyprland.lua' if args.lua else 'config/hyprland.conf')
    config.write_text('hl.monitor({ output = "", mode = "1280x720@60", position = "auto", scale = 1 })\n' if args.lua else
                      'monitor = ,1280x720@60,auto,1\nmisc {\n disable_hyprland_logo = true\n disable_splash_rendering = true\n}\n')
    hypr_log = open(work / 'hyprland.log', 'w')
    hyprland = subprocess.Popen(['Hyprland', '--config', str(config)], env=env, stdout=hypr_log, stderr=subprocess.STDOUT)
    children.append(hyprland)
    socket = until(lambda: next((work / 'runtime/hypr').glob('*/.socket.sock'), None), 'isolated Hyprland IPC did not start')
    env['HYPRLAND_INSTANCE_SIGNATURE'] = socket.parent.name
    wayland = until(lambda: next((p for p in (work / 'runtime').glob('wayland-*') if not p.name.endswith('.lock')), None),
                    'isolated Hyprland Wayland socket did not start')
    env['WAYLAND_DISPLAY'] = wayland.name

    def monitors():
        return json.loads(subprocess.check_output(['hyprctl', '-j', 'monitors', 'all'], env=env, text=True))

    baseline = until(lambda: monitors(), 'isolated Hyprland has no initial output')
    original = {m['name']: (m['width'], m['height'], m['scale'], m['x'], m['y'], m['transform']) for m in baseline}

    def check_original():
        current = {m['name']: (m['width'], m['height'], m['scale'], m['x'], m['y'], m['transform'])
                   for m in monitors() if m['name'] in original}
        assert current == original, (current, original)

    def clean():
        return not any(m['name'].startswith('DeskPort-') for m in monitors())

    helper_log = open(work / 'helper.log', 'w')
    capture_count = 0
    host = os.environ.get('DESKPORT_TEST_HOST')
    host_namespace = []
    if host and shutil.which('unshare'):
        check = subprocess.run(['unshare', '--user', '--map-root-user', '--net', 'true'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if check.returncode == 0: host_namespace = ['unshare', '--user', '--map-root-user', '--net']

    def capture(identity, missing=False):
        global capture_count
        if not host: return
        case = work / f'capture-{capture_count}'
        case.mkdir()
        capture_count += 1
        descriptor = case / 'virtual-display.json'
        descriptor.write_text(json.dumps(dict(output=identity['outputName'], width=identity['width'],
                                              height=identity['height'], scale=identity['scale'])))
        config = case / 'sunshine.conf'
        config.write_text(f'''capture = wlr
output_name = {identity['outputName']}
encoder = software
keyboard = disabled
mouse = disabled
controller = disabled
stream_audio = disabled
upnp = disabled
system_tray = disabled
bind_address = 127.0.0.1
port = {58989 + capture_count * 100}
min_log_level = 1
file_state = {case}/state.json
credentials_file = {case}/control.json
pkey = {case}/key.pem
cert = {case}/cert.pem
file_apps = {case}/apps.json
log_path = {case}/sunshine.log
''')
        (case / 'apps.json').write_text('{"apps": []}')
        host_env = dict(env, DBUS_SYSTEM_BUS_ADDRESS='unix:path=' + str(case / 'no-system-bus'),
                        DESKPORT_VIRTUAL_DISPLAY=str(descriptor))
        host_env.pop('DESKPORT_ON_DEMAND_DISPLAY', None)
        with open(case / 'probe.log', 'w+') as log:
            process = subprocess.Popen(host_namespace + [host, str(config)], env=host_env, cwd=case,
                                       stdout=log, stderr=subprocess.STDOUT)
            children.append(process)
            deadline = time.monotonic() + 25
            text = ''
            while time.monotonic() < deadline:
                log.seek(0); text = log.read()
                if 'Found H.264 encoder:' in text or 'refusing physical display fallback' in text or process.poll() is not None: break
                time.sleep(.1)
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
        if missing:
            assert 'Found H.264 encoder:' not in text and 'refusing physical display fallback' in text, text[-12000:]
            print('PASS: removed Hyprland output refuses physical capture fallback', flush=True)
        else:
            assert 'Found H.264 encoder:' in text, text[-12000:]
            assert identity['outputName'] in text and f"Resolution: {identity['width']}x{identity['height']}" in text, text[-12000:]
            print(f"PASS: real WLR/software encoder probe {identity['width']}x{identity['height']}@{identity['scale']}", flush=True)

    def start():
        process = subprocess.Popen([args.helper, '1280', '720'], env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=helper_log, text=True, start_new_session=True)
        children.append(process)
        assert receive(process)['ready']
        assert clean()
        return process

    helper = start()
    for seq, (width, height, scale) in enumerate([(1280, 720, 1), (1668, 2388, 2), (1920, 1080, 1)], 1):
        response = request(helper, seq, width, height, scale)
        output = next(m for m in monitors() if m['name'] == response['outputName'])
        assert (output['width'], output['height'], output['scale']) == (width, height, scale), output
        check_original()
        capture(response)
    assert not request(helper, 4, session=False)['active']
    until(clean, 'disconnect did not remove the owned output')
    capture(response, missing=True)
    request(helper, 5)
    helper.stdin.close()
    assert helper.wait(timeout=8) == 0
    until(clean, 'EOF did not remove the owned output')
    helper = start(); request(helper, 6)
    helper.kill(); helper.wait()
    until(clean, 'SIGKILL did not remove the owned output')
    helper = start(); request(helper, 7)
    os.killpg(helper.pid, signal.SIGTERM); helper.wait(timeout=8)
    until(clean, 'process-group SIGTERM did not remove the owned output')
    check_original()
    print('Real isolated Hyprland passed: 3 pixel/scale modes, original layout, release/recreate, EOF, SIGKILL and group SIGTERM cleanup')
    if host: print('Host probes used a private network namespace' if host_namespace else 'Host probes bound loopback only; user network namespaces were unavailable')
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.kill(); child.wait()
