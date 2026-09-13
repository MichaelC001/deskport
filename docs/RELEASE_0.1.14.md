# DeskPort 0.1.14 — automatic connection ports

A headless host can move to a free streaming port group while a saved client
continues polling its old port. Approved DeskPort peers now refresh that endpoint
automatically over their fixed, authenticated connection entry.

- Settings → Connections exposes one connection port, default **48991**. Entering
  a device name uses that port; video/audio ports remain automatically allocated.
- Port changes preserve existing connections and retain previous entry listeners
  across restarts so an offline paired device can find the new port later. Up to
  eight previous ports are retained; occupied ports leave the current port intact.
- Background refresh pins both the TLS identity and streaming certificate/UUID,
  and rejects stale replies after local edits or revoked access. It does not
  request pairing, restart sharing, or interrupt active display/clipboard channels.
- Manual per-device port overrides are tucked under advanced controls.

Both sides need this version for automatic endpoint refresh. The saved connection
entry must remain reachable; firewall rules still govern custom ports. Legacy
Sunshine entries do not gain this protocol. Previous entry listeners are retained,
not automatically removed after a timeout.

Validated: 21 isolated binding cases (including port migration during an active
display-control channel), 15 UI cases, seven translation catalogs, Linux Nix build
and CLI smoke, and a stable signed macOS ZIP/DMG with verified dependency closure.
Compilation and isolated tests
do not replace real headless restart, sleep/wake, or live stream acceptance.
The macOS package keeps the existing Apple Development identity; it is not a
notarized Developer ID distribution. Running installations are not changed by
publishing this release.
