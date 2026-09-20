# Vendored third-party sources

The desktop viewer's upstream code is stored as plain files. The supported
Sunshine host sources are stored in checksum-verified archives under
`host/vendor`, together with upstream provenance and licenses. Shared core,
distribution libraries and build toolchains remain separately pinned dependencies.
This protects the included component sources from upstream repository deletion;
it is not a promise that the whole toolchain builds offline.

Vendored on 2026-09-17, for 0.4.1. Each directory keeps its own upstream
licence file untouched; DeskPort adds no licence terms to them. See
[UPSTREAM.md](UPSTREAM.md) for the Moonlight base this project derives from.

## What is vendored

| Path | Upstream | Commit | Licence |
| --- | --- | --- | --- |
| `moonlight-common-c/moonlight-common-c` | [moonlight-stream/moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) | `8599b6042a4ba27749b0f94134dd614b4328a9bc` | GPL-3.0 (`LICENSE.txt`) |
| `moonlight-common-c/moonlight-common-c/enet` | [cgutman/enet](https://github.com/cgutman/enet) | `bbf71856bb144729af4ed08af6bc2f5826a96db5` | MIT (`LICENSE`) |
| `qmdnsengine/qmdnsengine` | [cgutman/qmdnsengine](https://github.com/cgutman/qmdnsengine) | `b7a5a9f225d5e14b39f9fd1f905c4f505cf2ee99` | MIT (`LICENSE.txt`) |
| `app/SDL_GameControllerDB` | [gabomdq/SDL_GameControllerDB](https://github.com/gabomdq/SDL_GameControllerDB) | `e5a5fa2ac6e645d72c619ea99520a3a4586ee005` | zlib (`LICENSE`) |
| `soundio/libsoundio` | [cgutman/libsoundio](https://github.com/cgutman/libsoundio) | `34bbab80bd4034ba5080921b6ba6d61314126310` | MIT (`LICENSE`) |
| `h264bitstream/h264bitstream` | [aizvorski/h264bitstream](https://github.com/aizvorski/h264bitstream) | `34f3c58afa3c47b6cf0a49308a68cbf89c5e0bff` | LGPL-2.1 (`LICENSE`) |
| `libs/mac` | [cgutman/moonlight-qt-prebuilts](https://github.com/cgutman/moonlight-qt-prebuilts) | `a27d6a7995ef504963fa9058c69e6ba1b449cc0f` | upstream licences of each binary, see `libs/README.md` |

`enet` was a submodule of `moonlight-common-c`, not of this repository; its
gitlink and `moonlight-common-c`'s own `.gitmodules` were removed when the
directory became plain content.

The five code repositories were added with `git subtree add --squash`, so each
carries a `Squashed '<path>/' content from commit <sha>` commit that records
the exact upstream revision. `libs/mac` was committed as ordinary files.

## What is not vendored

- **`shared/deskport-core`** is still a submodule. It is this project's own
  code, shared with `deskport-client`, and vendoring it into both repositories
  would let the two copies drift. The Nix flake takes it from its own input and
  `scripts/generate-src.sh` splices it into the source tarball.
- **`libs/windows`** (252 MB) was deliberately dropped. See
  [`libs/VENDORED.md`](../libs/VENDORED.md) for why and how to restore it.
- **nixpkgs** supplies Qt, FFmpeg, SDL and the Linux host build recipe. The host source now comes from the repository archive.

## Updating a vendored dependency

```sh
git subtree pull --prefix=<path> https://github.com/<owner>/<repo>.git <ref> --squash
```

Then update the commit in the table above in the same change. For `libs/mac`,
copy the files from the upstream repository at the new commit and update both
this table and `libs/VENDORED.md`.

## Consequences

- `git archive` contains the vendored third-party content. Release source
  tarballs come from `scripts/generate-src.sh`, which also inserts the pinned
  shared core and no longer needs GNU tar or Bash 5. Toolchains and system
  libraries still come from their normal dependency providers.
- The Nix flake no longer refetches `moonlight-stream/moonlight-qt` with
  `fetchSubmodules` to recover gitlink contents. That workaround existed only
  because GitHub archive tarballs carry no submodules, which is how `mynix`
  consumes this flake.
- A fresh clone needs `git submodule update --init shared/deskport-core` and
  nothing else.

## Host sources and assets — 2026-09-20

- `host/vendor/sunshine.tar.gz`: pristine macOS/portable Linux host revision
  cb72dffa3233c5815cd5ba88f09f049dd679ba75 and required source submodules.
  `sunshine.json` records each upstream URL/revision and the archive SHA-256.
- `host/vendor/sunshine-nix.tar.gz`: the unchanged 2026.516.143833 source from
  the locked Nix package, preserving that recipe's compatibility. FFmpeg source
  build submodules not used by this recipe are excluded; Nix supplies FFmpeg.
- `host/vendor/sunshine-macos-resources.tar.gz`: static web resources/notices
  from the pinned, signature-verified upstream DMG, plus the matching Sunshine
  source license. The matching JSON identifies
  the original donor and hashes. Packaging no longer downloads that DMG.

All original license files are retained in their archive paths. Sunshine is
GPL-3.0; component notices retain their respective terms. `prepare-host-source.py`
verifies the archive before materializing a managed build-only checkout. Reviewed
DeskPort overlays remain outside the archives. It refuses to overwrite unmanaged
source directories. System/distribution dependency acquisition remains separate;
mobile Moonlight submodules are not part of this desktop change.
