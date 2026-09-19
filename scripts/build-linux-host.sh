#!/bin/bash
# Build the portable host with the same DeskPort protocol patches as Nix.
set -euo pipefail
repo=${DESKPORT_SOURCE:-/src}
work=${DESKPORT_WORK:-/work}
source="$work/cache/sunshine-source"
revision=cb72dffa3233c5815cd5ba88f09f049dd679ba75
if [ ! -d "$source/.git" ]; then
    git clone --no-checkout https://github.com/LizardByte/Sunshine.git "$source"
    git -C "$source" checkout --detach "$revision"
fi
test "$(git -C "$source" rev-parse HEAD)" = "$revision"
git -C "$source" submodule update --init --force --depth 1 -- third-party/build-deps
git -C "$source/third-party/build-deps" submodule update --init --force --depth 1 -- \
    third-party/FFmpeg/Vulkan-Headers third-party/FFmpeg/nv-codec-headers
git -C "$source" submodule update --init --force --depth 1 -- \
    third-party/glad third-party/libdisplaydevice third-party/libvirtualhid \
    third-party/lizardbyte-common third-party/moonlight-common-c \
    third-party/plasma-wayland-protocols third-party/Simple-Web-Server \
    third-party/wayland-protocols third-party/wlr-protocols
git -C "$source/third-party/libdisplaydevice" submodule update --init --force --depth 1 -- third-party/lizardbyte-common
git -C "$source/third-party/moonlight-common-c" submodule update --init --force --depth 1 -- enet nanors
python3 "$repo/scripts/patch-host-session-takeover.py" "$source" --revert
python3 "$repo/scripts/patch-host-session-settings.py" "$source" --revert
for patch in session-settings session-takeover linux-display; do
    python3 "$repo/scripts/patch-host-$patch.py" "$source"
done
# Ubuntu 24.04's libstdc++ lacks ranges::to; only debug formatting changes.
compatibility_patch="$repo/host/linux/patches/sunshine-gcc13-log.patch"
if git -C "$source" apply --check "$compatibility_patch" 2>/dev/null; then
    git -C "$source" apply "$compatibility_patch"
else
    git -C "$source" apply --reverse --check "$compatibility_patch"
fi
export BUILD_VERSION=2026.906.222525 BRANCH=deskport
cmake -S "$source" -B "$work/host-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DSUNSHINE_ASSETS_DIR=share/sunshine \
    -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
    -DSUNSHINE_ENABLE_CUDA=OFF -DSUNSHINE_BUILD_APPIMAGE=ON
cmake --build "$work/host-build" --target sunshine -j "${DESKPORT_JOBS:-4}"
