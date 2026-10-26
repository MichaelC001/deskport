# DeskPort 0.7.6

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.
This is a stable release, intentionally published without changing GitHub Latest.
Promotion is a separate maintainer decision after initial use.

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
limitation remains open. Native Wayland local-IME bridge coverage also retains
the documented SDL compatibility-fixture limitation.

Package and synthetic canvas checks do not establish physical GPU, audio, HDR,
latency, real-network recovery or long-session acceptance. Updating mynix prepares
configuration builds; activation and live use remain manual.
