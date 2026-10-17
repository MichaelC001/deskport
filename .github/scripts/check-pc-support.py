#!/usr/bin/env python3
"""Validate PC evidence provenance and render its reviewable documentation.

Standard library only; no network, application, VM or compositor access.
"""
import argparse
import copy
from datetime import date
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
START = "<!-- pc-support-evidence:start -->"
END = "<!-- pc-support-evidence:end -->"
KINDS = {"physical-hardware", "isolated-compositor", "isolated-fixture",
         "virtual-machine", "hosted-ci"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{40}", value)


def slug(heading):
    return re.sub(r"[^\w -]", "", heading.lower()).replace(" ", "-")


def local_reference(reference, root):
    if reference.startswith("https://"):
        require(bool(re.fullmatch(r"https://github\.com/keithxc/deskport/actions/runs/\d+", reference)),
                f"Evidence URL must identify an exact CI run: {reference}")
        return
    path, _, anchor = reference.partition("#")
    target = (root / path).resolve()
    require(target.is_relative_to(root.resolve()) and target.is_file(),
            f"Missing or external local evidence: {reference}")
    if anchor:
        headings = re.findall(r"^#{1,6} (.+)$", target.read_text(), re.M)
        require(anchor in {slug(heading) for heading in headings},
                f"Missing evidence heading: {reference}")


def workflow_inventory(root):
    """Read the workflows' current static job/runner subset; fail on ambiguity."""
    result = []
    paths = sorted(set((root / ".github/workflows").glob("*.yml")) |
                   set((root / ".github/workflows").glob("*.yaml")))
    for path in paths:
        content = path.read_text()
        require("\njobs:\n" in content, f"Cannot inventory jobs: {path.name}")
        jobs = content.split("\njobs:\n", 1)[1]
        matches = list(re.finditer(r"^  ([\w-]+):\s*$", jobs, re.M))
        require(bool(matches), f"No static jobs: {path.name}")
        for index, match in enumerate(matches):
            block = jobs[match.end():matches[index + 1].start() if index + 1 < len(matches) else len(jobs)]
            runners = re.findall(r"^    runs-on: ([\w.-]+)\s*$", block, re.M)
            require(len(runners) == 1, f"Review dynamic/missing runner: {path.name}/{match[1]}")
            result.append({"path": path.relative_to(root).as_posix(),
                           "job": match[1], "runner": runners[0]})
    return sorted(result, key=lambda row: (row["path"], row["job"]))


def validate(data, root):
    require(data.get("schema_version") == 1, "Unsupported evidence schema")
    review = date.fromisoformat(data["review_date"])
    for field in ("baseline_source_sha", "baseline_core_sha"):
        require(sha(data[field]), f"Full SHA required: {field}")
    lock = json.loads((root / "flake.lock").read_text())
    require(data["baseline_core_sha"] == lock["nodes"]["deskport-core"]["locked"]["rev"],
            "Core pin changed: review the PC source/core snapshot")
    require(data["ci"] == workflow_inventory(root),
            "CI job/runner inventory changed: review scope before updating evidence")
    ids = set()
    require(bool(data["evidence"]), "Evidence register is empty")
    for row in data["evidence"]:
        ident = row["id"]
        require(bool(re.fullmatch(r"[a-z][a-z0-9-]+", ident)) and ident not in ids,
                f"Invalid/duplicate evidence ID: {ident}")
        ids.add(ident)
        require(date.fromisoformat(row["date"]) <= review, f"Evidence after review: {ident}")
        require(row["environment_kind"] in KINDS, f"Unknown environment kind: {ident}")
        if row["source_sha"] is None:
            require(bool(row["source_unknown_reason"].strip()), f"Explain unknown SHA: {ident}")
        else:
            require(sha(row["source_sha"]), f"Full tested SHA required: {ident}")
            require(row["source_unknown_reason"] == "", f"Conflicting SHA provenance: {ident}")
        if row["package_sha256"] is None:
            require(bool(row["package_hash_unknown_reason"].strip()),
                    f"Explain unknown package hash: {ident}")
        else:
            require(bool(re.fullmatch(r"[0-9a-f]{64}", row["package_sha256"])),
                    f"Full package SHA-256 required: {ident}")
            require(row["package_hash_unknown_reason"] == "",
                    f"Conflicting package provenance: {ident}")
        for field in ("package", "installed", "environment", "features", "result", "limits"):
            require(isinstance(row[field], str) and bool(row[field].strip()),
                    f"Missing {field}: {ident}")
            require("\n" not in row[field], f"Use one line for {ident}/{field}")
        require(bool(row["references"]), f"Missing evidence source: {ident}")
        for reference in row["references"]:
            local_reference(reference, root)
        if row["environment_kind"] == "hosted-ci":
            require(sha(row["source_sha"]), f"Exact tested CI source SHA required: {ident}")
            require(any(ref.startswith("https://") for ref in row["references"]),
                    f"Exact CI run URL required: {ident}")


def render(data):
    lines = [START, "", "Generated from the reviewed register; recorded results are scoped to each row.", ""]
    for row in data["evidence"]:
        lines += [f"### Evidence {row['id']}", "", "| Field | Record |", "| --- | --- |"]
        source = f"`{row['source_sha']}`" if row["source_sha"] else "Unknown: " + row["source_unknown_reason"]
        package_hash = (f"`{row['package_sha256']}`" if row["package_sha256"] else
                        "Unknown: " + row["package_hash_unknown_reason"])
        fields = [("Date / tested source", row["date"] + "; " + source),
                  ("Package / installed identity", row["package"] + ". " + row["installed"]),
                  ("Package SHA-256", package_hash),
                  ("Environment", row["environment_kind"] + ": " + row["environment"]),
                  ("Feature / actual result", row["features"] + ". " + row["result"]),
                  ("Limits / pending", row["limits"])]
        for name, value in fields:
            lines.append(f"| {name} | {value.replace('|', '&#124;')} |")
        refs = []
        for index, ref in enumerate(row["references"], 1):
            target = ref if ref.startswith("https://") else "../" + ref
            refs.append(f"[source {index}]({target})")
        lines += ["| References | " + " · ".join(refs) + " |", ""]
    return "\n".join(lines) + END


def check(root, write=False):
    data = json.loads((root / "docs/pc-support-evidence.json").read_text())
    validate(data, root)
    page = root / "docs/PC_SUPPORT_MATRIX.md"
    content = page.read_text()
    require(content.count(START) == content.count(END) == 1, "Evidence markers must occur once")
    before, after_start = content.split(START)
    current, after = after_start.split(END)
    expected = render(data)
    if write:
        page.write_text(before + expected + after)
    else:
        require(START + current + END == expected,
                "Evidence table is stale: run python3 .github/scripts/check-pc-support.py --write")
    for path in ("README.md", "docs/ROADMAP.md", "docs/PEER_BINDING.md",
                 "docs/LINUX_ADAPTIVE_DISPLAY.md", "docs/INPUT_SERVICE_ACCEPTANCE.md",
                 "docs/WINDOWS_DEVELOPMENT.md", "docs/BROWSER_ACCESS.md",
                 "docs/CLIPBOARD.md", "docs/SESSION_LIFECYCLE.md", "docs/ADAPTIVE_DISPLAY.md"):
        require("PC_SUPPORT_MATRIX.md" in (root / path).read_text(), f"Missing current scope link: {path}")
    for ident in re.findall(r"\]\(#evidence-([a-z0-9-]+)\)", before + after):
        require(ident in {row["id"] for row in data["evidence"]}, f"Unknown evidence link: {ident}")
    binding = (root / "docs/PEER_BINDING.md").read_text()
    for stale in ("Local host sharing remains unsupported on Windows.",
                  "a private Linux virtual display is not implemented."):
        require(stale not in binding, f"Superseded current binding claim: {stale}")
    linux = (root / "docs/LINUX_ADAPTIVE_DISPLAY.md").read_text()
    require("**Hyprland / Omarchy:**" in linux.split("## Validation")[0],
            "Linux backend inventory must include the implemented Hyprland adapter")
    return data


def self_test(data, root):
    """Ensure actual provenance/scope omissions are rejected, not just formatting."""
    def record(value, ident):
        return next(row for row in value["evidence"] if row["id"] == ident)

    mutations = [
        lambda value: record(value, "windows-dpi").update(source_sha="b0c28182"),
        lambda value: record(value, "windows-old-lock-video").update(source_unknown_reason=""),
        lambda value: record(value, "windows-dpi").update(installed=""),
        lambda value: record(value, "windows-dpi").update(limits=""),
        lambda value: record(value, "windows-dpi").update(package_sha256="abc"),
        lambda value: record(value, "windows-broker").update(package_hash_unknown_reason=""),
        lambda value: record(value, "windows-dpi").update(environment_kind="native-ci"),
        lambda value: record(value, "windows-dpi").update(references=["docs/CLIPBOARD.md#missing"]),
        lambda value: value["evidence"].append(value["evidence"][0]),
        lambda value: value["ci"][0].update(runner="windows-latest"),
        lambda value: value.update(baseline_core_sha="0" * 40),
        lambda value: record(value, "ci-windows").update(source_sha=None, source_unknown_reason="Not recorded"),
    ]
    for index, mutation in enumerate(mutations):
        invalid = copy.deepcopy(data)
        mutation(invalid)
        try:
            validate(invalid, root)
        except ValueError:
            continue
        raise ValueError(f"Invalid provenance mutation {index} was accepted")
    print(f"Rejected {len(mutations)} invalid provenance/scope fixtures")
    advanced = copy.deepcopy(data)
    advanced["baseline_source_sha"] = record(data, "windows-dpi")["source_sha"]
    validate(advanced, root)
    print("Retained historical CI SHAs when the implementation snapshot changes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="regenerate the evidence section")
    parser.add_argument("--self-test", action="store_true", help="exercise invalid evidence fixtures")
    args = parser.parse_args()
    try:
        data = check(ROOT, args.write)
        if args.self_test:
            self_test(data, ROOT)
    except (ValueError, KeyError, TypeError) as error:
        print(f"PC support evidence: {error}", file=sys.stderr)
        return 1
    print(f"PC support evidence: {len(data['evidence'])} records and {len(data['ci'])} CI jobs checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
