# DeskPort 0.3.0

This stable desktop release brings the session-settings development branch to main.

- Card-only device browsing, system marks, light/dark themes and per-device streaming settings.
- Session traffic and duration reporting on macOS and Linux.
- Fractional-scale client workspace sizing with full HiDPI video frames.
- More reliable text clipboard observation, including background macOS copies.
- Responsive connection navigation, input ownership across page replacement, and adaptive resize transitions.

## Downloads

macOS Apple Silicon (macOS 26+): Developer ID signed and Apple-notarized DMG and ZIP.
Linux x86_64: DEB (Ubuntu 24.04+/Debian 13+), RPM (Fedora 44), Arch package,
AppImage (glibc 2.39+), client-only Flatpak (Freedesktop Platform 25.08), and Nix.
The native Linux packages and AppImage include a separate Sunshine runtime.
Flatpak does not provide host sharing. See LINUX_PACKAGES.md for installation details.

Windows, Intel Mac, ARM Linux and mobile store packages are not part of the established desktop release matrix.

## Verification boundaries

Build, isolated UI and package checks are recorded in the accompanying verification file.
Real streaming, GPU decoding, input permissions, cross-device clipboard and long-session
acceptance remain hardware checks. Publication does not activate installed applications.

Source and pinned component references accompany the release; upstream notices are retained.
