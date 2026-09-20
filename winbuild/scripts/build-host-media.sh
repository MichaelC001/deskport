#!/usr/bin/env bash
# Build the pinned host media sources with the existing MSVCRT toolchain.
# The client prefix and its completed Qt/FFmpeg build are never modified.
source "$(dirname "$0")/env.sh"
FULL="$WB/full"
DEPS="$FULL/sunshine/third-party/build-deps"
MEDIA="$FULL/media-source"
DEST="$FULL/media-prefix"
mkdir -p "$MEDIA" "$DEST"
export PKG_CONFIG_LIBDIR="$DEST/lib/pkgconfig:$FULL/prefix/lib/pkgconfig:$WB/prefix/lib/pkgconfig"
for component in x264 x265_git SVT-AV1 FFmpeg; do
  if [ ! -d "$MEDIA/$component" ]; then
    cp -R "$DEPS/third-party/FFmpeg/$component" "$MEDIA/$component"
    rm -f "$MEDIA/$component/.git"
  fi
done
for patch in "$DEPS"/patches/FFmpeg/FFmpeg/{cbs,AMF,mf,nv-codec-headers,SVT-AV1,x264,x265}/*.patch; do
  [ -f "$patch" ] || continue
  if patch -d "$MEDIA/FFmpeg" -p1 --dry-run --forward < "$patch" >/dev/null 2>&1; then patch -d "$MEDIA/FFmpeg" -p1 --forward < "$patch";
  else patch -d "$MEDIA/FFmpeg" -p1 --dry-run --reverse < "$patch"; fi
done
for patch in "$DEPS"/patches/FFmpeg/x265_git/*.patch; do
  if patch -d "$MEDIA/x265_git" -p1 --dry-run --forward < "$patch" >/dev/null 2>&1; then patch -d "$MEDIA/x265_git" -p1 --forward < "$patch";
  else patch -d "$MEDIA/x265_git" -p1 --dry-run --reverse < "$patch"; fi
done
mkdir -p "$FULL/x264-build"
cd "$FULL/x264-build"
[ -f config.mak ] || AS=nasm bash "$MEDIA/x264/configure" --prefix="$DEST" --host=x86_64-w64-mingw32 --cross-prefix="$TRIPLE-" --enable-static --disable-cli --disable-opencl
make -j"$JOBS"
make install
common=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$WB/scripts/mingw-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEST" -DBUILD_SHARED_LIBS=OFF)
cmake -S "$MEDIA/x265_git/source" -B "$FULL/x265-build" "${common[@]}" -DENABLE_CLI=OFF -DENABLE_SHARED=OFF -DSTATIC_LINK_CRT=ON -DENABLE_HDR10_PLUS=ON
cmake --build "$FULL/x265-build" --parallel "$JOBS"
cmake --install "$FULL/x265-build"
cmake -S "$MEDIA/SVT-AV1" -B "$FULL/svt-build" "${common[@]}" -DBUILD_APPS=OFF -DBUILD_DEC=OFF -DENABLE_AVX512=ON -DSVT_AV1_LTO=OFF
cmake --build "$FULL/svt-build" --parallel "$JOBS"
cmake --install "$FULL/svt-build"
mkdir -p "$DEST/include/AMF" "$DEST/include/ffnvcodec"
cp -R "$DEPS/third-party/FFmpeg/AMF/amf/public/include/"* "$DEST/include/AMF/"
cp -R "$DEPS/third-party/FFmpeg/nv-codec-headers/include/ffnvcodec/"* "$DEST/include/ffnvcodec/"
sed "s#@@PREFIX@@#$DEST#g" "$DEPS/third-party/FFmpeg/nv-codec-headers/ffnvcodec.pc.in" > "$DEST/lib/pkgconfig/ffnvcodec.pc"
mkdir -p "$FULL/ffmpeg-build"
cd "$FULL/ffmpeg-build"
[ -f ffbuild/config.mak ] || bash "$MEDIA/FFmpeg/configure" \
 --prefix="$DEST" --target-os=mingw32 --arch=x86_64 --enable-cross-compile --cross-prefix="$TRIPLE-" \
 --cc="$CC" --cxx="$CXX" --ar="$AR" --ranlib="$RANLIB" --pkg-config=pkg-config --pkg-config-flags=--static \
 --extra-cflags="-I$DEST/include -I$FULL/prefix/include -I$WB/prefix/include" \
 --extra-ldflags="-L$DEST/lib -L$FULL/prefix/lib -L$WB/prefix/lib" \
 --extra-libs="-lstdc++ -lws2_32 -lbcrypt -lole32" \
 --disable-all --disable-autodetect --disable-iconv --enable-gpl --enable-static \
 --disable-pthreads --enable-w32threads --enable-avcodec --enable-avutil --enable-bsfs --enable-swscale \
 --enable-amf --enable-mediafoundation --enable-cuda --enable-ffnvcodec --enable-nvenc \
 --enable-libsvtav1 --enable-libx264 --enable-libx265 --enable-d3d11va --enable-libvpl \
 --enable-encoder=mpeg2video,h263p,h264_amf,hevc_amf,av1_amf,h264_mf,hevc_mf,av1_mf,h264_nvenc,hevc_nvenc,av1_nvenc,libsvtav1,libx264,libx265,h264_qsv,hevc_qsv,av1_qsv,mpeg2_qsv
make -j"$JOBS"
make install
# Sunshine consumes the pinned internal coded-bitstream API as a separate library.
mkdir -p "$FULL/cbs-build"
for name in cbs cbs_h2645 cbs_av1 cbs_vp8 cbs_vp9 cbs_mpeg2 cbs_jpeg cbs_sei h264_levels h2645_parse vp8data; do
 "$CC" -O2 -I"$FULL/ffmpeg-build" -I"$MEDIA/FFmpeg" -c "$MEDIA/FFmpeg/libavcodec/$name.c" -o "$FULL/cbs-build/$name.o"
done
"$CC" -O2 -I"$FULL/ffmpeg-build" -I"$MEDIA/FFmpeg" -c "$MEDIA/FFmpeg/libavutil/intmath.c" -o "$FULL/cbs-build/intmath.o"
"$AR" rcs "$DEST/lib/libcbs.a" "$FULL/cbs-build/"*.o
cp "$FULL/ffmpeg-build/config.h" "$DEST/include/"
# Match the internal header list exported by the pinned upstream build-deps.
python3 - "$DEPS/cmake/ffmpeg/ffmpeg.cmake" "$MEDIA/FFmpeg" "$DEST/include" <<'PY'
import pathlib,re,sys,shutil
cmake,src,dest=map(pathlib.Path,sys.argv[1:])
text=cmake.read_text()
for group,prefix in [('AVCODEC_GENERATED_SRC_PATH','libavcodec'),('AVUTIL_GENERATED_SRC_PATH','libavutil')]:
 for name in re.findall(r'\$\{'+group+r'\}/([^\s)]+\.h)',text):
  name=name.replace('${CBS_ARCH_PATH}','x86');source=src/prefix/name
  if source.exists():
   out=dest/prefix/name;out.parent.mkdir(parents=True,exist_ok=True);shutil.copy(source,out)
PY
