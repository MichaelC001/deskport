#!/usr/bin/env python3
"""Exercise the packaged CLI and foreground owner in a disposable Linux profile.

No sharing, saved peers, session connections or input injection is performed.
The binding listener uses an unused high port; the local control socket is
derived from this fixture's private configuration directory.
"""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="deskport-cli-") as temporary:
    work = Path(temporary)
    environment = dict(os.environ)
    for variable in ("DISPLAY", "WAYLAND_DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE",
                     "DBUS_SESSION_BUS_ADDRESS", "XDG_CURRENT_DESKTOP"):
        environment.pop(variable, None)
    for variable, folder in (("HOME", "home"), ("XDG_CONFIG_HOME", "config"),
                             ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"),
                             ("XDG_RUNTIME_DIR", "runtime")):
        path = work / folder
        path.mkdir(mode=0o700)
        environment[variable] = str(path)
    # Every short command must work even with an intentionally invalid GUI
    # platform; host run explicitly chooses its own offscreen platform.
    environment["QT_QPA_PLATFORM"] = "deskport-test-no-gui-platform"

    def command(*arguments, code=0):
        result = subprocess.run([binary, *arguments], cwd=work, env=environment,
                                text=True, capture_output=True, timeout=15)
        assert result.returncode == code, (arguments, result.returncode, result.stdout, result.stderr)
        return result.stdout

    command("--version")
    assert "host run" in command("--help")
    command("host", "run", "--help")
    for verb in ("status", "sharing", "devices", "config", "doctor", "service"):
        command(verb, "--help")
    assert not json.loads(command("status", "--json", code=1))["ok"]
    assert not list((work / "config").rglob("*.conf")), "Read-only commands created settings"
    assert not list((work / "data").rglob("*.pem")), "Read-only commands created credentials"
    config = work / "config/DeskPort"
    config.mkdir(exist_ok=True)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        binding_port = reservation.getsockname()[1]
    (config / "DeskPort.conf").write_text(f"[binding]\nport={binding_port}\n")
    with (work / "daemon.log").open("w+") as log:
        owner = subprocess.Popen([binary, "host", "run", "--no-share"], cwd=work,
                                 env=environment, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 15
            while True:
                result = subprocess.run([binary, "status", "--json"], cwd=work,
                                        env=environment, capture_output=True, text=True, timeout=4)
                if result.returncode == 0:
                    state = json.loads(result.stdout)["data"]
                    break
                assert owner.poll() is None, "Foreground host exited before management became ready"
                assert time.monotonic() < deadline, "Management endpoint did not become ready"
                time.sleep(0.1)
            assert not state["running"] and not state["ready"]
            assert state["port"] == binding_port
            command("host", "run", "--no-share", code=3)
            assert json.loads(command("config", "set", "name", "CLI fixture", "--json"))["ok"]
            assert json.loads(command("config", "get", "--json"))["data"]["name"] == "CLI fixture"
            assert json.loads(command("devices", "list", "--json"))["data"]["devices"] == []
            assert not json.loads(command("devices", "pending", "--json"))["data"]["requestId"]
            assert not json.loads(command("devices", "approve", "stale", "--json", code=1))["ok"]
            command("config", "set", "port", "22", "--json", code=2)
            command("sharing", "stop", "--json")
            owner.send_signal(signal.SIGTERM)
            assert owner.wait(timeout=12) == 0
            command("status", "--json", code=1)
        finally:
            if owner.poll() is None:
                owner.terminate()
                try:
                    owner.wait(timeout=12)
                except subprocess.TimeoutExpired:
                    owner.kill()
                    owner.wait()
            if sys.exc_info()[0]:
                log.seek(0)
                print(log.read(), file=sys.stderr)
print("PASS: packaged CLI works without a display; isolated daemon, config, ownership, stale approval and SIGTERM")
