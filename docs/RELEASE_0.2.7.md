# DeskPort 0.2.7 development preview

Cards are now the only device layout. System marks distinguish macOS, Windows,
NixOS, Ubuntu, Debian, Fedora, Arch and generic Linux; unknown hosts remain generic.
Each card has a direct device-settings action. All streaming settings are reached
through that device. Global settings contain only local appearance and language,
plus an optional sidebar data-usage display (off by default).

The sidebar separates devices, the active session, and local sharing/settings.
Theme and accent independently follow Qt's native system preferences, or use
manual light/dark and blue/green/purple/orange selections. If a platform does not
provide an accent or scheme, Qt's platform palette is the fallback. System accent
brightness may be adjusted for contrast; this is not an exact-color guarantee.

## Data usage definition

Measured successful client transport socket reads/writes include media, ENet
control/input, FEC/retransmissions and RTSP. Clipboard counts application payload
bytes. Peeking or failed I/O does not increment counters. The sidebar samples once
per second when visible; cumulative counters continue with the viewer hidden.
Adaptive resizing and explicit reconnects retain the baseline; a new connection
starts a new baseline. The counter is not a carrier billing meter: IP/VPN and
clipboard TLS overhead, discovery, other apps and host-side sharing are excluded.
Windows counting is outside this Mac/Linux preview and is not displayed there.

## Validation and activation

Isolated QML tests cover the app shell, card identity, local/device setting entry,
light/dark and accent overrides, and palette contrast. Screenshots use synthetic
devices only. Loopback tests exercise all instrumented socket APIs plus concurrent
transfers. Profile snapshots, authenticated binding and clipboard regression are
checked separately. Build/notary results are recorded when release gates finish.

Distribution scope: signed/notarized macOS arm64 ZIP/DMG and the Linux x86_64 Nix
package. This is a prerelease, not the stable release. Consumers update mynix;
activation remains a manual user `rebuild switch`. Real video/audio/input, OS
preference changes and hotspot usage require acceptance after activation.
