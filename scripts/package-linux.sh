#!/bin/bash
# Build portable Linux packages without modifying the host distribution.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export DESKPORT_TARGET=linux-portable
source "$repo/scripts/build-paths.sh"
format=${DESKPORT_LINUX_FORMAT:-all}
case "$format" in all|appimage) ;; *) echo "DESKPORT_LINUX_FORMAT must be all or appimage" >&2; exit 2 ;; esac
work=$DESKPORT_LINUX_WORK
mkdir -p "$work"
work=$(cd "$work" && pwd)
mounts=(-v "$repo:/src:ro" -v "$work:/work")
if [ -n "${DESKPORT_RUN_ID:-}" ]; then
    mkdir -p "$DESKPORT_DEPENDENCY_CACHE_DIR" "$DESKPORT_DIST_DIR"
    cache=$(cd "$DESKPORT_DEPENDENCY_CACHE_DIR" && pwd -P)
    dist=$(cd "$DESKPORT_DIST_DIR" && pwd -P)
    # Preserve the container's established /work/cache and /work/output API.
    # Refuse an older cache or an unexpected link instead of moving its data.
    for entry in cache output; do
        if [ "$entry" = cache ]; then destination=$cache; else destination=$dist; fi
        if [ -L "$work/$entry" ]; then
            [ "$(readlink "$work/$entry")" = "$destination" ] || {
                echo "Unexpected $entry link in managed run: $work/$entry" >&2; exit 2;
            }
        elif [ -e "$work/$entry" ]; then
            echo "Refusing to replace existing managed-run $entry: $work/$entry" >&2; exit 2
        else
            ln -s "$destination" "$work/$entry"
        fi
    done
    # Dependency helpers update extracted sources and ABI-specific prefixes.
    # Serialize this cache without guessing whether an existing lock is stale.
    cache_lock="$cache/.deskport-build.lock"
    mkdir "$cache_lock" 2>/dev/null || {
        echo "Dependency cache is locked; inspect $cache_lock before retrying." >&2; exit 2;
    }
    printf '%s\n' "$$" > "$cache_lock/pid"
    trap 'rm -f "$cache_lock/pid"; rmdir "$cache_lock"' EXIT
    mounts+=(-v "$cache:$cache" -v "$dist:$dist")
fi
podman run --rm \
    "${mounts[@]}" \
    -e "DESKPORT_JOBS=${DESKPORT_JOBS:-4}" \
    -e "DESKPORT_LINUX_FORMAT=$format" \
    docker.io/library/ubuntu@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254 \
    bash /src/scripts/package-linux-container.sh
