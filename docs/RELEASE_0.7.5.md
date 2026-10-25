# DeskPort 0.7.5

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.

## Changes since 0.7.4

- **One persistent window:** the device list, connection progress, takeover
  confirmation and remote desktop use the same native application window.
  Video presentation runs inside the application process. The separate Sunshine
  host remains independent. The remote canvas fills the content area and hides
  DeskPort's navigation bar until the device list returns.
- **Immediate desktop recall:** returning to Devices retains the selected
  session's canvas, decoder and last ready frame. Returning to the desktop does
  not show a new connection animation or renegotiate resolution merely because
  the hidden page changed geometry. Hiding the window or showing Devices releases
  remote input.
- **Window-state handling:** preserve fullscreen, maximized and windowed states
  through connection and recall. macOS chrome changes preserve the outer frame,
  native fullscreen controls remain available, and pending Metal presentation
  callbacks survive renderer teardown safely.
- **Configurable recall shortcut:** choose and record a global window shortcut
  in Settings, with immediate updates to native registration and captured viewer
  input. Conflicts and unavailable platform bindings remain visible to the user.
- **Linux package rendering:** include the libplacebo frontend required by the
  embedded canvas. The device list remains available when Vulkan cannot
  initialize, instead of crashing in Qt 6.4. Linux video presentation requires a
  working Vulkan driver.
- **Input and regression coverage:** preserve physical canvas keys, isolate
  macOS modifier events with missing native codes, bound local transport waits,
  and exercise retained presentation through resize, fullscreen, maximize,
  hidden-window recall and device-list return without a personal host.

The shared core is `505ae0ccd0a4ead4decb6eaba62585bf278e6dbe`, with matching
Git and Nix pins. Mobile clients and the deferred Seamless branch are outside
this desktop release.

## Downloads and scope

- macOS arm64, macOS 26+: Developer ID signed and Apple-notarized DMG and ZIP.
- Linux x86_64: DEB, RPM, Arch package, AppImage, portable archive and Nix.
  Native packages and portable archives require glibc 2.39 or newer. AppImage
  includes a private runtime; GPU, capture and compositor prerequisites remain.
- Linux x86_64 Flatpak: viewer only, using Freedesktop Platform 25.08.
- Windows x64: full Setup and portable viewer ZIP. The application and installer
  are unsigned; bundled driver signatures are checked separately.

See the [release assets](https://github.com/keithxc/deskport/releases/tag/v0.7.5)
and [Linux installation guide](LINUX_PACKAGES.md). Fully quit the previous
application before upgrading. Existing settings and pairing identities remain.

`SHA256SUMS.txt`, `PROVENANCE.json` and `VERIFICATION.txt` identify the exact
packages, source revisions and validation results. Source is referenced by
public repository and commit; license notices remain inside the packages.
Every uploaded asset is checked against GitHub's server-provided SHA-256 and
byte size. Separate source/relink archives are not generated.

## Validation boundary

Final-package validation is recorded in the release verification asset. Earlier
0.7.4 self-test results describe their exact recorded binaries, not the new
0.7.5 packages. Virtual desktop and synthetic decoder checks do not establish
physical GPU, audio, HDR, latency or long-session acceptance.

The [PC support matrix](PC_SUPPORT_MATRIX.md) retains platform-specific limits,
including GNOME capture/recovery, Windows secure desktop, native Wayland IME
and broader hardware coverage. Updating mynix does not activate an installation;
the user performs `rebuild switch` and subsequent live verification.
