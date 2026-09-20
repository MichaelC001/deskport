#!/usr/bin/env python3
"""Export full client/host sources and matching static relinking material."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import sys
root = Path(__file__).resolve().parents[2]
wb = root / 'winbuild'
out = Path(sys.argv[1]).resolve()
suffix = os.environ.get("MATERIAL_SUFFIX", "")
inputs = {}
for script in ('build-deps.sh','build-qt.sh','build-vulkan-deps.sh'):
    for value in re.findall(r'^S_\w+=(.+)$', (wb/'scripts'/script).read_text(), re.M):
        value = value.strip('"').replace('$WB',str(wb))
        p = Path(value)
        if p.exists():
            inputs[str(p)] = p.name
for p in map(Path, ['/nix/store/6sh1ninjz55qpy73dxna9i6qi7klry65-mingw-w64-v13.0.0.tar.bz2','/nix/store/4vl8n00z8jzg4vmh4z73gbjafckw6s65-nsis-3.11-src.tar.bz2','/nix/store/vclnb6d6yja2m42pjw5jay5zinnh54y3-gcc-15.2.0.tar.xz','/nix/store/0ilsdqa52nrrzrdx22r3w2i0nx3pmgb2-source']):
    inputs[str(p)] = p.name
manifest = []
def filter_source(info):
    if '.git' in Path(info.name).parts:
        return None
    return info
for name in ('source-materials'+suffix+'.tar.gz','relink-materials'+suffix+'.tar.gz'):
    if (out/name).exists(): raise SystemExit('Refusing to replace existing material: '+name)
with tarfile.open(out/('source-materials'+suffix+'.tar.gz'),'w:gz',compresslevel=1, dereference=True) as tar:
    tracked = subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
    for name in tracked:
        p=root/name
        if not name or name=='shared/deskport-core' or not p.exists(): continue
        tar.add(p,arcname='deskport/'+name,recursive=p.is_dir(),filter=filter_source)
    for name in ('shared/deskport-core','app/deskport.ico','winbuild/scripts','winbuild/toolchain-notices','winbuild/THIRD-PARTY-NOTICES.txt','winbuild/THIRD-PARTY-NOTICES-full.txt','host/windows','app/clipboard/windowsnative.h'):
        tar.add(root/name,arcname='deskport/'+name,filter=filter_source)
    tar.add(out.parent/'shell.nix',arcname='shell.nix')
    full = wb/'full'
    host_names = ('sunshine','media-source','curl','miniupnpc','minhook','onevpl','cppwinrt',
                  'mingw-host-toolchain.cmake','fetch-host-deps.py','download-hashes.json',
                  'host-deps-inputs.json','build-deps-releases.json','VDD-LICENSE',
                  'Signed-Driver-v24.12.24-x64.zip','Sunshine-Windows-AMD64-lite.zip')
    for name in host_names:
        p = full/name
        if p.exists(): tar.add(p,arcname='deskport/winbuild/full/'+name,filter=filter_source)
    for p in full.glob('*.tar.*'):
        tar.add(p,arcname='deskport/winbuild/full/'+p.name,filter=filter_source)
    # CMake FetchContent's pinned Boost source is required for an offline relink/rebuild.
    for dependency in sorted((full/'host-build/_deps').glob('*-src')):
        tar.add(dependency,arcname='deskport/winbuild/full/host-build/_deps/'+dependency.name,filter=filter_source)
    for src, name in inputs.items():
        p=Path(src)
        tar.add(p,arcname='deskport/winbuild/source-inputs/'+name,filter=filter_source)
        if p.is_file():
            manifest.append({'source':src,'path':name,'sha256':hashlib.file_digest(p.open('rb'),'sha256').hexdigest()})
        else:
            h=hashlib.sha256()
            for f in sorted(p.rglob('*')):
                if f.is_file() and '.git' not in f.relative_to(p).parts:
                    h.update(str(f.relative_to(p)).encode()+b'\0')
                    h.update(hashlib.file_digest(f.open('rb'),'sha256').digest())
            manifest.append({'source':src,'path':name,'tree_sha256_path_nul_file_digest':h.hexdigest()})
(out/('source-inputs'+suffix+'.json')).write_text(json.dumps(manifest,indent=2)+'\n')
print('Source archive complete',flush=True)
if '--source-only' in sys.argv: sys.exit(0)
with tarfile.open(out/('relink-materials'+suffix+'.tar.gz'),'w:gz',compresslevel=1) as tar:
    for name in ('build-app','prefix','qt-static','compat-include','toolshim','full/host-build','full/media-prefix','full/prefix','full/ffmpeg-build','full/cbs-build','full/deskport-display-recovery.exe','full/deskport-driver-setup.exe','full/deskport-maintenance.exe'):
        tar.add(wb/name,arcname='deskport/winbuild/'+name,filter=filter_source)
print('Relinking archive complete',flush=True)
