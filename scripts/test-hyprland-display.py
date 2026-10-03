#!/usr/bin/env python3
"""Exercise the production Hyprland adapter with an isolated fake IPC endpoint.

Run inside the locked devShell. The tests never connect to the user's compositor.
An explicit --helper may supply an already compiled deskport-display executable.
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
parser.add_argument('--build-only', metavar='OUTPUT')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]

MOCK = r'''import fcntl, json, os, re, sys, time
from pathlib import Path
p = Path(os.environ['DESKPORT_TEST_HYPR_STATE'])
with open(str(p) + '.lock', 'w') as lock:
 fcntl.flock(lock, fcntl.LOCK_EX)
 state = json.loads(p.read_text())
 args = sys.argv[1:]
 state.setdefault('commands', []).append(args)
 result = 'ok'
 if args == ['-j', 'monitors', 'all']:
  result = json.dumps(state['monitors'])
 elif args[:3] == ['output', 'create', 'headless']:
  state['monitors'].append(dict(name=args[3], width=1280, height=720, scale=1, transform=0, disabled=False, mirrorOf='none'))
 elif args[:2] == ['output', 'remove']:
  state['monitors'] = [m for m in state['monitors'] if m['name'] != args[2]]
 elif args[0] in ['eval', 'keyword']:
  if args[0] == 'eval' and os.environ.get('DESKPORT_TEST_HYPR_LEGACY'):
   result = 'eval is only supported with the lua config manager'
  else:
   if args[0] == 'eval':
    name = re.search(r'output = "([^"]+)"', args[1])[1]
    w, h = map(int, re.search(r'mode = "(\d+)x(\d+)@60"', args[1]).groups())
    scale = int(re.search(r'scale = (\d+)', args[1])[1])
   else:
    name, mode, pos, scale, *_ = args[2].split(',')
    w, h = map(int, mode.split('@')[0].split('x')); scale = int(scale)
   if not state.get('reject_resize'):
    for m in state['monitors']:
     if m['name'] == name: m.update(width=w, height=h, scale=scale)
 else: result = 'unknown request'
 p.write_text(json.dumps(state))
 if args[:3] == ['output', 'create', 'headless']:
  time.sleep(float(os.environ.get('DESKPORT_TEST_HYPR_CREATE_DELAY', '0')))
 print(result)
'''


def line(process, timeout=8):
    ready, _, _ = select.select([process.stdout], [], [], timeout)
    assert ready, 'helper response timed out'
    value = process.stdout.readline()
    assert value, f'helper exited {process.poll()}'
    return json.loads(value)


def request(process, seq, width=1280, height=720, scale=1, **extra):
    process.stdin.write(json.dumps(dict(seq=seq, width=width, height=height, scale=scale, **extra)) + '\n')
    process.stdin.flush()
    value = line(process)
    assert value.get('seq') == seq, value
    return value


def await_clean(state, baseline):
    end = time.monotonic() + 6
    while time.monotonic() < end:
        if json.loads(state.read_text())['monitors'] == baseline:
            return
        time.sleep(.05)
    raise AssertionError('owned output was not cleaned up or physical monitors changed')


with tempfile.TemporaryDirectory(prefix='deskport-hyprland-test-') as tmp:
    directory = Path(tmp)
    helper = str(Path(args.helper).resolve()) if args.helper else str(directory / 'hyprland-helper')
    if not args.helper:
        (directory / 'main.cpp').write_text('''#include <QCoreApplication>
#include "hyprland-display.h"
int main(int argc, char** argv) {
 QCoreApplication app(argc, argv); const auto args = app.arguments();
 if(args.value(1)=="--restore-hyprland") return restoreHyprlandDisplay(args.value(2));
 return runHyprlandDisplay(1280,720);
}
''')
        (directory / 'test.pro').write_text(f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = hyprland-helper
INCLUDEPATH += "{root}/host/linux"
SOURCES += "{directory}/main.cpp" "{root}/host/linux/hyprland-display.cpp"
''')
        subprocess.run(['qmake', 'test.pro'], cwd=directory, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['make', '-j2'], cwd=directory, check=True, stdout=subprocess.DEVNULL)
    if args.build_only:
        shutil.copy2(helper, args.build_only)
        raise SystemExit(0)
    binary_dir = directory / 'bin'
    binary_dir.mkdir()
    mock = binary_dir / 'hyprctl'
    mock.write_text(f'#!{sys.executable}\n' + MOCK)
    mock.chmod(0o700)
    state = directory / 'state.json'
    baseline = [dict(name='TEST-PHYSICAL', width=1920, height=1080, scale=1, transform=0, disabled=False, mirrorOf='none')]
    state.write_text(json.dumps(dict(monitors=baseline)))
    env = dict(os.environ, PATH=str(binary_dir) + os.pathsep + os.environ['PATH'],
               DESKPORT_TEST_HYPR_STATE=str(state), DESKPORT_DISPLAY_ON_DEMAND='1',
               XDG_CURRENT_DESKTOP='Hyprland', HYPRLAND_INSTANCE_SIGNATURE='isolated-fixture')
    count = 0
    for legacy in [False, True]:
        case_env = dict(env)
        if legacy: case_env['DESKPORT_TEST_HYPR_LEGACY'] = '1'
        process = subprocess.Popen([helper, '1280', '720'], env=case_env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            ready = line(process)
            assert ready['ready'] and not ready['active'] and ready['backend'] == 'hyprland'
            assert json.loads(state.read_text())['monitors'] == baseline
            response = request(process, 1)
            assert response['active'] and response['outputName'].startswith('DeskPort-'), response
            owned = response['outputName']
            response = request(process, 2, 1668, 2388, 2)
            assert response['width'] == 1668 and response['height'] == 2388 and response['scale'] == 2, response
            before = len([c for c in json.loads(state.read_text())['commands'] if c[0] in ['eval', 'keyword']])
            assert 'error' not in request(process, 3, 1668, 2388, 2)
            after = len([c for c in json.loads(state.read_text())['commands'] if c[0] in ['eval', 'keyword']])
            assert before == after, 'unchanged geometry unnecessarily changed the compositor'
            assert 'error' in request(process, 4, 641, 720)
            assert 'error' in request(process, 5, displayPolicy=1)
            assert not request(process, 6, session=False)['active']
            await_clean(state, baseline)
            assert request(process, 7)['outputName'] == owned
            process.stdin.close()
            assert process.wait(timeout=8) == 0
            await_clean(state, baseline)
            count += 8
        finally:
            if process.poll() is None: process.kill(); process.wait()
    # A helper killed without destructors still releases only its own output.
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process); request(process, 8)
    process.kill(); process.wait()
    await_clean(state, baseline)
    count += 1
    # The owner can die before the guard has sent its ready acknowledgment.
    process = subprocess.Popen([helper, '1280', '720'], env=dict(env, DESKPORT_TEST_HYPR_CREATE_DELAY='0.8'),
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process)
    process.stdin.write(json.dumps(dict(seq=80, width=1280, height=720, scale=1)) + '\n'); process.stdin.flush()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if len(json.loads(state.read_text())['monitors']) > len(baseline): break
        time.sleep(.01)
    else: raise AssertionError('delayed output creation did not start')
    process.kill(); process.wait(); process.stdout.close()
    await_clean(state, baseline)
    count += 1
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               text=True, start_new_session=True)
    line(process); request(process, 81)
    os.killpg(process.pid, signal.SIGTERM); process.wait(timeout=8)
    await_clean(state, baseline)
    count += 1
    # Compositor loss must stop capture, not silently select another monitor.
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process); request(process, 9)
    value = json.loads(state.read_text()); value['monitors'] = baseline; state.write_text(json.dumps(value))
    assert 'error' in line(process)
    assert process.wait(timeout=8) == 1
    await_clean(state, baseline)
    count += 1
    # A successful IPC submission with an unacknowledged mode is not success.
    value = json.loads(state.read_text()); value['reject_resize'] = True; state.write_text(json.dumps(value))
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process)
    assert 'error' in request(process, 10, 1920, 1080)
    assert process.wait(timeout=8) == 1
    await_clean(state, baseline)
    count += 1
    print(f'{count} Hyprland adapter lifecycle checks passed (isolated IPC fixture; no real compositor)')
