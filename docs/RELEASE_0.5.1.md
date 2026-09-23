# DeskPort 0.5.1

Stable macOS and Linux desktop maintenance release.

## Changes

- Handle failed ENet host creation without dereferencing a null host during
  session startup; preserve the existing connection error path.
- Preserve the configured connection entry when refreshing authenticated peer
  endpoints, avoiding accidental replacement of the saved entry.

## Downloads and validation scope

macOS Apple Silicon (macOS 26+): Developer ID signed and Apple-notarized DMG/ZIP.
Linux x86_64: DEB, RPM, Arch, AppImage, portable archive, client-only Flatpak,
and the pinned Nix flake. Native portable binaries require glibc 2.39 or newer.
Windows and mobile binaries and private mobile sources are not included.

See the accompanying verification report and SHA-256 checksums for package
checks. Package checks do not establish native GPU, live input, WAN latency or
long-session acceptance; these require user testing after manual activation.
