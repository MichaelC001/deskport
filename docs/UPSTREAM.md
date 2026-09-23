# Upstream and licensing

DeskPort is an independent derivative, not an official Moonlight or Sunshine
release. It connects to a separately installed Sunshine host.

- Base: [Moonlight Qt v6.1.0](https://github.com/moonlight-stream/moonlight-qt/tree/v6.1.0)
- Commit: `f786e94c7b2f943e24e65d7d74deb539b827fc84`
- Reason: matches the known Nix package baseline used for initial development.
  This is not a claim that v6.1.0 is the latest upstream version.
- Original build/reference documentation: [README.upstream.md](readme/README.upstream.md).
- Initial DeskPort modifications dated 2026-09-09: application identity, desktop
  defaults, update-channel separation, Nix packaging and project documentation.

The inherited LICENSE is retained. DeskPort-specific code is distributed under
GPL-3.0-or-later; original notices and component-specific licenses remain in
place. Every vendored dependency keeps its own license file; see
[VENDORED.md](VENDORED.md) for the full list with upstream URLs and commits.
Include corresponding source and build files when distributing binaries — since
0.4.1 the release source tarball is complete on its own.

Since 0.4.1 the third-party dependencies are vendored into this repository
rather than tracked as gitlinks, so the Nix flake builds entirely from this
repository and no longer refetches the upstream source to recover submodule
contents. Only `shared/deskport-core`, this project's own shared code, is still
a submodule.

No personal device configuration, credentials, employer code, SDKs or work logs
belong in the public repository.

Some inherited UI wording and Windows/macOS artwork still refer to Moonlight.
Native packaging scripts are upstream reference material until adapted and
tested; Linux is the only bootstrap validation target. Do not use an upstream
installer recipe or publish a platform build as verified without testing it.

To track upstream locally:

```sh
git remote add upstream https://github.com/moonlight-stream/moonlight-qt.git
git fetch upstream
```
