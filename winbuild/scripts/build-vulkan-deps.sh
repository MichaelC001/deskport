#!/usr/bin/env bash
# Optional Vulkan renderer dependencies: glslang + libplacebo (static).
source "$(dirname "$0")/env.sh"

S_GLSLANG=/nix/store/7yp1p12qkg9j9c7q1kds31vh9was40cz-source
S_LIBPLACEBO="$WB/src/libplacebo"
CROSS="$WB/scripts/meson-cross.txt"

stamp() { [ -f "$PREFIX/.stamp-$1" ]; }
done_stamp() { touch "$PREFIX/.stamp-$1"; }

build_glslang() {
  unpack "$S_GLSLANG" "$WORK/glslang"
  "${NICE_WRAP[@]}" cmake -S "$WORK/glslang" -B "$WORK/glslang/b" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_BINARIES=OFF \
    -DBUILD_EXTERNAL=OFF
  "${NICE_WRAP[@]}" cmake --build "$WORK/glslang/b" -j"$JOBS"
  "${NICE_WRAP[@]}" cmake --install "$WORK/glslang/b"
}

build_libplacebo() {
  unpack "$S_LIBPLACEBO" "$WORK/libplacebo"
  # libplacebo looks for an unprefixed dlltool to build its UCRT math import lib.
  mkdir -p "$WB/toolshim"
  ln -sfn "$(command -v "$TRIPLE-dlltool")" "$WB/toolshim/dlltool"
  export PATH="$WB/toolshim:$PATH"
  # Meson cross builds ignore CFLAGS/LDFLAGS, so bake the prefix into a
  # generated cross file.
  CROSS="$WORK/libplacebo/cross.txt"
  cat "$WB/scripts/meson-cross.txt" > "$CROSS"
  cat >> "$CROSS" <<EOC

[built-in options]
c_args = ['-I$PREFIX/include']
cpp_args = ['-I$PREFIX/include']
c_link_args = ['-L$PREFIX/lib']
cpp_link_args = ['-L$PREFIX/lib']
EOC
  # vulkan-sdk redirects the header search, so install libplacebo's own
  # bundled Vulkan-Headers into the prefix first.
  cp -R "$WORK/libplacebo/3rdparty/Vulkan-Headers/include/." "$PREFIX/include/"

  "${NICE_WRAP[@]}" meson setup "$WORK/libplacebo/b" "$WORK/libplacebo" \
    --cross-file "$CROSS" --prefix "$PREFIX" --libdir lib \
    --default-library static --buildtype release \
    -Dvulkan=enabled -Dglslang=enabled -Dshaderc=disabled \
    -Dvulkan-sdk="$PREFIX" --prefer-static \
    -Dopengl=disabled -Dd3d11=disabled -Ddemos=false -Dtests=false \
    -Dlcms=disabled -Dxxhash=disabled -Ddovi=disabled -Dlibdovi=disabled
  "${NICE_WRAP[@]}" ninja -C "$WORK/libplacebo/b" -j"$JOBS"
  "${NICE_WRAP[@]}" ninja -C "$WORK/libplacebo/b" install
}

for t in "$@"; do
  if stamp "$t"; then echo "== skip $t"; continue; fi
  echo "== building $t"
  "build_$t" 2>&1 | tee "$LOGS/dep-$t.log"
  done_stamp "$t"
  echo "== done $t"
done
