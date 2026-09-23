# Windows development checkpoint

This branch preserves the Windows x64 host and client integration on top of
the current DeskPort 0.5.6 desktop baseline. It is not a release candidate
approved for distribution. The main release branch is unchanged.

The deliverable is a self-contained x64 installer (plus an optional portable
ZIP). End users do not need Nix, Qt, MinGW, npm, or a compiler. The maintained
cross-build recipes accept ordinary source archives from `winbuild/source-inputs`
or explicit `S_*` environment overrides; machine-specific `/nix/store` paths
are forbidden by the Windows-branch CI.

## Scope and platform impact

Windows-specific capture, driver ownership, display recovery, native OLE
clipboard and installer helpers coexist with shared changes to peer binding,
display-mode negotiation, session behavior and QML. Shared changes require
macOS and Linux regression review before merging; platform isolation must not
be assumed from Windows compilation.

## Existing evidence

Earlier Windows cross-build and constrained integration builds passed. Targeted UI, native
window transitions, H.264 decoded-frame and clipboard ownership checks passed.
Earlier VM runs exercised bidirectional streaming and clipboard transfers,
tray recall, input release, upgrades and identity retention. Some Linux input
checks used an isolated test-only XTest adapter, not native uinput.
The latest display helper preserved the physical primary through startup,
mode changes and restoration in targeted VM checks. These observations do
not certify the final combined installer on real GPU hardware.

## Outstanding acceptance

Final exact-package upgrade/uninstall/reinstall and state retention, abnormal
disconnect/reconnect, revocation/rebinding, complete current-UI streaming and
display stability, and cross-platform regression remain to be completed.
An earlier Windows Defender detection remains unresolved; testing while
protection was disabled does not establish safety or a false positive.
Normal-protection retesting is required before release.

VM testing was stopped after the user reported a VM crash and host stalls.
The cause has not been established. Do not restart heavy VM tests merely to
reproduce the old setup. Keep build outputs, private logs, screenshots and
credentials out of Git. Cross-build and packaging recipes are preserved under
`winbuild/scripts`; generated dependencies and binaries remain local.

## Host build order and offline delivery

Prepare the cached inputs first. Client recipes use `winbuild/source-inputs`
(or matching `S_*` overrides). Host curl/miniupnpc/minhook/onevpl archives,
prepared media libraries, signed VDD payload and immutable host assets are
currently required under `winbuild/full`; this is not yet a one-command bootstrap
from an empty cache. Enter `nix-shell winbuild/scripts/shell.nix` on Linux for
the repository-locked cross compiler, Qt host tools, npm and NSIS. The host recipe verifies the
vendored Sunshine archive, creates `winbuild/full/sunshine-prepared`, applies
the Windows and common DeskPort overlays, and builds into
`winbuild/full/host-build-prepared`:

```sh
nix-shell winbuild/scripts/shell.nix
winbuild/scripts/build-host-deps.sh
winbuild/scripts/build-host-media.sh
winbuild/scripts/build-host.sh
winbuild/scripts/build-app.sh
winbuild/scripts/build-maintenance.sh
winbuild/scripts/package-full.sh
```

Missing Windows-only source archives are downloaded during preparation from
fixed public commits and checked against `host-vendored-deps.json`. Cached
archives are rechecked on reuse. None of these downloads are performed by the
installer or installed application.

Use `DESKPORT_HOST_SOURCE_DIR` and `DESKPORT_HOST_BUILD_DIR` to inspect
separate prepared trees. `export-full-materials.py` records those actual trees
under the normal extraction paths and excludes obsolete `full/sunshine` and
`full/host-build` outputs.

The current Windows work also includes an SSH native CLI patch; its `--version`
check has passed. Native streaming, input, display, installer and upgrade
acceptance remain pending, so a successful cross-build or CLI check is not
native Windows acceptance.

## 2026-09-21 delivery requirements

Windows is a full desktop peer: it both connects to other PCs and hosts incoming
connections. The deliverable is one offline x64 installer containing the static
client, private host, display/recovery helpers, signed virtual-display driver and
notices. Build-time dependency downloads do not run on end-user machines. Windows
system components and GPU drivers remain OS/vendor-managed. `audit-package.py`
rejects external Qt, multimedia, crypto and compiler runtime DLL dependencies.

Mutual binding now waits for host readiness before announcing completion. The
Windows host build reapplies the current common network, encoder, input activity
and cadence overlays from verified source, instead of reusing an old patched
tree. Windows enables the same smart-streaming host switch. Actual performance
and bidirectional session behavior still require native acceptance.

Native CLI regressions can be run over SSH with `tests/windows-cli.ps1`. The
main window title bar follows the application's theme, including system appearance
notifications and window recall. Neither check substitutes for streaming/input
acceptance or proves virtual-display lifecycle parity.

### Current checks

The current candidate passed 98 mutual/client binding regressions, shared core
workspace fixtures, translations, ENet failure handling and smart-stream policy
tests. A Linux Nix build passed for the shared binding/CLI changes. On Windows 11
hardware, all four noninteractive version/help cases passed, and isolated Light
and Dark profiles both matched native DWM title-bar attributes and screenshots.
The client directory scan completed with Defender real-time protection enabled
and no detection records returned. These checks do not certify the final combined
installer, normal-protection operation of the rebuilt host, or bidirectional
streaming/input acceptance.

The final full candidate also passed installer/ZIP payload equivalence and PE
dependency audits. Its exact client binary matches the native title-bar test
binary; its bundled host reported `2026.906.222525` on Windows. Static Defender
scans of the extracted payload and installer completed with protection enabled
and no detection records returned, and the included driver catalog reported a
valid signature. A later installed/portable helper was nevertheless quarantined
by Defender as `Trojan:Win32/Bearfoos.A!ml`; this remains a release blocker and
must be investigated with normal protection enabled. Exact-package installation,
two-way real streaming/input and recovery remain pending; the Windows virtual
display is still sharing-scoped.

The full installer verifies all required DeskPort executables and driver files
immediately after extraction and again before completion. If a file is missing,
setup fails and leaves the uninstaller available for cleanup. Review Windows
Security protection history or another security product's quarantine record
before repairing the installation; protection must not be disabled.


## Physical Windows checkpoint — 2026-09-23

The Windows development branch now includes the current desktop UI and core
revision. Native CLI and light/dark title-bar attributes passed. An upgrade
from 0.5.0 to the 0.5.6 candidate preserved the tested identity/state files.
Cross-platform core, binding, lifecycle, UI and Linux package checks passed.

On a closed-lid AMD laptop, both an Apple tablet and a macOS desktop received
actual Windows lock-screen video. Tablet acceptance also exercised twelve
workspace changes, disconnect/reconnect, and takeover of a live desktop viewer;
the previous viewer reported that the session was taken over. The Apple client
needed finite display-mode negotiation to work with the Windows virtual driver.
These results do not establish password-entry, secure-desktop transitions,
pre-login operation, audio/clipboard parity, or reverse-stream input acceptance.

Fast native display enable/resize/release/crash loops exposed output-enumeration
and device-disable races. `tests/windows-display.ps1 -Rounds 10` exercises each
round's normal release, forced exit, and reuse of crash state, checking the
physical layout and disabled owned adapter between scenarios. Run it elevated
in the interactive session, with sharing stopped; session 0 is intentionally
rejected. Retained recovery snapshots and phase logs are administrator-only.
A failed round is a failed acceptance gate even when manual recovery succeeds.
The installed candidate passed all thirty scenarios with `-PortraitSwitch`,
which changes to 1080x1920 and back to 1920x1080 before release or forced exit.
Its helper and mode-list hashes matched the audited package; upgrading retained
the host certificate and key. The physical tablet then passed twelve orientation
changes and disconnect/reconnect against that installation.
Silent uninstall removed the application and its recovery task; reinstall
restored both while preserving the certificate, private key and peer database.
The physical tablet received frames again after reinstall, and all four native
CLI help/version checks passed. The Windows viewer also rendered the macOS
desktop. These checks still do not establish reverse-stream input or audio.

An elevated interactive user can open the ordinary desktop but receives access
denied opening the Winlogon desktop on this target. A controlled service boundary
is still needed for full secure-desktop support; changing authentication or UAC
policy is not a substitute for implementing that boundary.
The development-only [session-service probe](WINDOWS_SESSION_SERVICE.md) passed
thirteen native authentication, desktop-access, timeout and cleanup checks. It is
not packaged and is not yet connected to the streaming host or input path.

The installer allows only the supported application port families from the
local subnet across Windows network profiles. Existing explicit block rules
can still override those allows and require diagnosis. Silent installer errors
must return a nonzero exit code without waiting on an invisible dialog.

The machine's install-directory antivirus exclusion was configured by its owner.
Testing within that exclusion does not resolve public-distribution detection.
The remaining live matrix is still needed; this development checkpoint is not
a stable-release certification.
