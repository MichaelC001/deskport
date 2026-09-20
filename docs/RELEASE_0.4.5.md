# DeskPort 0.4.5

Stable desktop release incorporating the bounded macOS adaptive-resume probes
from 0.4.4. The user confirmed physical resize/resume, reconnect, keyboard/mouse
input and local display recovery before this release.

## Changes

- Skip generic display wake polling for managed macOS virtual displays.
- Reject unsupported AVFoundation-only capture formats immediately.
- Preserve native ScreenCaptureKit 8-bit H.264/HEVC capture; managed-display
  10-bit/HDR remains unavailable.
- Include the desktop startup-window and shared product-catalog improvements.

## Downloads

macOS Apple Silicon (macOS 26+): Developer ID signed and Apple-notarized DMG/ZIP.
Linux x86_64: DEB, RPM, Arch, AppImage, portable archive, client-only Flatpak,
and the pinned Nix flake. Native portable binaries require glibc 2.39 or newer.
See the Linux installation guide for distribution and host-permission limits.
Windows and mobile binaries are not part of this release.

See the accompanying verification report and SHA-256 checksums for package checks.
