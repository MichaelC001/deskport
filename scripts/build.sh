#!/usr/bin/env bash
# Stable public entry point; platform build recipes stay in their existing files.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
export PYTHONDONTWRITEBYTECODE=1
exec python3 "$repo/scripts/build-workflow.py" "$@"
