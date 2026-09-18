<p align="center">
  <img src="app/res/deskport.svg" alt="DeskPort" width="128" height="128">
</p>

<h1 align="center">DeskPort</h1>

<p align="center">
  <b>English</b> ·
  <a href="README.zh-CN.md">简体中文</a> ·
  <a href="README.zh-TW.md">繁體中文</a> ·
  <a href="README.ja.md">日本語</a>
</p>

<p align="center">
  A remote desktop workspace built on Moonlight and Sunshine.<br>
  Keep your remote desktop ready in the background, bring it onto your current
  workspace with one action, and tuck it away without reconnecting.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.4.1"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.4.1-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Platform status

| Platform | Role | Progress | Get it |
| --- | --- | --- | --- |
| **macOS** (Apple Silicon, macOS 26+) | Viewer + host + virtual display | ✅ Stable — 0.4.1, Apple-notarized | [DMG](https://github.com/keithxc/deskport/releases/download/v0.4.1/DeskPort-0.4.1-macos-arm64.dmg) |
| **Linux x86-64** | Viewer + host | ✅ Released — Nix 0.4.1; DEB / RPM / Arch / AppImage / Flatpak at 0.4.0 | [Releases](https://github.com/keithxc/deskport/releases) · [guide](docs/LINUX_PACKAGES.md) |
| **Linux ARM64** | Viewer + host | 🧪 Nix package definition only; build and runtime not yet verified | — |
| **Windows** | Viewer (inherited source) | 🚧 Build and packaging not yet done | — |
| **iOS / iPadOS** | 📱 Client only | ✅ Released — US$4.99 on the App Store | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| **Android** (8.0+) | 📱 Client only | 🚧 In development — native UI and MediaCodec; Google Play at the same US$4.99 | — |

> **Mobile scope:** iOS/iPadOS and Android are planned as **clients only**. They
> connect to authorized DeskPort/Sunshine hosts and never provide host features —
> no screen capture, virtual display or local input injection. Host duties stay on
> macOS, Linux and (later) Windows. The mobile clients are developed in a
> separate repository.

## Install

**iPhone / iPad** — [DeskPort on the App Store](https://apps.apple.com/us/app/deskport/id6812389978),
**US$4.99, a one-time purchase**. A native client for authorized DeskPort/Sunshine
hosts, with direct touch, an office keyboard mode and adaptive workspace sizing on
bound hosts. The Android client will carry the same US$4.99 price on Google Play
when it ships.

> 💚 **Thank you for supporting DeskPort.** The desktop app stays free and open
> source; the mobile clients are what keep the project funded. Every purchase goes
> straight back into developer accounts, code signing, test hardware and the time
> to keep building. If you have bought it — thank you, genuinely. If you have not,
> issues, translations and feedback are just as welcome.

**macOS** — [Apple-notarized DMG](https://github.com/keithxc/deskport/releases/download/v0.4.1/DeskPort-0.4.1-macos-arm64.dmg)
for Apple Silicon running macOS 26 or later. Open the DMG, drag DeskPort into
Applications, then open it. Host features require first-use Screen Recording and
Accessibility authorization. No separate Sunshine, Qt, Nix or Homebrew is needed.

**Linux** — 0.4.1 ships for Nix and NixOS only — `nix run github:keithxc/deskport/v0.4.1`,
or see [Build and run on Linux](#build-and-run-on-linux) below. It changes how the
project vendors its dependencies, not the client itself, so the
[0.4.0 DEB, RPM, Arch, AppImage and Flatpak packages](https://github.com/keithxc/deskport/releases/tag/v0.4.0)
remain current for those formats; see the
[Linux installation guide](docs/LINUX_PACKAGES.md) for supported systems and
first-use setup. Native packages require x86_64 and glibc 2.39 or newer.

## What DeskPort is

**Stable version: 0.4.1 — ready-to-install desktop packages.** DeskPort combines a
viewer and optional host in one application, with a shared device list, mutual
binding and permission controls. The macOS package includes Sunshine and a native
virtual display; the Linux native packages and AppImage include a Sunshine host for
the existing desktop. Flatpak provides the client only; Nix remains supported.

The dedicated macOS workspace follows the client window's drawable pixel size.
Clients at 150% scale or above request a 2× HiDPI workspace for sharp text.
Resizing briefly reconnects video while retaining the client window and showing
a loading animation. It is not seamless encoder reconfiguration.

See the [release notes](docs/RELEASE_0.4.1.md),
[architecture](docs/ARCHITECTURE.md) and
[macOS installation guide](docs/MACOS_PACKAGE.md).
Persistent hide/show is implemented; native long-session acceptance remains open.
Shared display policies support capable macOS and KDE hosts. Opted-in bound
DeskPort devices share text immediately and fetch images/files on demand.
Windows packaging remains unfinished. See the release notes for known limitations.

## Build and run on Linux

With Nix and flakes enabled:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

All third-party dependencies are vendored in this repository, so the Nix build
needs no extra fetches; see [docs/VENDORED.md](docs/VENDORED.md).
`nix run . -- --help` prints the inherited command-line interface. Start Sharing on the host and bind the devices before connecting. Legacy
Sunshine PIN pairing is also available. No personal host or pairing
credential is included or imported from Moonlight.
New manual addresses default to DeskPort's port `48989`. Include the port shown
on the host's sharing page if different, or use `host:47989` to connect explicitly
to a default standalone Sunshine installation. Saved/discovered endpoints retain
their own ports.

For an editable native build:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

The upstream project filenames remain unchanged to keep the fork reviewable.

## What is different today?

- Separate `deskport` executable, `DeskPort` Qt settings namespace and
  `io.github.keithxc.DeskPort` Linux application ID.
- Windowed streaming and absolute-pointer control by default.
- Mute on focus loss; game optimization, gamepad mouse, multi-controller mode,
  background gamepad input and Discord presence disabled by default.
- No upstream Moonlight update prompts for this independent application.
- A locked Nix environment and a Linux build workflow.

The desktop interface provides device, sharing and settings pages, with language
selection and separate host permissions. Closing the viewer keeps its session available and opens the device list.
An active device offers Return to desktop; other devices show details until the
current session is disconnected. Pin frequently used devices and choose a compact
list or cards. Appearance follows the system, with light and dark overrides.

## Platform scope

See the [platform status table](#platform-status) above for release state per
platform. KDE Wayland / AMD on Linux x86-64 is the first live-use target.

The first development workflow is Linux → macOS through Sunshine. Client platform
support and host support are separate: a Mac host does not require a DeskPort Mac
client. Hardware decode, live input, image quality and recall latency need real
session testing; a successful build does not establish them.

## Next milestone

Keep one session connected across **50 hide/show cycles**, show the window on the
current workspace, and reliably return local input. Measure fresh-frame latency
and background resource use separately from window appearance.

See [the roadmap](docs/ROADMAP.md) for acceptance criteria and deferred features,
and [upstream notes](docs/UPSTREAM.md) for provenance and maintenance boundaries.

## Validation

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

The smoke check uses temporary XDG configuration/cache directories and an offscreen
Qt platform. It does not pair with a host, start a stream or inject input.

## Daily desktop controls

Closing a window keeps DeskPort running in the tray/menu bar. Use **Open DeskPort**
to recall it, **Disconnect viewer** to end only the current connection, or
**Quit DeskPort** to exit the service. Local sharing continues when a viewer closes.

Plain-text clipboard sharing and system-shortcut capture are enabled by default on
both bound devices. Setting changes apply after reconnecting. Only new copies are
shared, up to 1 MiB; images and files are not transferred. In desktop pointer
mode, keyboard routing follows the pointer inside the focused video.
**Ctrl+Alt+Shift+Z** releases input;
**Ctrl+Alt+Shift+Q** disconnects the viewer. Click inside to regain input after
explicit release. OS-reserved shortcuts depend on the desktop compositor.

Login startup and recovery require an active graphical login session. See
[acceptance checks and limitations](docs/INPUT_SERVICE_ACCEPTANCE.md) before relying
on a computer for unattended access.

## License and attribution

DeskPort is an independent derivative of [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
initially based on v6.1.0. It is not an official Moonlight or Sunshine release.
Moonlight provides the streaming foundation; [Sunshine](https://github.com/LizardByte/Sunshine)
is bundled in the macOS and portable Linux host packages and supplied by the Linux Nix package.
Separately installed Sunshine services are kept independent.

GPL-3.0-or-later; see [LICENSE](LICENSE), retained source notices and the
license of each vendored dependency listed in [docs/VENDORED.md](docs/VENDORED.md). Original documentation is preserved in
[README.upstream.md](README.upstream.md).
