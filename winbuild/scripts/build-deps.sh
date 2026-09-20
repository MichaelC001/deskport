#!/usr/bin/env bash
# Build the static third-party dependencies for the Windows x64 cross build.
source "$(dirname "$0")/env.sh"

S_ZLIB=/nix/store/13vkic2v8zac4m6p38asdhasb3xylv07-zlib-1.3.2.tar.gz
S_OPENSSL=/nix/store/28gjcxgpb8wznqc5dn45s5xrnbvvf2v7-openssl-3.6.3.tar.gz
S_OPUS=/nix/store/xyzmpla2ax3r79d4m0l6g8jmwris7a8q-opus-1.6.1.tar.gz
S_SDL2="$WB/src/SDL2-2.32.10.tar.gz"
S_SDL2TTF=/nix/store/vx6r4gzgx7wqx4qdn8qxfwvh072gbwq5-SDL2_ttf-2.24.0.tar.gz
S_FREETYPE=/nix/store/043d41b261zzgpmrxp4islgh9l1j2j67-freetype-2.14.3.tar.xz
S_FFMPEG=/nix/store/lrsm8w8023kpna6kpw4kr4ixgkm850i9-ffmpeg
S_DAV1D=/nix/store/2h6bp1bn3fi1nsj5y82nnn8g9y5m63sh-source
S_LIBPLACEBO=/nix/store/8naam5d8x9d7wv7a8czhzszgnfqj1dns-source

stamp() { [ -f "$PREFIX/.stamp-$1" ]; }
done_stamp() { touch "$PREFIX/.stamp-$1"; }

build_zlib() {
  unpack "$S_ZLIB" "$WORK/zlib"
  "${NICE_WRAP[@]}" cmake -S "$WORK/zlib" -B "$WORK/zlib/b" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DZLIB_BUILD_EXAMPLES=OFF
  "${NICE_WRAP[@]}" cmake --build "$WORK/zlib/b" -j"$JOBS"
  "${NICE_WRAP[@]}" cmake --install "$WORK/zlib/b"
  # zlib's CMake build always emits a DLL; keep only the static archive and
  # give it the conventional -lz name.
  mv -f "$PREFIX/lib/libzs.a" "$PREFIX/lib/libz.a"
  rm -f "$PREFIX"/lib/libz.dll.a "$PREFIX"/bin/libz.dll
}

build_openssl() {
  unpack "$S_OPENSSL" "$WORK/openssl"
  ( cd "$WORK/openssl" && \
    env -u AR -u RANLIB -u NM -u RC -u WINDRES -u STRIP -u DLLTOOL -u CXX \
    CC=gcc ./Configure mingw64 no-shared no-tests no-apps no-docs \
      --prefix="$PREFIX" --libdir=lib --cross-compile-prefix="$TRIPLE-" && \
    "${NICE_WRAP[@]}" make -j"$JOBS" && "${NICE_WRAP[@]}" make install_sw )
}

build_opus() {
  unpack "$S_OPUS" "$WORK/opus"
  "${NICE_WRAP[@]}" cmake -S "$WORK/opus" -B "$WORK/opus/b" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF
  "${NICE_WRAP[@]}" cmake --build "$WORK/opus/b" -j"$JOBS"
  "${NICE_WRAP[@]}" cmake --install "$WORK/opus/b"
}

build_sdl2() {
  unpack "$S_SDL2" "$WORK/SDL2"
  "${NICE_WRAP[@]}" cmake -S "$WORK/SDL2" -B "$WORK/SDL2/b" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON \
    -DSDL_TEST=OFF -DSDL_STATIC_PIC=ON
  "${NICE_WRAP[@]}" cmake --build "$WORK/SDL2/b" -j"$JOBS"
  "${NICE_WRAP[@]}" cmake --install "$WORK/SDL2/b"
}

build_freetype() {
  unpack "$S_FREETYPE" "$WORK/freetype"
  "${NICE_WRAP[@]}" cmake -S "$WORK/freetype" -B "$WORK/freetype/b" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_BZIP2=ON \
    -DFT_DISABLE_PNG=ON -DFT_REQUIRE_ZLIB=ON
  "${NICE_WRAP[@]}" cmake --build "$WORK/freetype/b" -j"$JOBS"
  "${NICE_WRAP[@]}" cmake --install "$WORK/freetype/b"
}

build_sdl2_ttf() {
  unpack "$S_SDL2TTF" "$WORK/SDL2_ttf"
  "${NICE_WRAP[@]}" cmake -S "$WORK/SDL2_ttf" -B "$WORK/SDL2_ttf/b" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DSDL2TTF_SAMPLES=OFF -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_VENDORED=OFF \
    -DSDL2TTF_INSTALL=ON
  "${NICE_WRAP[@]}" cmake --build "$WORK/SDL2_ttf/b" -j"$JOBS"
  "${NICE_WRAP[@]}" cmake --install "$WORK/SDL2_ttf/b"
}

build_ffmpeg() {
  unpack "$S_FFMPEG" "$WORK/ffmpeg"
  local extra=()
  if [ -f "$PREFIX/lib/libdav1d.a" ]; then
    extra+=(--enable-libdav1d --enable-decoder=libdav1d)
  fi
  ( cd "$WORK/ffmpeg" && ./configure \
      --prefix="$PREFIX" --arch=x86_64 --target-os=mingw32 \
      --cross-prefix="$TRIPLE-" --enable-cross-compile \
      --pkg-config=pkg-config --pkg-config-flags=--static \
      --enable-static --disable-shared --disable-programs --disable-doc \
      --disable-everything --disable-network --disable-autodetect \
      --disable-avdevice --disable-avfilter \
      --disable-swresample \
      --enable-avcodec --enable-swscale \
      --enable-avformat --disable-demuxers --disable-muxers \
      --disable-protocols --disable-bsfs \
      --enable-decoder=h264 --enable-decoder=hevc --enable-decoder=av1 \
      --enable-parser=h264 --enable-parser=hevc --enable-parser=av1 \
      --enable-d3d11va --enable-dxva2 \
      --enable-hwaccel=h264_dxva2 --enable-hwaccel=hevc_dxva2 --enable-hwaccel=av1_dxva2 \
      --enable-hwaccel=h264_d3d11va --enable-hwaccel=h264_d3d11va2 \
      --enable-hwaccel=hevc_d3d11va --enable-hwaccel=hevc_d3d11va2 \
      --enable-hwaccel=av1_d3d11va --enable-hwaccel=av1_d3d11va2 \
      "${extra[@]}" && \
    "${NICE_WRAP[@]}" make -j"$JOBS" && "${NICE_WRAP[@]}" make install )
}

for t in "$@"; do
  if stamp "$t"; then echo "== skip $t (already built)"; continue; fi
  echo "== building $t"
  "build_$t" 2>&1 | tee "$LOGS/dep-$t.log"
  done_stamp "$t"
  echo "== done $t"
done
