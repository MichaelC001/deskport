#!/usr/bin/env python3
"""Regression test for colliding macOS dylib install names."""
import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[1]
relocator = repo / 'scripts/fix-macos-dependencies.py'
scratch = pathlib.Path(os.environ.get(
    'DESKPORT_BUILD_ROOT', pathlib.Path.home() / 'mygit/build/deskport')) / 'relocation-tests'
scratch.mkdir(parents=True, exist_ok=True)

def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

def file_hashes(root):
    return {path.relative_to(root): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob('*') if path.is_file() and not path.is_symlink()}

with tempfile.TemporaryDirectory(prefix='deskport-macos-relocation-', dir=scratch) as temporary:
    root = pathlib.Path(temporary)
    app = root / 'Probe.app'
    executable_dir = app / 'Contents/MacOS'
    executable_dir.mkdir(parents=True)
    consumers = []
    for label, value in [('darwin', 7), ('gnu', 10)]:
        source = root / label
        source.mkdir()
        collision = source / 'libiconv.2.dylib'
        implementation = source / 'iconv.c'
        implementation.write_text(f'int {label}_iconv(void) {{ return {value}; }}\n')
        install_name = ('@loader_path/libiconv.2.dylib' if label == 'darwin'
                        else '@rpath/libiconv.2.dylib')
        run('xcrun', 'clang', '-dynamiclib', str(implementation),
            f'-Wl,-install_name,{install_name}', '-o', str(collision))
        consumer_source = source / f'consumer-{label}.c'
        consumer_source.write_text(
            f'extern int {label}_iconv(void);\n'
            f'int consumer_{label}(void) {{ return {label}_iconv(); }}\n')
        consumer = source / f'libconsumer-{label}.dylib'
        linker_arguments = (['-Wl,-rpath,' + str(source)] if label == 'gnu' else [])
        run('xcrun', 'clang', '-dynamiclib', str(consumer_source), str(collision),
            *linker_arguments, f'-Wl,-install_name,{consumer}', '-o', str(consumer))
        consumers.append(consumer)

    main_source = root / 'main.c'
    main_source.write_text(
        '#include <stdio.h>\n'
        'extern int consumer_darwin(void);\n'
        'extern int consumer_gnu(void);\n'
        'int main(void) { printf("%d\\n", consumer_darwin() + consumer_gnu()); return 0; }\n')
    probe = executable_dir / 'probe'
    run('xcrun', 'clang', str(main_source), *(str(path) for path in consumers), '-o', str(probe))

    run(sys.executable, str(relocator), str(app))
    first_files = sorted(path.name for path in (app / 'Contents/Frameworks').iterdir())
    iconv_files = [name for name in first_files if name.startswith('libiconv.2')]
    assert len(iconv_files) == 2, iconv_files
    result = run(str(probe), capture_output=True)
    assert result.stdout == '17\n', result.stdout

    darwin_links = subprocess.check_output(
        ['otool', '-L', str(app / 'Contents/Frameworks/libconsumer-darwin.dylib')], text=True)
    gnu_links = subprocess.check_output(
        ['otool', '-L', str(app / 'Contents/Frameworks/libconsumer-gnu.dylib')], text=True)
    darwin_iconv = next(line.strip().split(' (')[0] for line in darwin_links.splitlines()
                        if 'libiconv.2' in line)
    gnu_iconv = next(line.strip().split(' (')[0] for line in gnu_links.splitlines()
                     if 'libiconv.2' in line)
    assert darwin_iconv != gnu_iconv, (darwin_iconv, gnu_iconv)
    assert darwin_iconv.startswith('@loader_path/'), darwin_iconv
    assert gnu_iconv.startswith('@loader_path/'), gnu_iconv

    # A second pass must remain stable and must not create another collision copy.
    first_hashes = file_hashes(app)
    run(sys.executable, str(relocator), str(app))
    second_files = sorted(path.name for path in (app / 'Contents/Frameworks').iterdir())
    assert second_files == first_files, (first_files, second_files)
    assert file_hashes(app) == first_hashes
    result = run(str(probe), capture_output=True)
    assert result.stdout == '17\n', result.stdout

print('PASS: colliding dylib basenames retain distinct source ABIs and remain runnable')
