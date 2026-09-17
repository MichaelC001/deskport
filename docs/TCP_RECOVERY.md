# TCP path recovery

Updated 2026-09-17: recover Linux control connections on TCP segment-size
blackholes without changing system networking or the existing TLS identity model.

Normal connections remain the first choice. The Linux Qt control paths can retry
once with `TCP_MAXSEG=900`, set after binding an ephemeral socket and before
connecting. The kernel advertises this limit in the SYN; negotiated payloads can
be smaller because of TCP options. No privilege or firewall rule is required.
The existing media-core transport configuration is unchanged. This is not a
repair for UDP video loss.

- Binding and endpoint refresh retry a still-pending TLS handshake after 1.5 s.
  Existing overall binding/refresh deadlines remain in force. No request or
  display/clipboard message has been sent at this point. A rejected certificate,
  completed TLS connection or explicit socket failure does not trigger this timer.
- Display and clipboard workers retry one timed-out handshake, before reading
  the server hello or sending application messages. Each attempt retains its
  existing 4 s / 3 s timeout. Peer certificate checks remain at the original call sites.
- HTTPS keeps `QNetworkAccessManager`. Only `serverinfo`, `applist`, and `appasset`
  may retry after a timeout. Launch, resume, quit, pairing and other commands are
  never replayed by this mechanism. They can reuse an already learned mode.
- HTTPS compatibility uses a temporary loopback CONNECT tunnel restricted to one
  fixed target and one accepted connection. The tunnel forwards opaque TLS bytes,
  with bounded 64 KiB socket buffers; Qt still handles HTTP, TLS and certificate
  validation. Proxy-side DNS is disabled, including for IPv6 literal support.
  It is neither a system proxy nor a TLS termination point.
- Successful compatibility connections are cached in memory for two minutes,
  keyed by host, port and the current active-interface address set. Native TLS
  retries reuse the actual failed peer address when available. Cached connections
  reuse the last successful numeric peer until expiry; HTTPS resolves names again.
  The HTTPS tunnel DNS lookup has a separate two-second budget; normal native
  connections keep Qt asynchronous DNS. Successful polling does not
  extend the fixed cache expiry. Interface changes select a new key; failures clear the
  entry, and the cache is bounded to 64 entries. A route-only change or DNS change
  can retain the smaller-segment preference and cached native address until expiry;
  it cannot change trust.
- macOS and Windows retain their existing Qt connection behavior. This release
  does not claim equivalent MSS fallback on those systems.

## Validation

`nix develop -c python3 scripts/test-small-tcp.py` compiles the production HTTP
and TCP code. A separate synthetic TLS server records the kernel-negotiated MSS
and withholds handshakes from large-MSS connections. Checks cover native blocking
and asynchronous fallback, IPv4/IPv6, normal-path preference, certificate rejection
on both direct and tunneled connections, a 256 KiB response, cache reuse/expiry,
bounded failure and no replay after a side-effecting request reaches the server.
Fixtures use ephemeral loopback ports, generated certificates and isolated settings.
They model the observed transport failure; they do not emulate packet loss itself.

An independent read-only probe of an affected route also exercised the production
helper and HTTP path with a separately obtained pinned public certificate. The
first HTTPS request recovered after its default timeout; a cached request completed
without that delay. The probe used an isolated identity and did not launch a stream,
request a display lease, change pairing, or access clipboard contents.

After user activation, verify device online status, connection, resize, clipboard,
client handoff and reconnection on the actual network. Build, fixture and probe
success do not replace authenticated application or physical-device acceptance.

Public API references: [Qt socket binding](https://doc.qt.io/qt-6/qabstractsocket.html#bind),
[Qt proxy capabilities](https://doc.qt.io/qt-6/qnetworkproxy.html#Capability-enum),
and [Qt TLS sockets](https://doc.qt.io/qt-6/qsslsocket.html).
