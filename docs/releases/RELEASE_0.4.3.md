# DeskPort 0.4.3

Improves connection progress and window recall, responsive device cards, session takeover and cancellation, and remote input/window handling.

## Desktop packages

macOS Apple Silicon (macOS 26+): Developer ID signed and notarized DMG/ZIP.
Linux x86_64: portable AppDir archive, DEB (Ubuntu 24.04+/Debian 13+), RPM (Fedora 44), Arch package, AppImage (glibc 2.39+), client-only Flatpak (Freedesktop 25.08), and Nix. Native Linux packages and AppImage include the patched Sunshine host; Flatpak is client-only.

Windows is not included: its separate candidate has unresolved protection and final installer validation gates. The iPhone/iPad client is distributed separately through Apple. Android store distribution requires completion of developer-account verification and production signing.

## Validation scope

See VERIFICATION.txt with the published assets for packaging, signature and isolated checks. Prior physical iPad/Android tests against macOS and KDE covered real video/input, orientation, clipboard, takeover and layout restoration. They do not establish Windows hardware acceptance, desktop-client interoperability, subjective audio quality or performance stress results. Adaptive resizing may briefly reconnect video; occasional slow-connection notices were observed during KDE display changes.

Publishing does not activate or replace installed applications.
