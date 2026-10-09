# Linux virtual display helper

Output enumeration uses the KWin 6.7 device registry when available and retains
the individual device globals used by KWin 6.6. Both removal paths end the owned
lease without falling back to a physical capture target.

On KDE, DeskPort owns one virtual output through a dedicated Wayland connection. KDE
Plasma 6.6 or newer is required for virtual custom modes. The UUID-named virtual
output becomes primary, and physical outputs enabled before connection mirror its viewport. Physical
pixel modes, scale and rotation are preserved. An independent recovery process
restores the original physical enablement, modes, scale, rotation, positions,
replication sources and output order when the owner exits,
including SIGKILL. It is armed before the first layout change. Closing stdin, stopping the helper
or losing its Wayland connection releases the virtual output.

Full client disconnection removes the virtual output before restoring the physical
snapshot. The helper remains available and takes a fresh snapshot before recreating
the output for a later connection. Idle recovery is disarmed to preserve subsequent
local edits. Video-only reconnects retain the output within a display-control session.
Custom modes use KDE output management. It acknowledges only the observed current
pixel mode and scale. A protocol timeout closes the helper, preventing a late
configuration result from being mistaken for a newer request.

KWin 6.7 derives CVT custom timings, so their advertised refresh can differ from
the requested 60 Hz. The helper selects the closest advertised refresh while
requiring exact pixels and scale. With output-management v21, CVT also rounds
custom widths to multiples of eight. If no exact mode is available, the helper
sequentially replaces only its own virtual output using an exact initial mode,
retaining the admitted output name, policy and original recovery snapshot.
Sunshine reopens the same owned name after the PipeWire stream disconnects.
A rounded mode is never acknowledged as the requested size.

The three protocol XMLs and generated client bindings are from the pinned
Sunshine plasma-wayland-protocols submodule. The XMLs carry their upstream
copyright/license notices (MIT-CMU for output management/device and
LGPL-2.1-or-later for screencast; see COPYING.LIB). Regenerate with
`wayland-scanner client-header` and `wayland-scanner private-code`.

On GNOME, `gnome-display.cpp` owns a Mutter ScreenCast session and a PipeWire
sizing stream. It verifies pixel modes and applies temporary virtual-output scale
while preserving the other logical monitor configurations. See
[Linux adaptive display](../../docs/LINUX_ADAPTIVE_DISPLAY.md).

2026-09-17: Failed local restoration releases the old lease and retains its
baseline for later recovery. Subsequent clients can recreate and verify an
independent workspace. Physical-layout policy failures become recorded warnings
when workspace pixels/scale are correct. Diagnostic layout snapshots are saved
under `$XDG_DATA_HOME/DeskPort/display-layout-events.jsonl` (default
`~/.local/share/DeskPort`). Wayland disconnects and unverified modes still fail.
