# Hyprland display leases

DeskPort uses the pinned core's three display policy IDs. The Hyprland adapter
uses public [Hyprland runtime monitor commands](https://wiki.hypr.land/configuring/core/monitors/): `hl.monitor` through `eval` for Lua
configuration, and `keyword monitor` for legacy text configuration. It does not
edit configuration files, reload the user's configuration, stop the compositor,
or change pairing or input permissions.

| Policy | Runtime behavior |
| --- | --- |
| Mirror (0) | Originally enabled outputs mirror the owned remote workspace. |
| Turn off others (1) | Originally enabled outputs are disabled while the owned workspace remains enabled. |
| Extend (2) | Existing outputs and their layout are left unchanged. |

Hyprland has no global primary-display property equivalent to CoreGraphics or
Windows display topology. The dedicated output is the mirror source or sole
originally enabled output in policies 0 and 1. DeskPort does not force local
focus or synthesize input to imitate a primary-display flag. Hyprland itself
may change focus or pointer placement while mirroring, disabling outputs or
moving workspaces.
Outputs attached after admission are excluded from mirror/disable operations.

## Ownership and restoration

The helper starts a guardian outside its process group. The guardian holds a
per-compositor `QLockFile` under the private `XDG_RUNTIME_DIR`, recovers a pending
journal before reporting ready, and performs every compositor mutation. EOF
from its owner restores the topology even when the owner is killed. Control-command children are tied to the guardian death signal, so a delayed
command cannot survive it and create an unjournaled output. Killing the
guardian itself makes the helper fail closed and start journal recovery. If both
processes are killed, the next helper recovers the journal before admission.
A POSIX process-group SIGKILL of the owner does not kill the detached guardian.
A service-manager cgroup kill can terminate both; startup journal recovery covers
that case.

Before creating the output, the guardian atomically writes the attached output
snapshot, original mirror sources resolved from ephemeral IDs to names, and
occupied or persistent positive-ID workspace-to-monitor assignments. Repeated size/scale changes and
same-origin reuse keep that snapshot. A policy change within a lease fails.
The only admitted capture target remains the unique `DeskPort-<UUID>` output;
missing or incorrect geometry never falls back to a physical screen.

Physical rules bind a unique EDID description containing a nonzero serial,
rather than a connector name. The description selector includes the full DRM
connector boundary because Hyprland matches descriptions by prefix; serial 123
must not match 1234. Replacement devices do not inherit temporary rules, and the
original device can return on another connector. Standard software outputs use
their backend names. Missing or ambiguous physical identity retains extension.

Recovery matches stable identity, clears previous mirror relationships before
assigning a different source, restores sources before mirrors, and verifies mode, refresh rate, position, scale, transform, disabled
state and mirror source before deleting the journal. It rechecks topology and
workspace assignments after removing the owned source, since removing a source
can itself change compositor replication state. Surviving normal workspaces
with positive IDs are returned to still-attached original monitors. Empty transient workspace IDs
are not replayed: Hyprland can destroy and recycle them during migration.
Detached displays are skipped for observation, but their original runtime rule
is replayed so a later return cannot inherit DeskPort's disable/mirror rule.
No explicit focus, pointer-warp or input commands are sent. A different device reusing the same connector is not configured as the original;
workspace restoration also resolves the original hardware to its current connector.
Failed restoration retains the journal, retries, and reports an error. A later
authenticated lease can use an independently verified owned workspace while
local recovery remains pending; the existing layout warning is propagated and
the original journal is retained. A corrupt journal still refuses mutations. DeskPort
never removes a mirror source while its original outputs remain unverified.

Only originally enabled outputs are changed. Originally disabled displays,
newly attached displays, persistent monitor configuration and input settings
are outside the lease. Restoring observed output state does not restore an
underlying `preferred`/automatic monitor rule expression; an explicit user
configuration reload still uses the user's unchanged configuration files.
Named or special workspaces, application geometry, local focus and pointer placement are
not forcibly replayed. HDR/ICC configuration and physical link transitions need
separate hardware acceptance.

## Capability and failure reporting

The helper advertises the existing policy capability only when the original
monitor state can be safely enumerated, identified and snapshotted. Topology policies are
limited to ordinary 8-bit sRGB outputs with VRR off: the monitor-rule API cannot
preserve unexposed HDR/ICC/VRR rule attributes. Advanced color/VRR setups retain
extension without touching physical rules. HostManager propagates
that result instead of excluding every Hyprland host. A compositor without the
required snapshot information retains the legacy extended workspace path and
does not advertise three-policy support. The existing translated layout warning
also makes this legacy extension limitation visible. Explicit unsupported policies fail
before mutation. An IPC `ok` alone is insufficient: actual output state must
match before a successful resize is returned.

External display-management software and compositor configuration reloads can
conflict with a lease. An unverified capture target stops admission. Local-layout failures keep the
verified workspace usable with a warning and the original recovery journal.
DeskPort does not disable external tools or reload the user's desktop to force
its own rules to win.

## Reproducible checks

Run the fixture and core checks in the locked devShell:

```sh
nix develop -c python3 scripts/test-hyprland-display.py
nix develop -c python3 scripts/test-core.py
nix develop -c python3 scripts/test-host-lifecycle.py -- hyprlandCapabilityAndHostDeath
nix build --no-link
```

For native capture and restoration, use the existing isolated fixture with
matching `Hyprland`, `hyprctl`, `kwin_wayland` and `dbus-run-session` binaries:

```sh
DESKPORT_TEST_HOST=/path/to/package/libexec/deskport-host \
  python3 scripts/test-hyprland-display-live.py \
  --helper /path/to/package/libexec/deskport-display --lua
```

Repeat without `--lua` for legacy configuration. The fixture uses virtual KWin,
a private Hyprland instance, private XDG directories and a private D-Bus bus.
Temporary files live under the checkout's resolved build root; only a short
socket-path symlink lives in `/tmp`. Host probes disable input/audio and prefer
a private network namespace. They do not connect a real client or access the
user's desktop. Reserve the shared host build/compositor resources before
running these commands; do not run alongside another task's isolated desktop.

Fixture, full-package, isolated capture, and physical hardware results must be
reported separately. Software H.264 probing does not prove client frame delivery,
hardware encoding, HDR, audio, live input, latency, logout or reboot behavior.

## Verification record — 2026-10-17

| Layer | Observed result |
| --- | --- |
| Pinned core | C/C++, catalog, session graph, Seamless and Qt workspace contract checks passed; pins unchanged. |
| Isolated IPC | 68 checks passed, including Lua/text control, fractional scale/rotation, EDID replacement and connector return, transient workspace ID reuse, corrupt journals and process failures. |
| HostManager | Seven QtTest results passed across the selected capability/host-death, parent-crash, idle-admission and capture-descriptor cases. Legacy helpers without the capability field retain ID 0. |
| Linux package | `nix build --no-link` passed for x86_64 Linux; changed code/test inputs matched the worktree by SHA-256. The builder and sampled compiler used CPU 0–3 and nice 15, with at most four build jobs. |
| Native compositor | Hyprland 0.55.4 nested in virtual KWin 6.7.5 passed both Lua and legacy configuration with the final package helper and host. |

The package build used a temporary local resource-only hook for builder affinity,
priority and the IPC preflight; no toolchain or flake changes are part of this
implementation. Native checks covered all three policies, three pixel/scale
modes, 2× resize, later-output exclusion, output removal/recreation, persistent
workspace assignments, pre-existing mirror graphs, EOF, owner/guardian SIGKILL
and process-group SIGTERM. Each configuration completed eight WLR/software H.264
encoder probes and one missing-target refusal in private network namespaces.
Owned processes exited and both shared resource reservations were returned.

Physical DRM/EDID hotplug remains untested; its identity cases above use mocked
monitor records. Client frame delivery, hardware encoding, HDR/ICC/VRR, audio,
live input, latency, logout and reboot acceptance remain separate work.
