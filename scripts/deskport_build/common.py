"""Shared constants, refusals and small file and Git helpers."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TARGETS = ("macos", "linux-nix", "linux-portable", "windows")
SCHEMA = 1
OWNERSHIP = "deskport-managed-build-v1"
SIGN_KEYS = {"DESKPORT_SIGN_IDENTITY", "DESKPORT_SIGN_TEAM", "DESKPORT_NOTARY_PROFILE"}
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,100}\Z")
ASSET_SUFFIX = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,31}\Z")


class Refused(RuntimeError):
    pass


def check(command, **kwargs):
    return subprocess.check_output([str(x) for x in command], **kwargs).decode().strip()


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
    os.replace(tmp, path)


def no_links(path):
    for entry in (path, *path.parents):
        if entry.is_symlink():
            raise Refused(f"Symlink in managed path: {entry}")


def git_state():
    return {"revision": check(["git", "rev-parse", "HEAD"], cwd=ROOT),
            "dirty": bool(check(["git", "status", "--porcelain", "--untracked-files=normal"], cwd=ROOT)),
            "diff_sha256": hashlib.sha256(subprocess.check_output(
                ["git", "diff", "HEAD", "--binary"], cwd=ROOT)).hexdigest(),
            "core": check(["git", "-C", ROOT / "shared/deskport-core", "rev-parse", "HEAD"])}


def footprint(path):
    """lstat only: never follow links into shared dependencies or source trees."""
    if not path.exists():
        return {"fingerprint": None, "bytes": 0}
    h, size = hashlib.sha256(), 0
    root_info = path.lstat()
    h.update(json.dumps([root_info.st_ino, root_info.st_dev, root_info.st_mode]).encode())
    for parent, dirs, files in os.walk(path, followlinks=False):
        dirs.sort()
        for name in sorted(dirs + files):
            item = Path(parent) / name
            info = item.lstat()
            size += info.st_blocks * 512
            h.update(json.dumps([str(item.relative_to(path)), info.st_mode, info.st_ino,
                                 info.st_size, info.st_mtime_ns, info.st_ctime_ns,
                                 os.readlink(item) if item.is_symlink() else None]).encode())
    return {"fingerprint": h.hexdigest(), "bytes": size}


def privacy():
    subprocess.run([sys.executable, ROOT / "scripts/check-build-privacy.py"], cwd=ROOT, check=True)
