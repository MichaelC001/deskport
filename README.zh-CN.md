<p align="center">
  <img src="app/res/deskport.svg" alt="DeskPort" width="128" height="128">
</p>

<h1 align="center">DeskPort</h1>

<p align="center">
  <a href="README.md">English</a> ·
  <b>简体中文</b> ·
  <a href="README.zh-TW.md">繁體中文</a> ·
  <a href="README.ja.md">日本語</a>
</p>

<p align="center">
  基于 Moonlight 与 Sunshine 的远程桌面工作区。<br>
  让远程桌面常驻后台，一个动作把它带到当前工作区，
  收起时也无需重新连接。
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.4.5"><img alt="桌面版本" src="https://img.shields.io/badge/desktop-0.4.5-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="LICENSE"><img alt="许可证" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## 主要功能

- 桌面端整合查看、共享、设备授权和独立配置，原生安装包内置 Sunshine。
- 兼容 macOS/KDE 主机的自适应工作区、HiDPI、每设备桌面微调；调整尺寸时会短暂重连视频。
- 虚拟主屏镜像、虚拟主屏并关闭其他屏幕、扩展工作区三种策略，断开后恢复屏幕布局。
- 跨设备接管前确认，被接管端显示明确原因。
- 桌面会话后台保留与快速呼出；隐藏时释放远程输入。
- 每台电脑保存画面、声音、输入和地址设置；桌面之间文字即时共享，图片/文件按需获取。
- 原生移动触控、触控板与办公键盘；可选登录启动和 macOS 进程恢复。

## 同类产品对比

核对日期：2026-09-20。下表对比平台和使用方式，不代表性能测试。

| 项目 | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| 主要用途 | 持续远程工作区、自适应大小、跨设备接管 | 游戏和桌面串流 | 远程控制与支持 | 交互式远程桌面与协作 |
| 桌面主机 | macOS Apple Silicon、Linux x86_64 | Windows、macOS、Linux、FreeBSD | Windows、macOS、Linux | Windows、macOS；Linux 不能作为主机 |
| 桌面客户端 | macOS Apple Silicon、Linux x86_64 | Windows、macOS、Linux 等 | Windows、macOS、Linux | Windows、macOS、Linux、兼容 Chromium 浏览器 |
| 移动客户端 | iPhone/iPad 已发布；Android 开发中 | iOS/iPadOS、Android | iOS/iPadOS、Android | Android；不支持 iOS/iPadOS |
| 配置方式 | 原生桌面包内置主机，设备端请求授权 | 单独配置 Sunshine 后配对 Moonlight | 公共服务器或自建服务器 | Parsec 账号与应用 |
| 网络路径 | 自备局域网/VPN，无 DeskPort 中继服务 | 自建串流，配置网络可达性 | 公共服务或自建 OSS/Pro 服务 | Parsec 账号/服务体系 |

官方来源和完整说明见[英文对比表](README.md#comparison-with-similar-products)。Windows、Intel Mac 和 ARM Linux 尚不属于 DeskPort 已验证发布平台；Flatpak 仅为客户端。

## 平台进度

| 平台 | 角色 | 进度 | 获取 |
| --- | --- | --- | --- |
| **macOS**（Apple Silicon，macOS 26+） | 查看端 + 主机端 + 虚拟显示器 | ✅ 稳定版 — 0.4.5，已经 Apple 公证 | [DMG](https://github.com/keithxc/deskport/releases/download/v0.4.5/DeskPort-0.4.5-macos-arm64.dmg) |
| **Linux x86-64** | 查看端 + 主机端 | ✅ 已发布 — 0.4.5：Nix / DEB / RPM / Arch / AppImage / 便携包；Flatpak 仅客户端 | [发布页](https://github.com/keithxc/deskport/releases) · [指南](docs/LINUX_PACKAGES.md) |
| **Linux ARM64** | 查看端 + 主机端 | 🧪 仅有 Nix 包定义，构建与运行尚未验证 | — |
| **Windows** | 查看端（沿用上游源码） | 🚧 构建与打包尚未完成 | — |
| **iOS / iPadOS** | 📱 仅客户端 | ✅ 已上架 — App Store 售价 4.99 美元 | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| **Android**（8.0+） | 📱 仅客户端 | 🚧 开发中 — 原生界面与 MediaCodec；Google Play 同为 4.99 美元 | — |

> **移动端范围：** iOS/iPadOS 与 Android 目前**只规划做客户端功能**。它们连接已授权的
> DeskPort/Sunshine 主机，不提供任何主机端能力——没有屏幕采集、虚拟显示器或本地输入注入。
> 主机端仍然由 macOS、Linux 以及（今后的）Windows 承担。移动端客户端在另一个仓库开发。

## 安装

**iPhone / iPad** — [App Store 上的 DeskPort](https://apps.apple.com/us/app/deskport/id6812389978)，
**售价 4.99 美元，一次买断**。面向已授权 DeskPort/Sunshine 主机的原生客户端，支持直接触控、
办公键盘模式，并可在已绑定的主机上自适应工作区尺寸。Android 客户端上架 Google Play 后
也会是同样的 4.99 美元。

> 💚 **感谢你对 DeskPort 的支持。** 桌面端始终免费并保持开源；移动端的收入是这个项目
> 得以继续的来源。每一笔购买都会回到开发者账号、代码签名、测试设备，以及继续开发所需的
> 时间上。如果你已经购买——真心感谢。如果还没有，提 issue、参与翻译和给出反馈同样珍贵。

**macOS** — 面向 macOS 26 及以上 Apple Silicon 的
[Apple 公证 DMG](https://github.com/keithxc/deskport/releases/download/v0.4.5/DeskPort-0.4.5-macos-arm64.dmg)。
打开 DMG，把 DeskPort 拖入「应用程序」，然后启动即可。主机端功能会在首次使用时请求
「屏幕录制」和「辅助功能」授权。无需另外安装 Sunshine、Qt、Nix 或 Homebrew。

**Linux** — 0.4.5 提供 DEB、RPM、Arch、AppImage、便携包、仅客户端的 Flatpak，以及 Nix/NixOS。使用 `nix run github:keithxc/deskport/v0.4.5`，或按 [Linux 安装指南](docs/LINUX_PACKAGES.md) 安装对应格式。原生包需要 x86_64 与 glibc 2.39+。发布附有 [SHA-256 校验和](https://github.com/keithxc/deskport/releases/download/v0.4.5/SHA256SUMS.txt)与[验证报告](https://github.com/keithxc/deskport/releases/download/v0.4.5/VERIFICATION.txt)。

## DeskPort 是什么

**稳定版本：0.4.5 — 开箱即装的桌面安装包。** DeskPort 把查看端与可选的主机端合并在一个
应用里，共用设备列表、双向绑定与权限控制。macOS 安装包内含 Sunshine 和原生虚拟显示器；
Linux 原生包与 AppImage 内含面向现有桌面的 Sunshine 主机端。Flatpak 只提供客户端；Nix 依然支持。

macOS 上的专用工作区会跟随客户端窗口的可绘制像素尺寸。缩放在 150% 及以上的客户端会请求
2× HiDPI 工作区以获得清晰文字。调整尺寸时视频会短暂重连，期间保留客户端窗口并显示加载动画。
这不是无缝的编码器重配置。

参见[发行说明](docs/RELEASE_0.4.5.md)、[架构说明](docs/ARCHITECTURE.md)与
[macOS 安装指南](docs/MACOS_PACKAGE.md)。持久化的隐藏/显示已经实现；原生长时间会话的验收仍未完成。
共享显示策略支持具备条件的 macOS 与 KDE 主机。选择加入的已绑定 DeskPort 设备会立即共享文本，
并按需获取图片与文件。Windows 打包尚未完成。已知限制见发行说明。

## 在 Linux 上构建与运行

启用 Nix 与 flakes 后：

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

所有第三方依赖都已内置在本仓库中，因此 Nix 构建无需额外下载；见
[docs/VENDORED.md](docs/VENDORED.md)。`nix run . -- --help` 会打印沿用自上游的命令行接口。
连接前先在主机上开始共享并绑定设备。旧版 Sunshine PIN 配对同样可用。本项目不包含、也不会从
Moonlight 导入任何个人主机或配对凭据。
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

## 与上游有何不同

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

各平台的发布状态见上方的[平台进度表](#平台进度)。Linux x86-64 上的 KDE Wayland / AMD
是第一个实际使用目标。

最早的开发路线是通过 Sunshine 实现 Linux → macOS。客户端平台支持与主机端支持是相互独立的：
Mac 主机并不要求使用 DeskPort 的 Mac 客户端。硬件解码、实时输入、画质与召回延迟都需要真实
会话测试，构建成功并不能说明这些。

## 下一个里程碑

让一个会话在 **50 次隐藏/显示循环**中保持连接，把窗口显示在当前工作区，并可靠地交还本地输入。
新鲜画面延迟与后台资源占用要与窗口出现时间分开测量。

验收标准与延后的功能见[路线图](docs/ROADMAP.md)，来源与维护边界见
[上游说明](docs/UPSTREAM.md)。

## 验证

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

冒烟检查使用临时的 XDG 配置/缓存目录与离屏 Qt 平台。它不会与主机配对、启动串流或注入输入。

## 日常桌面操作

关闭窗口后 DeskPort 仍在托盘/菜单栏运行。用**打开 DeskPort** 召回它，用**断开查看端**只结束
当前连接，或用**退出 DeskPort** 结束整个服务。查看端关闭时，本地共享继续运行。

两端绑定设备默认启用纯文本剪贴板共享与系统快捷键捕获。设置更改在重新连接后生效。只共享新的
复制内容，上限 1 MiB；兼容且已启用共享的桌面设备之间可按需获取图片与文件。在桌面指针模式下，键盘路由跟随位于聚焦视频内的指针。
**Ctrl+Alt+Shift+Z** 释放输入；**Ctrl+Alt+Shift+Q** 断开查看端。显式释放后，点击画面内部即可
重新获得输入。系统保留的快捷键取决于桌面合成器。

登录启动与恢复需要处于活动的图形登录会话。在依赖某台电脑进行无人值守访问之前，请先阅读
[验收检查与限制](docs/INPUT_SERVICE_ACCEPTANCE.md)。

## 许可与致谢

DeskPort 是 [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt) 的独立衍生项目，
最初基于 v6.1.0，并非官方的 Moonlight 或 Sunshine 发行版。Moonlight 提供串流基础；
[Sunshine](https://github.com/LizardByte/Sunshine) 被打包进 macOS 与便携式 Linux 主机包，
并由 Linux Nix 包提供。单独安装的 Sunshine 服务保持独立。

GPL-3.0-or-later；见 [LICENSE](LICENSE)、保留的源码声明，以及
[docs/VENDORED.md](docs/VENDORED.md) 中列出的每个内置依赖的许可证。原始文档保留在
[README.upstream.md](README.upstream.md)。
