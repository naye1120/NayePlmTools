# HANDOVER — PS5 Payload Manager (CN-FORK)

> 单一事实来源。功能已定稿，本文档面向"新接手的资深工程师"，只保留继续维护所需的骨架与关键约定。
> 详细历史背景见 `FORK-CN.md` / `DEVELOPMENT.md` / `CUSTOM_REPOSITORIES.md`。

## 0. 当前定稿状态
- 分支：`cn-fork`（upstream = `https://github.com/itsPLK/ps5-payload-manager`，已合并上游 v0.5.2，merge 提交 `97bb4dc`）。
> - 版本：`MENU_VERSION = 0.5.2.106`（`include/pldmgr.h`）。fork 规则 = 上游基线 + 构建序号，每次调整构建前 +1。
- 工作树：`.88`–`.91` 是 `.87` 契约内的观感微调，`.92`–`.97` 是**骨架/交互重构六批**（配色令牌一行未动），`.98` 是这批重构的**运行期崩溃清账**（四处整站死 + mock 缺字段 + 一处上游错词条），`.99` 是**账号工具（PS5 Account Tools）源码并入主 ELF + 动作橙转档 + 账号板块 9 处实测修订**（正式出包版 `NayePlmTools_0.5.2.99-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `aca3852`），`.100` 是**整套换参考页配色（灰墨 + 蓝主色）+ 改名补第二层 + 去操作前确认弹窗 + 磁贴自适应**（正式出包版 `NayePlmTools_0.5.2.100-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `f831e98`，两包各 9,858,824 字节、titleId 自检 NAYE00001/NAYE00004 通过），`.101` 是**磁贴再放大 1/4 + PKG 卡搬到常用配置底部 + 次要按钮档转紫 + 改名逐层取证**（正式出包版 `NayePlmTools_0.5.2.101-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `2dc3088`，两包各 9,858,824 字节、titleId 自检 NAYE00001/NAYE00004 通过）。`.100` 的包、`.99` 的包与更早的现行包 `NayePlmTools_0.5.2.87-me.elf` / `-ga.elf`（哈希 `87e6ad3`）都已过期。`.85`/`.86` 是中间态，从未单独出包。`.102` 是**修掉 `.101` 改名取证代码的堆溢出（整 payload 崩）+ 按钮按功能转实心六色（样张 A + I2）+ 页头去粘顶**（正式出包版 `NayePlmTools_0.5.2.102-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `be7fa8f`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过），`.101` 的包已过期。`.103`–`.105` 是**纯前端/文档改动、从未单独出包**（`.103` 收敛上色 + 深空黑 + 舒展留白；`.104` 账号板块按钮状态分色 + 主色换朱橙 `#F43E06`；`.105` 同步按钮补图标 + PIN 进度条重做），`.106` 把这四轮**一次性出包**（正式出包版 `NayePlmTools_0.5.2.106-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `0e9afb2`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过），`.102` 的包已过期。**构建产物不入库**：根目录 `NayePlmTools_*.elf`、`include/assets_*.h`、`frontend/dist/` 均为本地产物，只在本地留存；仓库内唯一二进制是 `third_party/pkg-manager.elf`（唯一 `git add -f` 例外）。
- 近期定型（.64 → .100）：
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
  - `.87` 全站「一档一色」收口（对应「页面颜色太杂、同类型部件不要有多个背景色、浅色字太灰暗、选中不明显」）：**每套主题只有 `canvas`/`rail`/`card`/`inset` 四档中性底 + 一档选中底**。做法是在 `index.css` 末尾加一层「收口层」，把同类部件上混用的 `bg-white/5|10|20|[0.01~0.03]`、`bg-black/20|40|50`、`bg-zinc-600` 一律映射到 `var(--color-inset)`，`border-white/*` 并到一根线，动作色薄染 `bg-ps-blue/5~20` 四档并一档；因为映射目标是变量，**浅色档只翻变量、`light-theme.css` 里那套 bg/border 类名重映射整段删掉**（从此不存在「骨架层一个色、其余另一个色」）。选中态统一 S2 = 实心橙芯片（新骨架类 `.sel-chip`）：侧栏选中（去掉左侧橙竖条）、开关开启槽、温控「已启用」与档位、仓库当前优先序号、overlay 当前执行行。实心橙底上的字改走 `--color-sel-fg`（深档墨字 `#1B0E02`、浅档白字）。浅色文字阶整档提黑（次要 `#3A4661`、辅助 `#46536E`、更淡 `#5A677F`；占位符 `#8B95A8` 是唯一刻意留淡的一档）。深色档主内容区再沉一档（卡片 `#0A101D`、内嵌件 `#0F1728`、侧栏 `#070A12`、线 `#1B2438`）。顺手把 `amber`/`green` 两个一直吃 Tailwind 默认色的色族并进暖黄/薄荷绿，并删掉 `ink` 色阶与一批零引用的死变量。**新增页面时的规矩：底色只有这 4+1 档，别再用 `bg-white/N` 挑浓淡**；豁免不收口的是 `bg-white`（旋钮/进度条）、`bg-black/55~80`（封面叠层与模态遮罩，两套主题都保持深色）、`bg-ps-blue/85`（封面角标）。**注意：自 `.90` 起「板块容器」不再占 `card` 档，见下一条**
  - `.88` **只调淡 `rail` 与 `inset` 两档**（用户圈定「侧栏 + 已安装 Payload 那几行」，深浅两套主题同步；上一版把 card 也整站半透明、合成后与 .87 几乎无差别，方向错了已回退）。`inset` 改真半透明同色相 tint：深 `rgba(74,106,181,.035)`（压在卡片上 ≈ `#0C1322`，原 `#0F1728`）、浅 `rgba(12,69,155,.042)`（合成 ≈ `#F5F7FB`，原 `#EEF2F8`），悬停档同比例；`rail` 淡到几乎与画布同色（深 `#05080F`、浅 `#FBFCFE`），侧栏分界交给发丝描边。**`rail` 必须保持实色**——它兼任模态面板 `.glass-panel` 与粘顶条的浮层底，透明会让滚过的内容透出来（同理 overlay 粘顶条从 `bg-black/20` 改 `bg-ps-surface`）。`canvas` 与 `card` 不动。**要再调通透度就改这三个 alpha**：`index.css` 的 `:root` 与 `light-theme.css` 的 `:root[data-theme="light"]` 各一处。
  - `.89` **内嵌件再调透 + 图标盒去矩形底 + 图标颜色提鲜**（用户：「深色的管理 Payload 没改 / 浅色所有地方透明度依然不够 / 截图这种地方不要图标的矩形背景颜色，图标本身颜色可以鲜艳一点」）。`inset` alpha 再降一半：深 `.018`（压卡片 `#0B1220`，与卡片只差一档，「管理 Payload」里的行与计数徽章的灰块基本化掉，层次交给发丝线）、浅 `.02`（合成 `#FAFBFD`，接近纯白），悬停深 `.07` / 浅 `.05`。图标盒（板块头、条目、设置项、来源管理、从 USB 移动页）的 `bg-ps-blue/10`、`bg-white/5`、`bg-ps-blue/20+border` 矩形底全部去掉，只留 `p-*` 与圆角占位，盒上的 `group-hover:bg-*` 也一并删（否则悬停把矩形画回来）；原本靠悬停才变橙的图标改为恒常鲜橙。动作色「文字/图标」档提鲜：浅 `#B8410A`→`#F25C05`（`light-theme.css` 三处字面量 + 变量），深 `#FF9152`→`#FF7A2E`（注意 `index.css:23` 老内核兜底那条 `.text-ps-blue` 是写死的，改这里才生效）。**图标类部件的现行规矩：只上色不上底**——新加图标一律直接 `text-ps-blue`，不要再套 `bg-*/10 rounded-*` 的方块。
  - `.90` **板块容器并档：`.panel-card` 底色从 `card` 改读 `inset`**（用户拿两张截图反问「这两个背景一样吗」，并指定「`bg-white/[0.03]`、`panel-card` 改成和其他的相同」）。病灶：`.panel-card` 全站只有 `StorageHub.jsx` 那三个板块在用（深 `#0A101D`），其它页的板块——设置 9 块、温控、游戏管理、常用入口、PKG 卡——都是上游手写 `bg-white/[0.03]` + `rounded-2xl` + `p-5`，在 `.87` 收口层里被当成内嵌件档（深合成 ≈ `#05080E`、浅 `#FAFBFD`）：同一个「板块」角色两套底，相差 R5/G8/B15。**现在的角色表：板块容器 = `inset`（`card` 档实际退役，只剩 `bg-ps-card` 这类字面类还在读它），板块内的行/输入框/徽章同为 `inset`，靠双层叠加与描边区分。** 只改 `index.css` 的 `.panel-card` 一行；圆角、描边、结构与类名未动。
  - `.91` 补 `.90` 漏掉的一块：云端仓库「多来源」列表容器（`StorageHub.jsx` 里 `multiSources ? …` 那一整块）原用字面类 `bg-ps-card`，还在读 card 档实色 `#0A101D`，在已经变平的板块里凸起一块；改成 `bg-white/[0.03]` 走同一条 inset 通道。**至此全站板块容器只剩一档底，`card` 档零组件引用**（`index.css` 里 `.bg-ps-card → var(--color-card)` 那条映射保留，只为上游若再写这个类名时仍跟主题）。

> **骨架重构 `.92`–`.97`（2026-10，用户放行「配色保持现有方案，其他都可以弄」，目标：PC / PS5 电视 / 手机三端操作逻辑顺畅）**
> - `.92` 焦点可见（`:focus-visible` 2px 动作色环 + 老内核由 JS 挂 `.is-focus`）；新增 `utils/spatialNav.js`：电视端按**几何位置**做方向键空间导航（同列优先、垂距 3 倍惩罚、屏内无候选就滚 0.7 屏、OK 落在 `div` 行上转成 `click()`、返回键派发 `ps5-back` 交外壳裁决：先关浮层再退视图，安装中不许退）；尺度进令牌层 `--ctl-h`（PC 44 / 电视 68px）与 `--row-h`（56 / 88px），由 `html[data-device="ps5"]` 切换（`App.jsx` 按 `isPS5` 写属性）。
> - `.93` 打断式与反馈归一：`ui/Modal.jsx`（footer 改 `.stack-x`、恒有关闭钮 + Esc、面板可滚、打开即聚焦、`dismissible={false}` 给不可打断浮层）、`ui/Toast.jsx`（去掉组件级 `fixed`，定位交给容器；成功 4s / 错误 9s + 手动关）、`showConfirm(title, msg, onConfirm, danger)` 按可逆性分档。
> - `.94` 结构归一：新增 `ui/Section.jsx`、`ui/PageHead.jsx`（粘顶，实色 `rail` 底 + 负外边距出血；限列宽的页面传 `bleed={false}`）、`ui/EmptyState.jsx`、`ui/LoadingState.jsx`、`ui/Switch.jsx`（全站唯一开关，尺寸读 `--switch-w/h`、`--knob-d`）；各视图页头从卡片里搬到页面级并粘顶；设置页语言选择器从「隐形原生 `<select>` 盖在假按钮上」改成真 `<select class="field">`；「更新日志」45 段写死副本改 `CHANGELOG` 数组 + 共用 `Modal`。
> - `.95` **老内核 flex gap 清零**：PS5 内核不支持 flex 盒的 `gap`（`grid` 的 `gap` 支持），ThemeToggle / 温控 / 管理 Payload / 游戏 / 支持作者 / PKG 内嵌页等处的 `flex … gap-*` 全部改 `* + *` 外边距。
> - `.96` 电视端补齐核心操作 + 清账：来源管理页不再「PS5 只给一张二维码」，三端共用同一份 CRUD（QR 降为页尾次级板块）；导航收敛为一份 `NAV_ITEMS`/`NAV_GROUPS`（侧栏与手机底部栏同源）；删死代码 `ui/PayloadButton.jsx` 与 `favoritePayloads`/`toggleFavorite`/`moveFavorite`/`isFavoriteEditMode`/`launchHistory`/`refreshHistory`。
> - `.97` 互斥分段控件收成一个 `ui/Segmented.jsx`（`role=radiogroup`/`radio`，轨道 `inset` 档 + 一根线，选中档仍是全站唯一 `.sel-chip`，轨道用 `grid` 等宽列 + `gap`，不用 flex `gap`）；接入外观三档 / 管理页排序 / 温控档位 / 自启延时四处，顺带去掉外观档上那圈早于改色时代的蓝色光晕。

> **`.98`–`.99`（2026-10）**
> - `.98` 崩溃清账：`AccountToolsSection` 之外的四起「构建全绿、运行期整站死」全是同一类（漏 import / 残留已删 state / 引用越界变量），已修；mock 补齐缺字段；修掉一处上游错词条。教训写进 §3 验收线。
> - `.99` **PS5 Account Tools 源码并入主 ELF**（用户指令：不照搬 pkgmanager 的黑盒+iframe 模式，走「纯内置」）。`src/psaccounts/`（`accounts.c/.h` 账号与注册表、`registry.c` SysReg 16 槽扫描、`np_files.c` auth.dat/config.dat、`avatar.c` 头像 64/128/260/440、`remoteplay.c` PIN、`backup_ledger.c` 安全备份台账、`sys_lite.c` 凭据提权）编进 `NayePlmTools_*.elf`，API 挂在本机 `:8911` 的 `/api/*` 下，前端「常用配置 → 账号工具」是原生 React 板块（`components/views/AccountToolsSection.jsx`），无第二进程、无 iframe。**两处并入时必须的处理（后续加路由都要照做）**：① 全部 `/api/*` 走 `pthread_mutex_t acct_api_mu` 串行化（账号侧读写注册表/文件非并发安全）；② `con_cls` 所有权用带 magic 标记的 `acct_conn_t` + 四处 daemon 启动点都注册 `MHD_OPTION_NOTIFY_COMPLETED`，`http_on_request_completed` 里以 `(void *)1` 哨兵守卫，避免双方重复 free。与已测试原版（`D:\offact-new`）**有意保留的两处偏差**：登入不再走 web 侧确认口令闸门（服务端 `confirm=online` 校验仍在）；PIN 有效期 300s → **180s**（`src/psaccounts/accounts.c` 的 `PIN_TTL_SEC`，看门狗是我们自己的超时，不改固件的发码窗口）。
> - `.99` **动作橙转档**（用户：「主页的棕色调整为橙色」，圈定「整块橙色按钮本身」）：`#ff7a2e` → `#ff9410`（色相 21.8°→33.2°、B 46→16，饱和到底）。触点 = `tailwind.config.js`（`ps-blue`/`ps-blue-glow`/`vividOrange` 300/400/500）+ `index.css`（`--color-sel-bg`/`--color-action`/`-hover`/`--color-on-action`/`--color-action-tint`/`-line`/`--color-ps-blue`/`-glow` + `index.css:23` 老内核兜底字面量）+ `AutoloadOverlay.jsx:138` 阴影字面量。块体对 `#0a101d` 8.6:1、`#1b0e02` 标签字对块 8.7:1。**4+1 档令牌契约与 `light-theme.css` 一律未动**（浅色实心档仍是 `#F25C05`）。
> - `.99` 账号板块 9 处实测修订：登入去弹窗、PIN 180s、「重命名」→「修改」、自动登录标记改暖黄特殊色、PIN 说明 `break-all`→`break-words`（不再从词中间换行）、复制按钮紧跟 PIN 数字且复制的是**无空格原始 8 位**、同步按钮核实为「已激活且十六进制不同 → 写注册表 activate」（与原版一致），并让 PIN 卡片账号行跟随换算、三种账号 ID 的复制按钮移到格子内部且点击后实心反馈、可编辑框与只读框以 `.field-ro` 虚线区分、头像框加 `Camera` 角标提示可点。
> - `.99` **页面容器约定**（写进 `ui/PageHead.jsx` 头注释，新页面照抄）：根节点一律 `<div className="space-y-6">`，**不限页面宽、不居中、不另加页底内边距**，页头永远吃默认出血（`bleed` 开关已删，因为限宽会让页头内缩、和相邻菜单页对不齐）；要窄阅读列就限板块内部。浮层/粘顶/遮罩底**必须实色**（`--color-rail` / `--color-card`，`bg-black/80` 那类半透明会在滚动内容后面透出来）。尺度不写死：底部栏高度读 `--bar-h`。

> **`.100`（2026-10-05，正式出包版）**
> - `.100` **整套配色照搬用户给的参考页（灰墨 + 蓝主色）**（用户：「按这个颜色重新设计页面的颜色风格，同时根据这个颜色风格定制浅色风格界面，只改颜色」；追问确认口径 = **「整套照搬（含蓝主色）」**）。**这一条解除了 fork 自 `.85` 起的硬约束「主色（动作色）不得为灰或蓝」** —— 那是当时的观感取向，不是技术限制，现由用户明确改判；后续别再拿旧禁令回退这次转档。中性底从墨蓝阶改灰墨阶（`--color-canvas #111317` / `rail`=`card` `#1b1f25` / `line #30363d` / `inset rgba(8,10,14,.3)`，浅档画布白、`rail #f6f8fa`、`line #d0d7de`），动作色 `#ff9410` → **`#2563eb`**（深档 hover `#3b82f6`、浅档 hover `#1d4ed8`），状态色跟参考页（绿 `#3fb950`、金 `#d29922`、红 `#f87171`；浅档 `#1a7f37`/`#9a6700`/`#cf222e`），文字阶 `#e6edf3`/`#c9d1d9`/`#a1a9b3`/`#8b949e`。**触点仍只三层文件**：`tailwind.config.js`（`navyInk`/`vividOrange`/`lime`/`warmYellow`/`coral` 五条色阶 + `ps-*` 字面量；**标识符一个没改**，`vividOrange` 这个名字现在装的是蓝）+ `index.css`（LEGACY 兜底字面量 + `:root` 深档 + `html[data-device="ps5"]`）+ `theme/light-theme.css`（整文件重写：变量翻转 + 上游暗色工具类重映射 + 实心底白字豁免清单照留）+ `AutoloadOverlay.jsx:138` 阴影 rgba。**「4+1 档一色」契约与禁用渐变照旧**（参考页的 `linear-gradient` 进度条与金色渐变徽标**没有**照搬）。深档「主色的字与图标」单独成一档 `#58a6ff`（实心 `#2563eb` 压 `#111317` 只约 3:1，做文字读不清；浅档对应 `#0969da`）—— `.text-ps-blue` 永远吃 `index.css:27` 那条 LEGACY `!important` 字面量，只改变量不生效。
> - `.100` **`.panel-card` 底色退回 `card` 档**（`.90` 那次并到 `inset` 的前提是「半透明 tint 双层叠加会自己叠出第二档」；新色板是实色叠加，叠不出层次）。现在的角色表：**板块容器 = `card`（`#1b1f25` 浮起），板块内的行/输入框/徽章 = `inset`（凹下）**，与参考页「画布 → 卡片 → 内嵌件」三层同构。要退回 `.90` 的平档只改 `index.css` 的 `.panel-card` 一行。
> - `.100` **改名不彻底的根因与修法**：原本只写 SysReg 槽 `REG_KEY_USERNAME + (slot-1)*65536`，而壳层「切换用户」列表与重启后显示读的是 **UserService** 那份，所以简介页名字变了、切换列表和重启后还是旧的。`src/psaccounts/npsignin.c` 补第二层：`sceUserServiceSetUserName(uid, name, len)` 后立刻 `sceUserServiceGetUserName` 回读比对，两种结果都进 `message`（Set 被拒时如实报「注册表已改、壳层可能仍显旧名」，不谎报成功）。SDK 没有这两个函数的头文件原型，按仓库既有惯例自声明 `extern`；符号在 `/opt/ps5-payload-sdk/target/lib/libSceUserService.so` 里 `nm -D` 查证存在。**实机是否真接受 Set 未经硬件验证**，接口失败会走 message 报出来。
> - `.100` **账号操作去确认弹窗**（用户：「登入登出 激活等操作的大提示全不要」；口径确认 = 去**操作前**的确认弹窗，非去反馈）：`AccountToolsSection` 不再调 `showConfirm`，登出/激活/反激活点击即发请求；**操作后的 Toast 保留**。`CommonEntriesView` 因此不再向下传 `showConfirm`。服务端 `confirm=online` 那道独立闸门照旧。
> - `.100` **游戏磁贴改自适应、尺寸降三分之一**（用户：「再小三分之一左右，变成自适应，屏幕宽就多放、窄就少放，别固定四个」）：`GameSection.jsx` 宫格容器从 `grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-3` 换成新增骨架类 **`.tile-grid`**（`repeat(auto-fill, minmax(var(--tile-w), 1fr))`），列宽下限立令牌：`:root` `--tile-w: 10.5rem`(168px)、`html[data-device="ps5"]` `15rem`(240px)。电视档正文列约 1470px → 6 列 ≈245px（原固定 4 列 ≈355px）。**`grid` 的 `gap` 老内核支持**，不违反 flex gap 禁令。
> - `.100` **账号板块文案与可编辑框配色**（用户：「说明文字取消」「『本公举欠他21条原值记录』这几个字也不要」「可编辑的两个文本框字体颜色做个区分」）：板块顶部 `<p>`（词条 `acctools.desc`）与 `pending_records` 片段不再渲染（**词条保留在语言包，只删渲染**）；两格可编辑输入框（十六进制 ID / 账号显示名）加 `.field-edit`（字色走 `--color-ps-blue`，深 `#58a6ff` / 浅 `#0969da`），只读的换算结果两格仍走 `.field-ro`（虚线边 + 辅助字色），三格并排时谁能动一眼分得清。

> **`.101`（2026-10-05，正式出包版，短哈希 `2dc3088`）**
> - `.101` **次要按钮档转紫（新增一档令牌）**（用户：「所有的 btn 都要有颜色，但别用蓝色」，颜色选择授权「你随便」）：`--color-secondary` / `-tint` / `-line`（深 `#a371f7`，浅 `#8250df` + `rgba(130,80,223,.1/.35)`），吃这一档的是 `.btn-outline`、`.btn-ghost`、`.icon-btn`（含 hover 与 `.is-copied`）、`.field-btn` 四处；**蓝只留给 `.btn-primary` 与 `.sel-chip`**（这一页唯一的主操作 + 当前选中）。紫选冷色同族是为了与蓝一眼分得开，同时不占用绿=成功、金=注意、红=危险这三档已有名有姓的语义色。仍然纯色无渐变、零改组件类名。
> - `.101` **磁贴相对 `.100` 再放大四分之一**：`--tile-w` PC/手机 `10.5rem`(168px) → `13.125rem`(210px)，电视档 `15rem`(240px) → `18.75rem`(300px)；`.tile-grid` 的 `repeat(auto-fill, minmax(var(--tile-w),1fr))` 未动，所以仍是自适应，1920 正文列实测回到 4 列 ≈362px。
> - `.101` **PKG Manager 开关卡片换页**（用户指令）：`PkgManagerCard` 挂载点 `SettingsView.jsx` → `CommonEntriesView.jsx` 温控板块之后（本页第四个板块，页面最底部）；卡片本身零改动，`SettingsView` 顺带去掉不再引用的 `Package` 图标导入。
> - `.101` **改名显示源调查（事实 / 推断分开）**：
>   - **事实**：`.100` 在实机上 SysReg 槽写入持久成功（`/api/accounts` 返回 slot1 `name:"Naye11"→"Naye87"`），Toast 亦报 `registry + UserService both verified`（Set 返回 0、Get 回读等于新名），**但主界面用户按钮与「切换用户」界面仍是旧名**。→ 结论：那两处 UI 不拿 UserService 缓存当显示源，`.100` 判据取错（回读成功 ≠ 界面用它）。
>   - **事实**：真机取证（`D:\offact-new\_rename_forensics.log`）里 `/system_data/priv/cache/profile/0x1A1D3629/online.json` 原本 **ABSENT**、由头像写回才存在；`/user/home/<uid目录>/` 实测只有 `config.dat`/`np`/`nobackup` 等，**没有 `username.dat`**；`/system_data/priv/home/user_list.dat` 是 72 字节 0600、另有 `.0` 备份份。
>   - **推断**：换头像在同目录能生效 → 壳层确实读 `profile/0x<UID>/online.json`，它的 `firstName` 是最有希望的显示源；`username.dat` 是 activity 类工具给用户打标签的读法（`elf-arsenal/src/activitydb.c`），本机可能压根不存在，故只能作为条件写 + 报告。
>   - **本轮做法**：改名按四层尽力而为，每层结论原样拼进 `message`（Toast 直接可读，格式 `base | profile cache … | user home … | user_list.dat …`），让实机一轮就判死真实来源。③ 只认「全文唯一 + 无转义」的 `firstName` 字符串值，④ 只在去尾 `\r\n\0` 后**正好等于旧名**才改写，行尾字节原样保留；两者都写前登记备份（且**只在 `safety_file_was_absent()<0` 即未登记过时登记**，否则会把新名当原值覆盖掉备份）、写后回读比对，任一条件不满足就原样不动只报一句话。`user_list.dat` **只读探针**：结构无文档、写它等于拿全体账号名单去赌，故只报旧名/新名的字节偏移。
>   - **下一轮的分支**：若探针报「旧名在 `user_list.dat` 某偏移、新名不在」→ 显示源就是它，届时按定宽字段做**等长或截断**的原位改写（仍是先备份后写）；若报「profile cache verified 而界面仍旧名」→ 说明这两处 UI 走的是内存里 ShellCore 的会话缓存，只能靠重启/切用户刷新，得改的是提示文案而不是写入层。
>   - **配套放大**：`npsignin_result_t.message` 320 → 768；`accounts.c` 的 `emsg` 384 → 896、响应 `cap` 2048 → 4096（否则截成半句话，甚至把 JSON 截断成非法）。

> **`.102`（2026-10-05，正式出包版，短哈希 `be7fa8f`）**
> - **先记账一条教训：`.101` 的改名取证代码把整个 payload 弄崩了**（用户实机：改名弹出 `Failed to fetch` + 「读不到账号列表：Failed to fetch」，随后 ELF 死亡）。根因不在网络、不在 PS5 内核，在一行容量算法：`rename_profile_cache()` 里 `blen = len + nlen - vlen` 只覆盖「`firstName` 值之后」的尾部，**漏了 `vs - raw` 这段前缀**（头像 URL 之类，实测一百多字节），于是第三段 memcpy 往 malloc 块尾后面多写同样多的字节 → 砸穿堆元数据 → 进程没了 → 前端所有 fetch 一起失败。修法是 `pre = vs - raw; suf = len - vlen; blen = pre + nlen + suf`。**这类「按原文件长度推算新文件长度」的地方，凡有偏移前的头部，必须把头部一起算进容量**；`-Wall -Wextra` 查不出来，只有实机（或本地跑一遍拼接）才会炸。
> - **崩在哪一步决定了设备上留了什么**：溢出发生在备份登记与 `write_file()` **之前**，所以 `online.json` 从未被写过、不需要清理；但第①②层（SysReg 槽 + UserService）在那之前已经写成功，注册表里现在是新名。`.101` 计划好的「按 Toast 逐层判读」这一轮**没有拿到数据**，重新跑改名后仍然要按 `base | profile cache … | user home … | user_list.dat …` 四段判读。
> - **按钮整套转实心、按功能分色**（用户：「图三那种改法不对，不同场景不同功能要不同颜色的实心图标，跟图二风格一样，颜色不同而已」；样张 `d:\pldmanager\btn-solid-preview-v1.html` 选定 **A + I2**）。`.101` 的 `--color-secondary*` 三枚令牌与 `light-theme.css` 对应三行**已删**，换成 `:root` 的 `--fill-{blue,purple,violet,teal,green,gold,red}` + 各自 `-hover` / `-icon` 档 + `--fill-fg`，**深浅两主题同值**（实心底自己保证对比，不再靠翻档）。功能档：蓝＝本页唯一主操作（`.btn-primary`，仍与 `.sel-chip` 同值）· 紫＝启动/重启/修改/取消（`.btn-outline`）· 深紫＝幽灵（`.btn-ghost`）· 红＝不可逆（`.btn-danger`）· 绿＝已启用/安装/更新（`.btn-ok`，新增）· 青＝刷新/重试/同步/复制（`.btn-info`，新增）· 金＝上传/写入/覆盖（`.btn-warn`，新增）。六档配白字 ≥4.2:1。
> - **I2 的实现约束（下次加实心档照着做，别踩）**：图形取同色相提亮一档写在 `.btn-* > svg`（特异性 0,1,1，压得住 `.btn-* > *` 的 0,1,0），但**必须带 `!important`** —— 文件顶部 LEGACY 把 `.text-ps-blue` / `.text-white` 写成 `!important` 字面量给老内核兜底，而组件里的图标普遍挂着这两个类；`.is-copied` 那类复合选择器（0,2,1）也要一并带上，否则会被自己的 `> svg` 反压。
> - **档用错过一处，已纠**：温控「已启用」原本挂 `.sel-chip`，而 `.sel-chip` 的契约是「当前选中」不是「状态」，现改 `.btn-ok`；进程列表的 Retry 原本挂 `.btn-danger`（红＝不可逆），现改 `.btn-info`。全站其余调用点保持「次要动作＝紫、取消/返回＝紫、主操作＝蓝」不动。
> - **页头不再粘顶**（用户：「所有 head-bar 要跟随页面一起滚动」）：`.head-bar` 去掉 `.94` 的 `position: sticky / top / z-index`，只留出血负边距 + rail 实色底 + 底部分界线。

> **`.103`（2026-10-05，UI 重做：收敛上色 + 深空黑 + 舒展留白；**未出包**，只改了前端与文档）**
> - 用户指令：「看一下我这个项目的前端页面，帮我调整一下布局和配色，要现代化 简约 高大上」。
>   三个方向由用户逐条选定（均取推荐项）：**① 按钮配色＝收敛（只留主操作上色）② 视觉基调＝深空黑 + 极细线 + 大留白 ③ 布局密度＝舒展（大留白 + 中大圆角）**。
> - **按钮契约改写（最重要，后续加按钮必须照这个来）**：作废 `.102` 的「功能六色实心」。
>   现在的规则是 —— **全站只有「这一页的主操作」是实心彩块**（`.btn-primary`，与 `.sel-chip` 同色同值）；
>   其余按钮一律中性/淡染档：
>   | 档 | 类名 | 形态 | 语义 |
>   | --- | --- | --- | --- |
>   | 主操作 | `.btn-primary` | 实心蓝 + 白字 | 这一页唯一的主操作（上传 / 安装 / 保存 / 移动到内部） |
>   | 中性 | `.btn-outline` | 淡底 + 发丝线 + 常规字 | 次要动作：取消 / 返回 / 启动 / 重启 / 修改 |
>   | 幽灵 | `.btn-ghost` | 无底无边，悬停浮底 | 低优先级跳转 |
>   | 信息 | `.btn-info` | 淡底 + 线 | 刷新 / 重试 / 同步 / 复制 |
>   | 危险 | `.btn-danger` | 淡红底 + 红线 + 红字 | 删除 / 终止 / 关闭（不可逆） |
>   | 成功 | `.btn-ok` | 淡绿底 + 绿线 + 绿字 | 已启用 / 安装 / 更新 |
>   | 警示 | `.btn-warn` | 淡金底 + 金线 + 金字 | 上传 / 写入 / 覆盖 |
>   令牌在 `index.css` 的 `:root`（`--btn-bg/--btn-line/--btn-fg/--btn-ghost-*` + `--btn-{danger,ok,warn,info}-{fg,tint,tint-hover,line}`），
>   浅色档在 `light-theme.css` 各翻一份（浅底上「淡染」必须换实色浅染，深档那套 rgba 压白底会脏）。
>   **`--fill-{blue,purple,violet,teal,green,gold,red}` 七组令牌已整组删除**（零引用），别再找它们。
> - **`.icon-btn` 从实心青块改为无底幽灵档**（静息态只有一个中性色图标，悬停浮出 `--color-inset-hover` 底）。
>   新增 `.icon-btn.is-danger`：列表行内的删除/移出类图标钮**图标恒为危险色**、悬停淡红底 ——
>   原来靠 `hover:bg-red-500/10 hover:text-red-500` 才显红，而触屏/遥控器根本没有悬停态。
>   已接入三处：`StorageHub` 行内删除、`AutoloadView` 移出序列、`ManageSourcesView` 删除来源。
> - **中性底「深空黑」**：画布 `#111317`→**`#0B0D10`**、侧栏 `#1B1F25`→**`#0E1116`**、卡片 `#1B1F25`→**`#12151A`**、
>   发丝线 `#30363D`→**`#1E232A`**（`line-hover #2F3742`）。层级不再靠色块深浅差，而是靠 1px 发丝线 + 留白划出来。
>   触点：`tailwind.config.js`（`navyInk` 的 700/800/900/950 + `ps-*` 四个字面量）、`index.css` 的 `:root`、
>   `light-theme.css`（浅档线一并收细 `#D0D7DE`→`#E4E7EB`，`line-hover #AFB8C1`→`#CDD3DA`）。文字阶 50~600 **一个字节没改**。
> - **`.head-bar` 去掉实色底**：`.102` 已经不粘顶，没有任何内容会压到它，因此不再需要「浮层底必须实色」那条约束；
>   原来 rail 档的底比画布亮一档，在近黑画布上形成一条横贯全宽的浅色带（割裂感）。现在 `background-color: transparent`，
>   只留底部那根发丝线。**注意：若将来把页头改回粘顶，这一行必须改回实色。**
> - **舒展层**（`index.css` 末尾，收口层之前）：页面根节点统一是 `space-y-6`（见 `ui/PageHead.jsx` 容器约定），
>   所以只在这一条选择器上把板块间距 0.9rem → **1.35rem（≥768px 1.5rem）**，一处生效全站；
>   并显式排除 `.md\:space-y-8`（AutoloadView 两栏那处自定 2rem），否则会被压回去。
>   圆角同步放大：`.panel-card` 0.75→**1rem**、`.btn`/`.icon-btn`/`.field` 0.5→**0.625rem**、`.segmented-track` 0.75→**0.875rem**。
>   内边距：`.panel-head` 0.875/1.25rem → **1.05/1.4rem**；`ui/Section.jsx` 板块体 `p-4` → **`p-5`**；`.tile-grid` gap 0.45 → **0.7rem**。
>   **`--ctl-h` / `--nav-h` / `--row-h` / `--bar-h` / `--switch-*` 一律未动**（控件高度是 `.99` 用户点名降过的，别顺手放大）。
> - **游戏磁贴徽标收敛**（`GameSection.jsx`）：原来是「饱和实心底 + 白字」，一屏最多同时出现 8 种饱和色块
>   （平台 2 色 + 格式 7 色 + 来源 2 色），是游戏页最吵的一处。现在统一成**深色芯片**
>   （`chipStyle()`：`rgba(8,10,14,.72)` 底 + 同色字 + 同色 35% 发丝线），色相只由字与线传达 ——
>   GitHub label 的做法。`FMT_LABEL` / `PLAT_COLOR` 里的 `c` **语义已变**：现在是文字/描边色（提亮一档），
>   不再是底色，改色时别按「底色」去挑。
> - **漏网实心块一并收口**（绕过骨架层、手写 Tailwind 实心类的两处）：
>   `StorageHub.jsx` 仓库列表的「更新」按钮 `bg-emerald-600…` → `.btn-ok`；「已安装的 Payload」行内
>   「更新」按钮同改。`App.jsx` 确认弹窗的**危险确认仍保留实心 `bg-red-600`** —— 那是收口层明确豁免的
>   「危险本身」，且它是模态闸门后的单次不可逆动作，实心是它应得的份量；其余场景没有这个豁免。
> - **浅色主题补一段「反向豁免」**（`light-theme.css` 末尾）：`§2` 把 `text-white/text-zinc-*/text-ps-blue`
>   整体翻成浅底深色（权重 0,2,0 + `!important`），而语义按钮档在 `index.css` 里是 `.btn-danger > svg`（0,1,1）——
>   浅色下会被洗掉，红边淡红底上再也看不出「这是危险操作」。现在按 (0,2,1)/(0,3,1) 补回，只在浅色主题生效。
> - **验证口径**：`npm run build` 通过（817.93 kB / gzip 226.11 kB）；`npm run lint` **38 项（32 error / 6 warning）与改动前完全一致**（基线问题，非本次引入）。
>   深/浅两套主题下逐页截图复核（管理 Payload / 自动加载 / 游戏管理 / 活动进程 / 设置）。**未实机（PS5）验收，未出包。**

> **`.104`（2026-10-05，账号板块按钮状态分色 + 主色换朱橙；**未出包**，只改前端与文档）**
> - **主色由蓝换朱橙 `#F43E06`**（用户给了具体色值 `F43E06`，替换 `.100` 起用了四个版本的 `#2563EB`）。
>   三档取值：实心/选中底 `#F43E06`、深档「字与图标」档 `#FF7043`、浅档字与图标档 `#C2330A`（浅底上用实心值做文字读不清）；
>   悬停深档 `#FF5A1F` / 浅档 `#D93A00`；薄染 `rgba(244,62,6,.16/.09)`、线 `rgba(244,62,6,.45/.32)`。
>   触点：`tailwind.config.js`（`vividOrange` 整阶重写 + `ps-blue` / `ps-blue-glow` 字面量）、
>   `index.css`（LEGACY `.text-ps-blue` 兜底字面量 `#FF7043` + `:root` 六枚令牌）、
>   `light-theme.css`（变量档 + `bg-ps-blue` / `bg-ps-blue\/85` / `\!text-ps-blue` / `text-ps-blue*` / `border-ps-blue` 五组字面量）、
>   `AutoloadOverlay.jsx:138` 的阴影 rgba。**`vividOrange` 这个标识符这次终于名副其实了**（`.100` 的注释说「别按字面理解」可以撤了）。
> - ⚠ **对比度备忘（下一轮若有人追问「按钮字看不清」先看这条）**：白字压 `#F43E06` 实测 **3.79:1**，
>   低于 WCAG AA 对正文的 4.5:1（`#2563EB` 时代是 5.12:1，是过的），但高于大字号/图形元素的 3:1。
>   两条一行改法：① `--color-sel-bg` 压深到 `#D93A00`（白字 4.61:1）；② 实心档改配深色字（`#F43E06` 配 `#1A0A02` 得 5.09:1）。
>   已把三个方案的真实按钮实拍交给用户选，**等他定；未定之前不要自作主张改**。
> - ⚠ **暖色撞色**：主色朱橙（色相 15°）与危险红 `#F87171`（0°）、警示金 `#D29922`（40°）同处暖色区。
>   实拍下三者仍可分（实心块 vs 淡染描边、朱橙 vs 暗金），但若有人反馈「删除/终止的红不够跳」，
>   把 `--btn-danger-fg` 往深红推一档（如 `#E5484D`）即可，其余不动。
> - **账号板块按钮按状态分色**（用户口径原文：「登入 和激活 是绿档，登出 是红档，反激活是灰档」）：
>   `AccountToolsSection.jsx` 新增 `BTN_GRANT`（`.btn-ok`）与 `BTN_REVOKE`（`.btn-danger`）两个常量；
>   登入/激活 → 绿档，登出 → 红档，**反激活 → 中性档**（可随时重做的开关，份量低于登出，不跟它抢红）。
> - **「同步」按钮转金档**（用户：「这个按钮也加颜色」）：`BTN_INFO`（`.btn-info`，`.103` 起已是中性档）→ `BTN_SYNC = 'btn btn-warn'`。
>   语义依据：同步的实质是把换算后的 ID 写回注册表，归「写入」档。
> - **删掉「暂无 PIN，点击下方按钮生成」**（用户：「这句废话不要」）：没有 PIN 时该位置直接渲染 `null`。
>   词条 `acctools.pin_none` 保留在语言包里（与 `.100` 删说明文字同一处理惯例），只是不再渲染。
> - **「账号显示名」输入框缩短并与「同步」对齐**（用户：「这个框缩短，将修改按钮和上边同步按钮对齐」）：
>   原来这一行是 `flex-1` 铺满整宽，输入框顶到最右、修改按钮吊在卡片右边缘，与上方「同步」不在一条竖线。
>   现在套用与 ID 三格**同一张** `grid-cols-1 sm:grid-cols-3`，字段落第 1 列、按钮贴本列右缘，
>   右缘自然与「同步」对齐（窄屏 <640px 仍是整宽单列）。结构也与十六进制那格对齐（外层 flex + `relative flex-1 min-w-0`）。
> - **游戏磁贴「已安装」徽标由旧主色蓝改绿 `#3fb950`**：它原来是 `#58a6ff`（= 旧主色的字/图标档），
>   主色一换就成了无主的遗留蓝。改绿与「已安装 = 成功态」同源。**平台徽标的 PS5 蓝保留**——
>   那是 PlayStation 的平台身份色，不是动作主色。
> - **验证口径**：`npm run build` 通过（817.94 kB / gzip 226.09 kB）；`npm run lint` **38 项，与基线一致**。
>   深/浅两套主题下复核管理 Payload / 常用配置（账号板块）/ 游戏管理。**未实机（PS5）验收，未出包。**

> **`.105`（2026-10-05，账号板块「同步」按钮补图标 + PIN 进度条重做；**未出包**，只改前端与文档）**
> - **「同步」按钮补小图标**（用户：「同步按钮也加个小图标，跟修改按钮一样大，一样的风格」）：
>   `AccountToolsSection.jsx` 的同步钮在文字前加 `<RefreshCw className="w-4 h-4" />`，与「修改」钮的
>   `<Check className="w-4 h-4" />` 同尺寸同结构；间距由骨架层 `.btn > * + *` 给（老内核 flex 的 gap 不生效，
>   别改成 `gap`）。忙时文案切「处理中…」（与「修改」一致），图标静态不换。**用的是 `.btn-warn` 金档**——
>   用户上一轮已定「同步要有颜色」，本轮只加图标、不动档位。
> - **PIN 倒计时进度条重做**（用户：「生成 pin 的进度条要高一些，炫酷一些，颜色可以用别的」）：
>   原来是 `h-2`(8px) 的纯色条（`bg-ps-blue` 填充），主色换朱橙后它变成一条橙条，与「等待配对」的中性感也不搭。
>   现在换成骨架层新组件 `.pin-bar` / `.pin-bar-fill`（定义在 `index.css` 的 `.field-btn` 段之后、舒展层之前）：
>   ① **加厚**：高度走新令牌 `--pin-bar-h`（PC `0.85rem`≈14px / 电视 `1.25rem`=20px，见 `:root` 与
>   `html[data-device="ps5"]` 各一档；它是装饰条，**不跟 `--ctl-h` 走**，免得顺手把按钮/输入框一起改）；
>   ② **炫酷**：填充段走「青 `#22D3EE` → 蓝 `#3B82F6` → 紫 `#8B5CF6`」冷色渐变 + 外发光（两档 box-shadow），
>   再叠一条循环扫过的白色流光（`.pin-bar-fill::after` + `@keyframes pin-sheen`，免去额外 DOM）；
>   ③ **换色 + 临界反馈**：剩余 ≤30 秒整条切「琥珀 `#F59E0B` → 红 `#EF4444`/`#DC2626`」渐变、发光转红、
>   并 `@keyframes pin-pulse` 整体脉冲。宽度过渡移到 `.pin-bar-fill` 的 `transition: width 1s linear`，组件只给宽度。
> - ⚠ **两个实现坑（下次改这条别踩）**：
>   · 渐变必须写在 `background-image` 上 —— `index.css` 的「一档一色」收口层只映射 `background-color`，
>     用 `bg-*` 类会被翻成中性底、渐变就没了。
>   · 轨道 **不能设 `overflow:hidden`**（那样会把填充段的外发光一起裁掉，整条就没有「亮」的感觉）；
>     需要裁的是流光，而流光是填充段的后代，所以 `overflow:hidden` 放在 `.pin-bar-fill` 上 ——
>     `overflow` 裁的是后代，不会裁元素自己的 `box-shadow`。
>   · 填充段绝对定位、`left/top/bottom` 归零：containing block 是轨道的 padding box（边框内侧），
>     所以它压不住那根 1px 发丝线；宽度百分比也是相对 padding box，100% 时正好铺满。
> - **为什么用冷色**：主色已是朱橙（暖色），进度条再走暖色就糊成一片；青→蓝→紫与主色互补，
>   从「橙色主操作」里一眼跳出来，也更像「正在跑的技术感」。用户说的是「颜色可以用别的」，属授权，未再出候选。
> - **验证口径**：`npm run build` 通过（818.91 kB / gzip 226.41 kB）；`npm run lint` **38 项，与基线一致**。
>   深/浅两套主题下实拍复核（同步按钮行 + 账号卡 + 进度条正常态/临界态）。**未实机（PS5）验收，未出包。**

> **`.106`（2026-10-05，**正式出包版**，短哈希 `0e9afb2`，子模块标题辨识度上调 + 收口 `.103`–`.105` 三轮 UI 改动）**
> - **背景**（用户：「所有这类子模块的标题辨识度都提高一些，具体有什么措施你自己思考一下」，附三张截图：
>   游戏列表 / 入口清单 / 账号·远程游玩配对）。三处都是 `ui/Section.jsx` 的 `.panel-head` 标题，
>   所以**只改 `Section.jsx` + `index.css` 两个文件**，全站所有板块头一次性生效。
> - **四条措施**（缺一条都差口气）：
>   ① **字号 `text-sm`(0.85rem≈13.6px) → `1.05rem`(≈16.8px)** —— 最直接的一档；13.6px 在 1080p 电视上本就偏小。
>   ② **字重 600 → 700** —— 近黑底上的浅色文字笔画越细越「虚」，加粗一档才立得住。
>   ③ **字距 `+0.01em`** —— 汉字彼此紧挨时笔画容易连成一片，给一点缝让每个字独立成形。
>   ④ **颜色不动**（仍是 `text-white`）—— 白字压 `#12151A` 已有约 19:1，瓶颈在字号与字重、不在色值。
> - **两处配套**：图标同步 `w-4 h-4`(16px) → `1.15rem`(≈18.4px)（标题变大、图标不跟上会显得小一圈）；
>   计数徽章 `text-zinc-500` → `text-zinc-400`（深底上那颗「共 8 款」原本太灰）。
> - **做法**：标题从原子类（`text-sm font-semibold`）收成**骨架层语义类 `.panel-title`**（定义在 `index.css`
>   的 `.panel-head` 之后）。以后调标题只改这一处，也不会再冒出「某个视图自己写一套 text-sm font-semibold」的副本。
>   **浅色主题不用另翻**：`.panel-title` 不写 `color`，浅档仍由 `light-theme.css` 把 `text-white` 反着映射成近黑 `#1f2328`。
> - **层级未被压平**：板块内嵌卡的小标题（如账号板块的「远程游玩配对 PIN」、温控的「风扇温控」）仍是
>   `text-[14px]`，落在板块标题（16.8px）下一档；`.panel-head` 还有图标 + 底边线做结构区分，不会混。
> - **验证口径**：`npm run build` 通过（818.9 kB / gzip 226.4 kB）；`npm run lint` **38 项，与基线一致**。
>   深/浅两套主题下逐块实拍（「改动前」用注入样式还原旧值，非 git stash）。
> - **出包（2026-10-05，短哈希 `0e9afb2`）**：四轮 UI 改动（`.103`–`.106`）合成一个提交后一次出包。
>   两包各 **9,875,208 字节**（与 `.102` 同尺寸属 16 KB 页对齐的正常巧合，已从二进制内容核对过，不是旧包）：
>   `NayePlmTools_0.5.2.106-me.elf`（NAYE00001 媒体栏）/ `NayePlmTools_0.5.2.106-ga.elf`（NAYE00004 游戏栏）。
>   二进制核对：内嵌标题 `NayePlmTools v0.5.2.106 by naye (0e9afb2, built at 2026-10-05 06:29:29 UTC)`（**非 `DEV`**）、
>   `PLDMGR_VER:0.5.2.106`、titleId 分别 NAYE00001/NAYE00004、新前端标记 `pin-bar-fill`/`panel-title`/`#f43e06` 均在包内。
>   ⚠ 小瑕疵（无功能影响）：标题里的 `built at` 是 Windows 侧前端构建的时刻（06:29:29），
>   `cache.appcache` 的时间戳是 WSL 侧 `make` 重跑时的（06:34:30），两者差 5 分钟 ——
>   因为标题占位符在前端构建阶段就被替换掉了，WSL 的 sed 是空操作（`$(ASSET_HEADER)` 那条 recipe 不带脏树检查，
>   所以短哈希仍是真实值）。要两者一致，得让 WSL 侧独占做标题注入。
>   **仍未实机（PS5）验收。**


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
- `src/psaccounts/`（`.99` 并入）账号工具：注册表 16 槽扫描、离线激活、NP 假登录（auth.dat/config.dat）、头像写回、安全备份台账、`libSceRemoteplay` 配对 PIN（单会话 + 看门狗线程，退出前 `accounts_pin_shutdown()`）。路由全部在 `http_server.c` 的 `/api/*` 分支，**串行化 + con_cls 所有权**两条约定见 §0 的 `.99` 条目。
- **铁律**：fork 不改 C 后端的路由/配置键/文件路径，保证升级上游时行为一致。（例外：`.99` 新增的 `/api/*` 账号路由，纯增量，不动上游路由。）

### 2.2 React 前端（`frontend/src/`）
- `App.jsx` 布局/路由 + **`NAV_ITEMS`/`NAV_GROUPS`（一份菜单表驱动侧栏与手机底部栏）**；`components/views/*` 各功能页（GameManageView 游戏管理、GameSection 游戏宫格、ThermalSection 温控、**AccountToolsSection 账号工具（`.99`，常用配置板块：登录/激活/改名/PIN/头像/账号 ID 换算）**、StorageHub、Autoload、ActiveProcesses、LogViewer、Settings、ManageSources、CommonEntries、MoveFromUsb、Donate、PkgManagerFrame...）。
- `i18n.js` 加载 locale（子模块）+ **合并 `locales-overlay/<lang>.json`（后者优先）**；完成度 <70% 的语言不注册。
- `components/ui/` 共用件：`Section`（板块 = `.panel-card` + 板块头 + body，`headless`/`bodyPad` 供自带头的卡片复用）、`PageHead`（页头 + 粘顶，`back`/`bleed`）、`EmptyState`、`LoadingState`、`Modal`、`Toast`、`Switch`（唯一开关）、`Segmented`（唯一互斥分段）、`NavButton`、`PayloadName`。**新页面一律用这些件，不要再手写板块容器 / 空态 / spinner。**
- `utils/spatialNav.js`：仅 `isPS5` 挂载的遥控器空间导航。
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
- **出包前必做（否则标题烙成 `DEV`）**：Makefile 用 `git diff --quiet || COMMIT="DEV"` 判脏树，而 WSL 里的 git 没继承 Windows 的 `core.autocrlf=true`，会把 78 个 CRLF 工作区文件看成已修改 —— 哪怕刚提交完、Windows 侧 `git status` 全干净，WSL 照样判脏。构建命令前加一次性归一化（**不要写进 git 配置**）：
  ```bash
  export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=true
  ```
  出包后从二进制核对：`grep -ao 'v<ver> by naye ([0-9a-f]*' xxx.elf | tr -cd '[:print:]\n'` 应为真实短哈希，出现 `DEV` 就得重出。
- 预览电视端形态：`http://localhost:5173/?device=ps5`（`utils/helpers.js` 的 `isPS5` 认这个 query）。命中后与真机同一套分支 —— 侧栏常显、底部栏隐藏、`html[data-device="ps5"]` 抬命中区尺度、方向键空间导航接上。真机靠 UA 命中，不看该参数。
- **前端改动的验收线是 `npm.cmd run lint` + 逐页实测，不是 `vite build`**：esbuild 不做作用域检查，漏 import / 残留已删 state / 引用越界变量在构建期全绿、运行期才炸（`v0.5.2.98` 修的四起整站死全是这类）。`mock-server.js` 里 Node `process` 的 `no-undef` 报错属 globals 配置的既有误报。

## 4. 版本号同步（全链路）
`include/pldmgr.h` 的 `MENU_VERSION` 是唯一真源；`make` 会把它注入前端标题与产物文件名，运行时 `/version` 路由、启动通知与通知文案都读同一宏。**仍需手动同步的 4 处**：`frontend/mock-server.js`（mock 的 /version）、`DonateView.jsx`（关于页兜底串）、`locales-overlay/en-US.json`、`locales-overlay/zh-CN.json`（`settings.donate_desc`）。**出正式包入口在构建脚本层，勿手改单个产物。**

## 5. Fork 维护约定
1. 所有 fork 专属改动打 `// [CN-FORK]` 注释标记；中文措辞/新增 key 只进 `locales-overlay/`，**绝不改 locales 子模块**。
2. UI 配色分两层，都不改上游组件类名：**先改令牌层**（`tailwind.config.js` 的 `theme.extend.colors` + `index.css` 的 `:root` CSS 变量）——类名保留、值替换，全站一次生效；**再补重映射**（`theme/light-theme.css`）把上游写死的暗色工具类映射为浅色语义色。骨架层（`.nav-group-title`/`.nav-item`/`.page-head`/`.head-bar`/`.btn-*`/`.icon-btn`/`.row-link`/`.field`/`.field-ro`/`.field-edit`/`.field-btn`/`.switch-*`/`.segmented-*`/`.stack-x`/`.sel-chip`/`.tile-grid`，定义在 `index.css` 末尾）颜色一律走 CSS 变量，浅色只在 `light-theme.css` 翻变量，不写 `!important`。**命中区与行高不写死 px**：`--ctl-h`、`--row-h`、`--tile-w`、`--switch-*` 在 `:root` 与 `html[data-device="ps5"]` 各一档，组件只引用令牌。**动作主色自 `.104` 起是朱橙 `#F43E06`**（深档文字/图标档 `#FF7043`、浅档 `#C2330A`）—— 历史沿革：`.85` 亮橙 → `.100` 蓝 `#2563EB`（用户选定「整套照搬含蓝主色」，当时明确解除了「主色不得为灰或蓝」那条旧约束）→ `.104` 按用户给的色值换回朱橙。**主色只允许出现在两处**：`.btn-primary`（这一页唯一的主操作）与 `.sel-chip`（当前选中）。**状态类按钮（同一颗按钮随状态换语义）照 `.104` 的账号板块做法**：建立状态 = 绿档、撤销状态 = 红档（可随时重做的开关回中性档），见 `AccountToolsSection.jsx` 的 `BTN_GRANT` / `BTN_REVOKE`。**其余按钮自 `.103` 起一律中性/淡染档**（`.btn-outline` 中性、`.btn-ghost` 无底、`.btn-info` 信息、`.btn-danger` 淡红、`.btn-ok` 淡绿、`.btn-warn` 淡金，令牌 `--btn-*`），用户口径：「只留主操作上色」；`.101` 的 `--color-secondary` 与 `.102` 的 `--fill-*` 六色实心档**都已删除**，加按钮请照 `.103` 的角色表走。
3. 升级上游：`git fetch upstream && git checkout main && git merge --ff-only upstream/main`，再 `git checkout cn-fork && git rebase main`。冲突集中在带 `[CN-FORK]` 的局部插入块。
4. third_party 基线 ELF 需 `git add -f`；`include/assets_*.h`、`frontend/dist/`、根目录 `*.elf` 属构建产物，始终不入库。
5. Commit 规范：`[CN-FORK] v<版本号>: <描述>`；**只本地提交，不配置 remote、禁止 push**。

## 6. 关键约定 / 易踩坑（速查）
- **风扇温控（安全红线）**：只通过 `/dev/icc_fan` autoservo ioctl 设「目标温度」交固件闭环（GET `0xC01C8F08` / SET `0xC01C8F07`，28 字节对象，byte[2]=selector、byte[5]=目标 °C）。必须 GET → 只改 byte5 → SET 整个对象 → 回读校验，**绝不全零 SET**；目标夹 60~85 °C。进游戏/待机会复位目标，靠常驻守护线程（`FAN_INTERVAL` 默认 7s）拉回；`safe_mode` 短路。
- **自启**：`autoload.txt` 延时行 `!` 前缀 + 前导空行需自愈规范化；安装 payload 后同步登记到 `ps5_autoloader`（全局覆盖）。
- **网络**：用户态代码用 socket 前必须 `sceNetCtlInit` + `sceUserServiceInitialize(256)`。
- **传感器/硬件**：`sceKernelIccGet*` 会 crash、Sony 传感器 API 非可重入 —— 这是 `.64` 下线硬件信息的根因；`/dev/dce` ioctl 默认停用 + `safe_mode` 哨兵文件用于防死机。
- **提权**：官方范式只改 authid/caps 且用完还原（勿改 uid/prison/rootdir）。**已知偏差（待裁决）**：`.99` 移植的 `src/psaccounts/sys_lite.c` 里 `escalate_creds()` 沿用 `D:\offact-new` 已测行为——一次性提满 root + jailbreak 且不还原，因为写 SysReg 与 `/system_data/priv` 头像路径都需要它。这是移植保真，不是本 fork 的新增风险；若要收口成官方范式，必须先实测账号写入是否仍能成功。
- **前端**：Vite 未代理的 API 会被 SPA fallback 吞成 index.html → 页面转圈；`!text-white` important 变体、tailwind 字面色需单独映射。
- **PS5 老内核三条硬约束**：① flex 盒的 `gap` 不生效（`grid` 的 `gap` 生效）——横排/竖排间距一律用 `* + *` 外边距或 `.stack-x`；② 不支持 `@layer`，`index.css` 顶部那批 `LEGACY FALLBACKS` 是给老内核兜底的关键工具类，别删；③ `animate-in / slide-in-from-*` 之类是死类名（`tailwindcss-animate` 未安装），别指望它做动画。
- **C 字符串**：分段字符串字面量不要拆断 JSON key（会导致设置页开关失灵）。

## 7. 接手后的下一步（待定，非阻塞）
- 功能已定稿，后续以维护/上游同步为主；如需重启硬件信息，须先解决传感器可重入性与死机哨兵。
