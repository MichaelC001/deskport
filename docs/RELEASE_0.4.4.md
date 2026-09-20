# DeskPort 0.4.4 prerelease

A validation candidate for macOS Apple Silicon and NixOS x86_64 following the
withdrawn 0.4.3 release. This is not a stable release.

## Changes

- Skip generic display wake polling for managed macOS virtual displays.
- Reject unsupported AVFoundation-only capture formats immediately so optional
  encoder probing cannot consume the adaptive-resume request deadline.
- Keep native ScreenCaptureKit 8-bit H.264/HEVC capture. Managed virtual-display
  10-bit/HDR is unavailable pending a native capture implementation and testing.

## Packages and validation

Only signed/notarized macOS arm64 DMG/ZIP and the NixOS x86_64 flake target are
provided. No Windows, mobile, DEB, RPM, AppImage or Flatpak release is included.

The capture regression fails against the old source and passes with this fix.
Capture lifecycle, session topology and shared-core checks passed. Build,
signature and notarization evidence is recorded with the release assets.

Real video, repeated resize/resume, reconnect, keyboard/mouse input and local
display recovery remain manual acceptance gates. Packaging does not prove them.
