#!/usr/bin/env python3
"""Wrap the tested portable payload in DEB, RPM and pacman packages."""
import json
import pathlib
import subprocess
import sys

appdir, work = (pathlib.Path(p).resolve() for p in sys.argv[1:3])
version = sys.argv[3]
cache = work / 'cache'
subprocess.run(['tar', '-xzf', str(cache / 'nfpm.tar.gz'), '-C', str(cache)], check=True)
launcher = work / 'deskport-launcher'
launcher.write_text('#!/bin/sh\nexec /opt/deskport/AppRun "$@"\n')
launcher.chmod(0o755)
permission = work / 'io.github.keithxc.DeskPort.display.desktop'
permission.write_text('[Desktop Entry]\nType=Application\n'
    'Name=DeskPort virtual display permission\n'
    'Exec=/opt/deskport/usr/libexec/deskport-display\nNoDisplay=true\n'
    'X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1\n')
host_permission = work / 'io.github.keithxc.DeskPort.host.desktop'
host_permission.write_text(permission.read_text().replace(
    'DeskPort virtual display permission', 'DeskPort host capture permission').replace(
    '/opt/deskport/usr/libexec/deskport-display',
    '/opt/deskport/usr/libexec/sunshine/usr/bin/sunshine'))
config = {
    'name': 'deskport', 'arch': 'amd64', 'platform': 'linux',
    'version': version, 'release': '1', 'section': 'net', 'priority': 'optional',
    'maintainer': 'DeskPort <keithxc@users.noreply.github.com>',
    'description': 'Remote desktop viewer and optional Sunshine host\n'
                   'Includes a private Qt/media runtime under /opt/deskport. '
                   'Host capture and input require session/device permissions.',
    'homepage': 'https://github.com/keithxc/deskport', 'license': 'GPL-3.0-or-later',
    'contents': [
        {'src': str(host_permission),
         'dst': '/usr/share/applications/io.github.keithxc.DeskPort.host.desktop'},
        {'src': str(permission),
         'dst': '/usr/share/applications/io.github.keithxc.DeskPort.display.desktop'},
        {'src': str(appdir) + '/', 'dst': '/opt/deskport', 'type': 'tree'},
        {'src': str(launcher), 'dst': '/usr/bin/deskport'},
        {'src': str(appdir / 'usr/share/applications/io.github.keithxc.DeskPort.desktop'),
         'dst': '/usr/share/applications/io.github.keithxc.DeskPort.desktop'},
        {'src': str(appdir / 'usr/share/icons/hicolor/scalable/apps/deskport.svg'),
         'dst': '/usr/share/icons/hicolor/scalable/apps/deskport.svg'},
        {'src': str(appdir / 'usr/share/metainfo/io.github.keithxc.DeskPort.appdata.xml'),
         'dst': '/usr/share/metainfo/io.github.keithxc.DeskPort.appdata.xml'},
    ],
    'overrides': {
        'deb': {'depends': ['libc6 (>= 2.39)', 'libstdc++6', 'libgl1', 'libegl1', 'libdrm2',
                           'libx11-6', 'libxcb1', 'libxkbcommon0', 'libudev1',
                           'libpulse0', 'libasound2t64', 'libdbus-1-3',
                           'libfontconfig1', 'libfreetype6', 'libpipewire-0.3-0t64',
                           'libcom-err2', 'libgpg-error0', 'libopengl0', 'libharfbuzz0b', 'libfribidi0', 'libsm6', 'libice6',
                           'bash', 'coreutils']},
        'rpm': {'depends': ['glibc >= 2.39', 'libstdc++', 'libglvnd-glx', 'libglvnd-egl', 'libdrm',
                           'libX11', 'libxcb', 'libxkbcommon', 'systemd-libs',
                           'pulseaudio-libs', 'alsa-lib', 'dbus-libs',
                           'fontconfig', 'freetype', 'pipewire-libs', 'libglvnd-opengl',
                           'harfbuzz', 'fribidi', 'libSM', 'libICE', 'bash', 'coreutils']},
        'archlinux': {'depends': ['glibc>=2.39', 'gcc-libs', 'libglvnd', 'libx11', 'libdrm',
                                 'libxcb', 'libxkbcommon', 'systemd-libs', 'libpulse',
                                 'alsa-lib', 'dbus', 'fontconfig', 'freetype2',
                                 'pipewire', 'harfbuzz', 'fribidi', 'libsm', 'libice', 'bash', 'coreutils']},
    },
}
manifest = work / 'nfpm.json'
manifest.write_text(json.dumps(config, indent=2) + '\n')
for packager, name in [('deb', f'deskport_{version}-1_amd64.deb'),
                       ('rpm', f'deskport-{version}-1.x86_64.rpm'),
                       ('archlinux', f'deskport-{version}-1-x86_64.pkg.tar.zst')]:
    subprocess.run([str(cache / 'nfpm'), 'package', '--config', str(manifest),
                    '--packager', packager, '--target', str(work / 'output' / name)], check=True)
