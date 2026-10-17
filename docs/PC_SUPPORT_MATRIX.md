# PC implementation and acceptance matrix

Reviewed 2026-10-17 against desktop source
`a8174c968eff7de7b962396279720141063da193` and pinned core
`e1850b07c072f283669a64ec8f312ffc73ba1219`. This is a source snapshot,
not the version installed on every computer. Main advanced from `b0c28182` to
`a8174c96` with a documentation-only takeover checkpoint; application code and
the core pin are unchanged across that update. The public 0.7.2 release predates
the latest Windows display broker/DPI changes. A version label alone cannot
identify those private changes.

This page is the current PC scope index. Dated release notes and roadmap entries
retain their original environment and result. A later implementation supersedes
an old limitation without retroactively completing its acceptance checklist.

## Reading the matrix

**Implemented** means a production path exists at the source snapshot. It does
not mean every package, OS, GPU, compositor or client combination passed.
**Pending acceptance** means the named observation has not been established for
the current exact package; it is not a finding that code is missing.
**Unsupported** identifies an explicit product/backend boundary. **Deferred**
identifies work intentionally outside the current alignment commitment.

Evidence below distinguishes physical hardware, isolated compositors, VM/fixture
results and hosted CI. Recorded passes quote the scope of existing repository
checkpoints; this documentation audit did not rerun their hardware experiments.
An unknown SHA or package hash is stated explicitly and limits reuse. No row
promotes a historical partial pass into current end-to-end acceptance.

## Native PC capabilities and remaining gates

| Feature | macOS arm64 | Linux x86_64 | Windows x64 | Remaining acceptance / explicit boundary |
| --- | --- | --- | --- | --- |
| Viewer, host and mutual binding | Implemented in full package | Implemented in native/Nix/AppImage host packages; Flatpak viewer only | Implemented in full installation; portable viewer does not install the sharing broker | Test exact installed endpoints in both directions; binding success is not video/input success. Client-only binding remains an explicit mode. See [binding](PEER_BINDING.md). |
| Adaptive workspace and physical layout | Owned virtual display; exclusive/mirror/extended policy adapters | KWin virtual-primary/policies; Mutter and Hyprland extended owned output | Owned VDD; exclusive/mirror/extended policies | Real hardware, compositor/version, hotplug, multi-monitor restoration and client resize/input mapping remain combination-specific. See [display policies](SESSION_DISPLAY.md) and [Linux backends](LINUX_ADAPTIVE_DISPLAY.md). |
| Workspace scale / DPI | Native 1x/2x backing | Native 1x/2x when compositor accepts | **Implemented and partially accepted:** owned VDD 100%/200%, observed window DPI and recovery; mirror preserves physical DPI | [windows-dpi](#evidence-windows-dpi) covers a private package, including old-client 1x fallback. Do not reopen DPI as missing code or apply it to physical outputs. |
| Background transport, hide/recall and input release | Implemented | Implemented; Wayland shortcut interception depends on compositor | Implemented | Hidden viewers release input; presented/background audio and clipboard ownership depend on session policy. Route-change recovery, sleep/wake, long sessions and real key/button release need exact-package checks. See [desktop UX](DESKTOP_UX.md) and [lifecycle](SESSION_LIFECYCLE.md). |
| Login startup and service lifetime | Opt-in per-user supervisor at graphical login | User service/autostart or foreground CLI in a suitable graphical session | HKCU login startup; full installer owns automatic SYSTEM **display broker**; GUI/host stay ordinary user | [windows-broker](#evidence-windows-broker) and [windows-dpi](#evidence-windows-dpi) record signed-in sharing without repeated UAC. Cold boot, logout/session change and full upgrade/rollback/uninstall matrix remain pending for current packages. Service auto-start is implemented; reboot acceptance is separate. |
| Pre-login, lock screen, credential/UAC desktop | No FileVault unlock or pre-login promise | No universal greeter/login-screen service; session capture permissions remain necessary | Old lock-screen video is a partial historical result; current signed-in broker does not supply a pre-login network host | [windows-old-lock-video](#evidence-windows-old-lock-video) is not current secure-desktop/input acceptance. Current locked-session transitions, credential input and pre-login hosting remain with the Windows lifecycle work; no service rewrite is authorized by this matrix. |
| Keyboard, IME and caret | Physical key routing and host IME; native caret adapter | Physical key routing and host IME; AT-SPI caret adapter | Physical key routing and host IME; UIA/Win32 caret adapter | Native viewer stops SDL text input; local IME preedit forwarding is deferred. Caret geometry, composed text, shortcuts and mixed DPI require native editors/terminals/browser/IME tests; synthetic key delivery alone is insufficient. |
| Audio | Capture/session receive and host-playback controls implemented; OS permissions apply | Capture/session controls implemented; session audio stack required | Capture/session controls implemented; device/driver routing required | Host configuration enables audio capability; per-device session choice controls receiving sound. Do not reuse the old globally-disabled default. Actual output device, mute/focus behavior, playback quality, reconnect and A/V sync remain hardware acceptance. |
| Desktop clipboard | Native lazy pasteboard text/PNG/files | Wayland data-control/Qt providers; compositor capability required | Native OLE delayed provider and pipe transport | [clipboard](#evidence-clipboard) contains isolated and historical VM checks. Current three-platform clipboard matrix needs synthetic bilingual text/images/file bytes, inactive app, cancellation/reconnect and large-paste timing. No initial clipboard push; no permission bypass. See [limits](CLIPBOARD.md). |
| HDR / 10-bit | Managed virtual capture supports 8-bit H.264/HEVC; managed HDR unavailable | Inherited codec/HDR paths depend on capture, encoder, decoder and output | Inherited codec/HDR paths depend on capture, encoder, decoder and output | A setting, encoder flag or build is not HDR acceptance. No complete current PC HDR/color pipeline matrix is accepted. Browser is H.264 SDR. See [encoding audit](ENCODING_AUDIT.md). |
| Packaging and trust | Developer ID/notarization/stapling/extracted-ZIP Gatekeeper required | Nix/native/portable/AppImage; distro/session runtime requirements; Flatpak viewer only | Full offline Setup and portable viewer; exact-sample normal-protection checks required | Published package verification and a later source/private package are separate. New Windows samples still need protection-enabled checks; old detection clearance is sample-specific. No activation is implied. |
| Seamless | Deferred | Deferred | Deferred | Excluded from current PC alignment and acceptance commitments; no Seamless development, experiment or live test in this work. |

The later [installed iPad takeover checkpoint](#evidence-ipad-takeover) records
Linux/Windows video, synthetic text and ordinary reconnect. Its incomplete
installed-version provenance and the unexercised slow-restoration budget mismatch
remain explicit; it does not complete new-main or all-PC acceptance.

## Browser access is a separate client

All three full desktop host builds contain the browser gateway/media path;
Flatpak does not host. The browser implements authenticated HTTPS pairing,
H.264 SDR/Opus, adaptive size, one controller, physical keys and explicit composed
text/paste. This is not automatic bidirectional native clipboard sync or native
local-IME preedit forwarding. File transfer, HDR, HEVC/AV1 and gamepads remain
unsupported in the browser client. GNOME/Hyprland supply an extended display;
they do not gain physical mirroring from a browser option.

[browser](#evidence-browser) records Chrome fixture capture/audio and persistence;
[mac-browser](#evidence-mac-browser) records a partial macOS host checkpoint.
Windows host browser media/input, installed Linux compositor restoration, physical
Safari certificate/touch/audio and real IME/latency combinations remain pending.
The unsuccessful WebKitGTK setup completed no product assertions. See
[browser controls and validation](BROWSER_ACCESS.md).

## CI scope

The three baseline runs below were read through GitHub's run **and job** APIs on
2026-10-17. The workflow's name is not its execution platform. Linux build and
Windows source checks ran on `ubuntu-24.04`; display contracts ran on `macos-15`.
There is no native Windows runner, physical Linux/Windows GPU or personal-host
input job. The macOS job runs contracts, not distribution signing or physical UI.

`python3 .github/scripts/check-pc-support.py` checks evidence provenance, local references,
workflow/job/runner inventory and this generated evidence section. It runs in
Linux CI before the build and needs only Python's standard library. It never
launches a compositor, VM, installed application or remote connection.

The local [audit verification](PC_VERIFICATION_2026-10-17.md) records this task's
Linux build/CLI result and an isolated long-temporary-path setup failure. It does
not extend the historical GitHub runs or establish physical streaming acceptance.

## Evidence register

The machine-readable source is [pc-support-evidence.json](pc-support-evidence.json).
Regenerate this section with `python3 .github/scripts/check-pc-support.py --write` after
reviewing evidence. Do not fill unknown provenance with the current main SHA.
Keep historical CI rows on their tested SHA when advancing the implementation
snapshot; append new exact-run results instead of relabeling old passes.

<!-- pc-support-evidence:start -->

Generated from the reviewed register; recorded results are scoped to each row.

### Evidence windows-dpi

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `6a33b4ee4999ee23c40cc39eb15dd13f80e6f031` |
| Package / installed identity | DeskPort-0.7.2-windows-x64-setup-full-test-20261017-184047-6a33b4ee.exe. Reported installed 0.7.2 / test-20261017-184047-6a33b4ee; installation not reread by this audit |
| Package SHA-256 | `dace6daa2b6478097f5596dd0d18b4dd85a9f980a6d0ec801c0a6ead13675a9b` |
| Environment | physical-hardware: Windows 11 x64 console; owned VDD; existing macOS viewer; synthetic animated desktop |
| Feature / actual result | VDD 100%/200%, disconnect/crash restoration, signed-in sharing without repeated UAC. Recorded pass: 24 scale cases, four 125% restorations, mirror preservation, pointer/keyboard, reconnect, four sharing cycles |
| Limits / pending | Partial checkpoint, not a complete IME/audio/clipboard/latency or locked/pre-login matrix. Package ID and Setup SHA-256 were cross-checked against the existing private asset-verification record; no payload was installed or executed in this audit. Main SHA is not an installed version. |
| References | [source 1](../docs/WINDOWS_DISPLAY_SERVICE.md#native-dpi-checkpoint--2026-10-17) |

### Evidence windows-broker

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `d659dac9ecf3e3acbdbe0c8909963ab60d114b59` |
| Package / installed identity | Full Windows x64 candidate from d659dac9. Reported installed candidate at that checkpoint |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | physical-hardware: Windows 11 x64 signed-in console, medium-integrity app/host/helper, SYSTEM broker/guardian |
| Feature / actual result | Automatic SCM display broker, signed-in leases, identity retention, recovery and request denials. Recorded pass: 20 sharing cycles, 21 display scenarios, broker death recovery, six denial cases, direct macOS video/pointer/keyboard |
| Limits / pending | Not cold-boot verification, complete denial/lifecycle coverage, or a pre-login network host. No secure-desktop or credential-input acceptance. |
| References | [source 1](../docs/WINDOWS_DISPLAY_SERVICE.md#native-checkpoint--2026-10-17) |

### Evidence windows-old-lock-video

| Field | Record |
| --- | --- |
| Date / tested source | 2026-09-23; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | 0.5.6 development candidate; exact package/hash absent from public summary. Historical installation; superseded by later candidates |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | physical-hardware: Closed-lid Windows AMD laptop, Apple tablet/macOS viewers |
| Feature / actual result | Lock-screen video, twelve orientation changes, display recovery. Recorded partial pass: actual lock-screen frames and display/reconnect/takeover observations |
| Limits / pending | Incomplete provenance; video alone does not establish credential input, secure-desktop transitions, pre-login hosting or current broker behavior. |
| References | [source 1](../docs/WINDOWS_DEVELOPMENT.md#physical-windows-checkpoint--2026-09-23) |

### Evidence linux-compositors

| Field | Record |
| --- | --- |
| Date / tested source | 2026-09-16; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | 0.3.6 Linux preview; exact package/hash absent from public summary. Isolated fixture only; installed desktop untouched |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | isolated-compositor: KWin 6.6.6 with permission checks; Mutter 50.4; private D-Bus/Wayland/PipeWire |
| Feature / actual result | Virtual output resize/scale, software capture, EOF/crash cleanup. Recorded pass: twelve mode/scale changes per backend and capture probes |
| Limits / pending | Historical incomplete provenance. Does not qualify current compositor versions, physical GPU encoding, input or hotplug. |
| References | [source 1](../docs/LINUX_ADAPTIVE_DISPLAY.md#validation) |

### Evidence linux-hyprland

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-03; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | Development Nix package; exact package/hash absent from public summary. Isolated compositor; no Omarchy installation acceptance |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | isolated-compositor: Real isolated Hyprland compositor, WLR capture and software H.264 |
| Feature / actual result | Owned headless output, three pixel/scale modes, target-loss rejection and cleanup. Recorded pass: fixture and real-compositor checks; disconnect/EOF/SIGKILL/group SIGTERM cleanup |
| Limits / pending | Not a physical Omarchy/GPU/input/audio/reboot result. Mirroring/physical-disable policy is not offered. |
| References | [source 1](../docs/CLI.md#isolated-verification) · [source 2](../docs/ROADMAP.md#ssh-host-management-and-omarchy--2026-10-03) |

### Evidence clipboard

| Field | Record |
| --- | --- |
| Date / tested source | 2026-09-15; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | 0.3.2 protocol checkpoint; Windows OLE follow-up 2026-09-20. Named macOS pasteboards/private compositor/VM fixtures only |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | isolated-fixture: Synthetic Unicode, PNG and file trees; macOS, KWin and historical Windows VM OLE follow-up |
| Feature / actual result | On-demand desktop text/images/files; source validation and cancellation. Recorded pass: protocol exchanges, lazy reads and isolated native providers; partial Windows VM transfers |
| Limits / pending | Not current three-platform installed clipboard acceptance. Browser explicit text/paste is separate. Historical summaries lack full package provenance. |
| References | [source 1](../docs/CLIPBOARD.md#validation-and-manual-acceptance) · [source 2](../docs/ROADMAP.md#display-recovery-incident-follow-up) |

### Evidence browser

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-05; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | Browser integration fixture; exact source/package absent from summary. Disposable browser profiles and native Linux fixture; no personal installation |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | isolated-fixture: Google Chrome 154; Linux Xvfb/PulseAudio capture; native input disabled |
| Feature / actual result | HTTPS pairing persistence, H.264/Opus, reconnect and revocation. Recorded pass: changing 720p30 frames, nonzero decoded audio, restart persistence and revocation |
| Limits / pending | Does not qualify Safari/WebKit, Windows host WebRTC, physical audio/touch/IME or compositor restoration. Later up-to-60-fps implementation is not this 30-fps checkpoint. |
| References | [source 1](../docs/BROWSER_ACCESS.md#implementation-and-validation) |

### Evidence mac-browser

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-08; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | Isolated macOS host instance; exact binary/hash absent from summary. Used installed display policy with isolated instance; not general release acceptance |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | physical-hardware: macOS host and headless Chrome client over private VPN |
| Feature / actual result | Browser H.264/Opus, adaptive size and continued frame reception. Recorded partial pass: changing video/audio, portrait/ultrawide/content size/fullscreen |
| Limits / pending | Incomplete provenance; no physical Safari, actual audio playback quality, input/IME or latency qualification. |
| References | [source 1](../docs/BROWSER_ACCESS.md#macos-host-checkpoint--2026-10-08) |

### Evidence ci-linux

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `b0c28182b8048497e32eadbae3561147d2c1969f` |
| Package / installed identity | CI source checkout at baseline; no installed package. No deployment |
| Package SHA-256 | Unknown: No package artifact is produced by this CI evidence row. |
| Environment | hosted-ci: Linux build job on ubuntu-24.04 |
| Feature / actual result | Nix Linux package, CLI/desktop-entry smoke, isolated contracts and regressions. Observed completed/success through GitHub run and job API |
| Limits / pending | Hosted Ubuntu runner; no physical AMD/KDE session, native GPU/input/audio or distro installation acceptance. |
| References | [source 1](../.github/workflows/build.yml) · [source 2](https://github.com/keithxc/deskport/actions/runs/39028059457) |

### Evidence ci-macos

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `b0c28182b8048497e32eadbae3561147d2c1969f` |
| Package / installed identity | CI source checkout at baseline; no installed package. No deployment |
| Package SHA-256 | Unknown: No package artifact is produced by this CI evidence row. |
| Environment | hosted-ci: Display control contracts job on macos-15 |
| Feature / actual result | Core, binding, lifecycle and topology contracts. Observed completed/success through GitHub run and job API |
| Limits / pending | Hosted macOS runner; no signed/notarized app package, physical display/input, installation or latency acceptance. |
| References | [source 1](../.github/workflows/core.yml) · [source 2](https://github.com/keithxc/deskport/actions/runs/39028059520) |

### Evidence ci-windows

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `b0c28182b8048497e32eadbae3561147d2c1969f` |
| Package / installed identity | CI source checkout at baseline; no installed package. No deployment |
| Package SHA-256 | Unknown: No package artifact is produced by this CI evidence row. |
| Environment | hosted-ci: Windows source checks job on ubuntu-24.04 |
| Feature / actual result | Build-script syntax, source/network/smart-stream/translation checks. Observed completed/success through GitHub run and job API |
| Limits / pending | Linux runner; no Windows compilation, installation, SCM, native Windows UI or GPU acceptance. |
| References | [source 1](../.github/workflows/windows-branch.yml) · [source 2](https://github.com/keithxc/deskport/actions/runs/39028059456) |

### Evidence native-audit

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `be315a1a788b21defb782104e3009be7094b43b1` |
| Package / installed identity | Nix output /nix/store/cclirqg1svs2qqaz4r75cavm0z4g98n0-deskport-0.7.2; hash is the uncompressed output NAR, not a release installer or full closure. Not installed or activated; isolated CLI processes only |
| Package SHA-256 | `4acdb72971703c2f05e5f752681c968714653fba72d92148a2c1c7a9f8b2814d` |
| Environment | isolated-fixture: Native NixOS x86_64; builder CPU 0-3 / nice 15 / four cores; private HOME/XDG and loopback TLS; no sharing or input |
| Feature / actual result | Evidence validation, full package build, package identity and isolated local CLI. Observed pass: constrained Nix build, version/help/desktop identity/icon, short-TMPDIR CLI enrollment/config/ownership/revocation/SIGTERM; initial 131-byte socket-path setup was rejected before product assertions |
| Limits / pending | No GPU, physical streaming/input/audio/clipboard/IME/HDR or native Windows/macOS acceptance; Seamless not run. Same binary passed after using a short isolated TMPDIR inside the build root; long TMPDIR support remains unaccepted. |
| References | [source 1](../docs/PC_VERIFICATION_2026-10-17.md#observed-results) · [source 2](../scripts/test-host-cli.py) |

### Evidence ipad-takeover

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; Unknown: Documentation commit a8174c968eff7de7b962396279720141063da193 records the observation but does not identify all installed host/client source revisions. |
| Package / installed identity | Existing installed Linux KDE Wayland and Windows 11 builds; exact package/version IDs absent from this public checkpoint. Existing host installations and native iPad/macOS clients; no deployed binary or service replaced/restarted |
| Package SHA-256 | Unknown: The exact installed package hashes are not recorded in this public checkpoint. |
| Environment | physical-hardware: Native iPad takes over active macOS viewers on installed Linux KDE Wayland over Tailscale and Windows 11 over Wi-Fi LAN |
| Feature / actual result | Normal takeover, HEVC video, synthetic text, disconnect/reconnect and Linux virtual-output cleanup/physical-layout preservation. Recorded partial pass: old viewer processes exited, HEVC reached iPad, synthetic text reached the remote app, reconnect restored video; observed Linux physical positions/scales/enablement preserved |
| Limits / pending | Not new-main acceptance, Windows overlay networking, cold boot, secure desktop, audio, clipboard or long recovery. Package provenance is incomplete. The 10.5-second admission barrier versus 20/25-second helper/settling budgets remains an unexercised slow-path risk, not fixed by the fast successful takeover. |
| References | [source 1](../docs/ROADMAP.md#native-ipad-takeover-of-linux-and-windows--2026-10-17) |
<!-- pc-support-evidence:end -->

## Updating after other PC work merges

This audit completes the documentation item only. It does not mark the other
sixteen PC work items complete. On integration, review each contributing commit
and update the source/core snapshot together with the affected feature row and
evidence. Preserve old checkpoints instead of overwriting their tested SHA.

Required incoming sources: current Web gateway/media/browser tests and real client
results; native IME/caret/input results; audio capture/playback/focus results;
clipboard provider/protocol and synthetic file results; capture/codec/HDR results;
platform display/hotplug/recovery results; service lifecycle/installer results and
exact final package verification. Read the affected topic and `ROADMAP.md`, record
the actual source SHA, package ID/hash, installed identity (or explicitly unknown),
date, environment, feature, result and limits. A build/VM/mock pass must retain
its evidence type. A failed setup is not a failed product assertion; an unrun
case remains pending. Re-run the lightweight checker after each update.
