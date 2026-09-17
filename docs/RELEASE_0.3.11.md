# DeskPort 0.3.11 preview

This development release integrates the shared workspace core and the existing
Linux adaptive-display branch. macOS and KDE clients can choose virtual-primary
mirroring (default), virtual-primary with other previously active screens disabled,
or an extended client display. A connection snapshots physical output state once
and restores it after disconnect. Initially disabled and newly attached screens
are not taken over. Per-device address editing preserves the bound host identity.

GNOME retains its existing extended display behavior and does not advertise the
three-policy capability. Nondefault policies are refused on unsupported hosts.
Android gains saved address editing; its virtual-display transport is still pending.

Validation and publication results will be recorded after the release builds.
Physical GPU, touch, multi-monitor recovery and streaming remain user acceptance
checks after manual activation on pk4, wmn and mm4.
