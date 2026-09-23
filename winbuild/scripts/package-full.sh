#!/usr/bin/env bash
# Assemble the full offline payload. Native acceptance is recorded separately.
source "$(dirname "$0")/env.sh"

VERSION="$(cat "$SRC_ROOT/app/version.txt")"
SOURCE_REVISION="${DESKPORT_SOURCE_REVISION:-$(git -C "$SRC_ROOT" rev-parse HEAD)}"
SOURCE_DIFF_SHA256="${DESKPORT_SOURCE_DIFF_SHA256:-$(git -C "$SRC_ROOT" diff HEAD --binary | sha256sum | cut -d ' ' -f 1)}"
[[ "$SOURCE_REVISION" =~ ^[0-9a-f]{40}$ ]] && [[ "$SOURCE_DIFF_SHA256" =~ ^[0-9a-f]{64}$ ]] || {
  echo "Valid source revision and diff digest are required; set DESKPORT_SOURCE_REVISION and DESKPORT_SOURCE_DIFF_SHA256 for an exported source tree." >&2
  exit 1
}
PACKAGE_SUFFIX="${PACKAGE_SUFFIX:--full}"
BUILD="$WB/build-app"
HOST_SOURCE="${DESKPORT_HOST_SOURCE_DIR:-$WB/full/sunshine-prepared}"
HOST_BUILD="${DESKPORT_HOST_BUILD_DIR:-$WB/full/host-build-prepared}"
EXE="$BUILD/app/release/DeskPort.exe"
[ -f "$EXE" ] || EXE="$BUILD/app/release/Moonlight.exe"
[ -f "$EXE" ] || { echo "application binary not found under $BUILD/app/release"; exit 1; }

STAGE="${PACKAGE_STAGE:-$WB/stage-full}"
OUT="${PACKAGE_OUT:-$WB/out-full}"
if [ -e "$STAGE" ] || [ -e "$OUT" ]; then
  echo "Use new PACKAGE_STAGE and PACKAGE_OUT paths; existing outputs are preserved." >&2
  exit 1
fi
mkdir -p "$STAGE/licenses" "$OUT"

cp "$EXE" "$STAGE/DeskPort.exe"
"$STRIP" --strip-unneeded "$STAGE/DeskPort.exe"
cp "$SRC_ROOT/app/SDL_GameControllerDB/gamecontrollerdb.txt" "$STAGE/"
cp "$SRC_ROOT/app/deskport.ico" "$STAGE/"
cp "$SRC_ROOT/LICENSE" "$STAGE/LICENSE.txt"

# Project and vendored component licences
cp "$SRC_ROOT/LICENSE"                                    "$STAGE/licenses/DeskPort-GPLv3.txt"
cp "$SRC_ROOT/shared/deskport-core/LICENSE" "$STAGE/licenses/deskport-core-GPLv3.txt"
cp "$SRC_ROOT/shared/deskport-core/NOTICE.md" "$STAGE/licenses/deskport-core-NOTICE.md"
cp "$SRC_ROOT/shared/deskport-core/portable/LICENSE" "$STAGE/licenses/deskport-core-catalog-MIT.txt"
cp "$SRC_ROOT/moonlight-common-c/moonlight-common-c/LICENSE.txt" "$STAGE/licenses/moonlight-common-c.txt"
cp "$SRC_ROOT/qmdnsengine/qmdnsengine/LICENSE.txt"        "$STAGE/licenses/qmdnsengine.txt"
cp "$SRC_ROOT/soundio/libsoundio/LICENSE"                 "$STAGE/licenses/libsoundio.txt"
cp "$SRC_ROOT/h264bitstream/h264bitstream/LICENSE"        "$STAGE/licenses/h264bitstream.txt"
cp "$SRC_ROOT/app/SDL_GameControllerDB/LICENSE"           "$STAGE/licenses/SDL_GameControllerDB.txt"

# Statically linked third-party licences, taken from the exact source trees used
cp "$WORK/SDL2/LICENSE.txt"          "$STAGE/licenses/SDL2.txt"
cp "$WORK/SDL2_ttf/LICENSE.txt"      "$STAGE/licenses/SDL2_ttf.txt"
cp "$WORK/freetype/LICENSE.TXT"      "$STAGE/licenses/FreeType.txt"
cp "$WORK/freetype/docs/FTL.TXT"     "$STAGE/licenses/FreeType-FTL.txt"
cp "$WORK/openssl/LICENSE.txt"       "$STAGE/licenses/OpenSSL.txt"
cp "$WORK/opus/COPYING"              "$STAGE/licenses/Opus.txt"
cp "$WORK/zlib/LICENSE"              "$STAGE/licenses/zlib.txt"
cp "$WORK/ffmpeg/COPYING.LGPLv2.1"   "$STAGE/licenses/FFmpeg-LGPLv2.1.txt"
cp "$WORK/qtbase/LICENSES/LGPL-3.0-only.txt" "$STAGE/licenses/Qt-LGPLv3.txt"
if [ -f "$PREFIX/lib/libplacebo.a" ]; then
  cp "$WORK/libplacebo/LICENSE"  "$STAGE/licenses/libplacebo.txt"
  cp "$WORK/glslang/LICENSE.txt" "$STAGE/licenses/glslang.txt"
fi
cp "$WB/THIRD-PARTY-NOTICES-full.txt" "$STAGE/THIRD-PARTY-NOTICES.txt"

{
  echo "DeskPort $VERSION Windows x64"
  echo "Build variant: ${PACKAGE_SUFFIX#-}"
  echo "Base source revision: $SOURCE_REVISION"
  echo "Tracked source diff SHA-256: $SOURCE_DIFF_SHA256"
  echo "Client executable SHA-256: $(sha256sum "$STAGE/DeskPort.exe" | cut -d ' ' -f 1)"
  echo "Full Windows integration candidate: client, patched host, display helper. See acceptance report."
} > "$STAGE/BUILD-INFO.txt"
python3 "$WB/scripts/collect-notices.py" "$STAGE/licenses"
mkdir -p "$STAGE/host" "$STAGE/driver"
cp "$HOST_BUILD/sunshine.exe" "$STAGE/host/deskport-host.exe"
cp "$BUILD/host/windows/release/deskport-display.exe" "$STAGE/host/deskport-display.exe"
cp "$WB/full/deskport-display-recovery.exe" "$STAGE/host/deskport-display-recovery.exe"
cp "$WB/full/deskport-maintenance.exe" "$WB/full/deskport-driver-setup.exe" "$STAGE/"
cp -R "$WB/full/upstream-assets/assets" "$STAGE/host/"
cp "$WB/full/vdd/"* "$STAGE/driver/"
cp "$SRC_ROOT/host/windows/vdd_settings.xml" "$STAGE/driver/vdd_settings.xml"
cp "$WB/full/VDD-LICENSE" "$STAGE/licenses/Virtual-Display-Driver-MIT.txt"
cp "$HOST_SOURCE/LICENSE" "$STAGE/licenses/Sunshine-GPLv3.txt"
cp "$HOST_SOURCE/deskport-session-settings.patch" "$HOST_SOURCE/deskport-session-takeover.patch" "$STAGE/licenses/"
for component in curl miniupnpc minhook onevpl; do
    mkdir -p "$STAGE/licenses/host-$component"
    find "$WB/full/$component" -maxdepth 1 -type f \( -iname '*license*' -o -iname 'copying*' \) -exec cp {} "$STAGE/licenses/host-$component/" \;
done
# Boost is linked by the host; retain its top-level license as well.
cp "$HOST_BUILD/_deps/boost-src/LICENSE_1_0.txt" "$STAGE/licenses/host-Boost-BSL-1.0.txt"
# Retain exact linked-component notices, including the separate license map.
for component in libvirtualhid libdisplaydevice ViGEmClient Simple-Web-Server nvapi; do
    directory="$HOST_SOURCE/third-party/$component"
    mkdir -p "$STAGE/licenses/host-$component"
    find "$directory" -maxdepth 1 -type f \( -iname '*license*' -o -iname 'copying*' \) -exec cp {} "$STAGE/licenses/host-$component/" \;
    if [ -d "$directory/LICENSES" ]; then cp -R "$directory/LICENSES" "$STAGE/licenses/host-$component/"; fi
done
for component in x264 x265_git SVT-AV1 FFmpeg; do
    mkdir -p "$STAGE/licenses/host-media-$component"
    find "$WB/full/media-source/$component" -maxdepth 1 -type f \( -iname '*license*' -o -iname 'copying*' -o -iname 'patents*' \) -exec cp {} "$STAGE/licenses/host-media-$component/" \;
done
cp "$SRC_ROOT/host/windows/patches/"*.patch "$STAGE/licenses/"
python3 "$WB/scripts/collect-host-notices.py" "$STAGE/licenses"
"$STRIP" --strip-unneeded "$STAGE/host/deskport-host.exe" "$STAGE/host/deskport-display.exe"
(cd "$STAGE" && sha256sum host/deskport-host.exe host/deskport-display.exe) >> "$STAGE/BUILD-INFO.txt"
# The ZIP uses portable settings; the installed payload uses the normal profile.
touch "$STAGE/portable.dat"
( cd "$STAGE" && zip -q -r "$OUT/DeskPort-$VERSION-windows-x64-portable$PACKAGE_SUFFIX.zip" . )
rm "$STAGE/portable.dat"

makensis -V2 \
  "-DVERSION=$VERSION" \
  "-DAUDIT_UNINSTALLER=$OUT/Uninstall.exe" \
  "-DPAYLOAD=$STAGE" \
  "-DOUTFILE=$OUT/DeskPort-$VERSION-windows-x64-setup$PACKAGE_SUFFIX.exe" \
  "$WB/scripts/deskport-full.nsi"

ls -l "$OUT"
