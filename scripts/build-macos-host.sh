#!/bin/bash
# Build the pinned DeskPort host with virtual-display input routing.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
source "$repo/scripts/build-paths.sh"
source_dir="$DESKPORT_HOST_SOURCE_DIR"
build_root="$DESKPORT_MACOS_BUILD_DIR"
build_dir="$build_root/sunshine-vendored-build"
python3 "$repo/scripts/prepare-host-source.py" "$source_dir"
python3 "$repo/scripts/patch-host-browser.py" "$source_dir" "$repo/host/browser" --revert
python3 "$repo/scripts/patch-host-diagnostics.py" "$source_dir"
python3 "$repo/scripts/patch-host-network.py" "$source_dir"
hid="$source_dir/third-party/libvirtualhid"
patch="$repo/host/macos/patches/libvirtualhid-target-display.patch"
if git -C "$hid" apply --check "$patch"; then
    git -C "$hid" apply "$patch"
else
    git -C "$hid" apply --reverse --check "$patch"
fi
python3 "$repo/scripts/patch-host-video-pause.py" "$source_dir" --revert
python3 "$repo/scripts/patch-host-session-takeover.py" "$source_dir" --revert
python3 "$repo/scripts/patch-host-session-settings.py" "$source_dir" --revert
# Undo our final overlay before checking the earlier pinned patches on rebuilds.
sck_patch="$repo/host/macos/patches/sunshine-screen-capture-kit.patch"
if git -C "$source_dir" apply --reverse --check "$sck_patch" 2>/dev/null; then
    git -C "$source_dir" apply --reverse "$sck_patch"
fi
# Bound the first-frame wait so a silent virtual display cannot hang /resume.
capture_patch="$repo/host/macos/patches/sunshine-capture-timeout.patch"
if git -C "$source_dir" apply --check "$capture_patch"; then
    git -C "$source_dir" apply "$capture_patch"
else
    git -C "$source_dir" apply --reverse --check "$capture_patch"
fi
# Use pkg-config's resolved library path, not a Homebrew-only -l search.
link_patch="$repo/host/macos/patches/sunshine-pkgconfig-link.patch"
if git -C "$source_dir" apply --check "$link_patch"; then
    git -C "$source_dir" apply "$link_patch"
else
    git -C "$source_dir" apply --reverse --check "$link_patch"
fi
python3 "$repo/scripts/patch-host-smart-stream.py" "$source_dir"
smart_patch="$repo/host/macos/patches/sunshine-smart-streaming.patch"
if git -C "$source_dir" apply --check "$smart_patch"; then
    git -C "$source_dir" apply "$smart_patch"
else
    git -C "$source_dir" apply --reverse --check "$smart_patch"
fi
mkdir -p "$source_dir/src/deskport/common" "$source_dir/src/deskport/macos"
cp "$repo/host/common/smartstream.h" "$source_dir/src/deskport/common/smartstream.h"
cp "$repo/host/macos/pixelmatch.h" "$source_dir/src/deskport/macos/pixelmatch.h"
git -C "$source_dir" apply --check "$sck_patch"
git -C "$source_dir" apply "$sck_patch"
cp "$repo/host/macos/screen-video.h" "$repo/host/macos/screen-video.m" "$source_dir/src/deskport/macos/"
cp "$repo/host/macos/admitted-display.h" "$source_dir/src/deskport/macos/"
cp "$repo/host/macos/admitted-display.h" "$hid/src/platform/macos/deskport-admitted-display.h"
python3 "$repo/scripts/patch-host-session-settings.py" "$source_dir"
python3 "$repo/scripts/patch-host-session-takeover.py" "$source_dir"
cp "$repo/host/common/encoderpolicy.h" "$source_dir/src/deskport/common/encoderpolicy.h"
python3 "$repo/scripts/patch-host-encoder-policy.py" "$source_dir"
python3 "$repo/scripts/patch-host-macos-lifecycle.py" "$source_dir"
cp "$repo/host/common/inputactivity.h" "$source_dir/src/deskport/common/"
python3 "$repo/scripts/patch-host-input-activity.py" "$source_dir"
cp "$repo/host/common/framecadence.h" "$source_dir/src/deskport/common/"
python3 "$repo/scripts/patch-host-sync-cadence.py" "$source_dir"
python3 "$repo/scripts/patch-host-video-pause.py" "$source_dir"
python3 "$repo/scripts/patch-host-browser.py" "$source_dir" "$repo/host/browser"
sdk=$(xcrun --sdk macosx --show-sdk-path)
pc="$build_root/host-pkgconfig"
mkdir -p "$pc"
curl_version=$(sed -n 's/^#define LIBCURL_VERSION "\([^"]*\)"/\1/p' "$sdk/usr/include/curl/curlver.h")
cat > "$pc/libcurl.pc" <<PC
Name: libcurl
Description: macOS SDK libcurl
Version: $curl_version
Libs: -lcurl
Cflags:
PC
if [ "${DESKPORT_NIX_DEPS:-0}" = 1 ]; then
    export PKG_CONFIG_PATH="$pc:${PKG_CONFIG_PATH:-}"
else
    export PKG_CONFIG_PATH="$pc:/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/openssl@3/lib/pkgconfig"
fi
# The vendored Sunshine source is materialized without Git metadata, so its
# upstream CMake fallback would otherwise download a mutable "latest" FFmpeg
# archive. Keep the known-compatible macOS archive content-addressed instead.
if [ -z "${DESKPORT_FFMPEG_ROOT:-}" ]; then
    ffmpeg_manifest="$repo/host/vendor/sunshine-macos-ffmpeg.json"
    ffmpeg_url=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["url"])' "$ffmpeg_manifest")
    ffmpeg_sha256=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["sha256"])' "$ffmpeg_manifest")
    ffmpeg_root="$build_root/ffmpeg-$ffmpeg_sha256"
    if [ ! -f "$ffmpeg_root/lib/libavcodec.a" ]; then
        ffmpeg_stage=$(mktemp -d "$build_root/.ffmpeg.XXXXXX")
        trap 'rm -rf "$ffmpeg_stage"' EXIT
        /usr/bin/curl --fail --location --retry 3 --output "$ffmpeg_stage/ffmpeg.tar.gz" "$ffmpeg_url"
        actual_ffmpeg_sha256=$(shasum -a 256 "$ffmpeg_stage/ffmpeg.tar.gz" | awk '{print $1}')
        if [ "$actual_ffmpeg_sha256" != "$ffmpeg_sha256" ]; then
            echo "Pinned macOS FFmpeg checksum mismatch" >&2
            exit 1
        fi
        tar -xzf "$ffmpeg_stage/ffmpeg.tar.gz" -C "$ffmpeg_stage"
        for library in libavcodec.a libswscale.a libavutil.a libcbs.a; do
            test -f "$ffmpeg_stage/ffmpeg/lib/$library" || {
                echo "Pinned macOS FFmpeg archive is missing $library" >&2
                exit 1
            }
        done
        mv "$ffmpeg_stage/ffmpeg" "$ffmpeg_root"
        rm "$ffmpeg_stage/ffmpeg.tar.gz"
        rmdir "$ffmpeg_stage"
        trap - EXIT
    fi
    export DESKPORT_FFMPEG_ROOT="$ffmpeg_root"
fi
extra=()
if [ -n "${DESKPORT_CMAKE_PREFIX_PATH:-}" ]; then
    extra+=("-DCMAKE_PREFIX_PATH=$DESKPORT_CMAKE_PREFIX_PATH" -DOPUS_USE_STATIC=OFF -DBOOST_USE_STATIC=OFF)
fi
if [ -n "${DESKPORT_FFMPEG_ROOT:-}" ]; then
    extra+=("-DFFMPEG_PREPARED_BINARIES=$DESKPORT_FFMPEG_ROOT")
fi
export BUILD_VERSION=2026.906.222525 BRANCH=deskport
cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
    -DOPENSSL_ROOT_DIR="${DESKPORT_OPENSSL_ROOT:-/opt/homebrew/opt/openssl@3}" \
    -DOpus_ROOT_DIR="${DESKPORT_OPUS_ROOT:-/opt/homebrew/opt/opus}" \
    -DICU_ROOT="${DESKPORT_ICU_ROOT:-/opt/homebrew/opt/icu4c@78}" -DSUNSHINE_PUBLISHER_NAME=DeskPort \
    -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/keithxc/deskport/issues "${extra[@]}"
cmake --build "$build_dir" --target sunshine -j"${DESKPORT_JOBS:-4}"
