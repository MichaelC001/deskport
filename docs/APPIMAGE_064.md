# AppImage 0.6.4 compatibility preview

[Download the x86_64 prerelease](https://github.com/keithxc/deskport/releases/tag/v0.6.4).
This is an AppImage-only compatibility preview. Version 0.6.3 remains the stable
release; its existing downloads and native-package minimums are unchanged.

```sh
sha256sum -c SHA256SUMS-linux.txt
chmod +x DeskPort-0.6.4-x86_64.AppImage
./DeskPort-0.6.4-x86_64.AppImage
```

Download the checksum file alongside the AppImage. If FUSE is unavailable, use
`./DeskPort-0.6.4-x86_64.AppImage --appimage-extract-and-run`.

## What changed

The AppImage bundles its glibc loader and runtime, libstdc++, dependency closures,
Qt/media libraries and a Mesa software-rendering fallback. The viewer and private
Sunshine host retain separate library trees. This removes the former requirement
for the system to supply GLIBC_2.38/GLIBCXX_3.4.32 when starting the AppImage.
The runtime uses checksum-pinned [sharun 0.8.1](https://github.com/VHSgunzo/sharun/tree/v0.8.1),
which preserves the executable identity that Qt resource lookup and KDE sharing
permissions require. Libraries are dynamically linked inside the bundle; this is
not a fully static executable.

The application/media code is unchanged from the published main branch before
this packaging change. This release does not include other development branches.
Native installers use their original packaging path. No other 0.6.4 installers
are published by this preview.

## Verification on 2026-09-29

The checks use disposable containers or isolated desktop sessions, private
configuration and no connections to personal devices.

- Built the viewer, display helper and patched Sunshine host from source.
- The locked Nix x86_64 package build also passed with version 0.6.4.
- Ubuntu 24.04 packaging container: version/help, QML startup, authenticated host
  management API, rejected unauthenticated/unpaired requests and state isolation.
- Ubuntu 22.04 container with networking disabled: the same checks against the
  final AppImage's extracted payload, mounted read-only at a path with spaces.
- Ubuntu 22.04 Xvfb/X11: a visible DeskPort window, private glibc/loader mappings,
  preserved `/proc/self/exe`, and no shared libraries mapped from outside the
  AppImage in the software-rendering test.
- The host API process also used private glibc and retained its public executable
  path.
- Isolated KWin: the packaged display helper passed first-use/repeated permissions,
  simulated remounts, Unicode/space/symlink paths, stale-entry cleanup, setup-error
  handling, cache-discovery fallback, 12 resizes, mode/scale and display-policy
  checks, and EOF/crash layout restoration. These sessions do not change the
  user's desktop or installed DeskPort.

`VERIFICATION.txt` accompanies the release. Reproduce the container checks with
`scripts/check-linux-package.py APPDIR 0.6.4` and
`xvfb-run -a python3 scripts/check-appimage-runtime.py APPDIR`.
For the isolated KDE regression, use the locked devShell and
`scripts/test-linux-display.py APPDIR/usr/libexec/deskport-display --auto-permission`.

## Remaining limits

Ubuntu 22.04 GUI/API startup is verified; this is not a claim of compatibility
with every Linux system. The kernel, graphical/audio services and hardware/vendor
drivers still come from the host. Hardware decoding/encoding, physical input and
audio, full streaming, other compositors and long-session stability require
separate device testing. The container shares its build host's kernel, so it does
not validate Ubuntu 22.04's original kernel. GPU driver directories remain ahead
of the bundled software fallback.

AppImageHub's existing pull request concerns 0.6.3. Publishing this prerelease
does not itself update that test target or establish catalog acceptance.

## Sources and notices

- `source-materials-0.6.4.tar.gz`: the tagged DeskPort source, including its pinned
  shared core. Build with `DESKPORT_LINUX_FORMAT=appimage bash scripts/package-linux.sh`.
- [Linux host sources and FFmpeg relinking materials from 0.6.3](https://github.com/keithxc/deskport/releases/download/v0.6.3/linux-host-materials-0.6.3.tar.gz):
  the pinned host and FFmpeg sources/patches are unchanged in this preview.
- `AppImage-runtime-sources.json`: exact Ubuntu source archive URLs, versions and
  checksums for the additional runtime libraries; also included in the AppImage.
- `build-packages.txt`: the packaging container's installed package versions.
- The AppImage includes Ubuntu copyright notices and package provenance under
  each runtime's `usr/share/doc/deskport/runtime-licenses`, plus the sharun license.
  See [bundled components](BUNDLED_COMPONENTS.md) for all upstream source references.

The added runtime libraries can be replaced in an extracted AppDir. Preserve
matching loader/libc versions and each process's separate library tree when
relinking or modifying the runtime.
