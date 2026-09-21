#!/usr/bin/env python3
"""Extract original toolchain license notices without depending on Nix paths."""
from pathlib import Path
import os
import tarfile
import shutil

wb = Path(__file__).resolve().parents[1]
out = wb / 'toolchain-notices'
source_inputs = wb / 'source-inputs'

archives = [
    ('mingw-w64', Path(os.environ.get('MINGW_W64_SOURCE', source_inputs / 'mingw-w64-v13.0.0.tar.bz2'))),
    ('NSIS', Path(os.environ.get('NSIS_SOURCE', source_inputs / 'nsis-3.11-src.tar.bz2'))),
    ('GCC', Path(os.environ.get('GCC_SOURCE', source_inputs / 'gcc-15.2.0.tar.xz'))),
]
for name, archive in archives:
    if not archive.is_file():
        raise SystemExit(f'Missing {name} source archive: {archive}')
    with tarfile.open(archive) as tar:
        for member in tar:
            p = Path(member.name)
            if member.isfile() and p.name.lower().startswith(('copying','license','copyright','notice')):
                dest = out / name / Path(*p.parts[1:])
                dest.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(member) as src, dest.open('wb') as dst:
                    shutil.copyfileobj(src,dst)

root = Path(os.environ.get('MCFGTHREAD_SOURCE', source_inputs / 'mcfgthread'))
if not root.is_dir():
    raise SystemExit(f'Missing mcfgthread source tree: {root}')
for p in root.rglob('*'):
    if p.is_file() and p.name.lower().startswith(('copying','license')):
        dest = out / 'mcfgthread' / p.relative_to(root)
        dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,dest)
