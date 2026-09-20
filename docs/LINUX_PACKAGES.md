# Linux release packages

DeskPort 0.4.5 provides x86_64 Linux downloads for users who do not build from
source. The AppImage and native packages contain the viewer, a separate Sunshine
host tree, Qt and media libraries. The Flatpak is a **client-only** package.

## Choose a download

| Format | Target | Install |
| --- | --- | --- |
| portable `.tar.gz` | x86_64, glibc 2.39+ | Extract and run `DeskPort.AppDir/AppRun` |
| `.deb` | Ubuntu 24.04+, Debian 13+ | `sudo apt install ./deskport_0.4.5-1_amd64.deb` |
| `.rpm` | Fedora 44 | `sudo dnf install ./deskport-0.4.5-1.x86_64.rpm` |
| `.pkg.tar.zst` | Current Arch Linux | `sudo pacman -U ./deskport-0.4.5-1-x86_64.pkg.tar.zst` |
| `.AppImage` | Modern glibc-based desktops, glibc 2.39+ | Make executable, then open |
| `.flatpak` | Distributions with Flatpak and Freedesktop Platform 25.08 | See below |
| Nix | NixOS / Linux with Nix | `nix run github:keithxc/deskport/v0.4.5` |

Native packages install a private runtime in `/opt/deskport`, an application-menu
entry and `/usr/bin/deskport`. Package managers install required system graphics,
font, audio and PipeWire libraries. These are upstream portable binary packages,
not entries in the distributions' official repositories or the AUR. RPM support
does not imply compatibility with RHEL, CentOS or openSUSE. ARM Linux builds are
not included in this release. Ubuntu 22.04 and Debian 12 need the Flatpak or a
separately supported source/Nix build rather than these glibc 2.39 binaries.

For AppImage:

```sh
chmod +x DeskPort-0.4.5-x86_64.AppImage
./DeskPort-0.4.5-x86_64.AppImage
```

Keep the AppImage in a permanent location before enabling login startup. Moving
or deleting it breaks that saved path; reopen it at its new location and refresh
login startup. On NixOS, prefer the native Nix package; `appimage-run` can run the
AppImage with an FHS environment. Desktop graphics drivers remain system-provided.

For Flatpak:

```sh
flatpak install --user ./DeskPort-0.4.5-client-x86_64.flatpak
flatpak run io.github.keithxc.DeskPort
```

The bundle references Flathub for its Freedesktop 25.08 runtime. Runtime installation
may require an additional download. The application itself is a GitHub release
bundle, not a Flathub listing. It permits network, Wayland/X11, audio and GPU access;
it does not grant host filesystem access or execute Sunshine outside the sandbox.
Host sharing and desktop login service integration are not supported by this
client-only package. Use a native package or AppImage when this Linux computer
must also act as a host.

## First connection and hosting

Open DeskPort and add the other device's reachable address. Approved DeskPort
peers use the connection entry, initially port 48991, and refresh streaming ports
automatically. LAN or a separately configured VPN must provide reachability;
installing DeskPort does not create an Internet tunnel.

For Linux hosting, KDE and GNOME require their supported virtual-display APIs;
see [adaptive display requirements](LINUX_ADAPTIVE_DISPLAY.md). Native installers
include KWin permission entries for the display helper and bundled host. AppImage mount paths
are transient and do not install that permission entry; use a native or Nix
package for KDE virtual-display hosting. Remote keyboard/mouse input requires
permission to access `/dev/uinput`. The application shows when input setup is
needed. This release does not silently install privileged device rules, grant
capabilities, join the user to input groups or replace a standalone Sunshine
service. Device access must be configured by the system administrator using the
distribution's supported method. A logged-in desktop is required.

Close hides the application; its tray menu can reopen it or quit. Native packages
can register DeskPort's own user login service when enabled. Existing Nix-managed
startup entries retain ownership. The bundled host uses DeskPort's private
configuration and port selection, independently of standalone Sunshine.

## Build and validate

Initialize the shared core with `git submodule update --init shared/deskport-core`,
then run:

```sh
bash scripts/package-linux.sh
bash scripts/package-flatpak.sh build-linux.noindex/DeskPort.AppDir \
  build-linux.noindex/flatpak-output "$(cat app/version.txt)"
```

The rootless Podman build uses a pinned Ubuntu 24.04 image and checksum-pinned
packaging tools/Sunshine assets. The portable host is compiled from the exact
Sunshine revision in `scripts/build-linux-host.sh`, with the same session-settings,
authenticated takeover and Linux display patches used by the Nix package. Its
pinned upstream submodules and build dependencies are fetched during the build. Ubuntu package versions are recorded in the build
output; apt repositories are not a historical snapshot. Qt 6 deployment includes
Wayland and offscreen plugins. `qmake -r` refreshes nested version headers. The
pipeline checks the internal version, CLI, packaged QML startup, host state
isolation and authenticated session API before producing packages. The API check
also rejects unpaired admission and browser-origin/unauthenticated requests. nFPM creates the DEB/RPM/pacman metadata;
the payload is shared, rather than rebuilding against each distribution's Qt.

`scripts/test-linux-installers.py WORK_DIR VERSION` installs packages in clean
Ubuntu, Debian, Fedora and Arch containers and runs the same isolated smoke check.
`scripts/test-flatpak-singleinstance.py` checks session-bus ownership and crash
recovery without the sandbox; `scripts/test-flatpak-package.py VERSION` repeats
GUI launch, duplicate activation and crash recovery inside the installed Flatpak
with a private session bus and networking disabled. Set a private `FLATPAK_USER_DIR`
for test installation.
Use separate test configuration for AppImage/Flatpak and disable network during
GUI automation to avoid discovering personal hosts. Native GPU decoding, live
Wayland input, desktop permissions and long sessions require hardware acceptance;
headless installation/GUI checks do not establish those results.

Verify downloads with the release's SHA-256 file. Keep upstream notices and exact
source references; see [bundled components](BUNDLED_COMPONENTS.md). Deeper source
integration is explicitly deferred to a later release.
