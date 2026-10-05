#!/usr/bin/env python3
"""Build an explicitly isolated HTTPS-to-Sunshine fixture with the real web client."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=Path.home() / "mygit/build/deskport/browser-client/live-gateway")
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
project = output / "fixture.pro"
project.write_text(f'''QT = core network
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += openssl
TARGET = browser-live-gateway
SOURCES += "{root}/tests/browser-live-gateway.cpp" "{root}/app/backend/browsergateway.cpp"
HEADERS += "{root}/app/backend/browsergateway.h"
INCLUDEPATH += "{root}/app/backend"
RESOURCES += "{root}/app/browser/browser.qrc"
''')
with (output / "build.log").open("w") as log:
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), str(project)], cwd=output, check=True, stdout=log, stderr=log)
    build = subprocess.run(["make", "-j2"], cwd=output, stdout=log, stderr=log)
if build.returncode:
    print((output / "build.log").read_text())
    raise SystemExit(build.returncode)
print(output / "browser-live-gateway")
