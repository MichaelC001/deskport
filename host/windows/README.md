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

The current display helper implementation and full installer are incomplete
candidates. In particular, supported-mode selection, topology policy, crash
recovery, driver-store cleanup, and live streaming acceptance must be completed
and verified before claiming release readiness. See the task acceptance report;
binary compilation alone does not establish any of these capabilities.

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
