#!/usr/bin/env python3
"""Build and run the device group/order rules test; no settings or hosts are touched."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
qmake = os.environ.get("QMAKE", "qmake")
with tempfile.TemporaryDirectory(prefix="deskport-host-layout-") as work:
    work = Path(work)
    (work / "host-layout.pro").write_text(f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = host-layout-test
SOURCES += "{root}/tests/host-layout.cpp" "{root}/app/gui/hostlayout.cpp"
HEADERS += "{root}/app/gui/hostlayout.h"
INCLUDEPATH += "{root}/app/gui"
''')
    subprocess.run([qmake, "host-layout.pro"], cwd=work, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["make", "-s"], cwd=work, check=True)
    sys.exit(subprocess.run([str(work / "host-layout-test")]).returncode)
