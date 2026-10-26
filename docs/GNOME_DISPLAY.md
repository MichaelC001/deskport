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

These are temporary session changes, not persistent monitor configuration.
They require an unlocked, running GNOME Wayland user session and the advertised
Mutter APIs. Login-screen access, unattended login and arbitrary compositor
versions are not implied.

## Validation

`scripts/test-linux-display.py HELPER --gnome` runs an owned headless compositor
with private D-Bus, PipeWire and configuration. Add `--gnome-native` to run the
full policy/recovery scenario there; use Python with PyGObject and the GTK 4/Gio
typelibs available. This headless route explicitly leaves interactive focus/input
and unavailable physical modes untested. `scripts/test-gnome-display.py`
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

The GTK input fixture checks Mutter's platform input path using guest-only
synthetic text. It does not qualify Sunshine transport input, physical GPUs,
hardware encoders, HDR, audio, clipboard or end-to-end latency. See the
[support matrix](PC_SUPPORT_MATRIX.md) for versioned evidence; release artifacts
and the user's activated version must be verified separately.

### Compatibility checkpoint — 2026-10-26

Implementation source: `59b8e324` (including the guarded-policy adapter imported
in `d495786f`), shared core unchanged at `505ae0ccd0a4ead4decb6eaba62585bf278e6dbe`.

| Runtime | Layer | Result |
| --- | --- | --- |
| GNOME 49.4 | Private headless Mutter/PipeWire session | PASS, 28 checks; all three policies, 11 capture/encoder probes, 20 alternating leases, EOF/SIGKILL, guardian recovery and foreign-output hotplug. Interactive input and unavailable physical 2x modes are explicitly untested. |
| GNOME 50.4 | Private headless Mutter/PipeWire session | PASS, the same 28 checks and boundaries. |
| GNOME 51.0 / Ubuntu 26.10 | Full QEMU/KVM desktop, software graphics | PASS, 30 checks; also guest GTK click/text input and restoration of a non-default physical 2x mode. |
| GNOME 50.1 / Ubuntu 26.04 | Full QEMU/KVM desktop, software graphics | FAIL, the stress scenario lost its Wayland compositor connection. Earlier runs also recorded compositor timeouts during foreign-output hotplug. The 50.4 result must not be generalized to this older build. |
| GNOME 51.0, forced PipeWire service loss | Separate full VM | FAIL for compositor recovery; see below. The normal workflow pass does not include this failure mode. |

The native helper tested above has SHA-256
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

## Compositor service failure

Stopping the guest PipeWire service during an active primary-only lease caused
GNOME Shell 51.0 itself to receive SIGSEGV in the Ubuntu 26.10 VM. The owner
exited and retained the recovery journal, but DisplayConfig stopped responding;
restarting PipeWire did not restore the desktop. This dependency-loss result is
a failed compositor recovery test, not a supported transparent reconnect path.
DeskPort cannot apply a layout while Mutter is unavailable and does not restart
the user's desktop session. Normal lease release, owner/guardian termination,
and capture-process restart are tested separately from compositor/service loss.
