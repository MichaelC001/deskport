# DeskPort 0.7.6

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.
This stable release was promoted to GitHub Latest on 2026-10-27.

## Final packages — 2026-10-27

The final downloads replace the earlier packages. Filenames contain only
version 0.7.6 and the target platform.
This replacement includes:

- Native foreground-scoped Windows shell-shortcut capture, including left/right
  Win+Space, with physical scan codes and local control preserved on release.
- Native macOS shortcut capture through the existing Accessibility permission,
  with local composition releasing capture.
- Bounded inline text events that safely pass through Linux SDL2 compatibility
  queues, also used by the other desktop adapters.

Windows and macOS packages reuse the verified binaries built from
`77cf280c98a26171e79015d888d47ce0111dbc0c`. Linux packages and Nix use
`622946ed6cfc95dde08bebf99d7ee78327089e07`; the intervening changes only update
acceptance documentation. The core remains unchanged. The corrected `v0.7.6`
tag also points to `622946ed6cfc95dde08bebf99d7ee78327089e07`; Nix examples keep
that exact revision pinned.

See `SHA256SUMS.txt`, `PROVENANCE.json` and `VERIFICATION.txt` on the
release for the current packages. The replaced assets and their records are
no longer offered as release downloads. Reinstall an existing 0.7.6 package;
see the [Linux installation guide](LINUX_PACKAGES.md) for each package manager.

## Changes since 0.7.5

- **GNOME display policies and recovery:** guarded extension, primary-only and
  matching-mode mirror policies, independent recovery journals, and corrected
  virtual-output admission. Recover authenticated sessions after capture-host
  replacement and start a fresh desktop when no resumable app remains. Preserve
  the existing admission, takeover and bounded retry rules.
- **GNOME capture lifetime:** avoid closing an unopened portal descriptor and
  defer stale display-layout replay after capture loss. The documented Mutter
  stream-lifetime patch is a separate compositor prerequisite for the qualified
  PipeWire-loss recovery cases; DeskPort does not install it automatically.
- **Remote IME shortcuts:** route shortcut inhibition through the retained
  canvas's keyboard surface and enable local composition only when selected.
  Release capture when focus is lost and distinguish requested inhibition from
  compositor-confirmed capture.
- **Responsive connection windows:** keep tray, recall, hide and device switching
  responsive before a worker produces its first frame. Late worker readiness or
  hide replies no longer reopen a hidden window.
- **Linux keyboard remappers:** identify the receiving virtual keyboard as
  `DeskPort Keyboard` in both Linux host backends, allowing receiver-side rules
  to select it independently of Sunshine.
- **Linux host startup:** initialize GPU environment variables before starting
  asynchronous logging, preventing a startup race with the logger's timezone
  lookup on Ubuntu 24.04.

The shared core remains `505ae0ccd0a4ead4decb6eaba62585bf278e6dbe`, with matching
Git and Nix pins. Mobile clients and deferred Seamless work are outside this release.

## Downloads and scope

- macOS arm64, macOS 26+: Developer ID signed and Apple-notarized DMG and ZIP.
- Windows x64: full Setup and portable viewer ZIP. Application and installer
  signatures are separate from the bundled driver's signature validation.
- Linux x86_64: DEB, RPM, Arch package, AppImage, portable archive and Nix.
  Native packages and portable archives require glibc 2.39 or newer. AppImage
  bundles a private runtime; GPU, capture and compositor prerequisites remain.
- Linux x86_64 Flatpak: viewer only, using Freedesktop Platform 25.08.

See [versioned assets](https://github.com/keithxc/deskport/releases/tag/v0.7.6)
and the [Linux installation guide](LINUX_PACKAGES.md). Fully quit the previous
application before upgrading. Existing settings and pairing identities remain.

`SHA256SUMS.txt`, `PROVENANCE.json` and `VERIFICATION.txt` record exact packages,
source revisions and validation results. Source is referenced by repository and
commit; upstream notices remain inside packages. Every uploaded asset is checked
against GitHub's server-provided SHA-256 and byte size. Separate source/relink
archives are not generated.

## Validation boundary

Final-package checks are recorded in `VERIFICATION.txt`. Earlier source and
development-package checks in the [roadmap](ROADMAP.md) and
[GNOME evidence](GNOME_DISPLAY.md) retain their original scope; they do not
establish acceptance of every final 0.7.6 package.

The GNOME recovery qualification uses an explicitly patched Mutter compositor
and isolated software-rendered guests. Installing DeskPort alone does not patch
Mutter. Matching-mode mirroring, supported scale choices and virtual-output APIs
remain compositor dependent. The previously recorded Hyprland fullscreen-restore
limitation remains open. The SDL compatibility text fixture passes; this does
not establish every desktop input method or composed-text combination.

Guarded native-input checks have successful observations in all six directed
macOS/Linux/Windows combinations. The final Windows package passes both
Win+Space variants and fixed held-key sequences to the macOS host's Ethernet
endpoint. The user also confirmed normal Windows physical-keyboard input.
Native-injection probes through the host's Wi-Fi endpoint still show delayed
key release and repeated letters; the exact network component is not diagnosed.
The physical-keyboard confirmation does not erase that synthetic-path result.
These checks do not establish every keyboard layout, compositor or long session.

Package and synthetic canvas checks do not establish physical GPU, audio, HDR,
latency, real-network recovery or long-session acceptance. Updating mynix prepares
configuration builds; activation and live use remain manual.

## Current publication

The release contains 10 installers: macOS DMG/ZIP, Windows Setup/portable ZIP,
and Linux DEB/RPM/Arch/AppImage/tar/Flatpak. Together with eight platform records
and three aggregate records, these are the 21 current release assets, verified
against GitHub server SHA-256 and byte size. The repaired installer bytes are
unchanged by the filename correction; their records use the current names.
GitHub Latest is v0.7.6 following maintainer approval on 2026-10-27.
The package provenance and verification records retain their pre-promotion
publication state; installer bytes and checksums are unchanged by promotion.

Final native packages pass fresh-install and isolated CLI/QML/host API checks
on Ubuntu 24.04, Debian 13, Fedora 44 and Arch rolling. The extracted final
AppImage passes Ubuntu 22.04 Xvfb/private-runtime checks; the final tar passes
Ubuntu 24.04 startup checks. Both payloads match the packaged files, modes and
links. The exact Flatpak passes isolated 25.08 runtime checks, duplicate launch
and three crash/restart cycles. Its document portal was unavailable and is not
qualified. Same-version package upgrades and desktop VM runs were not repeated
in this packaging run; native input evidence retains the scope described above.

## Historical desktop acceptance

The 2026-10-26 validation used Linux source
`06274045c9323f74f8776bcbeb50660848542393`. Its Ubuntu startup fix passed 20
consecutive package checks on glibc 2.39. Fedora KDE, Ubuntu GNOME, Arch KDE and
Bazzite KDE passed their desktop lifecycle/canvas cases. Debian client lifecycle
and canvas checks passed, while its older KWin cannot provide host virtual
outputs. Omarchy retained unsupported virtual-output admission and a fullscreen
restore timeout after 11 of 16 canvas stages. These are historical observations,
not repeated VM acceptance of the current packages. Original publication records
are retained in the maintainer's local release history.
