# DeskPort 0.7.2

Stable desktop release preparation for macOS Apple Silicon, Linux x86_64 and
Windows x64. Final packages and verification records are being prepared;
0.7.1 remains the published stable release until this release is published.

## Changes since 0.7.1

- Faster adaptive resize reconnects, with a blurred picture of the previous
  stream behind the progress indicator. Windows draws the transition using
  Direct3D 11. Initial connections and HDR retain the plain background.
- Smoother remote drags and pixel-paced wheel scrolling on macOS hosts, with
  quicker refinement of still text after movement.
- Adaptive host bitrate and recovery-frame scheduling for changing network
  conditions, preserving the negotiated display resolution.
- Live data-saver controls, a compact traffic display and daily traffic-budget
  warnings. The host applies the current bitrate ceiling without requiring a
  reconnect and reduces idle-frame and recovery overhead.
- Video pauses after the viewer has been hidden from the tray or closed for
  five seconds, and while the Linux client screen is locked after that delay.
  Minimization alone keeps streaming. Reopening or unlocking restores video;
  hiding still releases remote input immediately.
- More reliable input recovery when a desktop viewer regains focus, including
  forwarding the first click, and tray control while a connection is starting.
- Tray artwork derived from the application icon and updated native appearances.
- Bazzite and modern Linux AppImage fixes: keep the selected loader and libc
  together for helpers, backport the Qt Wayland screen-lifetime fix, restore
  physical layouts before removing virtual outputs, and clean up owned host
  processes after an unexpected GUI exit.
- Linux installation instructions organized by distribution, with explicit
  native-package, AppImage, Flatpak and Nix support boundaries.
- Reproducible Windows build inputs with pinned hashes, public mirrors and a
  clean-checkout bootstrap; transition diagnostics record capture outcomes.

## Downloads and scope

The intended desktop formats match the previous full release: macOS arm64
(macOS 26+) DMG and ZIP; Windows x64 Setup and portable ZIP; Linux x86_64 DEB,
RPM, Arch, AppImage, portable archive and Nix; and a viewer-only Flatpak using
Freedesktop Platform 25.08. Native Linux packages require glibc 2.39+; the
AppImage contains a private runtime and uses supported system graphics drivers.

Mobile clients are distributed separately. Experimental Linux Seamless host
mode remains optional and incomplete. The additional supervisor and diagnostic
work on the Seamless development branch is not included in this release; this
release does not add remote application windows or Seamless input.

Exact application, shared-core and modified dependency sources, third-party
notices, relinking materials, package lists, provenance, verification records
and SHA-256 checksums accompany the prepared artifacts. The shared-core Git and
Nix pins both use `4064059f51d41bcd0b460b5353be57554e361bd6`.

## Validation boundary

Earlier candidate results are recorded in the
[0.7.2-rc.1 notes](RELEASE_0.7.2-rc.1.md) and
[0.7.2-rc.2 notes](RELEASE_0.7.2-rc.2.md). They do not certify the final 0.7.2
artifacts. The final verification record must identify the exact source and
package hashes and report current signing, notarization, package, CI and
isolated regression results separately.

Container and headless compositor checks do not establish every physical GPU,
driver, sustained stream, remote-input path or latency result. Bazzite issue #5
remains subject to the reporter's installed-system retest. Managed activation
and subsequent live acceptance remain user-controlled.
