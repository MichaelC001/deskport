#!/usr/bin/env python3
"""Exercise production display policy and recovery without touching real displays."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-topology-") as temporary:
    binary = Path(temporary) / "topology"
    subprocess.run(["xcrun", "clang", "-fobjc-arc", "-fblocks", "-fsanitize=address,undefined",
                    str(root / "tests/session-topology.m"), "-framework", "Foundation",
                    "-framework", "ApplicationServices", "-framework", "AppKit", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
