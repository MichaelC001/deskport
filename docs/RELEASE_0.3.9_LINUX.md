# DeskPort 0.3.9 Linux display lifecycle preview

Preserve physical screen policy around remote sessions. On KDE, only screens
that were enabled before connection mirror the virtual primary; a disabled
internal panel stays disabled. Capture a fresh physical layout before each
connection, including enabled state, mode, scale, rotation, position, priority
and replication source.

Full disconnection removes the virtual output, then restores the physical
snapshot. Sharing keeps listening, and the next connection recreates the virtual
workspace. GNOME also removes and recreates its extended virtual output, updating
the PipeWire capture identity. Idle cleanup no longer replays stale physical
settings after the user changes their local layout. Adaptive client resolution
and video-only reconnects within an active display-control session are preserved.

This is a Linux Nix prerelease. macOS remains on 0.3.5; no mobile update is
required. GNOME physical mirroring is still outside this preview. Individual
application window placement and monitor hotplug during a session are not covered
by physical-layout restoration.

Validation uses isolated KWin and Mutter sessions, including disabled-output
preservation, repeated resizes, disconnect removal, reconnect capture, and
EOF/SIGKILL recovery. Physical-device reconnect and local-layout acceptance
remain user checks after activation.
