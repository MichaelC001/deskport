#!/usr/bin/env python3
"""Extract type-2 AppImages without invoking a host-specific binfmt handler."""
import pathlib
import subprocess
import sys

source, destination = map(pathlib.Path, sys.argv[1:])
data = source.read_bytes()
offset = 0
while True:
    offset = data.find(b'hsqs', offset)
    if offset < 0:
        raise SystemExit(f'No valid SquashFS filesystem in {source}')
    check = subprocess.run(['unsquashfs', '-s', '-o', str(offset), str(source)],
                           capture_output=True)
    if check.returncode == 0:
        break
    offset += 4
subprocess.run(['unsquashfs', '-no-progress', '-d', str(destination), '-o',
                str(offset), str(source)], check=True)
