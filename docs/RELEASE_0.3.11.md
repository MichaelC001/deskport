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

Published as [v0.3.11](https://github.com/keithxc/deskport/releases/tag/v0.3.11)
from `299a36c9aa2925178060330c81117568968d9f2a`. The final
[display CI run](https://github.com/keithxc/deskport/actions/runs/35180702096)
passed. Full pk4/wmn NixOS configurations built on pk4; the mm4 configuration
built from the signed public asset. All five GitHub asset digests matched.

The release includes a reconnect/restoration serialization fix discovered by CI.
The subsequent CI-only timer assertion fix does not change the packaged Mac
application sources. See the release's VERIFICATION.txt for detailed coverage.
Apple client 1.0 (5) is available to the existing Internal QA TestFlight group.
Physical GPU, touch, multi-monitor recovery and streaming remain user acceptance
checks after manual activation on pk4, wmn and mm4.
