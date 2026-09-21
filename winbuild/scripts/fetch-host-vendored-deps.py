#!/usr/bin/env python3
"""Fetch only the fixed public submodule archives absent from the host tarball."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

source = Path(sys.argv[1]).resolve()
manifest_path = Path(__file__).with_name("host-vendored-deps.json")
cache = Path(os.environ.get("DESKPORT_HOST_DEP_CACHE", source.parent / "host-vendored-deps"))
cache.mkdir(parents=True, exist_ok=True)

def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

for item in json.loads(manifest_path.read_text()):
    archive = cache / (item["name"] + ".tar.gz")
    if archive.exists() and sha256(archive) != item["archive_sha256"]:
        archive.unlink()
    if not archive.exists():
        temporary = cache / (archive.name + ".part")
        if temporary.exists():
            temporary.unlink()
        urllib.request.urlretrieve(item["url"], temporary)
        if sha256(temporary) != item["archive_sha256"]:
            temporary.unlink()
            raise SystemExit("Host dependency archive checksum mismatch: " + item["name"])
        temporary.replace(archive)

    with tempfile.TemporaryDirectory(prefix="deskport-host-dep-") as staging_name:
        staging = Path(staging_name)
        with tarfile.open(archive) as package:
            package.extractall(staging, filter="data")
        roots = [path for path in staging.iterdir() if path.is_dir()]
        if len(roots) != 1:
            raise SystemExit("Unexpected archive layout: " + item["name"])
        extracted = roots[0]
        for relative, expected in item["files"].items():
            path = extracted / relative
            if not path.is_file() or sha256(path) != expected:
                raise SystemExit("Host dependency file checksum mismatch: " + item["name"] + "/" + relative)
        target = source / item["target"]
        target.mkdir(parents=True, exist_ok=True)
        shutil.copytree(extracted, target, dirs_exist_ok=True)

print("Verified host dependency sources", flush=True)
