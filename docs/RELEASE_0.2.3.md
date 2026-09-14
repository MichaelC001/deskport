# DeskPort 0.2.3 — per-device session settings

This development prerelease lets each saved device retain its own picture, audio,
keyboard, mouse and controller preferences. Open Settings from the device menu.
Saved changes apply after reconnecting; active sessions keep their original settings.

- Clients negotiate audio capture, remote input and macOS smart workspace behavior
  with the authenticated host at launch/resume. View-only sessions disable host input
  dispatch as well as local input capture.
- Session controls are collected on the client; redundant host audio and smart
  workspace toggles are removed. Host setup retains local service and permission controls.
- The tray menu has a Reconnect action separate from Restart. Reconnect preserves
  the remote application and loads the selected device's saved preferences.
- Hosts without the session-settings capability reject unsupported audio-off or
  view-only requests with an upgrade message rather than silently ignoring them.

Scope: signed and Apple-notarized macOS arm64 ZIP/DMG, plus NixOS x86_64 through
this tag's flake. The stable release and main branch are unchanged. Matching mobile
client work is on its development branch and is not included in these desktop assets.

Build and isolated regression checks cover profiles, lifecycle, clipboard policy,
translations and native compilation. Live audio/input, reconnect continuity and
per-device behavior still require verification after manually activating both ends.
See [session settings](SESSION_SETTINGS.md) for the inventory and acceptance checklist.
