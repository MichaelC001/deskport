# DeskPort 0.3.14 preview

Linux control connections now recover from paths that stall TLS when larger TCP
segments are dropped. Normal connections come first, followed by one bounded
900-byte MSS compatibility attempt, with a short in-memory success cache.

This covers HTTPS device discovery, binding and endpoint refresh, adaptive display
control and clipboard connections. Certificate checks and authentication remain
in place. Launch, resume, quit and pairing requests are never replayed by the new
HTTP recovery path. No system firewall or network configuration changes are needed.

macOS retains its existing TCP behavior and the display/session recovery fixes from
0.3.13. This preview ships the signed/notarized macOS arm64 package and Linux x86_64
Nix source. It does not update the mobile client or change UDP video transport.

See `TCP_RECOVERY.md` and the release verification asset for the checks and scope.
Activate through your normal system configuration, then verify real multi-device
connection, resize, clipboard and reconnect behavior. Keep the previous system
generation for rollback.
