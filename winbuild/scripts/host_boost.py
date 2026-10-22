#!/usr/bin/env python3
"""Keep the exact Windows host Boost source outside disposable build runs."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import tarfile
import tempfile
import urllib.request

VERSION = '1.89.0'
ARCHIVE_SHA256 = '67acec02d0d118b5de9eb441f5fb707b3a1cdd884be00ca24b9a73c995511f74'
ARCHIVE_URL = f'https://github.com/boostorg/boost/releases/download/boost-{VERSION}/boost-{VERSION}-cmake.tar.xz'
ROOT = Path(__file__).resolve().parents[2]


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def source_path(dependency_root):
    return Path(dependency_root) / 'full/boost' / ARCHIVE_SHA256 / 'source'


def checked_external(path):
    path = Path(os.path.abspath(path))
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise SystemExit(f'Boost cache path contains a symlink: {parent}')
    if path == ROOT or ROOT in path.parents or path == Path('/nix/store') or Path('/nix/store') in path.parents or 'runs.noindex' in path.parts:
        raise SystemExit(f'Boost inputs must be outside source, build runs and the Nix store: {path}')
    return path


def verify_host_pin(host_source):
    text = (host_source / 'cmake/dependencies/Boost_Sunshine.cmake').read_text()
    version = re.search(r'set\(BOOST_VERSION\s+"([^"]+)"\)', text)
    checksum = re.search(r'set\(BOOST_HASH\s+"SHA256=([a-f0-9]+)"\)', text)
    if not version or version[1] != VERSION or not checksum or checksum[1] != ARCHIVE_SHA256:
        raise SystemExit('Prepared host Boost pin differs from the reviewed source cache input')


def records_digest(records):
    digest = hashlib.sha256()
    for name in sorted(records):
        digest.update(json.dumps(records[name], separators=(',', ':')).encode() + b'\\n')
    return digest.hexdigest()


def archive_tree_digest(archive):
    """Derive trusted content identity from the verified archive, not its marker."""
    records = {}
    with tarfile.open(archive, 'r|*') as package:
        for member in package:
            path = PurePosixPath(member.name)
            if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] != f'boost-{VERSION}':
                raise SystemExit('Unexpected Boost archive layout')
            relative = PurePosixPath(*path.parts[1:])
            if relative == PurePosixPath('.'):
                if not member.isdir():
                    raise SystemExit('Unexpected Boost archive root')
                continue
            for parent in relative.parents:
                if parent != PurePosixPath('.'):
                    records.setdefault(str(parent), ['directory', str(parent)])
            name = str(relative)
            if member.isdir():
                value = ['directory', name]
            elif member.isfile():
                digest = hashlib.sha256()
                with package.extractfile(member) as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b''):
                        digest.update(block)
                value = ['file', name, bool(member.mode & stat.S_IXUSR), digest.hexdigest()]
            else:
                # The reviewed archive contains only directories and regular files.
                raise SystemExit(f'Unexpected Boost archive entry: {name}')
            if name in records and records[name] != value:
                raise SystemExit(f'Conflicting Boost archive entry: {name}')
            records[name] = value
    if 'LICENSE_1_0.txt' not in records:
        raise SystemExit('Boost archive is missing its license')
    return records_digest(records)


def tree_digest(source):
    """Ignore umask-dependent permissions; include bytes and executable intent."""
    records = {}
    for parent, directories, files in os.walk(source, followlinks=False):
        for name in directories + files:
            path = Path(parent) / name
            info = path.lstat()
            relative = path.relative_to(source).as_posix()
            if stat.S_ISLNK(info.st_mode):
                target = os.readlink(path)
                resolved = path.resolve()
                if resolved != source and source not in resolved.parents:
                    raise SystemExit(f'Boost source link escapes its cache: {relative}')
                value = ['link', relative, target]
            elif stat.S_ISREG(info.st_mode):
                value = ['file', relative, bool(info.st_mode & stat.S_IXUSR), sha256(path)]
            elif stat.S_ISDIR(info.st_mode):
                value = ['directory', relative]
            else:
                raise SystemExit(f'Unsupported Boost source entry: {relative}')
            records[relative] = value
    return records_digest(records)


def prepare(dependency_root, downloads, host_source, archive=None):
    verify_host_pin(Path(host_source))
    source = checked_external(source_path(dependency_root))
    downloads = checked_external(downloads)
    source.parent.mkdir(parents=True, exist_ok=True)
    downloads.mkdir(parents=True, exist_ok=True)
    # The normal backend also holds its recipe lock; this protects standalone use.
    lock_path = checked_external(source.parent / '.prepare.lock')
    with lock_path.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if archive is None:
            archive = downloads / f'boost-{VERSION}-{ARCHIVE_SHA256[:16]}.tar.xz'
            download_lock_path = checked_external(downloads / '.boost-download.lock')
            with download_lock_path.open('a') as download_lock:
                fcntl.flock(download_lock, fcntl.LOCK_EX)
                if not archive.exists():
                    with tempfile.NamedTemporaryFile(dir=downloads, prefix='.boost-download-') as temporary:
                        with urllib.request.urlopen(ARCHIVE_URL, timeout=300) as response:
                            while block := response.read(1024 * 1024):
                                temporary.write(block)
                        temporary.flush()
                        if sha256(Path(temporary.name)) != ARCHIVE_SHA256:
                            raise SystemExit('Boost archive SHA-256 mismatch')
                        # Copy through a verified temporary file; retain the original input.
                        os.link(temporary.name, archive)
        archive = Path(archive)
        if archive.is_symlink() or not archive.is_file() or sha256(archive) != ARCHIVE_SHA256:
            raise SystemExit('Boost archive SHA-256 mismatch')
        expected_tree = archive_tree_digest(archive)
        marker = source.parent / 'source.json'
        identity = dict(schema=1, tree_protocol='content-v1', version=VERSION, archive_sha256=ARCHIVE_SHA256,
                        archive_url=ARCHIVE_URL, source=str(source))
        if source.exists() or marker.exists() or marker.is_symlink():
            if not source.is_dir() or not marker.is_file() or marker.is_symlink():
                raise SystemExit('Refusing unmanaged or incomplete Boost source cache')
            record = json.loads(marker.read_text())
            if tree_digest(source) != expected_tree or record != dict(identity, tree_sha256=expected_tree):
                raise SystemExit('Boost source cache changed; retain it for inspection')
            return source
        with tempfile.TemporaryDirectory(dir=source.parent, prefix='.prepare-') as temporary:
            scratch = Path(temporary)
            with tarfile.open(archive) as package:
                package.extractall(scratch, filter='data')
            extracted = scratch / f'boost-{VERSION}'
            if sorted(p.name for p in scratch.iterdir()) != [extracted.name] or not (extracted / 'LICENSE_1_0.txt').is_file():
                raise SystemExit('Unexpected Boost archive layout')
            fingerprint = tree_digest(extracted)
            if fingerprint != expected_tree:
                raise SystemExit('Extracted Boost source differs from the verified archive')
            temporary_marker = scratch / 'source.json'
            temporary_marker.write_text(json.dumps(dict(identity, tree_sha256=fingerprint), indent=2) + '\n')
            extracted.rename(source)
            temporary_marker.rename(marker)
        return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dependency_root', type=Path)
    parser.add_argument('downloads', type=Path)
    parser.add_argument('host_source', type=Path)
    parser.add_argument('--archive', type=Path, default=os.environ.get('DESKPORT_HOST_BOOST_ARCHIVE'),
                        help='Existing archive (also DESKPORT_HOST_BOOST_ARCHIVE); SHA-256 is always checked')
    args = parser.parse_args()
    print(prepare(args.dependency_root, args.downloads, args.host_source, args.archive))


if __name__ == '__main__':
    main()
