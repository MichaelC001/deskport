#!/usr/bin/env python3
"""Exercise privacy boundaries and verify the actual ZIP with an independent reader."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-diagnostics-') as temporary:
    work = Path(temporary)
    (work / 'test.pro').write_text(f'''QT += core gui widgets testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
SOURCES += "{root}/tests/diagnostics-test.cpp" "{root}/app/backend/diagnostics.cpp"
HEADERS += "{root}/app/backend/diagnostics.h"
INCLUDEPATH += "{root}/app/backend"
TARGET = diagnostics-test
''')
    env = dict(os.environ, TEST_DIAGNOSTICS_ROOT=temporary, QT_QPA_PLATFORM='offscreen')
    env.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
    subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'], cwd=work, env=env, check=True)
    subprocess.run(['make', '-j4'], cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(work / 'diagnostics-test')], cwd=work, env=env, check=True, timeout=120)
    with zipfile.ZipFile(work / 'verified.zip') as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) <= {'manifest.json', 'README.txt'} | {f'{s}-{n}.jsonl' for s in ('host', 'client', 'display') for n in range(3)}
        manifest = json.loads(archive.read('manifest.json'))
        assert manifest['version'] == (root / 'app/version.txt').read_text().strip()
        assert len(archive.read('host-0.jsonl')) <= 1024 * 1024
        for name in archive.namelist():
            if name.endswith('.jsonl'):
                records = [json.loads(line) for line in archive.read(name).splitlines()]
                assert records
                assert all(set(r) <= {'source', 'run', 'event', 'elapsed_ms', 'stage', 'tick_ms', 'width', 'height', 'phase', 'state', 'error'} for r in records)
    print('Independent ZIP CRC, allowlist, metadata and record validation passed')
