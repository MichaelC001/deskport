#!/usr/bin/env python3
"""Complete and relocate a Qt deployment before signing it.

Run separately on each application before nesting and signing its bundle.
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

app = Path(sys.argv[1]).resolve()
frameworks = app / 'Contents/Frameworks'
frameworks.mkdir(exist_ok=True)
magic = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}

def file_digest(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def machos(root):
    for path in root.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as f:
                if f.read(4) in magic:
                    yield path

origins = {}
source_targets = {}
content_targets = {}
framework_origins = {}
for existing in frameworks.iterdir():
    if existing.is_file() and not existing.is_symlink():
        content_targets[(existing.name, file_digest(existing))] = existing

def flat_target(source):
    """Copy a non-framework dependency without collapsing different ABIs."""
    source = source.resolve()
    if source in source_targets:
        return source_targets[source]
    digest = file_digest(source)
    content_key = (source.name, digest)
    if content_key in content_targets:
        target = content_targets[content_key]
        existing_origin = origins.get(target.resolve())
        if existing_origin is not None and existing_origin != source:
            raise RuntimeError(
                f'Identical dependency came from different loader contexts: '
                f'{existing_origin} and {source}')
    else:
        target = frameworks / source.name
        if target.exists():
            # Different libraries can share one install name (notably Darwin's
            # and GNU's libiconv.2.dylib). Keep both and rewrite each consumer
            # to the exact file it was linked against.
            target = frameworks / f'{source.stem}-{digest[:12]}{source.suffix}'
            if target.exists() and file_digest(target) != digest:
                target = frameworks / f'{source.stem}-{digest}{source.suffix}'
            print(f'Preserving colliding dependency as {target.name}: {source}')
        if target.exists() and file_digest(target) != digest:
            raise RuntimeError(f'Relocation target has conflicting contents: {target}')
        if not target.exists():
            shutil.copy2(source, target)
        content_targets[content_key] = target
    source_targets[source] = target
    origins[target.resolve()] = source
    return target

SYSTEM_ICONV = '/usr/lib/libiconv.2.dylib'

def build_machine_iconv(path):
    """Apple's libiconv built by Nix loads its converter modules and csmapper
    tables from absolute /nix/store paths at run time. Those exist only on the
    packaging machine, so GLib's error text (and every conversion) breaks on
    other Macs. The system copy is the same Apple ABI with system-path data."""
    if path.name != 'libiconv.2.dylib':
        return False
    data = path.read_bytes()
    return b'/nix/store/' in data and b'/share/i18n/csmapper' in data

def loader_rpaths(binary):
    commands = subprocess.check_output(
        ['otool', '-arch', 'arm64', '-l', str(binary)], text=True).splitlines()
    paths = []
    for index, line in enumerate(commands):
        if line.strip() == 'cmd LC_RPATH':
            paths.append(commands[index + 2].strip().removeprefix('path ').split(' (offset')[0])
    return paths

def resolve(dep, binary):
    # A copied library may use @loader_path for a sibling in its original
    # dependency prefix. Resolve from that source location before considering
    # the flattened application Frameworks directory, or provenance is lost as
    # soon as two prefixes contain the same basename.
    origin = origins.get(binary.resolve())
    if origin is not None and dep.startswith('@loader_path/'):
        candidate = origin.parent / dep.removeprefix('@loader_path/')
        if candidate.exists():
            return candidate.resolve()
    if dep.startswith('@loader_path/'):
        candidate = binary.parent / dep.removeprefix('@loader_path/')
        if candidate.exists():
            return candidate.resolve()
    if dep.startswith('@executable_path/'):
        candidate = app / 'Contents/MacOS' / dep.removeprefix('@executable_path/')
        if candidate.exists():
            return candidate.resolve()
    if dep.startswith('@rpath/'):
        suffix = dep.removeprefix('@rpath/')
        source_binary = origins.get(binary.resolve(), binary)
        candidates = set()
        for entry in loader_rpaths(source_binary):
            entry = entry.replace('@loader_path', str(source_binary.parent))
            entry = entry.replace('@executable_path', str(app / 'Contents/MacOS'))
            candidate = Path(entry) / suffix
            if candidate.exists():
                candidates.add(candidate.resolve())
        if len(candidates) == 1:
            return candidates.pop()
        if len(candidates) > 1:
            raise RuntimeError(f'Ambiguous {dep} from {source_binary}: {sorted(candidates)}')
        if source_binary != binary:
            raise RuntimeError(f'Cannot resolve {dep} from original source {source_binary}')
    suffix = dep.removeprefix('@rpath/').removeprefix('@loader_path/')
    for candidate in [frameworks / suffix, Path('/opt/homebrew/lib') / suffix, Path(dep)]:
        if candidate.exists():
            return candidate.resolve()
    for candidate in Path('/opt/homebrew/opt').glob('*/lib/' + suffix):
        if candidate.exists():
            return candidate.resolve()
    raise RuntimeError(f'Cannot resolve {dep} from {binary}')

processed = set()
while True:
    pending = [p for p in machos(app) if p not in processed]
    if not pending:
        break
    for binary in pending:
        binary.chmod(binary.stat().st_mode | 0o200)
        identity = subprocess.check_output(
            ['otool', '-arch', 'arm64', '-D', str(binary)], text=True).splitlines()
        current_identity = identity[1].strip() if len(identity) > 1 else None
        output = subprocess.check_output(['otool', '-arch', 'arm64', '-L', str(binary)], text=True)
        edits = []
        for line in output.splitlines()[1:]:
            dep = line.strip().split(' (')[0]
            if dep == current_identity:
                continue  # LC_ID_DYLIB is metadata, not a dependency to resolve.
            if dep.startswith(('/System/', '/usr/lib/')):
                continue
            source = resolve(dep, binary)
            if build_machine_iconv(source):
                if dep != SYSTEM_ICONV:
                    edits.extend(['-change', dep, SYSTEM_ICONV])
                continue
            target = source
            if not source.is_relative_to(app):
                parts = source.parts
                framework = next((i for i, part in enumerate(parts) if part.endswith('.framework')), None)
                if framework is not None:
                    src_root = Path(*parts[:framework + 1]).resolve()
                    dest_root = frameworks / src_root.name
                    previous = framework_origins.get(dest_root)
                    if previous is not None and previous != src_root:
                        raise RuntimeError(
                            f'Framework name collision: {previous} and {src_root}')
                    if not dest_root.exists():
                        subprocess.run(['ditto', str(src_root), str(dest_root)], check=True)
                        for copied in [dest_root, *dest_root.rglob('*')]:
                            if not copied.is_symlink():
                                copied.chmod(copied.stat().st_mode | 0o200)
                    framework_origins[dest_root] = src_root
                    target = dest_root / Path(*parts[framework + 1:])
                    if not target.exists():
                        raise RuntimeError(f'Framework member is missing after relocation: {target}')
                else:
                    target = flat_target(source)
            if target.resolve() == binary.resolve():
                continue
            new = '@loader_path/' + os.path.relpath(target, binary.parent)
            if new != dep:
                edits.extend(['-change', dep, new])
        if len(identity) > 1 and binary.is_relative_to(frameworks):
            desired_identity = '@rpath/' + str(binary.relative_to(frameworks))
            if identity[1].strip() != desired_identity:
                edits.extend(['-id', desired_identity])
        commands = subprocess.check_output(['otool', '-arch', 'arm64', '-l', str(binary)], text=True).splitlines()
        for index, line in enumerate(commands):
            if line.strip() == 'cmd LC_RPATH':
                path = commands[index + 2].strip().removeprefix('path ').split(' (offset')[0]
                if path.startswith(('/opt/homebrew/', '/usr/local/', '/nix/', '/Users/')):
                    edits.extend(['-delete_rpath', path])
        if edits:
            subprocess.run(['install_name_tool', *edits, str(binary)], check=True)
        processed.add(binary)
# A copy already deployed before relocation is unused once consumers use the system library.
for copy in list(frameworks.glob('libiconv.2*.dylib')):
    data = copy.read_bytes() if copy.is_file() and not copy.is_symlink() else b''
    if b'/nix/store/' in data and b'/share/i18n/csmapper' in data:
        print(f'Removing build-machine libiconv in favour of {SYSTEM_ICONV}: {copy.name}')
        copy.unlink()
print(f'Relocated {len(processed)} Mach-O files into the application')
