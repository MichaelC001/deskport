"""GitHub release lookup, upload and server-digest verification."""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import re
import subprocess

from . import common
from .assets import asset_info, source_material_archive
from .common import NAME, Refused, check, write_json


def release_metadata(repo, tag):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo) or not NAME.fullmatch(tag):
        raise Refused("Use an explicit OWNER/REPO and a simple release tag")
    return json.loads(check(["gh", "api", f"repos/{repo}/releases/tags/{tag}"]))


def remote_hash(repo, asset):
    # Stream directly through SHA-256; do not keep a second local installer copy.
    process = subprocess.Popen(["gh", "api", f"repos/{repo}/releases/assets/{asset['id']}",
                                "-H", "Accept: application/octet-stream"], stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL)
    h, size = hashlib.sha256(), 0
    assert process.stdout is not None
    try:
        for chunk in iter(lambda: process.stdout.read(1024 * 1024), b""):
            h.update(chunk)
            size += len(chunk)
        if process.wait():
            raise Refused("Could not download the GitHub asset for verification")
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.terminate()
            process.wait()
    return h.hexdigest(), size


def remote_verification(repo, asset, download=False):
    if asset.get("state", "uploaded") != "uploaded":
        raise Refused(f"GitHub asset is not fully uploaded: {asset['name']}")
    if download:
        sha, size = remote_hash(repo, asset)
        return sha, size, "download-sha256"
    value = asset.get("digest")
    if not isinstance(value, str) or not re.fullmatch(r"sha256:[0-9a-fA-F]{64}", value):
        raise Refused(f"GitHub has no SHA-256 digest for {asset['name']}; retain local files or explicitly use --download-verify")
    return value.split(":", 1)[1].lower(), asset["size"], "github-sha256"


def verify_remote(args, paths, record):
    release = release_metadata(args.repo, args.tag)
    if release.get("draft"):
        raise Refused("Draft release assets are not a completed delivery; retain local packages")
    remote = {item["name"]: item for item in release["assets"]}
    verified = []
    for item in record["assets"]:
        other = remote.get(item["name"])
        if not other or other["size"] != item["size"]:
            raise Refused(f"Missing or wrong-size GitHub asset: {item['name']}")
        sha, size, method = remote_verification(args.repo, other, getattr(args, "download_verify", False))
        if (sha, size) != (item["sha256"], item["size"]):
            raise Refused(f"GitHub SHA-256 or size does not match: {item['name']}")
        verified.append({**item, "asset_id": other["id"], "url": other["browser_download_url"],
                         "verification": method})
    if not verified:
        raise Refused("Run has no recorded release assets")
    receipt = {"repo": args.repo, "tag": args.tag, "release_id": release["id"],
               "verified_at": datetime.now(timezone.utc).isoformat(), "assets": verified}
    write_json(paths["record"] / "github-receipt.json", receipt)
    return receipt


def publish(args, paths, record):
    common.privacy()
    if record["source"]["dirty"] or not record.get("source_unchanged") or record.get("failed"):
        raise Refused("Publish requires a successful build from unchanged, clean source")
    if args.target == "macos" and record["status"] != "notarized":
        raise Refused("Public macOS assets must complete the notarized distribution route")
    if any(source_material_archive(a["name"]) for a in record["assets"]):
        raise Refused("Source/relink archives are excluded from release uploads; package a fresh run")
    # Validate every local input before the first upload (including resume).
    for item in record["assets"]:
        path = paths["dist"] / item["path"]
        if asset_info(paths, path) != item:
            raise Refused(f"Local asset changed after packaging: {item['name']}")
    release = release_metadata(args.repo, args.tag)
    existing = {a["name"]: a for a in release["assets"]}
    for item in record["assets"]:
        path = paths["dist"] / item["path"]
        if item["name"] in existing:
            sha, size, _ = remote_verification(args.repo, existing[item["name"]], getattr(args, "download_verify", False))
            if (sha, size) != (item["sha256"], item["size"]):
                raise Refused(f"Existing release asset differs; never overwrite: {item['name']}")
            continue
        subprocess.run(["gh", "release", "upload", args.tag, str(path), "--repo", args.repo], check=True)
    verify_remote(args, paths, record)
    print("All recorded release assets matched by SHA-256 and size.")
