# HANDOVER — PS5 Payload Manager (CN-FORK)

> 单一事实来源。功能已定稿，本文档面向"新接手的资深工程师"，只保留继续维护所需的骨架与关键约定。
> 详细历史背景见 `FORK-CN.md` / `DEVELOPMENT.md` / `CUSTOM_REPOSITORIES.md`。

## 0. 当前定稿状态
- 分支：`cn-fork`（upstream = `https://github.com/itsPLK/ps5-payload-manager`，已合并上游 v0.5.2，merge 提交 `97bb4dc`）。
- 版本：`MENU_VERSION = 0.5.2.87`（`include/pldmgr.h`）。fork 规则 = 上游基线 + 构建序号，每次调整构建前 +1。
- 工作树：干净（v0.5.2.87 批次已入库，提交 `3f4713b` + `87e6ad3`）。**已出正式包**：`NayePlmTools_0.5.2.87-me.elf` / `-ga.elf`（两个二进制内嵌标题都是真实短哈希 `87e6ad3`，非 DEV）。.87 的配色改动**尚未实机验收**——它是直接取代 .85/.86 两版中间态出的包，那两版从未单独出包。**构建产物不入库**：根目录 `NayePlmTools_*.elf`、`include/assets_*.h`、`frontend/dist/` 均为本地产物，只在本地留存；仓库内唯一二进制是 `third_party/pkg-manager.elf`（唯一 `git add -f` 例外）。
- 近期定型（.64 → .87）：
  - `.64` 主页与全部硬件信息探测下线（`src/stats.c` 删除），启动直达「管理 Payload」页
  - `.65` 「默认云端仓库」总开关 `DEFAULT_REPO_ENABLED`（现默认关，可彻底停用内置官方源）
  - `.66` 界面文案精简（更新日志压缩为一行摘要）
  - `.67` 磁贴双变体（NAYE00001 媒体栏 / NAYE00004 游戏栏）+ `pic0` 背景图；首次运行预置启用来源 `repo.630810.xyz`
  - `.68` 仓库预取 TTL 修复：PS5 时钟回退（delta<0）同样触发刷新
  - `.69` 常用配置页三段式布局（温控只读 + 游戏管理）
  - `.70`/`.71` 风扇温控（模型 A）+「启用/应用」两步式 + 常驻重发守护
  - `.72`–`.77` 游戏磁贴系列：宫格化、平台/格式双徽标、照搬 ShadowMountPlus 格式集、直读 `mount.lnk` 探测、编号行标注体积
  - `.78` 常用入口页大标题独立于侧栏文案（侧栏保持「常用配置」，词条 `entries.title`）
  - `.79` 「游戏管理」独立页；「常用入口」自动收录 app.db 磁贴 http(s) 链接（按需同步 + `src`/`hidden` 抑制表）；JSON 转义中文修复
  - `.80` 「常用入口」自动收录的 127.0.0.1 磁贴链接改写为本机 LAN IP（IP 变化触发重同步自愈）
  - `.81` 「常用入口」本机程序类入口（PKG Manager :8844）点击端口仲裁：端口存活直接打开，否则拉起（elfldr）就绪后再打开
  - `.82` 前端版本握手自动刷新：构建烧入 `__APP_VERSION__`，运行时常驻比对 `/version` 不一致即刷新（换版后旧 SPA 文档残留问题；`sessionStorage` 计数上限 3 防死循环）
  - `.83` 「常用入口」灰点条目通用拉起：按名称（归一化双向前缀、唯一候选）匹配已安装 payload 后 `/loadpayload:` 拉起、探活就绪再同标签打开；先查 `/processes_list` 防重复起实例；探活/收录逻辑未改动，纯前端实现
  - `.84` 同上改开新标签：点击手势内先同步 `window.open('about:blank')` 占位（异步再开必被拦），程序就绪才指向目标地址，面板页不再被顶掉；失败/已在运行则关掉空白标签，被拦时提示再点一次
  - `.85` 整站配色改「藏蓝结构 + 亮橙动作」（只换 token 值、零改类名）+ 骨架层落地（侧栏分组头/选中左竖条、`.page-head`、`.btn` 三档、`.field`）；**中间态，未实机验收、未单独出包**
  - `.86` 配色按实机观感改「墨蓝通透」（样张变体 B）：夜底压到 `#04060B`、卡片 `#0E1424`、描边 `#1E2740`，浅色档画布/卡片纯白、侧栏 `#F6F8FC`；蓝味收敛到线条与选中底，高饱和彩色只给图标与状态标签（薄荷绿 `#2EE6A8` / 暖黄 `#FFD84D` / 鲜红 `#FF6B6B`）；动作橙深色档 `#FF7A2E`、浅色档实心 `#F25C05`（浅底保白字对比），浅色下 `text-red-*` 由跟橙改回真红 `#A91F22`；**同样只动令牌层三个文件；中间态，未实机验收、未单独出包**
  - `.87` 全站「一档一色」收口（对应「页面颜色太杂、同类型部件不要有多个背景色、浅色字太灰暗、选中不明显」）：**每套主题只有 `canvas`/`rail`/`card`/`inset` 四档中性底 + 一档选中底**。做法是在 `index.css` 末尾加一层「收口层」，把同类部件上混用的 `bg-white/5|10|20|[0.01~0.03]`、`bg-black/20|40|50`、`bg-zinc-600` 一律映射到 `var(--color-inset)`，`border-white/*` 并到一根线，动作色薄染 `bg-ps-blue/5~20` 四档并一档；因为映射目标是变量，**浅色档只翻变量、`light-theme.css` 里那套 bg/border 类名重映射整段删掉**（从此不存在「骨架层一个色、其余另一个色」）。选中态统一 S2 = 实心橙芯片（新骨架类 `.sel-chip`）：侧栏选中（去掉左侧橙竖条）、开关开启槽、温控「已启用」与档位、仓库当前优先序号、overlay 当前执行行。实心橙底上的字改走 `--color-sel-fg`（深档墨字 `#1B0E02`、浅档白字）。浅色文字阶整档提黑（次要 `#3A4661`、辅助 `#46536E`、更淡 `#5A677F`；占位符 `#8B95A8` 是唯一刻意留淡的一档）。深色档主内容区再沉一档（卡片 `#0A101D`、内嵌件 `#0F1728`、侧栏 `#070A12`、线 `#1B2438`）。顺手把 `amber`/`green` 两个一直吃 Tailwind 默认色的色族并进暖黄/薄荷绿，并删掉 `ink` 色阶与一批零引用的死变量。**新增页面时的规矩：底色只有这 4+1 档，别再用 `bg-white/N` 挑浓淡**；豁免不收口的是 `bg-white`（旋钮/进度条）、`bg-black/55~80`（封面叠层与模态遮罩，两套主题都保持深色）、`bg-ps-blue/85`（封面角标）

## 1. 仓库拓扑（工作区 d:\pldmanager）
| 目录 | 角色 |
| --- | --- |
| `ps5-payload-manager/` | **主项目**：C 后端守护进程 + React 前端，编译成单个 PS5 payload ELF，内置 HTTP 面板（`:8911`）。 |
| `ps5-payload-manager/frontend/src/locales/` | git 子模块 `itsPLK/ps5-payload-manager-translations`，Crowdin 单向同步，**fork 内禁止改**。 |
| `ps5-payload-manager/third_party/pkg-manager.elf` | 内置 PKG Manager 基线二进制（黑盒），需 `git add -f` 入库。 |
| `ps5-payloads-mirror/` | 上游 payload 镜像仓库（GitHub Pages 静态 `payloads.json` + 每日 cron）；内置官方源 `REPOSITORY_SOURCE_URL` 指向它（默认开关关闭）。 |
| `icon-concepts/` | 图标设计素材与脚本（非构建产物）。 |
| `third_party/` (工作区级) | `LibProsperoPKG`、`ps5-browser-app` 等外部依赖。 |

> **已退役（2026-10）**：`ps5-pkg-installer/`（NayeElfBoot 磁贴 PKG 打包工程）已从工作区删除，唯一副本为 `d:\pldmanager\ps5-pkg-installer.retired.zip`（全量 178 条目，含 `build/` 下各 variants 试验包与 release 成品 PKG）。主机侧 `/data/pkg` 当时已是空目录。**后续维护、需求分析与问题排查一律不再考虑该工程与「制作 PKG 安装包」这条线**；面板内的 PKG Manager 集成（`src/pkgmanager.c` + `pkgmanager_blob.S` + `:8844` iframe）不在此列，仍然保留。

## 2. 架构（一句话）
一个跑在 PS5 用户态的 C HTTP 服务（libmicrohttpd + libcurl + mbedTLS），把 `frontend/dist/index.html`（单文件 React 产物）作为资源头嵌入 ELF，通过 REST/SSE 路由暴露 payload 管理、仓库安装、自启配置、进程列表、快速入口、游戏磁贴、温控、PKG 管理等能力。

### 2.1 C 后端（`src/` + `include/`）
- `main.c` 启动装配；`http_server.c` 路由分发（路由宏集中在 `include/pldmgr.h`）。
- `payload_mgr.c` payload 列表/加载；`repository.c`+`sources.c` 远程仓库与多来源管理（TTL 预取、首次预置清单）；`autoload.c` 自启配置（`/data/nayeplmtools/autoload.txt`，并同步 `/data/ps5_autoloader/`）。
- `process_mgr.c` 进程枚举；`ps5_launcher.c`/`app_installer.c` 应用启动与磁贴安装（双变体）；`pkgmanager.c`+`pkgmanager_blob.S` 内置 PKG Manager 独立进程集成。
- `game_mgr.c` 游戏磁贴扫描、格式探测（直读 `mount.lnk`/`mount_img.lnk`）与 deeplink 枚举（app.db 列序按建表 SQL 实时定位）；`thermal.c` 温控只读状态；`fan_control.c` 风扇温控（模型 A + 常驻守护线程）。
- `quicklinks.c` 常用入口探活 + app.db 磁贴链接按需同步（stat+IP 闸门 + `src`/`hidden` 字段；收录时 127.0.0.1 改写为本机 LAN IP）；`history_mgr.c` 会话历史；`notification.c` 启动通知；`log_server.c` 日志；`config.c` 配置持久化；`sha256.c`/`json_helpers.c`/`utils.c` 基础设施。
- **铁律**：fork 不改 C 后端的路由/配置键/文件路径，保证升级上游时行为一致。

### 2.2 React 前端（`frontend/src/`）
- `App.jsx` 布局/路由/侧栏菜单；`components/views/*` 各功能页（GameManageView 游戏管理、GameSection 游戏宫格、ThermalSection 温控、StorageHub、Autoload、ActiveProcesses、LogViewer、Settings、ManageSources、CommonEntries、MoveFromUsb、Donate、PkgManagerFrame...）。
- `i18n.js` 加载 locale（子模块）+ **合并 `locales-overlay/<lang>.json`（后者优先）**；完成度 <70% 的语言不注册。
- `theme/`：`light-theme.css`（暗→亮类名重映射，零改上游组件）、`cn-typography.css`（中文排版）、`useTheme.js`（localStorage `pldmgrTheme`）、`ThemeToggle.jsx`。

## 3. 构建与部署
前端产物嵌入 ELF，分步：
```bash
# 0) 子模块：git submodule update --init（locales 翻译）
# 1) 前端（Windows 用 npm.cmd；本地联调 npm.cmd run dev:mock → mock:8081 + vite:5173）
make frontend-build    # 生成 frontend/dist：替换占位符 [[TITLE_PLACEHOLDER]]/[[BUILD_DATE]]，并烧入 __APP_VERSION__（版本握手用；构建时自动读 pldmgr.h MENU_VERSION，env APP_VERSION 可覆写，dev 恒为 'dev' 跳过）
# 2) ELF（WSL2 + /opt/ps5-payload-sdk；Makefile CC=/opt/ps5-payload-sdk/bin/prospero-clang）
make all               # 主 ELF NayePlmTools_<version>.elf（媒体栏变体 NAYE00001）
bash tools/build_tiles.sh   # 双磁贴变体一次出齐：-me(NAYE00001 媒体栏) + -ga(NAYE00004 游戏栏)，并自校验
./deploy.sh [PS5_IP]   # shutdown → 构建 → socat 推送 :9021；面板 http://[PS5_IP]:8911
```
- 防缓存：`cache.appcache` 每次注入构建时间戳触发 AppCache 更新。
- 主 ELF 内嵌版本签名 `PLDMGR_VER:<版本>`（`src/main.c`），可用于核对任意产物的实际版本。
- 约定：界面/功能改动**定稿确认后**再出正式构建包；中途验证走 dev-server 预览。

## 4. 版本号同步（全链路）
`include/pldmgr.h` 的 `MENU_VERSION` 是唯一真源；`make` 会把它注入前端标题与产物文件名，运行时 `/version` 路由、启动通知与通知文案都读同一宏。**仍需手动同步的 4 处**：`frontend/mock-server.js`（mock 的 /version）、`DonateView.jsx`（关于页兜底串）、`locales-overlay/en-US.json`、`locales-overlay/zh-CN.json`（`settings.donate_desc`）。**出正式包入口在构建脚本层，勿手改单个产物。**

## 5. Fork 维护约定
1. 所有 fork 专属改动打 `// [CN-FORK]` 注释标记；中文措辞/新增 key 只进 `locales-overlay/`，**绝不改 locales 子模块**。
2. UI 配色分两层，都不改上游组件类名：**先改令牌层**（`tailwind.config.js` 的 `theme.extend.colors` + `index.css` 的 `:root` CSS 变量）——类名保留、值替换，全站一次生效；**再补重映射**（`theme/light-theme.css`）把上游写死的暗色工具类映射为浅色语义色。骨架层（`.nav-group-title`/`.nav-item`/`.page-head`/`.btn-*`/`.field`，定义在 `index.css` 末尾）颜色一律走 CSS 变量，浅色只在 `light-theme.css` 翻变量，不写 `!important`。
3. 升级上游：`git fetch upstream && git checkout main && git merge --ff-only upstream/main`，再 `git checkout cn-fork && git rebase main`。冲突集中在带 `[CN-FORK]` 的局部插入块。
4. third_party 基线 ELF 需 `git add -f`；`include/assets_*.h`、`frontend/dist/`、根目录 `*.elf` 属构建产物，始终不入库。
5. Commit 规范：`[CN-FORK] v<版本号>: <描述>`；**只本地提交，不配置 remote、禁止 push**。

## 6. 关键约定 / 易踩坑（速查）
- **风扇温控（安全红线）**：只通过 `/dev/icc_fan` autoservo ioctl 设「目标温度」交固件闭环（GET `0xC01C8F08` / SET `0xC01C8F07`，28 字节对象，byte[2]=selector、byte[5]=目标 °C）。必须 GET → 只改 byte5 → SET 整个对象 → 回读校验，**绝不全零 SET**；目标夹 60~85 °C。进游戏/待机会复位目标，靠常驻守护线程（`FAN_INTERVAL` 默认 7s）拉回；`safe_mode` 短路。
- **自启**：`autoload.txt` 延时行 `!` 前缀 + 前导空行需自愈规范化；安装 payload 后同步登记到 `ps5_autoloader`（全局覆盖）。
- **网络**：用户态代码用 socket 前必须 `sceNetCtlInit` + `sceUserServiceInitialize(256)`。
- **传感器/硬件**：`sceKernelIccGet*` 会 crash、Sony 传感器 API 非可重入 —— 这是 `.64` 下线硬件信息的根因；`/dev/dce` ioctl 默认停用 + `safe_mode` 哨兵文件用于防死机。
- **提权**：官方范式只改 authid/caps 且用完还原（勿改 uid/prison/rootdir）。
- **前端**：Vite 未代理的 API 会被 SPA fallback 吞成 index.html → 页面转圈；`!text-white` important 变体、tailwind 字面色需单独映射。
- **C 字符串**：分段字符串字面量不要拆断 JSON key（会导致设置页开关失灵）。

## 7. 接手后的下一步（待定，非阻塞）
- 功能已定稿，后续以维护/上游同步为主；如需重启硬件信息，须先解决传感器可重入性与死机哨兵。
