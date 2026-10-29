"""Build, publish and retire explicitly owned DeskPort desktop runs.

No installed app, system service, Git checkout or Nix store is cleaned here.
Historical output directories are deliberately outside this tool's ownership.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
import signal
import subprocess
import sys

from . import common, package, runs
from .assets import asset_info, asset_suffix
from .cleanup import clean
from .common import Refused, TARGETS, check
from .package import build, finish_package
from .release import publish, verify_remote
from .runs import freeze, locked, read_record


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
            args.run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + check(["git", "rev-parse", "--short", "HEAD"], cwd=common.ROOT)
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
    paths, env = runs.layout(args.target, args.run, args.dependency_cache)
    if args.command == "paths":
        print(json.dumps({k: str(v) for k, v in paths.items()}, indent=2))
        return 0
    if args.command == "doctor":
        package.host_check(args.target)
        package.tool_check(args.target, cleanup=True)
        runs.capacity(paths, args)
        common.privacy()
        print("Host, storage budget and source privacy checks passed.")
        return 0
    if args.command == "cache":
        print(json.dumps({"path": str(paths["cache"]), **common.footprint(paths["cache"]),
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
                package.host_check(args.target)
                package.local_signing(env, args.signing_config)
                try:
                    if record["status"] != "notarized":
                        package.run_logged(["nix", "develop", "-c", "bash", "scripts/release-macos.sh", "notarize",
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


def run():
    """Command-line entry point with interrupt and refusal reporting."""
    signal.signal(signal.SIGTERM, interrupted)
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("Interrupted; owned child stopped and run outputs retained for inspection.", file=sys.stderr)
        raise SystemExit(130)
    except (Refused, OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"Refused: {exc}", file=sys.stderr)
        raise SystemExit(2)
