#!/bin/bash
# Build the portable host with the same DeskPort protocol patches as Nix.
set -euo pipefail
repo=${DESKPORT_SOURCE:-/src}
work=${DESKPORT_WORK:-/work}
source="$work/cache/sunshine-vendored-source"
python3 "$repo/scripts/prepare-host-source.py" "$source"
# Every DeskPort host change is listed, in order, in host/series.
python3 "$repo/scripts/apply-host-patches.py" --platform linux-portable "$source"
datachannel=$(bash "$repo/scripts/build-linux-libdatachannel.sh" | tail -n 1)
bash "$repo/scripts/build-linux-ffmpeg.sh"
export BUILD_VERSION=2026.906.222525 BRANCH=deskport
cmake -S "$source" -B "$work/host-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="$datachannel" -DCMAKE_BUILD_RPATH="$datachannel/lib" \
    -DFFMPEG_PREPARED_BINARIES="$work/cache/ffmpeg-pinned" \
    -DCMAKE_CXX_FLAGS="-I$work/cache/vulkan-headers/include" \
    -DSUNSHINE_ASSETS_DIR=share/sunshine \
    -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
    -DSUNSHINE_ENABLE_CUDA=OFF -DSUNSHINE_BUILD_APPIMAGE=ON
cmake --build "$work/host-build" --target sunshine -j "${DESKPORT_JOBS:-4}"
