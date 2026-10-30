# DeskPort 0.7.7

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.

## Changes since 0.7.6

- **Background video recovery:** stop host encoding while video is paused,
  use the full 32-bit RTP frame index, and request a key frame or reconnect
  when video does not resume. These changes address a frozen picture after a
  long background pause, including when connecting to an older host.
- **Optional hidden title bar:** enable the setting in Appearance or the tray
  menu to give the remote canvas more room. It is off by default. On macOS,
  hold Control and Command while dragging to move the window or drag an edge
  or corner to resize it. On Windows, turn the option off to move or resize
  with the mouse. Native Linux Wayland keeps the Devices page frameless while
  the option is enabled to avoid changing decorations between pages.
- **Display and window recovery:** retry a host's ambiguous busy/invalid
  display refusal briefly, preserve window geometry through fullscreen and
  decoration changes, and suspend remote input during local window gestures.
- **Canvas transitions and input:** retain a per-session blurred backdrop
  during reconnect and resize when a suitable recent frame is available;
  HDR and unavailable captures use a plain background. Forward touchscreen
  contacts through the existing adapters, confine absolute pointers to the
  visible canvas, and release held input before resuming after a queue overflow.
  Presentation errors are retained and shown in device details.
- **Video timing and diagnostics:** use a monotonic clock for canvas pacing
  and distinguish pre-transport frame gaps from local queue drops. A reported
  pre-transport gap does not by itself establish physical packet loss.

The shared core is pinned to `866c09547cd47d4b26a0725374e70614cf2845db` in
both Git and Nix. Mobile clients are outside this desktop release.

## Downloads and scope

- macOS arm64, macOS 26+: Developer ID signed and Apple-notarized DMG and ZIP.
- Windows x64: full Setup and portable viewer ZIP. The application and installer
  are unsigned; bundled driver signature validation is a separate check.
- Linux x86_64: DEB, RPM, Arch package, AppImage, portable archive and Nix.
  Native packages and the portable archive require glibc 2.39 or newer.
  AppImage uses a private runtime; GPU, capture and compositor prerequisites
  still apply.
- Linux x86_64 Flatpak: viewer only, using Freedesktop Platform 25.08.

Fully quit the previous application before upgrading. See the release's
`SHA256SUMS.txt`, `PROVENANCE.json` and `VERIFICATION.txt` for the exact
packages, source revisions and checks. Source is referenced by repository
and commit; upstream notices remain inside packages.

## Validation boundary

Package checks and their results are recorded in the release verification
records. The isolated window, canvas, input and video regressions described in
the [roadmap](../ROADMAP.md) do not establish physical touch, pointer capture,
mixed-DPI behavior, display timing, HDR, audio, latency or live streaming on
every platform. Linux compositor capabilities and decoration behavior vary.
Updating mynix prepares configuration builds; activation and live use remain
manual.

## Published artifacts and source

The [release](https://github.com/keithxc/deskport/releases/tag/v0.7.7) contains
22 assets. The Linux packages rebuilt from `1b559bf013d9dfbd8962af15dd63030293e858da`
use standard filenames; their bytes match the tested packages originally named
with an `-r2` suffix. The tag and macOS/Windows packages retain source
`6bd3d80003b14df3f6a6fd0c5d287acb06ea5c58`. Shared core remains
`866c09547cd47d4b26a0725374e70614cf2845db`.

The complete `SHA256SUMS.txt` covers the other 21 assets. GitHub's server
SHA-256 digests and sizes were checked against the package records; the small
checksum, build, provenance and verification records were also read and checked.
See [Linux verification](https://github.com/keithxc/deskport/releases/download/v0.7.7/VERIFICATION-linux.txt)
and the [Linux review guide](LINUX_0.7.7_REVIEW.md) for the Debian virtual-display
and Omarchy fullscreen-restore limitations.

The verification and provenance attachments are snapshots made before Latest
promotion. References to Latest 0.7.6 in those snapshots describe that earlier
state; the release page records the subsequent promotion decision. No package
changes are required to promote the verified release.
