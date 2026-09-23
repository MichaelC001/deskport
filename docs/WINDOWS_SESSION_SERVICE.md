# Windows session-service boundary probe

This is development infrastructure, not a secure-desktop hosting feature. It is
deliberately excluded from the installer and does not start Sunshine, parse host
configuration, inject input, or accept executable paths or command lines.

An elevated interactive user on the hardware target can open `Default` but gets
access denied opening `Winlogon`. The probe verifies a possible service boundary
without changing authentication, UAC, endpoint protection, or desktop permissions.

## Implemented boundary

`host/windows/session-service-probe.cpp` implements a manual LocalSystem service
named `DeskPortSessionProbe`. Its local-only named pipe accepts one fixed 16-byte
request. It checks the kernel-reported client PID, executable path, impersonated
user SID, process SID, active console session and console-user SID. Failed
impersonation is always a denial. The client authenticates the server against
the administrator-controlled SCM configuration and running service PID.

The endpoint permits medium-integrity authenticated clients to exchange data and
attributes, but omits `FILE_CREATE_PIPE_INSTANCE`, DACL modification and ownership
rights. Its medium integrity label applies only to this IPC object. The service
and worker remain SYSTEM. First-instance creation rejects a pre-existing pipe;
remote pipe clients and low-integrity callers are excluded.

The only accepted operation launches the same fixed executable as a short-lived
SYSTEM worker in the authorized console session. The worker opens and immediately
closes `Winlogon`, returning its result. It receives no client-selected path,
environment, desktop name or command. A private kill-on-close job and five-second
deadline bound its lifetime. Pipe I/O is cancellable and bounded; an acknowledgement
avoids blocking `FlushFileBuffers` or discarding an unread response at disconnect.

## Build and native verification

Build on the Linux cross-build host using the existing pinned toolchain:

```sh
nix-shell winbuild/scripts/shell.nix --run 'bash winbuild/scripts/build-session-service-probe.sh'
```

Copy the resulting `winbuild/full/deskport-session-probe.exe` into the
administrator-owned `C:\Program Files\DeskPort\host` directory on a dedicated
development machine. The directory and its ancestors must not be writable by
ordinary users. Run the following elevated as the active console user; the output
directory must be new and writable by that user's non-elevated token:

```powershell
.\tests\windows-session-service.ps1 `
  -Executable 'C:\Program Files\DeskPort\host\deskport-session-probe.exe' `
  -OutputDirectory "$env:LOCALAPPDATA\DeskPort\session-probe-run"
```

The harness refuses to replace an existing service, creates a temporary limited
interactive task, and removes both task and service in `finally`. Remove the
copied probe executable after verification. Never substitute a user-writable
binary or configure this probe as an automatic startup service.

On 2026-09-23 the hardware run passed thirteen checks: console-user success,
interactive completion, SSH/session-zero rejection, invalid operation rejection,
anonymous rejection, direct unprivileged worker rejection, unknown CLI rejection,
low-integrity rejection, foreign executable rejection, silent request timeout,
stop during a silent request, pre-existing pipe rejection, and rejection after
service restart. Stopping with a silent client completed in 3 ms. The PE imports
only Windows system libraries. These observations do not prove video capture,
secure-desktop input, password entry, or unattended operation.

## Before production integration

The next implementation must keep the GUI and its binding state unprivileged.
Do not run an arbitrary user-editable Sunshine configuration as SYSTEM. Define
typed, bounded operations and generate a protected configuration for the fixed
bundled host with only the built-in desktop application. Preserve pairing identity
without creating privileged file-write paths through user-controlled files or
reparse points. Define active-session changes, caller death, host restart and
display-lease ownership before connecting the broker to `HostManager`.

The probe accepts only its own executable as a client. Production must explicitly
validate the installed DeskPort client and administrator-owned installation, scope
pipe access to the intended logon/session, and integrate owned registration,
upgrade, stop and uninstall handling. Locked-session hosting and pre-login hosting
are separate acceptance cases; neither follows from an open desktop handle.

References: [named-pipe access rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights),
[mandatory integrity control](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control),
[console user tokens](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken).
