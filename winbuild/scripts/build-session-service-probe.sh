#!/usr/bin/env bash
# Development-only probe, deliberately excluded from the shipping installer.
source "$(dirname "$0")/env.sh"
mkdir -p "$WB/full"
"${NICE_WRAP[@]}" "$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -static -municode \
  "$SRC_ROOT/host/windows/session-service-probe.cpp" \
  -o "$WB/full/deskport-session-probe.exe" -ladvapi32 -lwtsapi32 -luser32
