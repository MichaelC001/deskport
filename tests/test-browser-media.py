#!/usr/bin/env python3
"""Build/run isolated browser transport and input checks, without desktop capture.

Pass a libdatachannel installation prefix. Generated binaries stay in the shared
DeskPort build root (or --output). The real transport check uses loopback ICE,
DTLS, SRTP and a DataChannel; it does not establish display/encoder acceptance.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--rtc-prefix', type=Path, required=True)
parser.add_argument('--json-include', type=Path)
parser.add_argument('--output', type=Path, default=Path(os.environ.get('DESKPORT_BUILD_ROOT', str(Path.home()/'mygit/build/deskport'))) / 'browser-client/media/tests')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.output.mkdir(parents=True, exist_ok=True)
compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++')
includes = ['-I'+str(args.rtc_prefix/'include'), '-I'+str(repo/'moonlight-common-c')]
if args.json_include:
    includes.append('-I'+str(args.json_include))
common = [compiler, '-std=c++20', '-DRTC_ENABLE_MEDIA=1'] + includes
input_binary = args.output/'input-codec-test'
subprocess.run(common + [str(repo/'host/browser/tests/input-codec.cpp'), '-o', str(input_binary)], check=True)
subprocess.run([str(input_binary)], check=True, timeout=10)
transport_binary = args.output/'rtc-transport-test'
subprocess.run(common + [str(repo/'host/browser/rtc-session.cpp'), str(repo/'host/browser/tests/rtc-transport.cpp'),
    '-L'+str(args.rtc_prefix/'lib'), '-ldatachannel', '-Wl,-rpath,'+str(args.rtc_prefix/'lib'), '-o', str(transport_binary)], check=True)
subprocess.run([str(transport_binary)], check=True, timeout=40)
