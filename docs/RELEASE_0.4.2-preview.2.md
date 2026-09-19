# DeskPort 0.4.2-preview.2

Development prerelease for macOS arm64 and NixOS x86_64.

An interrupted client now reports that another device took over its connection
when the authenticated host supplies that reason. The desktop client consumes
that notification immediately and prevents a generic video error from replacing it.

Compact workspaces on macOS now retain their aspect ratio and backing scale while
respecting a conservative desktop-size floor after tuning. This avoids modes that
WindowServer lists but refuses to select. Very small tuning factors can therefore
reach the same minimum usable size.

Validation: shared workspace vectors, authenticated binding and takeover tests,
macOS compilation and NixOS package build. Companion mobile development builds
were checked with simulator tests and a physical Android/iPad takeover.

This is not a stable release. Activate through the updated Nix configuration and
verify the deployed desktop clients after switching. No service restart or system
activation is performed by the release workflow.
