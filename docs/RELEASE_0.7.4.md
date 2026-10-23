# DeskPort 0.7.4

Staged desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.
Linux and macOS packages are published, while Windows packages are built and
statically audited, awaiting native validation. The release remains a prerelease
and is not marked Latest until that validation is completed and promotion is confirmed.
The current stable release remains 0.7.3 during this stage.

## Changes since 0.7.3

- **Compact device cards:** desktop device and add cards use a compact square
  layout. Each visible row is centered by its actual card span, including
  partially filled rows, without changing grid keyboard navigation. The add
  card keeps its two actions at their existing size and gives the plus its own
  click area above them.
- **Managed desktop builds:** the common build entry point isolates application
  work, packages, logs and tests by run, while retaining third-party dependencies
  independently. It records exact source and artifact identities, guards disk
  budgets, and checks GitHub's server-provided SHA-256 and size after upload.
- **Packaging maintenance:** translation compilation stays in the build tree;
  Windows retains Boost sources and audits separate work/output paths; macOS
  bundle checks remain inside their managed run. Linux smoke fixtures use short
  private socket paths. Full Linux packaging includes the portable archive and
  optional viewer-only Flatpak in the managed artifact inventory.
- **Public release materials:** downloads contain installers, checksums and
  small provenance records. Source is identified by its public repository and
  exact commit rather than duplicate source/relink archives. Third-party
  license notices remain inside packages. README language coverage is expanded.

The shared core remains `f34fc7b1524ce436185c4b085a903e6af4314790`, with matching
Git and Nix pins. This release does not integrate the deferred PC task branches
or the isolated Seamless development branch.

## Downloads and scope

- macOS arm64, macOS 26 or newer: Developer ID signed and Apple-notarized DMG
  and ZIP, published only after app/DMG stapling and extracted-ZIP Gatekeeper
  verification succeed.
- Linux x86_64: DEB, RPM, Arch package, AppImage, portable archive and Nix.
  Native packages require glibc 2.39 or newer. The AppImage bundles a private
  runtime; compositor, graphics, capture and input prerequisites still apply.
- Linux x86_64 Flatpak: viewer only, using Freedesktop Platform 25.08.
- Windows x64: full Setup and portable viewer ZIP are prepared separately.
  Native package/Defender checks and user validation remain pending; the
  application and installer are unsigned. Windows publication and promotion
  follow that gate.

See the [release assets](https://github.com/keithxc/deskport/releases/tag/v0.7.4).
`SHA256SUMS.txt`, `PROVENANCE.json` and `VERIFICATION.txt` record the published
asset set, exact source, CI and validation results. Platform build records carry
the corresponding package hashes. Nix uses the pinned public source instead of
a separate binary attachment. Mobile clients are outside this desktop release.

## Validation boundary

The packages use source commit `423ab9febd633e58fe16afc6f02fa27515621854`.
Its Windows source, display-control and Linux build CI checks passed. The
published macOS packages passed Developer ID, Apple notarization, stapling and
extracted-ZIP Gatekeeper checks. Linux passed the Nix build and isolated smoke
checks, container package installation on Ubuntu 24.04, Debian 13, Fedora 44
and Arch, offline AppImage checks on Ubuntu 22.04, isolated Flatpak installation
and recovery checks, and portable archive safety/notices checks.

All 17 uploaded assets were checked against GitHub's server-provided SHA-256
digest and exact byte size. `VERIFICATION.txt` records the detailed scope and
`SHA256SUMS.txt` covers the other 16 assets in this staging snapshot. Windows
packages remain unpublished. Earlier 0.7.3 checks retain their original package
identity and are not claimed as validation of these new bytes.

The [PC support matrix](PC_SUPPORT_MATRIX.md) retains the platform acceptance
limits: Windows pre-login/secure desktop and installation lifecycle, GNOME
capture/recovery, physical Hyprland/X11 behavior, native Wayland IME, broad
browser/device coverage, long-session recovery, clipboard/audio quality, HDR
and latency remain separately scoped. Successful builds, CI, isolated tests
and package audits do not establish physical streaming or remote-input
acceptance.

mynix was pulled, updated and pushed with the exact source and signed macOS
package pins. Updating mynix does not activate an installation; the user performs
`rebuild switch` and subsequent live validation. Stable promotion remains pending
until Windows validation is confirmed.
