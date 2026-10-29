# DeskPort 0.7.2-rc.1

Pre-release for macOS Apple Silicon, Linux x86_64 and Windows x64. 0.7.1 remains
the stable release until device acceptance of this candidate is complete.

## Changes since 0.7.1

- **Faster adaptive resize.** When the client window settles at a new size, the
  stream now reconnects about one second sooner. The reconnect no longer waits a
  second quiet interval for a size that already settled, and gamepad subsystems
  stay initialized instead of enumerating devices again. Measured from pk4
  (NixOS, KDE Wayland) to a macOS host over 30 resizes per build: the median from
  stopping the old stream to the first new frame fell from 2626 ms to 1646 ms.
- **Reconnect backdrop.** During a resize or network reconnect, the window shows
  a blurred, dimmed picture of the previous stream behind the progress indicator
  instead of a flat background. First connections and HDR streams keep the plain
  background. On Windows the transition is now drawn with Direct3D 11; previously
  the last stream frame stayed frozen until the new stream started.
- **Smoother drags on macOS hosts.** Remote window and selection drags post
  pointer motion without warping the cursor for every event.
- **Tray and icons.** The tray can hide a window that is still connecting. Tray
  icons derive from the application artwork, with native platform appearances.
- **Diagnostics.** Client diagnostics record whether each transition picture was
  captured, skipped for HDR, or failed with its error code.
- **Reproducible Windows build.** Every third-party Windows build input is pinned
  in `winbuild/inputs.json` with upstream URLs and SHA-256, mirrored in the
  [`windows-build-inputs-1`](https://github.com/keithxc/deskport/releases/tag/windows-build-inputs-1)
  build-input release. `winbuild/scripts/bootstrap.sh` builds the installer from a
  clean checkout; see [Windows development](../WINDOWS_DEVELOPMENT.md).

## Downloads and scope

The same formats as 0.7.1: macOS arm64 (macOS 26+) DMG and ZIP; Linux DEB, RPM,
Arch, AppImage and portable archive (glibc 2.39+; the AppImage includes a private
glibc runtime), Nix; Flatpak (viewer only); Windows x64 Setup and portable ZIP.
Exact sources, third-party notices, relinking materials, SHA-256 checksums and
`VERIFICATION.txt` accompany the release.

Resize timing and the backdrop were checked on pk4 → macOS and on a Windows 11
client → macOS (14 resizes, maximize/restore and stepped drags, sampled about
every 20 ms with no black or flat frame). Native modal border dragging on
Windows, other GPUs and long sessions are not yet accepted. Mobile clients are
distributed separately.

## Published artifacts

The [prerelease](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.1) is
not marked Latest; 0.7.1 remains the stable release. Tag `v0.7.2-rc.1` resolves to
desktop source `617f03045dfe336e21f6aa995456e9cfaaea5d10`, with shared core
`108ca7d24c023b443687971690b1066427012608`. The binaries were built from
`eb130d2c`; `617f0304` only fixes the Windows material exporter.

All 19 public assets were downloaded without login and matched their local bytes,
GitHub sizes/digests and
[SHA256SUMS.txt](https://github.com/keithxc/deskport/releases/download/v0.7.2-rc.1/SHA256SUMS.txt).
[VERIFICATION.txt](https://github.com/keithxc/deskport/releases/download/v0.7.2-rc.1/VERIFICATION.txt)
records notarization, native Windows checks, distribution installation tests, Nix
and Flatpak validation, and the scope of live checks. Managed activation remains
separate.
