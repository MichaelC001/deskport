# Device session settings

Updated: 2026-09-14. Reason: resume the client-controlled settings backlog on
`dev/client-session-settings`, keeping the main branch and deployed services intact.

Open **Devices → device menu → Device settings**. The normal and advanced
settings pages now edit that device's profile. Saving displays **Reconnect to
apply changes**. Profiles use host identity, not a display name or IP address;
renaming or changing an endpoint does not mix two devices' settings.

A connection owns a snapshot. Editing settings cannot change a running session,
and automatic window-size renegotiations retain that snapshot. **Reconnect** in
the tray menu stops the transport, releases held input and reads the saved
profile before resuming the same remote application. It does not quit the remote
application, even when “quit app after streaming” is selected. **Restart** still
restarts the local DeskPort application to pick up a new executable.

## Settings inventory

| Settings | Saved/applied by |
| --- | --- |
| Resolution, automatic sizing, FPS, bitrate, codec, HDR, chroma, audio channels, host playback | Device profile; existing launch/RTSP negotiation |
| Receive sound, allow keyboard/pointer/controller input | Device profile; authenticated launch/resume session options |
| Smart streaming | Device profile; client bandwidth/pacing and the patched macOS encoder policy |
| System-key capture, cursor visibility, pointer/touch mode, scrolling, swapped buttons, controllers | Device profile; client input adapter |
| Plain-text clipboard | Device profile; authenticated bound-device clipboard channel |
| Decoder, VSync, frame pacing, focus-loss mute, overlays, warnings, keep-awake, window mode | Device profile; local session snapshot |
| Language, theme, device-list density, main-window presentation, discovery | Local application preferences |
| OS permissions, approved clients, sharing on/off, login startup, diagnostics/idle display | Local host setup |

Existing global connection preferences are defaults for devices without a saved
profile. First profile save stores the complete current connection selection.
Explicit CLI overrides are copied before the session starts. Host language,
appearance and discovery preferences are never transmitted or overwritten.

The sharing page no longer duplicates stream-audio and smart-encoder switches.
Host audio capture is available, but a new client can disable capture for its own
session. The host's outgoing clipboard preference no longer blocks an approved
incoming clipboard request. Certificate pinning, approval, exclusive clipboard
ownership, sequence validation, revocation and disconnect cleanup remain required.

## Protocol and compatibility

Patched Sunshine advertises `DeskPortSessionSettings=1` in server info. On the
existing authenticated HTTPS `/launch` or `/resume`, a supporting client sends
`deskportAudio`, `deskportInput` and optionally `deskportSmart` as `0`/`1`.
These fields are copied through launch state into the individual stream's config.
They never rewrite global Sunshine input/audio configuration or restart the host.
Both encrypted input dispatch paths honor view-only mode; disabled audio waits
for session shutdown without opening a capture device. Missing fields retain
normal input/audio behavior for existing clients. The macOS smart policy follows
the session value; the stock Linux encoder retains its existing pacing.

An older host remains usable with ordinary audio/input settings. Selecting audio
off or view-only against a host without this capability fails with an explicit
upgrade message rather than silently ignoring the setting. These controls are
session preferences, not an administrator's restriction on a bound client's rights.

The host integration script validates every source anchor before applying its
patch, supports checked reversal for incremental Mac builds, and is also used by
the pinned Nix Sunshine derivation. No upstream dependency revision is upgraded.

## Verification and acceptance

Automated checks cover saved-device isolation, default inheritance, immutable
session/resize snapshots, CLI precedence, local-language separation, QML loading
and language switching, and authenticated clipboard lifecycle. Native Mac client
and patched-host compilation and an isolated NixOS x86_64 build are required.

Live acceptance is still pending: connect two deliberately selected test devices,
change sound/input/video independently, reconnect ten times, verify sound off/on
and view-only against the host, verify no held input, and confirm remote apps stay
open. Compilation and offline UI tests do not establish live media/input quality.
No deployed service or installed application is replaced during development.

Verified development evidence (2026-09-14): Mac client and patched Sunshine host
compiled in the locked Nix devShell; NixOS x86_64 built in an isolated temporary
checkout and passed executable/desktop-identity/CLI smoke checks. Desktop-state
checks: 7 passed; QML regression: 15 passed; authenticated clipboard regression:
4 passed; seven-language desktop resource coverage passed. Live streaming and
real reconnect behavior remain pending as described above.
