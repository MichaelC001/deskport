# DeskPort TODO

## Session usability and lifecycle — 2026-09-20

Reason: user approved completing the audited backlog together, with Mac, iPad and Android emulator verification. Development and testing only; deployment and release remain separate.

- [x] Audit existing fullscreen, vendored dependencies, display policies, device cards and reconnect paths against source.
- [x] Existing desktop fullscreen toggle (Ctrl+Alt+Shift+X) and native disconnect shortcut (Ctrl+Alt+Shift+Q).
- [x] Vendor desktop viewer dependencies and preserve upstream source/version/license records (0.4.1).
- [x] Add authenticated, capability-negotiated host request to leave desktop fullscreen without ending the stream; make the native quit shortcut leave fullscreen first.
- [x] Expose device Details, Actions and Settings separately, including active-session disconnect/reconnect actions.
- [x] Create macOS/Linux virtual displays on admission, restore physical topology and destroy on explicit disconnect; retain a bounded grace period for opted-in transport recovery. The audit also found and removed Linux's short-lived startup probe display.
- [x] Verify macOS mirror/disable/extend recovery adapters, including initially disabled and hotplugged screens; verify three native extended-display create/remove cycles.
- [ ] Verify physical macOS mirror/disable topology and real two-device video/input after activation. Adapter checks do not close this item.
- [x] Add bounded desktop network recovery with backoff/cancel; never retry intentional disconnect, revocation or takeover; never automatically take over another device.
- [x] Vendor the supported Sunshine host source and required source dependencies with provenance; preserve the shared-core single source of truth. System toolchains/libraries are outside this scope.
- [x] Mac desktop/host builds, 62 binding/session tests, host lifecycle tests (Mac: 29 passed/2 Linux skips; Linux: all 31 passed), 20 QML checks, core/state/navigation and macOS topology checks.
- [x] iPad simulator: three UI cases. Android emulator: build/install, cards/settings and landscape. Existing Apple/Android protocol adapters pass.
- [x] User-selected Linux host on the home LAN: final Nix build and all three isolated KWin policies passed, each with 12 resizes, real capture/encoder probes, disabled-output preservation and EOF/crash recovery.
- [ ] Real stream recovery across a temporary Wi-Fi/VPN path change; host-requested fullscreen exit on a connected desktop viewer.
- [ ] GNOME on-demand runtime acceptance; macOS VM topology run (disk-space prerequisite not met).
- [ ] Publish the new core revision before distributing the desktop pin; package/release/activate only through the established delivery workflow.

- [x] User-authorized direct local validation update (2026-09-20): same-identity notarized macOS bundle and independent Linux package/service override installed; rollback preserved. No system-generation activation or public release. See [local update route](docs/LOCAL_UPDATE.md).

- [x] Fix first live macOS admission returning HTTP 503: long-lived CoreGraphics consumers miss newly created display modes. Publish the verified backing dimensions with the admitted target, validate live identity/bounds in capture and input, and add a persistent-consumer native regression.
- [x] Install the notarized macOS correction and verify authenticated paired `/launch` returns status 200 with successful H.264 / 8-bit HEVC capture probing.
- [ ] Retest full video and input from the installed desktop client; the launch probe is not a completed stream acceptance test.

Next checkpoint: validate the activated local candidate with paired live streams. Mobile automatic reconnect and mobile upstream vendoring remain separate follow-ups. See [implementation and verification](docs/SESSION_LIFECYCLE.md).


## 设备 UI、主题与流量（2026-09-14）

- [x] 卡片式设备列表、系统标识、每设备串流设置、本机外观与语言独立。
- [x] 系统明暗与强调色分别跟随或手动覆盖，可选侧栏流量摘要。
- [ ] 手动 switch 后验收系统主题切换、实际串流与热点用量；不以隔离截图代替。
- [ ] 后续评估按网络识别计费连接、月度统计；此次不实施。

## CPU 与分辨率自适应（2026-09-13）

原因：用户要求优先降低 CPU 和加快尺寸适配，同时保持长期可维护性。

- [x] P0 工具：CPU 区间采样、尺寸切换分段日志及解析工具已交付；已保存升级前现场采样。
- [ ] P0 实测验收：旧版无完整分段埋点，现场内容未固定且伴随构建；严格同场景 A/B 仍待完成。渲染提交不等于物理显示。
- [x] P1 实现：300 ms 稳定等待、实际显示模式立即检查并二次确认、恢复前读取最新窗口尺寸、同次初始化内复用成功解码探测、请求唤醒控制线程、限制过渡动画重复绘制。Mac/Linux 构建与隔离回归通过。
- [ ] P1 实测验收：两端 switch 后确认耗时、CPU、跨屏尺寸/输入正确性和长会话；不把自动测试当实机通过。
- [ ] P2：根据热点优化静态重复帧与客户端显示队列；本批不实施。
- [ ] P3：实现有能力协商和重连回退的视频局部重配置，避免整条会话重连；本批不实施。
- [ ] P4：有测量或维护收益后，再考虑合并显示助手；本批保留进程边界。

预发布范围（2026-09-13 用户确认）：只交付 mm4 的公证 Mac 包与 pk4 的 NixOS flake 包，不打其他平台/发行版格式。

交付：0.2.1 已公开为 prerelease，5项目标资产的GitHub摘要与本地一致，0.2.0仍为最新稳定版；mynix 已更新且双端完整系统构建通过。下一动作：用户 switch 后开始实测。
检查点：用户 rebuild switch 后验证两端实际版本、30 次尺寸切换、CPU 同场景对照和长会话。


## 组件源码集成（2026-09-13 历史计划；2026-09-20 已恢复）

- [x] Sunshine 支持的 host 源码已纳入统一构建；macOS 静态资源也已内置。
- [x] 已盘点桌面源码、输入组件和音视频依赖；系统库/工具链与移动端源码另列范围。
- [x] 已锁定上游版本，保留许可证、来源和归档摘要，并接入现有平台补丁流程。
- [ ] 首个检查点：选一个组件完成源码构建与现有发布包的功能对照，确认包体积、构建时间、签名及运行行为后再扩大范围。

历史边界：0.2.0 未包含此项。0.4.1 已内置桌面 viewer 依赖；2026-09-20 用户授权继续，当前范围和未完成项以上方清单为准。

## 后续性能优化（2026-09-12，暂缓）

- [ ] 排查客户端显示队列丢帧：检查帧节奏、VSync 与显示刷新率的配合，以及队列等待时间；以后再处理。
- [ ] 对 0.1.13 做固定分辨率、相同内容和相同时长的静态/视频对照，分别记录宿主 CPU、接收/渲染 FPS、网络丢帧和显示队列丢帧。
- [ ] 检查静态转动态、输入唤醒、重连和调整尺寸时的响应；完成较长时间的稳定性验证。
- [ ] 评估 10-bit/P010 路径的原生静态节省。目前保留 AVFoundation 兼容采集，已移除 CPU 像素比较，但未获得 ScreenCaptureKit 路径的静态节省。

### 当前基线

0.1.13 已实际启用 ScreenCaptureKit，当前 8-bit HEVC 路径不再做 CPU 全帧比较。
约 30 秒短采样中，宿主 CPU 平均 11.1%（单核口径，范围 10.4–11.6%）；
客户端三个统计窗口接收约 55–56 FPS、渲染约 52–53 FPS，显示队列丢帧
2.6–6.8%，队列等待约 26–28 ms。该短采样期间未记录网络丢帧或 IDR 错误，
不代表长期问题消失。旧版视频观察的宿主 CPU 平均约 23.3%，但内容与时长
不同，不能据此宣称严格的性能提升比例。

下一步：用户决定继续时，先完成同场景对照，再根据数据选择改动；本轮不继续优化。


## Client-controlled session settings — 2026-09-14

Reason: user resumed this item and requested development-branch-only delivery.

- [x] Per-device client profiles covering normal and advanced stream settings.
- [x] Immutable connection snapshots; edits take effect after reconnecting.
- [x] Authenticated per-session host audio/input options and old-host detection.
- [x] Client-controlled clipboard and macOS smart encoder policy; simplify Sharing.
- [x] Separate tray Reconnect action; preserve remote apps and release held input.
- [x] Inventory scattered options and keep language/appearance/startup local.
- [ ] Live two-device audio/input/video and ten-reconnect acceptance after the user
  chooses to activate a build. Main and deployed services remain unchanged.

See [session settings](docs/SESSION_SETTINGS.md) for protocol, migration and limits.

- [x] Simplify desktop device actions: remove duplicate settings/details entries, Applications and Wake PC; retain dedicated card buttons and direct Desktop connection.

- [x] Match the mobile card header with four equal slots: status, details, actions and settings.

- [x] Remove settings duplicated between Device settings and Advanced streaming settings; manual frame-rate/bitrate edits leave automatic mode.

## H.264 streaming recovery — 2026-09-20

- [x] Reproduce reference-picture corruption with synthetic multi-reference H.264; preserve the original SPS reference count in the desktop decoder.
- [x] Verify fixed decoded pixels match the original bitstream and preserve reordered-stream declarations.
- [x] Validate the corrected Linux client on a live 3824x2000 H.264 macOS stream: the reference/slice errors and decoder-triggered recovery loop are absent.
- [x] Confirm a 26-minute live H.264 session with no reference/slice errors, decode failures, decoder-triggered recovery or disconnects; user reports the slowdown resolved.
- [ ] Investigate residual occasional decode-queue overflow separately from SPS corruption; no further overflows occurred after the first two during this observation.
- [ ] Investigate the separate macOS session-teardown watchdog: one rapid reconnect hit the 10-second join timeout; retain paired logs and the crash stack before changing host lifecycle.
