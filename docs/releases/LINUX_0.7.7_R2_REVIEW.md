# DeskPort 0.7.7 Linux r2 review

Linux r2 is available under the [0.7.7 release](https://github.com/keithxc/deskport/releases/tag/v0.7.7).
GitHub Latest remains **0.7.6**, pending the operator's review. The original
assets and their hashes are retained; macOS and Windows packages are unchanged.

The Linux r2 source is [`1b559bf0`](https://github.com/keithxc/deskport/tree/1b559bf013d9dfbd8962af15dd63030293e858da).
The original tag identifies `6bd3d800`. The fix corrects the isolated Wayland
canvas test's own window restoration and makes package smoke requests bypass
inherited proxies. Production rendering and user recall logic are unchanged.

## AppImage check

Download [the r2 AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.7/DeskPort-0.7.7-x86_64-r2.AppImage)
and [its checksum list](https://github.com/keithxc/deskport/releases/download/v0.7.7/SHA256SUMS-linux-portable-r2.txt)
into the same directory, then run:

```sh
sha256sum --check --ignore-missing SHA256SUMS-linux-portable-r2.txt &&
chmod +x DeskPort-0.7.7-x86_64-r2.AppImage &&
./DeskPort-0.7.7-x86_64-r2.AppImage --canvas-self-test
```

This uses local sample video without connecting a remote host or initializing
normal application settings, credentials, clipboard, host services or input.
It requires the real renderer to submit its first frame. On Wayland the test
remaps its own minimized window because it has no user activation token;
the same Qt root and window ID remain, while the Wayland surface may change.
It does not establish physical scanout or actual user-activation behavior.

For normal interactive review, fully quit the previous DeskPort instance before
launching the new AppImage, so single-instance activation reaches the new file.
Keep the AppImage at its permanent path before refreshing login startup.

## Other packages and evidence

Choose Linux filenames ending in `-r2` on the release page. The application and
native package version remain 0.7.7; when replacing an installed 0.7.7 native
package, use the package manager's reinstall operation. Nix users can use source
revision `1b559bf013d9dfbd8962af15dd63030293e858da`.

- [Complete r2 checksums](https://github.com/keithxc/deskport/releases/download/v0.7.7/SHA256SUMS-r2.txt)
- [Verification and known limits](https://github.com/keithxc/deskport/releases/download/v0.7.7/VERIFICATION-linux-r2.txt)
- [Source and artifact provenance](https://github.com/keithxc/deskport/releases/download/v0.7.7/PROVENANCE-linux-r2.json)

Formal r2 AppImage checks completed 16 stages in Bazzite, Fedora, Ubuntu, Debian
and Arch; Bazzite Flatpak and the corresponding native package checks also passed.
Debian's KWin 6.3 virtual display is unsupported. Omarchy retains the 11/16
fullscreen-restore limit observed with the official 0.7.6 control. These limits
are retained in the verification record; a full pass is not claimed for them.
