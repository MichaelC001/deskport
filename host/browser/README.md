# Browser media adapter

This adapter runs inside DeskPort's bundled Sunshine helper. It sends the existing
capture/encoder output through libdatachannel 0.24 instead of the Moonlight UDP
transport. Native streams retain their original queues and transport. It does not
decode or re-encode video and does not expose Sunshine's administration UI.

The private `POST /api/deskport/browser` endpoint uses the existing loopback-only,
certificate-pinned Basic management boundary. It refuses browser Origin/Referer
headers. The public HTTPS gateway authenticates the browser and calls this endpoint;
the helper never receives the public access code.

Every request is a JSON object with `action` and a random `id` (16–64 alphanumeric
or hyphen characters). Replies include `version: 1`, `status`, and an optional
error `code`.

| Action | Additional fields | Result |
| --- | --- | --- |
| `reserve` | none | Acquires the common exclusive admission lease; returns `busy` for an existing browser or native owner. |
| `start` | `width`, `height`, `fps`, `bitrateKbps`, `audio`, `input`, optional `videoCapabilities` | After the supervisor has verified its display, creates a complete ICE `offer` SDP. |
| `answer` | `type: "answer"`, `sdp` | Applies the browser's complete answer SDP. |
| `heartbeat` | none | Renews the 30-second controller deadline. |
| `status` | none | Read-only `state`, `connected`, `frames`, `audioPackets`; never renews a deadline. |
| `input` | `event` | Optional HTTP input fallback; normally input uses the encrypted DataChannel. |
| `stop` | `keepReservation: true` (default) | Joins capture/transport and fences native input, retaining admission for display restoration. |
| `release` | none | Clears the reservation after display restoration. |

Start validates dimensions and constrains output to a 1280 × 720 bounding box,
30 fps and 14 Mbps, returning the effective dimensions, frame rate and bitrate.
The initial transport advertises H.264 Constrained Baseline, packetization mode 1, level 3.1, SDR
4:2:0, plus 48 kHz stereo Opus. Supplied browser codec capabilities must contain
H.264 Baseline with packetization mode 1 and receive level 3.1 or higher; unsupported profiles return
`unsupported-codec`. This is a fixed-bitrate LAN/VPN implementation, not a claim
of Internet congestion adaptation, TURN traversal, or universal browser support.
ICE uses UDP 48100–48115 in addition to the HTTPS listener. There is no implicit
external STUN/TURN server.

The server creates the reliable ordered `input` DataChannel. Its JSON events are:

- `move`: absolute `x`, `y`, `width`, `height` reference coordinates.
- `relative`: signed `dx`, `dy`.
- `button`: `button` 1=left, 2=middle, 3=right, 4/5=extra; boolean `down`.
- `key`: Windows virtual-key `key`, boolean `down`, and `modifiers`
  (Shift=1, Control=2, Alt=4, Meta=8).
- `text`: UTF-8 `text` up to 4096 bytes, split at Unicode boundaries internally.
- `scroll`: signed `x`, `y` wheel units; 120 per notch, positive right/up.
- `release`: releases all held input without ending media.
- `heartbeat`: renews the controller deadline, including on static screens.

Malformed messages are rejected. Input is limited to 500 resulting native packets
per second; exceeding the budget releases held keys/buttons, and explicit release
and heartbeat remain available. With `input: false`, no
per-client native input context is created.

ICE disconnection, failed capture, missing answer after 15 seconds, or heartbeat
expiry stop media/input. The stopped reservation remains for the supervisor's
display restoration, with a 15-second orphan fallback. Native takeover returns
`busy` during this reservation rather than crossing the restoration barrier.
Process shutdown joins this adapter before shutting down Sunshine's input worker.

Video queues are private and bounded to four encoded frames. SPS/PPS replacement
bytes become owned access-unit data before crossing the encoder thread boundary.
Queue discontinuity or PLI requests a new IDR. RTP uses the original monotonic
capture timestamps and RTCP sender reports; NACK retransmission has a bounded cache.
These safeguards do not establish hardware latency or long-session acceptance.

## Isolated validation

Run `scripts/test-browser-media.py --rtc-prefix PREFIX --json-include JSON_INCLUDE`.
It compiles the production RTC adapter and tests real loopback ICE, DTLS, SRTP
H.264/Opus, DataChannel and close behavior. Native input serialization tests validate
bounds and UTF-8 without linking platform input. Tests do not capture the user's
screen or inject input. The RTC test also has a `--browser SIGNAL_DIR H264_FILE`
mode for real browser decode validation with explicitly supplied AUD-delimited test
media. Full Sunshine capture, hardware encoding, iPad Safari, display restoration,
and physical keyboard/audio acceptance must be verified independently.

`scripts/test-browser-native.py` runs full Sunshine capture/encoding through the
production HTTPS gateway and browser in an isolated Linux Xvfb/private PulseAudio
fixture. Supply the host, fixture gateway, Chromium, PulseAudio, Node, OpenSSL and
Playwright-core paths explicitly. It verifies dynamic decoded video, nonzero Opus
audio energy, stop/reconnect, and admission release. It disables native input and
only terminates its own processes; it does not exercise a personal desktop,
physical iPad, hardware encoder, or HostManager display restoration.

For durable browser pairing, run `scripts/test-browser-pairing.py` with the same
required paths as `test-browser-native.py`: `--host`, `--gateway`, `--chromium`,
`--pulse-prefix`, `--node`, `--playwright-module`, and a fresh `--output` directory.
`--openssl` overrides certificate generation and `--runtime-root` selects a short
private UNIX-socket directory. Optional `--webkit-driver` uses an already available
WebKitGTK WebDriver after the Chromium suite; its separate result is engine
evidence, not Safari/iPad acceptance. On Nix, provide its existing desktop schema
directory through `GSETTINGS_SCHEMA_DIR` if MiniBrowser is not wrapped. No HTTP
responses are mocked. The automated browsers bypass certificate validation for
the isolated self-signed HTTPS origin; system certificate trust is unchanged.
The gateway still verifies the fixture Sunshine certificate.

`scripts/test-browser-pairing-profile.py --gateway PATH --chromium PATH --node PATH
--playwright-module PATH --output DIR` exercises an installed branded browser with
a disposable persistent profile and real HTTPS gateway, without a capture host.
It covers browser/gateway process restarts, temporary mode, forgetting, revocation,
and cookie flags. The fixture never uses the user's normal browser profile.
