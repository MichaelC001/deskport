# Repository packaging verification — 2026-09-27

## Inputs

- Public desktop release: `v0.6.3`, commit `84ba33d9bdd666d32df79c686d3f77495dd186ff`.
- Portable archive: `DeskPort-0.6.3-linux-x86_64.tar.gz`, SHA-256
  `04c2e732655b9d939ae606477fe07484e63e534964ed4d6ff277867bff00a50c`.
- Shared core: `e71b21808e15c8bcd55d4af3f7c0cbc769ef0bcb`.
- Nixpkgs: `e158d9ed9b51c98974c5e66e1ba1c9e0255fecaa` (nixos-unstable).
- Architecture: Linux x86_64. Nix sandbox enabled; builds limited to four cores
  and one concurrent derivation.

## AUR candidate

Passed in disposable Arch Linux containers, using normal-user `makepkg` and
container-local pacman:

- Download hash verification, package build and generated `.SRCINFO`.
- Dependency installation and `0.6.3-1` package installation.
- CLI version/help and desktop-file validation.
- Upgrade to a test-only `pkgrel=2`, version verification, then removal.
- Application files removed and isolated user configuration preserved.
- Existing `scripts/check-linux-package.py`: offscreen QML startup, host version,
  authenticated loopback session API, rejection of unauthorized/browser requests,
  web assets, and state isolation.

`namcap` was run before and after dependency installation. It is **not a clean
lint result**: it flags the intentional `/opt/deskport` ELF layout, bundled QML
and Qt libraries, runtime/dlopen dependencies, and upstream prebuilt ELF
hardening/stripping properties. The missing hicolor theme dependency was fixed;
standard runtime dependencies are declared. Private library/QML paths were also
checked by the actual offscreen application and host API smoke test. Remaining
prebuilt hardening warnings are not fixed by repackaging and are not represented
as passing checks.

The submitted recipe remains `pkgrel=1`; the test-only upgrade was not published.
No AUR account or package was created: the maintainer encountered the official
new-account registration pause. The recipe can be built locally meanwhile.

## Nixpkgs candidate

The first sandboxed full build and version test passed. Isolation testing then
found that a bare host symlink created default Sunshine configuration. The
candidate now uses a private host launcher, and its sandboxed configuration
isolation test passes, including a service configuration directory and an existing
standalone Sunshine sentinel.

The initial GUI smoke test exposed a null Vulkan function call in SDL2-compat
when probing a fallback window without Vulkan support. A backport checks
`SDL_WINDOW_VULKAN` before querying Vulkan extensions; the same fix is in the
DeskPort source tree. The final sandboxed rebuild passed, together with all three
package tests (`version`, `hostIsolation`, `headlessStartup`). Copying the candidate
into Nixpkgs' `pkgs/by-name/de/deskport` and adding the maintainer record produced
the same package and test derivations through `nix-build -A`.

The final output also passed the existing isolated package smoke test: GUI
startup, authenticated host session API, authorization rejection, host web assets,
and state isolation. Both KDE display permission entries resolve to existing
packaged executables.

Final outputs:

- Package: `/nix/store/0fjif7ghc8y045d52m2sn5yfxkyj86r2-deskport-0.6.3`
- Version test: `/nix/store/04m74a4z379z4q50i0s0xrvzxfh66ffz-deskport-0.6.3-test-version`
- Isolation test: `/nix/store/xlrn6ip4d22mzb92hk7ryqmgw9mj3acf-deskport-host-config-isolation`
- Headless startup: `/nix/store/16kzrvapy04fwl3gynl79a4nzjpr500w-deskport-headless-startup`

## Scope and publication

These checks do not establish physical capture, hardware encoding/decoding,
streaming latency, remote input, reconnect or desktop-session acceptance. Existing
personal hosts and services were not installed, replaced, activated or restarted.

AUR publication awaits account-registration availability. Nixpkgs submission
awaits the responsible maintainer's review and the required AI-assistance
disclosure; compilation alone is not repository acceptance. README status must
change only after actual publication/merge and channel verification.
