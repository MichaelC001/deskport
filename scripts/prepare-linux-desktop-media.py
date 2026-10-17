#!/usr/bin/env python3
"""Prepare cached tools for the existing isolated browser media fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--thread', required=True)
parser.add_argument('--playwright', type=Path, required=True, help='Existing playwright-core package directory')
parser.add_argument('--gateway-source', type=Path, help='Explicit source tree matching the retained host package')
parser.add_argument('--gateway-revision', help='Source revision of the explicit gateway tree')
for name in ('chromium', 'pulseaudio', 'xvfb', 'xterm', 'ffmpeg', 'node', 'openssl'):
    parser.add_argument('--' + name, type=Path, required=True, help='Existing /nix/store tool; no downloads')
args = parser.parse_args()
assert sys.platform == 'linux' and os.sched_getaffinity(0) <= {0, 1, 2, 3}
assert os.getpriority(os.PRIO_PROCESS, 0) >= 15
root = Path.home() / 'mygit/build/deskport'
output = args.output.resolve()
assert output.is_relative_to(root.resolve())
owner = json.loads((root / 'locks/pk4-heavy-build.lock/owner.json').read_text())
assert owner['thread'] == args.thread
assert Path('/proc/sys/kernel/random/boot_id').read_text().strip() == owner['boot_id']
assert Path(f'/proc/{owner["pid"]}/stat').read_text().rsplit(')', 1)[1].split()[19] == owner['start_ticks']
ancestor = os.getpid()
while ancestor not in (1, owner['pid']):
    ancestor = int(Path(f'/proc/{ancestor}/stat').read_text().rsplit(')', 1)[1].split()[1])
assert ancestor == owner['pid'], 'Preparation must run inside its owning heavy reservation'
tools = {name: str(getattr(args, name)) for name in
         ('chromium', 'pulseaudio', 'xvfb', 'xterm', 'ffmpeg', 'node', 'openssl')}
for value in tools.values():
    assert value.startswith('/nix/store/') and Path(value).is_file(), value
assert (args.playwright / 'package.json').is_file()
assert shutil.disk_usage(output.parent).free > 10 * 1024 ** 3
output.mkdir(exist_ok=False)
(output / '.keep').touch()
repo = Path(__file__).resolve().parents[1]
gateway_source = args.gateway_source.resolve() if args.gateway_source else repo
if args.gateway_revision:
    assert re.fullmatch('[0-9a-f]{40}', args.gateway_revision)
subprocess.run([sys.executable, str(gateway_source / 'scripts/build-browser-live-gateway.py'),
                '--output', str(output / 'work/gateway')], check=True)
gateway = output / 'work/gateway/browser-live-gateway'
inputs = output / 'inputs'
(inputs / 'scripts').mkdir(parents=True)
shutil.copy2(gateway, inputs / 'browser-live-gateway')
shutil.copytree(args.playwright, inputs / 'playwright-core')
for name in ('test-browser-native.py', 'test-browser-native.mjs'):
    shutil.copy2(repo / 'scripts' / name, inputs / 'scripts' / name)
files = {}
for path in sorted(inputs.rglob('*')):
    assert not path.is_symlink(), path
    if path.is_file():
        files[str(path.relative_to(inputs))] = sha(path)
roots = {str(Path('/nix/store') / Path(value).relative_to('/nix/store').parts[0]) for value in tools.values()}
dependencies = subprocess.check_output(['ldd', str(gateway)], text=True)
assert 'not found' not in dependencies, dependencies
roots.update(re.findall(r'/nix/store/[^/\s]+', dependencies))
closure = sorted(set(subprocess.check_output(['nix-store', '--query', '--requisites', *sorted(roots)], text=True).splitlines()))
archive_path = output / 'tool-runtime.tar.gz'
with tarfile.open(archive_path, 'w:gz', compresslevel=0) as archive:
    for name in closure:
        assert name.startswith('/nix/store/') and Path(name).exists(), name
        archive.add(name, arcname=str(Path(name).relative_to('/')))
sources = ['tests/browser-live-gateway.cpp', 'app/backend/browsergateway.cpp',
           'app/backend/browsergateway.h', 'app/res/deskport.svg', 'scripts/build-browser-live-gateway.py']
sources += [str(p.relative_to(gateway_source)) for p in (gateway_source / 'app/browser').rglob('*') if p.is_file()]
manifest = dict(schema=1, tools=tools, files=files, runtime_sha256=sha(archive_path),
    runtime_bytes=archive_path.stat().st_size, store_paths=closure,
    source_files={name: sha(gateway_source / name) for name in sorted(sources)},
    application_revision=args.gateway_revision,
    fixture_files={name: sha(repo / 'scripts' / name) for name in ('test-browser-native.py', 'test-browser-native.mjs')},
    playwright_version=json.loads((args.playwright / 'package.json').read_text())['version'],
    input_enabled=False, personal_desktop=False)
(output / 'source.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(dict(output=str(output), runtime_bytes=manifest['runtime_bytes'], store_paths=len(closure))))
