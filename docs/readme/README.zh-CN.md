<p align="center">
  <img src="../../app/res/deskport.svg" alt="DeskPort" width="128" height="128">
</p>

<h1 align="center">DeskPort</h1>

> **Current desktop release: [0.7.4 (r2)](https://github.com/keithxc/deskport/releases/tag/v0.7.4).**
> The translated package examples and verification history below still describe
> 0.7.3. Use the [current installation guide](../../README.md#install) and
> [0.7.4 release notes](../RELEASE_0.7.4.md) for the replacement downloads.

<p align="center">
  <a href="../../README.md">English</a> ·
  <b>简体中文</b> ·
  <a href="README.zh-TW.md">繁體中文</a> ·
  <a href="README.ja.md">日本語</a> ·
  <a href="README.ko.md">한국어</a> ·
  <a href="README.de.md">Deutsch</a> ·
  <a href="README.es.md">Español</a> ·
  <a href="README.fr.md">Français</a><br>
  <a href="README.pt-BR.md">Português (Brasil)</a> ·
  <a href="README.ru.md">Русский</a> ·
  <a href="README.it.md">Italiano</a> ·
  <a href="README.nl.md">Nederlands</a> ·
  <a href="README.pl.md">Polski</a> ·
  <a href="README.tr.md">Türkçe</a> ·
  <a href="README.uk.md">Українська</a><br>
  <a href="README.ar.md">العربية</a> ·
  <a href="README.hi.md">हिन्दी</a> ·
  <a href="README.id.md">Bahasa Indonesia</a> ·
  <a href="README.ms.md">Bahasa Melayu</a> ·
  <a href="README.th.md">ไทย</a> ·
  <a href="README.vi.md">Tiếng Việt</a> ·
  <a href="README.bo.md">བོད་ཡིག</a>
</p>

<p align="center">
  基于 Moonlight 与 Sunshine 的远程桌面工作区。<br>
  让远程桌面常驻后台，一个动作把它带到当前工作区，
  收起时也无需重新连接。
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="桌面版本" src="https://img.shields.io/badge/desktop-0.7.3-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="../../LICENSE"><img alt="许可证" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## 主要功能

- **一个桌面应用，既能查看也能共享。** 内置 Sunshine、设备授权、独立凭据与设置；
  macOS、Windows 与 Linux 主机安装包无需另装 Sunshine（仅客户端的 Flatpak 除外）。
- **工作区跟随你的客户端。** 在兼容的 macOS 与 KDE 主机上自适应尺寸，支持 HiDPI、
  移动端横竖屏布局与每设备尺寸设置。调整尺寸时视频会短暂重连，并非无缝的编码器切换。
- **选择主机的显示策略。** 虚拟主屏并镜像其他屏幕、虚拟主屏并关闭其他屏幕，或在符合条件的
  主机上使用扩展工作区。会话结束时 DeskPort 会恢复保存的屏幕布局。
- **在已授权设备之间切换。** 接管正在进行的会话前需要确认；被替换的客户端会收到经过认证的接管说明。
- **桌面工作随手可及。** 隐藏和唤回已连接的查看端，隐藏时释放远程输入，
  并可在不影响本地共享的情况下单独断开查看。
- **每台电脑独立保存设置。** 画面、声音、输入、桌面微调与地址编辑都跟随设备保存；
  支持卡片、置顶、主题和多语言界面。
- **按需共享桌面剪贴板。** 文本即时更新；在已绑定且启用该选项的桌面设备之间，
  受支持的图片与文件按需获取。见[剪贴板范围与限制](../CLIPBOARD.md)。
- **原生移动端控制。** iPhone/iPad 与开发中的 Android 客户端提供触控/触控板输入、
  键盘控制，并可直接进入远程桌面。
- **可选的登录启动与 macOS 恢复。** 需要处于活动的图形登录会话并获得系统授权；
  它们不会解锁 FileVault，也不会替你登录。

## 界面预览

当前界面截图，使用的是演示设备。点击可查看大图；这些预览不代表已在商店上架或已正式发布。

| 设备与分组 | 每设备显示设置 | 内置使用手册 |
| --- | --- | --- |
| [![设备与分组](../media/devices.png)](../media/devices.png) | [![每设备显示设置](../media/device-settings.png)](../media/device-settings.png) | [![内置使用手册](../media/manual.png)](../media/manual.png) |

## 支持的平台

下方为 0.7.3 正式版链接。安装包检查与运行时验证各有范围，结果及未验证项目见[验证报告](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)。

| 平台 | 角色 | 发布状态 | 下载 |
| --- | --- | --- | --- |
| macOS Apple Silicon，macOS 26+ | 查看端 + 主机端 + 原生虚拟显示器 | **0.7.3 正式版**；Developer ID 签名、Apple 公证与 ZIP 解压后的 Gatekeeper 检查通过 | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | 查看端 + 主机端；符合条件的 KWin、Mutter 和 Hyprland 主机支持自适应虚拟工作区 | **0.7.3 正式版**：DEB、RPM、Arch、AppImage、便携包与 Nix | [发布页](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [安装指南](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | 仅查看端 | **0.7.3 正式版**，Freedesktop Platform 25.08 | [发布页](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad，iOS/iPadOS 15+ | 仅客户端 | 已上架 App Store；较新的版本另在 TestFlight 测试 | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | 仅客户端 | 开发中；正在实机检查，尚未在商店公开发布 | 暂不可用 |
| Windows x64 | 查看端 + 主机端 + 虚拟显示器 | **0.7.3 正式版**；完整安装包提供主机功能，便携 ZIP 仅为查看端；未安装状态下的包检查与 CLI 验证通过；应用与安装器未签名 | [安装包](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | 未验证 | 没有经过验证的发布包 | — |

> **Windows 杀毒软件提示：** 杀毒软件有可能将 DeskPort 的 Windows 辅助组件误报为威胁。
> 我们自测时未遇到这种情况。请保持 Defender 开启；如果出现警告，
> 请查看发布附带的验证报告和[样本记录](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice)。

原生 Linux 包需要 glibc 2.39+：Ubuntu 24.04+/Debian 13+、Fedora 44、
最新的 Arch。AppImage 自带私有运行时，可用于 glibc 较旧的桌面；主机能力仍取决于硬件驱动
与合成器支持。KDE 虚拟显示器主机需要合成器授权条目。原生包与 Nix 包提供固定的条目；
AppImage 与便携版会在开始共享时自动注册当前显示助手的路径。主机输入仍需要设备权限。
移动应用不能作为桌面主机。
仅有包定义或能编译成功，并不代表该平台受支持。

## 安装

**macOS：** 打开[DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg)，
把 DeskPort 拖入「应用程序」后打开。如需使用主机端功能，请授予「屏幕录制」和
「辅助功能」权限。无需 Nix 或 Homebrew。

**Linux：** 选择适合你系统的 [0.7.3 安装包](https://github.com/keithxc/deskport/releases/tag/v0.7.3)，
并按照[安装指南](../LINUX_PACKAGES.md)操作，包括主机输入权限的设置。使用 Nix：`nix run github:keithxc/deskport/v0.7.3`。
Flatpak 仅提供查看功能。发布文件附有 [SHA-256 校验和](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt)与[验证报告](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)。

**iPhone / iPad：** 从 [App Store 安装 DeskPort](https://apps.apple.com/us/app/deskport/id6812389978)。
移动应用为一次性付费购买，当地价格以你所在地区的商店为准。
桌面版始终免费并保持开源。你的购买会用于代码签名、测试设备和后续开发。

在主机上开始共享，在客户端上选择该主机，然后在主机上批准该设备。两台设备之间需要网络可达：
使用局域网或你自己的 VPN（如 Tailscale）。DeskPort 不提供账号服务、托管桌面、中继
或网络隧道。旧版 Sunshine PIN 配对仍然可用。

**0.7.3 正式版：** 本次整合了 22 种语言的共享界面目录与浏览器语言切换、Windows 已登录会话中的共享与虚拟显示器 DPI、Linux 运行时与 Wayland 剪贴板、受保护的 Hyprland/X11 工作区、可选的本地输入法，以及方形设备卡片的改进。 变更与验证范围见 [0.7.3 发行说明](../RELEASE_0.7.3.md)。

**0.7.3 验证范围：** Windows 最终包内容一致性、PE、原生 CLI 检查通过，Defender 扫描零检出；未进行安装、GUI 连接或串流验收。最终 AppImage 通过 Bazzite 虚拟机中的正常 FUSE 与软件 GPU 检查。Ubuntu 26.04 虚拟机复现了 0.7.2 DEB 的 VA 崩溃，并确认最终 0.7.3 DEB 已修复且升级通过。回滚重现了旧版崩溃，随后卸载仍保留了合成测试配置。GNOME 采集与恢复仍未通过验收。容器、Flatpak 结果及其他限制见[验证报告](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)；这些结果不代表完整的物理硬件验收。

**历史版本：0.7.2 稳定版：** [桌面版本](https://github.com/keithxc/deskport/releases/tag/v0.7.2)整合了调整尺寸、远程串流、省流量和 Linux 运行时方面的改进，并修复了 Bazzite/KDE Wayland 上的 AppImage 问题。详见 [0.7.2 发行说明](../RELEASE_0.7.2.md)；各 Linux 发行版的安装步骤见[英文 README](../../README.md#linux-installation-by-distribution)。

**浏览器访问：** 桌面主机提供 HTTPS 远程桌面，支持 30 秒登录码、可选的七天浏览器配对、
自适应尺寸和全屏。见 [0.7.1 发行说明](../RELEASE_0.7.1.md)与
[浏览器使用指南](../BROWSER_ACCESS.md)。AppImage 内含私有的 glibc 运行时；
原生包仍需要 glibc 2.39。

### Linux 软件仓库状态

| 渠道 | 状态 | 安装方式 |
| --- | --- | --- |
| GitHub 发布包 | 0.7.3 正式版 | 在上方下载适合你发行版的安装包 |
| 上游 Nix flake | 0.7.3 正式版 | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | 配方已测试；因 AUR 暂停新账号注册而无法发布 | [在本地构建配方](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | 已提交 [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) 等待审核；尚未合并 | 合并前请使用上游 flake |
| Arch 官方仓库 / Flathub | 未收录 | 使用发布页下载；Flatpak 仅为查看端 |

可下载的 Arch 包用 `pacman -U` 安装，并不意味着可以使用
`pacman -S deskport`。同样，上游 flake 也不会让 Nixpkgs 中出现
`pkgs.deskport`。主机输入、采集权限与防火墙设置独立于应用安装之外；见
[Linux 安装指南](../LINUX_PACKAGES.md#nixos-and-nix)。

## 同类产品对比

这是工作流程与平台的对比，依据所链接的官方文档于 **2026-09-20** 核对，
并非延迟或画质测试。各产品的功能与平台限制可能会变化。

| 方面 | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| 主要工作流程 | 持续的远程桌面工作区、自适应尺寸与跨设备接管 | 通过独立的客户端与主机应用进行游戏/桌面串流 | 通用远程控制与支持 | 交互式远程桌面与协作 |
| 桌面主机平台 | macOS Apple Silicon、Linux x86_64 与 Windows x64 | Windows、macOS、Linux 与 FreeBSD，受各平台限制 | Windows、macOS 与 Linux | Windows 与 macOS；Linux 不能作为主机 |
| 桌面客户端 | macOS Apple Silicon、Linux x86_64 与 Windows x64 | Windows、macOS、Linux 等 | Windows、macOS 与 Linux | Windows、macOS、Linux 与受支持的 Chromium 浏览器 |
| 移动客户端 | iPhone/iPad 已发布；Android 开发中 | iOS/iPadOS 与 Android | iOS/iPadOS 与 Android；iOS 不能作为主机 | Android；无 iOS/iPadOS 客户端 |
| 配置方式 | 原生桌面包内置主机；在 DeskPort 中批准设备 | 单独安装并配置 Sunshine，再与 Moonlight 配对 | 公共服务器或自建服务器 | Parsec 账号与应用 |
| 网络/服务模式 | 自备局域网/VPN 可达性；无 DeskPort 中继 | 自建串流；自行配置网络可达性 | 公共服务或自建 OSS/Pro 服务器 | Parsec 账号/服务体系 |
| 源码与分发 | GPL 桌面版，桌面安装包免费；付费 Apple 应用单独分发 | 开源客户端与主机端 | 开源客户端与 OSS 服务器；另有商业 Pro 服务器 | 专有应用/服务 |

来源：[Moonlight](https://moonlight-stream.org/)、
[Sunshine installation and platform support](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html)、
[RustDesk clients](https://rustdesk.com/docs/en/client/)、
[RustDesk self-hosting](https://rustdesk.com/docs/en/self-host/)、
[Parsec platform compatibility](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility)、
[Parsec Linux setup](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux)。
DeskPort 当前的平台限制见上文；本表并不意味着各操作系统功能完全一致，
也不代表经过测量的性能优势。

参见 [0.7.3 发行说明](../RELEASE_0.7.3.md)、
[架构说明](../ARCHITECTURE.md)与[路线图](../ROADMAP.md)。

## 在 Linux 上构建与运行

启用 Nix 与 flakes 后：

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

查看端的第三方源码依赖已内置（vendored）；共享核心与 Nix 构建输入仍为锁定的外部依赖。
原生构建需要初始化 `shared/deskport-core`。见 [docs/VENDORED.md](../VENDORED.md)。
SSH 设置、本地主机管理、VPS 前提条件以及 Omarchy/Hyprland 请参阅
[命令行指南](../CLI.md)。`deskport doctor` 检查主机运行的前提条件；
`deskport status --json` 无需打开窗口即可查看正在运行的实例。
`nix run . -- --help` 会列出命令行接口。连接前先在主机上开始共享并绑定设备。旧版
Sunshine PIN 配对同样可用。本项目不包含、也不会从 Moonlight 导入任何个人主机或配对凭据。
新建的手动地址默认使用 DeskPort 的端口 `48989`。如果不同，请填写主机共享页面上显示的端口；
若要显式连接默认的独立 Sunshine 安装，可使用 `host:47989`。已保存或发现的端点保留各自的端口。

可编辑的原生构建：

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

上游项目的文件名保持不变，以便这个分支仍然易于审阅。

## 与 Moonlight 有何不同？

- 内置 Sunshine 主机端，支持设备授权、接管与每设备设置。
- 查看端隐藏时会话保持连接；可从托盘/菜单栏唤回。
- 桌面主机提供 HTTPS 浏览器访问。
- 独立的 `deskport` 可执行文件、`DeskPort` Qt 设置命名空间，以及
  `io.github.keithxc.DeskPort` Linux 应用 ID。
- 默认窗口化串流与绝对指针控制。
- 失去焦点时静音；默认关闭游戏优化、手柄鼠标、多手柄模式、后台手柄输入与 Discord 状态。
- 这个独立应用不会出现上游 Moonlight 的更新提示。
- 锁定的 Nix 环境与一套 Linux 构建流程。

桌面界面提供设备、共享与设置页面，包含语言选择与独立的主机权限。关闭查看端会保留其会话
并打开设备列表。处于活动状态的设备会提供「返回桌面」，其他设备在当前会话断开前只显示详情。
可以置顶常用设备，并在紧凑列表与卡片之间切换。外观跟随系统，也可强制浅色或深色。

## 平台范围

各平台的发布状态见上方的[平台表](#支持的平台)。桌面版覆盖 macOS Apple Silicon、Linux x86_64
与 Windows x64，均可作为查看端和主机端；移动应用仅为客户端。

客户端平台支持与主机端支持是相互独立的：Mac 主机并不要求使用 DeskPort 的 Mac 客户端。
硬件解码、实时输入、画质与唤回延迟都需要真实会话测试，构建成功并不能说明这些。

## 下一个里程碑

让一个会话在 **50 次隐藏/显示循环**中保持连接，把窗口显示在当前工作区，并可靠地交还本地输入。
新画面延迟与后台资源占用要与窗口出现时间分开测量。

验收标准与延后的功能见[路线图](../ROADMAP.md)，来源与维护边界见
[上游说明](../UPSTREAM.md)。

## 验证

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

冒烟检查使用临时的 XDG 配置/缓存目录与离屏 Qt 平台。它不会与主机配对、启动串流或注入输入。

## 日常桌面操作

关闭窗口后 DeskPort 仍在托盘/菜单栏运行。用 **Open DeskPort** 唤回它，用 **Disconnect viewer** 只结束
当前连接，或用 **Quit DeskPort** 结束整个服务。查看端关闭时，本地共享继续运行。

两端绑定设备默认启用纯文本剪贴板共享与系统快捷键捕获。设置更改在重新连接后生效。只共享新的
复制内容，上限 1 MiB。兼容且已启用该选项的桌面设备之间可按需获取受支持的图片与文件；见[限制](../CLIPBOARD.md)。
在桌面指针模式下，键盘路由跟随位于聚焦视频内的指针。
**Ctrl+Alt+Shift+Z** 释放输入；
**Ctrl+Alt+Shift+Q** 断开查看端。显式释放后，点击画面内部即可重新获得输入。
系统保留的快捷键取决于桌面合成器。

登录启动与恢复需要处于活动的图形登录会话。在依赖某台电脑进行无人值守访问之前，请先阅读
[验收检查与限制](../INPUT_SERVICE_ACCEPTANCE.md)。

## 许可与致谢

DeskPort 是 [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt) 的独立衍生项目，
最初基于 v6.1.0，并非官方的 Moonlight 或 Sunshine 发行版。Moonlight 提供串流基础；
[Sunshine](https://github.com/LizardByte/Sunshine) 被打包进 macOS、Windows 与 Linux 主机包（仅查看端的 Flatpak 除外），
并由 Linux Nix 包提供。单独安装的 Sunshine 服务保持独立。

GPL-3.0-or-later；见 [LICENSE](../../LICENSE)、保留的源码声明，以及
[docs/VENDORED.md](../VENDORED.md) 中列出的每个内置依赖的许可证。原始文档保留在
[README.upstream.md](README.upstream.md)。

### 诊断与反馈

诊断日志默认开启。复现问题后，生成日志 ZIP 并打开 GitHub issue 草稿。附加之前请先检查 ZIP 内容：
GitHub issue 是公开的，DeskPort 不会替你上传或提交任何内容。
见[诊断数据与限制](../DIAGNOSTICS.md)。
