# Session lifecycle development — 2026-09-20

This change is implemented locally and is not a public release. On 2026-09-20,
the user separately authorized direct installation on the designated Mac and
Linux machines for live acceptance; both candidates were activated with rollback
records. See [the local update route](LOCAL_UPDATE.md). The wire contract is in the pinned shared core's
`protocol/SESSION_LIFECYCLE.md`.

## Behavior

- Desktop cards expose Details, Actions and Settings. Active-session actions
  include disconnect, reconnect and fullscreen.
- The native Ctrl+Alt+Shift+Q shortcut leaves fullscreen first. Pressing it while
  windowed disconnects. Ctrl+Alt+Shift+X still toggles fullscreen. The Sharing page
  can request that the admitted desktop client leave fullscreen without stopping
  its stream. This requires negotiated capability and an authenticated session.
- macOS and Linux helpers start without a virtual display. Display admission
  creates it; explicit release restores the physical layout and removes it.
  Capture and pointer routing resolve the current virtual display rather than
  retaining an obsolete identifier. Capture fails if the admitted target is absent.
- Recoverable desktop transport loss retains host ownership for 15 seconds.
  The same certificate and unguessable in-memory lease token can resume the
  control channel, including when the old connection has not observed EOF.
  The viewer retries at most five times within 30 seconds with 1/2/4-second
  backoff and a cancel action. Admission loss, intentional disconnect, revocation
  and takeover are terminal. Recovery never automatically takes over another
  device. Older clients keep their existing disconnect behavior.
- Local layout failure is surfaced as a warning while verified independent
  capture can continue. The original topology journal remains available for
  restoration. This does not guarantee that every physical monitor accepts a
  requested mode.
- Supported desktop host source and required submodules are checked-in archives
  with origin/version/hash manifests and licenses. See [vendoring](VENDORED.md).

## Verified development checks

| Check | Observed result |
| --- | --- |
| macOS desktop and patched Sunshine host | Native builds passed |
| Linux x86_64 | Final Nix build passed on the user-selected host; all three isolated KWin policies passed, each with 12 resizes, three actual capture/encoder probes, disabled-output preservation and EOF/crash restoration |
| Authenticated binding/session regression | 62 passed, including owner recovery before/after EOF, wrong token/identity, release and fullscreen |
| Host supervision | macOS: 29 passed, 2 Linux cases skipped; Linux: all 31 passed |
| Desktop QML pages | 20 passed, including cancellation before a delayed retry |
| Shared core, desktop state and navigation | Passed; desktop state has 9 cases |
| macOS topology adapters | Sanitizer checks passed for all three policies, disabled/hotplugged displays, journal/fault recovery |
| macOS native lifecycle | Startup created no display; three create/remove cycles restored original display membership |
| iPad simulator | Three UI tests passed: card wrapping, per-device tuning and display policy persistence |
| Android emulator | Debug build/install, device cards/settings and landscape layout checked |
| Existing mobile protocol adapters | Apple workspace and Android display/TLS checks passed |
| Translations | Seven source and compiled catalogs updated; placeholder/coverage checks passed |

Linux development builds and isolated KWin capture/layout runs are recorded in the
active [TODO](../todo.md). These tests use a private compositor, bus and state
directory; they do not reconfigure the installed desktop session.

## Remaining acceptance and delivery

User acceptance after local activation remains pending: actual video/input
recovery after a Wi-Fi or VPN route change, two-device
fullscreen control, physical macOS mirror/disable behavior, and long sessions
remain live acceptance items. The iPad and Android checks above are simulator
checks, not a complete physical streaming matrix. GNOME on-demand behavior still
needs a Mutter runtime check. A macOS VM run was not started because the available
disk space did not meet the test runner's 80 GiB requirement.

The desktop changes negotiate optional capabilities with existing mobile clients.
Mobile automatic reconnect and mobile upstream-source vendoring are separate
backlog items. Standalone clients without DeskPort display admission cannot create
a workspace. Deferred encoder probing means first discovery conservatively
advertises baseline codec support until the host has probed an admitted display.

The new core revision is currently a local commit. Local validation uses
`--override-input deskport-core path:/absolute/path/to/deskport-core
--no-write-lock-file`. Publish that core revision before distributing desktop
references to its GitHub pin. Release packaging, signing/notarization, publication
and installed-host activation are separate from the checks above.

## First live admission correction

The initial local candidate returned HTTP 503 during encoder probing: a macOS
process started before creation of the virtual display could enumerate its active
ID and logical bounds but `CGDisplayCopyDisplayMode` and the mode list remained
empty. A fresh process could read the backing mode. Run-loop polling and a display
reconfiguration callback did not resolve the reproduced cache behavior.

The display owner now atomically publishes ID, verified backing width/height and
scale before admission completes. Capture and input validate the live ID and
logical bounds against that descriptor. The generic device-library mode preflight
is skipped only for this owned-target path; actual capture still fails closed
without a valid target. Both ScreenCaptureKit and AVFoundation initialization use
the verified dimensions, and input retains the correct HiDPI factor.

`scripts/test-macos-admitted-display.py --native HELPER` starts its consumer before
the display exists, then checks 3824x2000@2 and 2560x1440@2 across create/remove
cycles, including rejection of absent descriptors, mismatched bounds and removed
displays. This regression passed; paired live streaming is a separate checkpoint.

The corrected Developer ID candidate was notarized and installed at the existing
macOS app path. A paired Linux desktop host using its existing client certificate
admitted 3824x2000@2, passed H.264 and 8-bit HEVC capture/encoder probing, and
received `/launch` HTTP protocol status 200 with `gamesession=1`. The diagnostic
released its lease afterward. The optional 10-bit AVFoundation probe timed out;
this does not establish HDR support. Full client video and input acceptance
remains pending.
