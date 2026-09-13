#!/usr/bin/env python3
"""Sign or audit every embedded code object for Developer ID distribution."""
import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('app', type=pathlib.Path)
parser.add_argument('--identity', help='Exact Developer ID certificate SHA-1')
parser.add_argument('--team', required=True)
parser.add_argument('--verify-only', action='store_true')
args = parser.parse_args()
root = args.app.resolve()
if root.suffix != '.app' or not (root / 'Contents/Info.plist').is_file():
    parser.error('Expected an application bundle')
if not re.fullmatch(r'[A-Z0-9]{10}', args.team):
    parser.error('Expected a ten-character Apple Team ID')
if not args.verify_only and not args.identity:
    parser.error('--identity is required when signing')
entitlements = pathlib.Path(__file__).resolve().parents[1] / 'host/macos/entitlements.plist'
magic = {bytes.fromhex(x) for x in (
    'feedface', 'cefaedfe', 'feedfacf', 'cffaedfe',
    'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}
code = []
bundles = []
for path in root.rglob('*'):
    if path.is_symlink():
        continue
    if path.is_dir() and path.suffix in ('.app', '.framework', '.bundle', '.xpc'):
        # Resource-only bundles do not have an executable to sign.
        if (path / 'Contents/MacOS').is_dir() or (path / 'Versions').is_dir():
            bundles.append(path)
    if path.is_file():
        with path.open('rb') as stream:
            if stream.read(4) in magic:
                code.append(path)
objects = sorted(set(code + bundles + [root]), key=lambda p: (-len(p.parts), str(p)))
requirement = ('=anchor apple generic and certificate 1[field.1.2.840.113635.100.6.2.6] exists '
               'and certificate leaf[field.1.2.840.113635.100.6.1.13] exists '
               f'and certificate leaf[subject.OU] = "{args.team}"')
for index, path in enumerate(objects, 1):
    if not args.verify_only:
        command = ['/usr/bin/codesign', '--force', '--sign', args.identity,
                   '--timestamp', '--options', 'runtime']
        if path == root or path == root / 'Contents/Helpers/Sunshine.app':
            command += ['--entitlements', str(entitlements)]
        subprocess.run(command + [str(path)], check=True, capture_output=True)
    subprocess.run(['/usr/bin/codesign', '--verify', '--strict', '-R', requirement,
                    str(path)], check=True, capture_output=True)
    details = subprocess.run(['/usr/bin/codesign', '-dvv', str(path)],
                             check=True, capture_output=True, text=True).stderr
    if 'Timestamp=' not in details or '(runtime)' not in details:
        raise SystemExit(f'Missing secure timestamp or hardened runtime: {path}')
    if index % 25 == 0:
        print(f'Checked {index}/{len(objects)} code objects', flush=True)
subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(root)], check=True)
print(f'PASS: {len(code)} Mach-O files and {len(bundles) + 1} bundles; '
      f'Developer ID team {args.team}, secure timestamps, hardened runtime')
