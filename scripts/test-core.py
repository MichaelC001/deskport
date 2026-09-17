#!/usr/bin/env python3
"""Check the pinned core and the production Qt workspace adapter."""
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
core = root / 'shared/deskport-core'
if not (core / 'include/deskport/workspace.h').exists():
    raise SystemExit('Initialize shared/deskport-core with git submodule update --init')
if (core / '.git').exists():
    revision = subprocess.check_output(['git', '-C', str(core), 'rev-parse', 'HEAD'], text=True).strip()
    lock = json.loads((root / 'flake.lock').read_text())
    node = lock['nodes']['root']['inputs']['deskport-core']
    if revision != lock['nodes'][node]['locked']['rev']:
        raise SystemExit('Core submodule and flake.lock differ; update both pins before building')
subprocess.run([sys.executable, str(core / 'tests/test_workspace.py'),
                '--qt-header', str(root / 'app/backend/workspaceresolution.h')], check=True)
