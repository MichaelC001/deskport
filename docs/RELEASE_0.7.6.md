# DeskPort 0.7.6

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.
This is a stable release, intentionally published without changing GitHub Latest.
Promotion is a separate maintainer decision after initial use.

## Keyboard r3 replacement — 2026-10-27

Use downloads containing `keyboard-fix-r3`. The application version remains
0.7.6, and the original tag, assets and hashes are retained. GitHub Latest is
unchanged. This replacement includes:

- Native foreground-scoped Windows shell-shortcut capture, including left/right
  Win+Space, with physical scan codes and local control preserved on release.
- Native macOS shortcut capture through the existing Accessibility permission,
  with local composition releasing capture.
- Bounded inline text events that safely pass through Linux SDL2 compatibility
  queues, also used by the other desktop adapters.

Windows and macOS packages reuse the verified r3 binaries built from
`77cf280c98a26171e79015d888d47ce0111dbc0c`. Linux packages and Nix use
`622946ed6cfc95dde08bebf99d7ee78327089e07`; the intervening changes only update
acceptance documentation. The core remains unchanged. Nix consumers must pin
the r3 revision instead of the original `v0.7.6` tag.

See `SHA256SUMS-r3.txt`, `PROVENANCE-r3.json` and `VERIFICATION-r3.txt` on the
release for this replacement. Earlier unsuffixed records describe the original
publication. Reinstall an existing 0.7.6 package using the exact r3 filename;
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

`SHA256SUMS-r3.txt`, `PROVENANCE-r3.json` and `VERIFICATION-r3.txt` record exact packages,
source revisions and validation results. Source is referenced by repository and
commit; upstream notices remain inside packages. Every uploaded asset is checked
against GitHub's server-provided SHA-256 and byte size. Separate source/relink
archives are not generated.

## Validation boundary

Final-package checks are recorded in `VERIFICATION-r3.txt`. Earlier source and
development-package checks in the [roadmap](ROADMAP.md) and
[GNOME evidence](GNOME_DISPLAY.md) retain their original scope; they do not
establish acceptance of every final 0.7.6 package.

The GNOME recovery qualification uses an explicitly patched Mutter compositor
and isolated software-rendered guests. Installing DeskPort alone does not patch
Mutter. Matching-mode mirroring, supported scale choices and virtual-output APIs
remain compositor dependent. The previously recorded Hyprland fullscreen-restore
limitation remains open. The r3 SDL compatibility text fixture passes; this does
not establish every desktop input method or composed-text combination.

Guarded native-input checks have successful observations in all six directed
macOS/Linux/Windows combinations. The final Windows r3 package passes both
Win+Space variants and fixed held-key sequences to the macOS host's Ethernet
endpoint. The user also confirmed normal Windows physical-keyboard input.
Native-injection probes through the host's Wi-Fi endpoint still show delayed
key release and repeated letters; the exact network component is not diagnosed.
The physical-keyboard confirmation does not erase that synthetic-path result.
These checks do not establish every keyboard layout, compositor or long session.

Package and synthetic canvas checks do not establish physical GPU, audio, HDR,
latency, real-network recovery or long-session acceptance. Updating mynix prepares
configuration builds; activation and live use remain manual.

## r3 publication

The r3 addition contains 10 installers: macOS DMG/ZIP, Windows Setup/portable ZIP,
and Linux DEB/RPM/Arch/AppImage/tar/Flatpak. Together with eight platform records
and three aggregate records, all 21 new files pass GitHub server SHA-256 and
byte-size verification. The earlier 21 assets remain unchanged, for 42 total.
GitHub Latest remains v0.7.4.

Final r3 native packages pass fresh-install and isolated CLI/QML/host API checks
on Ubuntu 24.04, Debian 13, Fedora 44 and Arch rolling. The extracted final
AppImage passes Ubuntu 22.04 Xvfb/private-runtime checks; the final tar passes
Ubuntu 24.04 startup checks. Both payloads match the packaged files, modes and
links. The exact Flatpak passes isolated 25.08 runtime checks, duplicate launch
and three crash/restart cycles. Its document portal was unavailable and is not
qualified. Same-version package upgrades and desktop VM runs were not repeated
in this packaging run; native input evidence retains the scope described above.

## Original publication

[v0.7.6](https://github.com/keithxc/deskport/releases/tag/v0.7.6) is published
as a stable release without changing GitHub Latest. All 21 assets passed
GitHub server SHA-256 and byte-size verification. Linux downloads use the `r2`
suffix and source `06274045c9323f74f8776bcbeb50660848542393`; unchanged macOS and
Windows binaries retain source `a238f0ecc713f6cf1f231161cb7d98e05889239b`.
The shared core remains unchanged.

The Ubuntu startup fix passed 20 consecutive package checks on glibc 2.39.
All four container checks passed. Fedora KDE, Ubuntu GNOME, Arch KDE and Bazzite
KDE passed their final desktop lifecycle/canvas cases. Debian client lifecycle
and canvas checks passed, while its older KWin cannot provide host virtual
outputs. Omarchy retains unsupported virtual-output admission and a fullscreen
restore timeout after 11 of 16 canvas stages. The
[verification record](https://github.com/keithxc/deskport/releases/download/v0.7.6/VERIFICATION.txt)
and [provenance](https://github.com/keithxc/deskport/releases/download/v0.7.6/PROVENANCE.json)
record the exact scope. Configuration builds prepare the update; activation is
still manual.
