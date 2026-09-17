# Vendored prebuilt dependencies

This directory used to be the `libs` submodule pointing at
[cgutman/moonlight-qt-prebuilts](https://github.com/cgutman/moonlight-qt-prebuilts).
It is now vendored so DeskPort keeps building if that repository disappears.

- Upstream: https://github.com/cgutman/moonlight-qt-prebuilts
- Commit: `a27d6a7995ef504963fa9058c69e6ba1b449cc0f` (`v3.1.4-40-ga27d6a7`)
- Vendored: 2026-09-17

## What was kept and what was dropped

Only `mac/` (37 MB) is vendored. It is a hard build dependency: `app/app.pro`
adds `libs/mac/include`, the `SDL2`/`SDL2_ttf` framework headers and
`libs/mac/lib` to the macOS build, and installs `mac/Frameworks/*.framework`
plus `mac/lib/*.dylib` into the application bundle.

`windows/` (252 MB) was **not** vendored. Every reference to it lives inside a
`win32:` branch of `app/app.pro`, `moonlight-common-c/moonlight-common-c.pro`
and `AntiHooking/AntiHooking.pro`, and `AntiHooking` itself is gated behind
`win32:!winrt` in `moonlight-qt.pro`. DeskPort releases target macOS arm64 and
NixOS only, so those paths are never evaluated. The Windows project files are
left untouched; building for Windows again means restoring `libs/windows` from
upstream at the commit above.

Linux does not use this directory at all, and the Nix flake filters it out of
the build source.

Usage of these headers and binaries is governed by their respective upstream
licenses, as stated in [README.md](README.md).
