# PC task integration — 2026-10-18

Seven tested implementation/documentation slices are eligible for this batch.
This does not declare every platform, GPU, input device or installed build accepted.
The integration preserves the activated macOS/iPad checkpoint on main and changes
no installed application, system permission, release asset or mynix configuration.
Seamless remains deferred.

| Task | Integration decision | Verified scope and remaining gate |
| --- | --- | --- |
| 1. Windows installer/service lifecycle | Retain branch | Regression, cross-build and ordinary-user fixtures pass; first install, upgrade/rollback, uninstall/reinstall and full native lifecycle remain pending. |
| 2. Core PC interoperability | Retain branch | Native macOS clipboard fix and isolated tools pass; the 76-case four-direction physical matrix is incomplete. |
| 3. GNOME policies | Retain branch | Topology/recovery and Nix pass, but intermittent capture startup timeout and PipeWire/DisplayConfig recovery failure block acceptance. |
| 4. Hyprland policies | Integrate `559c5dcb` | IPC, HostManager, Nix and Lua/legacy WLR/H.264 probes pass. Physical topology and end-to-end client media/input remain separate. |
| 5. X11 workspace | Integrate `72eb11a4` | Preconfigured inactive dummy outputs, 1x extension, packaged capture and recovery pass. No general GPU virtual output, 2x or topology-policy promise. |
| 6. Wayland clipboard | Integrate `4a8f83e3` | Portal/wlr fallbacks, demand/cancel tests and isolated GNOME/Hyprland paths pass. Physical file-manager paste and wlr-v1-only remain pending. |
| 7. Local IME | Integrate through `abbe167e` | Native macOS/Windows Pinyin and Linux X11 CJK fixtures, builds and regressions pass. Default off; top-left candidates; native Wayland, HiDPI/fullscreen and remote insertion pending. |
| 8. Client power recovery | Retain branch | Platform builds and isolated lifecycle/input/worker checks pass; real sleep/lock/wake, physical focus/input and route recovery remain pending. |
| 9. Background/recall | Retain branch | Nix and native macOS/Windows synthetic windows pass; Linux GUI and real stream resource/audio/input acceptance remain pending. |
| 10. Live encoder bitrate | Retain branch | Builds, regressions and VideoToolbox hardware switches pass; NVENC/AMF/VAAPI hardware and network/stream acceptance remain pending. |
| 11. HDR/codec negotiation | Retain branch | Codec/VAAPI/native capture fixtures pass; complete macOS build and physical HDR/end-to-end validation remain pending. |
| 12. Web localization | Integrate `8e01e234` | Shared 126-key/22-locale resources, Chromium/RTC/HTTPS and Nix pass. Native-speaker and broader physical/browser checks remain pending. |
| 13. Windows Web/Safari | Retain branch | Build/Linux/Windows loopback fixtures and later 12 native Safari checks pass; Windows-host Web end-to-end remains incomplete. |
| 14. macOS lifecycle | Retain branch | Topology/lifecycle/binding/sanitizer and Nix checks pass. The separate activated iPad takeover checkpoint is on main; physical multiscreen, upgrade/rollback and permission/recovery-helper lifecycle remain pending. |
| 15. Linux packages | Integrate through `f6084121` | Pinned VA runtime/launcher repair, Ubuntu candidate control, Nix, synthetic AV and 28 VM teardown checks pass. Historical old-DEB rollback and GNOME AppImage failures remain explicit. |
| 16. Fonts/RTL | Retain branch | Native macOS page/font and UI checks pass; required Linux/Nix and Windows checks have not run. Native-speaker review remains pending. |
| 17. Support evidence | Integrate through `edf745b2` | Versioned provenance, references, CI scope and isolated build/CLI checks pass. Refresh the snapshot for this batch without relabeling historical passes. |

Hyprland and X11 both extend HostManager capability detection. Their integration
retains both states, keeps X11 policy omission/rejection, and preserves the
Hyprland legacy-helper fallback. The lifecycle fixture resets both backend test
environments so one case cannot contaminate another.

The IME and Web branches had independent shared-core descendants. Core
`f34fc7b1524ce436185c4b085a903e6af4314790` merges their catalogs and IME contract;
Git, Nix revision and NAR pins advance together. Qt/Web resources must still
match the combined catalog. Individual-branch tests are historical evidence;
integration build and regression results are recorded separately after execution.
