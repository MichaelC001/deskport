# SSH administration and Omarchy hosts

DeskPort's Linux CLI lets you inspect prerequisites, administer the host, and
prepare a user service from SSH. A running desktop, a usable capture backend,
and an encoder are still required to stream pixels. A bare VPS with only SSH
does not become a remote desktop merely by installing the CLI.

The commands below run as the desktop's ordinary user. Do not use `sudo
deskport` or copy another user's credentials. CLI administration does not
require a Qt display connection. `host run` selects Qt's offscreen platform for
the host supervisor while preserving the desktop environment for its helpers.

## Run and administer the host

Keep the host in the foreground of a terminal for an initial test:

```sh
deskport host run
```

Use `deskport host run --no-share` to start administration without enabling
sharing. From another terminal or SSH connection as the same user, use:

```sh
deskport status --json
deskport sharing start
deskport sharing stop
deskport devices list --json
deskport devices pending --json
deskport devices approve EXACT_REQUEST_ID
deskport devices reject EXACT_REQUEST_ID
deskport devices remove DEVICE_ID
deskport config get --json
deskport config set name "My remote desktop"
deskport config set port 48999
```

Approve only a pending request you recognize; copy its exact `requestId` from
`devices pending`. Removal uses the exact `id` from `devices list`, including a
full certificate fingerprint for a client that has no host ID. Certificates,
private keys, and secrets are not returned in the device list.

These commands connect to the running GUI or `host run` process through a
private local socket for the same user and configuration. They do not create a
new host identity or expose a TCP administration API. If neither process is
running, start the host first. An SSH tunnel is unnecessary for this local
administration socket. `sharing stop` stops screen sharing while keeping the
administration process available.

The generated services request sharing on each service start. To keep sharing
paused across service restarts, use a user-managed unit with `host run --no-share`,
or disable the service. Editing a generated unit makes it user-managed; later
CLI installation preserves those edits.

Stop sharing before changing the host name. Names must have 1–64 characters
with no control characters; the binding port must be in 1024–65535. The binding
port setting is separate from the streaming transport ports. Mutating commands
wait for completion and can take up to about 70 seconds before reporting a
timeout.

Each administration command accepts `--json` and returns one object with
`version: 1`, `ok`, and either `data` on success or `code` and `error` on failure.
Their exit codes are `0` for success, `1` for an unavailable/rejected/failed
operation, and `2` for invalid arguments. The setup diagnostic JSON schema below
is separate because it reports multiple prerequisite checks.

## Inspect the host before changing it

```sh
deskport doctor
deskport doctor --json
deskport service status --json
```

`doctor` checks the current process's session environment, runtime directory,
session D-Bus, PipeWire socket, packaged host/display helpers, Hyprland IPC,
`/dev/uinput` access, and systemd user manager. It neither creates a display nor
injects input. The JSON report has `schemaVersion: 1`, a top-level `status`, and
`checks` with stable `id`, `status`, `message`, and `hint` fields. Check statuses
are `ok`, `degraded`, `unsupported`, or `not_applicable`; additional checks may
be added in future releases. Environment values, screen names, and credentials
are not printed.

Exit codes for setup commands are `0` for success, `1` for degraded prerequisites
or an operational failure, `2` for incorrect arguments, and `3` for an unsupported
platform or missing essential prerequisites. `doctor` deliberately leaves
capture permissions and actual encoding as `degraded`, because it does not
exercise either operation. Verify both separately; this is a prerequisite
report, not a first-frame acceptance test.
The setup commands currently support Linux only.

An SSH shell usually lacks `WAYLAND_DISPLAY`, `XDG_CURRENT_DESKTOP`, and
`HYPRLAND_INSTANCE_SIGNATURE`. Diagnose from a terminal in the active desktop
first. Import its session variables into the user manager from that terminal:

```sh
systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP HYPRLAND_INSTANCE_SIGNATURE
```

For a later SSH login as the same user, an existing systemd session normally
provides these two settings. If absent, point at that user's existing runtime
directory and bus; do not create substitute directories or launch a second bus:

```sh
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
```

Importing variables into systemd does not copy them back into an existing SSH
shell. For `doctor` or foreground `host run`, supply the exact display and
Hyprland instance values from the active desktop. Do not `eval` untrusted
environment output or paste full environment dumps containing secrets.

## Prepare and activate a user service

For an existing graphical desktop:

```sh
deskport service install
systemctl --user cat io.github.keithxc.DeskPort.service
```

Installation writes
`$XDG_CONFIG_HOME/systemd/user/io.github.keithxc.DeskPort.service` (normally under
`~/.config`) and runs `systemctl --user daemon-reload`. The graphical unit runs
`deskport --background --share` and belongs to `graphical-session.target`.
The unit terminates the supervisor first so its display cleanup guard can
remove the owned output before systemd's final timeout.
Installation does not enable or start it. After reviewing the generated unit,
activate it explicitly:

```sh
systemctl --user enable --now io.github.keithxc.DeskPort.service
deskport service status
```

For a supervisor with no DeskPort GUI, but with an already configured desktop
and capture environment:

```sh
deskport service install --headless
systemctl --user cat io.github.keithxc.DeskPort.service
systemctl --user enable --now io.github.keithxc.DeskPort.service
```

This unit runs `deskport host run` and belongs to `default.target`. It does not
install or start a compositor, create a user login, enable lingering, modify
the firewall, change input permissions, or grant capture permissions. If a
compositor starts later, its environment must be imported before starting the
host. User lingering alone does not supply a graphical session. Arrange
compositor readiness and logout behavior in your own service configuration if
your VPS requires a desktop independent of a physical login.

The CLI refuses to overwrite existing manual units, modified CLI units,
symlinks, Nix store-managed startup configuration, or units with manual
drop-ins. A checksum in a generated unit detects edits; continue managing an
edited unit yourself. Use Nix/Home Manager as the source of truth on declarative
systems. An existing service from another package or configuration is not
silently shadowed.

To remove an unmodified CLI-generated unit:

```sh
systemctl --user disable --now io.github.keithxc.DeskPort.service
deskport service uninstall
```

`uninstall` requires the service to be inactive and disabled; it never stops a
running host implicitly. It removes only its own unit and reloads systemd.
If installation reports that the file was written but reload failed, repair
the user session bus and rerun installation or `systemctl --user daemon-reload`.

## Omarchy and Hyprland

Omarchy uses Hyprland. DeskPort integrates with the active compositor through
`hyprctl`, creates a dedicated headless output for the remote workspace, and
uses the host's WLR capture backend. This is an extended remote workspace;
mirroring or disabling physical outputs is not offered by this integration.
The compositor and its capture protocol must be available even if the machine
has no physical monitor. PipeWire and the session audio stack are also needed
for audio. The CLI never edits Omarchy's compositor configuration automatically.

Check the installed Hyprland version and its matching configuration format.
Current upstream documentation uses Lua configuration; older versions use
Hyprland's earlier text configuration. When permission enforcement is enabled,
grant `screencopy` to the exact host process executable. Do not disable permission
enforcement or use an unrestricted binary pattern. A wrapper or symlink named
`deskport-host` may launch a different real executable; inspect the host process's
`/proc/<pid>/exe` before adding a permission rule. Package upgrades can change
that executable path, especially on NixOS.

For versions using the current Lua permission API, substitute the actual host
executable path into the user's existing Hyprland configuration:

```lua
hl.permission({ binary = "/exact/path/to/host-executable", type = "screencopy", mode = "allow" })
```

Older versions with text configuration use a version-specific `permission`
rule instead. Consult the installed version's documentation; do not paste a Lua
rule into an older text configuration. Permission-rule changes require a
Hyprland restart, not just a reload. Schedule that restart yourself because it
ends the desktop session. `doctor` reports capture permission as unverified
until an actual capture attempt demonstrates it.

See [Hyprland permissions](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/advanced-configuration/permissions.md)
and [Omarchy's session configuration](https://github.com/omacom/omarchy/tree/quattro/default/uwsm)
for the upstream configuration details.

## VPS acceptance checklist

Before relying on unattended access, verify the compositor survives the intended
login/logout behavior, the capture helper creates and removes its own output,
the chosen encoder starts, and a real client receives frames, audio, keyboard,
and pointer input. Reconnect after SSH logout and after a machine reboot.
Keep network access on the intended private network or VPN and configure any
required firewall rules explicitly. Neither service installation, an open
control endpoint, nor a passing CLI test establishes those streaming results.

## Isolated verification

Run these from the repository with its locked toolchain:

```sh
nix build
nix develop -c python3 scripts/test-host-lifecycle.py --cli
nix develop -c python3 scripts/test-host-lifecycle.py --service
nix develop -c python3 scripts/test-host-setup.py
nix develop -c python3 scripts/test-hyprland-display.py
python3 scripts/test-host-cli.py result/bin/deskport
```

For a real isolated compositor test, make matching `Hyprland`, `hyprctl`,
`kwin_wayland`, and `dbus-run-session` available in the test environment:

```sh
DESKPORT_TEST_HOST="$PWD/result/libexec/deskport-host" \
  nix develop -c python3 scripts/test-hyprland-display-live.py \
  --helper "$PWD/result/libexec/deskport-display" --lua
```

Omit `--lua` to exercise legacy Hyprland configuration. The fixture creates
private runtime/configuration directories, a separate session bus, and a virtual
parent compositor. Host probes prefer a private user/network namespace and
otherwise bind loopback only. No personal desktop or remote input is used.

On 2026-10-03, Hyprland 0.55.4 passed both configuration formats with
1280×720 at 1×, 1668×2388 at 2×, and 1920×1080 at 1×. Actual WLR capture and
software H.264 encoder probes passed; removing the target rejected physical
display fallback. Output cleanup passed after disconnect, EOF, SIGKILL, and
process-group SIGTERM. This evidence covers the isolated backend, not an
Omarchy installation or client audio/input acceptance.
