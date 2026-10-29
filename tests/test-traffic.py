#!/usr/bin/env python3
"""Exercise the same forced socket instrumentation as the transport build."""
from pathlib import Path
import os
import sys
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-traffic-") as work:
    binary = str(Path(work) / "traffic")
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-pthread", "-Wall", "-Wextra",
                    *(["-D_GNU_SOURCE=1"] if sys.platform.startswith("linux") else []),
                    "-include", str(root / "moonlight-common-c/traffic.h"),
                    str(root / "moonlight-common-c/traffic.c"), str(root / "tests/traffic.c"),
                    "-o", binary], check=True)
    subprocess.run([binary], check=True, timeout=20)
