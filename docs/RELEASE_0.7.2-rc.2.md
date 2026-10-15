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
