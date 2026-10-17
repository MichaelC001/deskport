# Linux adaptive workspace

The Linux native/Nix host creates a dedicated virtual desktop on supported
Wayland compositors. An explicit [X11 software-Xorg backend](X11_ADAPTIVE_DISPLAY.md)
leases an existing inactive dummy-driver connector at 1×; it does not add a
connector to ordinary physical-driver X11 sessions. A bound client can resize it over the existing authenticated
display-control connection. The output stays alive during an active display-control session, including video
reconnects. Full disconnection removes it after a short grace period; a later
connection recreates it. Sharing startup leaves the output absent until an authenticated display lease
admits a client; capture/encoder probing waits for that owned target. This lifecycle requires a
DeskPort client with display control.

## Backends

- **KDE Plasma / KWin 6.6+:** KDE screencast virtual output plus output-management
  custom modes. The packaged desktop entry grants only the helper's screencast
  protocol access. KWin permission checks remain enabled. During a client session the virtual
  output becomes primary and previously enabled physical outputs mirror it.
  Previously disabled outputs stay disabled. A snapshot records enablement, modes,
  scaling, rotation, positions, order and replication before each session.
  Disconnection removes the virtual output before restoring that snapshot; an
  independent recovery process also restores it after helper process death.
  Recovery is disarmed while idle so later local layout edits are not overwritten.
  KWin 6.7 uses the device registry instead of individual output globals. Its CVT
  custom modes may round refresh rates and widths; when no exact pixel mode is
  available, DeskPort sequentially replaces only its virtual output with an exact
  initial mode, preserving the admitted name and recovery snapshot. Capture may
  briefly reconnect to that same owned name; physical fallback remains forbidden.
- **GNOME / Mutter:** Mutter ScreenCast `RecordVirtual`, with PipeWire format
  negotiation and temporary DisplayConfig scaling. The owned output is verified
  against the current monitor state. Sunshine attaches to that output's PipeWire
  object serial; it does not open a second screen picker. Tested with Mutter 50.4.
- **Hyprland / Omarchy:** a dedicated headless output through public Lua/text
  runtime monitor APIs, with direct WLR capture. Development sources implement
  the shared mirror, turn-off-others and extension policies behind native
  capability checks. A locked guardian journals original topology and normal
  workspace assignments, excludes later outputs, verifies restoration and
  handles helper/host failure. Stable physical EDID identity, ordinary 8-bit
  sRGB and VRR-off state are required for physical topology leases; limited
  setups retain extension with a warning. See [Hyprland display leases](HYPRLAND_DISPLAY.md).
- **X11 / xf86-video-dummy:** explicitly selected inactive `DUMMY1`–`DUMMY15`,
  exact dynamic modes and an independent extension at 1×, with EOF/crash recovery.
  2× UI scaling and topology policies are unsupported and explicitly rejected.
  No Xorg/driver installation or physical output fallback. See the X11 document.
- Other desktops retain existing-screen portal capture and do not advertise
  adaptive-display support. An unavailable native backend reports an error rather
  than silently capturing another screen.

The common Portal API defines a `VIRTUAL` source, but availability and resizing
behavior depend on the compositor. It does not provide a universal arbitrary-mode
resize command. Backend-specific operations are kept behind the same helper
request/acknowledgment contract.

Sizes are bounded to 640×360–7680×4320 pixels, aligned to four. Wayland backends
support 1× and 2× scales when accepted by the compositor; the X11 backend accepts
only observed 1×. Successful replies require
observed pixels and scale, not just successful API submission. Failed or timed-out
requests are reported; a lost helper stops capture instead of falling back to a
physical screen. Only one approved device can control the display.

**KDE uses virtual-primary mirroring while a client is connected.** The physical screens
show the same workspace as the client; differing aspect ratios may add borders.
Disconnecting restores the physical layout captured before the connection.
This does not promise restoration of individual application window positions.
Monitor hotplug or manual layout edits during sharing are not yet acceptance-tested.

**GNOME still uses an extended virtual display.** Existing physical output settings
remain unchanged; move applications onto the dedicated display. Arbitrary-client-mode
physical mirroring on Mutter is not implemented by this preview. Video may
briefly reconnect during mode changes. An active logged-in Wayland session,
PipeWire and the existing Sunshine input permissions are required. This does not
implement a GNOME login-screen service or automatic session login.

## Validation

`nix build` builds both the client and helper plus the patched host. Run the real
compositor tests with `nix develop -c python3 scripts/test-linux-display.py
/path/to/deskport-display`; add `--gnome` with `gnome-shell` available, or set
`DESKPORT_GNOME_SHELL` to its executable. Tests create isolated D-Bus, Wayland,
PipeWire, configuration and data directories; they do not connect to personal
hosts or inject input. An independent Wayland observer checks modes, output
identity, physical pixel modes, EOF cleanup and crash cleanup. KDE additionally
checks mirrored outputs, virtual-primary priority and exact physical-policy
restoration. Add `--disabled-output` to cover a previously disabled panel.
Both backends check disconnect removal and recreation; GNOME checks unchanged
other outputs. Set
`DESKPORT_TEST_HOST=/path/to/deskport-host` to include real software-encoder
capture at 1280×720@1×, 1668×2388@2× and 1920×1080@1× after recreation, plus refusal to capture another output
after the owned display disappears.

The 0.3.6 Linux preview passed these capture checks and 12 successive mode/scale
changes on both KWin 6.6.6 (permission checks enabled) and GNOME 50.4. Host
lifecycle and binding suites each passed 27 tests; the UI suite passed 18 tests.

`nix develop -c python3 scripts/test-host-lifecycle.py` checks host supervision,
resize rejection/timeouts and capture-descriptor cleanup. `--binding` checks the
existing authenticated control path. Hardware encoding, iPad rotation, text size,
touch/mouse alignment and reconnect behavior still need real device acceptance.

## Upstream interfaces

- [Portal ScreenCast](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [KWin virtual output implementation](https://github.com/KDE/kwin/blob/Plasma/6.6/src/plugins/screencast/screencastmanager.cpp)
- [Mutter ScreenCast interface](https://github.com/GNOME/mutter/blob/main/data/dbus-interfaces/org.gnome.Mutter.ScreenCast.xml)

Both native compositor protocols are implementation-specific. Runtime capability
checks and explicit errors are intentional; this document does not promise
compatibility with every historical or future compositor version.
