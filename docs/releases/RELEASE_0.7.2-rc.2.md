# DeskPort 0.7.2-rc.2 — Bazzite AppImage test

Linux x86_64 AppImage candidate for [issue #5](https://github.com/keithxc/deskport/issues/5),
integrated with the current main branch. This prerelease supplies the AppImage,
exact application/core and host source materials, runtime source index, build
package list, verification record and SHA-256 checksums.

## Fixes

- Keep the selected host loader and matching libc cohort together when the
  AppImage launches its display helper and independent recovery process.
- Restore local outputs before removing or replacing the virtual primary, then
  verify the original layout after removal. Bounded recovery waits cover the
  existing compositor transactions; failures retain the baseline journal.
- Backport the upstream Qt Wayland placeholder-screen lifetime fix to the
  viewer's Qt 6.4.2. Only Wayland Client is rebuilt against the matching SDK;
  complete corresponding source, licenses and rebuild/relink materials ship
  with the modified library.
- Terminate owned Linux Sunshine/credential children when the GUI dies,
  including startup races. Extracted-AppImage login startup uses AppRun.
- Validate automatic KWin permissions against the actual helper executable.

The integrated source also includes main's recent data saver and daily budget,
live bitrate ceiling, recovery-frame scheduling, screen-lock video pause and
focus/input changes. Shared core Git and Nix pins both use `4064059`.

## Validation and retest

The previous fix candidate reproduced the original loader error and the exact
`QScreen::geometry()` crash in isolated Bazzite 44 / KWin 6.7.5 userspace with
glibc 2.43-9. Its fixed artifact passed 20 disconnect/reconnect cycles and 60
exact resizes. The release verification record identifies the new integrated
artifact and its own results separately.

The lab uses official Bazzite container userspace and a headless KWin compositor
on a NixOS kernel. It establishes the tested runtime, screen-lifetime, output
restoration and process-supervision behavior. Physical GPU/driver combinations,
network streaming, remote input and latency still require device acceptance.

For the reporter's retest, extract into a fresh directory and launch AppRun with
all bundled libraries present. Use the default Wayland platform and the normal
virtual-primary display policy, then check resize, disconnect, reconnect, local
layout restoration and process/port cleanup. Issue #5 stays open for that retest.

## Published artifacts

[Release](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2): eight assets,
with all asset sizes/digests checked through GitHub and all downloads verified
anonymously. The stable Latest remains 0.7.1.

- [Linux x86_64 AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.2-rc.2/DeskPort-0.7.2-rc.2-x86_64.AppImage)
- [Checksums](https://github.com/keithxc/deskport/releases/download/v0.7.2-rc.2/SHA256SUMS.txt)
- [Verification](https://github.com/keithxc/deskport/releases/download/v0.7.2-rc.2/VERIFICATION.md)
- [Provenance](https://github.com/keithxc/deskport/releases/download/v0.7.2-rc.2/PROVENANCE.json)

AppImage SHA256: `d6a11b45ceebc82a80014bc2b7fad9af2c97b62eaea70601bed621ac3476034d`
(219,834,872 bytes). Tag source: `4124e6318d3432d6dc6e4cb7638d5155e1ca49d8`;
binary source: `75cba028b591892357e13e88dd6911907ab493ae`; their Git trees are
identical. Core: `4064059f51d41bcd0b460b5353be57554e361bd6`.
The public source-archive NAR matched independently on macOS and Linux:
`sha256-031OMsLXM2TotqqGlbKGlR4yaZuyXJXsqCMVhbLnb/4=`.

The final AppImage passed 20 disconnect/reconnect cycles and 60 exact resizes,
with 483 registry events, 490 Qt screen events and 614 fixture frames. The real
GUI remained alive, baseline layouts returned and no tracked test process remained.
The native build, core checks, lifecycle 39, binding 172, service 13 and UI 27
tests passed. KWin 6.6.6/6.7.5 disabled-output baselines passed, as did four actual
Sunshine software H.264 capture/encoder probes. Exact relink inputs passed a
separate offline linker/CLI/dependency smoke test.

Tag-source CI: [Linux](https://github.com/keithxc/deskport/actions/runs/38682402312),
[display](https://github.com/keithxc/deskport/actions/runs/38682402379),
[Windows source](https://github.com/keithxc/deskport/actions/runs/38682402290).
All passed. These headless/container results leave physical GPU/driver, sustained
streaming, input and latency acceptance for the reporter's installed-system retest.
Issue #5 stays open; no mynix or deployed-service activation is included.
