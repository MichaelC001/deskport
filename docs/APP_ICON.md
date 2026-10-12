# DeskPort application icon

`app/res/deskport.svg` is the maintained geometry source: the mint/amber kite
lifts a tilted teal terminal display from an ocean-blue tile. Broad silhouettes,
a thicker frame and restrained highlights keep the mark readable at small sizes.
The same source also generates monochrome tray templates: keep the actual kite,
lift line, tilted display and stand geometry, remove the application tile/materials,
terminal prompt and tiny ribbon decoration, and increase optical stroke weights
for status-bar sizes. End the original lift curve at the display's outer silhouette
so it cannot show through the transparent screen. Keep the screen interior empty
at tray sizes. Do not redraw the tray artwork independently.

Regenerate all platform assets into an external staging directory:

```sh
icons=$(python3 ../tools/build-paths.py --project deskport --task icons \
  --purpose work)
python3 scripts/generate-icons.py --output "$icons"
```

This requires ImageMagick and Pillow. Review the PNGs at 16, 32, 48 and 128 pixels
before copying `deskport.ico`, `deskport.icns`, `AppIcon.icon/` into `app/` and
the size-specific PNGs into `app/res/icons/<size>/deskport.png`. It also produces
`deskport-tray-black.svg` and `deskport-tray-white.svg`; copy these
reviewed templates into `app/res/`. macOS keeps the black SVG as an AppKit template
through `QIcon::setIsMask(true)`, while other platforms select the contrast variant.
The templates have identical geometry and differ only in foreground color.
The generator extracts every Apple SVG layer from the same master; do not edit the generated
layer geometry independently. Apple's background palette comes from the master
SVG; the generator defines its appearance materials. Apple supplies the outer
mask and shadow, so the portable tile and baked shadows are not duplicated as
native layers. All PNG/ICO/ICNS corners preserve alpha.

Pass `--mobile` to also export a self-contained `mobile/` snapshot: the same
layered Apple icon, opaque legacy Apple pixels, a transparent in-app mark and
Android adaptive/themed/legacy launcher resources. Its manifest records the
canonical SVG SHA-256 and every derived asset hash. Native consumers validate
this snapshot during preparation and retain their platform build hooks.
Android foreground artwork stays within the circular 66dp safe zone of the
108dp layer, following the [adaptive icon specification](https://developer.android.com/develop/ui/views/launch/icon_design_adaptive).
Brand artwork and product packaging remain separate from deskport-core's
protocol and workspace policy; changing the brand does not advance core pins.

macOS qmake invokes `scripts/compile-macos-icon.sh` before linking and bundles
`Assets.car` alongside the portable `deskport.icns` fallback. This requires full
Xcode 26 or newer; `DEVELOPER_DIR` defaults to the Xcode application without
changing the system's developer selection. `CFBundleIconName=AppIcon` selects
the native layered Default, Dark and Clear appearances, following Apple's
[Icon Composer workflow](https://developer.apple.com/documentation/xcode/creating-your-app-icon-using-icon-composer).
Qt and SDL must not replace the macOS application icon at runtime.
`scripts/check-macos-bundle.py` rejects missing icon names or resources.

Windows qmake embeds `app/deskport.ico` in the executable. The current NSIS Setup
and portable packaging scripts use that same ICO. The upstream Moonlight WiX
and Steam Link templates are retained as upstream material, not DeskPort release
entry points. Linux installs the master SVG plus 16–512 pixel hicolor PNGs;
the desktop file, AppImage, native packages and Flatpak refer to that mark.

Build and package validation is separate from native display acceptance. Use an
isolated, uninstalled app on macOS and an owned test configuration elsewhere;
never replace a deployed app or clear system icon caches to inspect a candidate.
