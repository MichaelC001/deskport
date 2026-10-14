#!/usr/bin/env bash
# Build only Qt Wayland Client against the existing Ubuntu Qt 6.4.2 SDK.
# Run in the portable Ubuntu container; do not upgrade Qt or media dependencies.
set -euo pipefail
repo=${DESKPORT_SOURCE:-/src}
work=${DESKPORT_WORK:-/work}
version=6.4.2
url=https://download.qt.io/archive/qt/6.4/6.4.2/submodules/qtwayland-everywhere-src-6.4.2.tar.xz
expected=24cf1a0af751ab1637b4815d5c5f3704483d5fa7bedbd3519e6fc020d8be135f
archive=$work/cache/qtwayland-everywhere-src-$version.tar.xz
stage=$work/cache/qtwayland-$version-runtime
patch=$repo/host/linux/patches/qt-wayland-screen-lifetime.patch
builder=$repo/scripts/build-appimage-qt-wayland.sh

qt_version=$(qmake6 -query QT_VERSION)
if [ "$qt_version" != "$version" ]; then
    echo "Qt Wayland backport requires the matching Qt $version SDK; found $qt_version" >&2
    exit 2
fi
qt_libdir=$(qmake6 -query QT_INSTALL_LIBS)
case "$qt_libdir" in
    /usr/lib|/usr/lib/*) qt_build_libdir=${qt_libdir#/usr/} ;;
    *) echo "Expected the Ubuntu Qt SDK library directory under /usr/lib: $qt_libdir" >&2; exit 2 ;;
esac
mkdir -p "$work/cache" "$work/build" "$stage/usr/lib"
if [ ! -f "$archive" ]; then
    curl -fL --retry 3 -o "$archive" "$url"
fi
printf '%s  %s\n' "$expected" "$archive" | sha256sum --check

# Pin the cache to the actual installed SDK/compiler packages as well as the
# source recipe. A same-version Qt SDK with new distro ABI patches must rebuild.
sdk_manifest=$work/cache/qtwayland-$version-build-environment.txt
{
    printf 'Qt SDK version: %s\n' "$qt_version"
    printf 'Qt SDK library directory: %s\n' "$qt_libdir"
    c++ --version
    cmake --version
    dpkg-query -W -f='${binary:Package}\t${Version}\n' | LC_ALL=C sort
} > "$sdk_manifest"
recipe=$(sha256sum "$builder" "$patch" "$sdk_manifest" | awk '{print $1}' | sha256sum | cut -d' ' -f1)
build_dir=$work/build/qtwayland-$version-${recipe:0:16}
if [ ! -f "$stage/build-key" ] || [ "$(cat "$stage/build-key")" != "$recipe" ] || \
        [ ! -f "$stage/usr/lib/libQt6WaylandClient.so.$version" ]; then
    mkdir -p "$build_dir/source"
    if [ ! -f "$build_dir/source/.deskport-patched" ]; then
        tar -xf "$archive" --strip-components=1 -C "$build_dir/source"
        patch --batch --fuzz=0 -d "$build_dir/source" -p1 < "$patch"
        touch "$build_dir/source/.deskport-patched"
    fi
    cmake -S "$build_dir/source" -B "$build_dir/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
        -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF \
        -DQT_BUILD_DOCS=OFF -DFEATURE_wayland_client=ON -DFEATURE_wayland_server=OFF
    # The scanner and generated protocol bindings are built as dependencies.
    # No Qt base, QML, compositor, platform plugins, or media libraries are rebuilt.
    cmake --build "$build_dir/build" --target WaylandClient --parallel "${DESKPORT_JOBS:-2}"
    # Standalone Qt modules inherit the SDK's multiarch output directory.
    library=$build_dir/build/$qt_build_libdir/libQt6WaylandClient.so.$version
    test -f "$library"
    install -m755 "$library" "$stage/usr/lib/libQt6WaylandClient.so.$version"
    ln -sfn "libQt6WaylandClient.so.$version" "$stage/usr/lib/libQt6WaylandClient.so.6"
    ln -sfn libQt6WaylandClient.so.6 "$stage/usr/lib/libQt6WaylandClient.so"
    readelf -d "$library" | grep -F 'Library soname: [libQt6WaylandClient.so.6]'
    printf '%s\n' "$recipe" > "$stage/build-key"
fi

# Complete corresponding source, license notices, and an executable rebuild
# recipe travel with the replacement library. Qt stays dynamically replaceable.
install -m644 "$archive" "$stage/qtwayland-everywhere-src-$version.tar.xz"
install -m644 "$patch" "$stage/qt-wayland-screen-lifetime.patch"
install -m755 "$builder" "$stage/build-appimage-qt-wayland.sh"
install -m644 "$sdk_manifest" "$stage/BUILD-ENVIRONMENT.txt"
tar -xf "$archive" --strip-components=1 -C "$stage" \
    "qtwayland-everywhere-src-$version/LICENSES" "qtwayland-everywhere-src-$version/README"
install -m644 "$stage/LICENSES/LGPL-3.0-only.txt" "$stage/COPYING"
cat > "$stage/RELINK.md" <<'MD'
# Qt Wayland Client 6.4.2 screen lifetime backport

This dynamically linked library is built from the enclosed unmodified Qt 6.4.2
source archive plus `qt-wayland-screen-lifetime.patch`. All original copyright
notices and the complete upstream `LICENSES` directory are retained. The patch
backports upstream commit e038d0b750cf238fef6eeb07a90b7fc1b52e4640, which clears
the placeholder pointer before screen-removal callbacks can enter a nested event
loop. It does not change the Qt version, exported API, or media pipeline.

Use an Ubuntu 24.04 environment with the matching Qt 6.4.2 SDK. Dependencies:
build-essential, cmake, ninja-build, pkg-config, curl, ca-certificates, python3,
patch, qt6-base-dev, qt6-base-private-dev, qt6-declarative-dev, libwayland-dev,
libxkbcommon-dev, libegl1-mesa-dev, libgl1-mesa-dev, and libvulkan-dev.

To rebuild, create a source directory containing `scripts/` and
`host/linux/patches/`. Copy the enclosed builder and patch to those directories.
Choose a writable work directory and place the enclosed source archive in its
`cache/` directory. Run:

```sh
DESKPORT_SOURCE=/path/to/source DESKPORT_WORK=/path/to/work \
  bash /path/to/source/scripts/build-appimage-qt-wayland.sh
```

The resulting replacement is under `cache/qtwayland-6.4.2-runtime/usr/lib/`.
It may replace `usr/lib/libQt6WaylandClient.so.6` in an extracted DeskPort
AppImage. The existing Qt 6.4.2 platform and shell plugins remain ABI compatible.
Only the viewer runtime uses this replacement; the private host is unchanged.
MD

python3 - "$archive" "$stage" "$url" "$expected" <<'PY'
import hashlib, json, pathlib, sys
archive, stage = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
paths = [stage / 'qtwayland-everywhere-src-6.4.2.tar.xz',
         stage / 'qt-wayland-screen-lifetime.patch',
         stage / 'build-appimage-qt-wayland.sh', stage / 'RELINK.md',
         stage / 'COPYING', stage / 'README', stage / 'BUILD-ENVIRONMENT.txt']
paths.extend(sorted((stage / 'LICENSES').rglob('*')))
materials = [dict(path=p.relative_to(stage).as_posix(), bytes=p.stat().st_size,
                  checksum='SHA256:' + hashlib.sha256(p.read_bytes()).hexdigest())
             for p in paths if p.is_file()]
metadata = dict(binary='qtwayland-upstream-backport', version='6.4.2',
    source='qtwayland', source_version='6.4.2', modified=True,
    upstream_fix='e038d0b750cf238fef6eeb07a90b7fc1b52e4640',
    archives=[dict(url=sys.argv[3], filename=archive.name, bytes=archive.stat().st_size,
                   checksum='SHA256:' + sys.argv[4])], materials=materials)
(stage / 'source.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
