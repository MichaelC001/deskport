# DeskPort 0.2.5 — keep text clipboard sharing active

Unsupported images/files now produce a temporary skipped-copy notice rather than
a persistent warning. Native clipboard read failures no longer bypass channel
polling and reply processing; following text copies can continue in both directions.
The notice explicitly states that text sharing remains active and expires after
five seconds. Authentication and transport errors remain visible.

The 0.2.4 fractional-scale workspace and full HiDPI frame pipeline are unchanged.

Validation covers isolated SDL/Qt non-text offers, host image/file offers, notice
expiry and subsequent bidirectional text without reconnect, plus existing Unicode,
128 MiB bounds, ordering and authenticated transport. Native clipboard behavior
still requires acceptance after manually activating both ends.

Development prerelease: macOS arm64 signed/notarized ZIP/DMG and NixOS x86_64 flake.
No main-branch merge or stable release. On Linux, fully exit an old tray instance
and reopen DeskPort if a system switch installed the new package but left the old
process running. Closing only the remote window does not exit the application.
