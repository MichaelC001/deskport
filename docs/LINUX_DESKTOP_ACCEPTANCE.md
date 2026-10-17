# Linux desktop package acceptance

This is a full-VM lifecycle layer, separate from
`scripts/test-linux-installers.py` (containers), a native Nix build, and physical
GPU/streaming acceptance. Reuse the workspace `linux-distro-lab` installed desktop
baselines. Start one guest at a time; keep its overlay, image and baseline hashes,
package hashes, logs and teardown evidence.

## Reproduce the lifecycle scenario

Run on the designated Linux lab builder from an independent source checkout.
Point the two input directories at retained, verified release packages; these
commands do not build or publish additional distribution formats.

```sh
run_root="$(bash scripts/build-paths.sh releases)/linux-lifecycle-UNIQUE_ID"
taskset -c 0-3 nice -n 15 python3 scripts/run-linux-desktop-lifecycle.py \
  --profile fedora-kde --native rpm \
  --old "$OLD_PACKAGES" --new "$NEW_PACKAGES" \
  --output "$run_root" --thread "$TASK_ID"
```

The current scenario compares the existing 0.7.1 and 0.7.2 artifacts. Use `deb`
for the Ubuntu GNOME and Debian KDE profiles, `arch` for Arch KDE and Omarchy,
and omit `--native` for Bazzite. Add `--flatpak` when the guest already has
the required Freedesktop 25.08 runtime, or supply an explicit offline bundle
with `--flatpak-runtime /path/Freedesktop-25.08.flatpak`. Its adjacent
`Freedesktop-25.08.json` identifies `ref` and the 64-character OSTree `commit`;
the guest verifies that exact installed commit and the input bundle SHA-256.
Repeat `--flatpak-runtime` for the matching `org.freedesktop.Platform.GL.default`
25.08 bundle on a graphical desktop. A base runtime alone is not the complete
graphics runtime used by the normal Flatpak installation route.
The scenario installs the local viewer bundle with
`--no-deps --no-related`, without silently downloading runtimes. Flatpak rollback
uses bundle reinstallation with data retained, not an OSTree generation rollback.
Missing runtimes are a failed prerequisite, not successful Flatpak acceptance.

DEB rounds stage explicit local packages in the guest's APT archive cache and use
APT with `--allow-downgrades` while guest egress remains restricted. If a minimal guest
lacks a declared dependency, provide its matching distro package explicitly with
`--dependency /path/dependency.deb`; its checksum is part of the retained input
manifest. The Ubuntu baseline required `libopengl0` 1.7.0-3, matching its installed
`libglvnd0`. Do not use `--force-depends`, widen guest networking or drop a declared
dependency to make installation pass.

`--session x11` selects an already installed Plasma X11 session in an owned SDDM
guest, then restores the original Wayland session before the runner's final
health check. It requires actual Xorg and `kwin_x11` processes. X11 rounds check
that background launch has no viewable DeskPort window and that duplicate launch
maps a viewable window, using the actual X server's window properties. The
temporary guest-only display-manager override is removed in `finally`. Missing
X11 desktop packages fail explicitly; Xwayland is never substituted.
An explicit checksum-locked `kwin-x11` dependency may complete the Debian guest's
optional X11 desktop in its disposable overlay. The sealed Wayland baseline and
builder system are preserved.

`--wait-seconds` provides a bounded wait for the shared VM lock. It never removes
an existing lock. Inventory, the lab's own canonical locks and baseline checks
still apply after reservation. A stale lock needs an ownership audit, not a
second lock name. The wrapper retains its lock if teardown cannot be verified.

The guest refuses an existing `/opt/deskport`, requires the lab tester and a
full virtual machine, and uses a new private XDG configuration/data/cache tree.
It checks install, upgrade, rollback, removal and retention of a synthetic key
in the actual `DeskPort/DeskPort.conf`. AppImages replace a temporary candidate
atomically rather than overwriting an executing image. Each GUI starts in the
background; a second launch must return while the first owner survives. Exact
owned process identities, including daemonized AppImage FUSE processes, are
stopped before advancing to the next artifact.

For the new AppImage, the running launcher's own runtime environment launches
the bundled display helper. Two on-demand create/release cycles check protocol
acknowledgments and, where the desktop exposes a supported topology query,
restoration of the original outputs. This is display-helper acceptance, not
encoded video or a successful remote connection. Portal, PipeWire and ordinary
user uinput availability are recorded separately; the test grants no privileges.

## Evidence and boundaries

The run directory retains `manifest.json`, `image-pin.json`,
`baseline-manifest.json`, `lab-state.json`, guest health before/after, command
stdout/stderr and the exact uploaded bundle. The lab retains screenshots and its
owned PID/start-time, port/socket and baseline-hash teardown checks. A failed
guest command or teardown remains a failed run.

The release source identities are:

| Artifact version | Application source | Shared core |
| --- | --- | --- |
| 0.7.1 | `57b5ffed8664efceed3c8fce38ec456137903b5e` | `108ca7d24c023b443687971690b1066427012608` |
| 0.7.2 | `922c38c2c35b93a43477a7bd02c75baa63e265c8` | `4064059f51d41bcd0b460b5353be57554e361bd6` |

On Wayland, duplicate-launch survival verifies activation transport without
proving window visibility or focus. The X11 scenario additionally observes
window mapping; focus remains unverified. A compositor screenshot, still frame,
portal introspection, writable device or helper acknowledgment alone does not
verify remote clipboard, sustained synthetic video/audio or injected input.
Flatpak must remain viewer-only: the scenario checks that no Sunshine/libexec
host is present and that filesystem access was not added to its permissions.

The reusable baselines cover KDE Wayland, GNOME Wayland and Hyprland. An Xwayland
application inside one of those sessions is not an installed X11 desktop test.
Hardware AMD/Intel/NVIDIA behavior, latency, physical input and real streaming
retain separate acceptance requirements. Keep SELinux and other
security controls unchanged and record denied prerequisites as such. No personal
host, Moonlight identity, pairing, clipboard or installed desktop service is an
input to this scenario. Seamless is outside this work.

## 2026-10-17 checkpoint

Lifecycle matrix and retained run identifiers are recorded here after each
verified guest round. An unlisted or pending scenario has not passed.

| Guest | AppImage lifecycle | Native/viewer bundle lifecycle | Current AppImage display helper | Retained lab run |
| --- | --- | --- | --- | --- |
| Fedora 44 KDE Wayland | PASS | RPM PASS | Two create/release cycles, topology restored | `20261017T114427Z-841f4a92` |
| Arch KDE Wayland | PASS | Arch package PASS | Two create/release cycles, topology restored | `20261017T115011Z-d2de55b9` |
| Omarchy 4.0.4 Hyprland | PASS | Arch package PASS | Two supported extended-workspace create/release cycles, topology restored | `20261017T115555Z-99ca9031`; `20261017T122833Z-c99562a3` |
| Debian 13 KDE Wayland | PASS | DEB PASS | Unsupported on installed KWin 6.3; requires 6.6+ | Retained round `p2-linux-20261017-debian-kde-02` |
| Bazzite 44 KDE Wayland | PASS | Viewer-only Flatpak PASS with Platform + GL 25.08 | Two create/release cycles, topology restored | `20261017T115945Z-35588c0a`; retained Flatpak round `p2-linux-20261017-bazzite-kde-02` |
| Debian 13 Plasma X11 / Xorg | PASS | DEB PASS | Wayland virtual-output helper not invoked | `20261017T122149Z-20429a23` |
| Ubuntu 26.04 GNOME Wayland | PASS | Stable 0.7.1/0.7.2 DEB GUI FAIL; runtime candidate pending | Second virtual-output creation FAIL | Retained rounds `p2-linux-20261017-ubuntu-gnome-01` and `p2-linux-20261017-ubuntu-gnome-04` |

The Fedora round passed all nine scenarios. Portal and PipeWire were available,
ordinary-user uinput was not writable, and SELinux remained Enforcing. Its VM,
owned children, sockets/SSH forward and baseline verification passed teardown.
Failed setup/fixture rounds remain retained separately and do not contribute a
PASS to this matrix. A fixture failure is not evidence of a release-package defect.

The first Ubuntu 26.04 GNOME round also reproduced a release AppImage failure:
the second Mutter create request returned `Cannot uniquely identify the Mutter
virtual output` (retained round `p2-linux-20261017-ubuntu-gnome-01`). Keep
display-helper failures separate from package lifecycle results. The release
AppImage is immutable; this record does not claim a newer source candidate was
packaged or that an untested source change fixes the release artifact.

The Ubuntu native failure is independent of the display-helper case: GDB caught
SIGSEGV inside the packaged `libva-wayland.so.2`, called by `vaInitialize()` during
the startup hardware-decoder capability probe. Correct Xauthority did not remove
it. Native DEB 0.7.1 and 0.7.2 both reproduce it; the same packages passed the
Debian Xorg lifecycle. The source packaging now shares the already pinned,
unmodified libva 2.23.0 across viewer and host trees before any portable format
is produced. A local candidate preserves the released viewer/host executable
hashes and replaces only these VA runtime files plus their notices/metadata.
Published assets retain their original hashes; candidate acceptance is recorded
separately from the failing released binaries. Legacy native rollback on this
newer Mutter remains a recorded failure rather than an implied PASS.
