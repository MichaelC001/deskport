# Windows development checkpoint

This branch preserves the Windows x64 host and client integration on top of
the current DeskPort 0.5.0 desktop baseline. It is not a release candidate
approved for distribution. The main release branch is unchanged.

The deliverable is a self-contained x64 installer (plus an optional portable
ZIP). End users do not need Nix, Qt, MinGW, npm, or a compiler. The maintained
cross-build recipes accept ordinary source archives from `winbuild/source-inputs`
or explicit `S_*` environment overrides; machine-specific `/nix/store` paths
are forbidden by the Windows-branch CI.

## Scope and platform impact

Windows-specific capture, driver ownership, display recovery, native OLE
clipboard and installer helpers coexist with shared changes to peer binding,
display-mode negotiation, session behavior and QML. Shared changes require
macOS and Linux regression review before merging; platform isolation must not
be assumed from Windows compilation.

## Existing evidence

Earlier Windows cross-build and constrained integration builds passed. Targeted UI, native
window transitions, H.264 decoded-frame and clipboard ownership checks passed.
Earlier VM runs exercised bidirectional streaming and clipboard transfers,
tray recall, input release, upgrades and identity retention. Some Linux input
checks used an isolated test-only XTest adapter, not native uinput.
The latest display helper preserved the physical primary through startup,
mode changes and restoration in targeted VM checks. These observations do
not certify the final combined installer on real GPU hardware.

## Outstanding acceptance

Final exact-package upgrade/uninstall/reinstall and state retention, abnormal
disconnect/reconnect, revocation/rebinding, complete current-UI streaming and
display stability, and cross-platform regression remain to be completed.
An earlier Windows Defender detection remains unresolved; testing while
protection was disabled does not establish safety or a false positive.
Normal-protection retesting is required before release.

VM testing was stopped after the user reported a VM crash and host stalls.
The cause has not been established. Do not restart heavy VM tests merely to
reproduce the old setup. Keep build outputs, private logs, screenshots and
credentials out of Git. Cross-build and packaging recipes are preserved under
`winbuild/scripts`; generated dependencies and binaries remain local.
