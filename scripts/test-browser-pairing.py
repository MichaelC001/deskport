#!/usr/bin/env python3
"""Run real persistent-browser pairing with isolated native capture.

Accepts the same arguments as test-browser-native.py. The fixture uses a private
local control socket to restart/revoke only its own HTTPS gateway. Example:
  python3 scripts/test-browser-pairing.py --help
All output, including private browser profiles, stays in --output.
"""
from pathlib import Path
import runpy
import sys

scripts = Path(__file__).resolve().parent
sys.argv.extend(['--pairing-control', '--driver', str(scripts/'test-browser-pairing.mjs')])
runpy.run_path(str(scripts/'test-browser-native.py'), run_name='__main__')
