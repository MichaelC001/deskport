# DeskPort 0.7.3

Formal desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.
The integrated application baseline is
`50431b296a5b21c9d41551709d62c6fcafbf98f7`; the shared core is
`f34fc7b1524ce436185c4b085a903e6af4314790`, with matching Git and Nix pins.
The binaries were built from `3257bec4b7ef38d3cbc428e1774c4c71f3c9b36a`.
The final tag adds release documentation only. `PROVENANCE.json` records both
revisions, the audited documentation diff and exact artifact identities; earlier
source or private-package results do not certify these packages.

## Changes since 0.7.2

- **Shared UI localization:** primary desktop pages and the browser client use
  a shared 22-locale catalog. Desktop also retains additional legacy translations
  with varying coverage. Browser language changes update labels, status,
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

The formats are macOS arm64 (macOS 26+) DMG and ZIP; Windows x64 full
Setup and portable viewer ZIP; Linux x86_64 DEB, RPM, Arch package, AppImage,
portable archive, viewer-only Flatpak and Nix. Native Linux packages require
glibc 2.39 or newer. The AppImage includes a private runtime; host graphics,
capture and input prerequisites still apply. Flatpak uses Freedesktop Platform
25.08 and does not host desktops.

The [0.7.3 release](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
carries corresponding public application/dependency sources, third-party
notices, relinking materials and package inventories alongside the binaries.
`SHA256SUMS.txt` covers every other asset; `PROVENANCE.json` and
`VERIFICATION.txt` identify exact application/core revisions, package hashes,
CI runs and platform results. The 21 assets comprise two macOS files, five
Windows files, eleven Linux/source/material files and three verification files.
Nix builds from the pinned public source rather than a separate binary attachment.

Mobile clients are distributed separately through their store workflow. No
mobile source or development package is part of this desktop release. Seamless
remains deferred: the isolated development branch and M3 worktree are excluded.
The ten retained PC task branches in the
[integration ledger](../PC_TASK_INTEGRATION.md) are also outside this release.

## Validation boundary

The [integration ledger](../PC_TASK_INTEGRATION.md) records 13 passing targeted
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
package identity in the [PC support matrix](../PC_SUPPORT_MATRIX.md) and linked
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

Exact 0.7.3 package evidence includes:

- macOS: 156 Mach-O files and 35 bundles passed Developer ID signing checks with
  secure timestamps and hardened runtime. Apple accepted both submissions;
  app/DMG stapling, extracted-ZIP Gatekeeper and bundled-host loader checks passed.
- Windows: installer/portable payload equivalence, PE imports and version
  `0.7.3.0` passed. Four non-installed native CLI modes exited successfully.
  Defender completed the protected custom scan with zero exclusions and zero
  detections; package hashes remained unchanged and the extraction was removed.
  The driver catalog signature is valid; the application and installer remain
  unsigned. This release did not run an installation or GUI acceptance test.
- Linux Bazzite: the final AppImage passed normal FUSE launch, GUI and matching
  viewer/helper/recovery runtime checks in a fresh KDE Wayland VM overlay with
  SELinux Enforcing. Virtual-output creation/release, layout restoration and
  teardown passed. The VM uses software graphics, without a physical GPU.
- Linux Ubuntu 26.04: the final DEB passed the GNOME Wayland VA-runtime regression,
  background-owner/activation, upgrade and configuration-retention checks. The
  old 0.7.2 startup crash was reproduced before upgrade and after rollback.
  Baseline integrity and teardown passed. GNOME capture/recovery was not tested
  by this package gate and retains the gaps above.
- Isolated regressions: shared-core/adapters, smart-stream, translation
  placeholders, browser localization and local text input passed, alongside
  172 binding cases and 27 UI cases. The local Linux Nix package and CLI/desktop
  identity smoke passed with verified builder CPU/nice limits. Canonical Nix
  builds, the final tag's CI runs, container/Flatpak gates, material audits and
  source NAR identity are recorded in the release verification files.

The pre-publication verification files describe the sealed artifacts. Anonymous
download checks and delivery records are added to the published-artifacts record
after publication; they are separate from real streaming and input acceptance.

Publishing the desktop release and updating mynix do not activate installed
services. The user performs the later configuration switch and live validation.

## Published artifacts

The [stable 0.7.3 release](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
is public and marked Latest with 21 assets. The tag resolves to
`e05cf9529efbee13d0054a41631d2fa02a1c1f84`; binary source remains
`3257bec4b7ef38d3cbc428e1774c4c71f3c9b36a`. The final source archive NAR is
`sha256-+8YNPEX+gfIHoVPK2/l54P5s5dwjoAj6l25uyGm+u/E=`, independently matched
on macOS and Linux. This publication record is a later documentation update;
it does not move the tag or replace any release asset.

All 21 assets were downloaded anonymously and matched the sealed local byte
counts, SHA-256 hashes and GitHub asset digests. `SHA256SUMS.txt` contains exactly
20 entries, covering every other asset. Existing release assets remain intact.
The exact final-source CI runs passed: [Linux build](https://github.com/keithxc/deskport/actions/runs/39134399964),
[display contracts](https://github.com/keithxc/deskport/actions/runs/39134399976)
and [Windows source checks](https://github.com/keithxc/deskport/actions/runs/39134399977).

The attached verification metadata records artifact and isolated/VM checks.
Publishing and consumer configuration builds do not establish installed-system,
physical GPU, live streaming or remote-input acceptance, and do not activate
managed installations.
