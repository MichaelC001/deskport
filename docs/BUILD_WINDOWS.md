# Windows build workflow

Use the shared [build workflow](BUILD_WORKFLOW.md) for Windows x64 builds and
packages. The maintained backend cross-compiles on **x86_64 Linux** inside the
Nix environment locked by `flake.lock`. Windows users receive an offline Setup
installer or portable ZIP; they do not need the build tools.

The wrapper keeps Qt/qmake, MinGW, CMake/Ninja, Meson, NSIS and the existing
package audits. It does not install the result, start a Windows VM, change a
service, publish a release or modify an installed application during a build.
Native installation, streaming, input and display acceptance remain separate;
see [Windows development and acceptance](WINDOWS_DEVELOPMENT.md).

## Common commands

Run these commands from the desktop repository on the Linux build host:

```sh
# Inspect paths and check host, storage and source requirements.
bash scripts/build.sh paths --target windows --run candidate-1
bash scripts/build.sh doctor --target windows

# Compile the client, host and helper executables.
bash scripts/build.sh build --target windows --run compile-1

# Compile, create Setup/portable packages, audit their payloads,
# and record checksums and source-commit metadata.
bash scripts/build.sh package --target windows --run candidate-1
```

Omit `--run` on `build` or `package` to generate an ID from the UTC timestamp and
source revision. Each managed build/package command requires a new run ID.
A `build` run is not promoted in place by a later `package` command: create a new
package run, which reuses the persistent dependency cache. Publication requires
a clean committed checkout; build metadata records the exact source revision
and its commit link. A dirty-source package is local only.

## Directory ownership

The default project root is `$HOME/mygit/build/deskport`; `DESKPORT_BUILD_ROOT`
can select another external output root. A checkout path checksum separates
worktrees. Query the exact paths with `scripts/build.sh paths`.

```text
<project-build-root>/<checkout>-<checksum>/
├── runs.noindex/<id>/windows/
│   ├── work/
│   │   ├── build-app/                 # Current client objects and executables
│   │   ├── full/sunshine-prepared/    # Current prepared host source
│   │   ├── full/host-build-prepared/  # Current host objects and executable
│   │   ├── full/deskport-*.exe        # Current helper executables
│   │   ├── stage/                     # Payload before installer/ZIP assembly
│   │   └── audit/                     # Extracted payload, uninstaller and audit reports
│   ├── dist/                          # Packages, checksums and build metadata
│   ├── logs/                          # Private build and phase logs
│   └── tests/                         # Managed test output, when produced
├── dependencies.noindex/windows/
│   ├── downloads/                     # Size/SHA-256-verified pinned archives
│   └── <recipe-key>/                  # Stable third-party sources and compiled libraries
└── records/<id>/windows/              # Run state, artifact hashes and cleanup receipts
```

Only third-party trees are shared: Qt, static library prefixes, dependency
sources/objects, toolchain notices and CPM header-source checkouts. They are
compiled at their permanent cache paths. Explicit links under `work/` preserve
the backend's existing path layout; `.deskport-windows-paths.json` records those
links and their cache identity. Client/host application objects and final
executables stay in the individual run.

The dependency key covers locked inputs, compiler environment recipes and
relevant build recipes. Changing those inputs selects a new cache. Dependency
writes and downloads are locked. Identical verified input placements are
retained, so a new application run does not unpack over completed media-library
builds. Existing historical caches are not moved or adopted automatically.

## Publication and cleanup

A successful managed Windows package run records the Setup installer, portable
ZIP, checksums and small public build metadata containing the source revision
and commit link. The release workflow does not automatically generate or upload
application source archives, full source materials or static-relink materials.
Audit extractions, raw logs and the local source-input index are not publication
assets. Publication uses an **existing, explicitly named release** and preserves
its existing assets:

```sh
bash scripts/build.sh publish --target windows --run candidate-1 \
  --repo OWNER/REPOSITORY --tag RELEASE_TAG

bash scripts/build.sh verify-upload --target windows --run candidate-1 \
  --repo OWNER/REPOSITORY --tag RELEASE_TAG

# Preview removal of objects/logs/tests while retaining local packages.
bash scripts/build.sh clean --target windows --run candidate-1 --work-only

# Apply that managed cleanup after reviewing its printed scope.
bash scripts/build.sh clean --target windows --run candidate-1 --work-only --apply
```

Full cleanup, without `--work-only`, requires a verified upload receipt and
rechecks GitHub's server SHA-256 digest and size before removing local packages.
No installer download occurs unless `--download-verify` is explicit. Cleanup preserves
persistent dependency caches and run records. It refuses changed output trees,
active files/processes, preservation markers, nested repositories and references
from other outputs. It does not clean historical directories or the Nix store.
A failed run retains its outputs and logs for inspection; use its recorded
managed cleanup route when finished with them.

## Backend steps and recovery

The backend remains available for focused development and diagnosis:

```sh
export DESKPORT_TARGET=windows
export DESKPORT_RUN_ID=manual-1

nix-shell winbuild/scripts/shell.nix --run \
  'bash winbuild/scripts/bootstrap.sh --mode package'

# Resume a failed backend phase in the same manual run.
nix-shell winbuild/scripts/shell.nix --run \
  'bash winbuild/scripts/bootstrap.sh --mode package --from host'

# Repeat one compilation phase without packaging.
nix-shell winbuild/scripts/shell.nix --run \
  'bash winbuild/scripts/bootstrap.sh --mode build --only app'
```

The phases are `inputs`, `deps`, `vulkan`, `qt`, `notices`, `host-deps`,
`host-source`, `host-media`, `app`, `maintenance`, `host`, `package` and `audit`.
`--mode build` stops after `host`; the default `--mode package` includes the
installer/ZIP and payload audit. The managed wrapper additionally records
source-commit metadata and artifact hashes. The standalone
`winbuild/scripts/export-materials.py` and `export-full-materials.py` scripts
remain available for explicitly requested manual material exports; they are
not part of the default build, package or release workflow.

Standalone backend calls default to the external `dev` run when
`DESKPORT_RUN_ID` is absent. They do not create the wrapper's managed-run receipt,
so use the shared wrapper when automatic publication/cleanup tracking is needed.
Do not modify a completed managed run with backend commands: changed trees will
fail the wrapper's cleanup fingerprint check. Package assembly rejects existing
stage/output directories; choose a new run after a partial package attempt.

For standalone use, `DESKPORT_WINDOWS_BUILD_ROOT`, `PACKAGE_STAGE`, `PACKAGE_OUT`
and `DESKPORT_WINDOWS_AUDIT_DIR` can select external work/stage/package/audit
paths. `DESKPORT_DEPENDENCY_CACHE_DIR` selects the persistent cache base and
`DESKPORT_WINDOWS_INPUT_CACHE` can select a shared verified-download location.
Keep both caches outside every disposable run. Managed commands enforce their
own paths instead of inheriting these individual output overrides.

The older `scripts/build-arch.bat` is an upstream native MSVC build route and is
not the maintained full Windows x64 client/host package entry point.
