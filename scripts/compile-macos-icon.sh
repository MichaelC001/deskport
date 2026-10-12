#!/usr/bin/env bash
# Compile the maintained Icon Composer document without changing xcode-select.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
output=${1:?Usage: compile-macos-icon.sh OUTPUT_DIRECTORY}
export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
mkdir -p "$output/tmp"
export TMPDIR="$output/tmp"
xcrun actool --platform macosx --target-device mac \
    --minimum-deployment-target 26.0 --app-icon AppIcon \
    --output-partial-info-plist "$output/partial.plist" \
    --compile "$output" "$repo/app/AppIcon.icon"
test -s "$output/Assets.car"
