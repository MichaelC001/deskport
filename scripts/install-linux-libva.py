#!/usr/bin/env python3
"""Install the pinned VA-API runtime before native/Flatpak/AppImage packaging."""
import argparse
import importlib.util
from pathlib import Path
import sys

sys.dont_write_bytecode = True

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('appdir', type=Path)
parser.add_argument('stage', type=Path)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('deskport_appimage_runtime',
    Path(__file__).with_name('bundle-appimage-runtime.py'))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
for prefix in (args.appdir / 'usr', args.appdir / 'usr/libexec/sunshine/usr'):
    runtime.install_libva_runtime(prefix, args.stage)
