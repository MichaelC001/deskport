#!/usr/bin/env python3
"""Exercise the production SDL IME adapter without a host or user configuration."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
base = Path(subprocess.check_output(['bash', str(root/'scripts/build-paths.sh'), 'checkout'], text=True).strip())/'native-ime-tests'
base.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='run-', dir=base) as temporary:
    work = Path(temporary)
    (work/'test.pro').write_text(f'''QT = core gui
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle debug_and_release
CONFIG += debug
CONFIG -= release
TARGET = local-text-input
SOURCES = "{root}/tests/local-text-input.cpp" "{root}/app/streaming/video/overlaymanager.cpp"
INCLUDEPATH += "{root}/app"
macx {{
 QMAKE_CXXFLAGS += -F"{root}/libs/mac/Frameworks"
 LIBS += -F"{root}/libs/mac/Frameworks" -framework SDL2 -framework SDL2_ttf
 INCLUDEPATH += "{root}/libs/mac/Frameworks/SDL2.framework/Headers" "{root}/libs/mac/Frameworks/SDL2_ttf.framework/Headers"
 QMAKE_RPATHDIR += "{root}/libs/mac/Frameworks"
}} else {{
 PKGCONFIG += sdl2 SDL2_ttf
}}
''')
    env = dict(os.environ)
    env.setdefault('DEVELOPER_DIR','/Applications/Xcode.app/Contents/Developer')
    if '--native' not in sys.argv:
        env.update(QT_QPA_PLATFORM='offscreen', SDL_VIDEODRIVER='dummy')
    subprocess.run([os.environ.get('DESKPORT_QMAKE','qmake'),'test.pro'], cwd=work,env=env,check=True)
    subprocess.run(['make','-j4'],cwd=work,env=env,check=True)
    subprocess.run([str(work/'local-text-input'),*sys.argv[1:]],cwd=work,env=env,check=True)
