#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
FULL="$WB/full"
HOST_SOURCE="${DESKPORT_HOST_SOURCE_DIR:-$FULL/sunshine-prepared}"
HOST_BUILD="${DESKPORT_HOST_BUILD_DIR:-$FULL/host-build-prepared}"
"$WB/scripts/prepare-host.sh" >/dev/null
export PREFIX="$FULL/prefix"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$WB/prefix/lib/pkgconfig"
export BUILD_VERSION=2026.906.222525 BRANCH=deskport
NPM_BIN="${DESKPORT_NPM:-$(command -v npm || true)}"
[ -n "$NPM_BIN" ] || { echo "npm is required to configure the bundled Windows host; set DESKPORT_NPM if it is not on PATH." >&2; exit 1; }
TOOLCHAIN="${DESKPORT_HOST_TOOLCHAIN_FILE:-$FULL/mingw-host-toolchain.cmake}"
[ -f "$TOOLCHAIN" ] || TOOLCHAIN="$WB/scripts/mingw-host-toolchain.cmake"
"${NICE_WRAP[@]}" cmake -S "$HOST_SOURCE" -B "$HOST_BUILD" -G Ninja \
 -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
 -DCMAKE_CXX_FLAGS="-I$WB/compat-include" -DCMAKE_C_FLAGS="-I$WB/compat-include" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/ \
 -DCMAKE_PREFIX_PATH="$PREFIX;$WB/prefix" \
 -DLIB_EAY="$WB/prefix/lib/libcrypto.a" -DSSL_EAY="$WB/prefix/lib/libssl.a" \
 -DOPENSSL_INCLUDE_DIR="$WB/prefix/include" -DOPENSSL_USE_STATIC_LIBS=ON \
 -DMINHOOK_LIBRARY="$PREFIX/lib/minhook.x64.a" -DMINHOOK_INCLUDE_DIR="$PREFIX/include" \
 -DFFMPEG_PREPARED_BINARIES="$FULL/media-prefix" \
 -DBOOST_USE_STATIC=ON -DBUILD_SHARED_LIBS=OFF \
 -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
 -DSUNSHINE_ENABLE_CUDA=OFF -DSUNSHINE_ASSETS_DIR=assets \
 -DNPM="$NPM_BIN"
"${NICE_WRAP[@]}" cmake --build "$HOST_BUILD" --target sunshine --parallel "$JOBS"
