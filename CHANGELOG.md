## v0.5.2.95 (CN-FORK)

### 🐛 Bug Fixes
* **骨架重构第 4 批：老内核 flex gap 清零**。PS5 浏览器内核不支持 flex 盒上的 `gap`（grid 的 `gap` 是支持的，所以网格的一律保留），这批把仍然生效的 flex gap 全部换成 `* + *` 外边距写法；这类地方在 PC 上看着正常，到电视上就是控件贴死、图标压字。逐处：
  * `theme/ThemeToggle.jsx`：分段控件轨道 `gap-1` → `space-x-1`，三段按钮补 `min-h-[var(--ctl-h)]`（原来 `p-2.5` 撑出的高度够不到命中区尺度），`transition-all` 收窄成 `transition-colors`。
  * `ThermalSection.jsx` 风扇卡：标题行、目标温度行、档位行的 gap 改边距；「已启用 / 未启用」那颗自写 `px-3 py-1.5 rounded-lg` 的按钮和三枚档位钮归进 `.btn` 体系（开启态仍是 `.sel-chip` 实心橙档，色值不动），图标与文字之间交给 `.btn > * + *`。
  * `StorageHub.jsx`：云端安装行（窄屏竖排 `mt-3`、宽屏与电视横排 `ml-6`）、已安装卡片图标行、搜索 + 排序那一行、USB 行、QR 页脚与标题竖条，全部改边距。
  * `GameSection.jsx`：游戏卡纵向堆叠与封面左上角徽标堆叠 `gap-*` → `space-y-*`；体积那格改 `ml-2`。
  * `DonateView.jsx`：两栏 `gap-12 xl:gap-24` → `space-y-*/xl:space-x-24`，致谢行的 `gap-x-2 gap-y-1` 改成条目自身 `mr-2` + 分隔符 `mx-1.5`。
  * `PkgManagerFrame.jsx`：两处启动/失败状态块 `gap-4` → `space-y-4`。
  * 仍然留着 flex gap 的只剩 `ui/PayloadButton.jsx` —— 那是已经没人用的死文件，随下一批死代码清理一起删。

## v0.5.2.94 (CN-FORK)

### 🚀 Features & UX Improvements
* **骨架重构第 3 批：板块 / 页头 / 空态 / 加载态归一 + 开关与选择器收口**。配色契约（canvas/rail/card/inset 四档底 + 一档选中）与色值一行未动，本批只动结构与交互：
  * **新增四个共用件**：`ui/Section.jsx`（板块 = `.panel-card` + 板块头（图标 / 标题 / 计数徽章 / 右侧操作）+ 统一 body，`headless`/`bodyPad` 供卡中卡与自带头的卡片复用）、`ui/PageHead.jsx`（页头 + 粘顶）、`ui/EmptyState.jsx`、`ui/LoadingState.jsx`。病灶是同一个「板块」角色全站三套写法（`.panel-card`、手写 `bg-white/5 … rounded-2xl p-5 md:p-6`、`bg-white/[0.03] … rounded-ps-3xl`），空态九处各写一遍，spinner 四套。
  * **页头粘顶（`.head-bar`）**：以前列表一长，「这一页是什么、主操作在哪」立刻滚出视野，电视端和手机端要来回滚找按钮。底色用实色 rail 档，负外边距吃掉 `<main>` 的左右内边距让分界线铺满；限了列宽的页面（`max-w-5xl mx-auto` 这类）传 `bleed={false}`，否则粘顶条会横向伸出内容列去啃 main 的内边距。
  * **`ui/Switch.jsx`（全站唯一开关）**：设置页五个开关各自抄一份 `w-14 h-7 md:w-20 md:h-10` + `translate-x-7 md:translate-x-10` 的滑块（两份尺寸、五个副本），活动进程页又是另一套 `h-6 w-11`。改为轨道/旋钮尺寸只由尺度令牌给（新增 `--switch-w/--switch-h/--knob-d`，PS5 档整体抬大），旋钮靠 `ml-auto` 推到轨道右端，不再跟着尺寸改要同步两处。
  * **语言选择器不再「假按钮 + 隐形原生 select」**：原实现是 `opacity-0` 的原生 `<select>` 盖在一个假的 div 上，鼠标点得中但遥控器看不出自己站在什么控件上、聚焦环也画在隐形体上；改成真正的 `<select class="field">`。
  * **设置页结构整理**：`SettingRow` 只剩一种形状（图标 + 标题/说明 + 右侧控件，说明文字不再靠 `hidden md:!block` 写两份）；「点整行去别处」的四份副本（管理来源 / 日志查看器 / 更新日志 / 支持作者）收成 `NavRow`，整行即按钮、命中区从 24px 图标变成整行；分组标题走 `<Section>`；「更新日志」那段写死在 JSX 里的 45 条副本改为 `CHANGELOG` 数据数组 + 共用 `<Modal>`（原自制浮层不认 Esc/返回键、打开后焦点还在弹窗背后，且 `bg-[#1a1a1e]` 是脱离契约的第 6 种底色）。
  * **各视图接入**：常用入口（含二级页头返回钮 `back` 统一进页头，删除确认改走全站危险弹窗、操作结果改写进 Toast——原来那行状态小字在卡片网格一长就被顶到首屏之外）、游戏管理（页级标题上移到 `GameManageView`，板块只留小标题）、温控（从「自己写大页头」降为页内板块，避免同屏两个 24px 标题）、活动进程（筛选开关进粘顶页头、搜索条与管理页同形、Kill 按钮从 `opacity-50` 仅悬停显现改常显）、来源管理（删掉「手机一套 + 桌面一套」两份完全重复的行操作按钮，改一套响应式行）、从 USB 导入（按钮全量换 `.btn` 三档形态、按钮行改 `.stack-x`）、自动加载（序列行三个 32px 小方钮换 `.icon-btn`，空态换 `EmptyState`）、PKG Manager 卡片（自带板块头，设置页不再套一层卡中卡）。
  * **`.panel-head` 去掉 `gap: 0.75rem`**：flex 盒的 gap 在 PS5 老内核根本不生效（grid gap 生效），板块头的间距本来已由组件上的 `ml-3` 给，留着只会让人误以为它在电视上也起作用。
  * 新词条按 fork 规则只进 `locales-overlay/`（`common.refresh`、`app.buttons.close`、`entries.*`、`game.*`、`thermal.loading`、`manage_sources.*`、`settings.startup_title`、`autoload.move_up/move_down/remove_item` 共 16 条 ×2 语言），上游 locales 子模块未动。

## v0.5.2.93 (CN-FORK)

### 🚀 Features & UX Improvements
* **骨架重构第 2 批：打断式与反馈归一（弹窗 / 通知条 / 危险确认分级）**。仍然只动交互与盒模型，配色令牌不动：
  * **弹窗（`ui/Modal.jsx`）四处硬伤**：① footer 用 `flex gap-4`，PS5 老内核不支持 flex gap，「取消 / 确认」两个按钮在电视上是贴死在一起的，改用 `.stack-x`（`* + *` 外边距实现，窄屏自动竖排、≥768px 横排）；② 只要传了 `onClose` 就完全不画关闭入口，也不认 Esc/返回键，电视端打开弹窗只能二选一，现在标题右侧恒有一个 `.icon-btn` 关闭钮 + Esc 关闭；③ 面板 `overflow-hidden`，内容一长被切掉且遥控器滚不出去，改 `max-h-full overflow-y-auto`；④ 打开后焦点仍在弹窗背后，遥控器第一次按方向键没有起点，现在打开即把焦点送进面板（`role="dialog" aria-modal`）。新增 `dismissible={false}` 给「安装/上传中」这种不可打断浮层用。
  * **通知条（`ui/Toast.jsx`）重叠修复**：组件自己写死 `fixed top-8 right-8`，而 App 外层已经有一个 fixed 容器在排队，结果多条通知全落在同一坐标互相压字；现在条目本身走正常流，只由容器定位。驻留时长分级：成功 4s、错误 9s（原来一律 3 秒即逝，电视端来不及读完），并随时可用 OK/点击手动关闭。
  * **危险确认按可逆性分档**：`showConfirm(title, message, onConfirm, danger)` 新增第四参。此前每个确认弹窗都是实心红底白字，等于每个弹窗都在喊「危险」；现在删除文件、杀进程这类不可逆操作保留红档，覆盖安装/重装/移除来源这类可以再来的走主色橙（`.btn-primary`）。
  * **配套**：自动加载页「配置延时」弹窗的取消钮改 `.btn.btn-outline`，自定义延时那一行 `flex gap-3` 改 `.stack-x`、输入框改 `.field` 体系。
  * 未动：`canvas/rail/inset/sel-bg` 各档色值、字号尺度、后端接口。

## v0.5.2.92 (CN-FORK)

### 🚀 Features & UX Improvements
* **骨架重构第 1 批：电视端可达性底座（焦点可见 + 遥控器方向键导航 + 命中区尺度）**。配色令牌与「4 档底 + 1 档选中」契约一行未动，本批只改盒模型与交互层：
  * **焦点不再被抹掉**：`index.css` 原先是 `button:focus { outline: none }` 一行通杀，等于键盘与遥控器完全看不见「我现在在哪」。现改为鼠标点按无环、键盘/遥控器给 2px 实心橙环（`:focus-visible` + 老内核由 JS 手动挂 `.is-focus`，两条路都指向 `--color-action`，不引入第 5 种底）。
  * **新增 `utils/spatialNav.js`**：PS5 浏览器只有方向键 + OK + 返回，此前全站零按键逻辑。做法是按**几何位置**在当前可见的可操作元素里找方向上最近的一个来聚焦（同列优先，垂距 3 倍惩罚），方向上屏内没有候选时滚动 0.7 屏再找；OK 落在 `div` 实现的可点击行上转成一次 `click()`；返回键派发 `ps5-back` 交外壳裁决（先关浮层再退视图，安装中不许退）。不按页面逐个写 roving tabindex，上游新增页面不用配合改造。仅在 `isPS5` 下挂载，PC/手机的方向键行为不变。
  * **命中区尺度进令牌层**：`--ctl-h`（PC 44px / 电视 68px，由 `html[data-device]` 切换）与 `--row-h`（56px / 88px）。`.btn`、`.field`、`.nav-item` 改为 `min-height: var(--ctl-h)`（原 `.btn` 靠 padding 撑出的 ≈36px 达不到触屏/遥控器可用尺寸），新增 `.icon-btn`（正方形同档命中、图标本身仍是小尺寸，电视端图形同步放大到 1.75rem）与 `.row-link`（整行作为唯一点击/聚焦单元）。
  * **管理 Payload 页接入**：已安装的 Payload 卡片补 `role="button" + tabIndex`（否则遥控器永远走不进去），启动/删除/刷新三个图标钮换 `.icon-btn`（原 `p-2` 只有 32px 且 `title` 提示在触屏与遥控器上均不可见，补 `aria-label`），安装/更新按钮换 `.btn` 体系，USB 行「移入本机」换 `.btn.btn-outline`，分类头与来源头给到 `--ctl-h` 行高。
  * **排序控件改版**：原先是「隐形原生 `<select>`（opacity-0）盖在假控件上」，鼠标点得中、遥控器与触屏点不准，且看不见当前项；改为「分类 / 更新时间 / 名称」三枚芯片按钮，选中项用契约里唯一的实心选中档。搜索条改用 `.field`。
  * 顺带去掉这一页几处在 PS5 老内核上根本不生效的 `flex gap`（改 `.btn` 内部 `* + *` 外边距或 `space-x`）。

## v0.5.2.91 (CN-FORK)

### 🚀 Features & UX Improvements
* **补上 `.90` 板块并档漏掉的一块**：云端仓库「多来源」列表容器（`repo.xxx` 那一整块）原用字面类 `bg-ps-card`，仍读 card 档实色 `#0a101d`，在已经变平的板块里显得凸起一块；改用与其余板块同一条通道的 `bg-white/[0.03]`（收口后即 inset 档）。全站板块容器至此只剩一档底，`card` 档不再有组件引用。

## v0.5.2.90 (CN-FORK)

### 🚀 Features & UX Improvements
* **板块容器统一到一档底：`.panel-card` 的底色从 `card` 档并到 `inset` 档**（对应反馈「管理 Payload 与设置这两页的背景不一样 / bg-white/[0.03]、panel-card 改成和其他的相同」）：
  * 实测病灶：`.panel-card` 全站只有「管理 Payload」的三个板块在用（深色底 `#0a101d`），其它页的板块——设置 9 块、温控、游戏管理、常用入口、PKG 卡——都是上游手写的 `bg-white/[0.03]`，在 `.87` 收口层里被并进了内嵌件档（深色合成 ≈ `#05080e`）。同一个「板块」角色因此有两套底，相差 R5/G8/B15，浅色下也一样（纯白 vs `#fafbfd`）。
  * 现按用户指定把 `.panel-card` 改读 `var(--color-inset)`，两页起视觉同一档；`card` 档不再由板块容器使用，只留给 `bg-ps-card` 这类字面容器。板块内的行/输入框/徽章仍是 inset，靠双层叠加与描边区分。
  * 未动：圆角与描边尺度、字号间距、组件结构与后端。

## v0.5.2.89 (CN-FORK)

### 🚀 Features & UX Improvements
* **内嵌件再调透 + 图标盒去矩形底**（对应反馈「深色的管理 Payload 没改 / 浅色所有地方透明度依然不够 / 截图这种地方不要图标的矩形背景颜色，图标本身颜色可以鲜艳一点」）：
  * `inset` 内嵌件档 alpha 再降一半：深色 `rgba(74,106,181,.035)`→`.018`（压卡片上 `#0c1322`→`#0b1220`，与卡片只差一档亮度，「管理 Payload」里那些行/计数徽章的灰块基本化掉，层次只由发丝线承担）；浅色 `rgba(12,69,155,.042)`→`.02`（合成 `#f5f7fb`→`#fafbfd`，接近纯白），悬停档同步（深 `.07`、浅 `.05`）。
  * 图标盒不再有矩形背景：`bg-ps-blue/10`、`bg-white/5`、`bg-ps-blue/20 + border` 这类只包住一个图标的方底全部去掉（板块头、已安装/USB 条目、设置项、来源管理、从 USB 移动页），只保留原有的内边距与圆角占位；盒内的 `group-hover:bg-*` 悬停底色一并去掉，免得悬停又把矩形画回来。
  * 图标本身的颜色提鲜：浅色档动作色文字/图标从深阶橙 `#b8410a` 提到与实心底同值的鲜橙 `#f25c05`；深色档从 `#ff9152` 提到 `#ff7a2e`（老内核兜底那条 `.text-ps-blue` 写死值同步）。原先靠悬停才变橙的图标（已安装条目、设置项）改为恒常鲜橙。
  * 未动：`canvas` / `card` / `rail` 三档、选中态 `.sel-chip`、字号与间距尺度、组件结构与后端。

## v0.5.2.88 (CN-FORK)

### 🚀 Features & UX Improvements
* **只把「侧栏」与「内嵌件（列表行/输入框/图标块）」两档调淡，深浅两套主题同步**（上一版把卡片也整站改成半透明、结果合成后与原色几乎一样，属于改错方向，已回退）：
  * `inset` 改真半透明 tint：深色档 `rgba(74,106,181,.035)`（压在卡片上 ≈ `#0c1322`，旧值 `#0f1728`），浅色档 `rgba(12,69,155,.042)`（合成 ≈ `#f5f7fb`，旧值 `#eef2f8`）；悬停档同比例跟上。已安装的 Payload 那一类灰块因此明显收淡，落在卡片里只比卡片略亮一点。tint 用同色相蓝味而不是纯白纯黑，避免半透明后发灰。
  * `rail`（侧栏）淡到几乎与画布同色（深 `#070a12`→`#05080f`，浅 `#f6f8fc`→`#fbfcfe`），侧栏与内容区的分界改由那根发丝描边承担。**这一档刻意保持实色**：它同时是模态面板 `.glass-panel` 与粘顶条的浮层底，透明会让滚过的内容透出来。
  * 配套一处：自动加载 overlay 的列表粘顶标题条原用 `bg-black/20`（现等于半透明 inset），会透字，改 `bg-ps-surface`（rail 档，实色）。
  * `canvas` 画布与 `card` 卡片保持 .87 原值不动；类名、字号与间距尺度、组件结构、后端一行未改。

## v0.5.2.87 (CN-FORK)

### 🚀 Features & UX Improvements
* **全站「一档一色」收口：每套主题只留 4 档中性底 + 1 档选中底，浅色文字整档提黑**（对应反馈「浅色字体太灰暗、选中标记不明显、页面颜色太杂，同类型部件不要有多个背景色，所有页面」）：
  * 令牌层按角色重建（`index.css` 的 `:root`）：`canvas` 画布 `#04060b` / `rail` 侧栏 `#070a12` / `card` 卡片 `#0a101d` / `inset` 内嵌件 `#0f1728` / `line` 发丝线 `#1b2438`，外加唯一一档选中底 `sel`。深色档主内容区按「再沉一档」整体下沉（卡片 `#0e1424`→`#0a101d`、描边 `#1e2740`→`#1b2438`）。
  * 新增「一档一色」收口层（`index.css` 末尾，紧跟 `@tailwind utilities` 所以能盖住工具类）：把同类部件上混用的 `bg-white/5|10|20|[0.01~0.03]`、`bg-black/20|40|50`、`bg-zinc-600` 全部映射到 `--color-inset` 一个值（悬停只用 `--color-inset-hover` 做亮度反馈，不新增底色）；`border-white/5~20` 并到一根线；动作色薄染 `bg-ps-blue/5|10|15|20` 由 4 档浓淡并成 1 档。映射目标全是 CSS 变量，所以**浅色档只翻变量、不再各写一套 bg 重映射**（`light-theme.css` 因此从 231 行减到 ~150 行，也消掉了「骨架层一个色、其余另一个色」这类偏差）。
  * 选中态统一到 S2（实心橙芯片 + `.sel-chip`）：侧栏选中从「藏蓝底 + 左侧橙竖条」改为整块实心橙、活动进程「Show All」开关改为开启时整条槽变橙（旋钮恒白）、温控的「已启用」与档位选中、云端仓库当前优先序号、自动加载 overlay 的当前执行行一并跟过去；语义色各自也收成一档薄底 + 一根线（此前 `bg-red-950/90`、`/5`、`/20`、`bg-emerald-500/5|10` 混用）。
  * 浅色文字阶整档提黑：次要 `#606a80`→`#3a4661`、辅助 `#707a92`→`#46536e`、更淡 `#9aa3b5`→`#5a677f`，分组小标题/说明跟 `--color-ps-text-sub` 到 `#3a4661`；占位符是唯一刻意留淡的一档 `#8b95a8`。实心橙底上的字改走 `--color-sel-fg`（深档墨字 `#1b0e02`、浅档白字），不再写死 `#ffffff`。
  * 顺带清掉两个「颜色杂」的源头：此前一直吃 Tailwind 默认色的 `amber` / `green` 两个色族并进暖黄与薄荷绿（浅底同时补上 `text-amber-*` 的压深映射），并从 `tailwind.config.js` 删除全站无人引用的 `ink` 色阶与 `--color-ink*` / `--color-nav-*` / `--color-btn-solid*` / `--color-field-bg` 等死变量。
  * 保持豁免不收口的：`bg-white`（开关旋钮、进度条填充）、`bg-black/55~80`（封面叠层与模态遮罩，两套主题都保持深色 + 白字）、`bg-ps-blue/85`（封面角标）。
  * 未动：字号与间距尺度、组件结构与后端一行未改。本批次**只做到构建校验 + 本地提交，仍不实机打包**（等验收）。

## v0.5.2.86 (CN-FORK)

### 🚀 Features & UX Improvements
* **配色按实机观感改「墨蓝通透」（样张变体 B），底色去灰味、彩色只留给图标与状态标签**：
  * 深色档整体下沉：夜底 `#0c0f1a` → `#04060b`，侧栏 `#11162a` → `#080c16`，卡片 `#1b2033` → `#0e1424`，描边 `#2c3350` → `#1e2740`；蓝味从大面积铺底收敛到线条与侧栏选中底（`#141d33`）。文字冷白 `#eef2fb`、次要 `#7f8aa8`，把灰味从底子里提走。
  * 浅色档画布与卡片统一纯白 `#ffffff`、侧栏 `#f6f8fc`、描边 `#e7ebf3`、主文字 `#0c1220`；动作色实心 `#f25c05`、文字 `#b8410a`，结构墨色 `#1b2b54`。
  * 高饱和彩色只给状态/警示标签：成功青柠 `#99cc33` → 薄荷绿 `#2ee6a8`，警示 `#ffcc00` → `#ffd84d`，危险 `#ff5555` → `#ff6b6b`（浅底文字分别为 `#12996b` / `#bd7f0f` / `#a91f22`）。
  * 动作橙按档分色：深色档 `#ff7a2e`，浅色档实心压深到 `#f25c05`（`bg-ps-blue` 字面值不走变量，在 `light-theme.css` 单独补重映射，保证白底上橙底白字读得清）。
  * 修正一处旧遗留：浅色下 `text-red-*` 此前被映射成动作橙 `#b84d00`（暖棕改版留下的），与深色档的红色危险语义对不上；现回归真红 `#a91f22`，警示/危险两档语义在两个主题下一致。
  * 依旧只动令牌层三个文件（`tailwind.config.js` / `src/index.css` / `src/theme/light-theme.css`）+ 一处写死的投影色；类名、字号与间距尺度、组件结构、后端与数据流一行未改。

## v0.5.2.85 (CN-FORK)

### 🚀 Features & UX Improvements
* **整站配色改「藏蓝结构 + 亮橙动作」，并补上侧栏/页头/按钮的骨架层**：
  * 换色只动令牌层，不动任何类名：`tailwind.config.js` 把 `ps-blue` 由赤陶橘改亮橙 `#ff6600`、`ps-black/ps-surface/ps-card/ps-border` 改藏蓝夜阶（`#0c0f1a / #11162a / #1b2033 / #2c3350`）、中性 `zinc` 整阶换藏蓝灰（`#e8ecff…#0c0f1a`），语义色改青柠/暖黄/珊瑚；新增 `ink` 藏蓝阶与 `danger` 别名。全站 126 处 `text-ps-blue`、~180 处 `text-zinc-*` 等类名原样保留即自动跟随，上游后续沿用同类名也自动接色。
  * 深色档按「再深一些」把结构藏蓝从样张的 `#4a5fa5` 压到 `#33477f`，浅底上的动作色文字用亮橙深阶 `#b84d00` 保对比度，实心按钮仍用 `#ff6600` + 白字（与其它 `bg-ps-blue` 一致，避免两种橙）。
  * `index.css` / `light-theme.css` 的暖色字面量整体清扫到藏蓝阶（含滚动条、卡片悬停、Toast 底色、面板描边），`.label-caps` 改走 `--color-ps-text-sub` 变量，浅色主题翻变量即跟随。
  * 骨架层（`index.css` 新增，颜色全走 CSS 变量，浅色由 `light-theme.css` 翻变量整组跟随，无需 `!important` 重映射）：侧栏分组头 `.nav-group-title`、菜单项 `.nav-item` + 选中态藏蓝底与**左侧亮橙竖条**（不再整块实心橙，把唯一亮点留给主操作按钮）、统一页头 `.page-head` / `.page-desc` / `.page-actions`、三档按钮 `.btn` + `.btn-primary`/`.btn-outline`/`.btn-ghost`（危险语义 `.btn-danger`）、表单 `.field`。
  * 落地：侧栏（`App.jsx` 分组头与底色）、`NavButton` 选中态，以及「管理 Payload / 自动加载 / 活动进程 / 常用入口 / 游戏管理 / PKG Manager / 温控」各页头与按钮收口到骨架层——原先各视图各自复制的五份 `BTN_GHOST`/`BTN_PRIMARY` 字符串常量改为引用同一套类名。间隙用 `* + *` 外边距而非 flex `gap`（PS5 老内核 WebKit 不支持 flex gap）。
  * 未动：字号与间距尺度（含 `p-4 = 0.5rem`）、组件结构、后端与数据流一行未改。

## v0.5.2.84 (CN-FORK)

### 🚀 Features
* **「常用入口」灰点条目拉起后改开新标签，不再覆盖本页**：
  * 上一版（v0.5.2.83）为绕开弹窗拦截，程序就绪后用 `location.assign` 在同标签打开，代价是面板被目标服务顶掉、要回后退才能继续操作。
  * 现在改为在点击手势内同步 `window.open('about:blank')` 抢一个空白标签（异步里再开会被拦截，手势内开不会被拦），拉起成功才把它指向目标地址；面板页面完整保留。
  * 收尾：拉起失败或提示「已在运行」时关掉那块空白，不留一堆空白标签；若浏览器仍拦掉了新标签，则提示「程序已经起来了，但浏览器拦住了新标签，请再点一次该条目打开」——此时条目已变绿点，再点即走原生新标签。
  * 匹配、探活、防重复拉起、PKG Manager 专有路径全部沿用 v0.5.2.83，未改动。

## v0.5.2.83 (CN-FORK)

### 🚀 Features
* **「常用入口」灰点条目点击自动拉起（去写死，任意本机 payload 通用）**：
  * 触发条件：条目探活不通（灰点）**且**能在已安装 payload 里按名称对上唯一程序。对上就先拉起（`/loadpayload:`）再探活，就绪后同标签打开（弹窗拦截下 `window.open` 在 PS5 浏览器可能完全不可见，体感即「点了没反应」）。
  * 名称匹配：入口标题 vs payload 目录名 / `display_name`，「只留字母」归一化后双向前缀比较（丢版本号、变体、中文、符号），候选不唯一即放弃；本面板自身（`nayeplmtools*`）永久排除。新增 payload 无需改代码。
  * 防重复拉起：先查 `/processes_list`，同名进程已在跑就不再起第二个实例，改为提示「该程序看起来已经在运行，但这个地址从本机连不通（可能只监听了 127.0.0.1）」——正是只监听回环的本机服务（如 ShadowMountPlus :10101）的真实处境。
  * 不接管的情况：绿点条目、以及本机对不上程序的外网链接（NAS、路由器后台等），一律保持默认新标签打开。PKG Manager 仍走专有的 `/pkgmanager_launch`（那条路兼管内嵌版本安装）。
  * 探活与收录逻辑一行未动：`alive` 仍由主机侧 TCP 探活给出，自动收录的 LAN IP 改写保持原样；前端只读 `alive`，不另起一套判断。
  * 配套：`/list_payloads` 候选表随清单一起刷新（点击瞬间需同步判定，异步取完再 `preventDefault` 就来不及）；vite 代理补 `/processes_list`；mock 改用真实端口探活 + 拉起桩服务，dev 预览可端到端验证四条分支。

## v0.5.2.82 (CN-FORK)

### 🐛 Fixes
* **换版后旧页面残留：前端版本握手自动刷新**：
  * 现象：PS5 浏览器里面板是常驻 SPA 文档，AppCache 更新只在文档重新导航时才应用——换版重启守护进程后页面从不导航，旧版前端一直残留并在其上继续操作。
  * 修复：构建时把后端版本号烧进前端（vite 配置注入 `__APP_VERSION__`：构建时自动读 `include/pldmgr.h` 的 `MENU_VERSION`，环境变量 `APP_VERSION` 可覆写；dev server 恒为 `dev`）；运行时常驻比对 `/version`——挂载、页面重新可见、获得焦点、每 60s 各查一次，不一致即整页刷新；`sessionStorage` 计数上限 3，防服务端版本异常时刷新死循环；dev 自动跳过。原 `updateready` 兜底逻辑保留。
  * 说明：本版曾把「旧页面残留」当作点击拉起失败的根因，实机复核后不成立——真正的原因是拉起写死只认 PKG Manager，见 v0.5.2.83。握手本身仍是必要修复：0.5.2.81 及更早无此逻辑，换版后需手动刷新一次页面；自此换版自动收敛，界面与后端不再版本错位。

## v0.5.2.81 (CN-FORK)

### 🚀 Features
* **「常用入口」本机程序类入口点击自动拉起后打开（PKG Manager）**：
  * 后端：`GET /quicklinks` 对 host 为本机（`127.0.0.1` / `localhost` / 本机当前 LAN IP）且端口为 PKG Manager UI 端口（8844）的条目输出 `program=pkgmanager` 字段；本机 IP 每次请求只取一次，逐条仅做字符串比较。
  * 前端：这类卡片点击由前端接管，以**端口存活**为准——先探 `/pkgmanager_status`（即主机对 `127.0.0.1:8844` 的真实 TCP 探活）：端口存活 → 直接打开；不存活 → 调 `/pkgmanager_launch` 试着拉起（elfldr）并轮询端口，就绪后打开（最多 20s；launch 明确失败时 2s 缓冲后报错）。启动期间卡片盖「正在启动」遮罩；新标签页在点击手势内同步 `window.open`（规避弹窗拦截），先写占位文档避免白屏。其余条目保持默认新标签页打开。
  * mock 同步为真实端口语义：launch 起一个监听 `8844` 的桩服务、stop 关闭，`running`/`alive` 均来自真实 TCP 探活，dev 预览可端到端验证。

## v0.5.2.80 (CN-FORK)

### 🐛 Fixes
* **「常用入口」自动收录的磁贴链接改写为本机 LAN IP**：磁贴 deeplink 的 `http://127.0.0.1...` 收录时改写为 `http://<PS5 当前局域网 IP>...`（`pldmgr_get_local_ip()` 运行时自动获取；前缀整体匹配，不误伤 `127.0.0.10` 类地址；取不到 IP 保持原样），PC/手机点开即直达。本机 IP 并入同步闸门：换网/IP 变化触发重同步，旧 IP 条目按孤儿移除、按新 IP 追加，自愈。

## v0.5.2.79 (CN-FORK)

### 🚀 Features
* **「游戏管理」独立成页**：游戏磁贴板块从「常用入口」页整体抽出为 `GameManageView` 独立视图，侧栏「常用功能」组在「常用配置」下方新增「游戏管理」入口（手机端短标「游戏」）；「常用入口」页只保留 常用入口 + 温控管理 两板块。
* **「常用入口」自动收录已安装应用的 http 磁贴链接**：
  * 数据源：`app.db` 的 `tbl_contentinfo.pprDeeplinkUri`——列序号按建表 SQL 实时定位（不硬编码，跨固件稳）；凡 `http(s)://` 开头的磁贴链接全部收录，名称取 `titleName`、取不到回落 TITLE_ID。
  * 触发时机：不做定时扫描；`GET /quicklinks` 请求内顺带同步，以 `stat` 的 mtime+size 做闸门（严格 `==` 判无变化，PS5 开机时钟回退不误判），仅变化后的首个请求重建；解析失败不落闸门，下次请求自然重试。
  * 条目管理：新增 `src`（`auto`/`manual`）与 `hidden` 抑制表（≤64）——自动条目在界面标「自动」徽标；手动删除后进抑制表不再复活，手动重新添加即解除。总量仍上限 32 条，超出跳过。
  * 并发加固：`quicklinks.c` 读取/同步/增删加 `pthread_mutex` 串行，TCP 探活保持锁外；保存缓冲扩至 128KB。
  * 词条：`entries.auto_tag` 与自动收录说明进 `locales-overlay`（zh/en）；mock 增加自动条目演示。

### 🐛 Fixes
* **修复 JSON 响应中文被静默清空**：`pldmgr_json_escape()` 原实现只放行 ASCII 可打印字符（0x20–0x7E），非 ASCII 内容（游戏 `titleName`、仓库描述等）会被整段清掉；现对 ≥0x80 字节按 UTF-8 原样透传（JSON 字符串本允许直接内嵌 UTF-8）。

## v0.5.2.78 (CN-FORK)

### 🚀 Features
* **「常用入口」页大标题与侧栏文案解耦**：
  * 大标题改用独立词条 `entries.title`（zh-CN「常用入口」/ en-US「Quick Links」），侧栏菜单保持「常用配置」不变，不再共用同一词条联动。
  * 词条仅进 `locales-overlay`，不改 `locales` 子模块。

## v0.5.2.77 (CN-FORK)

### 🚀 Features
* **游戏磁贴「编号行」行末右侧标注游戏体积**：
  * 后端 `/game_list` 新增 `size`（字节）：镜像游戏取源镜像文件 `st_size`（正是 ShadowMountPlus 挂载时读的 `file_size`，秒出精确）；裸目录/LZ4 游戏用有上限的递归 `du` 累加普通文件 `st_size`。
  * 设备实查：app.db 的 `size` 列对这些 ShadowMount 游戏只存了几百 MB 的元数据足迹（地平线5的 145G 镜像被记为 1.39G），不可用，所以没走它。裸目录 du 有硬上限（深度 16 / 访问条目 20 万）防符号链接环与失控，且仅在 60s 快照重建时算一次，不轮询（设备目录条目很少，成本极低）。
  * 前端按 1024 递进格式化（B/KB/MB/GB/TB），`size<=0`（如内置 pkg）则该行不显示体积。

## v0.5.2.76 (CN-FORK)

### 🚀 Features
* **游戏磁贴新增 `LZ4` 格式档（ampr 实时解压变体）**：
  * 识别一类特殊的「文件夹」游戏：目录里带 `ampr_*.index` / `ampr_*.pak` 与被改名的 `eboot.bin.orig`（ampr 实时解压，底层资源 LZ4 压缩）。
  * 后端 `game_enum_dump` 新增 `gm_dir_is_lz4()`：当游戏判为裸目录（folder）时再扫一下目录，发现 `ampr_` 前缀条目则把 fmt 从 `folder` 改标 `lz4`。只多一次 opendir，不轮询。
  * 前端 `FMT_LABEL` 加 `LZ4` 徒标（靖蓝底）；mock 加一款 lz4 演示。

### 🐛 Fixes
* **重写 ShadowMount 格式探测（直连设备 FTP 校准，不再靠猜挂载表）**：
  * 根因：前几版都在挂载表（`getmntinfo` 扫 `/mnt/shadowmnt/`）里找格式，但设备 `persistent_image_mounts=0`——镜像只在游戏运行的那一刻才挂到 `/mnt/shadowmnt`，列表工具查询时镜像根本没挂载，格式表恒空 → 所有游戏一律回退 PKG。
  * 正解（经 `ftp://设备` 实地核对 `/user/app/<TID>/` 落盘文件）：
    * `mount_img.lnk`（镜像游戏独有）首行即源镜像真实路径，按其扩展名 `.ffpkg/.exfat/.ffpfs/.ffpfsc` 定档（照 `detect_image_fs_type`）；
    * 仅 `mount.lnk`（裸目录游戏）→ 指向 `/mnt/extN/<TID>-名称` 目录，记 `folder`，目录里带 `ampr_` 前缀则记 `lz4`；
    * 两者都没有 → 系统内置安装的 `pkg`。
  * 后端 `game_mgr.c` 删掉 `gm_build_mount_map/gm_fmt_from_fstype/gm_param_json_tid/gm_extract_tid` 整套挂载表逻辑，改为 `gm_fmt()` 直读 `/user/app/<TID>/mount_img.lnk`→`mount.lnk`；不再调 `getmntinfo`（图标解析仍按需查沙盒挂载，互不影响）。实机 10 款游戏全部命中：4×ffpfsc、5×folder、1×lz4。
  * 前端 `FMT_LABEL` 键名对齐后端小写取值（`ffpkg/exfat/ffpfs/ffpfsc/pkg/folder/lz4`），修掉上一版键 `ffPKG` 大小写不匹配导致的回退。

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
