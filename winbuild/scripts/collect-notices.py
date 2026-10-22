#!/usr/bin/env python3
"""Preserve source notices, including Qt attribution-referenced files."""
import json
import os
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from build_paths import paths, initialize, hold_cache_lock
import shutil
config = initialize(paths())
cache_guard = hold_cache_lock(config)
wb = config['work']
out = Path(sys.argv[1])
for component in ('qtbase', 'qtdeclarative', 'qtshadertools', 'qtsvg', 'libplacebo', 'glslang', 'freetype', 'opus', 'SDL2', 'SDL2_ttf', 'openssl'):
    root = wb / 'work' / component
    for p in root.rglob('*'):
        rel = p.relative_to(root)
        if 'b' in rel.parts or '.git' in rel.parts or not p.is_file():
            continue
        name = p.name.lower()
        if (name.startswith(('license', 'licence', 'copying', 'copyright', 'notice')) or name == 'qt_attribution.json' or 'LICENSES' in rel.parts):
            dst = out / component / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, dst)
        if name == 'qt_attribution.json':
            records = json.loads(p.read_text(), strict=False)
            for record in records if isinstance(records, list) else [records]:
                files = record.get('LicenseFile', [])
                for filename in files if isinstance(files, list) else [files]:
                    src = p.parent / filename
                    if src.is_file():
                        dst = out / component / src.relative_to(root)
                        dst.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(src, dst)
# zlib's exact copyright notice (retain even when the archive listing uses deduplication).
shutil.copyfile(wb / 'work/zlib/LICENSE', out / 'zlib.txt')

shutil.copytree(wb / 'toolchain-notices', out / 'toolchain', dirs_exist_ok=True)
