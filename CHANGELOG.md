## v0.5.2.75 (CN-FORK)

### 🚀 Features
* **游戏磁贴新增 `LZ4` 格式档（ampr 实时解压变体）**：
  * 识别一类特殊的「文件夹」游戏：目录里带 `ampr_*.index` / `ampr_*.pak` 与被改名的 `eboot.bin.orig`（ampr 实时解压，底层资源 LZ4 压缩）。
  * 后端 `game_enum_dump` 新增 `gm_dir_is_lz4()`：当游戏判为裸目录（folder）时再扫一下目录，发现 `ampr_` 前缀条目则把 fmt 从 `folder` 改标 `lz4`。只多一次 opendir，不轮询。
  * 前端 `FMT_LABEL` 加 `LZ4` 徒标（靖蓝底）；mock 加一款 lz4 演示。

### 🐛 Fixes
* **重写 ShadowMount 格式探测（直读人家源码定位根因，不再靠猜）**：
  * 根因：上一版在挂载表里 `strstr("ffpkg")` 永远命中不了——ShadowMountPlus 的 `build_image_mount_point_for_fs` 会 `strip_extension`，挂载点叫 `/mnt/shadowmnt/<镜像名去后缀>_<8位hash>`，扩展名根本没保留；而 `f_mntfromname` 是 `/dev/lvdN`/`/dev/mdN` 块设备。所以格式表永远为空→全回退 PKG。
  * 正解（照 `detect_image_fs_type`/`image_fs_name`/`get_game_info`）：只扫 `/mnt/shadowmnt/` 下的挂载，用 `f_fstypename` 判格式（ufs→ffpkg、exfat/msdos→exfat、含 pfs→ffpfs、`/mnt/shadowmnt/pfsc/` →ffpfsc）；游戏关联优先读挂载点根目录的 `sce_sys/param.json` 拿 titleId（与人家一致），读不到再回退挂载点名的 9 位前缀。首次快照将每个 shadowmnt 挂载的 `to/fstype/fmt` dump 到日志供实机校准。

## v0.5.2.74 (CN-FORK)

### 🐛 Fixes
* **修正游戏磁贴「格式」识别（照搬 ShadowMountPlus 的真实格式集，不再自发明）**：
  * 上一版把格式错发明为 `iso/pfsc/external/mounted/dump`，且只查 `/mnt/sandbox/<TID>*/app0` 挂载、对 app.db 命中的游戏一律写死 `pkg`——招致实机上所有游戏都显示 PKG。
  * 现改为与 ShadowMountPlus 一致的取值：`ffPKG / exFAT / ffpfs / ffpfsc`（镜像）+ `PKG`（内置安装）/ `文件夹`（裸目录）。
  * 后端 `gm_build_mount_map` 改扫 `/mnt/shadowmnt/<镜像名>_<hash>` 镜像挂载，从镜像名后缀认格式、用镜像名前 9 位 TITLE_ID 关联游戏；`gm_fmt()` 对所有游戏（含 app.db 命中的）统一优先取镜像格式，查不到才回退 pkg/folder。仍只在建 60s 快照时调一次 `getmntinfo`，不轮询；保留 `game_fmt:` 日志供实机校准。

## v0.5.2.73 (CN-FORK)

### 🚀 Features
* **游戏磁贴新增主机平台与格式标注**：卡片左上角改为竖排两枚统一尺寸的彩色徽标——上＝平台（`PS4`/`PS5`，由 TITLE_ID 前缀判定：`PPSA/EMUA/PAUS`→PS5、`CUSA/PPJB`→PS4），下＝游戏格式。
* **后端 `/game_list` 新增 `fmt` 字段**：已安装→`pkg`；dump 借助 ShadowMount 挂载表（`getmntinfo` 读 `/mnt/sandbox/<TID>_*/app0` 的挂载来源 `f_mntfromname`）归类 `iso`（.iso/.img）/ `pfsc` / `external`（外置盘）/ `mounted` / `dump`，并输出 `game_fmt` 日志供实机进一步校准分类。`getmntinfo` 仅在 60s 快照重建时调一次，不轮询。
* **徽标颜色用内联 style**：彩色底 + 白字均由内联 style 给定，不挂 `text-white` 类，从而不受 `light-theme.css` 浅色主题全局重映射影响（避免上次“黑底黑字看不清”），也不依赖 Tailwind JIT 是否生成任意色类。

## v0.5.2.72 (CN-FORK)

### 🚀 Features & UX Improvements
* **手机端底部任务栏加长**：补入「PKG安装」与「常用配置」两项，与桌面侧栏顺序对齐（共 6 项）；新项用短标签（PKG / 配置）避免挤压，窄屏可横向滑动。
* **温控页自动刷新**：CPU/GPU/SSD 温度与风扇占空比改为每 5s 只读自动刷新（右上角可关）；周期对齐后端 thermal.c 的 5s 快照缓存，5s 内命中缓存不产生内核调用，与传感器限流规范一致；页面隐藏时自动暂停、请求在途时不叠加。
* **风扇状态文案**：启用并生效后标题后的徒标由「运行中」改为「运行中-当前配置温度XX℃」（取已应用的目标温度）。

### 🐛 Fixes
* **修复浅色主题下游戏磁贴操作按钮/角标看不清**：根因是 `light-theme.css` 的 `[data-theme="light"] .text-white → #17181a !important` 全局重映射，把叠在深色芯片/蓝底角标上的白图标、白字洗成近黑（黑底黑图标近乎隐形）。在「实心彩色底上白字豁免名单」补上 `bg-black/55`、`bg-ps-blue/85`，两种主题下白字均保持白。同时把按钮样式收敛为克制的半透明深色芯片 + 细线白图标（去掉之前的实心黑块/大填充图标）。

## v0.5.2.71 (CN-FORK)

### 🚀 Features & UX Improvements
* **风扇温控改为「启用/应用」两步 + 常驻重发守护**（参考 drakmor/fan_target、owendswang/ps5-fan-control、ShadowMountPlusFan）：
  * **开启后默认失效**：点「启用」只持久化启用位、**不写硬件**；必须设定好目标温度再点「应用」才真正下发、生效（新增 `fan_applied` 标志区分“启用”与“生效”）。
  * **常驻重发守护**：`main.c` 启动时拉起一个只碰 `/dev/icc_fan`（open/ioctl/close）、**不调任何传感器 API** 的后台线程；仅当已启用且已生效时，每 `FAN_INTERVAL` 秒（默认 7s，夹 5~3600）检测硬件目标漂移并自动拉回——对抗进游戏/退游戏/回主界面/待机导致的固件复位。
  * **跨重启/升级保持**：`FAN_CONTROL_ENABLED / FAN_TARGET / FAN_APPLIED / FAN_INTERVAL` 均入 `PldmgrConfig` 持久化；只要不手动关闭，开机即从 config 恢复并自动续上守护。运行态以内存为准、变更即整份 read-modify-write 落盘，避免与 HTTP 写文件竞争。
  * **状态端点改为只读**：打开/刷新温控页不再隐性写硬件，重发完全交给守护。
* **风扇卡 UI 细节**：标题后新增彩色状态徽标（绿「运行中」/ 红「未生效」/ 灰「未接入」）；未启用时滑块/档位/应用置灰不可调；提示按启用/生效三态切换。
* **游戏磁贴操作按钮对比度提升**：图标改纯白 + 底衬加深 + 边框提亮 + 轻微毛玻璃，压在深色封面上更清晰。

### 🔧 Note
* 守护只写 `/dev/icc_fan`，与当初因 2s 轮询传感器 API 导致死机是不同路径；但仍属硬件写，上机首测建议备好 `safe_mode` 哨兵（FTP 建 `/data/nayeplmtools/safe_mode` 即可止血）。

## v0.5.2.70 (CN-FORK)

### 🚀 Features & UX Improvements
* **风扇温控（模型 A，`/fan_control`）**: 温控板块新增「风扇温控」卡片——启用开关 + 目标温度滑块（–85°C）+ 静音/标准/性能档位 + 应用。后端采用 ps5-fan-control 的做法：只通过 `/dev/icc_fan` 的 autoservo ioctl（`GET 0xC01C8F08` → 只改 byte5 目标温度 → `SET 0xC01C8F07` → 回读校验）设定「按温度调速的目标阈值」，交给 PS5 固件自己闭环，payload 不实现风扇曲线、不轮询、不写手动 duty。安全闸：`safe_mode` 短路 + `pthread_mutex` 串行 + 目标强制夹在 60–85°C（全部低于原厂 ~91°C，只会更凉不会更热）；设备打不开时 `supported:false`，前端优雅显示「未接入」。设定持久化到 `nayeplmtools_config.txt`（`FAN_CONTROL_ENABLED` / `FAN_TARGET`）。
  * **已知限制**：进游戏触发 pattern 切换会把目标复位到出厂值；本版无后台守护线程，改为每次打开/刷新温控页时检测漂移并重新下发。真机需已加载 `fan_ctl` 内核模块（`/dev/icc_fan` 存在）。
* **游戏管理宫格化**: 游戏卡片改为大宫格磁贴（图标 1:1 铺满、来源徽标叠右上角），操作按钮移到图标内部底部一排；无图标回退手柄占位图。

### 🔧 Note
* 风扇调速是硬件**写操作**、有整机死机前科，本版虽已落地，但**上机首测请务必留意**：建议先小步验证、备好 `safe_mode` 哨兵（FTP 建 `/data/nayeplmtools/safe_mode` 即可止血）。

## v0.5.2.69 (CN-FORK)

### 🚀 Features & UX Improvements
* **「常用入口」页三段式布局**: 右侧改为纵向三个板块——① 常用入口（沿用原 quicklinks 探活/添加/删除全部功能）② 温控管理 ③ 游戏管理。
* **温控只读板块 (`/thermal_stats`)**: 展示 CPU / GPU / SSD 实时温度与风扇占空比。温度取自 `sceKernelGetCpuTemperature` / `sceKernelGetSocSensorTemperature`，风扇取自 `sceKernelGetCurrentFanDuty`（duty 0..1024 折算百分比）。沿用 v0.5.1.63 `stats.c` 实机验证过的读法，并吸取 `/system_stats` 死机教训：`safe_mode` 短路 + `pthread_mutex` 串行 + 5s 快照缓存，前端进页拉一次 + 手动刷新、绝不轮询。
* **游戏管理板块 (`/game_list` + `/game_icon`)**: 参考 ShadowMountPlus 的「游戏识别」思路，合并两个数据源——已安装（只读解析 `/system_data/priv/mms/app.db` 的 `tbl_contentinfo`）+ 已存在（扫描 `/data`、`/mnt/ext*`、`/mnt/usb*`、`/mnt/shadowmnt` 下含 `sce_sys/param.sfo` 的 TITLE_ID 目录），按 TITLE_ID 去重（已安装优先）。卡片展示图标/名称/TID/来源徽标；图标走独立 `/game_icon?tid=` 读 `icon0.png` 二进制（找不到回 404）。列表同样 mutex + 60s 快照、一次性拉取不轮询。导出存档/导出截图/改名/多标签为后续功能，本版以禁用按钮占位。

### 🔧 Note
* **风扇按温调速**（参考 ps5-fan-control 写 `/dev/icc_fan` 阈值）属硬件**写操作**、有整机死机前科，且依赖 `fan_ctl` 内核模块先行加载；本版本**暂不实现**，留作后续单独确认。

## v0.5.2.68 (CN-FORK)

### 🛠️ Backend & Bug Fixes
* **云端仓库启动预取 (`repository_ensure_fresh`)**: 修正 TTL 判断。开机时 PS5 系统时钟常未同步/回退，导致 `now < last_update`（`delta < 0`）；旧写法把 `delta < 0` 当成可复用缓存，造成「启动不自动刷新、必须手动点」。现改为仅当「时间可信且距上次刷新 < 60 秒」才复用磁盘缓存，其余（时钟回退 `delta < 0`、真正超期 `delta >= 60`、缓存文件缺失）启动一律联网刷新。手动刷新 `/repository_payloads?refresh=1`（`force_refresh=1`）行为不变。

## v0.5.2
- Rebuilt with latest SDK

## v0.5.1

### 🚀 Features & UX Improvements
* **Translations**: Updated localization files across supported languages.
* **New Languages**:
  * **Arabic** (mohamad.z.k28, abdoulacadat, DumDum143, HoussamLovy, c0ncerta, sgs2ofameer)
  * **Thai** (Modmycon, hpokemon776)

**Full Changelog**: [v0.5.0...v0.5.1](https://github.com/itsPLK/ps5-payload-manager/compare/v0.5.0...v0.5.1)

## v0.5.0

### 🚀 Features & UX Improvements
* **Payload Favorites / Pinning**: Added support for pinning favorite payloads to the top of the dashboard with a dedicated edit mode ([#65](https://github.com/itsPLK/ps5-payload-manager/pull/65), @techsociology).
* **Payload Launch Indicator**: Introduced a visual checkmark indicator for payloads launched during the active session ([#44](https://github.com/itsPLK/ps5-payload-manager/issues/44)).
* **New Languages**:
  * **French** (Oblik20, Chronoss)
  * **Japanese** (Jane101KR)
  * **Korean** (Jane101KR)
* **Translations**: Updated localization files across supported languages.

### 🛠️ Backend & API Updates
* **Launch History Tracking**: Added an in-memory session payload history manager and `/history_list` endpoint to track executed payloads.

**Full Changelog**: [v0.4.2...v0.5.0](https://github.com/itsPLK/ps5-payload-manager/compare/v0.4.2...v0.5.0)

## v0.4.2

### 🚀 Features & UX Improvements
* **System Language Support**: Added a "System Default" option in Settings to dynamically follow the OS/browser language ([#69](https://github.com/itsPLK/ps5-payload-manager/pull/69)).
* **New Languages**:
  * **German** (xEasy4Breezy)
  * **Persian** (Epinor)
  * **Spanish** (Slayver95, kXmpX)
  * **Turkish** (Sezar61)
  * **Ukrainian** (Mikeeee)

### 🛠️ Backend & Bug Fixes
* **Build Script**: Fixed an issue where translation files could sometimes be missing when compiling releases from source (@owendswang).

**Full Changelog**: [v0.4.1...v0.4.2](https://github.com/itsPLK/ps5-payload-manager/compare/v0.4.1...v0.4.2)

## v0.4.1
### 🚀 Features & UX Improvements
* **Translations**: Updated existing translations and added new languages.
* **New Languages**:
  * **Italian** (Leon90)
  * **Portuguese (Brazilian)** (matmarson, slipttees, marcusvrb)
  * **Russian** (Akela-1979)

**Full Changelog**: [v0.4.0...v0.4.1](https://github.com/itsPLK/ps5-payload-manager/compare/v0.4.0...v0.4.1)

## v0.4.0

### 🚀 Features & UX Improvements
* **Translations**: Added localization support (i18n) for the web dashboard.
* **New Languages**: 
  * **Chinese Simplified** (@owendswang)
  * **Polish**

### 🛠️ Backend & Bug Fixes
* **Static Linking**: Statically link third-party dependencies in the build script to prevent dynamic linking issues (@owendswang in [#66](https://github.com/itsPLK/ps5-payload-manager/pull/66)).

**Full Changelog**: [v0.3.3...v0.4.0](https://github.com/itsPLK/ps5-payload-manager/compare/v0.3.3...v0.4.0)

## v0.3.3

### 🚀 Features & UX Improvements
* **Cloud Repository Search & Sort**: Added search functionality and sorting options (Category, Update Time, Name) to the Cloud Repository view. Sort preferences are persisted across sessions.
* **Custom Payload Categories**: Added support for custom payload categories in third-party repositories and increased source limits.
* **Media App Category**: Moved the Payload Manager launcher icon to the "Media" tab on the PS5 home screen to keep the Games tab clean (@lucaszhongsj).  
  *Note: You may need to remove your existing Payload Manager shortcut from the home screen first for the new one to correctly appear in the Media tab.*
* **Repository Version Display**: When a custom repository explicitly defines a version, the frontend now displays it instead of trying to guess it from the filename (@lucaszhongsj).

### 🛠️ Backend & Bug Fixes
* **Standby Recovery**: Fixed an issue where the HTTP server would become unresponsive after waking the console from Rest Mode or dropping the network connection. The daemon now cleanly restarts the socket to recover the connection (fixes [#61](https://github.com/itsPLK/ps5-payload-manager/issues/61)).
* **SSL Verification Fixes**: Resolved intermittent SSL verification failures with mbedTLS and added a fallback to insecure downloads on SSL verification failure (fixes [#42](https://github.com/itsPLK/ps5-payload-manager/issues/42)).
* **App Installer Cleanup**: Added missing termination calls for the application installer utility (`sceAppInstUtil`) to ensure it closes properly.
* **iOS File Upload**: Removed the `accept` attribute on the file input to help mitigate an issue where payloads were greyed out for some iOS users ([#39](https://github.com/itsPLK/ps5-payload-manager/issues/39)).

**Full Changelog**: [v0.3.1...v0.3.3](https://github.com/itsPLK/ps5-payload-manager/compare/v0.3.1...v0.3.3)

## v0.3.1

- Fixed an issue where installed payloads were not showing on the dashboard for some users ([#28](https://github.com/itsPLK/ps5-payload-manager/issues/28)).
- Restored the USB scanning limit strictly to the root level.
- Added options to copy instead of move when importing payloads from USB.

**Full Changelog**: [v0.3.0...v0.3.1](https://github.com/itsPLK/ps5_next_menu/compare/v0.3.0...v0.3.1)

## v0.3.0

### 🚀 Features & UX Improvements
* **Multi-Source Payload Repositories**: Added support for adding, removing, and reordering third-party payload repository URLs via a new **Manage Sources** settings view. Payloads within the **Storage Hub** are now grouped by source with dedicated catalogs and origin badges. ([f4c76fb](https://github.com/itsPLK/ps5_next_menu/commit/f4c76fb))
* **Active Processes Management**: Added a dedicated **Active Processes** dashboard tab to view real-time processes, PIDs, and memory footprint, with support for terminating them via SIGKILL. ([631e7de](https://github.com/itsPLK/ps5_next_menu/commit/631e7de))
* **Alphabetical Sorting & Repository Polish**: As the payload repository grew larger, sorting by payload update date became cluttered. Repository payloads are now sorted alphabetically by name (with available updates pinned to the top), and also display payload version/update information. ([293f44b](https://github.com/itsPLK/ps5_next_menu/commit/293f44b))
* **UI & Mobile Layout Polish**:
  * Optimized card layouts on mobile to eliminate wasted vertical space and prevent overlapping text on narrow viewports. ([af72c9b](https://github.com/itsPLK/ps5_next_menu/commit/af72c9b))
  * Refined Settings, Log Viewer, and Manage Sources UI elements to fit narrower viewports and mobile screens. ([b3fe1b5](https://github.com/itsPLK/ps5_next_menu/commit/b3fe1b5))

### 🛠️ Backend, Performance & Refactoring
* **Daemon Codebase Decomposition**: Decomposed monolithic `main.c` and `payload_mgr.c` into 8 separate, single-responsibility modules (`http_server`, `repository`, `sources`, `config`, `log_server`, `json_helpers`, `sha256`, and `process_mgr`) for easier maintainability. ([2189ee9](https://github.com/itsPLK/ps5_next_menu/commit/2189ee9))
* **Disc Player Kill (for BD-JB users)**: Reduced total wait time from 8s to 2s during the disc player suspension process. ([7a001e4](https://github.com/itsPLK/ps5_next_menu/commit/7a001e4))
* **Log Polish**: Polling-related logs (`/list_payloads`, etc.) are now muted under normal operating conditions to prevent spamming output, and explicit logs are printed for process kill requests. ([0bf2b5e](https://github.com/itsPLK/ps5_next_menu/commit/0bf2b5e))

### ⚙️ Documentation & Polish
* **Custom Repositories Guide**: Created a comprehensive documentation guide (`CUSTOM_REPOSITORIES.md`) explaining the JSON structure required to host third-party payload repositories. ([0e0a434](https://github.com/itsPLK/ps5_next_menu/commit/0e0a434))

**Full Changelog**: [v0.2.0...v0.3.0](https://github.com/itsPLK/ps5_next_menu/compare/v0.2.0...v0.3.0)

## v0.2.0

### 🚀 Features & UX Improvements
* **Sidebar State Persistence**: The sidebar's expanded/collapsed state is now persisted in `localStorage` across page reloads. ([7570084](https://github.com/itsPLK/ps5_next_menu/commit/7570084))
* **Settings Page Redesign**: Updated `SettingsView` layout to a responsive grid for better visual alignment and UI consistency. ([2e7de34](https://github.com/itsPLK/ps5_next_menu/commit/2e7de34))
* **UX & Navigation Enhancements**:
  * Added automatic scrolling to the **USB Storage** section when clicking the redirect button in the Autoload section. ([6d14acf](https://github.com/itsPLK/ps5_next_menu/commit/6d14acf))
  * Added automatic scrolling to the top of the main container when changing views. ([75b20ce](https://github.com/itsPLK/ps5_next_menu/commit/75b20ce))
* **Visual Polish**:
  * Added dedicated favicon and application icons. ([c8c380b](https://github.com/itsPLK/ps5_next_menu/commit/c8c380b))
  * Fixed alignment and spacing on the `DonateView`. ([10c4233](https://github.com/itsPLK/ps5_next_menu/commit/10c4233))
  * Refined download modal title. ([7dee294](https://github.com/itsPLK/ps5_next_menu/commit/7dee294))

### 🛠️ Backend & Bug Fixes
* **Directory Cleanup**: Empty directories are now automatically cleaned up after deleting a payload (fixes [#24](https://github.com/itsPLK/ps5-payload-manager/issues/24)). ([5ff5295](https://github.com/itsPLK/ps5_next_menu/commit/5ff5295))
* **USB Payload Loading**: Fixed loading and scanning payloads from the root directory of a USB drive, limiting root scans strictly to the root level. ([bfc4d09](https://github.com/itsPLK/ps5_next_menu/commit/bfc4d09))
* **Daemon Reliability**: Daemon startup now safely replaces existing instances. ([a34ecff](https://github.com/itsPLK/ps5_next_menu/commit/a34ecff))
* **Error Reporting**: Improved launch error reporting back to the user when a payload fails to execute. ([38b1124](https://github.com/itsPLK/ps5_next_menu/commit/38b1124))
* **Log Reduction**: Removed verbose payload scanning status logs under normal operating conditions to prevent spam. ([0380757](https://github.com/itsPLK/ps5_next_menu/commit/0380757))

### ⚙️ CI/CD & Build System
* **GitHub Releases**: Added an on-demand automated release workflow (`release.yml`) to compile, package, and upload the build ELF binary. ([10b786b](https://github.com/itsPLK/ps5_next_menu/commit/10b786b))
* **Docker Build Strategy**: Migrated the SDK image reference to a dedicated `ps5-payload-sdk-pldmgr` tag to avoid image conflicts. ([cfba6b8](https://github.com/itsPLK/ps5_next_menu/commit/cfba6b8))
* **Build Portability**: Made frontend build scripts (`sed` commands) compatible and portable across macOS and Linux environment hosts. ([54c2728](https://github.com/itsPLK/ps5_next_menu/commit/54c2728))
* **Script Housekeeping**: Removed deprecated curl shutdown steps from `deploy.sh`. ([1ebcf98](https://github.com/itsPLK/ps5_next_menu/commit/1ebcf98))

**Full Changelog**: [v0.1.1...v0.2.0](https://github.com/itsPLK/ps5_next_menu/compare/v0.1.1...v0.2.0)

## v0.1.1

- Resolved legacy WebKit (PS5 4.03 / Safari 12) compatibility bugs.
- Improved autoload countdown synchronization.


## v0.1.0

- Initial release
