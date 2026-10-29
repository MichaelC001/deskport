#!/usr/bin/env python3
"""Entry point for scripts/build.sh; the implementation is in deskport_build/."""
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from deskport_build.cleanup import deletion_guard  # noqa: E402  (winbuild tests call it here)
from deskport_build.cli import main, run  # noqa: E402

__all__ = ["deletion_guard", "main"]

if __name__ == "__main__":
    run()
