# DeskPort 0.7.4

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.
The replacement downloads use an `r2` filename suffix and include the built-in
global window shortcut. The application version remains 0.7.4. Earlier packages
used source `423ab9fe` and did not include the shortcut; their small receipts
remain historical evidence and do not describe the replacement binaries.
The GitHub release identifies the current publication status.

## Changes since 0.7.3

- **Built-in window shortcut:** Command + apostrophe on macOS and Windows/Super
  + apostrophe on Windows/Linux recall or hide DeskPort. The resident process
  uses the native platform registration; the viewer forwards the same chord
  while capturing keyboard input. Activation waits for key release and removes
  duplicate notifications. Wayland requires the GlobalShortcuts portal and its
  permission/binding flow; unavailable or conflicting bindings are reported
  without replacing other applications' shortcuts. A tokenless Wayland
  shortcut can remap the same visible background window to bring it forward;
  this fallback is limited to the user's shortcut action and does not apply
  to automatic session recovery. X11 checks the server's input focus so that
  holding the shortcut through Qt's temporary grab-related focus loss still
  hides the foreground device list on release.
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
- Windows x64: full Setup and portable viewer ZIP. The application and installer
  are unsigned; release verification includes native package, Defender and
  installation lifecycle checks.

See the [release assets](https://github.com/keithxc/deskport/releases/tag/v0.7.4).
Use `SHA256SUMS-r2.txt`, `PROVENANCE-r2.json` and `VERIFICATION-r2.txt` for the
replacement package set. Platform build records include exact source revisions
and asset hashes. The Linux-only X11 correction follows the verified macOS and
Windows binary revision; provenance records each platform separately rather
than attributing every binary to the release tag. Nix uses the pinned public source;
mobile clients are outside this desktop release.

## Validation boundary

The release verification record reports exact-package checks, including
bounded native shortcut checks, with their environments and limits. Earlier
staging checks apply only to their recorded source and package hashes.
Compilation, mocked portal tests and registration alone do not establish real
window activation or physical keyboard acceptance.

The [PC support matrix](PC_SUPPORT_MATRIX.md) retains the separately scoped
limits for Windows pre-login/secure desktop, GNOME capture/recovery, physical
Hyprland/X11 behavior, native Wayland IME, broad browser/device coverage,
long-session recovery, clipboard/audio quality, HDR and latency. Local shortcut
validation does not establish physical streaming or remote-input acceptance.

mynix delivery uses the exact source and immutable macOS ZIP URL/hash after
pulling the repository. Updating the pins does not activate an installation; the user
performs `rebuild switch` and subsequent live validation.
