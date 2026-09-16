# DeskPort 0.3.6 Linux adaptive-display preview

Linux hosts can now create a dedicated virtual display and follow the resolution
requested by a bound client, including portrait and HiDPI modes. KDE Plasma 6.6+
and GNOME/Mutter use native backends behind the existing authenticated control
protocol. Physical display settings remain unchanged.

The helper keeps the virtual output alive across stream renegotiations and
releases it on sharing shutdown or process loss. Capture targets the owned output;
a missing display is an error, not permission to capture another screen.

This preview targets the Nix Linux package. macOS remains on the published 0.3.5
package, and no mobile client update is required. Other Linux desktops retain
existing-screen portal capture without adaptive virtual modes.

See [Linux adaptive workspace](LINUX_ADAPTIVE_DISPLAY.md) for dependencies,
validation and limitations. Real iPad video/input acceptance remains pending.
