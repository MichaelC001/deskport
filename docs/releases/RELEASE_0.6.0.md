# DeskPort 0.6.0

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.

## Changes

- Organize devices with local names, folders and ordering, with refreshed device settings and built-in help.
- Keep fullscreen and workspace preferences per computer and improve session takeover and endpoint handling.
- Add Windows virtual-display sizing and mirror, extended and exclusive display policies, with restoration after disconnect or helper failure.
- Verify that the owned Windows virtual display is disabled during installation, including retrying transient display-manager vetoes.
- Preserve compatibility with Qt 6.4 and improve localized interface text.

## Downloads

- macOS arm64 (macOS 26+): Developer ID signed, Apple-notarized DMG and ZIP.
- Linux x86_64: DEB, RPM, Arch, AppImage and portable archive; native portable packages require glibc 2.39+. Nix users can pin this release.
- Flatpak: **client only**; it cannot share a desktop.
- Windows x64: full installer and portable ZIP. Use the installer for the virtual-display driver and host setup.

Use your own computer and local network or configured VPN. No cloud computer, relay or VPN service is included. Mobile clients are distributed separately through their stores.

## Verification and limitations

macOS signing, notarization, stapling and extracted-ZIP Gatekeeper checks passed. Linux installation checks passed on Ubuntu, Debian, Fedora and Arch, with isolated Flatpak startup/recovery checks. The exact Windows installer passed three clean-install/startup/Defender-scan rounds with protection enabled. Windows payload/dependency audits and source CI passed. These results are not Microsoft malware-analysis clearance or a guarantee against future antivirus classifications.

Windows clipboard parity remains unverified. Package checks do not establish physical streaming, input, GPU compatibility or long-session performance on every platform. See `VERIFICATION.txt` for artifact provenance and validation scope. SHA-256 checksums and public desktop source/relink materials are attached.
