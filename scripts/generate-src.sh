#!/usr/bin/env bash
# Build the release source tarball. Run from the root of the Git repo.
#
# Every third-party dependency is vendored in this repository, so plain
# `git archive` already produces a complete tree. Only shared/deskport-core is
# still a submodule and has to be spliced in. This deliberately avoids
# git-archive-all: that script needed GNU tar and Bash 5, which neither macOS
# nor the project devShell provides.

set -euo pipefail

fail() {
	echo "$1" 1>&2
	exit 1
}

git diff-index --quiet HEAD -- || fail "Source archives must not have unstaged changes!"
[ -f shared/deskport-core/CMakeLists.txt ] || [ -d shared/deskport-core/tests ] ||
	fail "shared/deskport-core is not checked out: run git submodule update --init"

VERSION=$(cat app/version.txt)
PREFIX="DeskPort-$VERSION"
OUT_DIR="$PWD/build/source"
OUT="$OUT_DIR/$PREFIX-source.tar.gz"

echo "Cleaning output directory"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/deskport-src.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/$PREFIX"
git archive HEAD | tar -x -C "$STAGE/$PREFIX"
mkdir -p "$STAGE/$PREFIX/shared/deskport-core"
git -C shared/deskport-core archive HEAD | tar -x -C "$STAGE/$PREFIX/shared/deskport-core"

tar -czf "$OUT" -C "$STAGE" "$PREFIX"

echo "Archive successful: $OUT"
