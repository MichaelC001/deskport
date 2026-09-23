#!/usr/bin/env bash
# Build the DeskPort Windows x64 client against the static Qt / deps prefix.
source "$(dirname "$0")/env.sh"

QT_PREFIX="$(cd "$WB/qt-static" && pwd -P)"
export DESKPORT_WIN_PREFIX="$PREFIX"
export DESKPORT_WIN_COMPAT_INCLUDE="$WB/compat-include"
"$WB/scripts/make-case-shims.sh" >/dev/null
BUILD="$WB/build-app"

mkdir -p "$BUILD"
QMAKE=("${DESKPORT_WINDOWS_QMAKE:-$QT_PREFIX/bin/qmake}")
if [ -z "${DESKPORT_WINDOWS_QMAKE:-}" ] && [ -n "${QT_HOST_PATH:-}" ]; then
    # Cached Qt wrappers embed the original build machine's host-tool path,
    # which can disappear after Nix garbage collection. Keep the cached wrapper
    # unchanged and resolve host tools from the current locked shell instead.
    python3 - "$QT_PREFIX/bin/target_qt.conf" "$BUILD/qt.conf" "$QT_HOST_PATH" "$QT_PREFIX" <<'PY'
import configparser
import sys
config = configparser.ConfigParser()
config.optionxform = str
config.read(sys.argv[1])
config['Paths']['Prefix'] = sys.argv[4]
config['Paths']['HostPrefix'] = sys.argv[3]
config['Paths']['HostData'] = sys.argv[4]
with open(sys.argv[2], 'w') as output:
    config.write(output, space_around_delimiters=False)
PY
    QMAKE=("$QT_HOST_PATH/bin/qmake" -qtconf "$BUILD/qt.conf")
fi
[ -x "${QMAKE[0]}" ] || { echo "qmake not found at ${QMAKE[0]}"; exit 1; }

# qmake loads qdevice.pri before the mkspec, so the cross prefix has to be
# recorded there; the CMake-driven Qt build leaves it empty.
QDEVICE="$QT_PREFIX/mkspecs/qdevice.pri"
grep -q "^CROSS_COMPILE" "$QDEVICE" 2>/dev/null || echo "CROSS_COMPILE = $TRIPLE-" >> "$QDEVICE"

cd "$BUILD"
"${NICE_WRAP[@]}" "${QMAKE[@]}" "$SRC_ROOT/moonlight-qt.pro" -spec win32-g++ CONFIG+=release
"${NICE_WRAP[@]}" make -j"$JOBS"
