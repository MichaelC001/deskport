# DeskPort 0.7.0-rc.2

Pre-release for macOS Apple Silicon, Linux x86_64 and Windows x64. 0.6.3 remains
the stable release until device acceptance of this candidate is complete.

## Changes since 0.7.0-rc.1

- **Windows: sharing with a portrait built-in panel and an external display.**
  Enabling the virtual display could make Windows retire the panel, split a
  duplicate group and change the external resolution, after which Windows
  rejected the layout (error 87) and sharing never started. DeskPort now
  rebuilds the complete pre-sharing layout around the virtual display, and
  stopping restores it exactly. Checked in duplicate, extend, external-only
  and panel-only modes.
- **Windows: recovery after the display guardian itself was terminated.** The
  next share recovers the saved display snapshot instead of failing until it
  was recovered by hand.
- **Windows: no 503 when a browser disconnects.** Display changes and
  restoration get the same time budget as the elevated guardian (20 and 25
  seconds), so a slow topology rebuild no longer reports failure. Display
  helper errors are now written to the host log.
- **Linux packaging** uses mirrored copies of the checksum-verified
  linuxdeploy, Qt plugin, appimagetool and AppImage runtime, because the
  upstream "continuous" files changed. The packaged tools are unchanged from
  rc.1.

## Changes in 0.7.0

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

Upgrading retires the earlier fixed browser access code and forgets browsers
remembered under it. Before the stable release, the following are still being
accepted on devices: iPad Safari, an authenticator app on a phone, the extended
display with a physical monitor, and browser sessions to a host installed from the
native Linux packages. Use the release checksums and verification report for
exact artifact provenance. Mobile clients are distributed separately.
