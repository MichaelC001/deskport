# CPU and adaptive resize measurement

## 0.2.1 prerelease (2026-09-13)

This P0/P1 release reduces resize settling from 900 to 300 ms, wakes the
control worker on requests instead of polling every 50 ms, and confirms the
actual virtual display mode immediately followed by a 100 ms stability check.
Bounded retries, mirroring checks and rollback remain. An already matching OS
mode avoids reapplying display settings. A continuation reads the retained
window's latest dimensions before requesting the mode; subsequent changes are
handled by the next serialized transition, not overlapping requests.

Successful decoder probes are reused only within one initialization and its
hidden window, keyed by display, selection, codec, dimensions and frame rate.
Failures are not cached. Nothing is cached across sessions or resolutions; the
real streaming decoder still initializes normally. This deliberately avoids
assuming that an old decoder supports a newly requested size.

The transition spinner still pumps input/window events on every call, but paints
at most once per 80 ms unless its size changes. This is a transition CPU change,
not a change to active-stream pacing or static-frame encoding.

P2 (static frames and display queues), P3 (video-only reconfiguration), and P4
(process consolidation) are deferred. The existing stop/resume fallback remains.

## Measurement

Identify the exact viewer and its own host/helper PIDs. Never include a standalone
Sunshine instance in the totals. The sampler reads cumulative CPU time and RSS:

```sh
python3 scripts/measure-performance.py --pid 123 --pid 456 --seconds 120 \
  --label static-2560x1440-hevc-60 --output /tmp/deskport-static.json
python3 scripts/measure-performance.py --log /tmp/deskport-client.log \
  --output /tmp/deskport-resize.json
python3 tests/performance-parser.py
```

CPU is per core (100% is one full core), weighted over measured intervals. RSS
is not unique memory. Missing PIDs are missing samples, not zero usage. Use at
least two-second intervals because process CPU counters have finite resolution.
Do not compare different PIDs across restarts. Keep private logs outside Git.

Resize logs include SDL monotonic ticks for last observed size, stop begin/end,
continuation, mode request/ready, codec probe begin/end, resume request/response,
decoder setup and first FFmpeg render submission. The parser uses the latest
observation and requires a matching frame size. Ticks belong to one process;
never subtract timestamps from different machines. `first-render-submit` means
submission to the rendering backend, not physical scanout, visible correctness,
or input readiness. Non-FFmpeg backends do not emit that completion marker.
The host helper adds `modeElapsedMs` to its control response for local diagnostics.

A pre-upgrade live CPU observation was collected, but content was uncontrolled
and builds were running. It is not a controlled performance baseline. Older
installed builds lack the new stage markers, so no pre-upgrade full stage timing
is claimed. For a strict timing A/B, use identical workload and settings on old
and new releases with external timestamps, or an instrumented old build.

After both machines switch, verify actual running versions and service restart
counts. With background builds stopped, compare fixed static text, scrolling,
video, and rapid resizing separately. Keep resolution, codec, frame rate,
bitrate settings and duration identical. Report CPU, render FPS, network drops,
queue drops and resize median/P95, not just averages. Run 30 alternating resizes,
20 reconnects and a two-hour mixed session; test hide/recall, cross-display scale,
input release and recovery. A sub-two-second P95 is a target, not an achieved
result. Keep the previous release available for rollback without deleting pairing.
