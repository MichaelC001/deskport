# DeskPort 0.2.0 — installable desktop release

DeskPort now provides downloadable desktop packages with bundled viewer
dependencies and an optional Sunshine host. This release carries forward the
device binding, automatic connection ports, clipboard text sharing and persistent
desktop sessions from 0.1.14.

## Downloads

| Download | Supported target | Includes host |
| --- | --- | --- |
| `DeskPort-0.2.0-macos-arm64.dmg` / `.zip` | Apple Silicon, macOS 26+ | Yes, with native virtual display |
| `deskport_0.2.0-1_amd64.deb` | Ubuntu 24.04+, Debian 13+ | Yes |
| `deskport-0.2.0-1.x86_64.rpm` | Fedora 44 | Yes |
| `deskport-0.2.0-1-x86_64.pkg.tar.zst` | Current Arch Linux | Yes |
| `DeskPort-0.2.0-x86_64.AppImage` | Linux x86_64, glibc 2.39+ | Yes |
| `DeskPort-0.2.0-client-x86_64.flatpak` | Linux x86_64, Freedesktop Platform 25.08 | Client only |

On Mac, open the DMG and drag DeskPort into Applications. All bundled executable
code has Developer ID signatures; Apple accepted the app and DMG for notarization.
Tickets are stapled, and Gatekeeper accepts the DMG and the app extracted from
the ZIP. Hosting still requires the user's Screen Recording and Accessibility
permissions. Existing installations signed with the old development identity may
need those permissions granted again.

Linux packages include a private Qt/media runtime. Native package managers resolve
the remaining system dependencies. The AppImage retains its original file path
for login startup. Flatpak uses session-bus ownership for window activation and
recovers from forced termination without stale sandbox PID locks.

See the [Linux installation guide](LINUX_PACKAGES.md) and
[macOS guide](MACOS_PACKAGE.md) for commands and first-use permissions.
Nix users can run `nix run github:keithxc/deskport/v0.2.0`.
Verify release assets against `SHA256SUMS.txt`.

## Validation and limits

Release checks cover Apple signing/notarization/stapling/Gatekeeper, the Linux Nix
build and CLI smoke, isolated UI/service checks, clean package installation on
Ubuntu 24.04, Debian 13, Fedora 44 and Arch, AppImage execution, and Flatpak GUI
startup, duplicate activation and forced-exit recovery. Packaged viewer and host
checks use separate state; no personal pairing or stream is used for acceptance.

These checks do not establish hardware decode performance, all desktop permission
flows, unattended restart or long-session reliability. Linux hosting still needs
appropriate capture/input access, including `/dev/uinput`; the installer does not
silently grant privileged device access or replace standalone Sunshine services.
Flatpak does not provide hosting or desktop login service integration. Reachable
LAN/VPN connectivity is required; DeskPort does not provision an Internet tunnel.

There are no Windows/ARM Linux packages, App Store builds, AUR entries or Flathub
listing in this release. Sunshine and related component source integration is
recorded for future work and is not implemented here. Future iOS/iPadOS/macOS
store and Android client work remains separate.

Source, build instructions and pinned dependencies are in this release's Git tag;
see [bundled components](BUNDLED_COMPONENTS.md) for upstream source and notices.
Existing 0.1.14 release assets remain unchanged. Publishing 0.2.0 does not replace
any running installation.
