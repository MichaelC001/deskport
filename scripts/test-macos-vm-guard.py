#!/usr/bin/env python3
"""Exercise VM storage safeguards without downloading or starting any VM."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('vm', Path(__file__).with_name('macos-vm.py'))
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)


class GuardTests(unittest.TestCase):
    def test_free_space_failure_never_starts_tool(self):
        with patch.object(vm, 'MIN_FREE', 2**80), patch.object(vm.subprocess, 'Popen') as spawn:
            # Real status() needs a subprocess for du; use a measured status first.
            measured = {'safe': False, 'free_gib': 0}
            with patch.object(vm, 'status', return_value=measured):
                with self.assertRaises(RuntimeError):
                    vm.supervised(['run', 'unused'])
            spawn.assert_not_called()

    def test_allocated_usage_and_running_child_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            lab = Path(directory)
            tool = lab/'fake-tart'
            tool.write_text('#!/bin/sh\ndd if=/dev/urandom of="' + str(lab/'payload') + '" bs=1048576 count=2 2>/dev/null\nexec sleep 60\n')
            tool.chmod(0o755)
            with patch.object(vm, 'LAB', lab), patch.object(vm, 'TART', tool), \
                 patch.object(vm, 'MAX_USED', 1024**2), patch.object(vm, 'MIN_FREE', 0):
                self.assertTrue(vm.status()['safe'])
                with self.assertRaises(RuntimeError):
                    vm.supervised(['run', 'unused'])
                self.assertFalse(vm.status()['safe'])
            # supervised() waits for its terminated child before returning.


if __name__ == '__main__':
    unittest.main()
