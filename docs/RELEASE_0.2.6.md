# DeskPort 0.2.6 — background Mac clipboard copies

Fixes missed Mac-to-client text copies while DeskPort stays in the background.
Qt Cocoa's clipboard change signal depends on app activation; the host now
checks the native pasteboard change counter on authenticated polls and refreshes
its text snapshot only when changed. Stable clipboard text is not re-encoded.

Validation adds external-process writes to a unique native named pasteboard
without activation and authenticated clipboard tests with Qt notifications
suppressed, including image/file-to-text recovery. These replace the previous
assumption that in-process Qt clipboard tests cover background external copies.
Tests never read or overwrite the user's general clipboard.

Clipboard content skipping from 0.2.5 and HiDPI/media behavior from 0.2.4 are
unchanged. Native end-to-end acceptance remains after manual activation: copy
text in a Mac application while DeskPort is backgrounded, paste on the client,
then repeat in the opposite direction and after an unsupported image/file copy.

Development prerelease on dev/client-session-settings: signed/notarized macOS
arm64 ZIP/DMG and NixOS x86_64 flake. No stable release or main merge. Fully exit
an old Linux tray instance after switching if it remains alive.
