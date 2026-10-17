# X11 independent workspace backend

This backend leases an **existing inactive xf86-video-dummy connector** and adds
owned dynamic modes to it through XRandR 1.3+. It extends that X screen to the
right of its active outputs. It does not create a DRM connector, a separate X
server, a new login session or a window-manager virtual desktop. Applications
must run on the selected X server and move to its new monitor rectangle.

The initial supported driver is deliberately narrow: every RandR output must be
in the xf86-video-dummy `DUMMY0`–`DUMMY15` family, with `DUMMY0` present. The chosen
`DUMMY1`–`DUMMY15` must exist, have no EDID or active CRTC, and have a free compatible
CRTC with identity transforms and disabled panning. Existing active outputs must
also have identity transforms and disabled panning; transformed source rectangles
are not accepted as independent monitor space. The X server's framebuffer must
fit both the existing workspace and the requested extension. `DUMMY0` is reserved
for that server's existing desktop.
Mixed drivers, Intel `VIRTUAL*`, NVIDIA, AMDGPU, modesetting, physical connectors,
Xvfb and XWayland are not supported by this implementation. A driver/output name
is a capability check, not a security boundary against another X client.

An administrator may already provide an isolated dummy-driver X server. Select
one of its idle connectors explicitly before launching DeskPort:

```sh
DESKPORT_X11_OUTPUT=DUMMY1 deskport
```

DeskPort does not install a driver, generate system Xorg configuration, restart
a display manager or select the user's physical HDMI/DP output. Leaving the
variable unset retains existing-screen sharing without advertising an adaptive
lease. An invalid explicit selection fails; it never falls back to a physical
screen. The X server and applications remain administrator/user managed. This
is useful on an already configured software Xorg session; it does not add a
virtual monitor to an ordinary AMD/NVIDIA desktop.

## Existing shared contract

The authenticated display lease and pixel bounds are the pinned core's existing
contract: 640×360 through 7680×4320, aligned to four. Native driver constraints
may reject a request. Only observed 1× pixels are acknowledged. **2× UI scale is
explicitly rejected before mutation.** XRandR transforms resample an image and do
not establish per-monitor application DPI; global Xft DPI/XSETTINGS are not
changed. Root framebuffer size is necessarily global; screen millimeters track
its growth to preserve screen DPI to integer-millimeter rounding. No shared wire
field, scale value, workspace arithmetic or core pin changes.

The optional topology-policy capability is not advertised. As with the existing
independent Mutter adapter, clients use the base lease. Requests containing a
policy are rejected before mutation. Mirror and exclusive policies are unavailable.
This backend is not feature parity with Wayland/macOS HiDPI workspaces.

## Ownership and recovery

A unique X selection serializes connector leases across processes, independent
of configuration directories. A separate guardian owns every mode/CRTC change;
EOF after helper death cannot race a still-running parent mutation. Before the
first change it writes a private recovery journal. EOF/disconnect disables only
the leased connector, removes its uniquely named modes and restores the original
root framebuffer when the other CRTCs and primary output are unchanged and the
current framebuffer dimensions and millimeters still match its owned state.
Concurrent user changes to global DPI are preserved.

A killed guardian leaves its journal. The next startup first checks server/output
identity and removes only modes carrying the saved lease token. It refuses an
output another X client has repurposed. If another layout changed, it releases
its connector and modes, archives the pending framebuffer journal and reports
a layout warning. A new independent lease remains available with a fresh current
snapshot; the older record is retried while idle and at startup only when its
original other-output layout and root-size conditions match. Changed outputs
are never overwritten. X11 server loss ends this X session's resources. An
ephemeral server cookie namespaces the journals, so a replacement server or another X screen never inherits a stale
resource identity; older records remain available for inspection. Recovery does
not restore application window positions.

Sunshine reads the atomically written capture descriptor, resolves the exact
RandR connector by name and checks its unique mode token, pixels, rotation and
CRTC exclusivity and a rectangle disjoint from every other active scanout.
Overlapping scanouts are mirrors and are refused. Current/pending RandR
transforms and panning are checked explicitly; image resampling is never
acknowledged as 1× UI scale. Both XGetImage and MIT-SHM revalidate that rectangle
before frames. Missing or mismatched outputs fail closed; numeric monitor-index
fallback and whole-root capture are forbidden for an owned lease. This backend does not
add input or accessibility session isolation. Linux input retains Sunshine's
existing uinput/seat route; choosing DISPLAY does not create or isolate an input
seat. A separately configured headless X server therefore requires its own
verified seat/input setup before interactive hosting. These isolated tests disable
all input and do not establish physical pointer/keyboard acceptance.

## Reproducible isolated checks

`scripts/test-x11-display.py /path/to/deskport-display` starts a private, TCP-disabled
Xorg server using a test-local config and an already available dummy module. It
uses Xorg's `-displayfd`, never the current user's DISPLAY. Set
`DESKPORT_X11_MODULE_PATH` to the dummy and Xorg module directories, and
`DESKPORT_X11_TEST_OUTPUT`/`TMPDIR` under the resolved build directory. On a shared
build host, reserve its compilation/test resource lock, inventory existing sessions
and record ownership before running this headless software-server test. It starts
no VM or physical GUI session. The script stops only its own server and helpers
on every exit.

The native test covers extension beside an unchanged baseline output, exact
resizing, honest scale/policy rejection, duplicate lease refusal, disconnect,
helper SIGKILL and guardian SIGKILL followed by journal recovery, unchanged
screen DPI, concurrent local edits with new-lease availability, concurrent
global-DPI preservation, and overlapping scanout refusal. Driver capability and
physical-hardware limitations above remain separate from this software-server
acceptance. Hardware encode, full video transport, input alignment, desktop-shell
placement, audio and clipboard require independent acceptance.

## Validation record — 2026-10-17

A native Linux `nix build --no-link` built the client, helper and patched host.
The pinned shared-core checks, 44 host-lifecycle cases and 172 binding cases
passed. The core Git and Nix pins remain unchanged.

The isolated native suite passed against Xorg 21.1.24 with xf86-video-dummy 0.4.1.
An independent observer checked five resizes, portrait modes, exact 1× dimensions,
unchanged state after 2×/policy rejection, release/recreation, EOF cleanup,
helper SIGKILL and guardian SIGKILL recovery. Local output edits and a concurrent
120-DPI change survived cleanup and a later lease; deferred recovery completed
only after the original baseline was restored.

The actual packaged host encoded software H.264 at 1280×720@1×,
1668×2388@1× and recreated 1920×1080@1×. Missing and overlapping outputs were
refused without root/physical fallback. This driver rejected transforms, so no
positive transform or 2× UI-scale acceptance is claimed. It accepted panning;
the packaged host refused to capture that as 1×, and an already panning baseline
refused adaptive startup without mutation. The test disabled input and ran host
probes in a separate network namespace. Every owned Xorg, helper, guardian and
host process stopped; the X socket and lock disappeared. No VM or physical GUI
session was used. Full client transport, real GPU drivers, hardware encode,
input/seat alignment, desktop-shell placement, audio and clipboard remain
unverified.
