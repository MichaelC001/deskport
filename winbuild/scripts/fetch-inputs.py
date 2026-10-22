#!/usr/bin/env python3
"""Fetch and place the pinned Windows build inputs listed in winbuild/inputs.json.

Each file is downloaded from its upstream URLs, then from the project mirror,
into a reusable cache and accepted only when its size and SHA-256 match. It is
then placed once in the recipe-keyed dependency cache. Verified placement
markers preserve compiled dependency trees on later runs. Changed pins select
a new cache, so existing runs and their dependency trees remain intact.

  fetch-inputs.py [--only NAME ...] [--download-only] [--export-mirror DIR]

DESKPORT_WINDOWS_BUILD_ROOT selects the run work directory outside the checkout.
DESKPORT_DEPENDENCY_CACHE_DIR selects persistent dependencies;
DESKPORT_WINDOWS_INPUT_CACHE optionally selects the verified download cache.
"""
import argparse
import fcntl
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path
sys.dont_write_bytecode = True
from build_paths import paths, initialize, cache_lock

SCRIPTS = Path(__file__).resolve().parent
MANIFEST = SCRIPTS.parent / 'inputs.json'


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def verified(path, entry):
    return path.is_file() and path.stat().st_size == entry['bytes'] and sha256(path) == entry['sha256']


def download(entry, cache, mirror):
    target = cache / entry['file']
    if verified(target, entry):
        return target
    errors = []
    for url in entry['urls'] + [mirror + entry['file']]:
        partial = target.with_name(target.name + '.part')
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'deskport-winbuild'})
            with urllib.request.urlopen(request, timeout=300) as response, open(partial, 'wb') as out:
                shutil.copyfileobj(response, out, 1 << 20)
        except Exception as error:  # Try the next source; report all failures together.
            errors.append(f'{url}: {error}')
            partial.unlink(missing_ok=True)
            continue
        if verified(partial, entry):
            os.replace(partial, target)
            return target
        errors.append(f'{url}: size or SHA-256 mismatch')
        partial.unlink(missing_ok=True)
    raise SystemExit(f"Cannot fetch {entry['name']} ({entry['file']}):\n  " + '\n  '.join(errors))


def safe_members(names):
    for name in names:
        parts = Path(name).parts
        if name.startswith('/') or '..' in parts:
            raise SystemExit(f'Refusing archive member outside its root: {name}')


def unpack(archive, entry, scratch):
    """Unpack into scratch and return the directory whose contents are placed."""
    name = archive.name
    if name.endswith('.zip'):
        with zipfile.ZipFile(archive) as source:
            safe_members(source.namelist())
            source.extractall(scratch)
    elif name.endswith('.tar.zst'):
        names = subprocess.run(['tar', '--zstd', '-tf', str(archive)], check=True,
                               capture_output=True, text=True).stdout.splitlines()
        safe_members(names)
        subprocess.run(['tar', '--zstd', '-xf', str(archive), '-C', str(scratch)], check=True)
    else:
        with tarfile.open(archive) as source:
            safe_members(source.getnames())
            source.extractall(scratch, filter='tar')
    root = scratch
    for _ in range(entry.get('strip', 1)):
        children = [p for p in root.iterdir()]
        if len(children) != 1 or not children[0].is_dir():
            raise SystemExit(f"{entry['name']}: cannot strip a level from {archive}")
        root = children[0]
    if 'member' in entry:
        root = root / entry['member']
        if not root.is_dir():
            raise SystemExit(f"{entry['name']}: missing {entry['member']} in {archive}")
    return root


def place(entry, archive, build_root):
    if 'copy' in entry:
        destination = build_root / entry['copy']
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(archive, destination)
        return destination
    destination = build_root / entry['extract']
    with tempfile.TemporaryDirectory(dir=build_root) as scratch:
        source = unpack(archive, entry, Path(scratch))
        if destination.is_symlink() or destination.is_file():
            destination.unlink()
        elif destination.exists():
            shutil.rmtree(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(source), destination)
    if entry.get('lowercase_copies'):
        # mingw-w64 on Linux is case-sensitive; sources include these headers in
        # lower case as the Windows SDK permits.
        for path in sorted(destination.rglob('*')):
            lower = path.with_name(path.name.lower())
            if path.is_file() and lower != path and not lower.exists():
                shutil.copyfile(path, lower)
    for path in destination.rglob('*'):
        if not path.is_symlink():
            path.chmod(path.stat().st_mode | 0o200)
    return destination


def place_once(entry, archive, build_root):
    """Keep matching inputs, including intentionally patched media sources."""
    marker = build_root / '.input-placements.json'
    placements = json.loads(marker.read_text()) if marker.exists() else {}
    relative = entry.get('copy', entry.get('extract'))
    destination = build_root / relative
    identity = hashlib.sha256(json.dumps(entry, sort_keys=True).encode()).hexdigest()
    if placements.get(relative) == identity and destination.exists():
        if 'copy' not in entry or verified(destination, entry):
            return destination
    destination = place(entry, archive, build_root)
    # Replacing a parent extraction invalidates all previously placed children.
    placements = {key: value for key, value in placements.items()
                  if not key.startswith(relative + '/')}
    placements[relative] = identity
    temporary = marker.with_suffix('.tmp')
    temporary.write_text(json.dumps(placements, indent=2) + '\n')
    temporary.replace(marker)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--only', nargs='+', metavar='NAME', help='limit to these input names')
    parser.add_argument('--download-only', action='store_true', help='verify the cache without placing files')
    parser.add_argument('--export-mirror', metavar='DIR', help='copy verified files for the mirror release')
    args = parser.parse_args()

    manifest = json.loads(MANIFEST.read_text())
    config = initialize(paths())
    build_root = config['cache']
    cache = config['downloads']
    cache.mkdir(parents=True, exist_ok=True)
    entries = manifest['inputs']
    if args.only:
        unknown = set(args.only) - {e['name'] for e in entries}
        if unknown:
            raise SystemExit('Unknown inputs: ' + ', '.join(sorted(unknown)))
        entries = [e for e in entries if e['name'] in args.only]

    with cache_lock(config), (cache / '.download.lock').open('a') as download_lock:
        fcntl.flock(download_lock, fcntl.LOCK_EX)
        for entry in entries:
            archive = download(entry, cache, manifest['mirror'])
            if args.export_mirror:
                mirror = Path(args.export_mirror)
                mirror.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(archive, mirror / entry['file'])
            if args.download_only or args.export_mirror:
                print(f"verified {entry['file']}")
                continue
            destination = place_once(entry, archive, build_root)
            print(f"{entry['name']}: {destination.relative_to(build_root)}")
    if args.export_mirror:
        with open(Path(args.export_mirror) / 'SHA256SUMS', 'w') as sums:
            for entry in manifest['inputs']:
                sums.write(f"{entry['sha256']}  {entry['file']}\n")


if __name__ == '__main__':
    sys.exit(main())
