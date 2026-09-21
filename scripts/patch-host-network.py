#!/usr/bin/env python3
"""Preserve ENet creation failures for the caller instead of dereferencing null."""
from pathlib import Path
import sys

p = Path(sys.argv[1]) / "src/network.cpp"
s = p.read_text()
anchor = "    // Enable opportunistic QoS tagging"
guard = "    if (!host) return host; // DeskPort: ENet creation can fail.\n\n"
if guard not in s:
    if s.count(anchor) != 1:
        raise SystemExit("Unrecognized ENet host initialization")
    s = s.replace(anchor, guard + anchor, 1)
    p.write_text(s)
