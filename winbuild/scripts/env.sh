# Shared environment for the DeskPort Windows x64 static cross build.
set -euo pipefail
WB="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="$(cd "$WB/.." && pwd)"
export WB SRC_ROOT
export TRIPLE=x86_64-w64-mingw32
export PREFIX="$WB/prefix"
export WORK="$WB/work"
export LOGS="$WB/logs"
mkdir -p "$PREFIX" "$WORK" "$LOGS"

export CC="$TRIPLE-gcc"
export CXX="$TRIPLE-g++"
export AR="$TRIPLE-ar"
export RANLIB="$TRIPLE-ranlib"
export STRIP="$TRIPLE-strip"
export WINDRES="$TRIPLE-windres"
export RC="$TRIPLE-windres"
export NM="$TRIPLE-nm"
export DLLTOOL="$TRIPLE-dlltool"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""

# The desktop session shares this machine: never exceed 4 build jobs and keep
# every build process on CPUs 0-3 at nice 15.
JOBS="${JOBS:-4}"
[ "$JOBS" -gt 4 ] && JOBS=4
export JOBS
export CMAKE_BUILD_PARALLEL_LEVEL="$JOBS"
NICE_WRAP=(nice -n 15 taskset -c 0-3)
export BUILD_CPUS=0-3

TOOLCHAIN_FILE="$WB/scripts/mingw-toolchain.cmake"
export TOOLCHAIN_FILE

unpack() { # unpack <src> <destdir>
  local s="$1" d="$2"
  if [ -e "$WB/source-inputs/$(basename "$s")" ]; then
    s="$WB/source-inputs/$(basename "$s")"
  fi
  if [ ! -e "$s" ]; then
    echo "Missing Windows build source input: $s" >&2
    echo "Place it under $WB/source-inputs or override the corresponding S_* variable." >&2
    return 1
  fi
  rm -rf "$d"; mkdir -p "$d"
  if [ -d "$s" ]; then cp -R "$s/." "$d/" && chmod -R u+w "$d";
  else tar -xf "$s" -C "$d" --strip-components=1; fi
}
