# Text clipboard transport

Both devices on 0.2.2 support new text copies up to **128 MiB of UTF-8**. Images,
files, invalid UTF-8 and embedded NUL are rejected; content is never truncated or
logged. This is a text limit, not a count of characters or a file-transfer feature.

The mutually authenticated, pinned TLS connection retains clipboard protocol 1.
`clipboard-start.maxText` advertises the client's limit and
`clipboard-ready.maxText` returns the minimum supported size. Missing, invalid or
nonpositive advertisements use the legacy 1 MiB bound, so upgrading one side does
not cause oversized replies to an old peer. Both devices must be upgraded for the
larger bound. Only one request is outstanding. JSON/Base64 frame limits include
encoding expansion; a text exchange can take up to 120 seconds, with a 150-second
host lease. Handshake deadlines remain short and cancellation interrupts waits.

Native clipboard change notifications invalidate snapshots. Idle polls do not
re-read or re-encode unchanged text. Incoming remote copies recheck the native
client clipboard before application so a newer local copy is not overwritten.
Framing scans newly received bytes instead of repeatedly scanning a growing frame.

The existing JSON/Base64 framing is memory-backed: a maximum-sized copy can use
several times its UTF-8 size temporarily. The bound is finite, and content is not
written to disk. A future chunked binary transport would reduce that peak, but
requires a separately negotiated protocol and backpressure tests. Do not infer
large-transfer native clipboard performance from loopback tests alone.

`nix develop -c python3 scripts/test-host-lifecycle.py --clipboard` uses isolated
Qt/SDL clipboards and pinned loopback TLS. It covers Unicode, the maximum boundary
in both directions, overflow rejection, legacy negotiation defaults, ordering,
exclusive access and authentication failures. It never reads the user's clipboard.

Unsupported native offers are skipped without closing the text channel. Content
notices expire after five seconds; a native read failure must not prevent reply
processing or host polling. A later remote text copy can replace an unchanged
non-text offer; a newer local copy still protects against stale in-flight replies.
Transport/authentication failures remain errors and are not hidden as content skips.
