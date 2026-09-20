#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
FULL="$WB/full"
export PREFIX="$FULL/prefix"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$WB/prefix/lib/pkgconfig"
export BUILD_VERSION=2026.906.222525 BRANCH=deskport
cmake -S "$FULL/sunshine" -B "$FULL/host-build" -G Ninja \
 -DCMAKE_TOOLCHAIN_FILE="$FULL/mingw-host-toolchain.cmake" \
 -DCMAKE_CXX_FLAGS="-I$WB/compat-include" -DCMAKE_C_FLAGS="-I$WB/compat-include" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/ \
 -DCMAKE_PREFIX_PATH="$PREFIX;$WB/prefix" \
 -DLIB_EAY="$WB/prefix/lib/libcrypto.a" -DSSL_EAY="$WB/prefix/lib/libssl.a" \
 -DOPENSSL_INCLUDE_DIR="$WB/prefix/include" -DOPENSSL_USE_STATIC_LIBS=ON \
 -DMINHOOK_LIBRARY="$PREFIX/lib/minhook.x64.a" -DMINHOOK_INCLUDE_DIR="$PREFIX/include" \
 -DFFMPEG_PREPARED_BINARIES="$FULL/media-prefix" \
 -DBOOST_USE_STATIC=ON -DBUILD_SHARED_LIBS=OFF \
 -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
 -DSUNSHINE_ENABLE_CUDA=OFF -DSUNSHINE_ASSETS_DIR=assets \
 -DNPM=/run/current-system/sw/bin/npm
cmake --build "$FULL/host-build" --target sunshine --parallel "$JOBS"
