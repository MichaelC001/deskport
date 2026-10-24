#!/usr/bin/env python3
"""Build, publish and retire explicitly owned DeskPort desktop runs.

No installed app, system service, Git checkout or Nix store is cleaned here.
Historical output directories are deliberately outside this tool's ownership.
"""
from __future__ import annotations

import argparse
import contextlib
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
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


def layout(target, run, dependency_cache=None):
    if not NAME.fullmatch(run) or run in {".", ".."}:
        raise Refused("Run ID must be a single alphanumeric/dot/dash/underscore name")
    env = {**os.environ, "DESKPORT_TARGET": target, "DESKPORT_RUN_ID": run}
    # Ignore inherited individual output overrides: a managed run must own every
    # output. Standalone platform scripts continue supporting those overrides.
    for key in ("DESKPORT_MACOS_BUILD_DIR", "DESKPORT_MACOS_DIST_DIR", "DESKPORT_HOST_SOURCE_DIR",
                "DESKPORT_HOST_BUILD_DIR", "DESKPORT_SOURCE", "DESKPORT_WORK", "DESKPORT_WINDOWS_SOURCE_ROOT",
                "DESKPORT_WINDOWS_BUILD_ROOT", "DESKPORT_WINDOWS_AUDIT_DIR", "DESKPORT_WINDOWS_INPUT_CACHE",
                "DESKPORT_WINDOWS_DEPENDENCY_BUILD", "DESKPORT_WINDOWS_CACHE_LOCK_HELD",
                "DESKPORT_WINDOWS_DEPENDENCY_ROOT", "DESKPORT_WINDOWS_DEPENDENCY_CACHE",
                "DESKPORT_LINUX_WORK", "DESKPORT_DEPENDENCY_CACHE_DIR", "PACKAGE_SUFFIX", "MATERIAL_SUFFIX"):
        env.pop(key, None)
    base = Path(check(["bash", ROOT / "scripts/build-paths.sh", "checkout"], env=env))
    no_links(base)
    if base == ROOT or ROOT in base.parents or str(base).startswith("/nix/store/"):
        raise Refused("Build outputs must be outside source and the Nix store")
    target_root = base / "runs.noindex" / run / target
    # Large library caches live independently of disposable application runs.
    cache = base / "dependencies.noindex" / target
    if dependency_cache is not None:
        cache = Path(dependency_cache).expanduser().absolute()
        no_links(cache)
        # Borrow an existing retained cache in place. Never adopt a run or move
        # CMake/qmake trees whose configured paths belong to another checkout.
        if (cache.name != target or cache.parent.name != "dependencies.noindex" or
                "runs.noindex" in cache.parts or not (cache / ".keep").is_file() or
                cache == ROOT or ROOT in cache.parents or str(cache).startswith("/nix/store/")):
            raise Refused("Dependency cache must be an existing retained dependencies.noindex/<target> directory")
    if target == "linux-portable":
        key = hashlib.sha256(b"deskport-linux-portable-dependencies-v1\0")
        recipes = [ROOT / "flake.lock", ROOT / "scripts/linux-tools.json", ROOT / "scripts/package-linux-container.sh"]
        recipes += list((ROOT / "scripts").glob("build-linux-*.sh"))
        recipes += list((ROOT / "scripts").glob("build-appimage-*.sh"))
        recipes += list((ROOT / "host/linux").glob("*.patch"))
        for recipe in sorted(set(recipes)):
            key.update(str(recipe.relative_to(ROOT)).encode() + b"\0" + recipe.read_bytes())
        cache /= key.hexdigest()[:24]
    paths = {k: target_root / k for k in ("work", "dist", "logs", "tests")}
    paths.update(base=base, target=target_root, cache=cache,
                 record=base / "records" / run / target)
    for value in paths.values():
        no_links(value)
    env.update(DESKPORT_DEPENDENCY_CACHE_DIR=str(cache), DESKPORT_NIX_DEPS="1",
               DESKPORT_WORK_DIR=str(paths["work"]), DESKPORT_DIST_DIR=str(paths["dist"]),
               DESKPORT_LOG_DIR=str(paths["logs"]), DESKPORT_TEST_DIR=str(paths["tests"]),
               DESKPORT_MACOS_BUILD_DIR=str(paths["work"]), DESKPORT_MACOS_DIST_DIR=str(paths["dist"]),
               DESKPORT_LINUX_WORK=str(paths["work"]), DESKPORT_WINDOWS_BUILD_ROOT=str(paths["work"]),
               DESKPORT_WINDOWS_SOURCE_ROOT=str(ROOT),
               PACKAGE_OUT=str(paths["dist"]), PACKAGE_STAGE=str(paths["work"] / "stage"),
               PYTHONDONTWRITEBYTECODE="1")
    return paths, env


@contextlib.contextmanager
def locked(paths, target):
    directory = paths["base"] / "locks"
    no_links(directory)
    directory.mkdir(parents=True, exist_ok=True)
    lock = directory / (target + ".lock")
    no_links(lock)
    with lock.open("a+") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise Refused(f"Another managed {target} operation owns the lock") from exc
        yield


def read_record(paths, args):
    path = paths["record"] / "run.json"
    try:
        record = json.loads(path.read_text())
    except FileNotFoundError as exc:
        raise Refused("No managed run record; legacy directories require a separate reviewed cleanup") from exc
    if (record.get("owner") != OWNERSHIP or record.get("schema") != SCHEMA or
            record.get("checkout") != str(ROOT) or record.get("run") != args.run or
            record.get("target") != args.target):
        raise Refused("Run ownership does not match this checkout/target")
    return record


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


def freeze(paths, record):
    record["trees"] = {key: footprint(paths[key]) for key in ("work", "dist", "logs", "tests")}
    record["finished_at"] = datetime.now(timezone.utc).isoformat()
    write_json(paths["record"] / "run.json", record)


def local_signing(env, filename=None):
    path = Path(os.path.abspath(Path(filename).expanduser())) if filename else Path.home() / ".config/deskport/build-signing.env"
    if path.exists():
        no_links(path)
        info = path.stat()
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise Refused("Signing config must be owned by you with mode 600")
        build_root = Path(env.get("DESKPORT_BUILD_ROOT", Path.home() / "mygit/build/deskport")).absolute()
        if path == ROOT or ROOT in path.parents or path == build_root or build_root in path.parents:
            raise Refused("Keep signing config outside source and generated output directories")
        for number, line in enumerate(path.read_text().splitlines(), 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, separator, value = line.removeprefix("export ").partition("=")
            if not separator or key not in SIGN_KEYS:
                raise Refused(f"Unsupported signing config entry on line {number}")
            values = shlex.split(value, comments=True)
            if len(values) != 1 or "$" in values[0] or "`" in values[0]:
                raise Refused(f"Signing config must contain literal values on line {number}")
            env.setdefault(key, values[0])
    for key in SIGN_KEYS:
        if key in env and ("REPLACE" in env[key] or "YOUR_" in env[key] or env[key] in {"DEVELOPER_ID_CERTIFICATE_SHA1", "APPLE_TEAM_ID"}):
            raise Refused(f"Replace the placeholder for {key} in your private configuration")


def privacy():
    subprocess.run([sys.executable, ROOT / "scripts/check-build-privacy.py"], cwd=ROOT, check=True)


def run_logged(command, paths, env, phase):
    paths["logs"].mkdir(parents=True, exist_ok=True)
    log = paths["logs"] / (phase + ".log")
    if log.exists():
        log = log.with_name(f"{phase}-{time.time_ns()}.log")
    print(f"{phase}: {log}", flush=True)
    # Do not log the environment or command arguments containing signing values.
    started = datetime.now(timezone.utc).isoformat()
    with log.open("wb") as stream:
        child = subprocess.Popen([str(x) for x in command], cwd=ROOT, env=env,
                                 stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while True:
                try:
                    code = child.wait(timeout=5)
                    break
                except subprocess.TimeoutExpired:
                    reserve = float(env.get("DESKPORT_MIN_FREE_GIB", "0")) * 2**30
                    if shutil.disk_usage(paths["base"]).free < reserve:
                        raise Refused("Build crossed the configured free-space reserve; stopping this command")
        except BaseException:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=15)
            except subprocess.TimeoutExpired:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(child.pid, signal.SIGKILL)
                child.wait()
            raise
    record_path = paths["record"] / "steps.json"
    steps = json.loads(record_path.read_text()) if record_path.exists() else []
    steps.append({"phase": phase, "exit_code": code, "started_at": started,
                  "finished_at": datetime.now(timezone.utc).isoformat(), "log_sha256": digest(log)})
    write_json(record_path, steps)
    if code:
        raise Refused(f"{phase} failed ({code}); inspect the private log above")


def host_check(target):
    if target == "macos":
        if platform.system() != "Darwin" or platform.machine() != "arm64":
            raise Refused("macOS packaging requires an Apple Silicon Mac")
    elif platform.system() != "Linux" or platform.machine() != "x86_64":
        raise Refused("Run Linux/Windows x86_64 builds on the Linux build host")


def tool_check(target, cleanup=False):
    required = ["git", "bash", "ps"]
    required += ["podman"] if target == "linux-portable" else ["nix-shell" if target == "windows" else "nix"]
    missing = [name for name in required if not shutil.which(name)]
    if cleanup and not (shutil.which("lsof") or Path("/usr/sbin/lsof").is_file()):
        missing.append("lsof (run cleanup inside nix develop on Linux)")
    if missing:
        raise Refused("Missing required tools: " + ", ".join(missing))


def capacity(paths, args):
    parent = paths["base"]
    while not parent.exists():
        parent = parent.parent
    reserve = args.min_free_gib if args.min_free_gib is not None else (80 if platform.system() == "Darwin" else 20)
    free = shutil.disk_usage(parent).free
    if free < reserve * 2**30:
        raise Refused(f"Free space {free / 2**30:.1f} GiB is below the {reserve:g} GiB build reserve")
    total = footprint(paths["base"] / "dependencies.noindex")["bytes"]
    if total > args.max_cache_gib * 2**30:
        raise Refused(f"Dependency caches exceed {args.max_cache_gib:g} GiB; inspect cache usage before building")
    pending = footprint(paths["base"] / "runs.noindex")["bytes"]
    if pending > args.max_work_gib * 2**30:
        raise Refused(f"Managed run outputs exceed {args.max_work_gib:g} GiB; clean completed runs before building")


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


def finish_package(args, paths, env, record):
    """Record exact artifacts only after all platform packaging steps succeeded."""
    state = record["source"]
    if git_state() != state:
        raise Refused("Source changed since this run started; start a fresh package run")
    version = (ROOT / "app/version.txt").read_text().strip()
    if args.target == "macos" and record["status"] == "notarized":
        evidence = paths["record"] / "notarization"
        evidence.mkdir(parents=True, exist_ok=True)
        for name in ("app-submit.json", "app-info.json", "dmg-submit.json", "dmg-info.json", "STATUS.txt"):
            source_file = paths["dist"] / "notarized" / name
            if source_file.is_file():
                shutil.copyfile(source_file, evidence / name)
    suffix = asset_suffix(record.get("asset_suffix") or None)
    metadata = paths["dist"] / suffixed_name(f"DeskPort-{version}-{args.target}-build.json", suffix)
    checksums = paths["dist"] / suffixed_name(f"SHA256SUMS-{args.target}.txt", suffix)
    for destination in (metadata, checksums):
        no_links(destination)
        if destination.exists() and asset_info(paths, destination) not in record.get("assets", []):
            raise Refused(f"Unrecorded or changed generated asset already exists: {destination.name}")
    assets = collect_assets(paths, args.target, record["status"])
    if args.target == "linux-portable":
        for p in sorted(paths["dist"].iterdir()):
            if p.name in {suffixed_name(name, value) for name in ("AppImage-runtime-sources.json", "build-packages.txt")
                          for value in ("", suffix)}:
                assets.append(asset_info(paths, p))
    assets = suffix_assets(paths, assets, suffix)
    # Public metadata contains no machine paths, hostnames, signing identities,
    # keychain profiles or raw logs.
    write_json(metadata, {"schema": SCHEMA, "version": version, "target": args.target,
                          "asset_suffix": suffix,
                          "source": state,
                          "source_url": f"https://github.com/keithxc/deskport/tree/{state['revision']}",
                          "assets": [{k: a[k] for k in ("name", "size", "sha256")} for a in assets]})
    assets.append(asset_info(paths, metadata))
    checksums.write_text("".join(f"{a['sha256']}  {a['name']}\n" for a in assets))
    assets.append(asset_info(paths, checksums))
    record.update(assets=assets, source_unchanged=True, failed=False)


def build(args, paths, env):
    suffix = asset_suffix(getattr(args, "asset_suffix", None))
    host_check(args.target)
    tool_check(args.target)
    if getattr(args, "flatpak", False) and not shutil.which("flatpak"):
        raise Refused("Flatpak packaging requires the flatpak command")
    capacity(paths, args)
    privacy()
    env["DESKPORT_MIN_FREE_GIB"] = str(args.min_free_gib if args.min_free_gib is not None else (80 if platform.system() == "Darwin" else 20))
    if paths["target"].exists() or (paths["record"] / "run.json").exists():
        raise Refused("Run ID already exists; use a new ID (notarize resumes a signed candidate)")
    state = git_state()
    if args.distribution and (args.target != "macos" or state["dirty"]):
        raise Refused("Distribution packaging requires macOS and a clean source checkout")
    if args.target == "macos":
        local_signing(env, args.signing_config)
    try:
        jobs = int(env.get("DESKPORT_JOBS", "4"))
        if jobs <= 0:
            raise ValueError
    except ValueError as exc:
        raise Refused("DESKPORT_JOBS must be a positive integer") from exc
    env["DESKPORT_JOBS"] = str(min(jobs, 4))
    paths["work"].mkdir(parents=True)
    scratch = paths["work"] / "tmp"
    scratch.mkdir()
    env["TMPDIR"] = str(scratch)
    paths["cache"].mkdir(parents=True, exist_ok=True)
    (paths["cache"] / ".keep").touch()
    record = dict(owner=OWNERSHIP, schema=SCHEMA, checkout=str(ROOT), run=args.run,
                  target=args.target, source=state, status="building", assets=[], asset_suffix=suffix,
                  created_at=datetime.now(timezone.utc).isoformat())
    write_json(paths["record"] / "run.json", record)
    try:
        if args.target == "macos":
            command = ["nix", "develop", "-c", "bash", "scripts/package-macos.sh"]
        elif args.target == "linux-nix":
            command = ["nix", "build", "-L", "--cores", "4", "--max-jobs", "1", "--out-link", paths["work"] / "result"]
        elif args.target == "linux-portable":
            env["DESKPORT_LINUX_FORMAT"] = args.format
            command = ["bash", "scripts/package-linux.sh"]
        else:
            mode = "build" if args.command == "build" else "package"
            command = ["nix-shell", "winbuild/scripts/shell.nix", "--run",
                       f"bash winbuild/scripts/bootstrap.sh --mode {mode}"]
        run_logged(command, paths, env, "build")
        if args.target == "linux-nix":
            # Unix-domain socket limits include every byte of TMPDIR. Keep
            # socket fixtures short while retaining them under the build root.
            short_base = paths["base"].parent / "t"
            no_links(short_base)
            short_base.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="dp-", dir=short_base) as temporary:
                smoke_env = {**env, "TMPDIR": temporary}
                run_logged([sys.executable, "scripts/deskport-smoke.py", paths["work"] / "result"], paths, smoke_env, "smoke")
        record["status"] = "built"
        if args.command == "package" and args.target != "linux-nix":
            if args.target == "linux-portable" and args.format == "all":
                version = (ROOT / "app/version.txt").read_text().strip()
                run_logged(["tar", "-C", paths["work"], "-czf",
                            paths["dist"] / f"DeskPort-{version}-linux-x86_64.tar.gz",
                            "DeskPort.AppDir"], paths, env, "portable-archive")
                if args.flatpak:
                    output = paths["work"] / "flatpak"
                    run_logged(["bash", "scripts/package-flatpak.sh",
                                paths["work"] / "DeskPort.AppDir", output, version],
                               paths, env, "flatpak")
                    bundle = output / f"DeskPort-{version}-client-x86_64.flatpak"
                    bundle.replace(paths["dist"] / bundle.name)
            if args.distribution:
                candidate = paths["dist"] / "notarized"
                run_logged(["nix", "develop", "-c", "bash", "scripts/release-macos.sh", "prepare",
                            paths["dist"] / "DeskPort.app", candidate], paths, env, "sign")
                record["status"] = "signed"
                write_json(paths["record"] / "run.json", record)
                run_logged(["nix", "develop", "-c", "bash", "scripts/release-macos.sh", "notarize", candidate], paths, env, "notarize")
                record["status"] = "notarized"
            finish_package(args, paths, env, record)
        record["source_unchanged"] = git_state() == state
        if not record["source_unchanged"]:
            raise Refused("Source changed during the build; do not publish this run")
    except BaseException:
        record["failed"] = True
        raise
    finally:
        freeze(paths, record)
    print(json.dumps({"run": args.run, "status": record["status"],
                      "dist": str(paths["dist"]), "record": str(paths["record"] / "run.json")}, indent=2))


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
    privacy()
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
        current = footprint(path)
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
    before = git_state()
    journal = []
    for item in plan:
        path = Path(item["path"])
        deletion_guard(path, paths, candidates)
        inactive(path)
        if footprint(path)["fingerprint"] != item["fingerprint"]:
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
        if git_state() != before:
            raise Refused("Source state changed during cleanup; remaining paths retained")
    record["cleaned"] = "work" if args.work_only else "all"
    write_json(paths["record"] / "run.json", record)
    # Only remove now-empty container directories; retained metadata stays separate.
    for path in (paths["target"], paths["target"].parent):
        try:
            path.rmdir()
        except OSError:
            pass


def main(argv=None):
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("paths", "doctor", "build", "package", "notarize", "publish", "verify-upload", "clean", "cache"))
    parser.add_argument("--target", choices=TARGETS, required=True)
    parser.add_argument("--run", help="Unique run ID; generated for build/package, required for existing runs")
    parser.add_argument("--distribution", action="store_true", help="macOS Developer ID and notarization")
    parser.add_argument("--asset-suffix", help="Optional download filename suffix, e.g. r2; inherited by notarize")
    parser.add_argument("--format", choices=("appimage", "all"), default="appimage")
    parser.add_argument("--flatpak", action="store_true", help="Include the client-only Flatpak with linux-portable --format all")
    parser.add_argument("--signing-config", help="Private literal env file outside the repository")
    parser.add_argument("--dependency-cache", help="Reuse an existing retained dependencies.noindex/<target> cache in place")
    parser.add_argument("--repo", help="Explicit GitHub OWNER/REPO (existing release only)")
    parser.add_argument("--tag", help="Existing GitHub release tag")
    parser.add_argument("--clean", action="store_true", help="After publish, verify again and clean this run")
    parser.add_argument("--download-verify", action="store_true", help="Explicitly download full assets instead of using GitHub SHA-256 digests")
    parser.add_argument("--work-only", action="store_true", help="Keep local packages; clear disposable intermediates")
    parser.add_argument("--apply", action="store_true", help="Apply the exact managed cleanup plan")
    parser.add_argument("--min-free-gib", type=float, help="Build reserve (default: macOS 80, Linux 20)")
    parser.add_argument("--max-cache-gib", type=float, default=40, help="Refuse new builds above this cache budget")
    parser.add_argument("--max-work-gib", type=float, default=30, help="Refuse new builds above this managed-output budget")
    args = parser.parse_args(argv)
    asset_suffix(args.asset_suffix)
    if args.asset_suffix is not None and args.command not in {"build", "package", "notarize"}:
        parser.error("--asset-suffix is only available with build, package or notarize")
    if any(value is not None and (not 0 < value < float("inf")) for value in (args.min_free_gib, args.max_cache_gib, args.max_work_gib)):
        parser.error("Storage limits must be positive finite numbers")
    if args.run is None:
        if args.command in {"build", "package"}:
            args.run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + check(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT)
        elif args.command in {"paths", "doctor", "cache"}:
            args.run = "dev"
        else:
            parser.error("--run is required")
    if args.command in {"publish", "verify-upload"} and not (args.repo and args.tag):
        parser.error("--repo and --tag are required")
    if args.command == "build" and args.distribution:
        parser.error("--distribution is only available with package")
    if args.flatpak and (args.command != "package" or args.target != "linux-portable" or args.format != "all"):
        parser.error("--flatpak requires package --target linux-portable --format all")
    paths, env = layout(args.target, args.run, args.dependency_cache)
    if args.command == "paths":
        print(json.dumps({k: str(v) for k, v in paths.items()}, indent=2))
        return 0
    if args.command == "doctor":
        host_check(args.target)
        tool_check(args.target, cleanup=True)
        capacity(paths, args)
        privacy()
        print("Host, storage budget and source privacy checks passed.")
        return 0
    if args.command == "cache":
        print(json.dumps({"path": str(paths["cache"]), **footprint(paths["cache"]),
                          "note": "Retained library inputs and compiled dependencies; legacy caches are not moved or deleted."}, indent=2))
        return 0
    with locked(paths, args.target):
        if args.command in {"build", "package"}:
            build(args, paths, env)
        else:
            record = read_record(paths, args)
            if args.command == "notarize":
                if args.target != "macos" or record["status"] not in {"signed", "notarized"}:
                    raise Refused("Only an existing signed macOS candidate can resume notarization")
                suffix = asset_suffix(record.get("asset_suffix") or None)
                if args.asset_suffix is not None and args.asset_suffix != suffix:
                    raise Refused("Notarization must retain the run's original asset suffix")
                host_check(args.target)
                local_signing(env, args.signing_config)
                try:
                    if record["status"] != "notarized":
                        run_logged(["nix", "develop", "-c", "bash", "scripts/release-macos.sh", "notarize",
                                    paths["dist"] / "notarized"], paths, env, "notarize")
                    else:
                        for item in record["assets"]:
                            if asset_info(paths, paths["dist"] / item["path"]) != item:
                                raise Refused(f"Local asset changed after packaging: {item['name']}")
                    record.update(status="notarized", failed=False)
                    finish_package(args, paths, env, record)
                except BaseException:
                    record["failed"] = True
                    raise
                finally:
                    freeze(paths, record)
            elif args.command == "publish":
                publish(args, paths, record)
                if args.clean:
                    args.apply = True
                    clean(args, paths, record)
            elif args.command == "verify-upload":
                verify_remote(args, paths, record)
            elif args.command == "clean":
                clean(args, paths, record)
    return 0


def interrupted(_signal, _frame):
    raise KeyboardInterrupt


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupted)
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("Interrupted; owned child stopped and run outputs retained for inspection.", file=sys.stderr)
        raise SystemExit(130)
    except (Refused, OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"Refused: {exc}", file=sys.stderr)
        raise SystemExit(2)
