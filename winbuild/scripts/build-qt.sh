#!/usr/bin/env bash
# Cross-build a static Qt 6.11.1 for x86_64-w64-mingw32.
source "$(dirname "$0")/env.sh"

QT_PREFIX="$WB/qt-static"
export QT_PREFIX
S_QTBASE=/nix/store/2h62s8fj35hf19kkxj6c9r2idrgzwdky-qtbase-everywhere-src-6.11.1.tar.xz
S_QTSHADERTOOLS=/nix/store/cli9a6i42687p911rr7w6sh7fs16skdh-qtshadertools-everywhere-src-6.11.1.tar.xz
S_QTDECLARATIVE=/nix/store/3mjbivmirixkdi37ahzkr38z1cinffjz-qtdeclarative-everywhere-src-6.11.1.tar.xz
S_QTSVG=/nix/store/92vnk2mkk8zphdrlbf3kqg274lq30ajh-qtsvg-everywhere-src-6.11.1.tar.xz

stamp() { [ -f "$QT_PREFIX/.stamp-$1" ]; }
done_stamp() { mkdir -p "$QT_PREFIX"; touch "$QT_PREFIX/.stamp-$1"; }

common_args=(
  -G Ninja
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
  -DCMAKE_INSTALL_PREFIX="$QT_PREFIX"
  -DCMAKE_PREFIX_PATH="$QT_PREFIX;$PREFIX"
  -DCMAKE_BUILD_TYPE=Release
  -DQT_HOST_PATH="$QT_HOST_PATH"
  -DQT_BUILD_EXAMPLES=OFF
  -DQT_BUILD_TESTS=OFF
  -DQT_BUILD_BENCHMARKS=OFF
)

build_qtbase() {
  unpack "$S_QTBASE" "$WORK/qtbase"
  "${NICE_WRAP[@]}" cmake -S "$WORK/qtbase" -B "$WORK/qtbase/b" "${common_args[@]}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_static_runtime=ON \
    -DINPUT_openssl=linked \
    -DOPENSSL_ROOT_DIR="$PREFIX" \
    -DOPENSSL_USE_STATIC_LIBS=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_printsupport=OFF \
    -DFEATURE_system_zlib=OFF \
    -DFEATURE_dbus=OFF \
    -DFEATURE_icu=OFF \
    -DQT_FEATURE_schannel=OFF \
    -DFEATURE_zstd=OFF
  "${NICE_WRAP[@]}" cmake --build "$WORK/qtbase/b" -j"$JOBS"
  "${NICE_WRAP[@]}" cmake --install "$WORK/qtbase/b"
}

build_module() { # build_module <name> <srcarchive>
  local name="$1" src="$2"
  # Resume an interrupted build instead of discarding finished objects.
  if [ -f "$WORK/$name/b/build.ninja" ]; then
    echo "-- resuming existing build tree for $name"
  else
    unpack "$src" "$WORK/$name"
    "${NICE_WRAP[@]}" cmake -S "$WORK/$name" -B "$WORK/$name/b" "${common_args[@]}"
  fi
  "${NICE_WRAP[@]}" cmake --build "$WORK/$name/b" -j"$JOBS"
  "${NICE_WRAP[@]}" cmake --install "$WORK/$name/b"
}

build_qtshadertools() { build_module qtshadertools "$S_QTSHADERTOOLS"; }
build_qtdeclarative() { build_module qtdeclarative "$S_QTDECLARATIVE"; }
build_qtsvg() { build_module qtsvg "$S_QTSVG"; }

for t in "$@"; do
  if stamp "$t"; then echo "== skip $t"; continue; fi
  echo "== building $t"
  "build_$t" 2>&1 | tee "$LOGS/qt-$t.log"
  done_stamp "$t"
  echo "== done $t"
done
