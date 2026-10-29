#!/bin/bash
# Build the pinned libdatachannel used by the browser host transport.
# ICE uses its bundled libjuice, so no GLib/libnice runtime data is involved.
set -euo pipefail
repo=${DESKPORT_SOURCE:-/src}
work=${DESKPORT_WORK:-/work}
version=0.24.1
revision=a02b751917ac8afc8c58dc6f4461d25ff9465d48
source="$work/cache/libdatachannel-$version-source"
prefix="$work/cache/libdatachannel-$version"
# Submodule revisions recorded by the v0.24.1 tag.
expected="deps/json 55f93686c01528224f448c19128836e7df245f72
deps/libjuice 5948a4162d37bc213d6051b67ee2876ccc5a99a6
deps/libsrtp ee1a77c9f9dc02c42bda9901038c500c5efe4cfa
deps/plog 94899e0b926ac1b0f4750bfbd495167b4a6ae9ef
deps/usrsctp fec583d54493f879d2ae44a743423bf8a04371ab"
# Mirror of the same tree and submodules, repacked without Git metadata.
mirror=https://github.com/keithxc/deskport/releases/download/build-deps-2026.10/libdatachannel-$version-source.tar.gz
mirror_sha256=326daf4be087d8b28c797160f6e219abe1eb94ca6a7e6433993408670ac1bc19
if [ ! -d "$source/.git" ] && [ ! -f "$source/.deskport-mirror-sha256" ]; then
    rm -rf "$source"
    if ! { git clone --quiet --branch "v$version" https://github.com/paullouisageneau/libdatachannel.git "$source" &&
           git -C "$source" submodule update --quiet --init --recursive; }; then
        echo "libdatachannel upstream clone failed; using the verified mirror" >&2
        rm -rf "$source" "$source.tar.gz"
        curl -fL --retry 3 -o "$source.tar.gz" "$mirror"
        actual_sha256=$(sha256sum "$source.tar.gz" | awk '{print $1}')
        [ "$actual_sha256" = "$mirror_sha256" ] || { echo "libdatachannel mirror checksum mismatch" >&2; exit 1; }
        mkdir -p "$source"
        tar -xzf "$source.tar.gz" --strip-components=1 -C "$source"
        rm -f "$source.tar.gz"
        echo "$mirror_sha256" > "$source/.deskport-mirror-sha256"
    fi
fi
if [ -d "$source/.git" ]; then
    [ "$(git -C "$source" rev-parse HEAD)" = "$revision" ] || { echo "libdatachannel revision mismatch" >&2; exit 1; }
    actual=$(git -C "$source" submodule status | awk '{sub(/^[-+ ]/, "", $1); print $2, $1}')
    [ "$actual" = "$expected" ] || { echo "libdatachannel submodule mismatch:" >&2; echo "$actual" >&2; exit 1; }
else
    [ "$(cat "$source/.deskport-mirror-sha256")" = "$mirror_sha256" ] || { echo "libdatachannel mirror mismatch" >&2; exit 1; }
fi
if [ ! -f "$prefix/lib/cmake/LibDataChannel/LibDataChannelConfig.cmake" ]; then
    cmake -S "$source" -B "$work/cache/libdatachannel-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=ON -DUSE_NICE=OFF -DUSE_GNUTLS=OFF -DUSE_MBEDTLS=OFF \
        -DNO_EXAMPLES=ON -DNO_TESTS=ON -DNO_WEBSOCKET=ON -DNO_MEDIA=OFF \
        -DENABLE_WARNINGS_AS_ERRORS=OFF
    cmake --build "$work/cache/libdatachannel-build" -j "${DESKPORT_JOBS:-4}"
    cmake --install "$work/cache/libdatachannel-build"
fi
# Notices travel with the binaries. The pinned Git and submodule revisions above
# identify the build source without generating another release source archive.
notices="$work/cache/libdatachannel-notices"
rm -rf "$notices"; mkdir -p "$notices"
cp "$source/LICENSE" "$notices/libdatachannel-LICENSE"
for dependency in json libjuice libsrtp plog usrsctp; do
    for file in LICENSE LICENSE.md LICENSE.MIT; do
        [ -f "$source/deps/$dependency/$file" ] && cp "$source/deps/$dependency/$file" "$notices/$dependency-$file"
    done
done
echo "$prefix"
