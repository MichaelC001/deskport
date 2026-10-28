#!/usr/bin/env python3
"""Exercise macOS window-drag event routing without global input or a host."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
if sys.platform != 'darwin':
    p.error('this test requires macOS')
root = Path(__file__).resolve().parents[1]
work = a.output.resolve()
work.mkdir(parents=True, exist_ok=False)
(work / 'test.pro').write_text('\n'.join([
    'QT += core gui quick testlib', 'CONFIG += console c++17 testcase',
    'CONFIG -= app_bundle', 'TARGET = macos-window-drag-test',
    'QMAKE_MACOSX_DEPLOYMENT_TARGET = 14.0',
    f'INCLUDEPATH += "{root}/app"',
    f'OBJECTIVE_SOURCES += "{root}/tests/macos-window-drag.mm" "{root}/app/backend/mactitlebar.mm"',
    f'HEADERS += "{root}/app/backend/mactitlebar.h"',
    'LIBS += -framework Cocoa',
]))
subprocess.run([os.environ.get('QMAKE', 'qmake'), 'test.pro'], cwd=work, check=True,
               stdout=subprocess.DEVNULL)
subprocess.run(['make', '-s', '-j2'], cwd=work, check=True)
subprocess.run([str(work / 'macos-window-drag-test')], check=True, timeout=30,
               env=dict(os.environ, QT_QPA_PLATFORM='cocoa', QT_QUICK_BACKEND='software'))
