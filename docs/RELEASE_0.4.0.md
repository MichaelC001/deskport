# DeskPort 0.4.0

This stable desktop release consolidates the adaptive-display and connection-recovery
development branches into main.

- Shared workspace core with virtual-primary mirroring, virtual-primary with other
  screens disabled, and extended-display policies on capable macOS and KDE hosts.
- Per-device address editing directly from a device card or device settings, with
  pinned certificate and identity preserved.
- macOS mirror-mode and adaptive-resolution recovery: disconnected display
  placeholders no longer block saving or restoring the original screen layout.
- KDE stale lease recovery: a removed output is recreated after a failed local
  monitor restore, and switching clients can still reach a correctly sized workspace.
- Linux control connections recover from network paths that stall TLS by dropping
  larger TCP segments, using a bounded 900-byte MSS fallback with a short in-memory
  success cache. Certificate checks and authentication are unchanged; launch, resume,
  quit and pairing requests are never replayed by this fallback.

## Downloads

macOS Apple Silicon (macOS 26+): Developer ID signed and Apple-notarized DMG and ZIP.
Linux x86_64: DEB (Ubuntu 24.04+/Debian 13+), RPM (Fedora 44), Arch package,
AppImage (glibc 2.39+), client-only Flatpak (Freedesktop Platform 25.08), and Nix.
The native Linux packages and AppImage include a separate Sunshine runtime.
Flatpak does not provide host sharing. See LINUX_PACKAGES.md for installation details.

Windows, Intel Mac, ARM Linux and mobile store packages are not part of the established desktop release matrix.

## Known limitation

On affected Linux TCP paths, an earlier pending inbound TLS handshake can block
the smaller-segment retry until the original handshake times out. Background
endpoint refresh may fail while an existing stream remains active. Bounded
handshake admission remains an open follow-up; see [TCP recovery](TCP_RECOVERY.md).

## Verification boundaries

Build, isolated UI and package checks are recorded in the accompanying verification file.
Real streaming, GPU decoding, input permissions, cross-device clipboard, multi-monitor
recovery and long-session acceptance remain hardware checks. Publication does not
activate installed applications.

Source and pinned component references accompany the release; upstream notices are retained.
