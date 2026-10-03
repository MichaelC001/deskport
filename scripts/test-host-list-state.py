#!/usr/bin/env python3
"""Build and run the durable list removal across isolated process restarts; no personal settings are read."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
qmake = os.environ.get("QMAKE", "qmake")
with tempfile.TemporaryDirectory(prefix="deskport-host-list-state-") as work:
    work = Path(work)
    (work / "host-list-state.pro").write_text(f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = host-list-state-test
SOURCES += "{root}/tests/host-list-state.cpp"
HEADERS += "{root}/app/backend/hostliststate.h"
INCLUDEPATH += "{root}/app"
''')
    subprocess.run([qmake, "host-list-state.pro"], cwd=work, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["make", "-s"], cwd=work, check=True)
    sys.exit(subprocess.run([str(work / "host-list-state-test")]).returncode)
