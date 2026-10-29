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
  embedded canvas and load the Vulkan runtime without requiring development
  packages. The device list remains available when Vulkan cannot
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
and [Linux installation guide](../LINUX_PACKAGES.md). Fully quit the previous
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

The release source is `5c4b2f2e19a03726f63972e13b59727bfbeadb4a`; its Linux
build, Windows source checks and display-contract CI workflows pass. The macOS
and Windows binaries use `18dc883a25767e475ab8d0c38caf8852af071fca`; subsequent
executable changes affect Linux only.

- Native macOS, Windows and NixOS packages pass the 16-stage synthetic canvas
  test. The Nix package selected by the mynix delivery also passes separately.
- Fedora KDE, Ubuntu GNOME, Debian KDE, Arch KDE and Bazzite KDE pass 16 canvas
  stages for each tested package route. Ubuntu's geometry check temporarily
  disables GNOME automatic maximization in its disposable guest.
- All six desktop guests pass package install, upgrade, rollback, uninstall and
  configuration-retention checks; every VM teardown and baseline hash is verified.
- **Omarchy 4.0.4 / Hyprland 0.56.2 has a known window-restoration limitation.**
  AppImage and Arch packages complete stages 0–10, including retained-frame
  recall and fullscreen entry, but returning to a normal window times out with
  Qt reporting Maximized while the decoder stays ready. Stages 11–15 are not
  accepted. This remains a failed canvas acceptance result even with a floating
  test window and explicit maximize requests enabled in the disposable guest.
  Protocol diagnostics record the application's unmaximize/unfullscreen requests;
  Hyprland's [mapped-window state](https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/protocols/XDGShell.cpp#L482-L489)
  and [maximize request handling](https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/desktop/view/Window.cpp#L1286-L1333)
  explain the compatibility boundary. No deployed compositor configuration is changed.
- Host virtual-display checks remain limited on GNOME, pre-6.6 KWin and the
  tested Hyprland guest. Package availability does not imply every host display
  policy or every Linux distribution is validated.

The [PC support matrix](../PC_SUPPORT_MATRIX.md) retains platform-specific limits,
including GNOME capture/recovery, Windows secure desktop, native Wayland IME
and broader hardware coverage. Updating mynix does not activate an installation;
the user performs `rebuild switch` and subsequent live verification.
