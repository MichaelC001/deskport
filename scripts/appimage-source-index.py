#!/usr/bin/env python3
"""Record exact Ubuntu source archives for added AppImage runtime packages.

The packaging container must have deb-src indices for its binary repositories.
Publish this index alongside the AppImage to provide corresponding-source links.
"""
import json
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1])
packages = {}
for manifest in root.rglob('runtime-licenses/packages.json'):
    for item in json.loads(manifest.read_text()).values():
        packages[(item['source'], item['source_version'])] = item
result = []
for (name, version), package in sorted(packages.items()):
    output = subprocess.check_output(['apt-get', '--print-uris', '--only-source',
        'source', '--download-only', f'{name}={version}'], text=True)
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
