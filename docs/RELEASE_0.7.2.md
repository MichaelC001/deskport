# DeskPort 0.7.2

[Stable desktop release](https://github.com/keithxc/deskport/releases/tag/v0.7.2)
for macOS Apple Silicon, Linux x86_64 and Windows x64. Packages use application
source `922c38c2c35b93a43477a7bd02c75baa63e265c8` and shared core
`4064059f51d41bcd0b460b5353be57554e361bd6`.

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

## Published artifacts (21)

The desktop formats match the previous full release: macOS arm64
(macOS 26+) DMG and ZIP; Windows x64 Setup and portable ZIP; Linux x86_64 DEB,
RPM, Arch, AppImage, portable archive and Nix; and a viewer-only Flatpak using
Freedesktop Platform 25.08. Native Linux packages require glibc 2.39+; the
AppImage contains a private runtime and selects compatible system graphics
dependencies. Its Qt Wayland backport is AppImage-specific; native packages and
Flatpak retain their baseline Qt runtime.

The 21 published assets comprise 2 macOS files, 5 Windows package/material files,
11 Linux package/source/material files and 3 verification metadata files. Nix
uses the release tag: `nix run github:keithxc/deskport/v0.7.2`.

Mobile clients are distributed separately. Experimental Linux Seamless host
mode remains optional and incomplete. The additional supervisor and diagnostic
work on the Seamless development branch is not included in this release; this
release does not add remote application windows or Seamless input.

Exact application, shared-core and modified dependency sources, third-party
notices, relinking materials, package lists, provenance, verification records
and SHA-256 checksums accompany the packages. The shared-core Git and Nix pins
both use the core revision above. Verify downloads using
[SHA256SUMS.txt](https://github.com/keithxc/deskport/releases/download/v0.7.2/SHA256SUMS.txt);
it covers the other 20 assets and excludes itself. Exact build identities and
check results are in
[PROVENANCE.json](https://github.com/keithxc/deskport/releases/download/v0.7.2/PROVENANCE.json)
and [VERIFICATION.txt](https://github.com/keithxc/deskport/releases/download/v0.7.2/VERIFICATION.txt).

## Validation boundary

Earlier candidate results are recorded in the
[0.7.2-rc.1 notes](RELEASE_0.7.2-rc.1.md) and
[0.7.2-rc.2 notes](RELEASE_0.7.2-rc.2.md). They do not certify the final 0.7.2
artifacts. The final verification record identifies the exact source and
package hashes and reports signing, notarization, package, CI and isolated
regression results separately.

- The exact application revision passed Linux build, display-contract and
  Windows source CI checks. Core, smart-stream, binding and UI checks passed.
- macOS packages passed Developer ID signatures, secure timestamps, hardened
  runtime, Apple notarization, app/DMG ticket stapling and Gatekeeper verification
  after final ZIP extraction.
- Windows packages passed PE/payload audits, non-installed CLI checks and the
  recorded Defender scan. Setup completed a visible upgrade from 0.7.1 to 0.7.2;
  all 694 installed payload files matched their expected hashes. Before first
  launch, 122 existing configuration files and HKCU settings were unchanged;
  pairing and keys were also preserved after the GUI checks. Disconnected local
  GUI checks passed shortcut launch, Settings/About 0.7.2, close-to-tray, recall
  and Quit. Uninstall and planned real streaming/input acceptance remain untested.
- Linux native installer checks ran in isolated containers; the Nix package
  build, Ubuntu 22.04 AppImage runtime checks and isolated Flatpak checks passed.
  Containers share the build host kernel and do not prove installed-desktop
  package integration.
- The final AppImage, SHA-256
  `0cbb521bfa36964740190afdc4a995eb4a55d54803d8b138aef89e889d5644e7`,
  passed a fresh Bazzite KDE Wayland VM run with normal FUSE startup and no
  extraction fallback. Its visible GUI stayed alive for 30.34 seconds; bundled
  Qt/backport mappings and matching GUI/helper/recovery loader/libc were verified.
  A 1280x720 virtual output was created and released with the layout restored.
  SELinux remained Enforcing, the read-only baseline was unchanged, and owned
  processes, ports, sockets and locks were released.

During Windows GUI automation, a focus race caused an unintended connection to
an existing paired host. The client was quit; read-only host checks confirmed
zero active sessions, no reserved lease and restoration of the original display
layout. This incident is not streaming acceptance, and diagnostics cannot
establish that no remote input was transmitted.

The Bazzite VM used software virtual graphics and displayed the expected
hardware-decoder-unavailable warning. Container, headless compositor and VM
checks do not establish physical GPU, driver, hardware-codec, sustained-stream,
remote-input, audio or latency acceptance. Bazzite issue #5 remains subject to
the reporter's installed-system retest. Managed activation and subsequent live
acceptance remain user-controlled.
