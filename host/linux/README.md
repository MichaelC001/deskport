# Linux virtual display helper

On KDE, DeskPort owns one virtual output through a dedicated Wayland connection. KDE
Plasma 6.6 or newer is required for virtual custom modes. Only the UUID-named
output created by this connection is configured. Physical display modes, scale,
position and primary selection are untouched. Closing stdin, stopping the helper
or losing its Wayland connection releases the virtual output.

The helper retains the same output across stream reconnects and applies custom
modes through KDE output management. It acknowledges only the observed current
pixel mode and scale. A protocol timeout closes the helper, preventing a late
configuration result from being mistaken for a newer request.

The three protocol XMLs and generated client bindings are from the pinned
Sunshine plasma-wayland-protocols submodule. The XMLs carry their upstream
copyright/license notices (MIT-CMU for output management/device and
LGPL-2.1-or-later for screencast; see COPYING.LIB). Regenerate with
`wayland-scanner client-header` and `wayland-scanner private-code`.

On GNOME, `gnome-display.cpp` owns a Mutter ScreenCast session and a PipeWire
sizing stream. It verifies pixel modes and applies temporary virtual-output scale
while preserving the other logical monitor configurations. See
[Linux adaptive display](../../docs/LINUX_ADAPTIVE_DISPLAY.md).
