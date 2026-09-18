<p align="center">
  <img src="app/res/deskport.svg" alt="DeskPort" width="128" height="128">
</p>

<h1 align="center">DeskPort</h1>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="README.zh-CN.md">简体中文</a> ·
  <b>繁體中文</b> ·
  <a href="README.ja.md">日本語</a>
</p>

<p align="center">
  以 Moonlight 與 Sunshine 為基礎的遠端桌面工作區。<br>
  讓遠端桌面常駐背景，一個動作把它帶到目前的工作區，
  收起時也不必重新連線。
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.4.1"><img alt="桌面版本" src="https://img.shields.io/badge/desktop-0.4.1-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="LICENSE"><img alt="授權" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## 平台進度

| 平台 | 角色 | 進度 | 取得 |
| --- | --- | --- | --- |
| **macOS**（Apple Silicon，macOS 26+） | 檢視端 + 主機端 + 虛擬顯示器 | ✅ 穩定版 — 0.4.1，已通過 Apple 公證 | [DMG](https://github.com/keithxc/deskport/releases/download/v0.4.1/DeskPort-0.4.1-macos-arm64.dmg) |
| **Linux x86-64** | 檢視端 + 主機端 | ✅ 已發佈 — Nix 為 0.4.1；DEB / RPM / Arch / AppImage / Flatpak 為 0.4.0 | [發佈頁](https://github.com/keithxc/deskport/releases) · [指南](docs/LINUX_PACKAGES.md) |
| **Linux ARM64** | 檢視端 + 主機端 | 🧪 僅有 Nix 套件定義，建置與執行尚未驗證 | — |
| **Windows** | 檢視端（沿用上游原始碼） | 🚧 建置與封裝尚未完成 | — |
| **iOS / iPadOS** | 📱 僅客戶端 | ✅ 已上架 — App Store 售價 4.99 美元 | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| **Android**（8.0+） | 📱 僅客戶端 | 🚧 開發中 — 原生介面與 MediaCodec；Google Play 同為 4.99 美元 | — |

> **行動平台範圍：** iOS/iPadOS 與 Android 目前**只規劃客戶端功能**。它們連線到已授權的
> DeskPort/Sunshine 主機，不提供任何主機端能力——沒有螢幕擷取、虛擬顯示器或本機輸入注入。
> 主機端仍由 macOS、Linux 以及（日後的）Windows 負責。行動端客戶端在另一個儲存庫開發。

## 安裝

**iPhone / iPad** — [App Store 上的 DeskPort](https://apps.apple.com/us/app/deskport/id6812389978)，
**售價 4.99 美元，一次買斷**。這是連線到已授權 DeskPort/Sunshine 主機的原生客戶端，支援直接觸控、
辦公鍵盤模式，並可在已綁定的主機上自動調整工作區尺寸。Android 客戶端上架 Google Play 後
也會是同樣的 4.99 美元。

> 💚 **感謝你支持 DeskPort。** 桌面端始終免費且開放原始碼；行動端的收入是這個專案得以繼續的來源。
> 每一筆購買都會回到開發者帳號、程式碼簽章、測試裝置，以及持續開發所需的時間上。
> 如果你已經購買——由衷感謝。如果還沒有，提出 issue、參與翻譯與給予回饋同樣可貴。

**macOS** — 適用於 macOS 26 以上 Apple Silicon 的
[Apple 公證 DMG](https://github.com/keithxc/deskport/releases/download/v0.4.1/DeskPort-0.4.1-macos-arm64.dmg)。
開啟 DMG，把 DeskPort 拖進「應用程式」後啟動即可。主機端功能會在首次使用時要求
「螢幕錄製」與「輔助使用」授權。不需要另外安裝 Sunshine、Qt、Nix 或 Homebrew。

**Linux** — 0.4.1 只提供 Nix 與 NixOS：`nix run github:keithxc/deskport/v0.4.1`，
或參考下方的[在 Linux 上建置與執行](#在-linux-上建置與執行)。此版本改變的是相依套件的內建方式，
而非客戶端本身，因此
[0.4.0 的 DEB、RPM、Arch、AppImage 與 Flatpak 套件](https://github.com/keithxc/deskport/releases/tag/v0.4.0)
對這些格式仍是目前版本；支援的系統與首次使用設定請見
[Linux 安裝指南](docs/LINUX_PACKAGES.md)。原生套件需要 x86_64 與 glibc 2.39 以上。

## DeskPort 是什麼

**穩定版本：0.4.1 — 可直接安裝的桌面套件。** DeskPort 把檢視端與選用的主機端整合在同一個
應用程式中，共用裝置清單、雙向綁定與權限控制。macOS 套件內含 Sunshine 與原生虛擬顯示器；
Linux 原生套件與 AppImage 內含供現有桌面使用的 Sunshine 主機端。Flatpak 只提供客戶端；Nix 持續支援。

macOS 上的專用工作區會跟隨客戶端視窗的可繪製像素尺寸。縮放在 150% 以上的客戶端會要求
2× HiDPI 工作區，讓文字更銳利。調整尺寸時視訊會短暫重新連線，期間保留客戶端視窗並顯示載入動畫。
這並不是無縫的編碼器重新設定。

請見[發佈說明](docs/RELEASE_0.4.1.md)、[架構說明](docs/ARCHITECTURE.md)與
[macOS 安裝指南](docs/MACOS_PACKAGE.md)。持續性的隱藏/顯示已經實作；原生長時間工作階段的驗收仍未完成。
共用顯示策略支援條件符合的 macOS 與 KDE 主機。選擇加入的已綁定 DeskPort 裝置會立即共用文字，
並在需要時取得圖片與檔案。Windows 封裝尚未完成。已知限制請見發佈說明。

## 在 Linux 上建置與執行

啟用 Nix 與 flakes 後：

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

所有第三方相依套件都已內建於本儲存庫，因此 Nix 建置不需額外下載；請見
[docs/VENDORED.md](docs/VENDORED.md)。`nix run . -- --help` 會列出沿用自上游的命令列介面。
連線前請先在主機開始共用並綁定裝置。舊版 Sunshine PIN 配對同樣可用。本專案不含、也不會從
Moonlight 匯入任何個人主機或配對憑證。
新建的手動位址預設使用 DeskPort 的連接埠 `48989`。若不同，請填入主機共用頁面顯示的連接埠；
若要明確連線到預設的獨立 Sunshine 安裝，可使用 `host:47989`。已儲存或探索到的端點會保留各自的連接埠。

可編輯的原生建置：

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

上游專案的檔名維持不變，讓這個分支仍便於審閱。

## 與上游有何不同

- 獨立的 `deskport` 執行檔、`DeskPort` Qt 設定命名空間，以及
  `io.github.keithxc.DeskPort` Linux 應用程式 ID。
- 預設為視窗化串流與絕對指標控制。
- 失去焦點時靜音；預設停用遊戲最佳化、手把滑鼠、多手把模式、背景手把輸入與 Discord 狀態。
- 這個獨立應用程式不會出現上游 Moonlight 的更新提示。
- 鎖定的 Nix 環境與一套 Linux 建置流程。

桌面介面提供裝置、共用與設定頁面，含語言選擇與獨立的主機權限。關閉檢視端會保留其工作階段
並開啟裝置清單。使用中的裝置會提供「返回桌面」，其他裝置在目前工作階段中斷前只顯示詳細資訊。
可以釘選常用裝置，並在精簡清單與卡片之間切換。外觀跟隨系統，也可強制淺色或深色。

## 平台範圍

各平台的發佈狀態請見上方的[平台進度表](#平台進度)。Linux x86-64 上的 KDE Wayland / AMD
是第一個實際使用目標。

最早的開發路線是透過 Sunshine 達成 Linux → macOS。客戶端平台支援與主機端支援彼此獨立：
Mac 主機並不需要 DeskPort 的 Mac 客戶端。硬體解碼、即時輸入、畫質與喚回延遲都需要真實
工作階段測試，建置成功並不足以證明這些。

## 下一個里程碑

讓單一工作階段跨越 **50 次隱藏/顯示循環**保持連線，把視窗顯示在目前的工作區，並可靠地交還
本機輸入。新畫面延遲與背景資源使用需與視窗出現時間分開量測。

驗收標準與延後的功能請見[藍圖](docs/ROADMAP.md)，來源與維護邊界請見
[上游說明](docs/UPSTREAM.md)。

## 驗證

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

煙霧測試使用暫時的 XDG 設定/快取目錄與離屏 Qt 平台。它不會與主機配對、啟動串流或注入輸入。

## 日常桌面操作

關閉視窗後 DeskPort 仍會在系統匣/選單列執行。用**開啟 DeskPort** 喚回它，用**中斷檢視端**
只結束目前的連線，或用**結束 DeskPort** 終止整個服務。檢視端關閉時，本機共用會繼續。

兩端綁定裝置預設啟用純文字剪貼簿共用與系統快速鍵擷取。設定變更在重新連線後生效。只會共用新的
複製內容，上限 1 MiB；圖片與檔案不會傳輸。在桌面指標模式下，鍵盤路由會跟隨位於聚焦視訊內的指標。
**Ctrl+Alt+Shift+Z** 釋放輸入；**Ctrl+Alt+Shift+Q** 中斷檢視端。明確釋放後，點按畫面內部即可
重新取得輸入。系統保留的快速鍵取決於桌面合成器。

登入啟動與復原需要有作用中的圖形登入工作階段。在依賴某台電腦進行無人值守存取之前，請先閱讀
[驗收檢查與限制](docs/INPUT_SERVICE_ACCEPTANCE.md)。

## 授權與致謝

DeskPort 是 [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt) 的獨立衍生專案，
最初以 v6.1.0 為基礎，並非官方的 Moonlight 或 Sunshine 發行版。Moonlight 提供串流基礎；
[Sunshine](https://github.com/LizardByte/Sunshine) 內含於 macOS 與可攜式 Linux 主機套件，
並由 Linux Nix 套件提供。另外自行安裝的 Sunshine 服務維持獨立。

GPL-3.0-or-later；請見 [LICENSE](LICENSE)、保留的原始碼聲明，以及
[docs/VENDORED.md](docs/VENDORED.md) 中列出的每個內建相依套件的授權。原始文件保留於
[README.upstream.md](README.upstream.md)。
