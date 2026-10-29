#!/usr/bin/env bash
# Materialize pinned Windows host sources; fetch only missing verified build inputs.
set -euo pipefail
source "$(dirname "$0")/env.sh"

FULL="$WB/full"
HOST_SOURCE="${DESKPORT_HOST_SOURCE_DIR:-$FULL/sunshine-prepared}"
HOST_PREPARE="$SRC_ROOT/scripts/prepare-host-source.py"

python3 "$HOST_PREPARE" "$HOST_SOURCE"
python3 "$WB/scripts/fetch-host-vendored-deps.py" "$HOST_SOURCE"

# Every DeskPort host change is listed, in order, in host/series; the
# Windows entries exclude the Linux display and macOS capture overlays.
python3 "$SRC_ROOT/scripts/apply-host-patches.py" --platform windows "$HOST_SOURCE"

printf '%s\n' "$HOST_SOURCE"
