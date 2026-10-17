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

Use `--no-appimage` to repeat only a selected native or Flatpak route after its
independent AppImage checks have completed. `--package-api` additionally runs the
existing private loopback host API, resources, version and QML package checks
against the current native package. These checks disable input and audio and do
not establish a media connection.

For a startup crash, `--debugger-runtime` accepts an explicit cached GDB closure
archive. Its adjacent `source.json` records `path` and `archive_sha256`; only
checksum-locked `/nix/store/` files are extracted into the disposable guest.
GDB runs the failed native GUI with network symbol fetching disabled. It does
not attach to the builder's or a personal desktop's processes.

The optional `--verify-va-runtime-fix` comparison requires native DEB,
`--no-appimage`, the GDB input and `--package-api`. Legacy install/rollback must
reproduce the actual SIGSEGV stack through `libva-wayland` and `vaInitialize`;
the current candidate must survive GUI startup and pass the package checks.
The observation verifies `/proc` mappings and SHA-256 for all four private VA
libraries against the 2.23.0 source metadata; optional libva log verbosity is
not treated as runtime identity.
Legacy stages retain `passed: false` with an explicit `expected_failure` marker.
The report identifies this as a regression comparison, and the matrix continues
to list legacy native rollback as failed.

For a local runtime candidate, copy the retained AppDir into a new owned work
directory, then run `scripts/install-linux-libva.py APPDIR PINNED_LIBVA_STAGE`.
`scripts/package-linux-native.py APPDIR WORK VERSION --format deb` limits the
candidate to DEB; its default still produces the existing three native formats.
Use the canonical heavy-build reservation and retain both source-stage and final
package hashes. The four VA libraries in both private trees must be replaced
together; the viewer and Sunshine executable hashes remain unchanged.

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

`--capture-probe` additionally starts the packaged Sunshine against each owned
virtual output, with software encoding, loopback binding and input/audio/UPnP
disabled. It requires the software H.264 initialization probe to finish and
identify that output. Its logs are retained in `capture_probes`; an encoded
probe frame alone is not a sustained media connection.
Selection checks use the actual screencast marker, not an echoed configuration
or output inventory. The WLR fixture also requires a capture-buffer size distinct
from original outputs; an ambiguous fixture fails rather than implying ownership.

For a separate complete synthetic AV check, prepare existing cached tools with
`scripts/prepare-linux-desktop-media.py` inside the caller's canonical heavy
reservation and locked devShell. It verifies the owning ancestor PID/start time,
CPU affinity and nice level. Pass explicit `--chromium`, `--pulseaudio`, `--xvfb`,
`--xterm`, `--ffmpeg`, `--node`, `--openssl` store paths, `--playwright` package,
`--thread` and a new `--output` directory. Nothing is downloaded. Use
`--gateway-source` and `--gateway-revision` to build the existing production
gateway fixture from the source matching the retained host package. The stage
records source hashes, tool/store closure, fixture/module hashes and archive hash.
These are private test inputs, not product or release assets.

Pass that stage using `--media-runtime STAGE --capture-probe`. Only the owned VM
extracts its checksum-locked tools. The existing `test-browser-native.py` fixture
uses an independent Xvfb, private PulseAudio null sink, changing synthetic image
and 440 Hz tone. Its two connections must show increasing decoded H.264 frames,
changing multicolor pixels, continuing Opus packets and increasing decoded audio
energy, then release media/admission on disconnect. The real gateway/host and
loopback WebRTC transport are used. Native OS input stays disabled; the existing
driver's contract must not be weakened. The fixture stops its own process groups;
the VM still undergoes baseline and teardown verification. This separate Xvfb
media result does not establish sustained Wayland capture/transport or hardware
quality. Synthetic screenshots are retained by the owned host observer.

The reusable baselines cover KDE Wayland, GNOME Wayland and Hyprland. An Xwayland
application inside one of those sessions is not an installed X11 desktop test.
Hardware AMD/Intel/NVIDIA behavior, latency, physical input and real streaming
retain separate acceptance requirements. Keep SELinux and other
security controls unchanged and record denied prerequisites as such. No personal
host, Moonlight identity, pairing, clipboard or installed desktop service is an
input to this scenario. Seamless is outside this work.
Where `wayland-info` is already installed, later rounds record advertised
clipboard/data-control protocols without reading content. Advertisement alone
does not verify clipboard transfer, focus behavior or portal authorization.

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
| Ubuntu 26.04 GNOME Wayland | PASS | Stable 0.7.1/0.7.2 DEB GUI FAIL; fixed candidate result below | Second virtual-output creation FAIL | Retained rounds `p2-linux-20261017-ubuntu-gnome-01` and `p2-linux-20261017-ubuntu-gnome-04` |

Additional packaged capture/software-encoder probes use the same 0.7.2 AppImage:

| Guest | Capture probe result | Retained lab run |
| --- | --- | --- |
| Bazzite 44 KDE Wayland | Two owned-output KWin/software H.264 probes PASS; topology restored | `20261017T134215Z-dbdd9007` |
| Omarchy 4.0.4 Hyprland | Two owned-output WLR/software H.264 probes PASS; topology restored | `20261017T134614Z-48efa5eb` |
| Fedora 44 KDE Wayland | Two owned-output KWin/software H.264 probes PASS; topology restored | `20261017T135014Z-068e1326` |
| Ubuntu 26.04 GNOME Wayland | FAIL: the portal stream negotiated 1280x720 and encoded one frame, but initialization did not finish within 25 seconds | `20261017T134816Z-6ce9c82d` |

Arch's capture probe remains untested. Debian KDE's helper prerequisite remains
unsupported; the Xorg package round did not run this Wayland-output probe.
The complete synthetic AV fixture passed in Fedora KDE, retained run
`20261017T142416Z-4f0897e1`, with 26 assertions and Chrome 155.0.8059.39.
Both connections showed changing rendered pixels and increasing decoded audio
energy. The consumed one-time code was rejected before requesting a fresh code
from the fixture's existing private clock control. Native OS input remained
disabled, SELinux remained Enforcing, all fixture process groups stopped, and
media/admission were released after both disconnects. VM teardown and baseline
verification passed. The gateway was built from the matching 0.7.2 source;
the host was the retained 0.7.2 AppImage.

| Synthetic media session | Decoded H.264 frames | Received Opus packets | Decoded audio energy |
| --- | --- | --- | --- |
| First connection | 124 → 338 | 72 → 778 | 0.00225 → 0.03183 |
| Second connection | 122 → 333 | 448 → 1153 | 0.01775 → 0.04637 |

The earlier AV round `20261017T141454Z-e58413af` passed its first media connection
but failed reconnect because its driver reused a consumed dynamic code. That
fixture error remains retained as FAIL. The driver now obtains fresh one-time
codes and checks replay rejection; production authentication was not changed.
The ordinary fixture exposes only private code-clock/status controls, while
restart/revocation controls still require `--pairing-control`.

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
is produced. A first local candidate preserves the released viewer/host executable
hashes and replaces only these VA runtime files plus their notices/metadata.
Published assets retain their original hashes; candidate acceptance is recorded
separately from the failing released binaries. Legacy native rollback on this
newer Mutter remains a recorded failure rather than an implied PASS.

The first native runtime comparison also exposed an independent host-launcher
failure: the launcher exported private libraries before running system utilities.
Ubuntu's newer coreutils required `LIBSYSTEMD_254`, while the private host copy
provided an older interface. `dirname`/`mkdir` failed and the host tried to create
`/runtime/sunshine`, preventing the loopback API from starting. The native launcher
now clears inherited private library paths first, resolves its root and creates
its isolated runtime directory with the system environment, then exports the
private host libraries immediately before execution. A second immutable candidate
adds this launcher change; both released executable hashes remain unchanged.
The first comparison remains a retained failed round, not a native GUI PASS.

The final native regression comparison passed in
`20261017T144430Z-3e40e885`. It reused the second immutable local DEB candidate,
SHA-256 `fb7cbdd334580acce96eab417a9c61c34c8663aba2710db289ce188e913eccfe`.
The viewer and host executable hashes match the released 0.7.2 payload; only
the pinned VA runtime/notices/metadata and native host launcher differ.

| Native comparison stage | Observed result |
| --- | --- |
| Legacy 0.7.1 installation | GUI SIGSEGV through `libva-wayland`/`vaInitialize`; expected negative control, product FAIL |
| Fixed 0.7.2 candidate upgrade | GUI and authenticated host API PASS; 30.02-second GUI observation; all four mapped private VA libraries match pinned SHA-256 |
| Rollback to legacy 0.7.1 | Same actual GUI crash; expected negative control, product FAIL |
| Uninstall after legacy rollback | Package removed; actual synthetic DeskPort configuration retained |

The procedure's PASS includes the two explicitly expected legacy failures. It
does not make legacy native rollback functional on this desktop. The preceding
comparison `20261017T143859Z-6c9cba31` passed the candidate API/GUI observation but
stopped at an incorrect fixture assumption that libva must print a version line.
That failed round remains intact; the final oracle verifies actual mapped files
and checksums instead. The first host-launcher failure also remains retained.
The candidate GUI screenshot was inspected in its isolated GNOME desktop; no
personal devices were present or connected. Focus and real input remain untested.

Final source validation passed `nix build --no-link --max-jobs 1 --cores 4` under
the canonical heavy reservation, CPU 0–3 and nice 15. A later offline check with
compilation disabled resolved the identical output for the final source tree:
`/nix/store/amm3xgis0kdy6xafp6zh5z0r5wkqrmrp-deskport-0.7.2`. VM-driver and
portable-entry-point changes are excluded from the Nix client source fingerprint
because those files are exercised by their dedicated checks, not built into that
binary. The retained records also include the two VA-file contract tests, local
guest-command refusal, and stricter capture-selection checks against the actual
logs. All 28 completed VM rounds passed baseline verification and owned teardown;
failed product/fixture scenarios remain separately marked.
