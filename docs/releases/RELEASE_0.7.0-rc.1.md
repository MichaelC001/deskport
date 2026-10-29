# DeskPort 0.7.0-rc.1

Pre-release for macOS Apple Silicon, Linux x86_64 and Windows x64. 0.6.3 remains
the stable release until device acceptance of this candidate is complete.

## Changes

- **Browser access.** Every desktop host serves a local HTTPS page, over the LAN
  and, where Tailscale or Headscale is running, the tailnet. An iPad, phone or
  computer opens it in Chrome, Safari or Edge without installing DeskPort and
  streams the desktop over WebRTC (H.264 and Opus) with touch, mouse, keyboard
  and text input.
  - Sign-in uses a six-digit code that changes every 30 seconds (RFC 6238). The
    Sharing page shows it with a countdown and can add it to an authenticator
    app such as Ente Auth; `deskport web authenticator` and
    `deskport web reset-code` do the same over SSH. Repeated wrong codes pause
    code sign-in for 15 minutes.
  - **Remember this browser for 7 days** is optional; otherwise closing the
    browser requires a new code. Devices owned by the same Tailscale user sign
    in without a code.
  - The remote desktop fits the browser window and follows resizing, rotation,
    fullscreen and a content-size control while connected. Video is encoded at
    the desktop's own resolution. Fullscreen shows only the desktop; an option
    uses the browser as an extended display where the host supports it.
- **One-time mobile invitations.** `deskport devices invite` creates a QR code
  that binds a DeskPort mobile client after confirmation on the device.
- **Experimental Linux Seamless host mode.**
- macOS packages use the system libiconv, fixing browser media for other users
  and other Macs.

## Downloads and scope

The same formats as 0.6.3: macOS arm64 (macOS 26+) DMG and ZIP; Linux DEB, RPM,
Arch, AppImage and portable archive (glibc 2.39+), Nix; Flatpak (viewer only, so
no browser access); Windows x64 Setup and portable ZIP. The browser transport
links libdatachannel 0.24.1 (MPL-2.0); its exact source is attached.

Known issue (Windows; this display code is unchanged since 0.6.3): sharing does not start while a portrait
built-in panel is part of the desktop together with an external display, in
either duplicate or extend mode (Windows rejects the layout with error 87). Using
only the external display works. If DeskPort is terminated during a session, the
next share is refused until the saved display snapshot is recovered.

Upgrading retires the earlier fixed browser access code and forgets browsers
remembered under it. Before the stable release, the following are still being
accepted on devices: iPad Safari, an authenticator app on a phone, the extended
display with a physical monitor, and browser sessions to a host installed from the
native Linux packages. A Windows 11 host installed from this Setup was checked
from Chrome over the LAN: code sign-in, video, audio, live resize and disconnect. Use the release checksums and verification report for
exact artifact provenance. Mobile clients are distributed separately.
