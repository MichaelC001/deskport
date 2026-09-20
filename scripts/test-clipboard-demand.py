#!/usr/bin/env python3
"""Isolated protocol/demand tests; never read the user's system clipboard."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-demand-test-") as tmp:
    work = Path(tmp)
    project = work / "demand.pro"
    sources = ["tests/clipboard-demand.cpp", "app/clipboard/agent.cpp", "app/clipboard/native.cpp"]
    extra = f'OBJECTIVE_SOURCES += "{root}/app/clipboard/macnative.mm"\nLIBS += -framework AppKit\n' if sys.platform == "darwin" else f'SOURCES += "{root}/app/clipboard/waylandnative.cpp" "{root}/app/clipboard/ext-data-control.c"\nCONFIG += link_pkgconfig\nPKGCONFIG += wayland-client\n'
    project.write_text('QT += core gui testlib\nCONFIG += console c++17 testcase\nCONFIG -= app_bundle\nTARGET = demand\n' + f'INCLUDEPATH += "{root}/app"\n' + 'SOURCES += ' + ' '.join(f'"{root / p}"' for p in sources) + '\n' + extra)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    subprocess.run(["qmake", str(project)], cwd=work, env=env, check=True)
    jobs = max(1, min(4, int(os.environ.get("JOBS", "2"))))
    subprocess.run(["make", f"-j{jobs}"], cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(work / "demand")], cwd=work, env=env, check=True, timeout=120)

    if sys.platform == "darwin":
        native_env = dict(env, QT_QPA_PLATFORM="cocoa")
        subprocess.run([str(work / "demand"), "nativeMacProviders"], cwd=work, env=native_env, check=True, timeout=30)
