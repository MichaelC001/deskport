#!/usr/bin/env bash
# Assemble the payload and build the offline NSIS installer + portable zip.
source "$(dirname "$0")/env.sh"

VERSION="$(cat "$SRC_ROOT/app/version.txt")"
PACKAGE_SUFFIX="${PACKAGE_SUFFIX:-}"
BUILD="$WB/build-app"
EXE="$BUILD/app/release/DeskPort.exe"
[ -f "$EXE" ] || EXE="$BUILD/app/release/Moonlight.exe"
[ -f "$EXE" ] || { echo "application binary not found under $BUILD/app/release"; exit 1; }
python3 "$WB/scripts/verify-version.py" "$EXE" "$VERSION"

STAGE="${PACKAGE_STAGE:-$WB/stage-audited}"
OUT="${PACKAGE_OUT:-$WB/out-audited}"
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
cp "$WB/THIRD-PARTY-NOTICES.txt" "$STAGE/THIRD-PARTY-NOTICES.txt"

{
  echo "DeskPort $VERSION Windows x64"
  echo "Build variant: ${PACKAGE_SUFFIX#-}"
  echo "Base source revision: $(git -C "$SRC_ROOT" rev-parse HEAD) plus local changes"
  echo "Client executable SHA-256: $(sha256sum "$STAGE/DeskPort.exe" | cut -d ' ' -f 1)"
  echo "Windows client-only outbound binding; no integrated host. Native acceptance pending."
} > "$STAGE/BUILD-INFO.txt"
python3 "$WB/scripts/collect-notices.py" "$STAGE/licenses"
# The ZIP uses portable settings; the installed payload uses the normal profile.
touch "$STAGE/portable.dat"
( cd "$STAGE" && zip -q -r "$OUT/DeskPort-$VERSION-windows-x64-portable$PACKAGE_SUFFIX.zip" . )
rm "$STAGE/portable.dat"

makensis -V2 \
  "-DVERSION=$VERSION" \
  "-DAUDIT_UNINSTALLER=$OUT/Uninstall.exe" \
  "-DPAYLOAD=$STAGE" \
  "-DOUTFILE=$OUT/DeskPort-$VERSION-windows-x64-setup$PACKAGE_SUFFIX.exe" \
  "$WB/scripts/deskport.nsi"

ls -l "$OUT"
