# DeskPort 0.5.0

Stable desktop release with bounded local diagnostics, update notifications and
encoding-efficiency improvements.

## Changes

- Record privacy-filtered, size-limited local diagnostics by default while
  respecting an existing disabled preference. Export a validated ZIP for manual
  review and feedback; nothing is uploaded automatically.
- Show desktop release details in the update dialog, with translated interface
  strings across all supported languages.
- Try bounded VBR for smart AMD RADV Vulkan H.264/HEVC sessions to reduce filler
  traffic in simple scenes; preserve fallback behavior for other drivers.
- Improve encoded-byte and asynchronous IDR diagnostics, and refresh device
  settings after background endpoint updates.

## Downloads and validation scope

macOS Apple Silicon (macOS 26+): Developer ID signed and Apple-notarized DMG/ZIP.
Linux x86_64: DEB, RPM, Arch, AppImage, portable archive, client-only Flatpak,
and the pinned Nix flake. Native portable binaries require glibc 2.39 or newer.
Windows and mobile binaries and private mobile sources are not included.

See the accompanying verification report and SHA-256 checksums for package
checks. Package checks do not establish native GPU, live input, WAN latency or
long-session acceptance; these require user testing after manual activation.
