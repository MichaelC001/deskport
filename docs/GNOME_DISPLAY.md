# GNOME virtual displays

DeskPort's GNOME Wayland host adapter uses Mutter's `ScreenCast.RecordVirtual`
and `DisplayConfig` APIs. A lease owns a virtual output and its PipeWire object
serial. It does not require a Shell extension or a dummy display plug. This is
an adapter to the shared session-display contract, not a separate policy model.

## Policies

| Policy | Behavior and admission requirements |
| --- | --- |
| Extension | Keep the existing physical layout and add the owned workspace. |
| Primary-only | Temporarily disable the original outputs and make the owned workspace primary. Restore the original layout on release. |
| Mirror | Group the original outputs with the owned workspace only when their existing pixel size and scale match the requested workspace and no rotation is required. |

Mutter cannot mirror an arbitrary portrait or differently scaled workspace
onto an unchanged physical mode through this API. Unsupported mirroring fails
before creating an output; it never silently becomes extension. Select
extension or primary-only for arbitrary client dimensions. A pre-existing
foreign virtual monitor prevents takeover policies. Outputs attached during
a lease are preserved; if Mutter changes the physical policy, DeskPort reports
a local-layout warning while retaining an independently verified workspace.

## Scaling

GNOME and KDE use the same pinned workspace policy and desktop-adjustment
setting. Both native adapters accept 1x/2x backing scales; fractional client
density is represented by a suitably sized 2x workspace rather than reducing
the desktop raster and enlarging it. For example, the shared 125% and 150%
client fixtures both produce a 3840x2160@2 workspace from their respective
2400x1350 and 2880x1620 drawable sizes. The logical desktop is 1920x1080.

Desktop adjustment retains that backing scale: larger values enlarge content
by reducing workspace dimensions, while smaller values provide more workspace.
Protocol bounds and four-pixel alignment still apply. At a fixed 1280x800@1
base, 150% requests 856x536@1 and 80% requests 1600x1000@1. The configured
adjustment is reapplied when a replacement capture session is admitted.
Extension preserves physical scaling; takeover restores the saved physical
mode/scale on release. Mirror still requires matching physical pixels and
scale, so arbitrary zoom cannot silently change an unchanged mirrored monitor.

## Ownership and recovery

PipeWire negotiation and DisplayConfig publication are asynchronous. Admission
waits for a uniquely identifiable new monitor, compares its complete
connector/vendor/product/serial identity, and records that identity before
changing physical outputs. A recycled `Meta-N` name alone is insufficient.
Capture targets the owned PipeWire object serial and refuses physical fallback.

An independent guardian holds an atomic, owner-only recovery journal under
`$XDG_RUNTIME_DIR/DeskPort`. Normal release first restores local control, closes
the owned stream, observes its removal, then verifies the original layout.
Owner death also triggers recovery. Guardian death terminates the owner and
retains the checkpoint for the next lease. Failed recovery retains the original
checkpoint and reports a warning; it does not overwrite it with a degraded
layout. GNOME cleanup has a separate timeout budget because monitor removal
and restoration can exceed the ordinary resize deadline.

After capture loss, the virtual CRTC may already have been destroyed while
DisplayConfig still advertises its output until Mutter's next idle dispatch.
DeskPort therefore skips pre-removal layout replay for a closed/failed stream;
the guardian waits for monitor removal before restoring the physical layout.
Reapplying the stale virtual monitor can crash Mutter's native renderer even
with the separate stream-lifetime patch installed.

The host supervisor closes authenticated controllers before replacing failed
capture/media processes. This is a transient transport loss; an explicit stop
still sends terminal `session-ended`. The native viewer retries graceful media
EOF only when its authenticated control channel also failed and remains
retryable. Temporary `unavailable` admission during an existing recovery can be
retried within the existing deadline. A replacement host requires fresh idle
admission: no stale lease is transferred and no automatic takeover is allowed.
If that authenticated replacement reports that no application exists to resume,
the viewer makes one launch attempt under its live admission and remaining
recovery deadline. Other launch errors are not converted into a fresh launch.

These are temporary session changes, not persistent monitor configuration.
They require an unlocked, running GNOME Wayland user session and the advertised
Mutter APIs. Login-screen access, unattended login and arbitrary compositor
versions are not implied.

## Validation

`tests/test-linux-display.py HELPER --gnome` runs an owned headless compositor
with private D-Bus, PipeWire and configuration. Add `--gnome-native` to run the
full policy/recovery scenario there; use Python with PyGObject and the GTK 4/Gio
typelibs available. This headless route explicitly leaves interactive focus/input
and unavailable physical modes untested. `tests/test-gnome-display.py`
runs in a disposable full GNOME VM with `DESKPORT_GNOME_TEST_SESSION=1` and
checks all policies, 1x/2x scaling, portrait/landscape, repeated leases, native
software capture, abnormal owner/guardian exits, pending recovery, foreign
outputs and physical-layout restoration. `--pipewire-loss` additionally tests
the guest's PipeWire service restart in a separate fresh VM.
The full VM fixture also needs `tests/pipewire-video-sink.c` compiled against
`libpipewire-0.3`, beside the helper or selected by `DESKPORT_GNOME_VIDEO_SINK`.
The headless harness builds this consumer itself. It represents another client
using ordinary RecordVirtual negotiation, including on GNOME 49 where the
newer fixed-mode virtual-monitor fixture is unavailable.

`tests/test-gnome-reconnect.py` runs only in an explicitly owned lab VM. It
seeds disposable certificate pins, launches the production host supervisor and
native viewer, observes three sustained rendered-frame samples, interrupts the
guest PipeWire service, then requires the same client and supervisor processes
to render three new samples from a fresh capture identity. It also verifies
unchanged compositor ownership and exact local layout restoration. Its fixed
1280x800 loopback workspace avoids client-window feedback when both endpoints
share one disposable desktop; the policy suite separately covers adaptation.
This is native media/control recovery, not pairing-UI or physical-network input
acceptance. Raw fixture logs and identities remain private run artifacts.

The GTK input fixture checks Mutter's platform input path using guest-only
synthetic text in a bounded child process. The controller writes `progress.json`
after each check/request so a native toolkit abort cannot erase the last result.
Full desktop tests first wait for the current Shell process's startup-complete
journal message. D-Bus availability alone precedes Shell's input-blocking
startup animation. The fixture uses an owned monitor stream for absolute pixel
input, checks the resulting logical coordinates and window scale, and repeats
click/text acceptance in three separate GTK processes. Add
`--workspace-fixtures shared/deskport-core/fixtures/workspace.json` to exercise
the standard, HiDPI, 125%/150% client-density and portrait cases against real
Mutter outputs and software capture without duplicating workspace arithmetic.
It does not qualify Sunshine transport input, physical GPUs,
hardware encoders, HDR, audio, clipboard or end-to-end latency. See the
[support matrix](PC_SUPPORT_MATRIX.md) for versioned evidence; release artifacts
and the user's activated version must be verified separately.

### Compatibility checkpoint — 2026-10-26

Original adapter source: `59b8e324` (including the guarded-policy adapter imported
in `d495786f`); production recovery follow-up: `21ab78c0`, with fixture
`1160e6dd`. Shared core is unchanged at `505ae0ccd0a4ead4decb6eaba62585bf278e6dbe`.

| Runtime | Layer | Result |
| --- | --- | --- |
| GNOME 49.4 | Private headless Mutter/PipeWire session | PASS, 28 checks; all three policies, 11 capture/encoder probes, 20 alternating leases, EOF/SIGKILL, guardian recovery and foreign-output hotplug. Interactive input and unavailable physical 2x modes are explicitly untested. |
| GNOME 50.4 | Private headless Mutter/PipeWire session | PASS, the same 28 checks and boundaries. |
| GNOME 51.0 / Ubuntu 26.10 | Full QEMU/KVM desktop, software graphics | PASS, 30 checks; also guest GTK click/text input and restoration of a non-default physical 2x mode. |
| GNOME 50.1 / Ubuntu 26.04 | Full QEMU/KVM desktop, software graphics | PASS, 30 checks after fixing the GTK fixture lifetime; see the diagnosis below. The earlier failed reports remain retained. |
| GNOME 51.0, distribution Mutter | Separate full VM, forced PipeWire loss | FAIL: confirmed compositor use-after-free; retained as the negative control. |
| GNOME 51.0, patched Mutter `51.0-1ubuntu3` | Full QEMU/KVM desktop, software graphics | PASS, 54 checks: normal workflow plus service stop and SIGKILL in all three policies, restoration without compositor restart and fresh owned capture. |
| GNOME Shell 51.1 / patched Mutter 51.1 | Full QEMU/KVM desktop, software graphics | PASS, 72 checks: shared 1x/2x and fractional-density workspace cases, nine native scale/input configurations with three independent windows each, all display policies, physical restoration and six PipeWire-loss cases. |
| GNOME 51.1 native client and host | GameStream over guest loopback | PASS, eight cases: stop/SIGKILL in all three policies plus 150%/80% desktop adjustment; same client/supervisor, fresh rendered frames and capture identity, unchanged compositor, exact shutdown restoration. |
| Mutter 51.1 source | Compiled dispatcher regression | PASS for the lifetime fix: 1,000 closes; original source fails as the negative control. |

The original `59b8e324` native helper has SHA-256
`6862dcf929211df4d6e3bb7501bb9d580d18545f1b8ce1699ab91045901a50a5`;
the fixed Nix Sunshine executable has SHA-256
`3670aaa1953fbd216c4ac961039102b8e8019eb408c05eb7f9a3bb324e8fb192`.
These identify development binaries, not published installers. All isolated
sessions were stopped; VM teardown and immutable baseline checks passed.

Managed Nix run `gnome-compat-final02` built the complete desktop application
and passed executable, desktop-identity and isolated CLI startup checks. Its
clean archive snapshot `49869ce6` maps to source `59b8e324` and records the
correct core revision above; the source remained unchanged throughout the run.
Host lifecycle passed 47 tests, control binding passed 172 tests, and the
shared-core suite passed. No release, installation or activation was performed.

The portal regression extracts and compiles the actual destructor from both
vendored source archives. The old code closes a foreign fd 0; the patched code
preserves it for 1,000 unused-portal lifetimes and still closes an opened portal's
own fd. This test also verifies patch application twice and is included in Linux CI.

## GNOME 50.1 stress diagnosis

The original controller opened GTK for the input check, destroyed the window,
then kept GTK's Wayland connection alive while running blocking capture and
topology operations. Protocol tracing showed accumulated output add/remove
events being dispatched later, and Mutter disconnected this client. A debugger
attached before reproduction confirmed that GNOME Shell remained alive; the
lost GTK connection was not evidence that Mutter had crashed.

The GTK check now runs in a separate process that exits after its input and
cleanup checks. The same unchanged Ubuntu 26.04 baseline then passed all 30
checks, including 20 alternating leases and both foreign-output hotplug orders
(lab run `20261026T033952Z-61f5da03`). This corrects the earlier interpretation;
it does not turn that failed fixture run into a pass or alter its evidence.

## Compositor service failure and source fix

With the original distribution compositor, stopping the guest PipeWire service
during an active primary-only lease caused
GNOME Shell 51.0 itself to receive SIGSEGV in the Ubuntu 26.10 VM. The owner
exited and retained the recovery journal, but DisplayConfig stopped responding;
restarting PipeWire did not restore the desktop. This dependency-loss result is
a failed compositor recovery test, not a supported transparent reconnect path.
DeskPort cannot apply a layout while Mutter is unavailable and does not restart
the user's desktop session. Normal lease release, owner/guardian termination,
and capture-process restart are tested separately from compositor/service loss.

The follow-up debugger trace resolves the crash to
`meta_stream_source_virtual_dispose()` after `pipewire_loop_source_dispatch()`
emits `closed`. The signal handler can release the last stream reference before
GLib releases its temporary source reference. Source disposal then follows a
borrowed pointer into the freed stream. The included
[Mutter patch and explicit integration instructions](../host/linux/patches/README.mutter.md)
retain the stream across signal emission. A compiled test of the actual 51.0
and 51.1 dispatchers fails with the original code and passes 1,000 close cycles
with the patch. This is a compositor patch; a DeskPort-only update cannot apply it.

Follow-up source `4f37c146` passed 54 checks in lab run
`20261026T035448Z-f735cba7`. The patched compositor library was built from Ubuntu
source package `51.0-1ubuntu3`; its SHA-256 is
`c2f32176dd928ffcda3b0a63c92367b31e6102be034df811c0714809b3a51842`.
The pristine source tarball SHA-256 is
`5d28f3ae225692428fcafb96500d673f34328b698b86960c9c1460d0b1d983b3`.
Only the disposable guest loaded this library. Baseline verification and VM
teardown passed; no personal desktop was changed.

The six loss cases cover extension, primary-only and matching-mode mirror under
both orderly service stop and SIGKILL. Each verifies owner termination, unchanged
Mutter D-Bus ownership, exact physical restoration, journal removal and a new
lease with real software capture. This qualifies recovery and fresh capture,
not uninterrupted transport or automatic client reconnection across service loss.
The opt-in Nix overlay preserves existing patches and rejects versions other
than the reviewed 51.0/51.1 sources. Its selection and system activation remain
separate from building or installing DeskPort.

Managed Nix run `gnome-recovery-final02` built the final DeskPort source
`4f37c146` and passed executable, desktop-identity and isolated CLI startup
checks. Its clean snapshot `2aa8cd10` has the identical Git tree
`f2b52d8bc53cc6cfb4193a77694ce2d9cfe4ea26`, with core `505ae0cc` unchanged.
The build does not implicitly include or activate the compositor overlay.
The final `4f37c146` harness was also rerun against private GNOME 49.4 and
50.4 sessions: both passed all 28 checks and verified owned-process teardown.

## GNOME 51.1 full-desktop and native recovery follow-up

Lab run `20261026T073832Z-489ff23b` passes all 72 desktop checks and eight
valid native streaming cases. The scale/input checks cover nine configurations
and 27 independent GTK windows. The desktop-adjustment cases preserve
856x536@1 at 150% and 1600x1000@1 at 80% before and after PipeWire recovery.
Recovery timing includes three positive rendering samples after the fault and
is not a first-frame latency measurement.

The first batch used an invalid 0.75 adjustment in its last case and retained a
nonzero batch exit. That value is absent from the shared KDE/GNOME catalog, so
the client correctly fell back to 1.0. The failed fixture result is preserved;
the corrected 0.8 case was rerun in the same owned desktop and passed. The
separate corrected acceptance record includes only the eight supported cases.

The same production helper was then regressed in the older full desktops:
GNOME 50.1 passes 30 checks in run `20261026T075456Z-545916bc`, and patched
GNOME 51.0 passes 54 in `20261026T075742Z-ddf14153`, including all six service-loss
cases. The upstream compositor patch remains necessary for the 51 series.
Private GNOME 49.4 and 50.4 regressions each pass 28 checks with the same
production helper and updated harness; all owned child processes exited.
Their initial setup failures (omitted probe protocol files and a Nix-expanded
socket path longer than Unix permits) are retained separately from the final
passing runs. All three full-desktop guests were stopped, teardown passed,
and their immutable baseline hashes were unchanged.

The full 51.1 desktop exposed a second compositor crash after the lifetime patch:
the failed stream had already destroyed its virtual CRTC, but DisplayConfig
still advertised that monitor. Replaying the layout during this interval reached
`meta_renderer_native_create_view()` with the stale CRTC. Source `eb9a042f`
defers restoration to the guardian after removal when capture has failed.
The original failing VM and debugger evidence remain retained.

Native loopback streaming then exposed two independent recovery gaps. Source
`9f291046` closes authenticated control before the supervisor replaces media,
preserves bounded retries for transient admission unavailability, and distinguishes
capture-loss EOF from an explicit terminal stop. Source `21ab78c0` handles a fresh
Sunshine process with no resumable application: its precise authenticated 503
reply permits one launch under the new admission. Explicit stop, denied trust
and occupied sessions still cannot trigger automatic takeover.

Two fixture failures were also diagnosed without attributing them to DeskPort:
starting monitor changes before Shell's startup animation completes can strand
its transparent reactive cover pane over the desktop; the full-desktop harness
now waits for the current Shell process's startup-complete journal record.
Rapid synthetic PipeWire faults can exhaust systemd's five-starts-per-ten-seconds
limit; independent injected faults now have a five-second interval. The test
does not disable the unit protection or reset its failure counter.

The managed Nix build `gnome511-reconnect03` built production source `21ab78c0`
and passed executable, desktop-identity and isolated CLI startup checks. Its
clean snapshot `ec50981b2973dc3fd7709f8c75b54cde1aeae029` has the identical tree
`bb02c65cd5020692aff06edc95f38ac663dcb6f2`; shared core remains `505ae0cc`.
The tested application launcher SHA-256 is
`852f02b63eb08a3c5b9014c45ca8f8d7b86dd7429486ec597307ae8d20d84b39`, and
the native display launcher SHA-256 is
`29815fd807dddd56e9722555bfc655aeaf7869e61196b274f7c85455a8dc4ca8`.
Control binding passes 176 tests, host lifecycle passes 47, and shared-core tests
pass. The actual patched Mutter 51.1 dispatcher passes 1,000 close cycles;
the original dispatcher remains a failing negative control.

The 51.1 guest uses upstream GNOME Shell and Mutter source builds on an isolated
Ubuntu 26.10 baseline, with the explicit Mutter lifetime patch. It is not a
claim that the baseline distribution ships 51.1. The verified upstream tarball
SHA-256 values are `c1eb08d9651a567ed5d9b2791371f1898e657cf45e75e1d88adefbab807977d6`
(Shell) and `0d311d87901dbcb94518fb89304febe30852ce5d8a4befa9f87e85c68eceefde`
(Mutter). No installed user desktop, published release or system activation is
part of this acceptance.
