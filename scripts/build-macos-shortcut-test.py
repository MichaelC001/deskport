#!/usr/bin/env python3
"""Build the native macOS fixture; execution needs an interactive test session."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
work = a.output.resolve()
work.mkdir(parents=True, exist_ok=False)
(work / 'test.pro').write_text('\n'.join([
    'QT = core gui testlib', 'CONFIG += console c++17 testcase',
    'CONFIG -= app_bundle', 'TARGET = macos-shortcut-capture-test',
    'QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0',
    f'INCLUDEPATH += "{root}/app"',
    f'SOURCES += "{root}/app/backend/shortcutrecorderinhibitor.cpp"',
    f'OBJECTIVE_SOURCES += "{root}/tests/macos-shortcut-capture.mm" "{root}/app/backend/shortcutrecorderinhibitor_mac.mm"',
    'LIBS += -framework Cocoa -framework ApplicationServices -framework Carbon',
]))
subprocess.run([os.environ.get('QMAKE', 'qmake'), 'test.pro'], cwd=work, check=True)
subprocess.run(['make', '-j2'], cwd=work, check=True)
print(work / 'macos-shortcut-capture-test')
