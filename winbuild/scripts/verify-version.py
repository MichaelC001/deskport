#!/usr/bin/env python3
"""Reject stale Windows payloads before they can receive a new package label."""
import argparse
from pathlib import Path
import re
import struct


def verify(executable, expected):
    if not re.fullmatch(r"\d+\.\d+\.\d+", expected):
        raise ValueError(f"Invalid release version: {expected}")
    data = Path(executable).read_bytes()
    pe = struct.unpack_from("<I", data, 60)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Not a PE executable")
    # Read only the PE resource section, never an unrelated signature in code.
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    resource = None
    for index in range(sections):
        offset = pe + 24 + optional_size + index * 40
        if data[offset:offset + 8].rstrip(b"\0") == b".rsrc":
            size, start = struct.unpack_from("<II", data, offset + 16)
            resource = data[start:start + size]
            break
    if resource is None:
        raise ValueError("Missing PE resources")
    key = "VS_VERSION_INFO\0".encode("utf-16le")
    key_offset = resource.find(key)
    if key_offset < 6 or resource.find(key, key_offset + 1) != -1:
        raise ValueError("Missing or ambiguous version resource")
    fixed = (key_offset + len(key) + 3) & ~3
    signature, structure, file_ms, file_ls, product_ms, product_ls = struct.unpack_from("<6I", resource, fixed)
    if signature != 0xFEEF04BD or structure != 0x10000:
        raise ValueError("Invalid VS_FIXEDFILEINFO")
    wanted = tuple(map(int, expected.split("."))) + (0,)
    for label, ms, ls in (("FileVersion", file_ms, file_ls), ("ProductVersion", product_ms, product_ls)):
        actual = (ms >> 16, ms & 65535, ls >> 16, ls & 65535)
        if actual != wanted:
            raise ValueError(f"{label} {'.'.join(map(str, actual))} != {expected}; rebuild the client before packaging")
    return expected


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("version")
    args = parser.parse_args()
    print(f"PASS: {args.executable.name} PE file/product version {verify(args.executable, args.version)}")
