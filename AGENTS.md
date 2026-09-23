# DeskPort project instructions

- Discuss work in Chinese; public documentation, code and commit messages use English.
- Preserve upstream notices and keep changes to the media pipeline small and justified.
- Primary target: Linux KDE Wayland / AMD client to macOS Sunshine host.
- Persistent background session and one-action window recall are the first milestone.
- Use an independent application ID and settings directory. Never migrate or modify
  the user's Moonlight settings, pairing credentials or running services implicitly.
- Hiding a window must release remote input; disconnection must preserve local control.
- Do not claim native platform, visual, latency or live-input acceptance from compilation alone.
- Validate code changes with `nix build` and appropriate targeted checks. Use isolated
  test configuration; do not connect to personal hosts or inject input in CI.
- Do not publish personal hostnames, addresses, credentials, real clipboard data,
  employer material or screenshots of work applications.
- Keep `docs/ROADMAP.md` current; do not implement deferred features without a reason.
- Shared workspace policy and display contracts belong in the pinned
  `shared/deskport-core`, not a second local implementation. Read
  `docs/SHARED_CORE.md` before changing policy or wire behavior; update the Git and
  Nix pins together and run `scripts/test-core.py` plus the affected adapter tests.
- Prefer the locked project devShell for macOS build dependencies; run packaging
  through `nix develop`. See `docs/MACOS_PACKAGE.md` for commands and validation.
  Keep Apple's SDK/compiler/signing and pinned upstream media prebuilts in place.
  Inspect binary-cache availability before dependency upgrades. If migration
  requires substantial third-party source builds or complex workarounds, retain
  the working toolchain instead of forcing an all-Nix conversion.
- Before macOS packaging or installation, read `docs/MACOS_PACKAGE.md`. Run the
  signing preflight in the same execution session as packaging. A successful
  manual helper signature does not prove automation can access the private key.
  Record the verified deployment route and rollback path after installing.
- Public macOS downloads must pass `scripts/release-macos.sh`: Developer ID
  signatures for every embedded code object, Apple notarization, stapled app/DMG
  tickets and Gatekeeper verification after ZIP extraction. Local development
  signatures are not sufficient. Preserve existing release assets and their hashes;
  use distinct asset names when adding notarized builds to a published version.

- Performance/test prereleases target only mm4 (macOS arm64 signed/notarized package)
  and pk4 (NixOS x86_64 flake build). Do not build or upload other platform or
  distribution formats for these prereleases unless the user explicitly requests them.

## DeskPort release and activation workflow — 2026-09-14

- Default delivery is a mynix update for pk4 (NixOS x86_64) and mm4 (macOS
  arm64), followed by the user's manual activation and live verification.
- Only run a formal/stable release workflow when the user explicitly requests a
  formal release. Other distribution installers (DEB, RPM, Arch, AppImage and
  Flatpak) are packaged only for an explicitly requested formal release.
  Do not expand to other platforms/distribution formats, publish
  mobile clients, or merge the DeskPort main branch by default.
- Build and verify the two required targets; publish the prerelease assets needed
  by mynix, then update and push its release URL/version/hash and pinned source.
  A prerelease needed for mynix does not authorize a formal/stable release.
- The user manually runs `rebuild switch` to activate the update. Wait for the
  user to confirm activation before checking the deployed version or live behavior.
- Never directly replace `/Applications/DeskPort.app`, use administrator prompts
  to install it, run `rebuild switch`, or stop/restart deployed DeskPort services
  as part of a release. A request to develop, test, publish or update mynix does
  not authorize those deployment actions.
- macOS arm64 and NixOS x86_64 are the prerelease targets unless explicitly changed.
- Explicit installation of the mobile client to the user's iPad is separate from
  the desktop host release/activation flow and does not authorize a host update.
