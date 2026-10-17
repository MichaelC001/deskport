# Installed Windows display service

## Scope

This candidate removes per-sharing UAC for an installed, signed-in physical
console session. The administrator-authorized full installer registers the
independent LocalSystem `DeskPortDisplayService` as an automatic-start SCM service.
It has no network listener, Sunshine configuration parser, pairing database,
application launcher, clipboard or input API. An idle service does not start the
GUI, create a user login or make the machine remotely accessible before login.

`HostManager`, `PeerManager`, identity and Sunshine still run as the interactive
user. Login startup remains the user's `HKCU` Run entry. Stop sharing and saved
startup opt-outs retain their existing meanings. Shared-core wire/display policy
is unchanged. The portable ZIP remains usable as a viewer; sharing requires the
full installation at the SCM-registered path. Missing/denied services produce one
repair error, without `runas` fallback or an elevation loop. Normal installer and
driver administrator authorization remains necessary.

## Trust boundary and lifetime

The broker accepts one versioned 88-byte acquisition request on a first-instance,
local-only, message-mode pipe. The sole operation contains a bounded UUID nonce,
not a PID, session ID, executable, command, path, environment or PnP identity.
Malformed/oversized/partial requests and unsupported operations fail closed. Pipe
I/O is cancellable/bounded; an acknowledgement avoids unbounded flushing. Only
one guardian is active at a time.

The service obtains the PID from the kernel and impersonates the caller. It
compares process/caller/console user and logon identities, requires medium or
higher integrity, the active console session and a live installed
`deskport-display.exe`. Installed executables and every parent path must be
administrator-owned, non-writable by ordinary users and free of reparse points.
SSH/session zero, other sessions/logons, foreign executables, anonymous and
low-integrity requests are rejected. The client grants only identification-level
SQOS, so even a pre-created rogue endpoint cannot borrow its authority. It
authenticates the server PID,
LocalSystem account and exact SCM ImagePath before requesting work. Users may
query SCM configuration/status but cannot start, stop or reconfigure the service.
Executable checks supplement restricted operations; they do not prove human
intent or defeat malware already executing inside the same user's process.

The service starts only the fixed sibling recovery guardian as SYSTEM in the
authorized console session, with a fixed environment and no cross-session handle
inheritance. The console logon receives only process query/synchronize access,
never a SYSTEM token or termination/duplication rights. Process creation times
prevent parent/broker PID-reuse substitution. The guardian validates its SYSTEM
context, SCM broker and installed helper. Local lease objects allow only SYSTEM
and that console logon. The broker stop event allows SYSTEM/administrators only
and rejects pre-existing names.

Existing bounded dimensions, exact owned-device identity, protected journals and
topology verification remain authoritative. Helper death/EOF, broker stop/death,
console-session change and session end release the lease and attempt restoration.
The guardian is outside a kill-on-close service job so broker death cannot kill
recovery. Service stop waits up to 60 seconds for it; installation refuses to
replace files while owned installed processes remain. Recovery failure retains
its protected journal. The boot-disable task still handles stale owned devices.

## Installation and upgrade

The full installer checks the service binary with other required payloads. Its
maintenance helper closes the GUI first, stops only the exact owned service,
waits for guardian cleanup and verifies executables are unused before replacement.
Registration installs/updates automatic startup, restricts the service DACL,
configures two bounded crash-restart delays, records ownership and waits for
readiness. Foreign same-name services are rejected. Errors retain the uninstaller
for repair. Uninstall removes only the exact owned service before existing driver
cleanup. Ordinary maintenance `stop` remains app-only; `stop-installation` is the
installer barrier. User identity/peers are not moved or promoted to SYSTEM.

## Isolated validation

Pass new output directories under the checkout build root:

```sh
python3 scripts/test-windows-display-service.py --output-directory "$POLICY_TEST_OUTPUT"
# After Windows cross-building the actual helpers:
python3 scripts/test-windows-display-service-build.py \
  --helpers "$WINDOWS_HELPERS" --display-helper "$DISPLAY_HELPER_EXE" \
  --output-directory "$NEW_FIXTURE_OUTPUT"
```

The policy suite executes production framing/authorization decisions, including
all 64 fact combinations, malformed nonces, unsupported operations, session zero,
disconnected/foreign consoles and invalid responses. It executes the guardian's
actual cancellation predicate/resize-enable branch with API doubles so stop/death
wins over pending resize. It checks installer ordering
and build/package/export wiring. Cross-build the real service, maintenance,
guardian and Qt helper; the second suite audits x64/static runtime imports and
compiles/extracts a synthetic NSIS fixture. That fixture contains invalid driver
and placeholder client/host data, is never executed and must never be installed or
distributed. A normal `nix build --no-link` checks the Linux consumer. These checks
do not emulate SCM, kernel token enforcement or native display/capture/input.

## Deferred native acceptance

Reserve a Windows 11 x64 target exclusively, with a local recovery operator and
dedicated test account. Record exact source/package hashes, original SCM/task/
device state, test owner, process/session IDs and verified cleanup. Keep Windows
credentials, private clipboard data and work applications out of evidence.

| Gate | Required observation |
| --- | --- |
| Install | One installation authorization; correct LocalSystem/automatic/ImagePath/DACL; identity and peers retained. |
| Cold boot | Broker idle/running without user login, GUI or display lease. This is service startup, not pre-login hosting. |
| Signed-in sharing | Non-elevated console user; 20 start/stop/resize cycles without UAC; real video/input and verified restoration. |
| Denials | Foreign executable/logon/session, anonymous, low IL, malformed/oversized requests, pipe precreation and unauthorized service control cause no device mutation. |
| Recovery | Owned helper/broker crash, SCM stop during preparation/resize, disconnect and console change leave no stuck input, orphan guardian or wrong-display fallback. |
| Lifecycle | Upgrade/rollback/uninstall/reinstall retain identity, preserve foreign services/tasks and wait for recovery before payload replacement. |
| Missing service | A sharing request fails once with a repair message, without UAC or a restart loop. Restore only test-owned state. |
| Later desktop work | Separately implement and accept existing-session lock screen, credential desktop, UAC secure desktop and fresh pre-login hosting. |

The final row is unsupported in this phase. The old probe only opened/closed a
desktop handle. Neither that, WGC window permission, an elevated process, a running
boot service nor a screenshot proves secure-desktop video, credential input or
pre-login hosting. BitLocker pre-boot authentication, automatic login, weakened
UAC/desktop permissions and software-SAS policy changes are excluded.

References: [interactive services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services),
[CreateProcessAsUser](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw),
[console-user tokens](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken),
[desktop duplication](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput).
