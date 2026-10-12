#!/usr/bin/env bash
# Compatibility entry point: stage all DeskPort icons from the maintained SVG.
set -euo pipefail
exec python3 "$(dirname "$0")/generate-icons.py" "$@"
