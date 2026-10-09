#!/usr/bin/env python3
"""Check superseded-source indexing without apt or network access."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from unittest.mock import patch

script = Path(__file__).with_name('appimage-source-index.py')
digest = 'a' * 64
valid = (f'Source: fixture\nVersion: 1:2.3-4\nChecksums-Sha256:\n'
         f' {digest} 42 fixture_2.3.orig.tar.xz\n').encode()
with tempfile.TemporaryDirectory(prefix='deskport-source-index-') as temporary:
    root = Path(temporary)
    notices = root / 'runtime-licenses'
    notices.mkdir()
    (notices / 'packages.json').write_text(json.dumps({'fixture': {
        'binary': 'fixture', 'version': '1:2.3-4', 'source': 'fixture',
        'source_version': '1:2.3-4'}}))
    destination = root / 'index.json'
    for data, expected in [(valid, True),
                           (valid.replace(b'Source: fixture', b'Source: other'), False),
                           (valid.replace(b'Checksums-Sha256:', b'Checksums-Sha1:'), False),
                           (valid.replace(b'fixture_2.3.orig.tar.xz', b'../unsafe.tar.xz'), False)]:
        with patch.object(sys, 'argv', [str(script), str(root), str(destination)]), \
             patch('subprocess.check_output', side_effect=subprocess.CalledProcessError(100, ['apt-get'])), \
             patch('urllib.request.urlopen', return_value=io.BytesIO(data)) as download, \
             contextlib.redirect_stdout(io.StringIO()):
            try:
                runpy.run_path(str(script), run_name='__main__')
            except RuntimeError:
                assert not expected
            else:
                assert expected
                result = json.loads(destination.read_text())[0]
                assert result['version'] == '1:2.3-4'
                dsc, archive = result['archives']
                assert dsc['filename'] == 'fixture_2.3-4.dsc'
                assert dsc['bytes'] == len(data)
                assert dsc['checksum'] == 'SHA256:' + hashlib.sha256(data).hexdigest()
                assert '/fixture/1%3A2.3-4/' in dsc['url']
                assert archive['bytes'] == 42 and archive['checksum'] == 'SHA256:' + digest
                assert archive['url'].endswith('/fixture_2.3.orig.tar.xz')
            assert download.call_count == 1
print('PASS: archived source identity, epoch filenames, SHA-256 and unsafe-record rejection')
