# DeskPort 0.4.2

Stable desktop release consolidating the 0.4.1 and 0.4.2 development work.

- Confirmed takeover between authorized devices, with an authenticated explanation
  on the displaced client instead of a generic video-disconnect error.
- Per-device desktop size tuning and conservative macOS minimum workspaces that
  preserve aspect ratio and HiDPI scale.
- macOS dedicated-display recovery when the display cannot be separated initially.
- Vendored viewer dependencies and a pinned shared workspace/protocol core.
- Portable Linux packages now build the patched host with authenticated session
  admission; native installers include the KDE display-helper permission entry.
- Updated feature, platform and product-comparison documentation.

## Downloads

macOS Apple Silicon (macOS 26+): Developer ID signed, notarized DMG and ZIP.
Linux x86_64: DEB (Ubuntu 24.04+/Debian 13+), RPM (Fedora 44), Arch,
AppImage (glibc 2.39+), client-only Flatpak (Freedesktop Platform 25.08), and Nix.
Native Linux packages and AppImage bundle Sunshine; Flatpak cannot host.
Matching source and checksums accompany the release.

The universal iPhone/iPad client is distributed separately through the App Store
and TestFlight. Android remains in development. Windows, Intel Mac and ARM Linux
are not qualified desktop release targets.

## Validation and limits

See the release asset VERIFICATION.txt for the exact build, signing, package and
regression checks. Physical iPad/Android checks against the preceding preview
covered macOS/KDE first frames, orientation, keyboard dismissal, confirmed handoff
and display restoration; these are not a fresh-machine stable installer test.

The known pending inbound TLS handshake limitation remains: on some Linux paths,
background endpoint refresh can wait for the original handshake timeout despite
the smaller-segment retry. See [TCP recovery](TCP_RECOVERY.md).

Adaptive resize reconnects video briefly. Real GPU, capture/input permissions,
cross-device clipboard and long-session acceptance remain hardware-dependent.
Publication does not activate or replace installed applications.
