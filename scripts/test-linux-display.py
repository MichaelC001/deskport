#!/usr/bin/env python3
"""Real KWin/Mutter output and capture tests in isolated desktop sessions."""
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time

helper = str(Path(sys.argv[1]).resolve())
gnome = '--gnome' in sys.argv
if '--inside' not in sys.argv:
    with tempfile.TemporaryDirectory(prefix='deskport-display-test-') as tmp:
        project = Path(tmp) / 'probe.pro'
        source = Path(__file__).resolve().parents[1] / 'tests/linux-output-probe.cpp'
        project.write_text(f'QT = core\nCONFIG += console c++17 link_pkgconfig\nPKGCONFIG += wayland-client\nTARGET = output-probe\nSOURCES += "{source}" "{source.parent.parent}/host/linux/kde-output-device-v2.c" "{source.parent.parent}/host/linux/kde-output-management-v2.c"\n')
        subprocess.run(['qmake', str(project)], cwd=tmp, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['make', '-j2'], cwd=tmp, check=True, stdout=subprocess.DEVNULL)
        if '--initial-mode-mismatch' in sys.argv:
            # Fault injection: emulate a compositor choosing a different initial
            # size/scale, while keeping the helper's requested target unchanged.
            fixture = Path(tmp) / 'initial-mode-fixture'
            shutil.copytree(source.parent.parent / 'host/linux', fixture)
            cpp = fixture / 'display-helper.cpp'
            text = cpp.read_text()
            anchor = 'name.toUtf8().constData(), width, height, wl_fixed_from_int(1),'
            assert text.count(anchor) == 1
            cpp.write_text(text.replace(anchor, 'name.toUtf8().constData(), 1024, 768, wl_fixed_from_int(2),'))
            subprocess.run(['qmake', str(fixture / 'linux.pro')], cwd=fixture, check=True, stdout=subprocess.DEVNULL)
            subprocess.run(['make', '-j4'], cwd=fixture, check=True, stdout=subprocess.DEVNULL)
            helper = str(fixture / 'deskport-display')
        env = dict(os.environ, XDG_RUNTIME_DIR=tmp, XDG_CONFIG_HOME=tmp + '/config',
                   XDG_DATA_HOME=tmp + '/data', XDG_CACHE_HOME=tmp + '/cache', XDG_DATA_DIRS=tmp + '/data:' + os.environ.get('XDG_DATA_DIRS', '/usr/share'),
                   WAYLAND_DISPLAY='deskport-test', QT_QPA_PLATFORM='offscreen',
                   KWIN_COMPOSE='O2', QT_FORCE_STDERR_LOGGING='1', QT_LOGGING_TO_CONSOLE='1',
                   LIBGL_ALWAYS_SOFTWARE='1')
        env.pop('KWIN_WAYLAND_NO_PERMISSION_CHECKS', None)
        applications = Path(tmp) / 'data/applications'
        applications.mkdir(parents=True)
        menus = Path(tmp) / 'config/menus'
        menus.mkdir(parents=True)
        menu = '<!DOCTYPE Menu PUBLIC "-//freedesktop//DTD Menu 1.0//EN" "http://www.freedesktop.org/standards/menu-spec/1.0/menu.dtd"><Menu><Name>Applications</Name><DefaultAppDirs/><Include><All/></Include></Menu>'
        (menus / 'applications.menu').write_text(menu)
        env.pop('XDG_MENU_PREFIX', None)
        real_helper = Path(helper).parent / '.deskport-display-wrapped'
        if not real_helper.exists(): real_helper = Path(helper)
        (applications / 'io.github.keithxc.DeskPort.display.desktop').write_text(
            f'[Desktop Entry]\nType=Application\nName=DeskPort virtual display test\nExec={helper}\nNoDisplay=true\nCategories=Network;RemoteAccess;\nX-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1\n')
        if str(real_helper) != helper:
            entry = applications / 'io.github.keithxc.DeskPort.display.desktop'
            (applications / 'io.github.keithxc.DeskPort.display-native.desktop').write_text(entry.read_text().replace(f'Exec={helper}', f'Exec={real_helper}'))
        env['DESKPORT_OUTPUT_PROBE'] = str(Path(tmp) / 'output-probe')
        env['XDG_CURRENT_DESKTOP'] = 'GNOME' if gnome else 'KDE'
        pw_config = Path(shutil.which('pipewire')).resolve().parent.parent / 'share/pipewire'
        env['PIPEWIRE_CONFIG_DIR'] = str(pw_config)
        wp_config = Path(tmp) / 'wireplumber'
        wp_config.mkdir()
        wp_defaults = Path(shutil.which('wireplumber')).resolve().parent.parent / 'share/wireplumber'
        shutil.copy(wp_defaults / 'wireplumber.conf', wp_config / 'wireplumber.conf')
        (wp_config / 'wireplumber.conf.d').mkdir()
        (wp_config / 'wireplumber.conf.d/90-isolated.conf').write_text('wireplumber.profiles = { main = { monitor.alsa = disabled monitor.bluez = disabled monitor.bluez-midi = disabled monitor.v4l2 = disabled monitor.libcamera = disabled } }')
        env['WIREPLUMBER_CONFIG_DIR'] = str(wp_config)
        env.pop('DISPLAY', None)
        env.pop('PIPEWIRE_REMOTE', None)
        # Do not autoactivate desktop services that can outlive the test and
        # race removal of its isolated configuration/data directories.
        bus_config = Path(tmp) / 'session-bus.conf'
        bus_config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
        subprocess.run(['dbus-run-session', '--config-file=' + str(bus_config), '--', sys.executable, __file__, helper, '--inside'] + (['--gnome'] if gnome else []) + (['--disabled-output'] if '--disabled-output' in sys.argv else []),
                       env=env, check=True, timeout=180)
    raise SystemExit(0)

children = []
logs = []
try:
    if not gnome: subprocess.run(['kbuildsycoca6', '--noincremental'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    compositor = [os.environ.get('DESKPORT_GNOME_SHELL', 'gnome-shell'), '--headless', '--wayland', '--no-x11', '--virtual-monitor', '1280x720', '--wayland-display', 'deskport-test'] if gnome else [shutil.which('kwin_wayland', path=os.pathsep.join(p for p in os.environ['PATH'].split(os.pathsep) if '/wrappers/' not in p)), '--virtual', '--output-count', '3', '--width', '1280', '--height', '720',
                                    '--socket', 'deskport-test', '--no-lockscreen', '--no-global-shortcuts', '--no-kactivities']
    for command in [['pipewire'], ['wireplumber'], compositor]:
        log = tempfile.TemporaryFile(mode='w+')
        logs.append(log)
        children.append(subprocess.Popen(command, stdout=log, stderr=log))
        if command[0] == 'pipewire':
            deadline = time.monotonic() + 10
            while not (Path(os.environ['XDG_RUNTIME_DIR']) / 'pipewire-0').exists() and time.monotonic() < deadline: time.sleep(.05)
    socket = Path(os.environ['XDG_RUNTIME_DIR']) / 'deskport-test'
    deadline = time.monotonic() + 12
    while not socket.exists() and time.monotonic() < deadline:
        time.sleep(.1)
    assert socket.exists(), 'Isolated KWin did not start'
    if gnome:
        for _ in range(100):
            ready = subprocess.run(['dbus-send', '--session', '--print-reply', '--dest=org.gnome.Mutter.DisplayConfig', '/org/gnome/Mutter/DisplayConfig', 'org.gnome.Mutter.DisplayConfig.GetCurrentState'], capture_output=True)
            if ready.returncode == 0: break
            time.sleep(.1)
        assert ready.returncode == 0, ready.stderr
    def outputs():
        return {o['name']: {k:v for k,v in o.items() if k != 'id'} for o in json.loads(subprocess.check_output([os.environ['DESKPORT_OUTPUT_PROBE']], timeout=5))}
    def devices():
        return {o['name']: o for o in json.loads(subprocess.check_output([os.environ['DESKPORT_OUTPUT_PROBE'], '--devices'], timeout=5))}
    if '--disabled-output' in sys.argv:
        assert not gnome
        before = devices()
        disabled = sorted(before)[-1]
        subprocess.run([os.environ['DESKPORT_OUTPUT_PROBE'], '--disable-last'], check=True, timeout=5)
        assert not devices()[disabled]['enabled'], 'Fixture did not disable the selected output'
    baseline = outputs()
    original_devices = devices() if not gnome else {}
    def restored():
        return outputs() == baseline and (gnome or devices() == original_devices)
    def verify_mirror(owned):
        current = devices()
        virtual = current.pop(owned)
        assert virtual['priority'] == 1 and not virtual['replication_source'], virtual
        for name, expected in original_devices.items():
            actual = current[name]
            if expected['enabled']:
                assert actual['replication_source'] == virtual['uuid'], actual
                assert actual['priority'] > 1, actual
            for key in ['width', 'height', 'refresh', 'scale', 'transform', 'enabled', 'x', 'y']:
                assert actual[key] == expected[key], (name, key, expected, actual)
        assert set(outputs()) == {owned}, 'Physical outputs still expose an extended workspace'

    p = subprocess.Popen([helper, '1280', '720'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(p)
    def receive():
        assert select.select([p.stdout], [], [], 10)[0], 'helper acknowledgment timed out'
        line = p.stdout.readline()
        assert line, f'helper exited: {p.poll()}'
        result = json.loads(line)
        assert 'error' not in result, result
        return result
    initial = receive()
    assert initial['width'] == 1280 and initial['height'] == 720, initial
    def capture(width, height, scale, missing=False):
        host = os.environ.get('DESKPORT_TEST_HOST')
        if not host: return
        work = Path(os.environ['XDG_RUNTIME_DIR']) / 'capture'
        work.mkdir(exist_ok=True)
        descriptor = work / 'virtual-display.json'
        descriptor.write_text(json.dumps(dict(node=initial.get('pipewireNode'), serial=initial.get('pipewireSerial'), output=initial['outputName'], width=width, height=height, scale=scale)))
        config = work / 'sunshine.conf'
        config.write_text(f'capture = {"portal" if gnome else "kwin"}\noutput_name = {initial["outputName"]}\nencoder = software\nkeyboard = disabled\nmouse = disabled\ncontroller = disabled\nstream_audio = disabled\nupnp = disabled\nsystem_tray = disabled\nbind_address = 127.0.0.1\nport = {57989 if gnome else 56989}\nmin_log_level = 1\nfile_state = {work}/state.json\ncredentials_file = {work}/control.json\npkey = {work}/key.pem\ncert = {work}/cert.pem\nfile_apps = {work}/apps.json\nlog_path = {work}/sunshine.log\n')
        (work / 'apps.json').write_text('{"apps": []}')
        hostenv = dict(os.environ, DBUS_SYSTEM_BUS_ADDRESS='unix:path=' + str(work / 'no-system-bus'))
        if gnome: hostenv['DESKPORT_VIRTUAL_DISPLAY'] = str(descriptor)
        log = tempfile.TemporaryFile(mode='w+')
        process = subprocess.Popen([host, str(config)], cwd=work, env=hostenv, stdout=log, stderr=log)
        children.append(process)
        deadline = time.monotonic() + 20
        text = ''
        while time.monotonic() < deadline:
            log.seek(0); text = log.read()
            if 'Found H.264 encoder:' in text: break
            if missing and ('refusing physical display fallback' in text or 'Could not find display with name' in text): break
            if process.poll() is not None: break
            time.sleep(.1)
        process.terminate()
        try: process.wait(timeout=5)
        except subprocess.TimeoutExpired: process.kill(); process.wait()
        if missing:
            assert 'Found H.264 encoder:' not in text, text[-9000:]
            assert 'refusing physical display fallback' in text or 'Could not find display with name' in text, text[-9000:]
            print('PASS: removed virtual output refuses physical capture fallback')
            return
        assert 'Found H.264 encoder:' in text, text[-9000:]
        assert (f'DeskPort owned virtual capture: {initial["outputName"]} {width}x{height}' if gnome else f'name {initial["outputName"]}') in text, text[-9000:]
        print(f'PASS: {"GNOME" if gnome else "KWin"} real Sunshine capture/encoder probe at {width}x{height}@{scale}')
    capture(1280, 720, 1)
    owned = initial['outputName']
    if not gnome:
        initial_devices = devices(); initial_devices.pop(owned)
        assert initial_devices == original_devices, 'Sharing startup changed physical policy before a client connected'
    for seq, (width, height, scale) in enumerate([(1920,1080,1), (2560,1600,2), (1668,2388,2), (1280,720,1)] * 3, 1):
        p.stdin.write(json.dumps(dict(seq=seq, width=width, height=height, scale=scale)) + '\n'); p.stdin.flush()
        result = receive()
        assert {k:result[k] for k in ('seq','width','height','scale')} == dict(seq=seq, width=width, height=height, scale=scale), result
        observed = outputs()
        actual = observed.pop(owned)
        assert (actual['width'], actual['height'], actual['scale']) == (width, height, scale), actual
        if gnome: assert observed == baseline, 'Other output modes, positions or scales changed'
        else: verify_mirror(owned)
        if seq == 3: capture(width, height, scale)
    # Session ends without stopping sharing: the output itself must disappear.
    p.stdin.write(json.dumps(dict(seq=100, width=1280, height=720, scale=1, session=False)) + '\n'); p.stdin.flush(); receive()
    for _ in range(50):
        if restored(): break
        time.sleep(.05)
    assert restored(), ('Disconnect did not remove virtual output and restore physical policy', outputs(), baseline)
    # A later client recreates the workspace and can capture it again.
    p.stdin.write(json.dumps(dict(seq=101, width=1920, height=1080, scale=1, session=True)) + '\n'); p.stdin.flush(); resumed = receive()
    if gnome:
        initial = resumed; owned = resumed['outputName']
    else: verify_mirror(owned)
    capture(1920, 1080, 1)
    p.stdin.close()
    assert p.wait(timeout=5) == 0
    for _ in range(50):
        if restored(): break
        time.sleep(.05)
    assert restored(), 'Layout was not restored after helper EOF'
    capture(1280, 720, 1, missing=True)
    p = subprocess.Popen([helper, '1280', '720'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(p)
    initial = receive()
    if not gnome:
        owned = initial['outputName']
        p.stdin.write(json.dumps(dict(seq=1, width=1920, height=1080, scale=1, session=True)) + '\n'); p.stdin.flush(); receive()
        verify_mirror(owned)
    p.kill(); p.wait(timeout=5)
    for _ in range(50):
        if restored(): break
        time.sleep(.05)
    assert restored(), 'Layout was not restored after helper crash'
    if not gnome and '--disabled-output' in sys.argv:
        p = subprocess.Popen([helper, '1280', '720'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        children.append(p); receive()
        p.stdin.write(json.dumps(dict(seq=1, width=1280, height=720, scale=1, session=False)) + '\n'); p.stdin.flush(); receive()
        assert restored()
        subprocess.run([os.environ['DESKPORT_OUTPUT_PROBE'], '--enable-last'], check=True, timeout=5)
        local_policy = devices()
        assert local_policy != original_devices
        p.stdin.write(json.dumps(dict(seq=2, width=1280, height=720, scale=1, session=False)) + '\n'); p.stdin.flush(); receive()
        assert devices() == local_policy, 'Repeated disconnect overwrote local layout edits'
        p.kill(); p.wait(timeout=5)
        time.sleep(.5)
        assert devices() == local_policy, 'Idle recovery overwrote subsequent local layout edits'
        print('PASS: idle helper death preserves subsequent local layout edits')
    print(f'PASS: isolated {"GNOME" if gnome else "KWin"} creation, 12 observed resizes, mode/scale verification, mirror/primary checks on KWin, EOF/crash layout restoration')
except Exception:
    for log in logs:
        log.seek(0); print(log.read()[-10000:], file=sys.stderr)
    raise
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.kill(); child.wait()
