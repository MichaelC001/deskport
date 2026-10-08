#!/usr/bin/env bash
# Cross-build the pinned static libdatachannel for the browser host transport.
# ICE uses its bundled libjuice; TLS uses the same static OpenSSL as the host.
set -euo pipefail
source "$(dirname "$0")/env.sh"
FULL="$WB/full"
TOOLCHAIN_FILE="${DESKPORT_HOST_TOOLCHAIN_FILE:-$WB/scripts/mingw-host-toolchain.cmake}"
export PREFIX="$FULL/prefix"
version=0.24.1
revision=a02b751917ac8afc8c58dc6f4461d25ff9465d48
source="$FULL/libdatachannel"
expected="deps/json 55f93686c01528224f448c19128836e7df245f72
deps/libjuice 5948a4162d37bc213d6051b67ee2876ccc5a99a6
deps/libsrtp ee1a77c9f9dc02c42bda9901038c500c5efe4cfa
deps/plog 94899e0b926ac1b0f4750bfbd495167b4a6ae9ef
deps/usrsctp fec583d54493f879d2ae44a743423bf8a04371ab"
if [ ! -d "$source/.git" ]; then
  rm -rf "$source"
  git clone --quiet --branch "v$version" https://github.com/paullouisageneau/libdatachannel.git "$source"
  git -C "$source" submodule update --quiet --init --recursive
fi
[ "$(git -C "$source" rev-parse HEAD)" = "$revision" ] || { echo "libdatachannel revision mismatch" >&2; exit 1; }
actual=$(git -C "$source" submodule status | awk '{sub(/^[-+ ]/, "", $1); print $2, $1}')
[ "$actual" = "$expected" ] || { echo "libdatachannel submodule mismatch:" >&2; echo "$actual" >&2; exit 1; }
[ -f "$PREFIX/lib/cmake/LibDataChannel/LibDataChannelConfig.cmake" ] && exit 0
"${NICE_WRAP[@]}" cmake -S "$source" -B "$FULL/libdatachannel-build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="$WB/prefix;$PREFIX" -DCMAKE_FIND_ROOT_PATH="$WB/prefix;$PREFIX" \
  -DOPENSSL_ROOT_DIR="$WB/prefix" -DOPENSSL_USE_STATIC_LIBS=ON \
  -DOPENSSL_INCLUDE_DIR="$WB/prefix/include" \
  -DOPENSSL_CRYPTO_LIBRARY="$WB/prefix/lib/libcrypto.a" -DOPENSSL_SSL_LIBRARY="$WB/prefix/lib/libssl.a" \
  -DBUILD_SHARED_LIBS=OFF -DUSE_NICE=OFF -DUSE_GNUTLS=OFF -DUSE_MBEDTLS=OFF \
  -DNO_EXAMPLES=ON -DNO_TESTS=ON -DNO_WEBSOCKET=ON -DNO_MEDIA=OFF \
  -DENABLE_WARNINGS_AS_ERRORS=OFF
"${NICE_WRAP[@]}" cmake --build "$FULL/libdatachannel-build" --parallel "$JOBS"
cmake --install "$FULL/libdatachannel-build"
