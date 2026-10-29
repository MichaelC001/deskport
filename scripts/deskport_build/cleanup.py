"""Guarded deletion of one run's owned outputs."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import stat
import subprocess

from . import common
from .assets import asset_info
from .common import OWNERSHIP, Refused, check, no_links, write_json
from .release import verify_remote


def deletion_guard(path, paths, planned=()):
    no_links(path)
    info = path.stat()
    device = info.st_dev
    if os.path.ismount(path) or device != paths["base"].stat().st_dev or info.st_uid != os.getuid():
        raise Refused(f"Cleanup root has a mount/ownership boundary: {path}")
    if path.parent != paths["target"] or path.name not in {"work", "dist", "logs", "tests"}:
        raise Refused("Cleanup candidate is not an owned purpose directory")
    for parent in (path, *path.parents):
        if (parent / ".keep").exists():
            raise Refused(f"Preservation marker: {parent / '.keep'}")
    for parent, dirs, files in os.walk(path, followlinks=False):
        for name in dirs + files:
            p = Path(parent) / name
            info = p.lstat()
            if info.st_dev != device or os.path.ismount(p):
                raise Refused(f"Mount boundary: {p}")
            if info.st_uid != os.getuid():
                raise Refused(f"Different owner: {p}")
            if (name in {".git", ".keep", ".env", "id_rsa", "id_ed25519"} or name.startswith(".env.") or
                    ("signing" in name.lower() and ".env" in name.lower()) or
                    name.endswith((".key", ".p12", ".pfx", ".p8", ".keychain", ".keychain-db", ".mobileprovision"))):
                raise Refused(f"Source/credential/preservation file in generated output: {p}")
            if not (stat.S_ISREG(info.st_mode) or stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode)):
                raise Refused(f"Special file: {p}")
    # Find links from other runs/caches before removing a target they may reuse.
    for parent, dirs, files in os.walk(paths["base"], followlinks=False):
        here = Path(parent)
        dirs[:] = [d for d in dirs if d != ".git" and here / d != path]
        if here == path or path in here.parents or any(here == p or p in here.parents for p in planned):
            continue
        for name in dirs + files:
            link = here / name
            if link.is_symlink():
                dest = link.resolve()
                if dest == path or path in dest.parents:
                    raise Refused(f"Another output references this tree: {link}")


def inactive(path):
    process_list = check(["ps", "-axo", "pid=,ppid=,command="])
    processes = [line.strip().split(None, 2) for line in process_list.splitlines()]
    parents = {int(v[0]): int(v[1]) for v in processes if len(v) == 3}
    own, pid = {os.getpid()}, os.getpid()
    while parents.get(pid) and parents[pid] not in own:
        pid = parents[pid]
        own.add(pid)
    for fields in processes:
        if len(fields) == 3 and int(fields[0]) not in own and str(path) in fields[2]:
            raise Refused(f"Process {fields[0]} still references {path}")
    lsof = shutil.which("lsof") or "/usr/sbin/lsof"
    result = subprocess.run([lsof, "-nP", "+D", str(path)], capture_output=True, timeout=60)
    if result.returncode not in (0, 1) or result.stderr or result.stdout or result.returncode == 0:
        raise Refused(f"Open files or incomplete activity visibility: {path}")


def clean(args, paths, record):
    keys = ("work", "logs", "tests") if args.work_only else ("work", "dist", "logs", "tests")
    if not args.work_only:
        if record.get("failed"):
            raise Refused("Run has an unfinished/failed phase; use --work-only or finish packaging first")
        for item in record["assets"]:
            local = paths["dist"] / item["path"]
            if local.exists() and asset_info(paths, local) != item:
                raise Refused(f"Local artifact changed after recording: {item['name']}")
        receipt_path = paths["record"] / "github-receipt.json"
        if not receipt_path.exists():
            raise Refused("Verify GitHub delivery first, or use --work-only to retain local packages")
        receipt = json.loads(receipt_path.read_text())
        args.repo, args.tag = receipt["repo"], receipt["tag"]
        verify_remote(args, paths, record)  # Fresh server digest, even if an old receipt exists.
    plan = []
    candidates = [paths[key] for key in keys if paths[key].exists()]
    for key in keys:
        path = paths[key]
        if not path.exists():
            continue
        current = common.footprint(path)
        # Allocated blocks can settle after writes without changing file identity.
        # Keep bytes for capacity reporting, not the tree-change decision.
        if current["fingerprint"] != record.get("trees", {}).get(key, {}).get("fingerprint"):
            raise Refused(f"Output changed since the recorded build: {path}; inspect it manually")
        deletion_guard(path, paths, candidates)
        inactive(path)
        plan.append({"path": str(path), "purpose": key, **current})
    write_json(paths["record"] / "cleanup-plan.json", {"owner": OWNERSHIP, "items": plan})
    print(json.dumps({"cleanup": plan, "apply": args.apply}, indent=2))
    if not args.apply:
        return
    if not shutil.rmtree.avoids_symlink_attacks:
        raise Refused("This Python cannot safely remove directory trees")
    before = common.git_state()
    journal = []
    for item in plan:
        path = Path(item["path"])
        deletion_guard(path, paths, candidates)
        inactive(path)
        if common.footprint(path)["fingerprint"] != item["fingerprint"]:
            raise Refused("Output changed after cleanup planning")
        fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            info = os.stat(path.name, dir_fd=fd, follow_symlinks=False)
            if not stat.S_ISDIR(info.st_mode) or info.st_ino != path.stat().st_ino:
                raise Refused("Cleanup directory changed while pinning it")
            shutil.rmtree(path.name, dir_fd=fd)
        finally:
            os.close(fd)
        journal.append({"path": str(path), "deleted": not path.exists()})
        write_json(paths["record"] / "cleanup-journal.json", journal)
        if common.git_state() != before:
            raise Refused("Source state changed during cleanup; remaining paths retained")
    record["cleaned"] = "work" if args.work_only else "all"
    write_json(paths["record"] / "run.json", record)
    # Only remove now-empty container directories; retained metadata stays separate.
    for path in (paths["target"], paths["target"].parent):
        try:
            path.rmdir()
        except OSError:
            pass
