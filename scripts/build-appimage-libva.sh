#!/usr/bin/env bash
# Build a pinned VA-API interface against the portable runtime's glibc.
set -euo pipefail
repo=${DESKPORT_SOURCE:-/src}
work=${DESKPORT_WORK:-/work}
archive=$work/cache/libva-source.tar.bz2
stage=$work/cache/libva-2.23.0-runtime
expected=9ac190a87017bfd49743248f5df7cf3b18a99a9962175caf6bbe3f1ea41b6dbb
printf '%s  %s\n' "$expected" "$archive" | sha256sum --check
if [ ! -f "$stage/usr/lib/libva-x11.so.2.2300.0" ]; then
    source_dir=$(mktemp -d "$work/cache/libva-build.XXXXXX")
    tar -xf "$archive" --strip-components=1 -C "$source_dir"
    (
        cd "$source_dir"
        ./configure --prefix=/usr --libdir=/usr/lib --disable-docs --disable-glx \
            --enable-drm --enable-x11 --enable-wayland
        make -j"${DESKPORT_JOBS:-2}"
        make DESTDIR="$stage" install
        install -m644 COPYING "$stage/COPYING"
    )
fi
python3 - "$repo/scripts/linux-tools.json" "$archive" "$stage/source.json" <<'PY'
import json, pathlib, sys
item = json.loads(pathlib.Path(sys.argv[1]).read_text())['libva-source.tar.bz2']
archive = pathlib.Path(sys.argv[2])
metadata = dict(binary='libva-upstream', version='2.23.0', source='libva',
    source_version='2.23.0', archives=[dict(url=item['url'], filename='libva-2.23.0.tar.bz2',
    bytes=archive.stat().st_size, checksum='SHA256:' + item['sha256'])])
pathlib.Path(sys.argv[3]).write_text(json.dumps(metadata, indent=2) + '\n')
PY
