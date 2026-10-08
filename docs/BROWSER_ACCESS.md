# Browser access

The desktop host starts a local HTTPS browser entry by default. An iPad or
another device can open its URL without installing DeskPort. The initial
implementation uses one six-character, server-generated access code; the same
code remains valid across restarts. No account, password registration, mobile
app, authenticator, or browser key enrollment is required.

## Connect

1. Start DeskPort on the computer and enable sharing.
2. Expand **Browser access** on the Sharing page. It shows whether the entry is
   running and on which networks. Scan the URL QR code or open one of the
   displayed LAN or Tailscale URLs on the other device. The access code stays
   hidden until you select **Show code**.
3. Complete the browser's certificate trust step, then type the six-character
   code displayed on the computer. Keep **Remember this browser** selected on
   your own device, or clear it for a temporary connection, then select **Connect**.
4. Use the touchpad, direct touch, mouse, physical keyboard, on-screen shortcuts,
   or text panel. The text panel accepts composed text, including Chinese.

The HTTPS entry runs whenever DeskPort runs; no command needs to be issued on
the computer before connecting. `deskport web info` only reports the URLs and
code.

## Tailscale devices without the code

A browser that reaches the computer through its Tailscale address can sign in
without the access code when the computer's own Tailscale agent confirms that
the device belongs to the same Tailscale (or Headscale) user as this computer.
The page asks once when it opens; an owned device sees **Connect** directly.
DeskPort runs `tailscale status` and `tailscale whois` locally for each such
request. tailscaled has already authenticated the device's WireGuard key, so its
tailnet address identifies the device. Tagged devices, other users' devices,
computers that are themselves tagged, LAN connections and any failure of the
local query fall back to the access code. A Tailscale sign-in creates an ordinary
session but no remembered pairing; ownership is checked again on each visit.

Set `web/tailnetIdentity=false` to require the code everywhere, or
`web/tailscale=/path/to/tailscale` when the CLI is not in a standard location.

The QR contains only the HTTPS URL. The code is entered separately and never
appears in URLs or browser storage. A remembered browser restores its pairing
after refreshing or reopening the page and can connect without entering the
code again. Starting desktop media still requires selecting **Connect**. An
unremembered browser requires the code for every new connection. The permanent
code itself does not expire every 30 seconds; individual media packets and API
requests never require repeated manual entry.

The server creates a self-signed certificate on first run. Browser trust is
separate from the access code: this version does not silently install a CA or
bypass certificate validation. A browser/device that cannot trust this
certificate needs an explicitly configured trusted certificate before use.

From a local terminal, or SSH as the same desktop user:

```sh
deskport web info
deskport web info --json
```

The local command returns URLs and the permanent code, and its human output
includes a terminal QR. Treat that output as a credential. Ordinary host status
and unauthenticated web responses do not include the code.

## Remembered browsers

Remembered pairing works in ordinary Chrome tabs; no extension, installed web
app, account or authenticator is required. Safari uses the same browser entry,
but its acceptance checks remain open. Each browser gets an independent
256-bit random credential in a persistent `Secure`, `HttpOnly`, `SameSite=Strict`
cookie. Page JavaScript cannot read it. The server saves only its hash and local
device metadata. A successful restore recovers a temporary session and
renews the one-year pairing validity. Browser and server restarts preserve the
pairing; inactive pairings expire after a year.

**Disconnect** stops media and releases control while keeping the pairing.
**Forget this browser** removes its saved authorization. The computer's
**Sharing → Browser access → Paired browsers** list can remove one browser and
invalidate its active sessions without removing the others. SSH users can use:

```sh
deskport web list
deskport web list --json
deskport web remove BROWSER_ID
```

Removal invalidates the old credential. Someone who still knows the permanent
six-character code can pair again. Code rotation and disabling new enrollments
are separate future controls.

Keep the HTTPS host name or IP address stable. Browser profiles and private
windows have separate storage; clearing cookies/site data or losing browser
storage requires pairing again. The saved server record cannot restore a
credential that the browser has lost. HTTPS certificate trust remains required
after pairing, including when a certificate is replaced.

## Available controls and boundaries

- H.264 Constrained Baseline Level 3.1 SDR desktop video, up to 1280 × 720 at
  30 frames per second; stereo Opus audio. The browser must support that profile
  and level with packetization mode 1. The capture encoder uses the profile and
  level advertised in WebRTC; higher resolutions are deferred until their
  receiving capability can be negotiated explicitly.
- Adaptive desktop size. The page reports its drawable area, device pixel ratio
  and the chosen content size; the host derives the remote desktop from the same
  shared-core workspace policy as native clients, including the macOS minimum.
  Resizing the window, rotating a tablet, entering fullscreen or changing the
  content size in the toolbar resizes the remote desktop while connected. Only
  video capture restarts; audio, input and the WebRTC connection continue. The
  transport still encodes the same aspect ratio within 1280 × 720.
- Fullscreen shows only the remote desktop; the toolbar appears near the top
  edge or from the handle and hides again after a few seconds.
- "Use as an extended display" (connection options) keeps the computer's own
  displays and adds the browser as a display to their right, where the host
  supports display policies (macOS, Windows virtual display, Linux except GNOME
  and Hyprland). A headless Mac has no physical display to extend, so the
  browser display becomes its only display, as in the default mode.
- Touchpad movement, tap/click, two-finger scrolling, long-press dragging, direct
  touch, mouse wheel, physical keys and on-screen shortcuts.
- Explicit text entry/paste into the remote computer; this is not automatic
  bidirectional clipboard synchronization. Clipboard reads require the browser's
  permission or manual paste into the text box.
- One active controller. Browser sessions and native clients share the host's
  existing admission gate. Starting a browser session cannot take over an active
  native session. Disconnect releases pressed inputs and stops media before
  restoring the display and releasing admission.
- Browser backgrounding, lost connections and missed heartbeats release control.
  Some browser or operating-system shortcuts cannot be intercepted; use the
  on-screen shortcut buttons in those cases.

This version uses direct WebRTC over the LAN or a Tailscale/Headscale tailnet,
with no STUN/TURN relay, public rendezvous, UPnP change, or automatic firewall
change. Over Tailscale, media follows the tailnet path (direct or DERP-relayed). File transfer, gamepads, HDR, HEVC,
AV1, multiple displays, and native-client feature parity are outside this first
browser client.

## Host configuration

The default HTTPS port is TCP **48992**. Media uses WebRTC UDP **48100–48115**.
Allow these only on the intended network if the host firewall blocks them.
HTTPS binds the available private/link-local IPv4 interfaces, Tailscale or
Headscale addresses, and loopback; it does not bind every public interface. A
tailnet address is RFC 6598 space (`100.64.0.0/10`) on a tunnel interface such as
`tailscale0` or `utun`; the same range on a physical or cellular link is carrier
NAT and is not bound. Listeners follow interface changes within about ten
seconds, for example when Tailscale starts after DeskPort at login, without
interrupting connected browsers.

The generated certificate is retained while it names every served address. When
a new address or name appears, DeskPort extends that self-signed certificate,
keeping the names it already had, so the connecting browser must trust the new
certificate once. The access code and remembered browsers are unchanged. A
custom certificate is never modified; it must already cover any Tailscale address
or MagicDNS name used. Keep `browser.ini` to retain the permanent access code.

The supervisor accepts `--no-web-server` to disable this entry for that run.
Persistent settings use the existing DeskPort settings file:

```ini
[web]
enabled=true
port=48992
# Optional specific local bind address; otherwise private IPv4 interfaces.
# bindAddress=192.168.1.20
# Optional matching PEM certificate and private key.
# certificate=/absolute/path/certificate.pem
# privateKey=/absolute/path/private-key.pem
# Optional DNS name covered by that certificate and resolving to this LAN host.
# allowedHosts=desktop.example.com
# Owned Tailscale devices may connect without the code (default true).
# tailnetIdentity=true
# Optional tailscale CLI path when it is not on PATH or a standard location.
# tailscale=/run/current-system/sw/bin/tailscale
```

`DESKPORT_WEB_PORT` and `DESKPORT_WEB_BIND` override port and bind address for a
run. With a custom certificate, explicitly configured `allowedHosts` appear
before IP URLs and can be used for the QR entry. Browser credentials and the
generated certificate are stored beneath the
application's local data directory in `browser/`. The directory and private
files are restricted to the current user. Saving the six-character code is
intentional so it survives restarts. Browser pairings are stored separately in
the same private directory. Multiple independent remembered browsers are
supported; only one may control the desktop at a time. Multiple six-character
codes and code rotation controls are future work.

## macOS host checkpoint — 2026-10-08

A Mac host previously admitted browser sessions but sent no video: the bundled
Nix copy of Apple's libiconv could not load its converter data away from the
packaging machine, and libnice stopped answering ICE checks after GLib produced
an empty error. The package now uses the system libiconv. Verified on mm1 with an
isolated instance using the installed primary/mirror display policy: a headless
Chrome on pk4 signed in over Tailscale without the code and received continuous
H.264 video (about 30 fps, 1280x528) and Opus audio.

Adaptive size checkpoint, same setup: from a 1368x728 viewport the desktop
followed portrait (868x1028), ultra-wide (1568x528), 150% content size, four
rapid window changes (coalesced to the last size) and fullscreen without
reconnecting; decoded frames kept increasing across each video restart.

## Implementation and validation

`BrowserGateway` serves embedded assets and authenticated same-origin HTTPS
requests. `BrowserHost` coordinates admission and the existing display manager.
Only the private, certificate-pinned loopback management channel reaches the
host's browser transport. Browser input uses an ordered WebRTC DataChannel, with
an authenticated HTTP fallback. The existing capture and encoder produce H.264
and Opus directly for libdatachannel; there is no second decode/encode step.

The pinned Nix Linux package and macOS host build include libdatachannel 0.24.1
and the browser overlay. Windows and other packaging recipes need their own
dependency/build integration before distributing this feature there.

Run the isolated checks inside the locked development shell:

```sh
python3 scripts/test-browser-gateway.py
python3 scripts/test-browser-host.py
python3 scripts/test-host-lifecycle.py --cli
```

`scripts/test-browser-media.py` checks input serialization and the native
WebRTC transport. `scripts/test-browser-client.mjs` exercises the embedded web
client with encoded test media. `scripts/test-browser-native.py --help` lists
the dependencies for the separate Linux Xvfb/PulseAudio capture fixture; it
uses `scripts/build-browser-live-gateway.py` and a disposable Chromium profile.
`scripts/test-browser-pairing-profile.py` uses an installed Chrome binary with a
new persistent profile and production HTTPS authentication, without a capture
host. `scripts/test-browser-pairing.py` adds the isolated native capture fixture.
Both restart the browser process and the gateway process, check profile isolation,
and exercise forgetting and local revocation. All fixture state and evidence
belong under the configured DeskPort build root.

Persistent-pairing checks passed in ordinary Google Chrome 154 without a PWA:
page refresh, full browser restart and server restart preserve authorization;
temporary mode, separate profiles, forgetting and revoked-credential replay
behave as expected. The HTTPS suite also covers malformed storage, persistence
failures, expiry and cross-tab session reuse. Frontend regressions cover delayed
cleanup in one tab while another tab takes control. macOS and native Linux builds,
the paired-browser UI, and the final packaged Linux CLI pass their checks.
Browser automation accepts the fixture's self-signed certificate explicitly; it
does not test the user's initial certificate trust flow or change system trust.
An additional WebKitGTK probe did not reach the page's initial pairing state;
it completed no product assertions and does not establish Safari compatibility.

The isolated native fixture passes real capture/encode/browser sessions with
changing 720p30 frames, nonzero decoded audio, stop and reconnect, and local
revocation followed by new enrollment. It uses
the production HTTPS gateway and web client with a test management adapter;
native input is disabled. The production `BrowserHost` lifecycle has separate
tests. These results do not establish physical iPad Safari certificate, audio,
touch or latency acceptance, nor actual compositor display restoration. See the
dated checkpoint in [ROADMAP.md](ROADMAP.md).
