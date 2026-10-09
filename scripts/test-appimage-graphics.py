#!/usr/bin/env python3
"""Offline checks for coherent, selective host graphics runtime selection."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == 'linux', 'Linux graphics runtime requires GNU coreutils')
class GraphicsRuntime(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='deskport-graphics-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.tools = self.root / 'tools'
        self.tools.mkdir()
        self.system = self.root / 'system'
        self.system.mkdir()
        self.dri = self.system / 'dri'
        self.dri.mkdir()
        for name in ('libc.so.6', 'libm.so.6', 'libstdc++.so.6', 'libdrm.so.2'):
            (self.system / name).touch()
        (self.dri / 'radeonsi_drv_video.so').touch()
        self.script = self.root / 'graphics.sh'
        text = Path(__file__).with_name('appimage-graphics-env.sh').read_text()
        text = text.replace('dp_tools=/run/current-system/sw/bin:/usr/bin:/bin:/usr/sbin:/sbin',
                            'dp_tools=' + shlex.quote(str(self.tools) + ':/usr/bin:/bin'))
        self.script.write_text(text)
        self.prefix = self.root / 'relocated app/usr'
        (self.prefix / 'shared/bin').mkdir(parents=True)
        (self.prefix / 'shared/bin/deskport').touch()
        self.executable('getconf', '#!/bin/sh\nprintf "glibc ${MOCK_GLIBC:-2.42}\\n"\n')
        self.executable('ldd', '#!/bin/sh\n'
            + f'''printf "libc.so.6 => {self.system}/libc.so.6 (0x1)\\n"
               printf "libstdc++.so.6 => {self.system}/libstdc++.so.6 (0x2)\\n"
               printf "libdrm.so.2 => {self.system}/libdrm.so.2 (0x3)\\n"
               printf "{self.system}/ld-linux-x86-64.so.2 (0x4)\\n"\n'''
            + 'if [ "${MOCK_MISSING:-0}" = 1 ]; then printf "missing.so => not found\\n"; fi\n')
        self.loader = self.system / 'ld-linux-x86-64.so.2'
        self.loader.write_text('#!/bin/sh\nexit "${MOCK_LOADER_FAILURE:-0}"\n')
        self.loader.chmod(0o755)

    def executable(self, name, text):
        path = self.tools / name
        path.write_text(text)
        path.chmod(0o755)

    def run_selection(self, **overrides):
        env = dict(os.environ, HOME=str(self.root), XDG_CACHE_HOME=str(self.root / 'cache with spaces'),
                   LIBVA_DRIVERS_PATH=str(self.dri))
        env.update(overrides)
        env.pop('SHARUN_LDNAME', None)
        env.pop('SHARUN_EXTRA_LIBRARY_PATH', None)
        command = '. "$1"; deskport_graphics_env "$2" deskport; printf "%s\\n%s\\n" "${SHARUN_LDNAME:-}" "${SHARUN_EXTRA_LIBRARY_PATH:-}"'
        return subprocess.run(['sh', '-eu', '-c', command, 'test', str(self.script), str(self.prefix)],
                              env=env, capture_output=True, text=True, timeout=10, check=True)

    def test_modern_host_selects_matching_loader_and_only_graphics_closure(self):
        result = self.run_selection()
        loader, directory = result.stdout.splitlines()
        self.assertEqual(loader, str(self.loader))
        cache = Path(directory)
        for name in ('libc.so.6', 'libm.so.6', 'libstdc++.so.6', 'libdrm.so.2'):
            self.assertEqual((cache / name).resolve(), self.system / name)
        self.assertFalse((cache / 'libQt6Core.so.6').exists())
        self.assertFalse((cache / 'libavcodec.so.60').exists())
        self.assertFalse((cache / 'libpipewire-0.3.so.0').exists())
        repeated = self.run_selection()
        self.assertEqual(result.stdout, repeated.stdout)
        self.assertFalse(list(cache.parent.glob('.resolve.*')))

    def test_old_host_keeps_private_runtime(self):
        result = self.run_selection(MOCK_GLIBC='2.35')
        self.assertEqual(result.stdout, '\n\n')
        self.assertFalse((self.root / 'cache with spaces').exists())

    def test_incomplete_driver_closure_is_not_selected(self):
        result = self.run_selection(MOCK_MISSING='1')
        self.assertEqual(result.stdout, '\n\n')
        self.assertFalse(list((self.root / 'cache with spaces/DeskPort/graphics-runtime').glob('.resolve.*')))

    def test_loader_preflight_failure_keeps_private_runtime(self):
        result = self.run_selection(MOCK_LOADER_FAILURE='1')
        self.assertEqual(result.stdout, '\n\n')
        self.assertIn('incompatible host cohort', result.stderr)


if __name__ == '__main__':
    unittest.main()
