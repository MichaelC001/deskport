#!/usr/bin/env python3
"""Exercise a packaged viewer and host without a real desktop or personal state."""
import base64
import json
import os
import ssl
import time
import urllib.error
import urllib.request
import pathlib
import signal
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
version = sys.argv[2]
if str(root) == '/opt/deskport':
    for name, executable in [('display', 'deskport-display'),
                             ('host', 'sunshine/usr/bin/sunshine')]:
        entry = pathlib.Path(f'/usr/share/applications/io.github.keithxc.DeskPort.{name}.desktop')
        metadata = entry.read_text()
        assert f'Exec={root}/usr/libexec/{executable}\n' in metadata, metadata
        assert 'X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1\n' in metadata, metadata
        assert (root / 'usr/libexec' / executable).is_file(), executable
with tempfile.TemporaryDirectory(prefix='deskport-package-smoke-') as temporary:
    state = pathlib.Path(temporary)
    env = {'PATH': '/usr/bin:/bin', 'HOME': temporary, 'QT_QPA_PLATFORM': 'offscreen',
           'QT_QUICK_BACKEND': 'software', 'SDL_VIDEODRIVER': 'dummy'}
    for key in ['XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME', 'XDG_RUNTIME_DIR']:
        path = state / key.lower()
        path.mkdir(mode=0o700)
        env[key] = str(path)
    for flag in ['--version', '--help']:
        result = subprocess.run([str(root / 'AppRun'), flag], env=env, cwd=state,
                                capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        assert (version if flag == '--version' else 'Usage:') in output, output
    host = root / 'usr/libexec/deskport-host'
    if host.exists():
        result = subprocess.run([str(host), '--version'], env=env, cwd=state,
                                capture_output=True, text=True, timeout=30, check=True)
        assert 'Sunshine version:' in result.stdout + result.stderr
        assert not (state / '.config/sunshine').exists(), 'Touched standalone Sunshine state'
        assert not (pathlib.Path(env['XDG_CONFIG_HOME']) / 'sunshine').exists(), 'Touched shared Sunshine state'
    if host.exists():
        # A version-only smoke check also accepts an unpatched upstream host.
        # Exercise the management contract required by every new client session.
        config = state / 'host.conf'
        config.write_text(
            'port = 52989\nbind_address = 127.0.0.1\naddress_family = ipv4\n'
            'upnp = disabled\nsystem_tray = disabled\norigin_web_ui_allowed = pc\n'
            'keyboard = disabled\nmouse = disabled\ncontroller = disabled\n'
            'stream_audio = disabled\nencoder = software\n'
            + ''.join(f'{key} = {state / name}\n' for key, name in [
                ('file_apps', 'apps.json'), ('file_state', 'host-state.json'),
                ('credentials_file', 'control.json'), ('pkey', 'key.pem'),
                ('cert', 'cert.pem'), ('log_path', 'host.log')]))
        (state / 'apps.json').write_text('{"apps": []}')
        subprocess.run([str(host), str(config), '--creds', 'package-check', 'isolated-test'],
                       env=env, cwd=state, capture_output=True, timeout=30, check=True)
        context = ssl._create_unverified_context()  # Isolated self-signed loopback server.
        authorization = 'Basic ' + base64.b64encode(b'package-check:isolated-test').decode()
        def request_api(payload=None, extra_headers=None):
            headers = {'Authorization': authorization, 'Content-Type': 'application/json'}
            headers.update(extra_headers or {})
            request = urllib.request.Request('https://127.0.0.1:52990/api/deskport/sessions',
                data=None if payload is None else json.dumps(payload).encode(), headers=headers)
            with urllib.request.urlopen(request, context=context, timeout=2) as response:
                return json.load(response)
        with (state / 'host-process.log').open('w+') as host_log:
            server = subprocess.Popen([str(host), str(config)], env=env, cwd=state,
                stdout=host_log, stderr=host_log, start_new_session=True)
            try:
                deadline = time.monotonic() + 45
                while True:
                    try:
                        snapshot = request_api()
                        break
                    except urllib.error.HTTPError as error:
                        raise AssertionError(f'Packaged session API returned HTTP {error.code}') from error
                    except (urllib.error.URLError, TimeoutError):
                        if server.poll() is not None or time.monotonic() >= deadline:
                            host_log.seek(0)
                            raise AssertionError('Packaged session API unavailable: ' + host_log.read())
                        time.sleep(0.25)
                assert snapshot['status'] is True and snapshot['version'] == 1, snapshot
                assert snapshot['sessions'] == 0 and snapshot['reserved'] is False, snapshot
                assert isinstance(snapshot['snapshot'], str), snapshot
                # Relative asset paths must work inside the relocated host tree.
                page = urllib.request.Request('https://127.0.0.1:52990/',
                                              headers={'Authorization': authorization})
                with urllib.request.urlopen(page, context=context, timeout=2) as response:
                    assert b'<html' in response.read().lower(), 'Host web assets unavailable'
                rejected = request_api({'action': 'acquire', 'uuid': 'unpaired',
                    'lease': 'package-check', 'snapshot': snapshot['snapshot']})
                assert rejected['status'] is False and rejected['code'] == 'unauthorized', rejected
                released = request_api({'action': 'release', 'lease': 'package-check'})
                assert released['status'] is True and released['sessions'] == 0, released
                for headers in [{'Authorization': ''}, {'Origin': 'https://example.invalid'}]:
                    try:
                        request_api(extra_headers=headers)
                    except urllib.error.HTTPError as error:
                        assert error.code in (400, 401, 403), error
                    else:
                        raise AssertionError('Session API accepted an unauthenticated/browser request')
            finally:
                if server.poll() is None:
                    os.killpg(server.pid, signal.SIGTERM)
                    try:
                        server.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        os.killpg(server.pid, signal.SIGKILL)
                        server.wait()
    with (state / 'gui.log').open('w+') as log:
        child = subprocess.Popen([str(root / 'AppRun'), '--no-host-autostart'], env=env,
                                 cwd=state, stdout=log, stderr=log, start_new_session=True)
        try:
            try:
                code = child.wait(timeout=6)
                log.seek(0)
                raise AssertionError(f'GUI exited before validation ({code}): {log.read()}')
            except subprocess.TimeoutExpired:
                pass
        finally:
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
        log.seek(0)
        output = log.read()
        for failure in ['QQmlApplicationEngine failed', 'is not installed', 'Cannot load library']:
            assert failure not in output, output
    assert not (state / '.config/sunshine').exists()
print('PASS: isolated version/help, packaged QML GUI startup, authenticated host session API and state isolation')
