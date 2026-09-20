# Direct local validation updates

Use this route only when the user explicitly requests direct installation and
restart. It is separate from the normal release and system-configuration workflow.
Keep the existing settings, pairing certificates and permissions in place.

## macOS

Build the complete application, then sign it with the installed Developer ID
identity. Verify every embedded code object and compare the designated requirements
of the application, bundled host, display helper and recovery helper with the old
installation. A matching Bundle ID alone is insufficient. Retain the same
`/Applications/DeskPort.app` path and the existing login agent.

Apple explains how compatible designated requirements preserve code identity and
access to privacy-protected resources in
[TN3127](https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements).
Do not reset TCC, change signing class, or use ad-hoc signatures to install an
update. Verify the application's permission status after launching the candidate;
signature checks alone do not prove actual capture or input.

Stage and validate before stopping the old app. Keep a timestamped `.noindex` or hidden
rollback copy (root-owned bundles can require a same-parent rename), replace the whole bundle, then restart only the verified DeskPort
processes. Verify the running executable against the new file, plus bundled-host
startup. Independent Sunshine installations remain separate. Notarized local
artifacts need not be published to GitHub.

## NixOS / KDE

Build the project package directly and retain it with a dedicated GC root. Point
a user-service drop-in at its absolute executable. Install matching user-level
desktop entries for the application and both display-helper executable paths,
preserving the existing KDE Wayland interface declarations. Keep the original
entries and service definition for rollback. Refresh the desktop cache and restart
the existing user service; do not activate a new system generation.

Verify `/proc/<main-pid>/exe`, the effective service command, host startup and
existing `/dev/uinput` access. This is a persistent local override: remove it and
restore the saved launchers before returning to system-managed updates. Merely
running the normal system update does not guarantee that a service drop-in stops
taking precedence.

## Rollback and acceptance

Keep machine-specific paths, hashes and rollback commands outside public source.
Rollback replaces only this update's bundle or service/launcher overrides; it
does not reset pairings or system privacy grants. Real picture, sound, input,
display restoration and reconnect behavior are separate user acceptance checks.
