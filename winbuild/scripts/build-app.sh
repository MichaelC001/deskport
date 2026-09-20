#!/usr/bin/env bash
# Build the DeskPort Windows x64 client against the static Qt / deps prefix.
source "$(dirname "$0")/env.sh"

QT_PREFIX="$(cd "$WB/qt-static" && pwd -P)"
export DESKPORT_WIN_PREFIX="$PREFIX"
export DESKPORT_WIN_COMPAT_INCLUDE="$WB/compat-include"
"$WB/scripts/make-case-shims.sh" >/dev/null
BUILD="$WB/build-app"

QMAKE="${DESKPORT_WINDOWS_QMAKE:-$QT_PREFIX/bin/qmake}"
[ -x "$QMAKE" ] || { echo "qmake not found at $QMAKE"; exit 1; }

# qmake loads qdevice.pri before the mkspec, so the cross prefix has to be
# recorded there; the CMake-driven Qt build leaves it empty.
QDEVICE="$QT_PREFIX/mkspecs/qdevice.pri"
grep -q "^CROSS_COMPILE" "$QDEVICE" 2>/dev/null || echo "CROSS_COMPILE = $TRIPLE-" >> "$QDEVICE"

mkdir -p "$BUILD"
cd "$BUILD"
"${NICE_WRAP[@]}" "$QMAKE" "$SRC_ROOT/moonlight-qt.pro" -spec win32-g++ CONFIG+=release
"${NICE_WRAP[@]}" make -j"$JOBS"
