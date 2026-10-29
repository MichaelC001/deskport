#!/usr/bin/env python3
"""Apply host/series to a freshly prepared vendored host source tree."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
PLATFORMS = {'macos', 'linux-nix', 'linux-portable', 'windows'}
TAGS = PLATFORMS | {'linux'}
ACTIONS = {'script', 'apply', 'apply-recount', 'copy'}
OVERLAYS = 'host/overlays'


def parse_series(path):
    """Return (line number, platforms, action, arguments) for every entry."""
    entries = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.split('#', 1)[0].split()
        if not fields:
            continue
        if len(fields) < 3:
            raise SystemExit(f'{path}:{number}: expected PLATFORMS ACTION ARGUMENTS')
        platforms, action, arguments = fields[0], fields[1], fields[2:]
        selected = TAGS if platforms == 'all' else set(platforms.split(','))
        if not selected <= TAGS:
            raise SystemExit(f'{path}:{number}: unknown platform in {platforms}')
        if action not in ACTIONS:
            raise SystemExit(f'{path}:{number}: unknown action {action}')
        if action != 'script' and len(arguments) != 2:
            raise SystemExit(f'{path}:{number}: {action} takes exactly two arguments')
        entries.append((number, selected, action, arguments))
    return entries


def plan(entries, platform, repo, source):
    """Expand one platform's steps and check every repository input up front."""
    steps = []
    for number, selected, action, arguments in entries:
        if platform not in selected and platform.split('-')[0] not in selected:
            continue
        arguments = [value.replace('{src}', str(source)).replace('{repo}', str(repo))
                     for value in arguments]
        if action == 'script':
            inputs = [repo / OVERLAYS / arguments[0]]
            inputs += [Path(value) for value in arguments[1:] if value.startswith(str(repo))]
        else:
            inputs = [repo / arguments[0 if action == 'copy' else 1]]
        missing = [str(path) for path in inputs if not path.exists()]
        if missing:
            raise SystemExit(f'series line {number}: missing {", ".join(missing)}')
        steps.append((number, action, arguments))
    return steps


def run(steps, repo, source):
    for number, action, arguments in steps:
        print(f'[series {number}] {action} {" ".join(arguments)}', flush=True)
        if action == 'script':
            subprocess.run([sys.executable, str(repo / OVERLAYS / arguments[0]), *arguments[1:]],
                           check=True)
        elif action in {'apply', 'apply-recount'}:
            recount = ['--recount'] if action == 'apply-recount' else []
            subprocess.run(['git', 'apply', *recount, str(repo / arguments[1])],
                           cwd=source / arguments[0], check=True)
        else:
            target = source / arguments[1]
            if arguments[1].endswith('/'):
                target = target / Path(arguments[0]).name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repo / arguments[0], target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=sorted(PLATFORMS), required=True)
    parser.add_argument('--repo', type=Path, default=REPO,
                        help='Directory holding host/series and the host/ inputs it names')
    parser.add_argument('--list', action='store_true', help='Print the steps without applying them')
    parser.add_argument('source', type=Path)
    options = parser.parse_args()
    repo, source = options.repo.resolve(), options.source.resolve()
    steps = plan(parse_series(repo / 'host/series'), options.platform, repo, source)
    if options.list:
        for number, action, arguments in steps:
            print(number, action, *arguments)
        return
    if not source.is_dir():
        raise SystemExit(f'Host source tree does not exist: {source}')
    run(steps, repo, source)


if __name__ == '__main__':
    main()
