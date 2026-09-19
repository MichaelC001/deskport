# Confirmed session takeover

2026-09-19: A new viewer must not bypass an occupied display lease by launching a
second fixed-size Sunshine stream. The wire contract is owned by
`shared/deskport-core/protocol/SESSION_TAKEOVER.md`.

The desktop viewer opts into admission on its retained display TLS connection.
It asks before takeover, defaults the dialog to Cancel, and treats any failure
after capability negotiation as fatal. Video-only resize/reconnect keeps the
admission connection. Losing an admitted connection stops the stream.

PeerManager checks completed binding and TLS identity, issues a connection-bound
30-second single-use challenge, and serializes admission. HostManager calls the
pinned-certificate, Basic-authenticated loopback management API. Sunshine shares a
mutex across snapshot/claim, GameStream launch/resume/cancel and final RTSP
admission. The verified request object owns the certificate mapping; an unrelated
TLS handshake cannot overwrite it. Generation fencing rejects older pending RTSP
negotiations. Joining the old streams and an input task-pool fence precede success.
PeerManager also waits for display recovery and clipboard-helper exit.

The management endpoint does not remove pairings, close desktop applications,
kill Sunshine, or restart sharing. A timeout fails closed. An eviction already
completed cannot be undone if the new client disconnects before receiving success.

## Desktop adjustment

Device settings and the tray menu offer 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.2, 1.3,
1.4 and 1.5. The final adjustment uses core `dp_workspace_adjust` after the existing
desktop calculation; scale is unchanged and the protocol bounds still apply.
Smaller values enlarge controls; larger values fit more content. The tray action
saves only this device's adjustment key and reconnects video using the retained
admission connection. It does not overwrite other preferences from an old snapshot.

## Isolated acceptance alongside an existing host

Do not run a candidate with the normal `--share` option while testing alongside a
production instance. Use a signed candidate bundle and:

```sh
bash scripts/run-isolated-session-host.sh /absolute/path/to/candidate/DeskPort.app
```

The script creates a private temporary directory, a new identity and portable
settings. The test window requires explicit binding approval and shows the ports.
The full TCP/UDP group is checked on all IPv4 interfaces and a non-default private
port bucket is preferred. The binding listener moves to video base port + 2, so
normal clients can discover it from the video address. A temporary random binding
listener remains an alias. Bind both test clients before starting the old stream:
initial trust approval restarts only the temporary host.

The test display has a separate product/serial identity. **This parallel test mode
always uses its own extended workspace**, regardless of the client's topology
selection. It neither reads the production display-recovery journal nor mirrors,
disables or restores another display. This mode cannot validate production
mirror/primary-only behavior. Closing its window or the 15-minute deadline stops
only its own child processes. The temporary state directory remains for inspection.
Never reuse the earlier pre-isolation candidates for concurrent-host testing.

Check idle admission, cancel preserving old video/input, confirmed takeover ending
the old actual video, old held-key release, two concurrent contenders, repeat
resize/rotation, 0.5/1.0/1.5 adjustment and reloaded per-device settings. Compilation,
signing and fake-host tests do not establish these physical-device outcomes.

## Automated evidence

```sh
nix develop -c python3 scripts/test-core.py
nix develop -c python3 scripts/test-host-lifecycle.py --binding
nix develop -c python3 scripts/test-host-lifecycle.py --ui
nix develop -c python3 scripts/test-device-preferences.py
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer python3 scripts/test-session-topology.py
```

The host suite uses real loopback TLS plus an authenticated fake Sunshine server.
Its cases cover idle reservations, same-identity contenders, legacy control
exclusion, cancel, successful takeover, changed snapshots, backend errors, expiry,
foreign/replayed tokens, competing confirmations and unapproved devices. The
macOS topology suite uses an in-memory WindowServer under ASan/UBSan and verifies
that isolated mode cannot write another display's state or shared journal.
