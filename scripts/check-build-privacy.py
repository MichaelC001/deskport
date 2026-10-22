#!/usr/bin/env python3
"""Reject private build/signing material in source trees without printing values.

By default, inspect tracked working-tree files, including initialized submodules.
Use --source-dir for unpacked source trees or --archive for source tar/zip files.
Archive members are streamed without extraction. Nested dependency archives and
binary signatures are not decoded; private key markers are checked in all bytes.
Exit status: 0 clean, 1 findings, 2 incomplete scan. A clean scan is silent.
"""
from __future__ import annotations

import argparse
import codecs
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tarfile
import zipfile


PRIVATE_MARKER = re.compile(r"-----BEGIN (?:[A-Z0-9]+ )?PRIVATE KEY-----")
IDENTITY = re.compile(
    r"(?:Developer ID Application|Apple Development|Apple Distribution|"
    r"iPhone Developer|iPhone Distribution|Mac Developer|"
    r"3rd Party Mac Developer Application):[^\r\n]{1,200}\([A-Z0-9]{10}\)"
)
SETTING_NAMES = (
    "DESKPORT_SIGN_IDENTITY", "DESKPORT_SIGN_TEAM", "DESKPORT_APPLE_TEAM",
    "DESKPORT_SDK_SIGN_IDENTITY", "CODE_SIGN_IDENTITY", "DEVELOPMENT_TEAM",
    "DevelopmentTeam", "TeamIdentifier", "APPLE_TEAM_ID", "DESKPORT_APPLE_ID",
    "DESKPORT_NOTARY_APPLE_ID", "DESKPORT_NOTARY_PASSWORD", "APPLE_ID_PASSWORD",
    "APP_SPECIFIC_PASSWORD", "ASC_KEY_ID", "ASC_ISSUER_ID",
)
ASSIGNMENT = re.compile(
    r"(?<![\w${])(?P<name>" + "|".join(SETTING_NAMES) + r")"
    r"(?:[\"']\s*\])?[\"']?\s*[:=]\s*"
    r"(?P<value>\"(?:\\.|[^\"\r\n])*\"|'(?:\\.|[^'\r\n])*'|[^\s,;#}\r\n]+)"
)
TEAM_OPTION = re.compile(r"--team-id(?:=|\s+)([\"']?)([A-Z0-9]{10})\1(?=\s|$|[;,])")
SIGN_OPTION = re.compile(r"--(?:sign|identity)(?:=|\s+)([\"']?)([a-fA-F0-9]{40})\1(?=\s|$|[;,])")
PRIVATE_SUFFIXES = {".key", ".p12", ".pfx", ".p8", ".keychain", ".keychain-db",
                    ".mobileprovision", ".provisionprofile", ".jks", ".keystore"}
TEMPLATE_SUFFIXES = (".example", ".sample", ".template")
NOTARY_FILES = {f"{kind}-{action}.json" for kind in ("app", "dmg")
                for action in ("submit", "info", "log")}
PLACEHOLDERS = {
    "", "-", "DEVELOPER_ID_CERTIFICATE_SHA1", "APPLE_TEAM_ID", "APPLE_ACCOUNT_ID",
    "APP_SPECIFIC_PASSWORD", "ASC_KEY_ID", "ASC_ISSUER_ID", "SIGNING_IDENTITY",
    "CERTIFICATE_SHA1", "Apple Development", "Apple Distribution", "iPhone Developer",
    "iPhone Distribution", "Developer ID Application", "Mac Developer",
    "your local signing identity",
}
# A pre-existing redaction test contains a marker string, not a key. The exact
# line hash prevents a broad tests/ or vendor/ exception from hiding new material.
MARKER_FIXTURES = {
    "tests/diagnostics-test.cpp": {
        "d4e5c296d35d28f097915132ff4e83b820a892525acae822684cb86aa933bd0d",
    },
}
# These are public Sunshine test fixtures from the exact pinned vendor archive,
# not build credentials. A complete file digest is required as well as the
# marker line digest: keeping a BEGIN line while replacing its PEM body must
# never inherit the exception. Source exports retain these upstream tests.
UPSTREAM_FIXTURE_ARCHIVE_SHA256 = "63d7b9c71e0ed759b85ddfe1d8e65520a5c36454703ec524c5d831c4e2ef204d"
UPSTREAM_MARKER_FIXTURES = {
    "tests/unit/test_confighttp.cpp": {
        "sha256": "c45b400ed5e768d53e7c27892a8a27723a0880d8aa4f5124ecf2ff150e069097",
        "markers": {41: "8d1f44ce76a0f398bf552776ec97b2e20677e1de149ab65b86a1d07dbf463ed3"},
    },
    "tests/unit/test_http_pairing.cpp": {
        "sha256": "6b508ed6eb7cc9d9e684f18fc60c5ca63d862ca25d109a828a618a3b47b322cd",
        "markers": {50: "c36bb131ee0364bdd6786a3d87e92d64487eac9e0c8a4f215836636e543a7674"},
    },
}


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    reason: str


def safe_path(path: str) -> str:
    """Keep control characters in a filename from injecting output lines."""
    return json.dumps(path, ensure_ascii=True) if any(ord(c) < 32 for c in path) else path


def emit(findings: list[Finding]) -> None:
    for finding in sorted(set(findings)):
        print(f"{safe_path(finding.path)}:{finding.line}: {finding.reason}")


def private_filename(path: Path) -> str | None:
    name = path.name.lower()
    if path.suffix.lower() in PRIVATE_SUFFIXES:
        return "private signing container or provisioning file"
    if name.endswith(TEMPLATE_SUFFIXES):
        return None  # Templates still receive all content checks.
    if (name == ".env" or name.startswith(".env.")
            or ("signing" in name and (".env" in name or ".local" in name))):
        return "local signing or environment configuration"
    if name in NOTARY_FILES or re.fullmatch(r"notary[^/]*\.(?:json|log|txt)", name):
        return "private notarization response or log"
    return None


def placeholder_or_reference(raw: str) -> bool:
    value = raw.strip().strip("\"'")
    if value in PLACEHOLDERS:
        return True
    if re.fullmatch(r"(?:YOUR|EXAMPLE|REPLACE_WITH|PLACEHOLDER)_[A-Z0-9_]+", value):
        return True
    if re.fullmatch(r"<[A-Za-z_ -]+>", value):
        return True
    if value.startswith("$"):
        # A literal fallback still discloses material even in a variable expression.
        fallback = re.search(r":[-=](.+?)(?:}|$)", value)
        return fallback is None or placeholder_or_reference(fallback.group(1))
    return False


def scan_text(text: str, relative: str, label: str) -> list[Finding]:
    findings = []
    for number, line in enumerate(text.splitlines(), 1):
        if PRIVATE_MARKER.search(line):
            line_hash = hashlib.sha256(line.encode("utf-8")).hexdigest()
            allowed = set().union(*(hashes for name, hashes in MARKER_FIXTURES.items()
                                    if relative == name or relative.endswith("/" + name)))
            if line_hash not in allowed:
                findings.append(Finding(label, number, "private key marker"))
        if IDENTITY.search(line):
            findings.append(Finding(label, number, "personal signing identity literal"))
        for match in ASSIGNMENT.finditer(line):
            if not placeholder_or_reference(match.group("value")):
                findings.append(Finding(label, number, "literal signing or account setting"))
        if TEAM_OPTION.search(line):
            findings.append(Finding(label, number, "literal Apple team command argument"))
        if SIGN_OPTION.search(line):
            findings.append(Finding(label, number, "literal signing fingerprint command argument"))
    return findings


def scan_file(root: Path, relative: str, label: str) -> tuple[list[Finding], bool]:
    path = root / relative
    findings = []
    reason = private_filename(path)
    if reason:
        findings.append(Finding(label, 1, reason))
    try:
        if path.is_symlink():
            # Source exports may preserve internal vendored symlinks. Never read a
            # target outside the chosen tree, including a private local env file.
            try:
                path.resolve(strict=True).relative_to(root.resolve())
            except (OSError, RuntimeError, ValueError):
                findings.append(Finding(label, 1, "external or unresolved source symlink"))
            return findings, False
        if not path.is_file():
            return findings + [Finding(label, 1, "source file is missing or not regular")], True
        with path.open("rb") as stream:
            findings.extend(scan_stream(stream, relative, label))
    except OSError:
        return findings + [Finding(label, 1, "source file could not be read")], True
    return findings, False


def tracked_files(root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "--cached", "--recurse-submodules", "-z"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True,
    )
    return [os.fsdecode(path) for path in result.stdout.split(b"\0") if path]


def tree_files(root: Path) -> list[str]:
    paths = []
    def fail(_error: OSError) -> None:
        raise OSError("source directory could not be read")
    for directory, dirs, files in os.walk(root, followlinks=False, onerror=fail):
        for name in list(dirs):
            path = Path(directory) / name
            if name == ".git":
                dirs.remove(name)
            elif path.is_symlink():
                paths.append(path.relative_to(root).as_posix())
                dirs.remove(name)
        for name in files:
            if name != ".git":
                paths.append((Path(directory) / name).relative_to(root).as_posix())
    return paths


def scan_stream(stream, relative: str, label: str) -> list[Finding]:
    """Scan archive content with bounded buffers, including long/binary lines."""
    findings = []
    block = stream.read(65536)
    encoding = "utf-16" if block.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
    decoder = codecs.getincrementaldecoder(encoding)(errors="strict")
    binary = encoding != "utf-16" and b"\0" in block
    pending = ""
    byte_tail = b""
    line_number = 1
    content_hash = hashlib.sha256()
    marker_lines = {}

    def text_findings(line: str, number: int) -> None:
        findings.extend(Finding(item.path, number + item.line - 1, item.reason)
                        for item in scan_text(line, relative, label))
        if PRIVATE_MARKER.search(line):
            marker_lines[number] = hashlib.sha256(line.rstrip("\r").encode("utf-8")).hexdigest()

    while block:
        content_hash.update(block)
        if not binary:
            try:
                pending += decoder.decode(block, final=False)
            except UnicodeError:
                binary = True
                for match in PRIVATE_MARKER.finditer(pending):
                    findings.append(Finding(label, line_number, "private key marker"))
                byte_tail = pending[-256:].encode("utf-8")
        if binary:
            combined = byte_tail + block
            for match in re.finditer(PRIVATE_MARKER.pattern.encode("ascii"), combined):
                findings.append(Finding(label, line_number + combined.count(b"\n", 0, match.start()),
                                        "private key marker"))
            cut = max(0, len(combined) - 256)
            line_number += combined[:cut].count(b"\n")
            byte_tail = combined[cut:]
        else:
            lines = pending.split("\n")
            pending = lines.pop()
            for line in lines:
                text_findings(line, line_number)
                line_number += 1
            if len(pending) > 262144:
                text_findings(pending, line_number)
                pending = pending[-4096:]
        block = stream.read(65536)
    if not binary:
        try:
            pending += decoder.decode(b"", final=True)
        except UnicodeError:
            pass
        text_findings(pending, line_number)
    for name, fixture in UPSTREAM_MARKER_FIXTURES.items():
        if ((relative == name or relative.endswith("/" + name))
                and content_hash.hexdigest() == fixture["sha256"]):
            findings = [finding for finding in findings
                        if not (finding.reason == "private key marker"
                                and finding.line in fixture["markers"]
                                and marker_lines.get(finding.line) == fixture["markers"][finding.line])]
    return findings


def archive_path(name: str) -> str | None:
    if not name or "\\" in name or name.startswith("/") or re.match(r"^[A-Za-z]:", name):
        return None
    parts = PurePosixPath(name).parts
    if ".." in parts:
        return None
    return "/".join(parts) or "."


def link_path(name: str, target: str, hardlink: bool = False) -> str | None:
    if not target or "\\" in target or target.startswith("/") or re.match(r"^[A-Za-z]:", target):
        return None
    parts = [] if hardlink else list(PurePosixPath(name).parent.parts)
    for part in PurePosixPath(target).parts:
        if part == "..":
            if not parts:
                return None
            parts.pop()
        elif part != ".":
            parts.append(part)
    return "/".join(parts) or "."


def scan_archive(path: Path) -> tuple[list[Finding], bool]:
    findings = []
    links = {}
    members = set()

    def inventory(name: str) -> tuple[str | None, str]:
        label = f"{path}!{name}"
        normalized = archive_path(name)
        if normalized is None:
            findings.append(Finding(label, 1, "unsafe archive member path"))
        elif normalized in members:
            findings.append(Finding(label, 1, "duplicate archive member path"))
        else:
            members.add(normalized)
        reason = private_filename(PurePosixPath(name))
        if reason:
            findings.append(Finding(label, 1, reason))
        return normalized, label

    def link(name: str, target: str, label: str, hardlink: bool = False) -> None:
        destination = link_path(name, target, hardlink)
        if destination is None:
            findings.append(Finding(label, 1, "archive link escapes source root"))
        else:
            links[name] = (destination, label)

    try:
        if zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as archive:
                for member in archive.infolist():
                    name, label = inventory(member.filename)
                    if name is None or member.is_dir():
                        continue
                    if stat.S_ISLNK(member.external_attr >> 16):
                        with archive.open(member) as stream:
                            target = stream.read(4097)
                        if len(target) > 4096:
                            findings.append(Finding(label, 1, "oversized archive link target"))
                        else:
                            link(name, target.decode("utf-8"), label)
                    else:
                        with archive.open(member) as stream:
                            findings.extend(scan_stream(stream, name, label))
        else:
            with tarfile.open(path, mode="r|*") as archive:
                for member in archive:
                    name, label = inventory(member.name)
                    if name is None or member.isdir():
                        continue
                    if member.issym() or member.islnk():
                        link(name, member.linkname, label, member.islnk())
                    elif member.isfile():
                        with archive.extractfile(member) as stream:
                            findings.extend(scan_stream(stream, name, label))
                    else:
                        findings.append(Finding(label, 1, "unsupported archive member type"))
        for name, (destination, label) in links.items():
            seen = {name}
            while True:
                parts = PurePosixPath(destination).parts
                prefix = next(("/".join(parts[:length]) for length in range(1, len(parts) + 1)
                               if "/".join(parts[:length]) in links), None)
                if prefix is None:
                    break
                if prefix in seen:
                    findings.append(Finding(label, 1, "cyclic archive link"))
                    break
                seen.add(prefix)
                suffix = destination[len(prefix):].lstrip("/")
                destination = links[prefix][0] + ("/" + suffix if suffix else "")
            # Links may target an implicit directory omitted from a zip listing.
            if destination not in members and not any(item.startswith(destination + "/") for item in members):
                findings.append(Finding(label, 1, "unresolved archive link"))
    except (OSError, UnicodeError, tarfile.TarError, zipfile.BadZipFile, RuntimeError, EOFError):
        findings.append(Finding(str(path), 1, "archive could not be completely scanned"))
        return findings, True
    return findings, False


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1],
                        help="Git checkout to scan (tracked working-tree files)")
    parser.add_argument("--source-dir", type=Path, action="append", default=[],
                        help="Scan an explicit unpacked source directory instead; repeatable")
    parser.add_argument("--archive", type=Path, action="append", default=[],
                        help="Stream a source tar/zip archive without extraction; repeatable")
    args = parser.parse_args(argv)
    findings = []
    incomplete = False
    for source in args.source_dir or ([] if args.archive else [args.repo]):
        root = source.resolve()
        label_root = str(source) if args.source_dir else "."
        try:
            if not root.is_dir():
                raise OSError("source directory does not exist")
            paths = tree_files(root) if args.source_dir else tracked_files(root)
        except (OSError, subprocess.SubprocessError):
            findings.append(Finding(label_root, 1, "source inventory could not be read"))
            incomplete = True
            continue
        for relative in sorted(set(paths)):
            label = str(source / relative) if args.source_dir else relative
            matches, failed = scan_file(root, relative, label)
            findings.extend(matches)
            incomplete |= failed
    for archive in args.archive:
        matches, failed = scan_archive(archive)
        findings.extend(matches)
        incomplete |= failed
    emit(findings)
    return 2 if incomplete else 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
