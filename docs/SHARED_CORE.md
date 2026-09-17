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
