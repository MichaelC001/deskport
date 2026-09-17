# Shared core extraction verification — 2026-09-17

Core pin: `7f9839c566c4be58b29b225621b9d359a9807b0c`.
Both product gitlinks and the desktop flake.lock resolve to this commit.

## Scope

Extract the existing workspace arithmetic into one header-only C implementation;
retain Qt drawable-pixel and Apple logical-point adapters. Substitute shared display
message names without changing framing, subscription, parsing, trust or lifecycle.
Add protocol fixtures, pin checks, CI wiring and complete generated-source inclusion.

## Completed checks

| Check | Result |
| --- | --- |
| Core C11 and C++17 fixtures, ASan/UBSan | Passed; 24 shared vectors |
| CMake/CTest | Passed |
| Baseline adapters before extraction | Same vectors passed against original Apple and Qt headers |
| Extracted adapters | Passed; Apple 24 vectors, Qt 22 (nonfinite pixel inputs are not representable by QSize) |
| Desktop binding/display tests | 36 passed, including 9 fixture-driven requests through production TLS handler |
| Desktop saved-window/preferences tests | 9 passed |
| Apple display results/carets | 11 shared cases passed through production handler |
| Apple loopback display/lifecycle | 12 transport scenarios plus lifecycle test passed |
| Apple loopback binding | 8 passed |
| Shared mobile input/direct touch | Passed against both pinned Apple and Android Moonlight headers |
| macOS native desktop build | Passed in a fresh build directory |
| Apple simulator build | Passed |
| Android non-root Debug build | Passed |
| iPad simulator | 3 workspace/keyboard layout tests passed |
| iPhone simulator | Same 3 tests passed after explicit simulator boot |
| Generated Apple/Android trees | Core headers and license match pin; Git metadata excluded |
| Nix x86_64-linux package evaluation | Passed dry-run; no x86_64 build claimed |
| Core GitHub Actions | [Passed](https://github.com/keithxc/deskport-core/actions/runs/35173952556) |

The desktop's old build directory had stale objects referencing unrelated host and
clipboard APIs; a completely new directory built successfully. iPhone's first run
failed simulator preflight with Busy; explicit boot/bootstatus followed by a fresh
test run passed. Neither failure required a production behavior change.

## Linux package limitation

The full aarch64-linux Nix package was attempted twice. Its patched Sunshine
dependency failed when cc1plus was killed, including the retry with lower requested
build concurrency. This does not establish the cause as OOM without kernel evidence,
and does not establish a successful full Linux package build. Client-only Nix
compilation is tracked separately below; it cannot replace full package acceptance.

Client-only aarch64-linux Nix build passed, including the actual Qt adapter vector
check inside the build sandbox. To avoid the unrelated Sunshine build, this local
validation used `overrideAttrs` with `postInstall = ""` (omitting the bundled-host
installation hook) and `enableParallelBuilding = false`. No production flake change
was made to remove the host. Result:
`/nix/store/3j0hcafv297jwq4wiw4na8yflmkbdbc8-deskport-0.3.0`.
This verifies Linux client compilation, not the complete bundled-host package.

## Acceptance boundaries

Product CI workflows are added locally; only the core workflow has run remotely.
No desktop/mobile product release, service restart, system activation, physical
device install, live stream, or old-binary interoperability test is claimed.
Legacy-field fixtures establish scoped compatibility rather than exhaustive version
interoperability. Generated core headers/notices were compared and the source packager passed syntax
checks; a complete new App Store source archive was not built or published.

## Migration to main — 2026-09-17

The local extraction changes were moved without conflicts from the old
`test/macos-vm-lab` base to `dev/shared-core`, based on main `286e3d4e` (v0.3.5).
This retains the clipboard and unattended-operation features already merged in main.
Core vectors, 36 binding/display tests, 9 desktop-state tests and a fresh macOS
native build passed again. The Nix devShell build and x86_64-linux package evaluation
also passed. Earlier Linux client-only build evidence above applies to the original
extraction base; no new full Linux package or live streaming acceptance is claimed.
