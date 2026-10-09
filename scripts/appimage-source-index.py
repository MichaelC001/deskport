#!/usr/bin/env python3
"""Record exact Ubuntu source archives for added AppImage runtime packages.

The packaging container must have deb-src indices for its binary repositories.
Publish this index alongside the AppImage to provide corresponding-source links.
"""
import json
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import urllib.parse
import urllib.request


def archived_source(name, version):
    """Launchpad retains exact sources after apt drops superseded versions."""
    filename = f'{name}_{version.split(":", 1)[-1]}.dsc'
    base = ('https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/'
            + urllib.parse.quote(name, safe='') + '/'
            + urllib.parse.quote(version, safe='') + '/')
    url = base + urllib.parse.quote(filename, safe='')
    with urllib.request.urlopen(url, timeout=30) as response:
        data = response.read()
    text = data.decode('utf-8')
    if not re.search(r'^Source: ' + re.escape(name) + r'$', text, re.M) or not re.search(r'^Version: ' + re.escape(version) + r'$', text, re.M):
        raise RuntimeError(f'Archived source identity mismatch: {name} {version}')
    hashes = re.search(r'^Checksums-Sha256:\n((?:[ \t]+[^\n]+\n)+)', text, re.M)
    if not hashes:
        raise RuntimeError(f'Archived source has no SHA-256 list: {name} {version}')
    archives = [dict(url=url, filename=filename, bytes=len(data),
                     checksum='SHA256:' + hashlib.sha256(data).hexdigest())]
    for line in hashes.group(1).splitlines():
        digest, size, filename = line.split()
        if not re.fullmatch('[0-9a-f]{64}', digest) or Path(filename).name != filename:
            raise RuntimeError(f'Invalid archived source record: {line}')
        archives.append(dict(url=base + urllib.parse.quote(filename, safe=''),
                             filename=filename, bytes=int(size), checksum='SHA256:' + digest))
    return archives

root = Path(sys.argv[1])
packages = {}
for manifest in root.rglob('runtime-licenses/packages.json'):
    for item in json.loads(manifest.read_text()).values():
        packages[(item['source'], item['source_version'])] = item
result = []
for (name, version), package in sorted(packages.items()):
    try:
        output = subprocess.check_output(['apt-get', '--print-uris', '--only-source',
            'source', '--download-only', f'{name}={version}'], text=True)
    except subprocess.CalledProcessError:
        archives = archived_source(name, version)
        result.append(dict(source=name, version=version, archives=archives))
        print(f'Indexed archived Ubuntu source: {name} {version}')
        continue
    archives = []
    for line in output.splitlines():
        match = re.match(r"'([^']+)' (\S+) (\d+) (\S+)", line)
        if match:
            url, filename, size, checksum = match.groups()
            archives.append(dict(url=url.replace('http://', 'https://', 1),
                                 filename=filename, bytes=int(size), checksum=checksum))
    if not archives or not any(x['filename'].endswith('.dsc') for x in archives):
        raise RuntimeError(f'No exact corresponding source archives: {name} {version}')
    result.append(dict(source=name, version=version, archives=archives))
Path(sys.argv[2]).write_text(json.dumps(result, indent=2) + '\n')
print(f'Indexed exact corresponding sources for {len(result)} runtime source packages')
