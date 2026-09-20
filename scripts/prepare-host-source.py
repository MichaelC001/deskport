#!/usr/bin/env python3
"""Materialize the verified, repository-owned host source without network access."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'host/vendor/sunshine.json').read_text())
archive = root / 'host/vendor' / manifest['archive']
if hashlib.sha256(archive.read_bytes()).hexdigest() != manifest['sha256']:
    raise SystemExit('Vendored host checksum mismatch')
target = Path(sys.argv[1]).resolve()
# Do not replace a caller's checkout or unknown directory.
marker = target / '.deskport-vendored-source'
if target.exists() and not marker.is_file():
    raise SystemExit(f'Refusing to overwrite unmanaged source directory: {target}')
if target.exists(): shutil.rmtree(target)
target.mkdir(parents=True)
with tarfile.open(archive) as source:
    source.extractall(target, filter='data')
marker.write_text(manifest['sha256'] + '\n')
# Existing reviewed overlays use git apply; no remote or checkout is needed.
subprocess.run(['git', 'init', '-q', str(target)], check=True)
subprocess.run(['git', 'init', '-q', str(target / 'third-party/libvirtualhid')], check=True)
print(f'Prepared vendored host at {target}')
