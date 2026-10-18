# DeskPort 0.7.3

Formal desktop release preparation for macOS Apple Silicon, Linux x86_64 and
Windows x64. The integrated application baseline is
`50431b296a5b21c9d41551709d62c6fcafbf98f7`; the shared core is
`f34fc7b1524ce436185c4b085a903e6af4314790`, with matching Git and Nix pins.
The release version commit and exact artifact identities will be recorded in
the release verification metadata. Packaging, final verification and publication
are pending; earlier source or private-package results do not certify 0.7.3.

## Changes since 0.7.2

- **Shared UI localization:** primary desktop pages and the browser client use
  the same 22-language catalog. Browser language changes update labels, status,
  errors, accessible names and text direction immediately, with system-language
  selection and same-origin tab synchronization. Translation drafts still need
  native-speaker review, especially Tibetan; font and layout coverage varies by
  platform. Protocol identifiers and user content remain unchanged.
- **Windows signed-in sharing:** the full installer registers an automatic
  display broker so an ordinary signed-in console user can start and stop sharing
  without repeated UAC prompts. The GUI, host and pairing identity remain in the
  user session. Display-lease recovery handles helper/broker failures and rejects
  foreign callers. Sharing requires the full installation; the portable ZIP
  remains a viewer. This is not pre-login or secure-desktop hosting.
- **Windows system scaling:** owned independent virtual desktops apply 100% or
  200% system DPI, verify the observed result and restore the original virtual
  display preference on disconnect or recovery. Mirroring preserves physical
  display scaling. Small workspaces that cannot meet Windows' effective desktop
  floor fall back to verified 100% with a localized warning.
- **Linux runtime fixes:** native and portable packaging share the pinned
  libva 2.23.0 runtime between viewer and host. This addresses the older bundled
  VA-API Wayland startup crash observed on newer GNOME. The native host launcher
  runs system utilities before applying its private library path, preventing
  incompatible private libraries from breaking newer system tools.
- **Guarded Linux display policies:** Hyprland can offer mirror, turn-off-others
  and extension when monitor identity and state can be restored safely, with
  journaled recovery and Lua/legacy configuration support. Advanced color/VRR
  setups retain the bounded extension path. X11 adds opt-in 1x extension through
  an existing inactive, explicitly selected software dummy output; it does not
  install drivers, change global DPI or add general GPU/XWayland support.
- **Wayland clipboard fallbacks:** wlr-data-control and a clipboard-only portal
  supplement the preferred ext-data-control path. Permission/session loss and
  replaced offers cancel stale work; images and files remain demand-driven.
  Portal availability and consent depend on the compositor. No initial clipboard
  content is pushed to a peer.
- **Optional local text input:** a default-off per-device mode lets desktop
  viewers use local IME composition and send committed UTF-8 text. Focus loss,
  hiding and teardown cancel composition and release held remote keys. Candidate
  windows use a fixed top-left anchor; the existing remote-IME mode remains
  available.
- **Display recovery and layout:** macOS recovery rejects an empty mirror source
  instead of accepting an invalid topology. Device, group and add cards now stay
  square as the desktop window changes its column count, including folder views.
- **Clearer support evidence:** the versioned PC support matrix separates source
  implementation, isolated tests, historical installed-package observations and
  remaining native acceptance. Linux lifecycle fixtures preserve upgrade,
  rollback, configuration-retention and teardown results, including failures.

## Downloads and scope

The planned formats are macOS arm64 (macOS 26+) DMG and ZIP; Windows x64 full
Setup and portable viewer ZIP; Linux x86_64 DEB, RPM, Arch package, AppImage,
portable archive, viewer-only Flatpak and Nix. Native Linux packages require
glibc 2.39 or newer. The AppImage includes a private runtime; host graphics,
capture and input prerequisites still apply. Flatpak uses Freedesktop Platform
25.08 and does not host desktops.

The [0.7.3 release](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
will carry corresponding public application/dependency sources, third-party
notices, relinking materials and package inventories alongside the binaries.
`SHA256SUMS.txt` will cover every other asset; `PROVENANCE.json` and
`VERIFICATION.txt` will identify exact application/core revisions, package hashes,
CI runs and platform results. Asset count, public download verification and final
trust checks remain pending until publication.

Mobile clients are distributed separately through their store workflow. No
mobile source or development package is part of this desktop release. Seamless
remains deferred: the isolated development branch and M3 worktree are excluded.
The ten retained PC task branches in the
[integration ledger](PC_TASK_INTEGRATION.md) are also outside this release.

## Validation boundary

The [integration ledger](PC_TASK_INTEGRATION.md) records 13 passing targeted
groups for the combined source: shared-core/Qt adapters, localization,
preferences, lifecycle (45), binding (172), Hyprland IPC, clipboard demand (8),
private-bus portal (19), local IME, Linux VA runtime and support evidence. The
subsequent square-card snapshot passed 27 UI cases and a full native Linux Nix
build with packaged CLI identity checks. These are pre-release integration
results, not exact 0.7.3 artifact or physical-hardware acceptance.

Historical scoped native results include Windows virtual-output DPI and signed-in
sharing, local Cocoa/Win32 IME composition, isolated Linux X11 CJK composition,
GNOME/Hyprland clipboard fixtures, Chromium browser localization/media fixtures,
and Linux VM package/runtime scenarios. Each retains its tested source and
package identity in the [PC support matrix](PC_SUPPORT_MATRIX.md) and linked
records. The [0.7.2 release notes](RELEASE_0.7.2.md) retain that release's
published-package results without attributing them to this version.

The following remain explicit limits:

- Windows cold-boot/session changes, full installation/upgrade/rollback/uninstall
  lifecycle, current lock-screen transitions, credential/UAC-desktop input and
  fresh pre-login hosting are not accepted as a complete matrix.
- GNOME capture-startup timeouts, repeated virtual-output creation and
  PipeWire/display-recovery failures remain separate from the fixed VA runtime
  startup crash. The deferred GNOME policy branch is not included.
- Hyprland physical hotplug, HDR/ICC/VRR, full client media/input and X11 physical
  GPU/input/audio acceptance are not established by the isolated output probes.
- Native Wayland local IME, fullscreen/HiDPI candidate placement and committed
  text insertion into real remote applications still need acceptance. Clipboard
  file-manager paste and wlr-v1-only coverage remain incomplete.
- Broad physical browser/device coverage, native-speaker translation review,
  long-session sleep/wake/route recovery, audio/clipboard quality, latency and
  the complete HDR pipeline remain separate checks. Build and CI success cannot
  establish those results.

Before publication, the exact macOS packages must pass Developer ID signing,
notarization, app/DMG ticket stapling and extracted-ZIP Gatekeeper verification;
Windows packages must pass payload/PE, non-installed native CLI and normal-
protection scan checks; Linux packages must pass their package/runtime gates.
The exact release source also needs the required Nix and CI checks. None of
these final 0.7.3 checks is claimed complete by this preparation document.

Publishing the desktop release and updating mynix do not activate installed
services. The user performs the later configuration switch and live validation.
