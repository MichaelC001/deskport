#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
FULL="$WB/full"
TOOLCHAIN_FILE="$FULL/mingw-host-toolchain.cmake"
export PREFIX="$FULL/prefix"
mkdir -p "$PREFIX"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$WB/prefix/lib/pkgconfig"
for name in curl miniupnpc minhook onevpl; do
  if [ ! -d "$FULL/$name" ]; then
    mkdir "$FULL/$name"
    archive="$FULL/$name.tar.gz"
    [ "$name" = curl ] && archive="$FULL/$name.tar.xz"
    tar -xf "$archive" -C "$FULL/$name" --strip-components=1
  fi
done
common=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$WB/prefix;$PREFIX" -DCMAKE_FIND_ROOT_PATH="$WB/prefix;$PREFIX" -DLIB_EAY="$WB/prefix/lib/libcrypto.a" -DSSL_EAY="$WB/prefix/lib/libssl.a" -DOPENSSL_INCLUDE_DIR="$WB/prefix/include" -DOPENSSL_CRYPTO_LIBRARY="$WB/prefix/lib/libcrypto.a" -DOPENSSL_SSL_LIBRARY="$WB/prefix/lib/libssl.a" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF)
cmake -S "$FULL/curl" -B "$FULL/curl-build" "${common[@]}" -DBUILD_CURL_EXE=OFF -DCURL_USE_OPENSSL=ON -DOPENSSL_ROOT_DIR="$WB/prefix" -DOPENSSL_USE_STATIC_LIBS=ON -DCURL_USE_LIBPSL=OFF -DCURL_ZSTD=OFF -DCURL_BROTLI=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON
cmake --build "$FULL/curl-build" --parallel "$JOBS"
cmake --install "$FULL/curl-build"
cmake -S "$FULL/miniupnpc" -B "$FULL/miniupnpc-build" "${common[@]}" -DUPNPC_BUILD_SHARED=OFF -DUPNPC_BUILD_STATIC=ON -DUPNPC_BUILD_TESTS=OFF -DUPNPC_BUILD_SAMPLE=OFF
cmake --build "$FULL/miniupnpc-build" --parallel "$JOBS"
cmake --install "$FULL/miniupnpc-build"
cmake -S "$FULL/minhook" -B "$FULL/minhook-build" "${common[@]}"
cmake --build "$FULL/minhook-build" --parallel "$JOBS"
cmake --install "$FULL/minhook-build"
cmake -S "$FULL/onevpl" -B "$FULL/onevpl-build" "${common[@]}" -DBUILD_TOOLS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF
cmake --build "$FULL/onevpl-build" --parallel "$JOBS"
cmake --install "$FULL/onevpl-build"
