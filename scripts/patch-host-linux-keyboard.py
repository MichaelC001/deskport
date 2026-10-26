#!/usr/bin/env python3
"""Give the Linux host's existing uinput keyboard a remapper-visible identity."""
from pathlib import Path
import sys


def patch_keyboard(root):
    inputtino = root / 'src/platform/linux/input/inputtino_common.h'
    virtualhid = root / 'src/platform/virtualhid_input.cpp'
    if inputtino.is_file():
        path = inputtino
        old = '.name = inputtino_name_for_seat("Keyboard passthrough"sv),'
        new = '''// Keep the receiving keyboard selectable independently of Sunshine.
          .name = inputtino_name_for_seat("DeskPort Keyboard"sv),'''
    elif virtualhid.is_file():
        path = virtualhid
        old = '    options.profile = lvh::profiles::keyboard();'
        new = '''    options.profile = lvh::profiles::keyboard();
#ifdef __linux__
    // Use the same evdev identity as the Nix host for receiver-side remappers.
    options.profile.name = "DeskPort Keyboard";
#endif'''
    else:
        raise SystemExit('Unsupported Linux host keyboard backend')

    source = path.read_text()
    if source.count(new) == 1:
        return
    if source.count(old) != 1:
        raise SystemExit(f'Linux keyboard identity anchor mismatch: {path.name}')
    path.write_text(source.replace(old, new))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(f'Usage: {sys.argv[0]} HOST_SOURCE')
    patch_keyboard(Path(sys.argv[1]))
