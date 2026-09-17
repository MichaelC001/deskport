# DeskPort 0.3.13 development prerelease

Switching clients after a failed local monitor restore can now establish the
new client's correctly sized workspace. macOS separates mirror sinks before
recovering native modes and retains unavailable-mode recovery. KDE releases
stale lease state and recreates its removed output after a restore failure.

Local primary/mirror/disable failures are recorded with timestamps and original
and observed layouts, without blocking a verified remote capture target.
Workspace dimensions, scale and independence remain mandatory. Invalid and
unsupported policies still fail before mutation. macOS idle recovery retries
are bounded; KDE preserves its recovery baseline across client handoff.

Targets: notarized macOS arm64 and NixOS x86_64. Validate client handoff, resizing,
local-display changes and reconnects after activating through mynix. Physical
streaming and iPad interaction remain user acceptance checks.
