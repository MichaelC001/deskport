#!/usr/bin/env python3
"""Give each AppImage process tree its own relocatable glibc runtime.

Run inside the Linux packaging container after linuxdeploy. Native packages keep
using the original AppDir. sharun is pinned and verified by linux-tools.json.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


PIPEWIRE_MODULES = ('rt', 'protocol-native', 'client-node', 'client-device',
                    'adapter', 'metadata', 'session-manager')
SPA_PLUGINS = ('support/libspa-support.so',
               'audioconvert/libspa-audioconvert.so',
               'videoconvert/libspa-videoconvert.so')
LIBVA_LIBRARIES = ('libva.so.2', 'libva-drm.so.2', 'libva-x11.so.2', 'libva-wayland.so.2')


def file_checksum(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return 'SHA256:' + digest.hexdigest()


def install_libva_runtime(prefix, stage):
    """Use the same pinned VA interface in native, Flatpak and AppImage trees."""
    metadata = json.loads((stage / 'source.json').read_text())
    if metadata.get('source_version') != '2.23.0' or not (stage / 'COPYING').is_file():
        raise RuntimeError('Missing pinned libva 2.23.0 source/license metadata')
    for name in LIBVA_LIBRARIES:
        if not elf(stage / 'usr/lib' / name):
            raise RuntimeError(f'Missing pinned VA-API runtime: {name}')
    # Validate all inputs before touching any existing library. Keep both the
    # interface and its platform adapters at one version.
    for old in (prefix / 'lib').glob('libva*.so*'):
        old.unlink()
    files = []
    for name in LIBVA_LIBRARIES:
        target = prefix / 'lib' / name
        shutil.copy2(stage / 'usr/lib' / name, target)
        files.append(dict(path=str(target.relative_to(prefix.parent)), bytes=target.stat().st_size,
                          checksum=file_checksum(target)))
    documents = prefix / 'share/doc/deskport/libva-runtime'
    documents.mkdir(parents=True, exist_ok=True)
    shutil.copy2(stage / 'COPYING', documents / 'COPYING')
    (documents / 'source.json').write_text(json.dumps(dict(metadata, runtime_files=files), indent=2) + '\n')
    return metadata


def copy_qt_wayland_runtime(prefix, stage):
    """Install the viewer-only backport with verified corresponding materials."""
    name = 'libQt6WaylandClient.so.6'
    library = stage / 'usr/lib' / name
    metadata = json.loads((stage / 'source.json').read_text())
    if metadata.get('modified') is not True:
        raise RuntimeError('Qt Wayland backport must be recorded as modified source')
    if not library.is_file() or not (stage / 'COPYING').is_file():
        raise RuntimeError('Missing Qt Wayland backport library or license')
    materials = metadata.get('materials', [])
    if not materials:
        raise RuntimeError('Missing Qt Wayland source, patch and relinking materials')
    verified = []
    for item in materials:
        relative = Path(item['path'])
        source = stage / relative
        if relative.is_absolute() or '..' in relative.parts or not source.is_file():
            raise RuntimeError(f'Invalid Qt Wayland material path: {relative}')
        if item['bytes'] != source.stat().st_size or item['checksum'] != file_checksum(source):
            raise RuntimeError(f'Qt Wayland material checksum mismatch: {relative}')
        verified.append((item, source, relative))
    # Validate the entire input before replacing the linuxdeploy binary. A
    # missing source/patch must never leave a partially updated candidate.
    documents = prefix / 'share/doc/deskport/qtwayland-backport'
    bundled = []
    for item, source, relative in verified:
        target = documents / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target, follow_symlinks=True)
        bundled.append(dict(item, path=str(target.relative_to(prefix.parent))))
    (prefix / 'lib').mkdir(parents=True, exist_ok=True)
    for old in (prefix / 'lib').glob('libQt6WaylandClient.so*'):
        old.unlink()
    target = prefix / 'lib' / name
    shutil.copy2(library, target, follow_symlinks=True)
    metadata = dict(metadata, materials=bundled, runtime_files=[dict(
        path=str(target.relative_to(prefix.parent)), bytes=target.stat().st_size,
        checksum=file_checksum(target))])
    (documents / 'source.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata, library


def write_sharun_environment(prefix):
    """Restore the selected host cohort when sharun launches a child process."""
    # sharun removes the one-shot EXTRA_LIBRARY_PATH but retains LDNAME. Without
    # this, a display helper inherits the host loader and loads the private libc.
    # dotenv expands the persistent environment value without exporting bundled
    # libraries through LD_LIBRARY_PATH into system tools.
    (prefix / '.env').write_text(
        'SHARUN_EXTRA_LIBRARY_PATH="${DESKPORT_APPIMAGE_GRAPHICS_RUNTIME}"\n')


def copy_pipewire_runtime(prefix, system, share):
    """Copy dynamically loaded client inputs; ldd cannot discover these."""
    inputs = [(system / 'spa-0.2' / name,
               prefix / 'shared/lib/spa-0.2' / name) for name in SPA_PLUGINS]
    inputs += [(system / 'pipewire-0.3' / f'libpipewire-module-{name}.so',
                prefix / 'shared/lib/pipewire-0.3' / f'libpipewire-module-{name}.so')
               for name in PIPEWIRE_MODULES]
    inputs += [(share / name, prefix / 'share/pipewire' / name)
               for name in ('client.conf', 'client-rt.conf')]
    # Fail before copying an incomplete client runtime into the candidate.
    for source, _ in inputs:
        if not source.is_file():
            raise RuntimeError(f'Missing PipeWire client runtime input: {source}')
    for source, target in inputs:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target, follow_symlinks=True)
    return inputs


def elf(path):
    try:
        with path.open('rb') as stream:
            return stream.read(4) == b'\x7fELF'
    except (OSError, IsADirectoryError):
        return False


def bundle(root, sharun, executables, excluded=None, libva=None, qt_wayland=None):
    prefix = root / 'usr'
    va_metadata = None
    if libva is not None:
        va_metadata = install_libva_runtime(prefix, libva)
    qt_metadata = None
    if qt_wayland is not None:
        qt_metadata, qt_library = copy_qt_wayland_runtime(prefix, qt_wayland)
    runtime = prefix / 'shared/lib'
    runtime.mkdir(parents=True)
    binaries = prefix / 'shared/bin'
    binaries.mkdir()
    shutil.copy2(sharun, prefix / 'sharun')
    env = dict(os.environ, LD_LIBRARY_PATH=str(prefix / 'lib'))
    queue = [p for p in prefix.rglob('*') if p.is_file()
             and (excluded is None or not p.is_relative_to(excluded)) and elf(p)]
    # glibc's loader, NSS/gconv modules and the GL/Vulkan dispatchers may be
    # opened dynamically, so they cannot be discovered from DT_NEEDED alone.
    system = Path('/usr/lib/x86_64-linux-gnu')
    extras = ['ld-linux-x86-64.so.2', 'libc.so.6', 'libm.so.6', 'libdl.so.2',
              'libpthread.so.0', 'librt.so.1', 'libresolv.so.2', 'libnss_dns.so.2',
              'libnss_files.so.2', 'libstdc++.so.6', 'libgcc_s.so.1',
              'libGL.so.1', 'libGLX.so.0', 'libGLdispatch.so.0', 'libEGL.so.1',
              'libGLX_mesa.so.0', 'libEGL_mesa.so.0', 'libgbm.so.1',
              'libvulkan.so.1', 'libdrm.so.2', 'libudev.so.1']
    manifest = {}
    notices = root / 'usr/share/doc/deskport/runtime-licenses'
    notices.mkdir(parents=True, exist_ok=True)
    packages = {}
    if va_metadata is not None:
        packages['libva-upstream'] = va_metadata
        shutil.copy2(libva / 'COPYING', notices / 'libva-upstream.copyright')
        for name in LIBVA_LIBRARIES:
            manifest[name] = {'path': str(libva / 'usr/lib' / name), **va_metadata}
    if qt_metadata is not None:
        packages['qtwayland-backport'] = qt_metadata
        shutil.copy2(qt_wayland / 'COPYING', notices / 'qtwayland-backport.copyright')
        manifest['libQt6WaylandClient.so.6'] = {'path': str(qt_library), **qt_metadata}

    def provenance(source):
        candidates = dict.fromkeys(str(path) for path in (source, source.resolve()))
        for path in list(candidates):
            if path.startswith('/usr/lib/'):
                candidates[path.removeprefix('/usr')] = None
            elif path.startswith('/lib/'):
                candidates['/usr' + path] = None
        for candidate in candidates:
            found = subprocess.run(['dpkg-query', '-S', candidate], text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            if found.returncode == 0:
                break
        if found.returncode:
            raise RuntimeError(f'Cannot identify runtime source package: {source}')
        package = found.stdout.splitlines()[0].rsplit(': ', 1)[0]
        if package not in packages:
            fields = subprocess.check_output(['dpkg-query', '-W',
                '-f=${binary:Package}\t${Version}\t${source:Package}\t${source:Version}',
                package], text=True).split('\t')
            copyright_file = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
            if not copyright_file.exists():
                raise RuntimeError(f'Missing copyright notice: {package}')
            shutil.copy2(copyright_file, notices / (package.replace(':', '_') + '.copyright'))
            packages[package] = dict(zip(['binary', 'version', 'source', 'source_version'], fields))
        return packages[package]


    def copy_library(source):
        source = Path(source)
        target = runtime / source.name
        # Prefer already deployed libraries; they match this process's ABI.
        if (prefix / 'lib' / source.name).exists():
            return
        if target.exists():
            return
        shutil.copy2(source, target, follow_symlinks=True)
        manifest[source.name] = {'path': str(source.resolve()), **provenance(source)}
        queue.append(target)

    for name in extras:
        source = system / name
        if not source.exists():
            raise RuntimeError(f'Missing runtime input: {source}')
        copy_library(source)
    # PipeWire loads SPA factories and client modules through dlopen using
    # compiled-in distro paths. Bundle matching inputs in both private trees,
    # never the server daemon or a session manager. Include their dependencies
    # and source/copyright records in the same closure as ordinary libraries.
    for source, target in copy_pipewire_runtime(prefix, system, Path('/usr/share/pipewire')):
        metadata = {'path': str(source.resolve()), **provenance(source)}
        if elf(target):
            manifest[str(target.relative_to(runtime))] = metadata
            queue.append(target)
    # GLVND discovers EGL implementations through JSON, not DT_NEEDED. The
    # bundled Mesa fallback otherwise remains invisible on non-Ubuntu hosts.
    egl_source = Path('/usr/share/glvnd/egl_vendor.d/50_mesa.json')
    if json.loads(egl_source.read_text())['ICD']['library_path'] != 'libEGL_mesa.so.0':
        raise RuntimeError(f'Unexpected Mesa EGL vendor registration: {egl_source}')
    egl_target = prefix / 'share/glvnd/egl_vendor.d/50_mesa.json'
    egl_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(egl_source, egl_target)
    provenance(egl_source)
    # Include software rendering for the old-system X11 startup check. Hardware
    # drivers remain supplied by the host kernel/vendor; do not bundle NVIDIA.
    dri = system / 'dri/swrast_dri.so'
    if dri.exists():
        (runtime / 'mesa-software').mkdir()
        shutil.copy2(dri, runtime / 'mesa-software/swrast_dri.so')
        provenance(dri)
        queue.append(runtime / 'mesa-software/swrast_dri.so')
    shutil.copytree(system / 'gconv', runtime / 'gconv')
    queue.extend(p for p in (runtime / 'gconv').glob('*.so') if elf(p))
    seen = set()
    while queue:
        path = queue.pop()
        if path in seen:
            continue
        seen.add(path)
        result = subprocess.run(['ldd', str(path)], env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if 'not found' in result.stdout:
            raise RuntimeError(f'Unresolved dependency for {path}:\n{result.stdout}')
        if result.returncode and not any(x in result.stdout for x in
                                        ['statically linked', 'not a dynamic executable']):
            raise RuntimeError(result.stdout)
        for dependency in re.findall(r'(?:=>\s+|^\s*)(/\S+)\s+\(', result.stdout, re.M):
            source = Path(dependency)
            if source.is_relative_to(root):
                continue
            copy_library(source)
    (notices / 'packages.json').write_text(json.dumps(packages, indent=2) + '\n')
    # The runtime supplies glibc; existing deployed libraries retain their ABI.
    (runtime / 'lib.path').write_text('+\n+/../../lib\n')
    (runtime / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    write_sharun_environment(prefix)
    # qt.conf remains adjacent to the public binary, as with linuxdeploy.
    for relative in executables:
        public = prefix / relative
        name = public.name
        shutil.move(public, binaries / name)
        # An ELF hardlink retains /proc/self/exe identity (unlike exec ld.so).
        public.parent.mkdir(exist_ok=True)
        os.link(prefix / 'sharun', public)
    # Display helper is addressed under libexec by the viewer; a symlink to its
    # public bin entry makes sharun discover the same runtime on direct launch.
    display = prefix / 'libexec/deskport-display'
    if display.exists():
        shutil.move(display, binaries / 'deskport-display')
        os.link(prefix / 'sharun', prefix / 'bin/deskport-display')
        display.symlink_to('../bin/deskport-display')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appdir', type=Path)
    parser.add_argument('sharun', type=Path)
    parser.add_argument('--libva-prefix', type=Path, required=True,
                        help='Pinned libva build including source.json and COPYING')
    parser.add_argument('--qt-wayland-prefix', '--qtwayland-prefix', type=Path,
                        help='Viewer Qt 6.4.2 Wayland backport with source/relink materials')
    args = parser.parse_args()
    root = args.appdir.resolve()
    host = root / 'usr/libexec/sunshine'
    bundle(host, args.sharun, ['bin/sunshine'], libva=args.libva_prefix)
    bundle(root, args.sharun, ['bin/deskport'], excluded=host, libva=args.libva_prefix,
           qt_wayland=args.qt_wayland_prefix)
    graphics_env = Path(__file__).with_name('appimage-graphics-env.sh')
    for tree in (root, host):
        (tree / 'usr/libexec').mkdir(exist_ok=True)
        shutil.copy2(graphics_env, tree / 'usr/libexec/deskport-graphics-env.sh')
    # Avoid exporting bundled glibc through LD_LIBRARY_PATH into host tools or
    # desktop applications launched later. sharun supplies --library-path.
    (root / 'AppRun').unlink()
    (root / 'AppRun').write_text('''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
unset SHARUN_DIR SHARUN_LDNAME SHARUN_EXTRA_LIBRARY_PATH DESKPORT_APPIMAGE_GRAPHICS_RUNTIME LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH
export APPDIR="$root"
export QML2_IMPORT_PATH="$root/usr/qml"
export SPA_PLUGIN_DIR="$root/usr/shared/lib/spa-0.2"
export PIPEWIRE_MODULE_DIR="$root/usr/shared/lib/pipewire-0.3"
export PIPEWIRE_CONFIG_DIR="$root/usr/share/pipewire"
export __EGL_VENDOR_LIBRARY_DIRS="${__EGL_VENDOR_LIBRARY_DIRS:+$__EGL_VENDOR_LIBRARY_DIRS:}/run/opengl-driver/share/glvnd/egl_vendor.d:/usr/share/glvnd/egl_vendor.d:$root/usr/share/glvnd/egl_vendor.d"
# Keep host hardware drivers ahead of our software fallback. Do not let sharun
# replace LIBGL_DRIVERS_PATH with a directory containing only swrast.
export LIBGL_DRIVERS_PATH="${LIBGL_DRIVERS_PATH:+$LIBGL_DRIVERS_PATH:}/run/opengl-driver/lib/dri:/usr/lib/x86_64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri:$root/usr/shared/lib/mesa-software"
export LIBVA_DRIVERS_PATH="${LIBVA_DRIVERS_PATH:-/run/opengl-driver/lib/dri:/usr/lib/x86_64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri}"
. "$root/usr/libexec/deskport-graphics-env.sh"
deskport_graphics_env "$root/usr" deskport
exec "$root/usr/bin/deskport" "$@"
''')
    (root / 'AppRun').chmod(0o755)
    launcher = root / 'usr/libexec/deskport-host'
    content = launcher.read_text().replace('unset APPIMAGE APPDIR LD_LIBRARY_PATH',
                                          'unset SHARUN_DIR SHARUN_LDNAME SHARUN_EXTRA_LIBRARY_PATH DESKPORT_APPIMAGE_GRAPHICS_RUNTIME APPIMAGE APPDIR LD_LIBRARY_PATH')
    content = content.replace('export LD_LIBRARY_PATH="$root/usr/lib"\n', '')
    content = content.replace('export APPDIR="$root"', 'export APPDIR="$root"\nexport __EGL_VENDOR_LIBRARY_DIRS="${__EGL_VENDOR_LIBRARY_DIRS:+$__EGL_VENDOR_LIBRARY_DIRS:}/run/opengl-driver/share/glvnd/egl_vendor.d:/usr/share/glvnd/egl_vendor.d:$root/usr/share/glvnd/egl_vendor.d"')
    content = content.replace('export APPDIR="$root"', 'export APPDIR="$root"\nexport SPA_PLUGIN_DIR="$root/usr/shared/lib/spa-0.2"\nexport PIPEWIRE_MODULE_DIR="$root/usr/shared/lib/pipewire-0.3"\nexport PIPEWIRE_CONFIG_DIR="$root/usr/share/pipewire"')
    content = content.replace('export APPDIR="$root"', 'export APPDIR="$root"\nexport LIBGL_DRIVERS_PATH="/run/opengl-driver/lib/dri:/usr/lib/x86_64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri:$root/usr/shared/lib/mesa-software"')
    content = content.replace('exec "$root/usr/bin/sunshine"',
        'export LIBVA_DRIVERS_PATH="${LIBVA_DRIVERS_PATH:-/run/opengl-driver/lib/dri:/usr/lib/x86_64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri}"\n'
        '. "$root/usr/libexec/deskport-graphics-env.sh"\n'
        'deskport_graphics_env "$root/usr" sunshine\n'
        'exec "$root/usr/bin/sunshine"')
    launcher.write_text(content)
    print('Bundled independent viewer and host runtimes with sharun 0.8.1')


if __name__ == '__main__':
    main()
