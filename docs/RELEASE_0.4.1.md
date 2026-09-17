# DeskPort 0.4.1

A supply-chain release. Every third-party dependency DeskPort builds against is
now vendored into this repository, so no build or release step fetches source
from another project's repository. Client and host behaviour is unchanged from
0.4.0.

## What changed

- Vendored `moonlight-common-c` (and its nested `enet`), `qmdnsengine`,
  `SDL_GameControllerDB`, `libsoundio`, `h264bitstream` and the macOS prebuilt
  dependency tree `libs/mac`. Each keeps its upstream licence file; upstream
  URLs and the exact commits are recorded in [VENDORED.md](VENDORED.md).
- Dropped `libs/windows` (252 MB). Every reference to it is inside a `win32:`
  project branch, and Windows is not part of the release matrix.
  [`libs/VENDORED.md`](../libs/VENDORED.md) records the commit to restore from.
- The Nix flake no longer refetches `moonlight-stream/moonlight-qt` with
  `fetchSubmodules` to recover gitlink contents, and filters `libs` out of the
  Linux build source.
- `scripts/generate-src.sh` builds the source tarball from `git archive` plus
  the shared core. `scripts/git-archive-all.sh` is removed: it required GNU tar
  and Bash 5, which neither macOS nor the project devShell provides.
- `shared/deskport-core` is still a submodule. It is this project's own code,
  shared with `deskport-client`; vendoring it into both would let them drift.

A fresh clone needs `git submodule update --init shared/deskport-core` and
nothing else.

## Downloads

macOS Apple Silicon (macOS 26+): Developer ID signed and Apple-notarized DMG and
ZIP. Linux: Nix and NixOS — `nix run github:keithxc/deskport/v0.4.1`.

This release deliberately does not rebuild the native Linux package matrix. The
change is to how sources are stored, not to the client, so the 0.4.0 DEB, RPM,
Arch, AppImage and client-only Flatpak remain the current builds for those
formats. Windows, Intel Mac, ARM Linux and mobile store packages are not part of
the established desktop release matrix.

## Known limitations

Carried forward from 0.4.0 and unresolved:

- TLS fallback does not cover a connection that is already mid-handshake when
  the path starts dropping larger TCP segments.
- `HostLifecycle::startupAndStopStayResponsive()` fails intermittently on
  GitHub's shared macOS runner (`'ticks >= 10' returned FALSE`). It passes
  locally on macOS and Linux; treat a single hosted-runner failure as CI timing
  sensitivity and re-run before calling it a blocker.
