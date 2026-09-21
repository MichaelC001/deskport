#!/usr/bin/env python3
"""Validate the stable release channel without network or personal hosts."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-updates-") as temporary:
    work = Path(temporary)
    (work / "version.h").write_text('#define VERSION_STR "0.4.5"\n')
    (work / "test.pro").write_text(f'''QT += core network testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
SOURCES += "{root}/tests/update-checker.cpp" "{root}/app/backend/autoupdatechecker.cpp"
HEADERS += "{root}/app/backend/autoupdatechecker.h"
INCLUDEPATH += "{root}/app" "{work}"
TARGET = update-checker
''')
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), "test.pro"], cwd=work, check=True)
    subprocess.run(["make", "-j4"], cwd=work, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(work / "update-checker")], cwd=work, check=True, timeout=30)
