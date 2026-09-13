#!/bin/bash
# The portable Linux pipeline builds the DeskPort AppImage and native packages.
set -euo pipefail
exec bash "$(dirname "$0")/package-linux.sh" "$@"
