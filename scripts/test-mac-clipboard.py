#!/usr/bin/env python3
"""Exercise external-process native clipboard changes on a unique named board."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-native-clipboard-") as tmp:
    binary = Path(tmp) / "clipboard-test"
    subprocess.run(["xcrun", "clang++", "-std=c++17", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "app"), str(root / "tests/macclipboard.mm"),
                    str(root / "app/backend/macclipboard.mm"), "-framework", "AppKit",
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
