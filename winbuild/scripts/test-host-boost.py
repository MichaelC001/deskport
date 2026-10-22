#!/usr/bin/env python3
"""Validate pinned Boost source ownership without compiling or downloading."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
import build_paths
import host_boost

spec = importlib.util.spec_from_file_location('workflow', SCRIPTS.parents[1] / 'scripts/build-workflow.py')
workflow = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workflow)


class HostBoost(unittest.TestCase):
    def setUp(self):
        checkout = subprocess.check_output(['bash', str(SCRIPTS.parents[1] / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()
        parent = Path(os.environ.get('DESKPORT_TEST_DIR', Path(checkout) / 'tests/host-boost'))
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.root = Path(self.temporary.name)
        self.base = self.root / 'build'
        self.cache = self.base / 'dependencies.noindex/windows/recipe'
        self.downloads = self.base / 'dependencies.noindex/windows/downloads'
        self.target = self.base / 'runs.noindex/check/windows'
        self.work = self.target / 'work'
        self.work.mkdir(parents=True)
        self.host = self.work / 'full/sunshine-prepared'
        self.archive = self.root / 'boost.tar.xz'
        self.members = {
            'LICENSE_1_0.txt': b'Boost license fixture\n',
            'boost/version.hpp': b'#define BOOST_VERSION 108900\n',
            'libs/gil/test/extension/io/output/.keep': b'',
            'libs/redis/tools/docker/tls/server.key': b'public upstream fixture\n',
            'unknown/.keep': b'retain user marker\n',
        }
        with tarfile.open(self.archive, 'w:xz') as archive:
            for name, data in self.members.items():
                info = tarfile.TarInfo(f'boost-{host_boost.VERSION}/{name}')
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        self.checksum = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.pin = patch.object(host_boost, 'ARCHIVE_SHA256', self.checksum)
        self.pin.start()
        self.pin_file = self.host / 'cmake/dependencies/Boost_Sunshine.cmake'
        self.pin_file.parent.mkdir(parents=True)
        self.pin_file.write_text(f'set(BOOST_VERSION "{host_boost.VERSION}")\nset(BOOST_HASH "SHA256={self.checksum}")\n')

    def tearDown(self):
        self.pin.stop()
        self.temporary.cleanup()

    def prepare(self):
        return host_boost.prepare(self.cache, self.downloads, self.host, self.archive)

    def test_verified_source_is_shared_and_all_fixtures_are_preserved(self):
        source = self.prepare()
        self.assertEqual(source, host_boost.source_path(self.cache))
        self.assertNotIn('runs.noindex', source.parts)
        for name, data in self.members.items():
            self.assertEqual((source / name).read_bytes(), data)
        self.assertEqual(self.prepare(), source)
        self.assertEqual(hashlib.sha256(self.archive.read_bytes()).hexdigest(), self.checksum)

    def test_archive_checksum_failure_preserves_existing_input(self):
        self.archive.write_bytes(b'wrong archive')
        with self.assertRaisesRegex(SystemExit, 'archive SHA-256 mismatch'):
            self.prepare()
        self.assertEqual(self.archive.read_bytes(), b'wrong archive')
        self.assertFalse(host_boost.source_path(self.cache).exists())

    def test_changed_source_is_refused_without_replacement(self):
        source = self.prepare()
        (source / 'boost/version.hpp').write_text('changed source')
        with self.assertRaisesRegex(SystemExit, 'cache changed'):
            self.prepare()
        self.assertEqual((source / 'boost/version.hpp').read_text(), 'changed source')

    def test_source_and_marker_changed_together_are_refused(self):
        source = self.prepare()
        (source / 'boost/version.hpp').write_text('changed source')
        marker = source.parent / 'source.json'
        record = json.loads(marker.read_text())
        record['tree_sha256'] = host_boost.tree_digest(source)
        marker.write_text(json.dumps(record))
        with self.assertRaisesRegex(SystemExit, 'cache changed'):
            self.prepare()

    def test_dangling_provenance_marker_is_preserved(self):
        source = host_boost.source_path(self.cache)
        source.parent.mkdir(parents=True)
        marker = source.parent / 'source.json'
        marker.symlink_to(self.root / 'missing-record')
        with self.assertRaisesRegex(SystemExit, 'unmanaged or incomplete'):
            self.prepare()
        self.assertTrue(marker.is_symlink())
        self.assertFalse(source.exists())

    def test_content_identity_is_independent_of_directory_umask(self):
        digests = []
        for mask in (0o022, 0o077):
            previous = os.umask(mask)
            try:
                source = host_boost.prepare(self.cache / str(mask), self.downloads, self.host, self.archive)
            finally:
                os.umask(previous)
            digests.append(host_boost.tree_digest(source))
        self.assertEqual(digests[0], digests[1])
        self.assertEqual(digests[0], host_boost.archive_tree_digest(self.archive))

    def test_added_retention_marker_is_refused_and_preserved(self):
        source = self.prepare()
        (source / '.keep').write_text('retain')
        with self.assertRaisesRegex(SystemExit, 'cache changed'):
            self.prepare()
        self.assertEqual((source / '.keep').read_text(), 'retain')

    def test_unmanaged_source_and_tampered_provenance_are_refused(self):
        source = host_boost.source_path(self.cache)
        source.mkdir(parents=True)
        with self.assertRaisesRegex(SystemExit, 'unmanaged or incomplete'):
            self.prepare()
        source.rmdir()
        self.prepare()
        marker = source.parent / 'source.json'
        record = json.loads(marker.read_text())
        record['source'] = str(self.root / 'unrelated')
        marker.write_text(json.dumps(record))
        with self.assertRaisesRegex(SystemExit, 'cache changed'):
            self.prepare()

    def test_source_cache_cannot_live_in_run_or_follow_links(self):
        with self.assertRaisesRegex(SystemExit, 'outside source, build runs'):
            host_boost.prepare(self.work, self.downloads, self.host, self.archive)
        real = self.root / 'real'
        real.mkdir()
        linked = self.root / 'linked'
        linked.symlink_to(real)
        with self.assertRaisesRegex(SystemExit, 'contains a symlink'):
            host_boost.prepare(linked, self.downloads, self.host, self.archive)

    def test_changed_host_pin_is_refused(self):
        self.pin_file.write_text('set(BOOST_VERSION "0.0.0")')
        with self.assertRaisesRegex(SystemExit, 'pin differs'):
            self.prepare()
        self.assertFalse(self.cache.exists())

    def test_escaping_source_link_is_refused(self):
        source = self.prepare()
        (source / 'escape').symlink_to(self.archive)
        with self.assertRaisesRegex(SystemExit, 'link escapes'):
            self.prepare()

    def test_notices_use_the_same_cached_source_and_ignore_stale_run_copy(self):
        source = self.prepare()
        stale = self.work / 'full/host-build-prepared/_deps/boost-src'
        stale.mkdir(parents=True)
        (stale / 'LICENSE_1_0.txt').write_text('stale license')
        out = self.work / 'notices'
        config = {'cache': self.cache, 'work': self.work}
        with patch.object(build_paths, 'paths', return_value=config), patch.object(build_paths, 'initialize', side_effect=lambda c: c), patch.object(build_paths, 'hold_cache_lock', return_value=None), patch.dict(os.environ, {'DESKPORT_HOST_SOURCE_DIR': str(self.host), 'DESKPORT_HOST_BUILD_DIR': str(stale.parents[1])}), patch.object(sys, 'argv', ['collect-host-notices.py', str(out)]):
            runpy.run_path(str(SCRIPTS / 'collect-host-notices.py'), run_name='__main__')
        notice = out / 'host-source-notices/boost-src/LICENSE_1_0.txt'
        self.assertEqual(notice.read_bytes(), (source / 'LICENSE_1_0.txt').read_bytes())
        self.assertNotIn(b'stale', notice.read_bytes())

    @unittest.skipUnless(__import__('shutil').which('cmake'), 'CMake required for the configure-only path check')
    def test_fetchcontent_configures_in_run_and_cleanup_does_not_follow_cache(self):
        source = self.prepare()
        project = self.work / 'probe'
        project.mkdir()
        (project / 'CMakeLists.txt').write_text("""cmake_minimum_required(VERSION 3.24)
project(BoostPathProbe LANGUAGES NONE)
include(FetchContent)
FetchContent_Declare(Boost URL "https://invalid.example/never-download.tar.xz" SOURCE_SUBDIR no_build_probe)
FetchContent_MakeAvailable(Boost)
file(WRITE "${CMAKE_BINARY_DIR}/paths.txt" "${boost_SOURCE_DIR}\\n${boost_BINARY_DIR}\\n")
""")
        build = self.work / 'full/host-build-prepared'
        result = subprocess.run(['cmake', '-S', str(project), '-B', str(build), '-DFETCHCONTENT_SOURCE_DIR_BOOST=' + str(source), '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        actual_source, actual_binary = (build / 'paths.txt').read_text().splitlines()
        self.assertEqual(Path(actual_source), source)
        self.assertEqual(Path(actual_binary), build / '_deps/boost-build')
        # Even if a caller adds an outward source link, protection never descends it.
        (self.work / 'boost-source').symlink_to(source)
        workflow.deletion_guard(self.work, {'base': self.base, 'target': self.target}, [self.work])
        self.assertTrue((source / 'unknown/.keep').is_file())
        self.assertTrue((source / 'libs/redis/tools/docker/tls/server.key').is_file())


if __name__ == '__main__':
    unittest.main()
