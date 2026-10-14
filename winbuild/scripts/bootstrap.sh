#!/usr/bin/env bash
# Build the full Windows x64 installer from a clean checkout and an empty build
# root. Run on x86_64 Linux inside `nix-shell winbuild/scripts/shell.nix`.
# Every third-party input is fetched and verified from winbuild/inputs.json.
#
#   bootstrap.sh [--from STEP] [--only STEP]
#
# DESKPORT_WINDOWS_BUILD_ROOT keeps all outputs outside the source tree;
# DESKPORT_WINDOWS_INPUT_CACHE can share verified downloads between roots.
set -euo pipefail
SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_ROOT="$(cd "$SCRIPTS/../.." && pwd)"
export DESKPORT_WINDOWS_SOURCE_ROOT="${DESKPORT_WINDOWS_SOURCE_ROOT:-$SOURCE_ROOT}"
export DESKPORT_WINDOWS_BUILD_ROOT="${DESKPORT_WINDOWS_BUILD_ROOT:-$SOURCE_ROOT/winbuild}"
WB="$DESKPORT_WINDOWS_BUILD_ROOT"
mkdir -p "$WB"
# Build scripts locate their helpers through the build root.
if [ "$(cd "$WB" && pwd -P)" != "$(cd "$SCRIPTS/.." && pwd -P)" ]; then
  if [ -e "$WB/scripts" ] && [ ! -L "$WB/scripts" ]; then
    echo "$WB/scripts exists and is not a link to $SCRIPTS" >&2; exit 1
  fi
  ln -sfn "$SCRIPTS" "$WB/scripts"
fi
[ -n "${QT_HOST_PATH:-}" ] && [ -n "${GCC_SOURCE:-}" ] ||
  { echo "Run inside: nix-shell $SCRIPTS/shell.nix" >&2; exit 1; }

STEPS=(inputs deps vulkan qt notices host-deps host-source host-media app maintenance host package audit)
from="${STEPS[0]}" only=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --from) from="$2"; shift 2 ;;
    --only) only="$2"; shift 2 ;;
    *) echo "Usage: bootstrap.sh [--from STEP] [--only STEP]; steps: ${STEPS[*]}" >&2; exit 2 ;;
  esac
done
for s in "$from" ${only:+"$only"}; do
  [[ " ${STEPS[*]} " == *" $s "* ]] || { echo "Unknown step: $s" >&2; exit 2; }
done

export PACKAGE_SUFFIX="${PACKAGE_SUFFIX:--full}"
export PACKAGE_STAGE="${PACKAGE_STAGE:-$WB/stage$PACKAGE_SUFFIX}"
export PACKAGE_OUT="${PACKAGE_OUT:-$WB/out$PACKAGE_SUFFIX}"

step() {
  case "$1" in
    inputs) python3 "$SCRIPTS/fetch-inputs.py" ;;
    deps) bash "$SCRIPTS/build-deps.sh" zlib openssl opus sdl2 freetype sdl2_ttf ffmpeg ;;
    vulkan) bash "$SCRIPTS/build-vulkan-deps.sh" glslang libplacebo ;;
    qt) bash "$SCRIPTS/build-qt.sh" qtbase qtshadertools qtdeclarative qtsvg ;;
    notices) DESKPORT_WINDOWS_BUILD_ROOT="$WB" python3 "$SCRIPTS/collect-toolchain-notices.py" ;;
    host-deps) bash "$SCRIPTS/build-host-deps.sh" ;;
    host-source) bash "$SCRIPTS/prepare-host.sh" ;;
    host-media) bash "$SCRIPTS/build-host-media.sh" ;;
    app) bash "$SCRIPTS/build-app.sh" ;;
    maintenance) bash "$SCRIPTS/build-maintenance.sh" ;;
    host) bash "$SCRIPTS/build-host.sh" ;;
    package) bash "$SCRIPTS/package-full.sh" ;;
    audit) python3 "$SCRIPTS/audit-package.py" "$PACKAGE_OUT" "${PACKAGE_SUFFIX#-}" ;;
  esac
}

mkdir -p "$WB/logs"
started=""
for s in "${STEPS[@]}"; do
  [ "$s" = "$from" ] && started=1
  [ -n "$started" ] || continue
  [ -z "$only" ] || [ "$s" = "$only" ] || continue
  echo "== $s $(date -Is)"
  step "$s"
done
echo "== complete $(date -Is)"
