# DeskPort 0.7.1

Stable PC release for macOS Apple Silicon, Linux x86_64 and Windows x64.

## Changes since 0.6.3

- Browser access over HTTPS and WebRTC (H.264/Opus), with touch, keyboard, mouse
  and text input. Sign in with a six-digit code that changes every 30 seconds;
  optionally remember a browser for seven days. Owned Tailscale devices can
  authenticate through the existing local identity service.
- Browser desktops follow resizing, rotation and fullscreen, stream at the
  desktop's resolution and support up to 60 fps. The compact header follows the
  native UI. Extended-display mode remains dependent on host support.
- One-time QR invitations for separately distributed mobile clients.
- Native clients can take over browser sessions after confirmation. The takeover
  question appears in the connecting window and scales with the desktop.
- Clicking the tray recalls or hides the viewer while keeping its background
  session. Hiding releases remote input immediately; video pauses after five
  seconds and resumes when recalled.
- Automatic codec selection prefers HEVC where negotiated and avoids H.264
  streams above its supported 4096-pixel dimension. EGL drawing uses the actual
  surface size, including wide windows.
- KWin 6.7 output discovery and exact-size virtual-display handling retain the
  KWin 6.6 path. AppImage runtime fixes include matching PipeWire SPA plugins,
  Mesa EGL registration and VA-API dependencies.
- Windows sharing recovers detached virtual displays, a terminated display
  guardian and portrait-panel layouts while preserving the saved physical layout.
- Preparation diagnostics distinguish admission, host, display and encoder
  failures without exporting raw private error text. Experimental Linux Seamless
  host mode remains optional.

## Downloads and verification

macOS arm64 requires macOS 26+. DMG and ZIP contain Developer ID signed,
notarized code and stapled tickets. Windows x64 Setup includes the host and
signed virtual-display driver for offline installation; the portable ZIP contains
the same runtime payload. Native Linux DEB/RPM/Arch and portable archives require
glibc 2.39+. The AppImage includes a private glibc runtime. Nix users can pin
`v0.7.1`. Flatpak is client only and uses Freedesktop Platform 25.08.

Exact sources, third-party notices, relinking materials, SHA-256 checksums and
`VERIFICATION.txt` accompany the release. Browser transport uses libdatachannel
0.24.1 (MPL-2.0), whose exact source is attached. Package and isolated regression
checks do not certify every GPU, live-input path, secure desktop or long session.
Microsoft antivirus determinations remain sample-specific. Mobile clients are
released separately and their private source is excluded.

Upgrading from the earlier fixed browser access code retires that code and
forgets browsers enrolled under it. Review local host permissions and activate
managed updates yourself.

## Published artifacts

The [stable release](https://github.com/keithxc/deskport/releases/tag/v0.7.1)
is marked Latest. Tag `v0.7.1` resolves to desktop source
`57b5ffed8664efceed3c8fce38ec456137903b5e`, with shared core
`108ca7d24c023b443687971690b1066427012608`.

All 19 public assets were downloaded without login and matched their local bytes,
GitHub sizes/digests and [SHA256SUMS.txt](https://github.com/keithxc/deskport/releases/download/v0.7.1/SHA256SUMS.txt).
[VERIFICATION.txt](https://github.com/keithxc/deskport/releases/download/v0.7.1/VERIFICATION.txt)
records notarization, native Windows checks, distribution installation tests,
Nix and Flatpak validation, and the scope of isolated regressions. Current
physical streaming/input acceptance and managed activation remain separate.
