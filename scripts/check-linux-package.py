#!/usr/bin/env python3
"""Exercise a packaged viewer and host without a real desktop or personal state."""
import os
import pathlib
import signal
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
version = sys.argv[2]
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
print('PASS: isolated version/help, packaged QML GUI startup, host startup and state isolation')
