#!/usr/bin/env python3
"""Test shortcut edges and registration without binding keys or injecting input."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
if sys.platform == "darwin":
    os.environ.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")


def compile_test(work, name, sources, extra="", headers=()):
    directory = work / name
    directory.mkdir()
    project = "\n".join([
        "QT += core gui testlib",
        "CONFIG += console c++17 testcase",
        "CONFIG -= app_bundle",
        "QMAKE_CXXFLAGS += -UNDEBUG",
        f'INCLUDEPATH += "{ROOT}/app"',
        "SOURCES += " + " ".join(f'"{ROOT / source}"' for source in sources),
        "HEADERS += " + " ".join(f'"{ROOT / header}"' for header in headers),
        f"TARGET = {name}",
        "QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0" if sys.platform == "darwin" else "",
        extra,
    ])
    (directory / "test.pro").write_text(project + "\n")
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), "test.pro"],
                   cwd=directory, check=True)
    jobs = str(max(1, min(4, int(os.environ.get("JOBS", "2")))))
    subprocess.run(["make", "-j" + jobs], cwd=directory, check=True,
                   stdout=subprocess.DEVNULL)
    return directory / name


def main():
    # The caller supplies a managed run's tests directory through TMPDIR.
    with tempfile.TemporaryDirectory(prefix="deskport-shortcut-") as temporary:
        work = Path(temporary)
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", SDL_VIDEODRIVER="dummy")
        env.pop("DESKPORT_DISABLE_GLOBAL_SHORTCUT", None)
        executable = compile_test(work, "global-shortcut", [
            "tests/global-shortcut.cpp", "app/backend/globalshortcut.cpp",
        ], headers=["app/backend/globalshortcut.h"])
        subprocess.run([str(executable)], env=env, check=True, timeout=30)
        for platform in ("offscreen", "minimal"):
            subprocess.run([str(executable), "--isolated-gui"],
                           env=dict(env, QT_QPA_PLATFORM=platform), check=True, timeout=30)

        if sys.platform == "darwin":
            sdl = ROOT / "libs/mac/Frameworks"
            extra = f'''QMAKE_CXXFLAGS += -F"{sdl}"
INCLUDEPATH += "{sdl}/SDL2.framework/Versions/A/Headers"
LIBS += -F"{sdl}" -framework SDL2
QMAKE_RPATHDIR += "{sdl}"
'''
        else:
            extra = "CONFIG += link_pkgconfig\nPKGCONFIG += sdl2\n"
        executable = compile_test(work, "shortcut-state", ["tests/shortcut-state.cpp"], extra)
        subprocess.run([str(executable)], env=env, check=True, timeout=30)

        if sys.platform.startswith("linux"):
            executable = compile_test(work, "global-shortcut-portal", [
                "tests/global-shortcut-portal.cpp", "app/backend/globalshortcut_linux.cpp",
            ], "QT += dbus\n")
            # A private bus is mandatory: this fixture must never contact the
            # user's portal, request permission or bind a real shortcut.
            subprocess.run(["dbus-run-session", "--", str(executable)],
                           env=dict(env, DESKPORT_TEST_PRIVATE_BUS="1"), check=True, timeout=60)


if __name__ == "__main__":
    main()
