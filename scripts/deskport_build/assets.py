"""Recorded package assets and their optional filename suffix."""
from __future__ import annotations

import os
from pathlib import Path
import re

from .common import ASSET_SUFFIX, NAME, Refused, digest, no_links


def asset_suffix(value):
    if value is not None and not ASSET_SUFFIX.fullmatch(value):
        raise Refused("Asset suffix must be 1-32 alphanumeric/dash/underscore characters, starting with an alphanumeric")
    return value or ""


def suffixed_name(name, suffix):
    if not suffix:
        return name
    extension = next((ext for ext in (".pkg.tar.zst", ".tar.gz", ".tar.xz", ".tar.zst")
                      if name.endswith(ext)), Path(name).suffix)
    stem = name[:-len(extension)] if extension else name
    result = stem + ("" if stem.endswith("-" + suffix) else "-" + suffix) + extension
    if not NAME.fullmatch(result):
        raise Refused("Suffixed asset name exceeds the portable filename limit")
    return result


def suffix_assets(paths, assets, suffix):
    """Preflight the complete rename set; never overwrite an existing file."""
    plan = [(paths["dist"] / a["path"],
             (paths["dist"] / a["path"]).with_name(suffixed_name(a["name"], suffix))) for a in assets]
    if len({destination.name for _, destination in plan}) != len(plan):
        raise Refused("Asset suffix would create duplicate download names")
    for source, destination in plan:
        no_links(destination)
        if source != destination and destination.exists():
            raise Refused(f"Asset suffix destination already exists: {destination.name}")
    for source, destination in plan:
        if source != destination:
            # Unlike rename(), link() refuses a destination created after preflight.
            os.link(source, destination)
            source.unlink()
    return [asset_info(paths, destination) for _, destination in plan]


def asset_info(paths, path):
    no_links(path)
    if not path.is_file() or paths["dist"] not in path.parents:
        raise Refused(f"Asset must be a regular file in this run's dist directory: {path}")
    if not NAME.fullmatch(path.name):
        raise Refused("Asset names must use portable alphanumeric/dot/dash/underscore characters")
    return {"path": str(path.relative_to(paths["dist"])), "name": path.name,
            "size": path.stat().st_size, "sha256": digest(path)}


def source_material_archive(name):
    return name.endswith((".tar.gz", ".tar.xz", ".tar.zst", ".zip")) and bool(
        re.search(r"(^|[-_])(source|sources|material|materials|relink)([-_.]|$)", name, re.IGNORECASE))


def collect_assets(paths, target, status):
    location = paths["dist"] / "notarized" if target == "macos" and status == "notarized" else paths["dist"]
    extensions = (".dmg", ".zip", ".exe", ".AppImage", ".deb", ".rpm", ".pkg.tar.zst", ".tar.gz", ".flatpak")
    items = [asset_info(paths, p) for p in sorted(location.iterdir())
             if p.name.startswith(("DeskPort-", "deskport-", "deskport_")) and p.name.endswith(extensions)
             and not source_material_archive(p.name)]
    if not items:
        raise Refused("No publishable package files found")
    return items
