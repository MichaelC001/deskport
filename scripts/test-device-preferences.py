#!/usr/bin/env python3
"""Test production per-device persistence without reading user settings."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-device-preferences-") as temporary:
    work = Path(temporary)
    (work / "test.pro").write_text(f'''QT += core gui qml network testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = device-preferences
SOURCES += "{root}/tests/device-preferences.cpp" "{root}/app/settings/streamingpreferences.cpp"
HEADERS += "{root}/app/settings/streamingpreferences.h"
INCLUDEPATH += "{root}/app"
''')
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=temporary)
    env.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), "test.pro"], cwd=work, env=env, check=True)
    subprocess.run(["make", "-j4"], cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(work / "device-preferences")], cwd=work, env=env, check=True)
