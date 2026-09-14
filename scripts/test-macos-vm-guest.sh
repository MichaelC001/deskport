#!/bin/bash
# Run ONLY inside the disposable Tart guest, via macos-vm.py exec.
set -euo pipefail
input='/Volumes/My Shared Files/input'
results='/Volumes/My Shared Files/results'
[[ -f "$input/DESKPORT_VM_TEST_ONLY" ]] || { echo 'Missing guest-only marker'; exit 1; }
# A hypervisor and VirtioFS shared marker are required: refuse the development host.
[[ $(/usr/sbin/sysctl -n kern.hv_vmm_present) == 1 ]]
/sbin/mount | /usr/bin/grep -F ' on /Volumes/My Shared Files ' | /usr/bin/grep -iq virtiofs
/bin/rm -f "$results/STATUS.txt" "$results/desktop.png"
exec > >(tee "$results/latest.log") 2>&1
printf 'Guest OS: '; sw_vers -productVersion
printf 'Architecture: '; uname -m
[[ $(uname -m) == arm64 ]]
# Guest agent uses Virtio sockets, so no guest IP network is needed for these tests.
# Disable guest Ethernet before launching discovery-capable software.
sudo -n /sbin/ifconfig en0 down
if /sbin/ifconfig en0 | /usr/bin/head -1 | /usr/bin/grep -q '<UP,'; then
    echo 'FAIL: guest network remained enabled'; exit 1
fi
app='/Applications/DeskPort.app'
[[ ! -e "$app" ]] || { echo 'Guest already contains DeskPort; run cleanup first'; exit 1; }
cleanup() {
    # Only the disposable guest test application is managed here.
    /usr/bin/pkill -x DeskPort 2>/dev/null || true
    sleep 1
    /bin/rm -rf "$app" "$HOME/deskport-vm-test"
    # Remove only crash reports created by this disposable application.
    /usr/bin/find "$HOME/Library/Logs/DiagnosticReports" -maxdepth 1 -type f -name 'DeskPort*' -delete 2>/dev/null || true
}
trap cleanup EXIT
cd "$input"
/usr/bin/shasum -a 256 -c package.sha256
mkdir -p "$HOME/deskport-vm-test/config" "$HOME/deskport-vm-test/data" "$HOME/deskport-vm-test/cache"
export XDG_CONFIG_HOME="$HOME/deskport-vm-test/config"
export XDG_DATA_HOME="$HOME/deskport-vm-test/data"
export XDG_CACHE_HOME="$HOME/deskport-vm-test/cache"
/usr/bin/unzip -q DeskPort.zip -d /Applications
/usr/bin/codesign --verify --deep --strict "$app"
/usr/bin/codesign --verify --strict -R '=identifier "io.github.keithxc.DeskPort" and anchor apple generic and certificate leaf[subject.OU] = "NP7DKCZ56N"' "$app"
echo 'PASS: clean guest installation and Developer ID signature'
sudo -n /usr/sbin/spctl --global-enable
[[ $(/usr/sbin/spctl --status) == "assessments enabled" ]]
/usr/sbin/spctl --assess --type execute --verbose=2 "$app" > "$results/gatekeeper.txt" 2>&1
echo 'PASS: offline guest Gatekeeper assessment'
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")
expected=$(cat "$input/expected-version.txt")
[[ "$version" == "$expected" ]]
"$app/Contents/MacOS/DeskPort" --version > "$results/version.txt" 2>&1
/usr/bin/grep -Fq "$expected" "$results/version.txt"
echo "PASS: package and executable version $expected"
"$app/Contents/MacOS/DeskPort" --help > "$results/help.txt" 2>&1
/usr/bin/grep -q 'Usage:' "$results/help.txt"
echo 'PASS: executable CLI help'
for cycle in 1 2 3; do
    /usr/bin/open -a "$app" \
        --env "XDG_CONFIG_HOME=$XDG_CONFIG_HOME" --env "XDG_DATA_HOME=$XDG_DATA_HOME" \
        --env "XDG_CACHE_HOME=$XDG_CACHE_HOME" \
        --stdout "$results/gui-$cycle.log" --stderr "$results/gui-$cycle.log" \
        --args --no-host-autostart
    sleep 8
    count=$(/usr/bin/pgrep -x DeskPort | /usr/bin/wc -l | /usr/bin/tr -d ' ')
    [[ "$count" == '1' ]]
    pid=$(/usr/bin/pgrep -x DeskPort)
    "$app/Contents/MacOS/DeskPort" --no-host-autostart > "$results/duplicate-$cycle.log" 2>&1 &
    duplicate=$!
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        /bin/kill -0 "$duplicate" 2>/dev/null || break
        sleep 1
    done
    if /bin/kill -0 "$duplicate" 2>/dev/null; then
        echo 'FAIL: duplicate executable did not hand off and exit'; exit 1
    fi
    wait "$duplicate"
    [[ $(/usr/bin/pgrep -x DeskPort) == "$pid" ]]
    "$input/window-check" > "$results/window-$cycle.json"
    echo "PASS: GUI launch, visible window and duplicate activation cycle $cycle"
    if [[ "$cycle" == 1 ]]; then
        if /usr/sbin/screencapture -x "$results/desktop.png"; then
            echo 'CAPTURED: guest desktop (requires visual review)'
        else
            echo 'UNVERIFIED: screenshot permission unavailable'
        fi
    fi
    /bin/kill -KILL "$pid"
    sleep 2
    ! /usr/bin/pgrep -x DeskPort >/dev/null
    echo "PASS: guest-only process termination cycle $cycle"
done
echo 'PASS: three cold GUI starts, duplicate activations and crash/restart cycles'
echo 'Not tested: live streaming, GPU codec performance, capture/input permissions or physical devices.'
cleanup
trap - EXIT
[[ ! -e "$app" ]]
echo 'PASS: test installation removed'
printf 'PASS\n' > "$results/STATUS.txt"
# Tart stop powers off the guest; persist cleanup and shared reports first.
/bin/sync
