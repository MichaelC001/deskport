# Linux adaptive workspace

The Linux native/Nix host creates a dedicated virtual desktop on supported
Wayland compositors. A bound client can resize it over the existing authenticated
display-control connection. The same virtual output remains alive across video
reconnects. Stopping sharing, helper failure or compositor loss releases it.

## Backends

- **KDE Plasma / KWin 6.6+:** KDE screencast virtual output plus output-management
  custom modes. The packaged desktop entry grants only the helper's screencast
  protocol access. KWin permission checks remain enabled.
- **GNOME / Mutter:** Mutter ScreenCast `RecordVirtual`, with PipeWire format
  negotiation and temporary DisplayConfig scaling. The owned output is verified
  against the current monitor state. Sunshine attaches to that output's PipeWire
  object serial; it does not open a second screen picker. Tested with Mutter 50.4.
- Other desktops retain existing-screen portal capture and do not advertise
  adaptive-display support. An unavailable native backend reports an error rather
  than silently capturing another screen.

The common Portal API defines a `VIRTUAL` source, but availability and resizing
behavior depend on the compositor. It does not provide a universal arbitrary-mode
resize command. Backend-specific operations are kept behind the same helper
request/acknowledgment contract.

Sizes are bounded to 640×360–7680×4320 pixels, aligned to four. Both 1× and 2×
scales are supported when accepted by the compositor. Successful replies require
observed pixels and scale, not just successful API submission. Failed or timed-out
requests are reported; a lost helper stops capture instead of falling back to a
physical screen. Only one approved device can control the display.

This is an **extended virtual display**. Existing physical output modes, position,
scale and primary selection are preserved. It is not physical-display mirroring;
move the applications you want to use onto the dedicated display. Video may
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
identity, unchanged other outputs, EOF cleanup and crash cleanup. Set
`DESKPORT_TEST_HOST=/path/to/deskport-host` to include real software-encoder
capture at 1280×720@1× and 1668×2388@2×, plus refusal to capture another output
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
