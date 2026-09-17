# Per-device virtual screen policy

2026-09-17: Add three session policies and make saved address editing available
directly from Device settings. The policy is independent of automatic resolution;
fixed-resolution sessions acquire the same display lease at their configured size.

| Mode | During the connection |
| --- | --- |
| Primary screen and mirror others (default) | The client workspace becomes primary; originally active displays mirror it. |
| Primary screen and turn off others | The client workspace becomes primary; originally active displays are disabled. |
| Use client as an extended screen | The original primary and layout are preserved. |

The protocol is defined in the pinned core's `protocol/SESSION_DISPLAY.md`.
`hello.meta.displayPolicy=1` gates the optional request field. Legacy clients use
the default mode. Unsupported non-default choices and policy changes within a
lease are rejected. Trust, exclusive ownership, frame bounds and heartbeat expiry
remain enforced by the existing control channel. Invalid or unsupported modes still fail before mutation. A verified independent
workspace can continue when only local layout operations fail; the host records
the deviation and retains recovery for later repair. Incorrect capture modes
still fail.

## Recovery

The macOS helper journals stable display UUIDs, enabled/disabled state, primary,
origin, mirror source, logical/backing mode and refresh rate before changing the
session topology. Resize requests retain the original snapshot. Displays attached
after the snapshot are excluded from mirror/disable operations.

Actual controller disconnect schedules restore after 250 ms, waiting for any
pending mode request. Video renegotiation keeps the controller and does not restore
the desktop. Lost transport heartbeats expire after the existing 20-second lease
timeout. Helper setup failures, EOF/signals and startup recovery also restore the
snapshot. Missing displays are skipped; configuration failures retain the journal.
The journal is cleared only after a later observation confirms the restored state.
Recovery retries run on the helper event loop without prompting the user.

Disabling screens uses a runtime-resolved private CoreGraphics operation, like the
existing virtual-display implementation. It is rejected before topology mutation
if either display enablement or disabled-display enumeration is unavailable.
No permanent display preferences are written. Automatic restoration does not
guarantee an invisible physical mode transition; hardware/compositor acceptance is
separate. Rotation, color profiles, brightness and application window placement
are not modified by this implementation.

## Platform scope

Qt desktop clients and the Apple native client send the policy to macOS and
KDE hosts. The Linux adaptive-display branch is integrated in this development
release. KDE snapshots enabled state, modes, scale, transform, position, priority
and replication source; it verifies restoration before disarming its independent
recovery process. GNOME preserves the existing extended display behavior and
does not advertise the three-policy capability. Nondefault choices are refused.
Android address editing is implemented in the native-client repository; Android's
authenticated virtual-display transport is a separate missing adapter.

## Verification

`scripts/test-session-topology.py` runs the production macOS topology header against
an isolated in-memory display service with ASan/UBSan. It covers all three modes,
snapshot retention, initially disabled screens, hotplug, unavailable enablement,
failed restoration and recovery from the persisted journal. It never configures
the user's displays. Run the shared vectors, binding suite, desktop state suite and
QML UI suite as described in `SHARED_CORE.md`.

Before Linux branch integration, the full Linux ARM64 package attempt failed in the unchanged Sunshine
dependency (`cc1plus` killed). This does not establish an OOM cause or validate the
complete Linux package. Native/client-only builds and physical acceptance are
reported separately.

Pre-integration local checks: 45 desktop binding/display cases (including rejection of a
policy change within a lease), 9 desktop-state cases, 18 QML UI cases, macOS native
desktop build and separately compiled display helper, ASan/UBSan topology tests,
Apple parser/capability and 12 TLS cases, strict/default workspace lifecycle, Apple
simulator build and Android Debug/address tests passed. iPad passed address editing
and, after correcting fixed-size preview handling, all three policy/workspace/keyboard
checks; iPhone passed those three plus address editing. These UI cases are offline.
A Linux ARM64 client-only build passed with the bundled-host installation hook
omitted; this is not full package or deployed behavior acceptance.

## Released validation — 2026-09-17

The 0.3.11 preview integrates KDE policy support and preserves GNOME extension.
The complete NixOS x86_64 package and both pk4/wmn system configurations built
on pk4. All three policies passed isolated KDE lifecycle checks; KDE/GNOME
software capture passed. The final display CI, including delayed restoration
and queued-controller cancellation, passed. The notarized Mac assets and mm4
system build passed; activation and physical-device acceptance remain user-owned.

## Recovery availability update — 2026-09-17

Reason: a local restoration failure blocked subsequent clients even though the
virtual capture display could be configured independently. macOS now detaches
mirror sinks before looking up their native modes and ignores legacy references
to its own workspace as an original physical mirror source. Missing saved modes
retain recovery instead of submitting incomplete transactions. A new lease
cancels old idle callbacks and verifies its own pixels and scale. Local primary,
mirror and disable failures are recorded without rejecting a correct workspace.
Idle retries are bounded so they do not continually overwrite later manual edits.

KDE clears the old lease even when restoration fails after output removal. It
retains the original baseline in its independent recovery process, recreates the
workspace and verifies enabled state, independent replication, transform, pixel
mode and scale. Local recovery/policy failures are recorded while the remote
workspace remains usable. Genuine Wayland connection loss still terminates the
helper; a compositor timeout is not treated as a verified mode.

Both adapters append time, reason, policy and original/observed layout to
`display-layout-events.jsonl` under the user's DeskPort data directory. One
previous log is retained at rotation (1 MiB); no screen or clipboard content is
included. macOS also keeps its original `display-recovery.json` journal. Linux
recovery retains its baseline in the separate recovery process and records a
failed final attempt for manual repair. Detached and added displays can be seen
by comparing the original and observed snapshots.

Validation covers the production macOS helper with an isolated WindowServer
adapter and KDE in an isolated real compositor, including injected local restore
and policy failures, different-policy reconnects, correct workspace dimensions,
recorded recovery targets and successful restoration after removing the fault.
These tests do not establish physical streaming or touch acceptance.
