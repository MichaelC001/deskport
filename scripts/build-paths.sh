#!/bin/bash
# Resolve checkout-specific build paths without creating files or directories.
# Source this file from Bash, or pass a path name to print it for other callers.
deskport_checkout=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
deskport_checkout_name=$(printf '%s' "${deskport_checkout##*/}" | LC_ALL=C tr -c '[:alnum:]_.-' '-')
deskport_checkout_hash=$(printf '%s' "$deskport_checkout" | cksum | awk '{print $1}')
DESKPORT_BUILD_ROOT=${DESKPORT_BUILD_ROOT:-$HOME/mygit/build/deskport}
case "$DESKPORT_BUILD_ROOT" in
    /*) ;;
    *) DESKPORT_BUILD_ROOT="$deskport_checkout/$DESKPORT_BUILD_ROOT" ;;
esac
DESKPORT_CHECKOUT_BUILD_ROOT="$DESKPORT_BUILD_ROOT/$deskport_checkout_name-$deskport_checkout_hash"
if [ "${DESKPORT_NIX_DEPS:-0}" = 1 ]; then
    deskport_macos_provider=nix
else
    deskport_macos_provider=homebrew
fi
deskport_macos_root="$DESKPORT_CHECKOUT_BUILD_ROOT/macos-arm64.noindex/$deskport_macos_provider"
DESKPORT_MACOS_BUILD_DIR=${DESKPORT_MACOS_BUILD_DIR:-$deskport_macos_root/build}
DESKPORT_MACOS_DIST_DIR=${DESKPORT_MACOS_DIST_DIR:-$deskport_macos_root/dist}
DESKPORT_HOST_SOURCE_DIR=${DESKPORT_HOST_SOURCE_DIR:-$DESKPORT_MACOS_BUILD_DIR/sunshine-vendored-source}
DESKPORT_LINUX_WORK=${DESKPORT_LINUX_WORK:-$DESKPORT_CHECKOUT_BUILD_ROOT/linux-x86_64.noindex/portable}
DESKPORT_RELEASE_ROOT="$DESKPORT_CHECKOUT_BUILD_ROOT/releases.noindex"
DESKPORT_LOG_DIR="$DESKPORT_CHECKOUT_BUILD_ROOT/logs"
export DESKPORT_BUILD_ROOT DESKPORT_CHECKOUT_BUILD_ROOT
export DESKPORT_MACOS_BUILD_DIR DESKPORT_MACOS_DIST_DIR DESKPORT_HOST_SOURCE_DIR
export DESKPORT_LINUX_WORK DESKPORT_RELEASE_ROOT DESKPORT_LOG_DIR

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    case "${1:-checkout}" in
        root) printf '%s\n' "$DESKPORT_BUILD_ROOT" ;;
        checkout) printf '%s\n' "$DESKPORT_CHECKOUT_BUILD_ROOT" ;;
        macos-build) printf '%s\n' "$DESKPORT_MACOS_BUILD_DIR" ;;
        macos-dist) printf '%s\n' "$DESKPORT_MACOS_DIST_DIR" ;;
        macos-host-source) printf '%s\n' "$DESKPORT_HOST_SOURCE_DIR" ;;
        linux-work) printf '%s\n' "$DESKPORT_LINUX_WORK" ;;
        releases) printf '%s\n' "$DESKPORT_RELEASE_ROOT" ;;
        logs) printf '%s\n' "$DESKPORT_LOG_DIR" ;;
        *) echo 'Usage: build-paths.sh [root|checkout|macos-build|macos-dist|macos-host-source|linux-work|releases|logs]' >&2; exit 2 ;;
    esac
fi
