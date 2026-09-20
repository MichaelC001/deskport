#!/usr/bin/env python3
"""Extract original toolchain license notices without compiling toolchain sources."""
from pathlib import Path
import tarfile
import shutil
out = Path(__file__).resolve().parents[1] / 'toolchain-notices'
for name, archive in [('mingw-w64', '/nix/store/6sh1ninjz55qpy73dxna9i6qi7klry65-mingw-w64-v13.0.0.tar.bz2'), ('NSIS','/nix/store/4vl8n00z8jzg4vmh4z73gbjafckw6s65-nsis-3.11-src.tar.bz2'), ('GCC','/nix/store/vclnb6d6yja2m42pjw5jay5zinnh54y3-gcc-15.2.0.tar.xz')]:
    with tarfile.open(archive) as tar:
        for member in tar:
            p = Path(member.name)
            if member.isfile() and p.name.lower().startswith(('copying','license','copyright','notice')):
                dest = out / name / Path(*p.parts[1:])
                dest.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(member) as src, dest.open('wb') as dst:
                    shutil.copyfileobj(src,dst)
root = Path('/nix/store/0ilsdqa52nrrzrdx22r3w2i0nx3pmgb2-source')
for p in root.rglob('*'):
    if p.is_file() and p.name.lower().startswith(('copying','license')):
        dest = out / 'mcfgthread' / p.relative_to(root)
        dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,dest)
