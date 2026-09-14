# DeskPort 0.2.4 — fractional-scale workspace preview

A client at 150% previously received a 2x Mac desktop with the same pixel count
as its window, making UI approximately 33% larger. Adaptive sizing now matches
logical desktop space by sending the full 2x backing raster: a 2868x1500 window
at 150% requests 3824x2000 pixels, equivalent to a 1912x1000 Mac desktop.

- Fractional scales between 1x and 2x use supersampled 2x frames. Native 1x and 2x
  remain pixel-matched; the viewer does not enlarge an undersampled workspace.
- The capture/encoder pipeline is unchanged: no low-resolution capture or new
  host-side downscale. At 150%, transmitted pixel count increases by about 78%.
  The existing user bitrate ceiling remains respected.
- Saved window geometry is restored, but the stream size is recomputed so old
  pixel-matched records cannot force the previous oversized desktop on reconnect.
- Existing 960x540 logical minimum and 7680x4320 backing maximum still apply.
  Above 2x, full drawable detail takes priority over matching logical UI size.

Scope: development-branch prerelease for macOS arm64 (Developer ID signed and
Apple-notarized ZIP/DMG) and NixOS x86_64 via the tagged flake. No stable release
or main-branch merge. Activate both ends manually through mynix.

Validation: isolated geometry/detail invariants across 100%, 125%, 150%, 175%
and 200%, binding/resize and saved-window tests, native builds and distribution
verification. These checks do not establish live visual quality. After activation,
check small English/Chinese text, colored glyphs, repeated resize and reconnect,
and pointer alignment. Fractional filtering and video compression can still
change sharpness; do not equate this preview with a guarantee of pixel-identical
output. Keep 0.2.3 as the rollback reference until hardware acceptance.

References:
- [Apple high-resolution model](https://developer.apple.com/library/archive/documentation/GraphicsAnimation/Conceptual/HighResolutionOSX/Explained/Explained.html)
- [SDL drawable pixel size](https://wiki.libsdl.org/SDL2/SDL_GetWindowSizeInPixels)
- [Qt physical size and scaling](https://doc.qt.io/qt-6/qscreen.html)

Physical size is advisory and is not used to override the selected output mode
or the user's desktop scaling. The Mac virtual-display helper continues to use
its existing private CoreGraphics API with runtime mode validation.
