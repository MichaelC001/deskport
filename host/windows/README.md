# Windows host integration (in development)

The full package uses a private patched Sunshine executable and configuration,
a per-process Windows job for owned host processes, a separate display helper,
and installer helpers scoped to the installation directory and owned device.
Normal Win32 keyboard and mouse input is provided by the MIT-licensed
libvirtualhid fallback backend. Deskflow is not a dependency of this source
revision. Clipboard transport remains DeskPort's authenticated helper protocol.

The libvirtualhid patch does not distribute or use its proprietary Windows
broker/UMDF driver, import entitlement constants, or connect its normal input
backend to an independently installed broker. It does not unlock licensed HID
features. Win32 input is subject to Windows integrity-level and secure-desktop
restrictions. Gamepads require a separately supported device backend and are
not claimed by this integration.

The virtual-display package is the original signed VirtualDrivers release
24.12.24 x64, distributed under its MIT notice. Device ownership is recorded
by exact PnP instance ID under `HKLM\Software\DeskPort`; adapter discovery verifies
that ID through SetupAPI and requires a unique matching hardware ID before
mapping the GDI output. It does not rely on a friendly name. An
independently installed adapter is not adopted or removed. The upstream vendor
settings namespace and command pipe are shared, so they must not be treated as
private per-device interfaces. No signing-policy, Secure Boot, or certificate
trust changes are permitted by these helpers.

The Windows package remains a private integration candidate. Native display
acceptance and installed streaming acceptance are recorded separately; compilation
and native topology tests alone do not establish full release readiness.

Implementation references: Microsoft's
[SetupCopyOEMInf contract](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupcopyoeminfw)
defines the existing-package result used to distinguish newly staged packages;
[SetupUninstallOEMInf](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupuninstalloeminfw)
without forced deletion protects packages used by other installed devices.
[CCD topology guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/ccd-example-code)
distinguishes virtual clone support from same-adapter hardware clone. Topology
support must be tested against the actual adapters, not inferred from an OS name.

The owned VDD is disabled at rest. Sharing requests normal UAC consent for an
independent recovery lease. It snapshots the active CCD topology to an
administrator-owned recovery directory, then enables only the registered owned
device. EOF, helper termination and Windows session-end notifications restore
the baseline; a separately owned startup task disables a stale owned device after
a reboot. The installer removes that exact task on uninstall. Native helper
normal/failure/forced-exit tests and task create/update/run/remove tests passed in
the VM; actual shutdown/reboot acceptance remains untested. No automatic retry
occurs after display authorization failure.

Windows host processes run from their bundled asset directory. Configuration,
identity, credentials and logs remain explicit absolute paths in private state.
This is necessary because upstream resolves `assets/apps.json` relative to cwd.

On Windows, a GDI mode update can reload unrelated registry modes. The helper
captures the complete active CCD mode set before enabling the owned adapter,
then reapplies the unchanged physical modes together with the new virtual mode
using `SetDisplayConfig` without `SDC_SAVE_TO_DATABASE` or `SDC_ALLOW_CHANGES`.
It verifies the existing source modes after applying the topology and ends the
lease on application failure. The independent guardian still restores the full
pre-sharing topology after helper exit. Native VM startup and 1152x648 mode-change
checks preserved the 3828x2026 primary and restored the disabled VDD baseline;
this is functional VM evidence, not a hardware compatibility or release claim.
See the [SetDisplayConfig contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setdisplayconfig).


## Session modes and dynamic dimensions

The owned signed driver now accepts a negotiated mode through its private,
administrator-owned XML configuration. The guardian accepts only bounded numeric
dimensions over a per-lease event/mapping channel, retains at most one generated
mode, and restarts only the registered owned adapter through short-lived workers.
No vendor-global command pipe, certificate import or signing-policy change is used.
The ordinary UAC lease also covers subsequent resizes in that sharing session.

The helper advertises continuous dimensions for its owned VDD, verifies the exact
applied dimensions, and publishes the current capture output atomically. The host
resolves that name again when capture is reinitialized and fails closed while the
output is unavailable. This prevents a driver restart from silently selecting a
physical desktop. Supported dimensions retain the protocol's bounds and multiple
of four alignment.

Mirror creates a CCD virtual clone group, normalizes its orientation, and selects
the readable shared source. Exclusive keeps only the owned virtual output active.
Extend preserves physical modes and adds the owned output to their right. Session
release restores the idle topology, and the independent guardian restores the
pre-sharing topology and disables the owned adapter on exit or failure.

`tests/windows-display.ps1` covers all three policies, custom dimensions, portrait
switches, successive dynamic resizes, session reuse, forced termination and crash
restart. Run each policy with `-CustomSize -PortraitSwitch -DynamicSwitch
-ReusePolicies` in the interactive elevated test session. The test deliberately
changes displays and is not an unattended CI test.
