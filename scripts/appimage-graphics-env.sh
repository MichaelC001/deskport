#!/bin/sh
# Source from AppImage launchers. Keep modern host GPU drivers and their ABI
# dependencies together, without replacing Qt, SDL, FFmpeg or PipeWire.
# Old hosts retain the portable glibc/software fallback. No host files change.
deskport_graphics_env() {
    [ "${DESKPORT_APPIMAGE_RUNTIME:-auto}" != private ] || return 0
    dp_prefix=$1
    dp_tools=/run/current-system/sw/bin:/usr/bin:/bin:/usr/sbin:/sbin
    dp_ldd=$(PATH=$dp_tools command -v ldd) || return 0
    dp_getconf=$(PATH=$dp_tools command -v getconf) || return 0
    dp_glibc=$(env -u LD_LIBRARY_PATH -u LD_PRELOAD "$dp_getconf" GNU_LIBC_VERSION 2>/dev/null) || return 0
    case "$dp_glibc" in
        'glibc 2.'*) dp_minor=${dp_glibc#glibc 2.}; dp_minor=${dp_minor%%.*} ;;
        *) return 0 ;;
    esac
    # The bundled interpreter is glibc 2.39. A host cohort must never lower it.
    case "$dp_minor" in ''|*[!0-9]*) return 0 ;; esac
    [ "$dp_minor" -ge 39 ] || return 0
    dp_base=${XDG_CACHE_HOME:-$HOME/.cache}/DeskPort/graphics-runtime
    mkdir -p "$dp_base" || return 0
    dp_temp=$(mktemp -d "$dp_base/.resolve.XXXXXX") || return 0
    : > "$dp_temp/inputs"
    dp_driver_dirs=${LIBVA_DRIVERS_PATH:-/run/opengl-driver/lib/dri:/usr/lib/x86_64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri}
    dp_old_ifs=$IFS
    IFS=:
    for dp_dir in $dp_driver_dirs; do
        IFS=$dp_old_ifs
        # Only known graphics libraries, never applications from PATH. ldd's
        # output is parsed as paths, not evaluated as shell source.
        for dp_library in "$dp_dir"/*_drv_video.so "$dp_dir"/../libEGL_mesa.so.0 "$dp_dir"/../libGLX_mesa.so.0; do
            [ -f "$dp_library" ] || continue
            dp_listing=$(env -u LD_LIBRARY_PATH -u LD_PRELOAD "$dp_ldd" "$dp_library" 2>/dev/null) || continue
            case "$dp_listing" in *'not found'*) continue ;; esac
            printf '%s\n' "$dp_library" >> "$dp_temp/inputs"
            printf '%s\n' "$dp_listing" | awk '
                $2 == "=>" && $3 ~ /^\// { print $3 }
                $1 ~ /^\// && $1 ~ /ld-linux/ { print $1 }
            ' >> "$dp_temp/inputs"
        done
        IFS=:
    done
    IFS=$dp_old_ifs
    sort -u "$dp_temp/inputs" -o "$dp_temp/inputs"
    dp_loader=
    dp_libc=
    dp_valid=1
    while IFS= read -r dp_library; do
        [ -f "$dp_library" ] || { dp_valid=0; break; }
        dp_name=${dp_library##*/}
        case "$dp_name" in
            ld-linux-x86-64.so.2) dp_loader=$dp_library ;;
            libc.so.6) dp_libc=$dp_library ;;
        esac
        # Different GPU providers may resolve a SONAME differently. Do not
        # silently construct a mixed driver cohort in that case.
        if [ -e "$dp_temp/$dp_name" ]; then
            [ "$(readlink -f "$dp_temp/$dp_name")" = "$(readlink -f "$dp_library")" ] || { dp_valid=0; break; }
        else
            ln -s "$dp_library" "$dp_temp/$dp_name" || { dp_valid=0; break; }
        fi
    done < "$dp_temp/inputs"
    if [ "$dp_valid" != 1 ] || [ -z "$dp_loader" ] || [ -z "$dp_libc" ]; then
        rm -r -- "$dp_temp"
        return 0
    fi
    # Include every libc companion from the SAME installation as its loader;
    # mixing a newer libc with the private loader is not ABI-safe.
    dp_libc_dir=${dp_libc%/*}
    for dp_name in libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libresolv.so.2 libnss_dns.so.2 libnss_files.so.2; do
        [ -f "$dp_libc_dir/$dp_name" ] || continue
        ln -sf "$dp_libc_dir/$dp_name" "$dp_temp/$dp_name"
        printf '%s\n' "$dp_libc_dir/$dp_name" >> "$dp_temp/inputs"
    done
    # Immutable, user-owned cache entries preserve already-running processes.
    # Verify the manifest too, so a checksum collision cannot select a cohort.
    dp_key=$(cksum < "$dp_temp/inputs" | awk '{print $1 "-" $2}')
    dp_cache=$dp_base/$dp_key
    dp_inputs=$(cat "$dp_temp/inputs")
    if [ -d "$dp_cache" ]; then
        if [ ! -f "$dp_cache/inputs" ] || [ "$(cat "$dp_cache/inputs")" != "$dp_inputs" ]; then
            rm -r -- "$dp_temp"
            return 0
        fi
        rm -r -- "$dp_temp"
    elif ! mv -T "$dp_temp" "$dp_cache" 2>/dev/null; then
        # Another process may have atomically installed the same cohort.
        if [ ! -f "$dp_cache/inputs" ] || [ "$(cat "$dp_cache/inputs")" != "$dp_inputs" ]; then
            rm -r -- "$dp_temp"
            return 0
        fi
        rm -r -- "$dp_temp"
    fi
    if ! "$dp_loader" --library-path "$dp_cache:$dp_prefix/shared/lib:$dp_prefix/lib" \
        --list "$dp_prefix/shared/bin/$2" >/dev/null 2>&1; then
        printf 'DeskPort AppImage graphics runtime: incompatible host cohort; retaining private runtime\n' >&2
        return 0
    fi
    export SHARUN_LDNAME="$dp_loader"
    export SHARUN_EXTRA_LIBRARY_PATH="$dp_cache"
    printf 'DeskPort AppImage graphics runtime: host glibc/driver cohort (%s)\n' "$dp_glibc" >&2
}
