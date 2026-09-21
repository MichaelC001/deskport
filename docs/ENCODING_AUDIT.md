# Encoding and transport audit — 2026-09-21

Scope: both pinned Sunshine revisions (`sunshine-nix.tar.gz` and
`sunshine.tar.gz`), DeskPort overlays, desktop negotiation, and mobile adapter
boundaries. Static inspection covers all compiled encoder backends. Hardware
probes cover AMD RADV Vulkan H.264/HEVC. Live checks additionally exercise
Apple Silicon VideoToolbox through isolated hosts and real desktop viewers.
Build, controlled stream validation and user activation are recorded separately.

## Findings and changes

1. **Confirmed excess bandwidth: Vulkan CBR padding.** The default Vulkan
   `rc_mode=2`, equal minimum/target/maximum rate and single-frame VBV cause the
   tested driver to fill almost the entire target even for tiny changes. Smart
   sessions now request bounded VBR for AMD RADV Vulkan H.264/HEVC, with zero minimum,
   unchanged target/maximum and unchanged VBV. Failure to open retries the
   original configuration once. Explicit driver-default, CQP and VBR selections,
   smart-off sessions, and other backends retain their existing behavior.
   Unsupported runtime behavior after opening still requires driver qualification;
   initialization fallback is not protection against every driver defect.
   The adapter checks the actual Vulkan vendor and driver IDs; Intel and other
   drivers retain their previous defaults until separately qualified.
2. **Shared measurements were missing.** Every host encoder now feeds one
   session-local five-second encoded-byte counter at the common video queue.
   Logs include actual encoded Mbps, packet/keyframe counts and largest packet.
   This measures elementary-stream bytes before replacements, packet headers,
   encryption and FEC, not wire bandwidth or the negotiated bitrate. Short
   sessions/windows may end before a report. No screen/input contents are logged.
3. **FPS control is not live bitrate control.** The common congestion policy
   lowers FPS after repeated unrecoverable FEC episodes, with hysteresis and
   recovery. It does not reconfigure hardware bitrate or react to rising RTT
   before loss. Lower FPS can reduce total bytes, but cannot guarantee a WAN
   throughput ceiling with every rate-control implementation.
4. **Transport pacing is not WAN-budget pacing.** The inherited sender uses
   approximately 800 Mbps packet pacing and batches up to 64 KiB / 64 packets.
   A large IDR can still burst into a slower link. RTSP already deducts FEC,
   audio and protocol allowance from the requested budget; this is distinct
   from pacing actual packets. No speculative pacing change is included here.
5. **GOP is not the main padding cause.** Common AVCodec uses no B frames and
   very long GOP except Media Foundation. Recovery requests can still produce
   IDRs. Extending GOP again would not remove per-frame filler. Never remove
   decoder recovery or falsify SPS reference counts to reduce bandwidth.
6. **Client budget rules are not yet unified.** Desktop smart mode has its own
   resolution/FPS/chroma budget and user ceiling. Mobile adapters have separate
   defaults/settings. Host-side encoding changes benefit any connecting client;
   that does not establish identical client-side budgets, rendering or touch
   activity behavior. Private mobile code is not copied into this repository.

7. **Asynchronous IDR diagnostics compared unrelated frames.** VideoToolbox
   returned an earlier output PTS while a new recovery frame was being submitted.
   A bounded session-local request tracker now checks the corresponding output
   PTS. Recovery requests and encoded data are unchanged; genuine non-key output
   for a requested PTS still reports an error. This applies to both AVCodec paths.
8. **Background endpoint refresh left the editor busy.** An incoming endpoint
   query cleared the active link without notifying QML. Emit the existing state
   notification when the link is cleared; a binding regression covers the signal.

## Backend matrix

Values below describe source configuration, not measured driver behavior unless
explicitly noted. Rate-control names alone do not prove whether padding occurs.

| Backend | Current rate / latency setup | GOP / recovery | Decision |
| --- | --- | --- | --- |
| Linux Vulkan | Default CBR, one-frame VBV, async depth 1, low latency; smart AMD RADV H.264/HEVC now try VBR | GOP 32767, on-demand IDR | H.264/HEVC measured; preserve AV1 pending separate investigation |
| Linux VAAPI | Driver/vendor-dependent; Intel, AV1 or explicit strict-buffer mode prefer VBR with one-frame VBV; other cases may use larger buffers for quality | GOP 32767, on-demand IDR | Preserve vendor checks; AMD VAAPI is not equivalent to Vulkan |
| macOS VideoToolbox | Realtime / prioritize speed; backend interprets AVCodec rate fields; no new VBR override | Effectively infinite configured GOP; on-demand IDR; H.264 low-delay fallback | Preserve native settings; isolated H.264/HEVC stream checks |
| NVIDIA NVENC | Native CBR, filler disabled by default, zero reorder delay, lookahead off; legacy FFmpeg path also exists | Native infinite GOP, recovery / reference invalidation support | Do not equate CBR with Vulkan padding; requires NVIDIA hardware test |
| Intel QSV | VBR selection via target below maximum, async depth 1, low-delay BRC; hardware-managed buffer | Long GOP, forced IDR, low-power fallback | Preserve native workaround; requires Intel test |
| AMD AMF | Latency-oriented VBR defaults, filler off, async depth 1; usage fallback | Long GOP, forced IDR | Preserve native defaults; requires Windows AMD test |
| Windows Media Foundation | CBR, display-remoting scenario | Fixed 120-frame GOP; backend lacks on-demand IDR | Cannot impose infinite GOP without breaking recovery assumptions |
| Software | x264 zerolatency; x265 special long-GOP/header handling; buffer enlarged for slices/HEVC | Long GOP; software AV1 disabled by default due upstream limitations | CPU/quality tradeoffs need their own measurements |

Both upstream revisions remain build inputs; the common overlay must work against
both. macOS managed capture currently supports 8-bit H.264/HEVC; an encoder's HDR
capability does not establish capture, color conversion and client HDR support.

## Reproducible synthetic evidence

A standalone program links the **same prebuilt static FFmpeg (`fb216b5`,
libavcodec 62.28.101)** used by the Nix host. AMD RADV PHOENIX; NV12 1920×1080,
60 FPS, 180 frames, fixed checkerboard plus a blinking 20×40 rectangle. Target
and maximum are 18,988,000 bps; VBV is target / 60. Explicit keyframes at 0 and
90. Only rate mode and minimum rate differ. No desktop capture or input injection.

| Codec | CBR encoded Mbps | VBR encoded Mbps | CBR filler payload share | Output / requested keyframes |
| --- | ---: | ---: | ---: | --- |
| H.264 | 18.964008 | 0.076045 | 99.466% | 180 frames / 2 keyframes in both modes |
| HEVC | 18.964008 | 0.177069 | 98.941% | 180 frames / 2 keyframes in both modes |

All four streams decoded without reported errors. VBR still emitted two small
filler NALs, so this is a large reduction, not a claim of zero padding. These
numbers describe this deliberately simple scene, not expected savings for video,
scrolling or arbitrary desktop use, nor an end-to-end latency/visual-quality result.
A separate system-FFmpeg test with a more detailed frozen background gave roughly
19 vs 0.42 Mbps; do not combine results from different content/builds.

AV1 CBR probing hit `VK_ERROR_DEVICE_LOST` / a RADV hard recovery. VBR returned
packets, but raw output validation was inconclusive. AV1 is **not qualified** by
this experiment and remains unchanged. No repeated driver-failure stress test was
run. The existing desktop service and host processes remained running afterward.

## Controlled live validation

Disposable host instances reuse the existing approved identities on separate
ports. Installed services remain running. A local test page supplies static fine
text, a moving 20×40 rectangle, full-page scrolling, and a large moving panel.
Host encoded-byte logs are paired with the real viewer's FPS, loss and timing.

Initial Linux Vulkan H.264 1280×720 results (3.428 Mbps encoder budget): static
1 FPS at 0.039–0.047 Mbps; small changes near 60 FPS at 2.39 Mbps; scrolling
3.48 Mbps; moving-panel windows 3.55–3.62 Mbps. Video network loss was zero.
These are desktop measurements, not the much lower synthetic probe numbers.

Apple Silicon H.264 1920×1080 (6.057 Mbps budget), after isolating the visible
fixture: static about 1–2 FPS at 0.006–0.009 Mbps; small changes about 58 FPS at
0.25 Mbps; scrolling about 2.3 Mbps. Static refreshes and the capture heartbeat
can coincide, so 1 FPS is an idle target rather than an exact packet-rate promise.

Final driver-gated Linux H.264/HEVC runs both displayed the fixture and
reconnected successfully. H.264 static windows were about 0.042 Mbps / 1 FPS,
small-change windows 2.57 Mbps / 60 FPS; HEVC static windows 0.05–0.08 Mbps,
small-change windows 3.29 Mbps / 60 FPS, scrolling 3.28–3.32 Mbps. The real
captured desktop does not reproduce the synthetic probe's extreme savings;
HEVC can still stay close to its 3.428 Mbps budget during small changes.

Apple Silicon HEVC 1920×1080: static about 1.4 FPS / 0.01 Mbps, small changes
about 58 FPS / 0.40 Mbps, scrolling 2.18 Mbps, large moving panel 0.46 Mbps.
No video-network loss was reported in the sampled final runs in either direction.
Presentation-queue drops remained intermittent (roughly 4–11% on sampled Linux
host / Mac viewer windows and higher spikes in the reverse direction). A reverse
HEVC comparison with client vsync and pacing disabled still showed a 6.6% sample.
These are not a zero-drop or end-to-end latency qualification. WAN jitter,
client scheduling and presentation require separate measurement; encoder savings
do not prove that display delivery is solved. No deployed configuration was activated.

Early runs with recursive bidirectional viewing, hidden/full-screen test windows,
or simultaneous compilation are excluded from clean performance acceptance.
A temporary exact-pixel fallback experiment was withdrawn: it did not establish
a need to replace native capture status. The final capture behavior still uses
native updates without CPU pixel scanning. Capture logs add only status counts,
not screen content. Clean final codec runs are recorded in the private manifest.

## Shared architecture

`host/common` owns pure host policy and statistics: input activity, frame cadence,
congestion hysteresis, rate-control selection/fallback, and encoded-byte windows.
The common AVCodec/session integration invokes that policy. Platform adapters own
capture damage, surface/pixel formats, driver capabilities, native rate-control
options, buffer restrictions and encoder reconfiguration APIs.

This policy executes on the host regardless of whether the viewer is desktop,
iPad or Android. Mobile viewers decode rather than encode the host desktop. A
future shared **client budget contract** can live in the portable core with
native adapters and identical fixtures; it should not pull GPL media/backend
implementation into the MIT core or private mobile UI into public source.

## Validation and remaining checkpoints

- Pure policy regression covers smart-off, unqualified backends, explicit modes,
  retry behavior and independent/resetting telemetry windows.
- Both pinned source overlays must apply, including the existing real mailbox
  template test, and repeated encoder-overlay application must be idempotent.
- Build macOS host and Nix Linux host; no installed service replacement.
- Isolated validation precedes private packaging. After manual activation compare identical
  resolution/FPS/scene on the same network, collect actual encoded Mbps, client
  incoming FPS/loss/RTT and IDR frequency. Check idle, typing, scrolling, video,
  resize, reconnect and recovery. Include high-motion quality and frame-size spikes.
- Next implementation priorities: shared client budget fixtures; per-session
  wire-byte/queue telemetry; measured WAN pacing; capability-gated live bitrate
  adaptation. Preserve FEC and recovery while validating those changes.
