# DeskPort 0.3.5

This stable desktop release consolidates the desktop development branches into main.

- On-demand image and file clipboard transfer, with immediate text sharing and compatibility fallback.
- More reliable clipboard transfer scheduling and disconnect cleanup.
- Opt-in macOS unattended startup and recovery, with visible approval and pause state.
- macOS session workspace mirroring and text caret geometry reporting for compatible clients.
- Restored desktop sidebar version display and isolated macOS VM validation tools.

## Downloads

macOS Apple Silicon (macOS 26+): Developer ID signed and Apple-notarized DMG and ZIP.
Linux x86_64: DEB (Ubuntu 24.04+/Debian 13+), RPM (Fedora 44), Arch package,
AppImage (glibc 2.39+), client-only Flatpak (Freedesktop Platform 25.08), and Nix.
Native Linux packages and AppImage include a separate Sunshine runtime.
Flatpak does not provide host sharing. See LINUX_PACKAGES.md for installation details.

Windows, Intel Mac, ARM Linux and mobile store packages are outside this release matrix.

## Verification boundaries

The accompanying verification file records automated build and package checks.
Real streaming, GPU decoding, input permissions, Finder/Dolphin paste behavior,
large-file paste timeouts, display reconnection and unattended recovery remain
hardware acceptance checks. Publication does not activate installed applications.

Source and pinned component references accompany the release; upstream notices are retained.
