# Diagnostics and public feedback

Added 2026-09-21 to collect useful user reports without collecting raw device or
connection details. This feature is enabled by default and applies to the desktop application
and the host/helper processes it starts. It does not change independently managed
Sunshine installations or the private mobile clients.

In **Settings → Diagnostics and feedback**, check that **Enable diagnostic logs** is on,
reproduce the problem, then select **Create logs ZIP and open GitHub…**. This
creates a local ZIP and opens a prefilled issue at `keithxc/deskport`. Review the
archive, drag it into the issue editor, describe the steps and expected behavior,
and submit it yourself. GitHub issues and their attachments are **public**.
DeskPort never uploads the archive, places its contents in a URL, submits an issue,
or claims that the attachment has already been added. If the browser cannot open,
the saved archive remains available and the UI shows manual instructions.

## What is recorded

The shared Qt sink projects messages into an explicitly allowed schema before any
application-owned diagnostic file is written. This deliberately sacrifices raw
error details instead of trying to recognize every possible secret with regular
expressions. It records:

- Source (`client`, embedded `host`, or native `display` helper).
- A fixed event class: connection, start/stop, failure, timeout, permission,
  encoder/decoder, capture, recovery, display, input initialization, or resize.
- A random run identifier, stable for that application run, unrelated to the
  device name or account. It correlates child-process and client events in that
  run without a persistent machine identifier.
- Numeric connection phase and error codes with starting, failed or terminated
  state, taken directly from connection callbacks.
- Relative milliseconds. Recognized resize traces also include their allowed
  stage name, monotonic tick and numeric width/height, including input-init and
  first-render milestones.
- Export metadata: numeric DeskPort version (or `development`), OS family,
  diagnostic switch state, schema revision and number of diagnostic files.

No raw message text, IP addresses (including IPv6), domains, device/host names,
URLs, query strings, authentication fields, usernames or home paths are copied
into records. Messages mentioning private input, clipboard, passwords, tokens or
keys are discarded; unknown message formats are omitted. There are no key text,
clipboard content, screen/image, audio, configuration or pairing-state attachments.
This is restricted diagnostic telemetry, not a complete debug transcript. It
cannot explain every failure, and changes to event producers need privacy review.
Review attachments and your own issue text before publishing; this is not a claim
that a general-purpose filter can recognize every possible sensitive datum.

## Storage and process coverage

Logs are enabled by default when no preference has been saved. Existing explicit
off choices remain respected. The switch persists as `diagnostics/enabled`, separately
from per-device streaming settings, and takes effect immediately. Qt, SDL and
FFmpeg callbacks use the shared sink. The parent always drains the embedded host's
stdout/stderr and the native display helper's stderr; it drops data while disabled.
Display stdout and clipboard-helper stdout remain private in-memory control
protocols and are never sent to the log sink. Credential helper stdout/stderr and
clipboard-helper stderr go to the null device. The macOS capture adapter uses the
parent's stderr pipe instead of `NSLog`. The host's own file sink and rotation are
disabled when configured with the null-device path. Application-created Windows
memory dumps are disabled because memory can contain private content.

Raw child output exists transiently in process pipes. A bounded 4 KiB in-memory
host-status buffer recognizes fixed permission/encoder failure messages even when
logging is off. The toggle does not disable OS crash reporting, third-party system
logging, manually redirected CLI output, or operational display recovery state.
Those sources are not collected or exported.

Diagnostic storage is the application's local data directory plus `/diagnostics`.
Each of three sources has at most three 1 MiB JSONL files (9 MiB total). Partial
lines are capped at 16 KiB and oversized messages discarded. Files older than
seven days are pruned at startup, during writes/exports and hourly while running.
Only one generated ZIP is retained; an explicit subsequent export replaces it,
and it is also pruned after seven days. Turning logging off stops new records but
keeps existing data for feedback. **Clear saved diagnostics** removes these files
and the generated ZIP. Old versions' raw logs are left untouched and never exported.

Export reads only the nine exact JSONL filenames, skips symlinks and oversized
files, parses every record again and reserializes only approved fields/values.
It never recursively scans the configuration or host directory. The ZIP contains
only accepted records, `manifest.json` and `README.txt`; when no records exist it
contains metadata and instructions only. The directory and files use owner-only
permissions where the OS supports them. This protects normal local operation; it
is not a security boundary against another process already controlling the same
user account.

## Verification

- `nix develop -c python3 scripts/test-diagnostics.py`: privacy fixtures, default
  off, live toggling, partial lines, stable timing fields, rotation, retention,
  symlinks, injected fields, export failures and no-upload URL interception;
  Python's independent ZIP reader verifies CRCs and the archive allowlist.
- `nix develop -c python3 scripts/test-host-lifecycle.py`: supervised child
  processes, null-device configuration, no raw log files and toggling without
  restarting sharing, alongside existing lifecycle cases.
- `nix develop -c python3 scripts/test-host-lifecycle.py --ui`: actual QML switch
  clicks, local archive generation and intercepted URL handling; synthetic light,
  dark and narrow settings screenshots. No personal hosts or private screens.
- `python3 scripts/test-translations.py`: seven existing UI language catalogs.

Development builds and these isolated checks do not constitute live-stream,
Windows runtime or mobile acceptance. Deployment and package publishing remain
separate actions.

### Development verification — 2026-09-21

Implemented on the input/startup optimization baseline `fc7ccee5`; shared core
remains `0badc8e3`. No release, installation or deployed service change was made.

| Check | Result |
| --- | --- |
| macOS arm64 viewer and embedded host compilation | Passed with the locked Nix devShell and installed Apple SDK |
| Built macOS viewer, isolated portable `--version` | Passed; no legacy log files or pre-application timer warning |
| NixOS x86_64 `nix build path:.` | Passed with the repository's pinned host and core |
| Privacy / JSONL / ZIP tests | 25 passed on each platform, plus independent ZIP CRC/schema verification |
| Host lifecycle | macOS: 30 passed, 2 Linux-only cases skipped; Linux: 32 passed |
| QML UI regression | 23 passed on each platform, including diagnostic switch and intercepted issue URL |
| Localization | Seven primary-page catalogs passed; regenerated QM resources; Chinese status messages added |
| Native host file-sink overlay | Both vendored revisions patched successfully and passed idempotence checks |

Screenshots were rendered from synthetic QML fixtures in light/dark and narrow
layouts. UI URL handlers intercepted the feedback action; no actual issue was
created. Windows compilation/runtime and live remote-session acceptance were not
performed in this development task.

## Private 0.4.6-D build (2026-09-21)

The user-requested private test version is `0.4.6-D`. The application and
diagnostics manifest preserve this complete version; Apple's bundle version
fields use `0.4.6` and `DeskPortDisplayVersion` retains the test suffix.
The diagnostic version allowlist accepts bounded prerelease identifiers and
rejects paths, hostnames, trailing newlines and incomplete suffixes. macOS
regression: 32 checks plus independent ZIP CRC/metadata validation passed.
This is packaging validation, not deployed streaming or input acceptance.
