#!/usr/bin/env python3
"""Run one existing distro-lab baseline with checksum-locked release artifacts.

Runs on the designated Linux builder; reuses immutable workspace lab baselines. No
fetch, provision, host install, policy change or personal GUI is performed.
"""
import argparse
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tarfile
import time


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', required=True)
    parser.add_argument('--old', type=Path, required=True, help='Existing 0.7.1 package directory')
    parser.add_argument('--new', type=Path, required=True, help='Existing 0.7.2 package directory')
    parser.add_argument('--output', type=Path, required=True, help='New run directory below ~/mygit/build/deskport')
    parser.add_argument('--thread', required=True)
    parser.add_argument('--native', choices=('deb', 'rpm', 'arch'))
    parser.add_argument('--no-appimage', action='store_true', help='Repeat only the selected native/Flatpak route')
    parser.add_argument('--session', choices=('wayland', 'x11'), default='wayland')
    parser.add_argument('--dependency', type=Path, action='append', default=[], help='Explicit offline native dependency package')
    parser.add_argument('--debugger-runtime', type=Path, help='Explicit cached GDB closure archive with source.json pin')
    parser.add_argument('--package-api', action='store_true', help='Check the current native package loopback API and runtime metadata')
    parser.add_argument('--flatpak', action='store_true', help='Use preinstalled runtime; no network pulls')
    parser.add_argument('--flatpak-runtime', type=Path, action='append', default=[], help='Offline Platform/GL runtime bundle with adjacent .json commit pin; repeat for GL')
    parser.add_argument('--wait-seconds', type=int, default=0, help='Bounded wait for the shared VM lock; never steal it')
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    guest = Path(__file__).with_name('test-linux-desktop-lifecycle.py')
    compile(guest.read_text(), str(guest), 'exec')
    build = (Path.home() / 'mygit/build/deskport').resolve()
    output = args.output.expanduser().resolve()
    assert output.is_relative_to(build), 'Evidence must stay under DeskPort build root'
    lab = Path.home() / 'mygit/workspace/skills/linux-distro-lab/scripts/lab.py'
    output.mkdir(parents=True, exist_ok=False)
    (output / '.keep').touch()
    profiles = json.loads((lab.parent.parent / 'profiles.json').read_text())['profiles']
    (output / 'image-pin.json').write_text(json.dumps(profiles[args.profile], indent=2) + '\n')
    baselines = json.loads(subprocess.check_output([sys.executable, str(lab), 'baselines'], text=True))
    baseline = Path(baselines[args.profile]['path'])
    shutil.copy2(baseline / 'manifest.json', output / 'baseline-manifest.json')
    locks = build / 'locks'
    locks.mkdir(parents=True, exist_ok=True)
    lock = locks / 'pk4-vm.lock'
    deadline = time.monotonic() + args.wait_seconds
    announced = False
    while True:
        try:
            lock.mkdir()  # Atomic, never steal an existing owner's lock.
            break
        except FileExistsError:
            if time.monotonic() >= deadline:
                raise
            if not announced:
                print(f'Waiting for existing VM owner: {lock}', flush=True)
                announced = True
            time.sleep(min(15, max(0, deadline - time.monotonic())))
    cleanup_verified = True
    try:
        (lock / 'owner.json').write_text(json.dumps(dict(thread=args.thread, pid=os.getpid(),
            start_ticks=Path('/proc/self/stat').read_text().rsplit(')', 1)[1].split()[19],
            boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
            time=datetime.datetime.now(datetime.timezone.utc).isoformat(), output=str(output))))
        subprocess.run([sys.executable, str(lab), 'inventory'], check=True)
        assert not args.no_appimage or args.native or args.flatpak
        manifest = dict(schema=1, session=args.session, package_api=args.package_api, packages=[], routes=([] if args.no_appimage else ['appimage']) + ([args.native] if args.native else [])
                        + (['flatpak'] if args.flatpak else []))
        patterns = dict(appimage='DeskPort-{v}-x86_64.AppImage', deb='deskport_{v}-1_amd64.deb',
                        rpm='deskport-{v}-1.x86_64.rpm', arch='deskport-{v}-1-x86_64.pkg.tar.zst',
                        flatpak='DeskPort-{v}-client-x86_64.flatpak')
        bundle = output / 'lifecycle.tar.gz'
        # Packages are already compressed. A valid gzip envelope with level 0
        # avoids recompressing hundreds of MB while retaining the lab input type.
        with tarfile.open(bundle, 'w:gz', compresslevel=0) as archive:
            archive.add(guest, arcname='scenario.py')
            archive.add(Path(__file__).with_name('check-linux-package.py'), arcname='check-linux-package.py')
            for route in manifest['routes']:
                for role, version, directory in [('old', '0.7.1', args.old), ('new', '0.7.2', args.new)]:
                    path = directory / patterns[route].format(v=version)
                    assert path.is_file() and not path.is_symlink(), path
                    manifest['packages'].append(dict(name=path.name, sha256=digest(path), bytes=path.stat().st_size,
                        role=role, route=route, version=version))
                    archive.add(path, arcname=path.name)
            manifest['runtimes'] = []
            for runtime_path in args.flatpak_runtime:
                assert args.flatpak, '--flatpak-runtime requires --flatpak'
                path = runtime_path.resolve()
                assert path.suffix == '.flatpak' and path.is_file()
                pin = json.loads(path.with_suffix('.json').read_text())
                assert pin['ref'] in ('org.freedesktop.Platform/x86_64/25.08', 'org.freedesktop.Platform.GL.default/x86_64/25.08')
                assert len(pin['commit']) == 64
                manifest['runtimes'].append(dict(name=path.name, sha256=digest(path), pin=pin))
                archive.add(path, arcname=path.name)
            manifest['dependencies'] = []
            for path in args.dependency:
                assert args.native == 'deb' and path.suffix == '.deb' and path.is_file()
                assert path.name not in [p['name'] for p in manifest['packages']]
                manifest['dependencies'].append(dict(name=path.name, sha256=digest(path)))
                archive.add(path, arcname=path.name)
            if args.debugger_runtime:
                path = args.debugger_runtime.resolve()
                pin = json.loads((path.parent / 'source.json').read_text())
                assert digest(path) == pin['archive_sha256']
                assert pin['path'].startswith('/nix/store/') and pin['path'].endswith('/bin/gdb')
                manifest['debugger'] = dict(name=path.name, sha256=pin['archive_sha256'], path=pin['path'])
                archive.add(path, arcname=path.name)
            payload = (json.dumps(manifest, indent=2) + '\n').encode()
            header = tarfile.TarInfo('manifest.json')
            header.size = len(payload)
            archive.addfile(header, io.BytesIO(payload))
        (output / 'manifest.json').write_bytes(payload)
        command = ['python3', '-c',
            'import tarfile,sys; p=sys.argv[1]; '
            'exec(compile(tarfile.open(p).extractfile("scenario.py").read(),"scenario.py","exec"))',
            '/home/tester/lab-inputs/lifecycle.tar.gz', '--session', args.session]
        with (output / 'runner.log').open('w') as log:
            child = subprocess.Popen([sys.executable, str(lab), 'run', '--profile', args.profile,
                '--upload', str(bundle), '--'] + command, stdout=log, stderr=subprocess.STDOUT)
            cleanup_verified = False
            interrupted = None
            try:
                exit_code = child.wait()
            except BaseException as error:
                child.send_signal(signal.SIGTERM)
                exit_code = child.wait(timeout=120)  # Allow the lab's owned teardown finally block.
                interrupted = error
        state = subprocess.check_output([sys.executable, str(lab), 'status'], text=True)
        (output / 'lab-state.json').write_text(state)
        status = json.loads(state)
        assert status.get('profile') == args.profile and status.get('teardown') == 'passed', status
        assert status.get('baseline_verification') == 'passed', status
        assert any(item['source'] == str(bundle) for item in status.get('uploads', [])), 'Runner status belongs to another input bundle'
        cleanup_verified = True
        if interrupted:
            raise interrupted
        evidence = Path(status['evidence'])
        for name in ('command.stdout', 'command.stderr', 'before-command-health.json', 'guest-health.json'):
            if (evidence / name).is_file():
                shutil.copy2(evidence / name, output / name)
        print(json.dumps(dict(profile=args.profile, output=str(output), lab_evidence=str(evidence),
            exit_code=exit_code, teardown=status.get('teardown'))))
        return exit_code
    finally:
        if cleanup_verified:
            (lock / 'owner.json').unlink(missing_ok=True)
            lock.rmdir()
        else:
            print(f'Cleanup is unverified; retain ownership lock: {lock}', file=sys.stderr)


if __name__ == '__main__':
    sys.exit(main())
