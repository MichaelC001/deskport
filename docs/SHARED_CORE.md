# Shared core development

See [extraction verification](SHARED_CORE_VERIFICATION.md) for completed checks and limits.

2026-09-17: Extract workspace arithmetic and freeze the existing display contract
to avoid implementing the same policy separately in desktop and native clients.

The public [deskport-core](https://github.com/keithxc/deskport-core) repository is
the source of truth for the drawable-pixel workspace algorithm and scoped display
wire contract. It is pinned at `shared/deskport-core`; Nix consumes the same commit
through the non-flake `deskport-core` input. `scripts/test-core.py` rejects differing
Git and Nix pins. Ordinary GitHub flake fetches do not include submodules, so the
derivation explicitly copies the locked core into the source tree before building.

```sh
git submodule update --init shared/deskport-core
nix develop -c python3 scripts/test-core.py
nix develop -c python3 scripts/test-host-lifecycle.py --binding  # macOS, fake host
```

For a core update, check out the reviewed commit in the submodule, then run
`nix flake update deskport-core --override-input deskport-core github:keithxc/deskport-core/COMMIT`.
Commit the gitlink and flake.lock together. Run the adapter tests and platform
checks before updating the other consumer. Pins may differ temporarily; on-wire
compatibility must not depend on simultaneous product upgrades.

`app/backend/workspaceresolution.h` only adapts QSize and output scale. The public
core takes drawable pixels, never logical points. The desktop adapter retains its
existing invalid-input sentinel and proposal behavior. HostManager independently
validates requests; native capture/display APIs remain authoritative. Window state,
input capture, TLS, media and Qt UI remain in this repository.

Every cross-platform feature should record its wire/behavior contract, platform
support, downgrade behavior and test cases once in the core. Consumers implement
only their native adapters. New optional messages require peer opt-in; a shared
header alone is not a compatibility test. `protocol/display-cases.json` is exercised
through the production desktop TLS handler and Apple result/caret handler.

Backlog: binding fixtures across both adapters, versioned capability evolution when
needed, and a session-scoped input state machine only after a second consumer needs
it. Do not move global Moonlight input wrappers, keyboard/safe-area policy or release
tooling into the arithmetic core. No deployed service activation is part of a core
update; the existing release and manual-activation workflow still applies.

## Session policy update — 2026-09-17

The next core pin adds the optional `displayPolicy` capability, three stable mode
values, and malformed-policy fixtures. See [SESSION_DISPLAY.md](SESSION_DISPLAY.md).
The base binding version remains 1; peers negotiate the optional field explicitly.
Local core commits must be pushed before publishing consumer branches that pin them.

## Session takeover prerelease — 2026-09-19

The prerelease pins the shared admission contract and workspace adjustment policy
in both consumer Git submodules and the host Nix input. See core
`protocol/SESSION_TAKEOVER.md`. Physical takeover acceptance remains pending
until the user activates the prerelease.

## Public catalog and private mobile boundary — 2026-09-20

The independently authored `shared/deskport-core/portable/` module is MIT and
owns the port family, desktop tuning options and display policy IDs. The existing
GPL core files retain their licenses. `hostports.h` consumes generated C rules;
StreamingPreferences exposes catalog choices/labels to QML. Platform sockets,
settings persistence, localized text and media remain native.

Run the normal core and device-preference tests after changing the catalog. Never
edit generated C/Java output directly. See core `portable/FEATURES.md` for adapter
coverage and the rules for advancing core and consumer pins. The mobile product's
private repository and paid/proprietary target do not relicense any PC code.

## Stable entry update — 2026-09-21

See core `protocol/ENDPOINTS.md`. Mobile and desktop authenticated refresh retain
the configured connection entry independently from streaming endpoints. Mobile
legacy stream overrides migrate their hostname to the default entry; new explicit
entries are preserved. Run the endpoint transport and binding regressions.

## Private cycle-guard snapshot — 2026-09-26

This test branch pins the core Git tree and the identical immutable NAR snapshot.
The path input is preloaded from the private test Release, not a mutable working
directory. `test-core.py` verifies the Git archive NAR hash against flake.lock.
The delivery preparation tool restores both source snapshots after GC or on a
new machine. Core/source commits remain local; a later public integration must
publish the reviewed core and restore a reachable public input before publishing
its consumer. This private pin must not be merged into the public release branch.

## Public release pin — 2026-09-26

The public main branch now uses the reachable GitHub revision
`e71b21808e15c8bcd55d4af3f7c0cbc769ef0bcb` in both the Git submodule
and flake.lock. The private snapshot instructions above are historical;
public releases must retain the matching public pins.

## Local video pause candidate — 2026-09-28

This development candidate pins core `a71542a` and its matching immutable NAR
snapshot for optional video transmission pause. See core `protocol/VIDEO_PAUSE.md`.
The local snapshot is preloaded on test machines; no signature bypass is used.
Before public consumer integration, publish the reviewed core and restore a
reachable GitHub pin. The current local path pin is not a public release input.

## Reachable dev core pin — 2026-09-29

The video pause core change is now published on the core `dev` branch as
`2ebf5c9` (the local `a71542a` with corrected authorship and an identical
tree). The Git submodule and flake.lock both pin that GitHub revision; its NAR
hash equals the earlier local snapshot. Consumers can fetch DeskPort and its core
without preloaded store paths. Core `main` is unchanged: merging the video
pause specification there still requires review.

## Desktop tuning direction — 2026-10-01

Core `a107606` (on `dev`) inverts desktop fine tuning: larger values now enlarge
remote content by dividing the workspace, so 1.5 shows larger controls and 0.5
fits more content. Saved per-device values keep their number and therefore flip
meaning. Other consumers adopt the new direction when they advance their pins.
The macOS 800×600 logical floor still applies after tuning, so small client
windows can reach the floor at 1.0 and show no further enlargement.

## Core main alignment — 2026-10-03

Core `dev` (video pause specification and the desktop tuning direction) is
merged into core `main` as `4e80ab8`, keeping main's newer full-screen manual
text. DeskPort `main` now pins that core `main` revision in both the Git
submodule and flake.lock.

## QR invitation development candidate — 2026-10-04

This candidate pins core `e7b4a47` and an identical immutable Nix store snapshot.
Only `protocol/PAIRING_INVITES.md` and its shared URI fixtures are added relative
to the desktop's prior core. Native adapters implement the optional invitation
capability without changing the binding version. The mobile candidate uses the
same contract and fixtures on its previous core base (`83235de`) to preserve
its existing workspace behavior. These local pins are development inputs;
publish reviewed core commits and restore a reachable immutable source before
public consumer integration. Core/pin verification and adapter tests remain
required. No release or host activation is performed by updating a core pin.
