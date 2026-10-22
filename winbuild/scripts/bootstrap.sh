#!/usr/bin/env bash
# Build Windows x64 inside the locked x86_64 Linux Nix shell.
# bootstrap.sh [--mode build|package] [--from STEP] [--only STEP]
# Application objects belong to one run; third-party libraries use a persistent
# content-keyed cache. The default package mode builds, packages and audits.
set -euo pipefail
SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
STEPS=(inputs deps vulkan qt notices host-deps host-source host-media app maintenance host package audit)
from="${STEPS[0]}" only="" mode=package
usage() {
  echo "Usage: bootstrap.sh [--mode build|package] [--from STEP] [--only STEP]"
  echo "Steps: ${STEPS[*]}"
}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --mode|--from|--only)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      case "$1" in --mode) mode="$2";; --from) from="$2";; --only) only="$2";; esac
      shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
[[ "$mode" = build || "$mode" = package ]] || { usage >&2; exit 2; }
for s in "$from" ${only:+"$only"}; do
  [[ " ${STEPS[*]} " == *" $s "* ]] || { echo "Unknown step: $s" >&2; exit 2; }
done
[ -z "$only" ] || from="$only"
if [ "$mode" = build ] && { [[ "$from" = package || "$from" = audit ]] || [[ "$only" = package || "$only" = audit ]]; }; then
  echo 'Package and audit steps require --mode package' >&2; exit 2
fi
[ -n "${QT_HOST_PATH:-}" ] && [ -n "${GCC_SOURCE:-}" ] ||
  { echo "Run inside: nix-shell $SCRIPTS/shell.nix" >&2; exit 1; }
source "$SCRIPTS/env.sh"
export PACKAGE_SUFFIX="${PACKAGE_SUFFIX:--full}"
echo "Windows work: $DESKPORT_WINDOWS_BUILD_ROOT"
echo "Windows packages: $PACKAGE_OUT"
echo "Windows logs: $LOGS"
echo "Windows dependency cache: $DESKPORT_WINDOWS_DEPENDENCY_ROOT"

step() {
  case "$1" in
    inputs) python3 "$SCRIPTS/fetch-inputs.py" ;;
    deps) bash "$SCRIPTS/build-deps.sh" zlib openssl opus sdl2 freetype sdl2_ttf ffmpeg ;;
    vulkan) bash "$SCRIPTS/build-vulkan-deps.sh" glslang libplacebo ;;
    qt) bash "$SCRIPTS/build-qt.sh" qtbase qtshadertools qtdeclarative qtsvg ;;
    notices) python3 "$SCRIPTS/collect-toolchain-notices.py" ;;
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
started=""
for s in "${STEPS[@]}"; do
  [ "$s" = "$from" ] && started=1
  [ -n "$started" ] || continue
  [ -z "$only" ] || [ "$s" = "$only" ] || continue
  [ "$mode" != build ] || [ "$s" != package ] || break
  echo "== $s $(date -Is)"
  step "$s" 2>&1 | tee "$LOGS/$s.log"
done
echo "== complete $(date -Is)"
