#!/usr/bin/env python3
"""Export modified sources, exact dependency inputs and static relinking material."""
import hashlib
import json
import os
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from build_paths import paths, initialize, hold_cache_lock
from material_archive import material_filter
import re
import subprocess
import tarfile
config = initialize(paths())
cache_guard = hold_cache_lock(config)
root = config['source']
wb = config['work']
out = Path(sys.argv[1]).resolve()
suffix = os.environ.get("MATERIAL_SUFFIX", "")
inputs = {}
for script in ('build-deps.sh','build-qt.sh','build-vulkan-deps.sh'):
    for value in re.findall(r'^S_\w+=(.+)$', (wb/'scripts'/script).read_text(), re.M):
        value = value.strip('"').replace('$WB',str(wb))
        p = Path(value)
        if p.exists():
            inputs[str(p)] = p.name
source_inputs = wb / 'source-inputs'
if source_inputs.is_dir():
    for p in source_inputs.iterdir():
        inputs[str(p)] = p.name
for value in filter(None, os.environ.get('DESKPORT_EXTRA_SOURCE_INPUTS', '').split(os.pathsep)):
    p = Path(value).expanduser().resolve()
    if not p.exists():
        raise SystemExit('Missing extra source input: ' + str(p))
    inputs[str(p)] = p.name
manifest = []
with tarfile.open(out/('source-materials'+suffix+'.tar.gz'),'w:gz',compresslevel=1, dereference=True) as tar:
    filter_source = material_filter()
    tracked = subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
    for name in tracked:
        p=root/name
        if not name or name=='shared/deskport-core' or not p.is_file(): continue
        tar.add(p,arcname='deskport/'+name,recursive=False,filter=filter_source)
    for name in ('shared/deskport-core','app/deskport.ico','winbuild/scripts','winbuild/toolchain-notices'):
        p = wb / name.removeprefix('winbuild/') if name.startswith('winbuild/') else root / name
        tar.add(p,arcname='deskport/'+name,filter=filter_source)
    shell_nix = out.parent / 'shell.nix'
    if shell_nix.exists():
        tar.add(shell_nix, arcname='shell.nix',filter=filter_source)
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
with tarfile.open(out/('relink-materials'+suffix+'.tar.gz'),'w:gz',compresslevel=1, dereference=True) as tar:
    filter_source = material_filter()
    for name in ('build-app','prefix','qt-static','compat-include','toolshim'):
        tar.add(wb/name,arcname='deskport/winbuild/'+name,filter=filter_source)
print('Relinking archive complete',flush=True)
