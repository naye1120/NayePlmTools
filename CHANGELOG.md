## v0.6.0.180 (CN-FORK) — 2026-10-18 · 修复「刷新总落回仓库配置页」+ 预置源改用 payloads_v2.json（仓库图标接入）

> 用户口径（原文）：「无论在哪个页面刷新，刷新完最后都会在仓库配置页面。另外自定义仓库的图标没有接入过来。」

### ① 刷新落回「仓库配置」页 = 两套「当前页」机制打架

CN-FORK 的 localStorage 视图恢复（`pldmgrView`）与上游 v0.6.0 合入的 hash 路由（`useHistoryNavigation`）并存：
侧栏切页只走 `setView` 不更新 hash，导致 hash 长期停在最后一次 `navigate('sources')` 留下的 `#/sources`；
刷新时 seed 逻辑读到这个过期 hash → 覆盖 localStorage 恢复的页面 → 总落回仓库配置页。

- `App.jsx`：侧栏 / 手机底部导航 / 二级菜单三处 `setView(...)` 统一改为 `navigate(...)`，切页即同步 hash。
- `useHistoryNavigation.js`：`getRouteFromHash` / `formatHash` 补齐 fork 独有页面（`about` / `common_entries` / `game_manage` / `pkg`）；
  popstate 守卫去掉 `curView === 'dashboard'` 条件，fallthrough 将 `dashboard` 归一为 `storage`。

### ② 自定义仓库图标没接入 = 出厂预置源指向了裸数组 payloads.json

`sources_seed_defaults`（`src/sources.c`）首次开机预置的 `repo.630810.xyz` 源写死指向 `payloads.json`（裸数组），
而裸数组无法声明顶层 `icons_url`，后端 `resolve_icons_url` 直接 return → 永远不拉图标目录 → 全彩色兜底方块。
仓库侧已发布对象包装的 `payloads_v2.json`（带 `"icons_url": "icons.json"`），预置地址同步改指 v2。

- `src/sources.c`：slot 1 预置 URL `payloads.json` → `payloads_v2.json`。
- `frontend/mock-server.js`：两处 mock 预置源同步改指 v2，保持与真机一致。
- ⚠ 仅对**首次开机（sources.json 不存在）**的新装生效；已开机设备需在「管理来源」手动删旧源、加 v2 地址。

## v0.6.0.179 (CN-FORK) — 2026-10-17 · 「个性化软件」：两个开关真正写进主机 + 行标题图标 + 换图标/换背景

> 用户口径（原文）：「有些按钮关机开机后会被重置；同时在每个标题前边加上一个图标，
> 以及在置顶按钮前边加上（上传背景的按钮）。」
> 追问后确认：① 被重置的是**隐藏与置顶两个都**；② 置顶要**写主机、主屏真的固定**；
> ③ 上传按钮**背景和图标都要**，且「这个页面的布局随着功能的增多也可以改的更好看一些」；
> ④ 行标题前的图标要用**主机里该软件的真实图标**（读不到就用默认）。

### ① 「关机开机后被重置」= 两个开关各自把状态放在了**主机不认**的地方

逐条读代码 + 三份真机 `app.db` 快照（`_ftp_probe/vis_evidence/`）：

| 开关 | 旧行为 | 为什么会被「重置」 |
|---|---|---|
| 隐藏 | 写 `tbl_iconinfo_<uid>.visible`（这一层本身是对的） | 主机自己会重写这几张表：快照里账号表集合 1 张 → 4 张 → 2 张，新出现的行 `visible` 默认 1；而列表读的是**跨账号「与」**语义 → 藏好的自己翻回来 |
| 置顶 | **只写我们自己的** `/data/nayeplmtools/app_pins.json` | 主机压根不知道有这回事。更要紧的是：全仓库**没有任何代码按它排序** —— 用户按了等于没按，主屏顺序完全由主机决定 |

### ② 统一修法：意图留在面板，开机对账重放

沿用仓库里已有的那条路（`game_title_sync_all()`：主机把改名冲回原名 → 开机把用户改的名字重放回去）：

| 意图文件 | 重放函数 | 幂等判据 |
|---|---|---|
| `/data/nayeplmtools/app_vis_wanted.json` | `app_vis_sync_all()` | 跨账号「与」语义与主机现值一致 → **一次写都不发** |
| `/data/nayeplmtools/app_pins.json`（`.164` 那份，语义降级为**意图记录**） | `app_pin_sync_all()` | `tile_group_has()` 说主机固定列表里已经有了 → 跳过 |
| 我们自己的图（见 ④） | `app_tile_sync_all()` | 那一列已经指着我们这份文件 → 跳过 |

三条挂在 `app_personalize_sync_start_daemon()`（`src/app_personalize.c`；延迟 25s、
`pldmgr_utils_safe_mode()` 时**整条跳过**、`pthread_create` + `detach`），调用点在 `main.c`
里紧跟 `game_title_sync_start_daemon()`。所以整条链每次开机都无条件跑，且天然幂等。

**不替用户做决定**：置顶对账只重放 `true`，绝不把用户在主机侧自己固定的条目挪走；
三条对账都只碰「意图文件里有记录」的条目，其余一律不动。

### ③ 置顶写主机：`tbl_group_<uid>` 的 MyHome 组（与游戏磁贴同一套）

`POST /system_app_pin` 改走 `tile_group_set_pin()`（`.166` 给游戏磁贴用的那条），回包补上
`accounts`（写进了几张账号表）与 `idx`（置顶后第几位），前端才能说清到底发生了什么。
真机取证里 MyHome 组**本来就含 NPXS 条目**（NPXS40037 / 40047 / 40071），`idx` 就是主屏位置。
`app_pin_get()` 也改成读主机（`tile_group_has()`）—— 拿我们自己那份 JSON 当状态，会出现
「用户在主机上取消了固定、面板还显示已固定」。

### ④ 换图标 / 换背景：内置软件只能走**列指针**，不能走游戏那条链

真机取证（19 条内置软件全量）：`icon0Info = /system_ex/rnps/apps/<TID>/appdb/default/icon0.png?ts=…`
（系统**只读**区），`pic0Info` 出厂是 SQL `NULL`；而 `game_mgr_sce_sys_dir()` 只认 `/user/app`
那几个前缀，**对 NPXS 一律返回 0**。照游戏那条路走的结果只会是「面板说写好了、主屏一动不动」——
与 `.164` 的置顶是同一个病，这一轮不重犯。

改为：图放**我们自己的目录** `APP_TILE_DIR = /data/nayeplmtools/apptiles/<TID>/`，只把 app.db 的两列指过去：

    icon0.png   ← icon0Info 指它（1:1；前端先缩到 512×512 —— 主屏上就是一格方图。文件名照抄
                  主机原来那一份的结尾，`.118` 消融的结论）
    pic0.dds    ← pic0Info 指它（1920×1080 / BC1 DXT1，规格只有 game_title.c 那一份）
    bg.png      ← 面板预览用（`GET /app_tile_bg`；浏览器的 <img> 解不了 DDS）

* 写之前把**原始那一串**存进底片（`orig_icon0info.txt` / `orig_pic0info.txt`；**0 字节 = SQL NULL**）。
  「恢复默认」按原样写回 —— 原来是 NULL 就写回 NULL，不是空串（写空串主屏会照着一张不存在的图去画）。
* 六条新路由：`GET /app_icon?tid=`（自定义优先，否则回**主机自带那张** → 行标题图标就是它）、
  `GET /app_tile_bg?tid=`、`POST /app_tile_icon?tid=`、`POST /app_tile_background?tid=`、
  `GET /app_tile_icon_reset?tid=` / `GET /app_tile_background_reset?tid=`（后两条是**变更型 GET**，过 CSRF）。
* **不假成功**：`ok:true` 只说「**面板这一份**存好了」；主机 app.db 那一列到底写没写进去，
  由新增的 `host_ok` 字段（`true`/`false`）如实带出来，原因在 `message` 里
  （「已存到面板，但主机没写进去：…」）。前端按 `host_ok` 选提示档（false = 红色 9 秒 alert），
  并把 `message` **原样**显示 —— 与 `/game_rename` 的 `host_ok` / `host_note` 同一套口径。
  没有 `host_ok` 时前端只能靠嗅 `message` 的字符串来判断，太脆；这一轮顺手把它做成字段。
* 分层：`src/app_tile.c` 只放路径 / 版本号 / 落盘 / 底片（纯文件，不依赖 sqlite 与 stb），
  `src/app_tile_write.c` 放 PNG 规范化 + DDS 编码 + 写两列；`app_visibility.h` 与宿主测试只拉前者的头。
* ⚠ **尚未真机验收**：这条链写内置软件是否被 Shell 采纳，**只能插上电视点一次才知道**
  （游戏那条是消融验证过的，内置软件只是形状相同）。回滚路径：`git revert` 本提交即可；
  已经改写过的列可用弹窗里的「恢复默认」逐条还原（底片就在 `apptiles/<TID>/orig_*.txt`）。

### ⑤ 列表与接口的字段变化（冻结合约）

`GET /system_apps` 每条新增 `custom_icon` / `custom_bg`（面板里设过自定义图没有），
`pinned` 改为读主机；`POST /system_app_pin` 回 `accounts` / `idx`。
既有口径一字不改：`controllable:false` 的条目（两个内容保存库 / 编辑 / 光碟占位）后端照旧返回、前端整组不显示。

### ⑥ 弹窗重做（`SystemAppsModal.jsx`）

每行 = 真图标（`/app_icon`，读不到显示占位方块）+ 标题 + `[换图标]` `[换背景]` `[恢复默认]` + 两个开关；
自定义过的行加标记；背景做成整行底图 + 预览；忙碌态逐行显示；两个开关保留乐观更新 + 回滚，
上传/还原**不做**乐观更新。布局按功能变多重排（栅格化、说明收拢）。
成功提示不再写死一句「已写入主机」—— 改成显示后端回的 `message`、按 `host_ok` 分档
（这条改动的理由就是上面 ④ 那条「不假成功」：写死的那句话会把失败盖成成功）。

顺带补 `frontend/vite.config.js` 的 dev 代理：`/system_app`（`/system_apps`、`/system_app_visible`、
`/system_app_pin`）与 `/app_icon`、`/app_tile`（`/app_tile_bg`、两条上传、两条恢复）。
`.158` 加这个弹窗时就漏了第一条，于是 `npm run dev` 里整个弹窗永远「读不到清单」——
与文件里 `/game_`、`/tile_group` 两条注释描述的是同一类症状（请求被 SPA fallback 吞成
index.html，前端只看到 JSON 解析错误）。只影响本地预览，不影响真机。

### ⑦ 测试与构建

* 新增 `test/app_tile_test.c`（纯文件层）：tid 白名单与路径穿越、版本号**严格递增**（同一秒内连传两次
  也必须变大，否则前端 `?v=` 不刷新 → 用户看到「换图没生效」）、底片只留第一次、NULL 底片语义、重复删。
* 新增 `test/app_tile_write_test.c`（**写主机那一层**，合成 `app.db`）：这一层真机上不可达，
  所以判据在这里钉死 —— 写完那两列指着我们自己的文件（`<路径>?ts=<秒>`）、底片留的是**动手之前**
  那一串、「恢复默认」按原样写回、`pic0Info` 出厂 NULL **还原成 NULL 而不是空串**
  （写空串主屏会照着一张不存在的图去画）、主机读不到那一行时 `host_ok == 0` 且 message 说明白、
  非法 tid 与坏 PNG 一律 `-1`。图片用仓库自己的 stb 现编（真 PNG，不然「不是 PNG」那条分支测不出来）；
  `game_title.c` 按普通 TU 链接，故 `game_mgr_*` 打桩（同 `param_rename_test.c` 的手法）。
  夹具库必须用 `SQLITE_OPEN_CREATE` —— 照抄 `tile_db.c` 的纯 `READWRITE` 会得到一个
  「文件不存在 → 静默按没有这一行处理」的假夹具，测试会一路绿却什么都没测到（第一次就踩了）。
* `test/app_visibility_test.c` 扩：夹具补**真的** MyHome 组表（`tbl_group_222`/`333`，条目集合故意不一致）
  与形状不对的诱饵（`tbl_group_111`，必须被 `tg_is_group_table` 排除）；钉住 pinned 读主机、
  跨账号并集、置顶**不动** `visible`、已置顶时再置顶**零写入**，以及**两条开机对账**
  （主机改回来后重放 / 第二次零写入）。
* `tests/http_responses_test.c` 的「表 A（POST）」补 2 条、「表 B（变更型 GET）」补 2 条；
  `tests/fork_backend_stubs.c` 补齐新符号的桩（缺一个这套测试就链接不过）。
* `tests/run_http_tests.py`：370 项响应 + 232 项生命周期 + 610 项方法闸门 + 10,000 次真实环回
  + 64 请求 / 8 并发客户端，全绿。`test/run_host_tests.sh` 全绿；
  `tools/check_post_routes.sh`（分发 25 条 = 限长 25 条）、`tools/check_stack_frames.sh` 全绿
  （新 GET 分支全部抽成独立 helper —— `http_on_request` 的栈帧已在闸门附近，链里再开缓冲就撞线）；
  PS5 `make -j8` 零告警。
* 前端：`npm run lint` 0 告警、`npm test` 127 项全绿（新增 9 项弹窗测试）。
  顺带补 `frontend/mock-policy.js` 的方法闸门（不加则 mock 对两条新 POST 回 405，冒烟测试跑不起来）；
  `mock-server.js` 的两条上传也回 `host_ok`（mock 里恒 `true`，注释里写了怎么手动演练 `false` 那条分支）。
* ⚠ 顺带说明（与本轮无关）：`tests/run_config_tests.py`、`run_integrity_tests.py`、
  `run_repository_icons_tests.py`、`run_self_update_tests.py`、`run_storage_tests.py`
  这 5 个套件**在 HEAD（`d69379e`）上就是红的**（用 `git worktree` 拉干净树复现过），另开一轮修。

### ⑧ 出包

`NayePlmTools_0.6.0.179-me.elf`（NAYE00001 媒体栏）/ `-ga.elf`（NAYE00004 游戏栏）。

## v0.6.0.178 (CN-FORK) — 2026-10-17 · 修「升级了，/data/ps5_autoloader/ 里还是旧版本」

> 用户口径（原文）：「177 版本并没有实现写入这个位置」（截图 `/data/ps5_autoloader/` 里
> 只有 `NayePlmTools_0.6.0.176-me.elf`）；追问后回答：「我已经在载荷管理上传了，
> 但是 autoloader 并没有更新」，且 `autoload.txt` 里**确实**有 176 那一行。

### ① 「装完同步引导器」这条路，只有一条安装路径真的会走

同步逻辑是 `payload_mgr.c` 的 `ps5_autoloader_sync()`，逐条读调用点：

| 安装路径 | 旧行为 |
|---|---|
| 网页上传 `/manage:upload` | ✅ 会同步（但 resolve 失败时静默跳过，见 ②） |
| 从 U 盘导入 `payload_mgr_usb_move()` | ❌ **整条没调同步** |
| 仓库 / 自定义源安装 `sources_repo_install()` | ❌ **整条没调同步** |
| 自更新 `repository_install_self_update()` | ❌ 从不调同步（落点是 `payloads/pldmgr/`） |
| 启动自检 `payload_mgr_bootstrap_self_sync()` | ❌ **死代码**，见 ③ |

`HANDOVER.md:2896` 与 `AboutView.jsx` 一直写着「安装 payload 后同步登记到
`ps5_autoloader`（全局覆盖）」「装完自动同步越狱引导器」—— 实际上只有上传那一条兑现。

### ② 同步函数里三处静默 return：出错时不留任何痕迹

「txt 读不开」「没匹配到条目」「复制失败」在旧代码里做的事完全一样：什么都不做、什么都不记。
所以真机上「上传成功但引导器没变」根本查不出卡在哪。现在每条分支都写 `[AutoloaderSync]`
日志（含 `strerror`）。上传路径也不再靠 `slot_resolve()` 反解路径 —— 解析失败时旧代码静默
跳过，现在直接用 `slot_commit_file()` 填好的 `meta.payload_path`。

### ③ 启动对账是 slot 化之前的死代码

`payload_mgr_bootstrap_self_sync()` 扫的是 `payloads/<工具名>/`，而 v0.6.0 起载荷落在
`payloads/local|repo/<key>/`（`slot.c`），那个目录不存在 → 直接 return，整段从未生效。
重写后：在 `payloads/pldmgr/`（自更新落点）与全部槽位里找**最新**的一份自己，只在比在册
版本新时才换进引导器（**绝不倒退**）；没登记时不新登记（登记只发生在用户明确安装时）。
自更新成功后的同步走的正是这里：`main.c` 交棒重启 → 新版本启动 → 对账，
所以不必在仓库层再挂钩子。

### ④ 补登记，并沿用现有命名

新增 `allow_register` 参数：本工具（`NayePlmTools`，兼容旧名 `pldmgr_*`）没登记时追加
登记（`!1000` + 文件名），第三方载荷仍旧「已登记才换版本」，不会被顺手塞进开机自启。
引导器目录里的文件名沿用现有约定 `NayePlmTools_<版本><变体>.elf`（`-me`/`-ga` 由
`-DTILE_VARIANT_GAME` 决定），没有引入新名字。

### ⑤ 测试

新增 `tests/run_autoloader_sync_tests.py` + `tests/autoloader_sync_driver.c`（进 `make test-host`）：
把 `include/pldmgr.h` 的数据路径改写到临时目录（同 `run_storage_tests.py` 的做法），用真的
`payload_mgr.c`/`slot.c`/`sources.c` 跑 9 个用例 / 130 条断言 —— 换版本、补登记、第三方不
登记、第三方换版本、U 盘导入同步、启动对账（对齐 / 不倒退 / 不新登记）、0 字节残包自愈。
顺带给 `PAYLOADS_STORAGE_DIR`、`PS5_AUTOLOADER_DIR`、`PS5_AUTOLOADER_CONFIG` 补
`#ifndef` 保护（宿主测试用 -D 覆盖，同 `BASE_DATA_DIR` 那批）。

### ⑥ 出包

`NayePlmTools_0.6.0.178-me.elf`（NAYE00001 媒体栏）/ `-ga.elf`（NAYE00004 游戏栏）。

## v0.6.0.177 (CN-FORK) — 2026-10-17 · 修「无缘无故 405」+ 多媒体远程备份

> 用户口径（原文）：「很多功能无缘无故 405」；另要求设置页加**多媒体存档配置**（SMB/FTP 远端 +
> 自动备份开关），多媒体管理页加**远程同步**按钮。

### ① 405 的根因：方法闸门那张表只登了上游自己的 14 条

`src/http_server.c` 的方法闸门原话是 `is_mutation_route(url) ? "POST" : "GET"`，而
`is_mutation_route()` 里的 `routes[]` 是**上游**的 14 条 POST 路由（`/manage:upload`、`/set_config`…）。
fork 从 v0.5.1 起自己加的 **25 条 POST 路由一条都没进表** —— 闸门要求 GET、前端发 POST，
于是 `Allow: GET, OPTIONS` + **405**。这不是「偶发」：每加一条 fork 路由就多一个 405，
`.176` 时点「删除游戏 / 改名 / 挂载卸载 / 上传存档 / 上传 PKG / 设背景音乐」全是这个症状。
前端另有两处裸 `fetch('/loadpayload:…')` 打的是 POST-only 路由（`GameDetailModal.jsx:205`、
`CommonEntriesView.jsx:220`），同一批。

**为什么一直没被测出来**：`tests/run_http_tests.py` 在这个 fork 上**从来没链接过** ——
见 ⑤，跑不了的测试和跑绿的测试在报告里长得一样。

### ② 修法：一张表拆成三张，各管一件事

| 表 | 谁用 | 内容 |
|---|---|---|
| `is_post_route()` | 方法闸门 + CSRF（POST-only） | 上游 14 + fork 25 = 39 条 |
| `is_get_mutation()` | **只进 CSRF，不进方法闸门** | 11 条变更型 GET |
| `is_mutation_route()` | 仅 = 两者并集，**只给 CSRF** | 50 条 |

- **表 B 绝不能并进表 A**：那 11 条（`/fan_control`、`/quicklinks_add`、`/game_media_delete`、
  `/pkgmanager_launch`…）前端**就是按 GET 调的**，并进 A 会把它们从「能用」变成 405 ——
  修一个 bug 造出另一个。两张表的形状互为反面，所以 `tests/http_responses_test.c` 里
  两边一起断言（POST 要放行 + GET 要 405；GET 变更型反过来）。
- **`psaccounts_wants` 的 `GET, POST`** 走新增的 `in_method_list()`（逗号分段比对），
  **只给方法闸门**、不并进 CSRF 层；405 的 `Allow` 头用 `snprintf("%s, OPTIONS", allow)`
  追加 OPTIONS，而 `Access-Control-Allow-Methods` **不带** OPTIONS（`tests/run_http_tests.py:260`
  钉死 `== "POST"`）。
- **顺带堵上的一个 CSRF 洞**（读 HEAD 代码实证）：HEAD 的 CSRF 校验挂在同一个
  `is_mutation_route()` 上，那 11 条变更型 GET 因此**完全没过 CSRF** —— 面板在 LAN 内无鉴权，
  用户浏览器打开任意网页，那页面用 `<img src="http://<PS5>:8911/game_media_delete?tid=…&id=…">`
  这种简单 GET 就能删掉他的截图/存档（简单请求不预检，浏览器照发）。现在它们进了表 B，
  跨站的 `Origin`/`Referer` 会被 `same_origin()` 挡成 403。

### ③ 新功能：多媒体的远程备份（**独立一份配置**）

- 设置页存档配置格下面新增「多媒体存档配置」弹窗（`MediaArchiveConfigModal.jsx`）：
  SMB / FTP 远端 + 「自动备份」开关；配置存 `MEDIA_ARCHIVE_CONFIG`，
  **不与「设置 → 存档配置」共用**（用户要求各配各的）。配置形状与存档一致，
  因此直接复用 `saveConfig.js` 的 `MODES/MODE_FIELDS` 与现成的 `/game_save_test` 连通性探测。
- 手动同步 `POST /game_media_sync`：有选中推选中、没选中全量；走 `GJ_KIND_SYNC`
  占全局任务槽；body 上限 1MB；响应形状与 `/game_media_pack` **逐字同形**，HTTP 立即回 job_id。
- 自动备份：唯一触发点是 `/game_media` 拉取成功后 `mr_auto_kick()`（铁律②：不轮询）；
  不占任务槽（`g_mr_mu` trylock）。
- 账本在本机 `MEDIA_BACKUP_STATE_DIR/<tid>.json`（只记 size 不记 mtime，理由见
  `media_remote.c` 文件头：媒体文件只增不改，多记一个字段就多一处会说谎的地方）；
  远端布局 `<base>/<TID>/media/<rel>`，`sr_media_rel_ok` 末段长度收紧到 1..200。
- 传输复用 `save_remote.o` 的 `sr_push_media`（step 回调签名改成 `int (*)(void*,int,int)`，
  cancel 由返回值传递而非调 `gj_cancelled()` —— 两个状态源会打架）。
- `mr_state_save` 顺带修掉 `fclose` 双调用（UB）。

### ④ `tests/run_http_tests.py` 在这个 fork 上从来没链接过

`tests/http_backend_stubs.c` 只接住了上游路由，fork 的 **79 个后端符号**全是 undefined reference。
**为什么没人发现**：`test/run_host_tests.sh`（fork 自己那套）**不包含**这一条，
`.176` 记的 `overall: PASS` 与它无关；`make test-host` 又排在 `run_integrity_tests.py` 后面，
在本机被 openssl 卡住，永远走不到 http 这一条。

- 补 `tests/fork_backend_stubs.c`：79 个桩，三条规矩（只写 JSON/回码、绝不碰文件系统、
  绝不起了线程；「起任务」类一律回失败）。
- `tests/http_responses_test.c` 新增 **98 条方法闸门断言**，把 405 那个 bug 变成封条。
- **负控验证过**：把 `ROUTE_GAME_PREFS_SET` / `ROUTE_GAME_DELETE` 从表 A 删掉 →
  立刻红 6 条（`POST → 405` 正是用户报的现象，`GET → 200` 是 POST-only 契约被绕过）。
- `run_http_tests.py` 补 `-Isrc`（桩要照 `psaccounts/api.h` 的原型声明）+ 链桩文件；
  `--config-only` 模式下那两个 config 桩按 `PLDMGR_TEST_REAL_CONFIG` 闸住（真 src/config.c 提供）。

### ⑤ 验证结果

- **ELF**：`prospero-clang` 链接通过，0 error / 0 warning（`media_remote.c` 已进 `SRCS`）。
- **出包**：`bash tools/build_tiles.sh` → `-me`（NAYE00001）/ `-ga`（NAYE00004）双变体，
  版本号烧成 `.177`。
- **宿主**：`tests/run_http_tests.py`（默认 + `--autoload-only` / `--logs-only` / `--config-only`）
  **360 checks / 0 failures**；`bash test/run_host_tests.sh` → `overall: PASS`；
  `tools/check_post_routes.sh` 三张表一致（disp=23 / lim=23 / gate=24）。
- **端到端**（`--serve` 起真 MHD，curl 打真实 loopback）：
  `POST /game_delete|/game_media_sync|/pkgmanager_upload` = 400、`/system_app_pin` = 200
  （**都不是 405**）；`GET /game_delete` = 405；`GET /fan_control`、`/quicklinks_add` = 200；
  跨站 `GET /quicklinks_add` = **403**。
- **前端**：`eslint . --max-warnings=0` 0 错；`node --test` **118/118**；
  `vite build` + `tools/check_frontend_bundle.mjs` 通过；`mock-policy.js` 改成两表镜像
  （`postRoutes` / `getMutationRoutes`），CSRF 判据从「仅 POST」改成「总表都查」。

### ⑥ 既有失败（在干净 HEAD 上逐个复现过，与本次无关）

`git worktree` 拉了一份 `0971d90` 对照：`run_repository_icons_tests`、`run_self_update_tests`、
`run_storage_tests`、`run_config_tests`（`config_driver.c:44` 的断言 + driver 自己漏的 38 字节）
在 HEAD 上**同样红**；`run_integrity_tests` 是本机 openssl 拒收 `-days -1`（环境）；
`run_mock_security_tests.mjs` 第 22 行挂（mock 目录没有 `icon_key` fixture，
`mock-policy.js` 那段压根没被执行到）。即 `make test-host` 在 HEAD 上本来就是红的，
本次把它从「6 个失败（含 http 链接不过）」降到「5 个失败，http 全绿」。

### ⑦ 顺带钉死：脚本行尾（Windows 检出坑）

`test/run_host_tests.sh` 在**用户自己那台 Windows + WSL 机器上跑不起来**：本机
`core.autocrlf=true`、仓库又没有 `.gitattributes`，检出时把 `*.sh` / `Makefile` 转成了 CRLF，
bash 读到 `set -u\r` 直接 `invalid option`，`cd "$(dirname "$0")/..\r"` 报 No such file or directory。
提交里的字节**一直是 LF**（`git ls-files --eol` → `i/lf`），坑只在 Windows 检出侧，
所以一直没人撞见 —— 本次为了坐实 ⑤ 那句 `overall: PASS`，只能先 `sed 's/\r$//'` 滤一遍 CR
再喂 bash。新增 `.gitattributes` 把 `*.sh` / `Makefile` / `*.mk` / `*.py` /
`.github/workflows/*.yml` 钉成 LF（当时工作区共 **244 个**文件被转成 CRLF，
根目录的 `build_release.sh` / `deploy.sh` / `build_deps.sh` 同样受影响）。

---

## v0.6.0.176 (CN-FORK) — 2026-10-14 · 上游 v0.6.0 全量合并

> 用户口径（原文）：「熟悉一下项目，然后将上游最新的更新拉取并合并」。
> 合并前确认三点：**全量合并** / **保留 `/data/nayeplmtools/` 数据目录** / 出包版本 **0.6.0.176**。

### ① 上游区间与合并方式

merge-base `e4e72dc`（v0.5.2）→ `fe5ca81`（v0.6.0），在临时分支 `merge-upstream-060` 上做**全量合并**
（保留 fork 全部改动，不做「只挑上游新文件」的窄合并）。冲突集中在 16 个前端文件，逐个人工裁决：
**fork 语义优先、上游新增能力并入**。C 后端无文本冲突（fork 的文件与上游新文件不重叠），
但编译期/运行期有 4 类必须处理的适配（见 ②）。

### ② fork 侧必须处理的 4 类适配（全部已落地）

**1) 栈帧红线（最重要）**：上游 v0.6.0 新增/改写的 4 个函数栈帧超 32 KB ——
`slot_write_meta` **111,064 B**、`sources_multi_repository_list_json` **66,584 B**、
`payload_mgr_list_json` **53,848 B**、`slot_migrate_legacy` **42,488 B**。真机 HTTP 线程栈只有
64 KB（`MHD_USE_THREAD_PER_CONNECTION`），宿主 8 MB 栈**全绿**、真机必然 SIGSEGV。已全部堆化：
`SlotMetaJsonScratch` / `IconMetaJsonScratch` / `RepoPayloadJsonScratch` / `PayloadListJsonScratch`
四个 malloc 结构，`ensure_icons` 加 `noinline` 防被 `-Os` 内联回调用方。**上游 CI 没接
`tools/check_stack_frames.sh`**，这条只能靠 fork 自己守。

**2) `include/pkgmanager.h` 的 include 必须用尖括号**：上游 `tests/` 的宿主 runner 会把
`pldmgr.h` 打补丁（数据目录重定向到临时目录）后写进 build 目录，并以 `-I<build>` **先于**
`-Iinclude` 传入。`pkgmanager.h` 原先用 `#include "pldmgr.h"`，引号 include 会**相对自身目录
(`include/`) 解析**、拿到**另一份** `pldmgr.h`，于是同一 TU 载入两份、`#pragma once` 按路径
去重失效 → `SCAN_DIRS` 等重定义、编译失败。改成 `#include <pldmgr.h>` 后与调用方走同一条
`-I` 搜索链、收敛到同一文件；生产构建 (`-Iinclude`) 与 fork 自有宿主测试行为不变。

**3) 数据目录改名连带测试夹具**：fork 的 `/data/pldmgr` → `/data/nayeplmtools`、配置文件
`nayeplmtools_config.txt`、U 盘库目录名 `nayeplmtools`。上游 runner 的 `.replace('/data/pldmgr', …)`
在 fork 下**空转**，需补第二次替换；U 盘夹具目录名、配置文件名也要跟着改。

**4) fork 的 config.c / http_server.c 引 cJSON**（上游不引）：宿主 runner 需补
`-Isrc/psaccounts/third_party` 并链 `src/psaccounts/third_party/cJSON.c`（生产构建经 Makefile
`SRCS` 链入，行为一致）。

### ③ 上游 v0.6.0 新增的构建门槛，把 fork 既有 lint 债变成硬阻断

上游把 `frontend/package.json` 的 `lint` 改成 `eslint . --max-warnings=0`，并**新增
`prebuild: npm run lint && npm test`** —— 于是 `npm run build` 会先跑 lint + 单测。
fork 里有 **9 个既有 lint 错误**（4 × `react-hooks/set-state-in-effect`、4 × `no-unused-vars`、
1 × `no-redeclare`），HEAD 时代因为**没有 prebuild** 所以从不影响出包，现在会直接卡住构建。
本轮按最小改动清账：`set-state-in-effect` 四处加**定向 disable 注释**（`load()` 内部先 `await`
再 `setState`，并非同步 setState，规则偏保守）；`atrac9/container.js` 的 `catch (e)` 改 `catch {}`；
`encoder.js` 三个未使用符号加 `_` 前缀（`varsIgnorePattern: '^[A-Z_]'` / 新增的
`argsIgnorePattern: '^_'` 正好放行）；`vite.config.js` 删掉多余的 `/* global process */`
（上游 eslint 配置已对 `*config.js` 注入 `globals.node`，同时声明触发 `no-redeclare`）。

### ④ 验证结果

- **ELF 出包成功**：`NayePlmTools_0.6.0.176.elf`，12,367,448 B，版本号已烧成 `.176`。
- **栈帧守卫全绿**：`=== check_stack_frames: 全部函数栈帧 <= 32768 B ===`。
- **fork 自有宿主测试**：`bash test/run_host_tests.sh` → `==== overall: PASS ====`。
- **前端**：`eslint .` 0 错；`npm test` **118/118**（在 WSL 里跑；Windows 宿主会因
  「node 内再 spawn node」EBUSY 假失败，属环境问题）。
- 上游 `tests/`（复数）自带套件：17 个 runner 中 11 个 PASS；剩余为 fork 结构差异所致，
  **有意留 backlog**，详见 `FORK-CN.md` §4.1「已知欠账」。fork 的验证门槛始终是
  `test/run_host_tests.sh`（单数），不是上游那套。

### ⑤ 关于页跟进（真机看后反馈）

- **「上游版本」没更新**：`frontend/src/components/views/AboutView.jsx` 里那行是**写死的** `v0.5.2`
  → 改成 `v0.6.0`。
- **版式**：用户口径「红框圈起来的东西放到绿框的位置」—— 原作者捐赠入口（`支持 itsPLK →`）
  从右列顶部搬到 fork 信息表**下方**（左列底部），右列只留本 fork 收款码 + 仓库入口。
- **页内更新日志**：`AboutView.jsx` 的 `CHANGELOG` 加 `2026-10-14` 分组，写清合并 v0.6.0 带来的
  能力（获取载荷页重做 / 载荷详情页 / 收藏存主机 / 返回键语义 / 荷兰语）。

### ⑥ 出包与推送

- **出包**：`NayePlmTools_0.6.0.176-me.elf`（NAYE00001 媒体栏）/ `-ga.elf`（NAYE00004 游戏栏），
  各 12,367,448 B。⚠ 内嵌标题短哈希 = **出包时的 `HEAD`**，所以必须**先提交再出包** ——
  本轮第一次出包时还没提交，标题烧成了 `DEV`，删掉 `frontend/dist` 重出后才对。
- **真机**（`192.168.1.214`）：`.176` 已推（推的是 **`-ga`** 变体，与真机上装的游戏栏磁贴一致），
  `GET /version` → `0.6.0.176`；启动日志里能看到合并后的存储迁移跑通
  （`[PLDMGR] Migrated: /data/nayeplmtools/payloads/ShadowMountPlus/`）。
- **代码推送**：用户口径「推送只 github」—— 只推 `origin`（`github.com/naye1120/NayePlmTools`），
  本地 `cn-fork` → `origin/main`；不动 `fork` / `upstream` 两个远程。
- **GitHub Release**：`v0.6.0.176` 已发（本仓库**首个** Release），附件为 `-me` / `-ga` 两个 ELF。
  同时把上游带来的 `.github/workflows/release.yml` 改成 fork 口径 —— 上游原版上传的是
  `pldmgr_v${VERSION}.elf`，与本仓库产物名 `NayePlmTools_${VERSION}[-me|-ga].elf` 不符且只出单变体，
  手动触发必然在 `gh release create` 那步失败；现改为在 SDK 容器内跑 `tools/build_tiles.sh`
  一次产出双变体并同时上传。⚠ 该工作流尚未在 CI 实跑验证（需现建 SDK 镜像）。

---

## v0.5.2.175 (CN-FORK) — 2026-10-13

> 用户口径（原文）：
> ① 「你还要告诉我本地快照为什么要进行退出和重试，而且发生记录也没有写到屏幕上」
> ② 「你干活利索点，别弄一些乱七八糟没用的测试对比啥的，太浪费时间。现在调整一个逻辑，
>    存档的本地快照保存和往远程推送的时机是一起的，把所有的所谓的静默窗口全部去掉。
>    只有这一个时机和手动。」
> ③ 「参考与致谢中差了好多，你把参考过的项目都写上，写全。」

### ① 删掉「静默窗口」与延迟重试 —— 快照只在两个时机发生

`.172` 的根因是：`gs_snap_take()` 里有个 `GS_SNAP_QUIET_SEC`（真机 20 秒）——
`now - src.max_mtime < 20` 就返回「存档刚被写过，等下一轮」。那句守卫写在还有**定时守护**
的年代（30 分钟一轮，「下一轮」真的会来）；`.154` 删掉定时守护、只留「游戏退出」这一个
触发点之后，退出是**一次性事件**，而退出那一刻主机正好把存档写下去 → mtime 恒为「刚刚」→
**每一次退出都被挡掉，本地快照永远不生成**。`.172` 的补救是「排队重试（≤40s）」。

`.174` 按用户口径把两层一起删干净：**静默窗口整条删除，`.172` 的延迟重试队列也删掉**。
现在快照只有两个时机 ——

| 时机 | 位置 |
|---|---|
| 游戏退出 | `game_auto_plan()` 的 `GA_ACT_SNAP`，**与 `GA_ACT_PUSH` 相邻**，共用同一次 `GA_EXIT_SETTLE`（`settled` 变量只等一次） |
| 手动「立即备份」 | `POST /game_save_snap {action:"run"}` |

删除清单：`src/game_saves.c` 的 `GS_SNAP_QUIET_SEC` 定义与静默窗口分支、`gs_snap_take()`
的 `int *skip_reason` 出参、`include/game_saves.h` 的 `GS_SNAP_SKIP_*` 与
`game_saves_snap_run_now_ex()`、`src/game_auto.c` 的 `GA_SNAP_RETRY_MAX` / `g_pend_snap[]` /
`ga_pend_snap_arm()` / `ga_pend_snap_tick()`、`test/run_host_tests.sh` 里三处
`-DGS_SNAP_QUIET_SEC=0`（宏已不存在）。**唯一保留的跳过理由 = 「内容与最近一份相同」**。

### ①b 快照**成功**时发一条主机系统通知

用户口径：「游戏结束时存档本地快照成功的时候要 `pldmgr_notify` 一条信息」。

`src/game_auto.c` 的 `GA_ACT_SNAP` 分支在 `made > 0` 时补一条
`pldmgr_notify("本地快照完成\n%s · %s\n（游戏退出时自动备份）", tid, smsg)`。
理由同 `.147` 给推 / 拉加通知那条：退出游戏那一刻用户往往已经回主屏、不看面板了，
系统弹窗是唯一能**当场**告诉他「存档安全落地」的通道。

**只发成功这一档**：「没拍成 / 没得拍 / 失败」刻意不发 —— 那是负向结果，弹窗只会平白
吓人；它们已经进了「本地快照 → 最近活动」与 `pldmgr.log`。
宿主测试 `test/game_auto_test.c` ⑩ 用现成的 `pldmgr_notify` 桩钉住四件事：成功发**恰好一条**、
带游戏编号、带「已生成」、**没拍成时一条都不多发**。

### ② 「快照活动」上屏 —— 负向结果不再只躺在日志里
用户口径：「发生记录也没有写到屏幕上」。原来快照的**负向结果**（内容没变 / 没得拍 / 拍失败）
只写 `pldmgr.log` 与全局 100 行内存环（`/log`、`/events`），而前端从不读它们；「本地快照」
那块界面只列**已经存在**的快照 → 负向结果**一个像素都不落**，用户看不到「到底跑没跑」。

新增一条只读接口 + 一块界面：

- **后端** `src/game_saves.c` 的 64 条事件环（`g_snap_ev[]`，全 `static`，不占栈），
  `game_saves_snap_ev()` 发射、`game_saves_snap_ev_json()` 按时间升序回**最近 N 条**；
  `GET /game_save_snap_log?tid=&n=`（`n` 默认 16、封顶 64；tid 空 = 全部游戏；
  6KB **栈**缓冲 + `queue_json`，不走 `alloc_response_buffer` 的 1MB）。
  ⚠ 它是被界面轮询的 GET，已登记进 `is_noisy_route()`。
- **前端** `SaveManagerModal.jsx` 的「本地快照」块新增「最近活动」子段，弹窗打开期间
  **每 5 秒**拉一次（独立 effect，与存档列表的一次性 `load()` 分开，避免列表每 5 秒重渲染）；
  `gameApi.js` 新增 `getSnapEvents()`；mock 也真记事件（含**跳过**），`/reset` 会重播基线。

**为什么不直接复用 `/log`**：内存环里那 100 行**没有时间戳**（时间戳只写进文件），而且会被
其它模块冲掉 —— 拿它当界面数据源既没有时刻、又随时丢记录。

### ③ 顺手修掉一个真 bug：快照名字可以「倒退」

写 ② 的端到端用例时暴露的（`.140` 起就有）：`gs_snap_take()` 的撞名循环只判「这个名字在不在」，
于是被 `gs_snap_prune` 删掉的名字会被**再次复用**。`keep=1` 时表现为「新拍的那份名字比活着
的那份还旧」→ 下一轮 prune 按名字排序删「最旧」→ **把刚拍的删掉、留下旧的**；`keep>1` 时
表现为名字乱序 → 每轮都判「内容变了」→ 重复快照、份数虚高。
修法：新名字必须**严格大于**已有的最新那个（`strcmp(cand, newest) > 0`）。

### ④ 「参考与致谢」补全

上一版只列 5 条（还是上游 README 的一个子集）。现在按来源分四组列全（22 条）：
上游与 SDK（itsPLK 六个仓库 / John Törnblom 的 SDK / ps5-payload-dev 的 pacbrew-repo、
websrv、offact）、游戏与存档（drakmor 的 ShadowMountPlus、earthonion 的 garlic-savemgr、
manos555555、Pharaoh2k）、音频与图像（blackbearreloaded、Thealexbarney 的 LibAtrac9、
nothings 的 stb）、风扇与系统（drakmor 的 ps5-hwinfo / fan_target、owendswang、BenNoxXD）。
`README.md` 的「致谢」同步改写。

### ⑤ 出包（产物指纹）

`NayePlmTools_0.5.2.175-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,294,104 字节**，md5 `e2ffe6674f6c47719e28ec92fe3f0ab6` /
`2b0299ff1d36fcc2dd7ce197c1006008`。标题
`NayePlmTools v0.5.2.175 by naye (6a187dd, …)`（真实 commit 哈希，非 DEV；
`[[TITLE_PLACEHOLDER]]` 已消费；`.174` 残留 0；`frontend/dist/index.html`（1,190,949 字节）
与 ELF 内嵌 blob 在偏移 `+2232672` **逐字节相等**）。

`pkg_175_verify.sh` **ALL CHECKS PASSED** —— 继承 `.170`～`.173` 全部断言，新增 `.175` 的：
dist 与 ELF 必须有「最近活动」/`snap_events_title`/「与推送远程同一时刻」/「主机系统通知」；
dist 里 `排队重试`、`存档刚被写过` 残留 0；ELF 必须有 `/game_save_snap_log`、
`游戏退出，检查本地快照`、`内容与最近一份相同，未新增`、`本地快照完成`，
以及两条致谢条目（`drakmor/ShadowMountPlus`、`Thealexbarney/LibAtrac9`）。

宿主套件 `overall: PASS`（`game_saves` **266**、`game_auto` **173**，各 0 failures）；
前端 `game112` **404/404**、`nav112` **20/20**、`pack125` **15/15**。

⚠ **版本号说明**：这批改动按时间本来该走 `.174`，但 `.174` 这个序号上一轮已经占用并作废，
所以实际出的是 **`.175`**（内容与文中所有 `.174` 字样指的不是同一批产物）。核对脚本已按
`.175` 重新命名（`git mv`）。

### 未做（需用户明确同意）

- `.173` / `.174` / `.175` 三个包都**还没推真机**（`192.168.1.214`）。
- 本批提交只推到了本地，未 push 到 `origin/main`。

---

## v0.5.2.173 (CN-FORK) — 2026-10-13

> 用户口径（原文）：「存档转换的标签和功能删除。这个调整布局，单独放一行。导入的时候如果是
> 其他用户的存档，直接改签成当前选定用户的。saves: PPSA21567 已退出 · 本地快照：存档刚被写过，
> 等下一轮（0）/ …本地快照已排队重试（最多 40s）/ …本地快照 20261013-001258（3 文件 / 20971887
> 字节）/ …本地快照重试成功：已生成 1 份快照 —— 这里为什么这么缩链路」

### ① 删掉「存档转换」（标签 + 功能，全链路）

`.163` 加的「每行右侧一栏：存档转换」整条删除。用户口径是「标签和功能删除」，所以**连后端
一起删**，不留死路由：

| 层 | 删掉的东西 |
|---|---|
| 前端组件 | `SaveManagerModal.jsx` 的第二列、`convertFor` 状态、来源选择浮层、`doConvert`、`ArrowLeftRight` / `X` 两个图标 import |
| 前端 API | `gameApi.js` 的 `convertSave()` |
| i18n | 两份 locale 各 9 个 `game.convert_*` 键 |
| 关于页 | `AboutView.jsx` 的两处文案（能力清单 + 更新日志） |
| 后端 | `game_saves_convert()` + `gs_merge_tree()`（`src/game_saves.c`）、头文件声明、`ROUTE_GAME_SAVE_CONVERT`、`http_server.c` 的分发分支、`gr_post_limit()` 的登记 |
| mock | `mock-server.js` 的 `/game_save_convert` |

**为什么它本来就该由「兼容导入」承担**：它想干的事是「把别人的存档弄到自己名下」，而
兼容导入是**逐文件合并**（保留主机自己的 `_bu_` 记账与 `sdimg_sce_sdmemory`），比它更干净；
两条并排只会让用户以为要选一个。

### ② 兼容导入搬到**独立满宽一行**

原来它挂在「存档列表」那张卡里，而那张卡只占屏幕的 **1/3** —— 目标用户分段按钮 + 导入按钮
+ 三行长提示挤在 1/3 宽里，提示被折成六到七行（用户截的就是那一团）。

现在它是网格**之外**的独立 `<Block>`，排在「本地快照」前面：宽屏下目标用户选择器与导入按钮
并排一行，两行提示满宽铺开。回归用**几何**断言钉住（宽 1054px vs 存档列表 343px，且不在同一
`top`）—— 只断言「文案还在」的话，「搬没搬」根本测不出来。

### ③ 导入 = 「改签」（用户口径：其他用户的存档直接改签成当前选定用户的）

存档镜像是**加密 PFS 镜像**，里面一个字节都读不出原主人（也改不动 —— 那要 `/dev/pfsmgr`
ioctl + 提权，`.54` 有死机前科）。所以「改签」落地成三件事，都在
`game_saves_import_garlic()` 这条唯一入口上：

1. **落点只由 `save`（选定的目标 uid）决定**，与文件从哪来无关 —— `.165` 起 `save` 为空时
   落到**当前前台用户**，前端选择器默认也是它。
2. **文件名里别人留下的 `<uid>_` 前缀剥掉**（`1a1d3627_sdimg_X` → `sdimg_X`）。
   ⚠ 只在那个前缀**确实是本机 `/user/home` 下的一个 uid** 时才剥 —— 与 `.165`
   「认不出来一律原样返回」同一条原则：猜错一个前缀的代价是把合法镜像改成主机找不到的名字。
3. **garlic 自己的记账 `manifest.json` 不再写进主机的存档目录**。真机取证：
   `/user/home/1a1d3627/savedata_prospera/PPSA21567/manifest.json`（367 B，
   `{version,title_id,title_name,export_time,saves[{name,size,type,is_backup}]}`）
   就是 `.172` 之前被原样写进去的 —— 那是**主机的**系统目录。
   只精确跳这一个名字，**不搞「只收 sdimg_* / *.bin」的白名单**：白名单碰到没见过的合法
   条目会把用户的存档悄悄丢掉（回归里 `weird_thing.dat` 那条就是给这个反向对照立的桩）。

日志与提示都写明了落点：`garlic 导入 ok -> <path> (改签 uid=<uid>, N files, M bytes, 剥uid前缀 N, 丢记账 N)`，
前端 toast 从「已导入到…」改成「**已改签并导入到**…」，确认框也从「导入到」改成「改签到」。

### ④ 快照日志链路「为什么这么缩」

用户问的是这四行：

```
saves: PPSA21567 已退出 · 本地快照：存档刚被写过，等下一轮（0）
saves: PPSA21567 本地快照已排队重试（最多 40s）
saves: PPSA21567/1a1d3627 本地快照 20261013-001258（3 文件 / 20971887 字节）
saves: PPSA21567 本地快照重试成功：已生成 1 份快照
```

**答案**：它们是**三个不同日志点、两个文件**拼出来的，每一行各说各的，没有统一格式：

| 行 | 出处 | 那串「缩」的是什么 |
|---|---|---|
| ① | `game_auto.c` 的 `GA_ACT_SNAP` 分支 | `（0）` 是 `game_saves_snap_run_now_ex()` 的**返回值**（>0 生成份数 / 0 跳过 / -1 失败）。三义合一，光看日志分不清 |
| ② | `game_auto.c` 的 `ga_pend_snap_arm()` 之后 | 「要不要重试」是**引擎**的决定 —— 快照层不该知道引擎有待办队列，所以只能另起一行 |
| ③ | `game_saves.c` 的 `gs_snap_take()` | 真的拍成那一刻才印。所以重试成功时它落在 ② 与 ④ **之间** —— 顺序看着乱，其实是对的 |
| ④ | `game_auto.c` 的 `ga_pend_snap_tick()` | ①+② 与 ③+④ 是同一件事的两头 |

措辞短还有第二个原因：`<note>` 是 `gs_snap_take` 的 out 参数，**同一句话也走
`/game_save_snap` 的 `message` 字段回给前端**，所以它必须是「用户能读的一行」，塞不进调试信息；
日志只是复用它。

本轮把能改的都改清楚了：`（0）` → `（本轮没拍）` / `（本轮生成 N 份）` / `（本轮失败）`；
「等下一轮」→「存档刚被写过（20 秒静默窗口还没过，稍后由重试补上）」（定时守护已删，
「下一轮」是谁原来没人知道）；排队日志补上节奏（「每 1 秒再试一次，最多 40 次」）。
⚠ `test/game_auto_test.c` ⑩ 靠 `已排队重试` / `本地快照重试成功` 两个**子串**认这条链，
这两个词刻意保留。

### 测试与验证

- 宿主：`game_saves` **239 checks / 0 failures**（+13）、`game_auto` **172 / 0**、`overall: PASS`；
  `check_post_routes` / `check_stack_frames` 全绿（删路由后两张表仍一致）。
- 新增用例 `test/game_saves_test.c` ⑭b（13 条）：改签落点 / 剥 uid 前缀 / **不是本机 uid 的前缀
  原样保留** / manifest.json 不落地 / 未知合法条目仍照收。
- 新增变异对照 `test/mutation_check_resign.sh`（两个变异，各恰好 2 条红）：
  ① 关掉 uid 前缀剥离 → 「该落到选定用户、名字剥成 sdimg_RESIGN」+「前缀没被剥掉」变红；
  ② 把 manifest.json 写回去 → 文件数变 3 + 那个文件真躺在存档目录里。
- ⚠ `test/mutation_check_freeze.sh` 的 retry 变异锚点原来是**抄日志原文**的，本轮改措辞后
  当场失配（脚本只打一句 traceback 然后判 BAD，看起来像「守卫坏了」）→ 已改成**结构式**锚点
  （按 `if` 行 + 下一个 `}` 定位）。重跑 `MUTATION OK`（4 红 / 4 命中）。
- 前端：`game112` **401/401**（+8：布局几何 2 条、删干净 2 条、改签说明 1 条、改签三段式 3 条）、
  `nav112` **20/20**。

### 产物指纹

出包 `NayePlmTools_0.5.2.173-{me,ga}.elf`，**各 11,294,104 B**：

| 变体 | md5 |
|---|---|
| `-me` | `177d7895e587ffd249e40a9b3a77f08d` |
| `-ga` | `c4adc1469399cfe70ffa8b300a7bae62` |

内嵌 title `NayePlmTools v0.5.2.173 by naye (99079ad, …)`（非 DEV）；内嵌网页 blob 与
`frontend/dist/index.html` **逐字节相等**（`len=1185780, start=+2232496`）。
`bash pkg_173_verify.sh` → `ALL CHECKS PASSED`。

⚠ 核对脚本本轮踩了两个**假红**（都不是产物缺陷，是断言写错），已写进脚本文件头 ⚠(a)/⚠(b)：

1. **`"manifest.json"` 带引号 grep 不到**：产物里 C 字面量在 `.rodata` 是**不带引号**的
   `manifest.json`（4 处）。带引号的写法 0 处。
2. **`本轮没拍` / `本轮失败` grep 不到**：clang 对共享前缀的字面量做**后缀合并**
   （`SHF_MERGE` / `SHF_STRINGS`）—— 两个串共用的「本轮」在 `.rodata` 里**只存一份**，
   所以整串匹配不到（实测 0 处），但独有词 `没拍` 在（1 处）。改成认**完整格式串**
   `已退出 · 本地快照：%s（%s）` + 独有词 `没拍`。
3. **`存档转换` 在 dist / ELF 各剩 1 处**：来源是本版**关于页更新日志里那句**
   `删掉「存档转换」：它想干的事…`（故意写的，不写用户看不出删了）。所以不能拿它当
   「没删干净」的判据 —— 判据换成**功能残留**（`convert_title` / `game_save_convert` /
   `从其它用户转入` / `档案转换`），并**正向**断言那句「删掉「存档转换」」在（且只此一处）。

### 未做 / 已知限制

- **PFS 层账号改签做不到**：镜像里的账号绑定要 `/dev/pfsmgr` ioctl + 提权（内核级，`.54` 有死机
  前科）。所以「改签」= 换落点 + 名字归一，不是改镜像里的账号。若用户导入后游戏仍读不到，
  那是账号绑定层面的事，不是落点问题 —— 界面上已经把这句话写出来了。
- 真机上那份历史遗留的 `manifest.json` **没有清理**（只在导入时不再写入）。它 367 字节、
  主机不读、删它属于对生产环境存档目录的写操作 —— 按红线要单独征得同意。
- `acct111` / `avatar109` / `cropfix` 三套（打 `localhost:4173`）本轮**未跑**：改动落在
  存档弹窗 / `gameApi` / locale / 关于页，不在这三套的覆盖面上。

## v0.5.2.172 (CN-FORK) — 2026-10-12

> 用户口径（原文）：「重点修复bug，首先帮我核实一下 兼容存档导入，是什么逻辑，导入的是加密的
> 还是解密的，导入的时候是不是强制改签，还有游戏关闭的时候并没有自动存储本地快照」
> 「所有存档列表删除按钮前边加一个下载按钮，并实现下载」

### ① 修 bug：游戏退出时本地快照永远不落地

**根因**：`gs_snap_take()`（`src/game_saves.c`）有一条静默窗口
`GS_SNAP_QUIET_SEC`（真机 **20 秒**）：

```c
if (src.max_mtime != 0 && now - src.max_mtime < GS_SNAP_QUIET_SEC) {
    ... "存档刚被写过，等下一轮"
    return 1;
}
```

这句「等下一轮」是**定时守护**（30 分钟一轮）时代写的。`.154` 把定时守护整条删掉、只留
「游戏退出」这一个触发点之后，**「下一轮」永远不会来**；而主机恰好是在退出那一刻写存档
→ mtime 恒为「刚刚」→ **每一次退出都被挡掉，快照列表永远不增长**。

真机日志实证（用户那台）：`saves: PPSA21567 已退出 · 本地快照：存档刚被写过，等下一轮（0）` ×3。

**修法**（三层）：

- `gs_snap_take()` 扩出参 `int *skip_reason`，取值 `GS_SNAP_SKIP_{NONE,UNCHANGED,TOO_FRESH,EMPTY}`。
  调用方由此能区分「内容没变」「刚被写过」「空的」—— **只有 TOO_FRESH 值得等**。
- 新增 `game_saves_snap_run_now_ex()`（把各根目录的跳过原因聚合，TOO_FRESH 优先）；
  原 `game_saves_snap_run_now()` 变成薄包装，老调用方一行不用改。
- `src/game_auto.c` 加一条**延迟重试队列**：退出那一步若被静默窗口挡掉就
  `ga_pend_snap_arm()`（`GA_SNAP_RETRY_MAX 40` × `GA_TICK 1` = 最多 40 秒），
  之后每轮 `ga_pend_snap_tick()` 在**没有游戏运行时**补拍。
  ⚠ tick 必须挂在 `game_auto_tick()` 那个 `if (!strcmp(prev, cur)) return;` 早退**之前** ——
  退出之后 prev 与 cur 都是空串，每一轮都走早退，重试只能在那里做。
  用户又开了游戏就把待办丢掉（`else g_pend_snap[0] = 0;`）。

### ② 新增：三张存档列表的「下载」按钮（都排在「删除」前面）

| 列表 | 下的是哪一份 | 路由 |
|---|---|---|
| 存档列表 | 主机上**当前**那份 | `/game_save_download`（`.116` 起就有） |
| 远程存档 | **远端**那份备份 | `/game_save_remote_download`（新，**GET**） |
| 本地快照 | **某个时刻**的那份 | `/game_save_snap_download`（新，**GET**） |

- `src/save_remote.c`：把 `sr_pull` 里的取回逻辑抽成 `sr_download_to()`，`sr_pull` 与新的
  `sr_fetch()` 共用。⚠ **`sr_fetch` 一个字节都不碰主机存档** —— 图省事直接复用 `sr_pull`
  会变成「点下载，主机上这份被云端那份整份覆盖」，是最坏的一种 bug（`include/save_remote.h`
  的声明上方写了警告）。
- `src/game_saves.c`：新增 `game_saves_snap_pack()`（把某一份快照打包，zip 顶层目录
  `<uid>_<ts>` —— 一次可能下好几份，都用 tid 当顶层名会互相覆盖）与
  `game_saves_remote_download()`（远程配置解析与 `game_saves_remote_info` **同一口径**）。
- `src/http_server.c`：两条 GET 路由。「远端没有这份」回 **404 ENOENT**（前端当「没得下」），
  「连不上 / 没配」回 **502 EREMOTE** + 人话 —— 这两类原因用户能自己修，回 500 只会让人
  以为面板坏了。两条都是 **GET**，所以不进 `gr_post_limit()`。
- 前端 `SaveManagerModal.jsx`：三颗按钮的 `aria-label` **刻意不同**
  （`下载` / `下载快照` / `下载远程存档`），既方便读屏，也让回归能精确取用。

### ③ 核实结论：garlic 兼容导入（用户问的三件事）

- **导入的是「已经加密」的镜像，不是解密后的明文。** 兼容对象是 garlic-savemgr 在主机盘上
  原样看到的那个东西：PS5 → 裸的一个 `sdimg_<名>`；PS4 → 一个 zip，里面是 `sdimg_<名>`
  与配套的 `<名>.bin` 密封密钥。面板**既不解密也不加密** —— 后者要 `/dev/pfsmgr` 的 ioctl
  （`0xc0845302`）+ `libSceFsInternalForVsh` 的 dlsym + 进程提权，属于内核级动作
  （本项目 `.54` 有整机死机的前科）。
- **不强制改签**（没有「把存档改签到当前账户」这一步）。全仓 grep
  `pfsmgr|sceFsInternal|re-?sign|reseal|改签|签名` 只找到「为什么不这么做」的注释。
  整条链是**原样搬运**：先落暂存 `.tmp_gx_<pid>_<ts>` → **逐文件合并**
  （保留主机自己的记账 `sdimg_sce_bu_*` / `sdimg_sce_sdmemory`）→ 整包成功才 rename。
- 不传 `save` 时落到**当前前台用户**（`.165`）；garlic 导出名开头的 `<TID>_` 会被剥掉。

### 测试与守卫

- 宿主新增 `test/game_auto_test.c` ⑩：**用「把存档 mtime 拨到未来」触发 TOO_FRESH 分支**，
  刻意**不依赖编译期的 `GS_SNAP_QUIET_SEC`**（宿主编的是 0、真机是 20；靠常量的话这一节等于
  没测 —— 而「宿主全绿、真机炸」正是这个项目栽过三次的形状）。
  断言链：退出被挡 → 排队日志 ×1（带 tid）→ 此刻 0 份快照 → 静默窗口过去 → 补上 1 份 +
  「重试成功」×1 → 再 tick 不重复生成。
  ⚠ 夹具必须在**快照关着**的时候归零 prev：上一节结束时 prev 里还留着 TID，开着快照去归零
  会被当成「游戏退出」而立刻落一份（第一版就是在这里「实得 1」红掉的）。
- 宿主新增 `test/game_saves_test.c` ⑨b：快照打包（顶层目录 `<uid>_<ts>` / 含 `_bu_` 备份 /
  源快照不动 / 坏 tid·uid·ts 与不存在的快照一律拒**且不落盘**）。
- `test/stub_sr_stat.c` 补 `sr_fetch` 替身（`game_saves` 关卡是纯文件系统那一路，不带
  `save_remote.c`）。替身口径与 `sr_stat` / `sr_delete` 一致：**绝不回 0、绝不回 SR_ENOTFOUND**。
- `test/mutation_check_freeze.sh` 新增变异 ⑧「retry」：整块删掉排队重试（**含那行日志**）→ 4 条红。
  ⚠ 只删 `ga_pend_snap_arm()` 一行是**不够**的：日志照旧打印「已排队重试」，那条断言就变成空话
  （实测红数只有 2，被 `run_case` 判成 BAD）。
- 前端回归 `game112.mjs`：数存档行的判据从 `a[download]` **收窄到 `a[aria-label="下载"]`**
  （三张列表现在都有 `a[download]` 了），并新增两条 href 断言 + 一次真实
  `page.waitForEvent('download')` 验落盘名（`/^PPSA01234_1a1d362\d_\d{8}-\d{6}\.zip$/`）。
- `src/save_remote.c` 补 `#include <sys/stat.h>`（`sr_fetch` 落盘后要 stat 取字节数）——
  漏了它 `game_saves` 关卡直接 `BUILD FAILED`。

### 结果

- 宿主套件 `overall: PASS`（`game_saves` **226** / `game_auto` **172**，各 0 failures；
  变异 `mutation_check_freeze.sh retry` MUTATION OK）。
- 前端 `game112` **393/393**、`nav112` **20/20**、`pack125` **15/15**（构建产物层）。
- README 界面图换成本 fork 自己的界面截图（游戏列表，中文，`pldmgr_screenshot.png` 992×824）。

### 出包

`NayePlmTools_0.5.2.172-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,294,104 字节**，md5 `d20e83df1d680d6fcb3869b3156c8f72` /
`15d8d66b19841e409c8493f28c057c15`。标题 `NayePlmTools v0.5.2.172 by naye (89a8ccb, …)`
（真实 commit 哈希非 DEV；`[[TITLE_PLACEHOLDER]]` 已消费；`.171` 残留 0；
`frontend/dist/index.html`（1,188,901 字节）与 ELF 内嵌 blob 在偏移 2,232,896 **逐字节一致**）。

`pkg_172_verify.sh` **ALL CHECKS PASSED** —— 继承 `.170`（改名修复的 SQL 指纹仍在 ⇒ 没碰坏
改名链）与 `.171`（捐赠码内联 + 仓库地址）全部断言，**新增 `.172` 产物层指纹**：
dist 与两个 ELF 里必须有 `/game_save_snap_download` 与 `/game_save_remote_download`
（少一个那颗按钮就是 404）、dist 里必须有「下载快照」「下载远程存档」、
ELF 里必须有 `找不到这份快照`（= `game_saves_snap_pack` 真的编进去了）与
`本地快照已排队重试`（= `.172` 的退出快照重试真的在包里）。

### 未做（需用户明确同意）

真机 `192.168.1.214` 的推包与验收 —— 写操作红线，未经同意不做。验收口径：
面板开本地快照 → 开一个游戏再退出 → 日志里应出现
`已退出 · 本地快照：存档刚被写过，等下一轮（0）` 紧跟 `本地快照已排队重试（最多 40s）`，
40 秒内再出现 `本地快照重试成功`，快照列表多出 1 份。

---

## v0.5.2.171 (CN-FORK) — 2026-10-12

> 用户口径（原文）：「将这个项目推送到我自己的gitHub naye1120 … 将我的捐赠码换成这个图片，
> 另外将我的github地址也写上」「仓库名称用我软件名称」

### 「关于」页：捐赠码换成真实收款码图片 + 补上本 fork 仓库地址
- **捐赠二维码改成图片**：原先右侧那张码是用 `QRCodeSVG` 现场画的**仓库链接**二维码
  （`https://github.com/naye/ps5-payload-manager`）—— 扫出来只是打开 GitHub，**不是收款码**，
  等于「捐赠」按钮扫了个寂寞。现在改成真实收款码图片 `frontend/src/assets/donate-qr.png`
  （用户提供的 500×500 PNG，27,765 B）。
  ⚠ 产物是 `viteSingleFile` 单页 HTML 嵌进 ELF，所以图片**必须内联**：走 Vite 资源 import，
  `assetsInlineLimit=100000` 把它转成 base64 `data:` URI 烧进 HTML。已核对 dist 里
  `data:image/png;base64` 出现 1 次、**无外链 `.png` 引用**（否则嵌进 ELF 后必 404）。
- **补上本 fork 仓库地址**（用户口径「把我的 github 地址也写上」）：新增常量
  `forkRepoUrl = 'https://github.com/naye1120/NayePlmTools'`，两处展示 ——
  ① 左侧「关于本 Fork」信息表新增一行「本 Fork 地址」（在上游地址下一行）；
  ② 右侧卡片在收款码下方加一颗**与「支持 itsPLK」同档的 GitHub 按钮**（带自带 GitHub mark）。
- 新增 i18n 键（`locales-overlay/zh-CN.json` + `en-US.json`，JSX 内联兜底同口径）：
  `donate.fork_repo` / `donate.fork_repo_label` / `donate.fork_qr_alt` / `donate.fork_maintainer_qr`。
- ⚠ `QRCodeSVG` 在 `AboutView` 里已不再使用（`MediaManagerModal` / `ManageSourcesView` /
  `StorageHub` 仍在用，依赖保留）；`forkDonateUrl` 变量已删除。

### 仓库
本 fork 独立发布到 `github.com/naye1120/NayePlmTools`（public），`cn-fork` 分支作为其 `main`。

### 出包
`NayePlmTools_0.5.2.171-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,294,104 字节**（比 `.170` 的 11,261,336 大 ~32.7KB = 内联的收款码图片），
md5 `04dbd6d74eaa052c68cf0e21eaa014a5` / `a55801ed35d321a235e0749515e49074`。
标题 `NayePlmTools v0.5.2.171 by naye (8b897fd, …)`（真实 commit 哈希非 DEV；
`[TITLE_PLACEHOLDER]` 已消费；`.170` 残留 0；内嵌网页 blob 与 `frontend/dist/index.html`
逐字节一致）。`pkg_171_verify.sh` **ALL CHECKS PASSED** —— 继承 `.170` 全部断言
（后端改名修复指纹仍在 ⇒ 这轮前端改动没碰坏改名链），**新增 `.171` 产物层指纹**：
dist 与 ELF 里 `data:image/png;base64` 必须在（图片真的内联了，否则嵌进 ELF 必 404）、
无外链 `donate-qr` 引用、`github.com/naye1120/NayePlmTools` 在、
旧的坏地址 `github.com/naye/ps5-payload-manager` 已消失。

---

## v0.5.2.170 (CN-FORK) — 2026-10-11

> 用户口径（原文）：「P端改完名称，PC端变了，但 smb 没变，ps5 只有一个位置变了」
> 「smb 里面是宇宙机器人88，我这里已经改成宇宙机器人1了，机器上还是宇宙机器人88」

### 修 bug：改名在主机上只生效一个位置（appinfo 漏写语言槽）
**根因**（真机 192.168.1.214 取证，PPSA21567 宇宙机器人）：`appinfo.db` 的
`tbl_appinfo` / `tbl_conceptinfo` 里，同一个 titleId 的名字 key 是**一整套语言维度**：

```
tbl_appinfo     : TITLE, TITLE_00, TITLE_01, TITLE_02 ... TITLE_30   (+ TITLE_ID)
tbl_conceptinfo : concept_name, concept_name_00 ... concept_name_30
```

`td_update_appinfo()` 自 `.118` 起只写 `key IN ('TITLE','TITLE_01')`（照抄 `y2jb-new`
参考实现），**剩下几十个语言槽一个都没动**：
- `TITLE_11` = 简体中文、`TITLE_10` = 繁体中文、`TITLE_09` = 韩文、`TITLE_27` = 泰文 …
- 简中系统读的就是 **`TITLE_11`** → 读到的还是旧名 → **主屏 / SMP 显示老名字**。

这就是「P端改了、PC端面板变了、PS5 只有一个位置变了、SMP 没变」的确切机制：
我们只改了 `TITLE`/`TITLE_01`（+ app.db 的列），**用户所在语言的那个槽没改**。

**修法**：匹配式从 `key IN ('TITLE','TITLE_01')` 改成
`key = 'TITLE' OR key GLOB 'TITLE_[0-9]*'`（concept 侧同理）。
- 之所以**不用 `LIKE 'TITLE_%'`**：`tbl_appinfo` 里还有个 **`TITLE_ID`** key，
  值是标题号（`PPSA21567`），`TITLE_%` 会把它一起改成游戏名 —— 写坏身份字段比
  不改名严重得多。`GLOB '[0-9]*'` 正好把它排除（第五个字符是 `I` 不是数字）。
- 一个 tid 现在写 `appinfo.db` **14 行**（7 个 TITLE 槽 + 7 个 concept_name 槽），
  以前只有 4 行。

### 补宿主测试：`test/tile_db_test.c`（这个 bug 漏到今天的原因）
仓库此前对 `tile_db.c` 两条写链**一个宿主测试都没有**（见 `app_visibility_test.c`
开头那句自述），全靠真机消融 —— 而「语言槽」这种维度只有真正用过简中系统的用户才撞得到。
新增 26 条断言，用 sqlite3 现造 appinfo.db/app.db 夹具（不依赖 python3），钉死四件事：
① 全部 `TITLE_NN`/`concept_name_NN` 都写成新名（**简中槽 TITLE_11 单点一条**）；
② **`TITLE_ID` 一个字节不许动**（负向对照）；③ 幂等；④ 别人的行 / 无关 key（AGE_LEVEL）不碰。
**变异对照**：把匹配式退回旧写法 → 精确红 6 条（TITLE_00/09/10/11/27 + 简中槽那条）。

### 测试
- 新增 `test/tile_db_test.c`（26 checks）接入 `run_host_tests.sh`（复用 `/tmp/sqlite3_host.o`）。
- 宿主套件 `overall: PASS`；`prospero-clang` 交叉编译 `tile_db.o` 零警告。
- 前端未改，不需要重跑回归。

### 出包
`NayePlmTools_0.5.2.170-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,261,336 字节**，md5 `458712ba1c67bc2c89a0b8c0ea970750` /
`2f586f7be8208567f4aa290f4a699018`。标题 `NayePlmTools v0.5.2.170 by naye (bedea87, …)`
（真实 commit 哈希非 DEV；`[TITLE_PLACEHOLDER]` 已消费；`.169` 残留 0；内嵌网页 blob 与
`frontend/dist/index.html` 逐字节一致）。`pkg_170_verify.sh` **ALL CHECKS PASSED** ——
继承 `.169` 全部前端指纹（它们仍全绿 ⇒ 这轮纯后端改动没碰坏前端），**新增 `.170` ELF 层
SQL 字面量指纹**：`key GLOB 'TITLE_[0-9]*'` 在（2 处 = UPDATE + 计数 SELECT）、
`concept_name_[0-9]*` 在、旧写法 `key IN ('TITLE','TITLE_01')` 残留 **0**、
GLOB 字符类 `[0-9]` 在（`TITLE_ID` 已排除）。

### ⚠ 待真机验收
真机 `192.168.1.214` 上改名的**完整链路仍需真机复核**：在面板改一个名字后，确认
`TITLE_11`（简中）等语言槽与 `app.db` 的 `tbl_contentinfo.titleName` 都变成新名，
SMP（`:10101/api/v1/games` → `title_name`）与主屏也跟着变。

---



> 用户口径（原文）：「游戏管理页面 游戏磁贴上边的排序按钮，不要了，放到右上角蓝框位置」

### 磁贴 ⇅ 排序按钮 → 板块标题栏右上角（单颗、全局共用）
用户把「游戏列表」标题栏右上角、🏷 同步名字 / ↻ 刷新 左侧那块空位圈了蓝框，要求磁贴上的排序
入口搬过去。**从「每个磁贴一颗」变成「全板块一颗」**：
- `GameCard` 右上角只剩 ★ 收藏一颗（`⇅ 排序` 整块撤掉）；`GameCard` 的 `onEnterSort` prop 一并删掉
  —— 排序入口不再是磁贴的职责，签名里不留死参数。
- 新按钮落在 `<Section actions>` 的**最左边**（🏷 之前），`icon-btn` 与旁边两颗同一套样式，
  禁用态跟 `sorting || loading`：排序进行中 / 列表加载中都不许再点一次进去。
- 排序模式本身的行为一字未动（位置号、拖拽、↑↓、完成/取消、提交逻辑全在原位）——
  只换了入口，没换功能。
- 三处文案同步改口径（`game.tile_hint` / `game.pref_hint` 的 JSX 内联兜底 + `locales-overlay`
  中英两包）：别再写「磁贴右上角还有 ⇅ 排序」，改成「排序按钮在板块标题栏右上角」。
  另有两处 v0.5.2.164 / .167 的旧注释还指着「磁贴上的 ⇅」，一并改指新位置。

### 测试与回归
- 前端 `vite build` 通过（`dist/index.html` 1,149,484 字节 / gzip 326.54 kB）。
- 真页面 DOM 实测（Playwright @ zh-CN，1600×950）：标题栏右上角三颗按钮按 x 轴排
  `排序 x=1404 → 同步名字 x=1442 → 刷新 x=1480`（同一 y=159），**全页 `aria-label=排序` 只 1 颗**，
  8 个磁贴各只有 ★ 收藏（y=364），磁贴上已无 ⇅。
- i18n 双包 `JSON.parse` 通过、无键重名（键重名会静默失败，见 `.workbuddy-ai/memory` 约定）。

### 前端回归套件（全绿 526/526）
- `game112` **389/389**（.168 是 383；净增 6 条：`game112.mjs` 第 12.3 节把「⇅ 也在封面右上角 /
  两颗挨着排」两条**反向**为「磁贴上没有 ⇅」+「封面右上角只剩 ★ 一颗」，并新增
  「标题栏三颗顺序 = 排序 → 同步名字 → 刷新」「排序在最左」「全页只有一颗 ⇅ 排序按钮」
  「提示文案改成新口径」四条；另外五处 `tile(...).locator('button[aria-label=排序]')` 全换成
  新加的 `enterSort(page)`（点标题栏那颗，与磁贴 tid 无关了））。
- `nav112` 20/20 · `acct111` 23/23 · `avatar109` 19/19 · `cropfix` 75/75 · `pack125` 15/15。
- ⚠ 顺手修了 `game112.mjs` 里所有「进排序模式」的入口：`.168` 那版是点**磁贴**上的 ⇅，
  `.169` 起那颗按钮没了，不改的话这 5 处一跑就红。

**出包**：`NayePlmTools_0.5.2.169-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,261,336 字节**，md5 `e46efaa5107c2e65af2e85592ced0f9b` /
`ba3eeb55129f4167b8265b93a2172848`。标题 `NayePlmTools v0.5.2.169 by naye (1f41eba, …)`
（**真实 commit 哈希，非 DEV**；`[TITLE_PLACEHOLDER]` 已消费；`.168` 残留 0；
两包内嵌网页 blob 与 `frontend/dist/index.html` **逐字节一致**）。
`pkg_169_verify.sh` **ALL CHECKS PASSED**（继承 `.168` 全部断言，新增产物层文案指纹：
旧口径「右上角还有」必须消失、新口径「排序按钮在板块标题栏右上角」/「所有磁贴共用一个」必须在）。
真机 `192.168.1.214` 本轮不可达，**未推包**。

---

## v0.5.2.168 (CN-FORK) — 2026-10-11

> 用户口径（本批三条，原文）：
> 「这个游戏是实打实安装的」
> 「红框圈起来的部分缩小」
> 「@image 将排序标签放在红框」

### ① 背景音乐弹窗误报「找不到 sce_sys」 —— 「这个游戏是实打实安装的」
**根因**：`.155` 起的 `game_music_set_host()` 在找不到 sce_sys 时统一报「找不到这个游戏的 sce_sys
（没装进 /user/app，或是个没登记的转储）」。但**镜像游戏**（mount 镜像、已登记到 `/user/app/<TID>`、
sce_sys 锁在镜像里，游戏没跑时 `/user/app/<TID>/sce_sys` 不存在）`game_mgr_sce_sys_dir()` 同样返回 0，
于是把「已登记但此刻不可达」与「真·裸 dump」混为一谈，对仁王3（PPSA32736）这类镜像游戏误报。
- 现在区分两种情形：`/user/app/<TID>` 是目录（已登记）→ 提示「已登记但 sce_sys 此刻不可达
  （镜像游戏锁在镜像里 / 只读源），音乐只写面板，PS5 主屏不会播」；否则才报「没装进 /user/app 或没登记的转储」。
- 两种都不算失败，音乐照样在面板层生效（契约不变：返回 2 只表示没写进主机 sce_sys）。
- 配套：把 `GAME_MGR_APP_BASE` 从 `game_mgr.c` 的 TU-local `#define` 提升到 `include/game_mgr.h`
  带 `#ifndef` 保护，让 `game_music.c` 也能看到（之前它看不到这个宏会编译不过）。

### ② + ③ 改名弹窗「名称 / 音乐」之间塞排序标签 —— 「红框缩小」+「排序标签放红框」
用户两批需求本质同一块：改名弹窗里「游戏名称」块与「背景音乐」块之间红框圈出的大片空白。
- 布局先收紧：去掉「磁贴图标」块的 `xl:row-span-2`（不再纵向跨两行压过音乐块），左列图标块按内容高度收住；
  右列用 `flex flex-col gap-5 min-w-0` 把「名称块 + 音乐块」包起来。
- 不单纯缩小，而是往那块空白里塞一个**只读**「当前排序名次」标签（指向箭头 `ArrowUpDown` + 文案
  「当前排序：第 N 位 / 共 M 款（模式）」），既填了空白又回答「这款游戏在面板列表里排第几」。
- 数据走 `GameSection` 已算好的 `orderedGames`（显示顺序 = 收藏 → 手工顺序 → 档位 → 原下标），
  给 `RenameModal` 传 `sortPosition` / `sortTotal` / `sortLabel` 三 prop：
  `sortPosition` = `orderedGames.findIndex(tid===当前游戏)`，`-1` 时不渲染；
  `sortTotal` 用 `orderedGames.length` 而非后端 `count`（截断时 count 是被截后的总数，名次会算错）。
- i18n 双改：`locales-overlay` 中英两包新增 `game.sort_position`（同时改 JSX 内联兜底，规避
  i18next 内联兜底盖不住的坑）。

### 测试与回归
- `game112` 382 → **383**：旧几何断言「磁贴图标纵向跨两行（底边压过音乐块）」反向为
  「图标底边 ≤ 音乐底边 - 2」（新布局图标不再跨行）；新增断言「排序标签塞在名称块与背景音乐块之间
  （v0.5.2.168）」—— 弹窗里含 `.lucide-arrow-up-down` 的 `<div>` 其 `top ≥ name.bottom - 2` 且
  `bottom ≤ music.top + 2`。
- 后端 `prospero-clang -fsyntax-only` 通过（`game_music.c` 新宏可见性 + `game_mgr.h` 头文件改动）。

**出包**：`NayePlmTools_0.5.2.168-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,261,336 字节**，md5 `38ba90c08afd8689dc7b281e53b92b91` / `3c7a2d650eca63ac9e558595c136c93b`。
内嵌标题 `NayePlmTools v0.5.2.168 by naye (296d162, built at 2026-10-11 08:39:30 UTC)` ——
短哈希就是本轮提交号（**非 DEV**）；`frontend/dist/index.html` **1,149,344 字节**，与两颗 ELF
内嵌网页段（偏移 **+2232032**）**逐字节一致**（`pkg_168_verify.sh` **ALL CHECKS PASSED**）；
`0.5.2.167` 残留 **0** 处；标题哈希自检 `296d162` 与提交号一致。

## v0.5.2.167 (CN-FORK) — 2026-10-11

> 用户口径（本批三条，原文）：
> 「@image 这个排序已经失效。这三个图标，将置顶功能取消。排序和收藏图标是放在磁贴上层，
>   不要单独成一行。找一个好点的位置。」

### ① 排序档位失效 —— 「这个排序已经失效」
**根因**：`.166` 把手工顺序（拖拽的结果）**无条件**排在档位之前，而保存时是把**全部** tid 写进
`game_prefs.order` 的。于是只要拖过一次，**每一款**在 order 里都有互不相同的位次，
`oa !== ob` 恒成立，档位那一支**永远走不到** —— 点「名称」「体积」列表纹丝不动。
探针实测（拖过之后三档顺序一模一样）：

```
⑤ 手工序(默认): PPSA01500,PPSA01234,PPSA00802,...   ← 拖完保存
⑥ 名称档(拖过): PPSA01500,PPSA01234,PPSA00802,...   ← 与⑤完全相同
⑦ 体积档(拖过): PPSA01500,PPSA01234,PPSA00802,...   ← 与⑤完全相同
```

- **手工顺序只在「默认」这一档套用**：切到「名称」/「体积」就按规则整表重排，档位重新说了算。
  其余两条不动 —— 收藏仍然**恒定优先**（用户早先：「收藏的游戏自动排到列表前面」）。
- **手工顺序不销毁**：切到别的档只是**不套用**它，切回「默认」它还在（有回归断言守）。
- **保存后档位自动拨回「默认」**：不然在「体积」档里拖完保存，列表纹丝不动、比「按钮坏了」更难解释。
  拨回去不跳 —— `order === draft`，按默认档算出来的顺序正好就是草稿本身。

### ② 置顶整条下线
磁贴上的「📌 置顶」按钮撤掉，相关文案（工具条说明、排序横幅）、`game.pin*` 词条一并清掉。
**后端 `POST /tile_group_pin` 保留**，只是没人再调它 —— 它是唯一能把条目**新增**进主机 MyHome 组的入口
（`/tile_group_order` 只能重排**已经在组里**的），删了就没有回头路。
原因：「把某款排到最前」排序模式里的「移到最前 / 拖到第 1 位」已经做掉了，两颗按钮干同一件事，
只会让人不知道该点哪个。

### ③ ★ 收藏 / ⇅ 排序 挪到封面右上角
原来是卡片最底下**单独一行**三颗（★ 📌 ⇅），压在封面下排四颗动作按钮之下 —— 一张磁贴底下堆 7 颗按钮，
电视端尤其糟：遥控器得穿过一片按钮才够得到游戏名。

- 两颗挪到**封面右上角**（整块封面最不被遮挡的一角，拇指与光标最近），压在封面之上、
  详情热区之外（`z-10 + pointer-events-auto`，与下排四颗同一套做法）。
- 开窗态用**实心**琥珀而不是半透明 —— 压在任意封面上都要一眼看出「开还是关」，
  半透明那版在浅色封面上会消失。
- 右上角那块地原属「已安装 / 转储」徽标，把它**并进左上角那一列徽标**（同属这款游戏的固有属性）。
- **封面下方现在一颗按钮都没有**。

### 测试与回归
- `game112` **366 → 382**：新增 12.3（置顶下线 + 图标位置的**几何**断言：按钮框 ⊆ 封面框、
  在右上象限、封面下方按钮数 = 0、三枚徽标同一 left）+ 12.4（档位在拖过之后仍有效 / 保存后拨回默认 /
  手工顺序切回默认还在）。
- ⚠ 两条断言第一版是我自己写脆的，值得记：① 拿早先那份「体积序」当期望值 —— mock 会在后台**预热体积**
  （folder/dump 类一开始 `size=0`），序本身会随时间变，测试在预热完成前后随机红；改成**现算**。
  ② 「完成后回到普通模式」按整页文本搜「排序模式」四个字 —— 那四个字现在就在工具条说明里
  （「⇅ 进入排序模式」），永远为真；改成量**排序模式独有的东西**（位置号 / 上移下移消失、档位条回来）。

**出包**：`NayePlmTools_0.5.2.167-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,244,952 字节**，md5 `88ccfeb28b526aad6015d50b5fc6700c` / `81e42da2839c0b2a293de0bbdb112026`。
内嵌标题 `NayePlmTools v0.5.2.167 by naye (1806ba3, built at 2026-10-11 07:20:08 UTC)` ——
短哈希就是本轮提交号（**非 DEV**）；`frontend/dist/index.html` **1,148,479 字节**，与两颗 ELF
内嵌网页段（偏移 **+2231888**）**逐字节一致**；`0.5.2.166` 残留 **0** 处；标题哈希自检 `1806ba3` 与
提交号一致；核对脚本 `pkg_167_verify.sh` **ALL CHECKS PASSED**。

## v0.5.2.166 (CN-FORK) — 2026-10-11

> 用户口径（本批七条，原文）：
> 「背景音乐改动没有生效，应该转换成 ac9 格式，置顶按钮在 ps5 里面没有生效，排序按钮没有实现我想让排哪个
>   位置就排哪个位置，而且啊您的位置也不好，最好跟磁贴结合起来，分享的二维码内容不对，我需要扫描后直接
>   下载分享的内容。日志同一个日期，只写一个就行，内容再下边排版。修改名称的布局用这样的。」
> 两处方案由用户当场选定：背景音乐走**「浏览器里转」**；排序走**「拖拽排序模式」**。

**这一批五条里，前三条是同一个病**：`.163` / `.164` 的实现**都做了、都没做在主机认的那个地方**。
面板上的开关会亮、会提示「已应用」，主机侧一个字节都没变 —— 而 PS5 主屏只读主机那份。所以用户看到的是
「没生效」，实际是「改的是另一个文件」。第四条（分享二维码）是链接拼错了对象；第五条是排版。

### ① 背景音乐 → ATRAC9，写进游戏包的 `sce_sys/snd0.at9`
**根因**：PS5 主屏磁贴音乐**只认 ATRAC9**。`.163` 把 mp3 / ogg / wav 原文件落到面板自己的
`/data/nayeplmtools/music/<TID>` 就报「已应用」—— 那个目录主机从来没读过。
真载体是游戏包里的 `sce_sys/snd0.at9`：RIFF/WAVE 容器里的 ATRAC9 流，2ch / 48kHz / 192 kb/s，
**上限 2,097,152 字节**；响度写在 `sce_sys/param.json` 的 `pubtools.loudnessSnd0`。

- **两份落点，语义分开**：面板那份（预览 / 回放用，仍落 `/data/nayeplmtools/music/<TID>`）+
  主机那份（真载体，`<game>/sce_sys/snd0.at9` + `param.json` 的 `loudnessSnd0`）。`POST /game_music`
  与新增的 `POST /game_music_at9` 两条路都写主机；`POST /game_music_reset` 把主机那份一起还原
  （原 `snd0.at9` 先备份成 `snd0.at9.orig` 再动，失败可回退）。
- **转码器**：ATRAC9 **没有开源 C 编码器**，唯一可用实现是 `blackbearreloaded/ps5-at9-converter`
  （纯 Python + numpy，GPL-3.0-or-later）。移植成**零依赖 ESM**（`frontend/src/utils/atrac9/`：
  `tables.js` / `dsp.js` / `encoder.js` / `container.js` / `index.js` / `worker.js`），
  由 `tools/atrac9_js_check.mjs` 与 Python 参考**逐字节比对**通过（`RESULT: PASS snr=50.79 dB`）。
- **在浏览器里转**（用户选的方案）：`frontend/src/utils/at9Encode.js` 包一层 Web Worker。
  ⚠ Worker **必须 `?worker&inline`** —— 产物是**单文件 HTML**，普通 Worker 会被打成独立 chunk，
  真机上必然 404。实测 0.25 秒音频约 113ms，界面上给「正在编码 ATRAC9」的进度提示。
- **`.at9` 快路径**：上传的本来就是 `.at9` 时不重编码，只写主机那份（提示「已读取 .at9」）。
- **失败必须出声**：主机写失败时**不许**报成功 toast，弹窗里出红字并给原因；响度实测值一路显示到提交处。

### ② 置顶 → 写真载体 `app.db` 的 `tbl_group_<uid>`
**根因**：PS5 主屏「固定列表」的真身是系统 `app.db` 里的 `tbl_group_<uid>`（`groupName='MyHome'`，
`itemId` = tid，**`idx` 就是主屏位置**；真机取证见 `include/tile_group.h` 文件头）。
`.164` 的置顶只改了面板自己的 `game_prefs.json`，主机那份纹丝没动 —— PS5 里当然「没生效」。

- 新增 `include/tile_group.h` / `src/tile_group.c`，三条路由：`GET /tile_group`（读主屏固定列表）、
  `POST /tile_group_pin`（置顶 / 取消）、`POST /tile_group_order`（按给定顺序重排已固定项）。
- 置顶 = 把该 tid 插到 MyHome 组第 1 位、**其余重新编号 1..N**（不留空洞、不撞号）；取消置顶 = 摘掉
  它再把后面的往前收。**游戏也能置顶**（原 `.164` 只允许内置软件）。
- 前端 `loadHostGroup()` 把主机那份读回来做显示依据 —— 面板那份（`game_prefs.json`）只当**本地列表
  排序**用，两者不再互相冒充。写主机失败时前端出红字、并把面板那份退回原状（不许「面板说改了、
  主机没改」）。

### ③ 排序 → 拖拽，与磁贴结合
**根因**：`.164` 的「排序」只是一个**循环档位**（点一下换个名次），用户要的是「排哪个位置就排哪个位置」。

- 点「排序」**进入排序模式**：磁贴左上角显示**位置号 1..N**，直接**拖拽互换**，松手即写主机；
  电视端遥控器用**上移 / 下移 / 移到最前**兜底（下排三颗）。
- 排序模式里**详情热区与四颗动作按钮整组不渲染** —— 否则拖磁贴必然误触进详情。
- 「取消」整份丢掉（一个字节不写）；「完成」才落地：面板 `game_prefs.order` + 主机 `tbl_group` 顺序。
- **`tile_group_reorder` 的语义刻意保守**（逐行对齐 `tg_order_cb`）：只有「出现在 order 里**且**当前就在
  MyHome 组里」的条目会动；**槽位池 = 这些条目自己占的 idx**（不是整组的 idx）；少于 2 条命中直接早退
  （`if (ns < 2) return 0;`）；没被提到的条目一个字节都不动。**抄错这一步的后果不是「结果差一点」，
  而是 `idx` 撞号** —— 真机上两条磁贴同时拿 `idx=1`，主屏顺序随机。

### ④ 分享二维码 → 扫码**直接下载**
**根因**：`.165` 的二维码内容是**面板里那个资源页的地址**，扫完是打开页面、还得再点一次下载。
用户要的是扫完就下。
- 单份 → `/game_media_file?id=&dl=1&tid=`（响应带 `Content-Disposition: attachment`）。
- 多份 / 全量 → 先出「正在打包，请稍候」，打包完给 `/game_media_pack_get?job=N`
  （`content-type: application/zip`）。
- 链接一律按**当前地址栏**拼（`absUrl`），主机上 `127.0.0.1`、PC 上 `192.168.x.x` 零配置都对。

### ⑤ 更新日志：同一天只写一个日期
「关于」页的更新日志原是一个扁平数组、每条自己带 `2026-10-11 · xxx` 前缀，**同一天被印了 8 遍**，
看着像 8 个版本。改成**按日期分组**（`{ date, items[] }`）：日期只出现一次做标题（琥珀色），
条目排在它下面；没有 `date` 的能力清单组用 `title` 当标题（蓝色）区分。同一天的新内容往同一个
`items` 里加。

### ⑥ 「修改名称」弹窗改成截图版式
磁贴图标 = **左侧小方图预览 + 右侧说明与按钮**（原来预览占满整列）；游戏名称与「保存名称」**同一行**；
背景音乐带**内嵌播放器**（播放 / 时长 / 进度 / 音量）；三层的「应用 / 取消」按钮**竖排全宽**。

### 顺手修掉的四个真 bug（都是「看起来做了、其实没落地」这一类）
1. **`q()` 无条件追加 `_=Date.now()`，让资源 URL 每次渲染都是新字符串。**
   `gameApi.js` 的 `q()` 给**所有** query 追加时间戳 —— 对 JSON 接口是对的（绕缓存），但
   `<img src>` / `<audio src>` 这类**资源** URL 每次渲染都变成新串，后果有两层：① 缓存彻底失效，
   每次都重新下载；② 更要命的是拿它当「值变了没有」的判据时**永远不相等** ——
   `RenameModal` 正是拿 `musicSrc !== loadedMusicSrc` 做渲染期比较来重置播放器的，于是**每渲染一次
   就 setState 一次**，直接撞上 React 的 `Too many re-renders`（同一个 key 还挂在 `<audio key={musicSrc}>`
   上，在名称框里打字都会重置播放器）。新增 **`qa()`**（同 `q()` 但不加时间戳），把 7 个**资源** URL
   构造器切过去（`mediaThumbUrl` / `mediaFileUrl` / `mediaPackUrl` / `tileIconUrl` / `tileBackgroundUrl` /
   `tileBackgroundDdsUrl` / `gameMusicUrl`）。资源破缓存**本来就有**显式 `&v=${ver}`。
   判据：**内容随 id 定死、靠 `v=` 手动破缓存**的走 `qa()`；内容会随时间变的下载端点（存档下载）继续走 `q()`。
2. **`musicInfo` 单独存 state，而提交处读的是暂存那份的 `info`。**
   响度实测值只喂给界面那行「已编码：… 响度 … LUFS」，`applyStaged` 里的 `cur.info` 永远是 `undefined`
   —— `lufs` 一路是**空串**，主机那份 `param.json` 的 `loudnessSnd0` **一次都没写过**，
   而界面上响度那行显示得好好的。改成跟着 `staged.music.info` 走（显示与提交**同源**）。
3. **`togglePref` 的置顶分支只改内存、不落盘。** 点完列表排到最前了，一刷新退回去，而主机那份还钉着。
   补上 `setGamePrefs`（面板那份必须真的落盘）。写失败**不**影响主机那份的结果（主机才是「PS5 里生效」
   的那一份），所以单独 try。
4. **`layerName` 只有两档，音乐层被 `else` 吞成「磁贴背景」。**
   `.163` 只有 图标 / 背景 两层，写成「是 icon 就图标、否则就是背景」；`.166` 加了音乐层之后，
   音乐块的「应用 / 取消」与背景块**可访问名一模一样** —— 纯静默 bug，界面上看不出任何异样，
   读屏与自动化分不出是哪一层。改成三档。

### 测试与回归
- **宿主测试 `overall: PASS`**。`test/game_prefs_test.c` 有两处**按整串比** `{"prefs":{}}` 的断言 ——
  `.166` 给 `game_prefs_list_json` 加了 `order` 字段后必红。改成 `is_empty_list()`（`prefs` 与 `order`
  都空才算空表），并写上「**每加一个字段这里都要跟着改一次**」的理由，免得下次又按整串比。
- **前端 `game112` 305 → 366 / 366**（新增第 12 节约 40 条 + 第 7 节 3 条版式），
  `nav112` 20、`acct111` 23、`avatar109` 19、`cropfix` 75 同绿；eslint 零 error 零 warning。
  新增的四节：**12.1** 分享二维码 = 下载直链（**真发一次请求**验 `Content-Disposition: attachment`；
  打包那条验 `content-type: application/zip`）；**12.2** 背景音乐两份落点（主机 `lufs` 是负数、
  主机写失败必出红字、`.at9` 快路径、`按钮[aria-label="应用（背景音乐）"]` 存在且唯一）；
  **12.3** 置顶写主机（该 tid `idx===1`、其余重编号 1..N **无撞号**、面板那份也落盘、主机坏时报红并退回）；
  **12.4** 拖拽排序（位置号 `1..N`、排序模式里动作按钮整组不渲染、上移/下移、拖到第 1 位、
  **拖动期间不写主机**、「取消」整份丢掉、「完成」后主机**槽位池不变**但按新名次重分配、
  系统条目一条未动、坏时红字）。
- **mock 夹具原来不像真机，导致一条断言长期空转。** `tileGroup` 里是 5 条**纯系统条目**，
  而 `/tile_group_order` 的语义是「只重排**出现在 order 里且当前就在 MyHome 组里**的条目」——
  系统条目压根不在游戏列表里，前端永远传不出它们的 tid，于是主机侧 `moved` **恒为 0**，
  那条「主机侧真的动了几条」的断言**从来没真正跑过**。夹具里补上两个游戏列表里也有的 tid
  （`PPSA01234` / `CUSA01111`），并抽成 `tileGroupFixture()`（`/reset` 里原先另抄了一份旧的 5 条，
  与新夹具不一致 —— 经典「两份夹具各自漂移」）。同时 mock 的 `tile_group_order` 原本把**整组** idx
  当槽位池，实测让 `CUSA01111` 与 `NPXS40037` 同时拿到 `idx=1`（撞号）；逐行对齐 `tg_order_cb` 后
  实测 `;order:[CUSA01111,PPSA01234]` → `{moved:2}` 且 `CUSA01111:2 / PPSA01234:5`、系统条目未动。
- **两条「游戏112 红了」是测试自己的问题**：「拖动真的把 B 挪到了第 1 位」与「完成时报告了主机侧真的动了几条」
  —— 同一个 `page.evaluate` 里一次派完 `dragstart` / `dragover` / `drop`，`onCardDrop` 闭包读到的
  `dragTid` 还是空串（`onDragStart` 只是 `setDragTid`，React 还没提交）。改成**两个 tick**
  （`dragstart` → 等 150ms → `dragover` / `drop`，`DataTransfer` 挂在 `window.__pldDragDt`）。
  另注意 **HTML5 DnD 的 `dragover` 必须 `preventDefault`**，否则不触发 `drop`。
- 核对脚本 `pkg_166_verify.sh`（基于 `pkg_165_verify.sh`，**每轮在上一版基础上加断言**）新增
  **6c**（产物里必须含本轮中文字面量：排序模式 / 拖动磁贴到想要的位置 / 位置号 / 移到最前 / 已置顶 /
  主机固定列表更新 / 面板顺序已保存 / 主机固定列表读不到 / 已写入主机 sce_sys/snd0.at9 / 正在编码 ATRAC9 /
  已读取 .at9 / 已编码： / 下载链接 / 手机扫码即可直接下载 / 正在打包，请稍候 + `atrac9` /
  `AT9_BITRATE_KBPS` / `snd0.at9`）与 **7c**（源码级锚点：`absUrl`、两条分享路径、`autoDownload = false`、
  `draggable={!!sorting}`、`ev.preventDefault`、`if (ns < 2)`、`slot[i] = sel[i].orig`、
  `game_music_set_host` / `game_music_reset_host`、`at9Encode.js`、`atrac9/` 文件数、`worker&inline`、
  `lufs` 链路、`staged.music.info`、`layerName` 三档、`function qa(params)` + 7 个函数用 `qa(`、
  `is_empty_list`、`game112` 第 12 节的 5 个标记）。

**出包**：`NayePlmTools_0.5.2.166-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,261,336 字节**，md5 `e3ee47c47dccdf3969b7ab3299d64f51` / `1ed33e60555b82b08d3223f69855d4d0`。
内嵌标题 `NayePlmTools v0.5.2.166 by naye (c676f86, built at 2026-10-11 05:16:51 UTC)` ——
短哈希就是本轮提交号（**非 DEV**）；`frontend/dist/index.html` **1,150,249 字节**，与两颗 ELF
内嵌网页段（偏移 **+2231888**）**逐字节一致**；`0.5.2.165` 残留 **0** 处；titleId 自检
NAYE00001 / NAYE00004 通过；`-Wall` 警告 0 条。核对脚本 `pkg_166_verify.sh` **387 项全过**
（第 6c / 7c 是本轮新加的）。

⚠ **核对脚本里那两条「编码器进包了没有」的断言，第一版是自欺的。** 原本写的是
`grep -ao "atrac9"` 与 `grep -ao "AT9_BITRATE_KBPS"` —— 前者是**模块路径**（打包后就没了）、
后者是**导出常量名**（terser 默认开 mangle 会改名），实测这两个在产物里都是 **0 次**。
这种断言永远红，然后就会被人顺手注释掉，等于没写。**能穿透打包的只有字符串字面量**：
现在改用 `not a RIFF/WAVE file`（编码器容器校验）+ `createObjectURL`（内联 Worker 外壳），
再量一次 `?worker&inline` 生成的**单引号源码串长度**（实测 **86,514 字符**，且内部含
`superframe`）—— 关键字 + 长度一起看，才防得住「打了个壳」。

⚠ **`build_tiles.sh` 的 `[2/3]` 又会打印一次「Title set to: …」（时间戳与 `frontend-build`
那次不同），但一个字节都没落地** —— 占位符已被消费掉，正是 `.162` 那个坑。本轮**无害**
（因为 `frontend-build` 已经烧了正确的版本与哈希），但核对脚本第 5 节的逐字节比对才是真凭据：
三处（dist / me / ga）标题实测都是 `c676f86, built at 05:16:51 UTC`，**别信那句打印**。

⚠ 尚未真机验收：① 背景音乐上机后 PS5 主屏聚焦磁贴**真的出声**；② 置顶 / 拖拽排序后主屏
**重启或待机唤醒**，磁贴位置真的按新顺序；③ 分享二维码用手机扫，**真的直接开始下载**
（单文件与打包两条路）。真机 `192.168.1.214` 的 8911 / 10101 目前仍拒连。

## v0.5.2.165 (CN-FORK) — 2026-10-11

> 用户口径（本批四条，原文）：
> ①「手机端界面 pkg 安装、常用配置、游戏管理，三板块做成二级菜单；活动进程、设置、关于同样做成二级菜单。
>   游戏存档自动导入导出屏蔽所有自制应用。游戏存档本地这行分成两列，第二列放存档转换 —— 可以将其他用户的
>   存档转换成当前用户可以使用的。存档管理的游戏名称后边加上游戏编号。名称与背景板块将磁贴图标与游戏名称
>   板块位置对调，同时将磁贴图标修改中的图标进行放大。让磁贴图标板块占两行，第二列游戏名称下边增加一个
>   背景音乐修改，背景音乐与游戏名称各占一行。多媒体管理中所有下载按钮旁边加上分享按钮，点击弹出二维码，
>   二维码内容是所分享内容的链接，这个链接要根据地址栏的访问内容而确定。取消支持作者板块，在关于本 fork 的
>   右边加上原作者捐赠链接和本作者捐赠二维码。补全参考与致谢中的参考项目，只表明作者和项目链接。
>   将今天所有的更新内容加入更新日志，标注日期。完成后构建 elf。」
> ②「个性化软件中，每个软件设置置顶开关，如果关闭则不指定，如果打开则置顶。游戏列表，每个磁贴增加收藏和
>   排序和置顶按钮，收藏的游戏自动排到列表前面，排序按钮可以看你怎么做都行，实行这个磁贴列表显示排序，
>   置顶按钮是指 ps5 中固定在前面。」
> ③「payload 管理页面，将上传按钮放在 ip 和端口上边，ip 和端口显示再突出一些，不限于改变颜色，改变样式，
>   增加背景等等。他们两个的右边放二维码，用来手机扫码访问。跟之前的任务一起加入更新日志。」
> ④「修复两个 bug：游戏名称修改完成后 ps5 当中没有生效；兼容导入后的存档，没有对当前用户生效。」

### ④ 两个 bug（本轮的核心）

**Bug ① 改名在 PS5 上「没生效」—— 其实是一个假成功把两种完全不同的结局盖成了同一种。**
`tile_db_set_name()`（写系统 `app.db` 平铺名字列 + `appinfo.db` 的 `TITLE`，也就是 **SMP 与电视主屏
真正读的那一层**）的成败以前**只进主机日志**，前端一律报「名称已改为…」。于是
「库里压根没写进去」与「写进去了、Shell 还没重绘」在界面上长得一模一样 —— 后者是**正常现象**：
磁贴名字与自定义图标同源，Shell 只在**重启主机或待机唤醒**后才重绘。

- `game_title_set_name()` 扩两个出参 `host_ok` / `hnote`（`include/game_title.h` 写明了语义），
  `POST /game_rename` 的响应跟着回 `host_ok` / `host_note`。
- `RenameModal` 的提示从「一条」改成「一列」（`notes` 数组，按因果由下到上：先源、后主机库）：
  源写不动是 warn，主机库写失败是 error 红字，主机库写成功则**明确说明要去重启 / 待机唤醒**才重绘。

**Bug ② 兼容导入后没对当前用户生效 —— 两个独立根因，各修一处。**

1. **落错用户**：前端目标用户取的是 `gxUid || (saves[0] && saves[0].id) || ''`，而 `saves` 按 uid
   **升序**排 —— 当前用户没在这款游戏上存过档时 `saves[0]` 是**别人**；选择器又只在
   `saves.length > 1` 时渲染，单人主机上连选都选不了，静默落错人。
   - 后端新增 `game_saves_current_uid()`：走既有的 `sceUserServiceGetForegroundUser` 封装
     （`sys_get_foreground_user`），把 uid 试成 `%x` / `%08x` 两种写法后**以主机上真存在哪个目录为准**。
     宿主测试用 `__attribute__((weak))` 兜底（返回 0 = 拿不到）。
   - `/game_saves` 回 `current_uid` 与 `users`（= 主机上**全部**用户目录，不只是存过档的）。
   - `gs_root_or_create()` 把「按 uid 找根」的判据提到**函数第一行** —— 否则「当前用户已有存档但
     不是 uid 最小」时照样取错人。
   - 前端默认目标 = 当前用户；目标用户选择器**总是显示**（只有一个候选时也写出来）。
2. **落盘名字错**：garlic 导出的 PS5 裸镜像是 `<TID>_sdimg_<名>`（真机取证：`PPSA27836_sdimg_STARBOARD`），
   老代码原样写进存档目录，主机要找的 `sdimg_<名>` 就不存在。
   - 新增 `gs_gx_name_for()`：剥掉开头那段 `<TID>_`。
   - `.156` 那条「导入的是不是这款游戏」的判定**必须跟着搬家**：判定只能看**原始名字**
     （剥完前缀就认不出了），所以从「扫暂存目录」改成在 `gs_gx_cb` 里记 `bad_tid`，
     `game_saves_import_garlic()` 在合并前用它拒收。

**顺手清掉两处挡路的既有 bug**：`MediaManagerModal` 的分享按钮写进了 `Lightbox` 组件里、引用了
作用域外的 `setSharing`（eslint `no-undef`）；`App.jsx` 9 条既有 eslint 错误（7 条
`no-unused-vars` + 1 条 `no-empty` + 1 条 `react-hooks/set-state-in-effect`）——它们会让
`npm run build` 直接失败。

### ① 布局与内容
- **手机端二级菜单**：原 7 项里 6 项并成「工具 / 系统」两组（`App.jsx` 的 `type='group'` +
  底部 nav 上方的浮层），桌面与电视端仍平铺。
- **自动存档跳过自制应用**：`ga_tid_is_homebrew()` 按物理源判（`/data/homebrew/<TID>`、
  `/data/etaHEN/games/<TID>`，只 `stat` 不开 fd），命中则**整条动作链都不跑**（推 / 拉 / 快照），
  并记一条日志 —— 伪造 TID 的 homebrew 也拦得住。
- **存档本地那行拆两列**，第二列放「存档转换」（`POST /game_save_convert {tid, source, target}`）：
  把另一个用户的存档镜像**逐文件合并**成当前用户可用的那一份。
- **存档管理里游戏名后加编号**（TID）。
- **「名称与背景」重排**：左 = 磁贴图标（占**两行**、预览放大到整列宽），右上 = 游戏名称，
  右下 = **背景音乐**（新增层，`POST /game_music` / `/game_music_reset`，落
  `/data/nayeplmtools/music/<TID>`，MP3/OGG/WAV，上限 30 MB），磁贴背景整幅铺在三块下方。
- **多媒体管理**：每个下载按钮旁边加分享按钮，点开二维码，内容是**按当前地址栏**拼出来的直链。
- **「关于」页重构**：删掉独立的「支持作者」板块，在「关于本 Fork」**右边**放原作者捐赠链接 +
  本作者捐赠二维码；「参考与致谢」收窄成「一个作者一行 + 项目链接」。

### ② 收藏 / 置顶 / 排序
- 每个磁贴三颗小按钮：**收藏**、**置顶**、**排序**（开/关 + 循环档位），压在详情热区之上。
  收藏与置顶**恒定优先于档位**（「收藏的游戏自动排到列表前面」），落
  `/data/nayeplmtools/game_prefs.json`（`src/game_prefs.c`）。
- 「个性化软件」每个软件一个**置顶开关**（`src/app_pin.c`，与 `visible` 分开存：一个写 `app.db`、
  一个写本面板的 JSON）。

### ③ Payload 管理页头部
- 上传按钮挪到 IP / 端口**上方**；IP 与端口做成整块高对比底纹（不只换颜色）；两者右侧放
  **访问二维码**（内容 = 当前地址栏推出来的手机可访问地址）。

### 测试与回归
- 宿主：`game_saves` **212 checks / 0 failures**（新增第 ⑭ 段 8 条：当前 uid 解析、不带 save 时
  落到前台用户、**反向**不许落到 uid 最小的那个、剥前缀、认不出的名字原样保留、
  剥前缀不许弄丢 `.156` 的游戏判定）；`game_prefs` 新增 117 行用例；`overall: PASS`。
- 变异对照（反向）两条都精确命中：`gs_gx_name_for` 不剥前缀 → 2 条红；`gs_root_or_create`
  忽略前台用户 → 3 条红；还原后全绿。
- 前端回归 `game112` **305 / 305**（`nav112` 20、`acct111` 23、`avatar109` 19、`cropfix` 75 同绿）。
  新增断言：导入落到当前用户（含 `uidQuery`）、选择器常在、当前用户被标出来、
  garlic 真实命名剥前缀、导入提示带目标用户、改名主机层两种结局、
  「名称在图标右侧 / 音乐在名称下方 / 图标跨两行 / 背景在三块下方」四象限。
  ⚠ 同步修正了两条**被 `.162` 与本次重排改旧了**的断言：`立即拉取` 的文案已按 `.162` 去重改成
  「已跳过拉取」（并补一条「本地全丢了仍能拉回来」的反向对照，防止去重把救回路径一起掐死）；
  「恢复默认」按钮从 2 颗变 3 颗（图标 / 音乐 / 背景）。
- **`lucide-react` 1.x 删掉了全部品牌图标** —— 从它 import `Github` 会在模块求值时直接抛
  `does not provide an export named 'Github'`，Vite 预打包的 dep 是静态 ESM，缺一个具名导出
  就**整包白屏**（本轮踩到，「关于」页自带一份官方 GitHub mark 的 SVG 替掉）。
  另加一条全量核查：`frontend/src` 里所有 `from 'lucide-react'` 的具名导入必须真的存在。

**出包**：`NayePlmTools_0.5.2.165-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,064,728 字节**，md5 `b275ef5435d0daebb95d085341ac6abc` / `f08c35fdfc6f2f235f0e37d121174caa`。
内嵌标题 `NayePlmTools v0.5.2.165 by naye (a1195d6, built at 2026-10-11 02:59:36 UTC)` ——
短哈希就是本轮提交号；`frontend/dist/index.html` **978,701 字节**，与两颗 ELF 内嵌网页段**逐字节一致**；
`0.5.2.162` 残留 **0** 处；`-Wall` 警告 0 条；核对脚本 `pkg_165_verify.sh` **295 项全过**
（第 6b / 7b / 11h 节是本轮新加的：产物里必须含本轮的中文字面量与源码级锚点）。
⚠ **这一轮第一次出包直接编译失败**：`include/game_music.h` **不自包含**（用了 `size_t` 却没引
`<stddef.h>`），而报错点全落在调用方 `http_server.c` 上（`call to undeclared function 'game_music_set'`），
很容易看成「函数没实现」。修完之后第二次又炸在**同一个文件没被 `#include` 进去**（`http_server.c`
里加了 `game_music.h` 才通）。**新加头文件一律引齐自己用到的类型；新加模块要同时把
「Makefile 的 SRCS」和「http_server.c 的 include」两处补上。**
⚠ 尚未真机验收：①「改名后重启主机 / 待机唤醒，主屏磁贴真的换了名」；
②「garlic 导入后进游戏能读到这份存档」；③ 收藏 / 置顶在 PS5 主屏的固定位置表现。

## v0.5.2.162 (CN-FORK)

> 用户口径：
> 「游戏存档拉取的时候，如果本地存档和远程存档一模一样，则不进行拉取，
> 也就是说，只有远程的存档比本地的新的时候 才进行拉取。」

**`.154` 那道门比的是错的那个时间。** 它已经做到了「本地更新就不拉」，但做不到「一模一样不拉」，
因为拿来比较的 `remote_mtime` 是**远端那个文件落盘的时刻**（SMB 取 zip 的文件 mtime，
FTP / HTTP 直接写 `time(NULL)`），而本地那侧取的是**存档被游戏写下的时刻**。后者永远更早，
于是「刚推上去的这份」在对面看起来永远比本地新 —— 紧接着的自动拉取就把它原样拉回来：
几百 MB 走一遍网络，落回一模一样的字节，面板上还写着「已拉取 1 份」。
把判据从 `>` 改成 `>=` 治不了这个，两个数根本不相等。

- 推送时随包写的 `<uid>.meta.json` 多一个 **`src_mtime`**：打这份包那一刻本地存档目录的
  最大 mtime（与判定用的 `game_saves_local_mtime()` 同一种量）。取数排在打包**之前**，
  宁可少报也不能多报。
- `sr_stat()` 回传它（三个协议同一口径：**缺字段回 0 = 说不清**，绝不回成某个真实时刻）。
- `ga_pull_needed()` 认得「同一份」了：有 `src_mtime` 时**严格更新才拉**（相等与更旧都不拉）；
  没有（`.162` 之前推上去的老包）就退回 `.154` 那条只防「用旧盖新」的口径。
  「本地都没有」仍然永远该拉 —— 换机 / 误删时哪怕来源时间戳更旧也要救回来。
- 拉回来的文件落盘时归档里的时间会被 `utimes()` 还原，所以「推上去 → 拉回来」之后
  本地仍是那个 `src_mtime`，下一轮自然判成一模一样、不再动。
- 提示与日志跟着改：原先那句「本地存档比远程新」在刚推完的场景里是**假话**，
  改成「远程没有比本地更新的一份，已跳过拉取（N 份未覆盖）」；日志行 `· 本地更新 N（未覆盖）`
  改成 `· 无需拉取 N（未覆盖）`。

**测试**：`ga_pull_needed` 从 8 条扩到 13 条（新增「相等不拉」「来源更旧不拉」
「老包没有 src_mtime 时相等也照拉」）；端到端补两条 ——
② 刚推上去的紧接着拉取**该跳过**，⑥ 手写成旧格式（只有 files/size/mtime）的 meta
**该照常拉**；`save_remote` 断言 `src_mtime == 本地最大 mtime`（比「落在 now 附近」严，
能证明这个字段真来自存档而不是又写了一遍 `time(NULL)`）。
变异对照 `gate` 那格的锚点换成**两条出口一起掐**（只掐一条另一条仍会跳过，红数会少），
期望从 4 条红改成判据命中 ≥6、总红 ≤7，实得 6 / 7；七条变异全被抓住。
栈帧门 `check_stack_frames.sh` 通过（meta 缓冲 160→256 仍在额度内）。

**出包**：`NayePlmTools_0.5.2.162-me.elf`（NAYE00001 媒体栏）· `-ga.elf`（NAYE00004 游戏栏），
两包各 **11,031,960 字节**，md5 `2ac754a9e76996ef5e8086d4dcc912c8` / `70706bd8fde20ac5ef6d023fe47ebe91`。
内嵌标题 `NayePlmTools v0.5.2.162 by naye (20bcbae, built at 2026-10-10 15:06:56 UTC)` ——
短哈希就是本功能的提交号；包内 `0.5.2.162` 5 处、`0.5.2.160/161` 残留 **0** 处，
`src_mtime` 2 处、`已跳过拉取` 与 `无需拉取` 各 1 处；`-Wall` 警告 0 条。
⚠ **这一轮第一次出包出的是假版**：`build_tiles.sh` 见 `frontend/dist/index.html` 存在就直接复用，
而标题注入是 sed 替换 `[[TITLE_PLACEHOLDER]]` —— 占位符上一轮已被消费掉，于是它打印
「Title set to: …162…」却一个字节都没改，包里烙的是 `.161`（提交 `3918662`）的标题，
烧进前端的 `__APP_VERSION__` 也还是 `0.5.2.161`（「关于」页那处兜底显示跟着一起旧）。
自证里 `0.5.2.161` 残留 3 处就是它露的馅。
**版本号一有 +1 就必须先 `make frontend-build` 再 `bash tools/build_tiles.sh`**（光靠
`build_tiles.sh` 那句「reusing existing frontend/dist」不会重烧版本）。

⚠ 尚未真机验收：上机要看见的是「推完再启动同一款游戏，不再出现拉取动作」，
而**换了另一台主机 / 本地存档被删时仍然照拉**。老存档（`.162` 之前推的）没有 `src_mtime`，
行为与 `.154` 一致，这是刻意的。

## v0.5.2.161 (CN-FORK)

> 用户真机口径（`.160` 上机之后）：
> 「所有应用程序 欢迎 是可以编辑显示和隐藏的。其他的不能编辑的选项直接就不用在前端显示了。」

**`.160` 那份名单划错了两条。** 错法是把「出厂值」当成了证据：`NPXS40056 所有应用程序`
在真机快照里本来就是 `visible=1`（主屏画它正是这一列在起作用，不是它抵抗），
`NPXS40063 欢迎` 出厂就是 0，可「主屏到底画不画它」我压根没肉眼核对过。
判据只有一条：**把 visible 写成 0，再看主屏**；静态快照的 0/1 分布不能反推规则。

- 名单收窄成真的改不动的那几条：`NPXS40071 游戏内容保存库`、`NPXS40139 应用程序内容保存库`
  （用户连着两轮报的就是这两条），加上 `编辑 40145/40205` 与光碟占位 `40140/40144/40148/40149/40150`
  这两组本身不是「可隐藏磁贴」的系统条目。所有应用程序 / 欢迎 回到普通开关，写得动。
- **不可控的条目前端整组不显示**（原先是列成「主屏固定条目」并写明原因）。
  后端**照旧**回 `controllable:false` + `reason`，也照旧 400 `EFIXED` 拒写 ——
  名单只能有一份，必须在后端；前端抄一份迟早和后端漂。

**测试把这次的错法钉住**：夹具里补上 所有应用程序 与 欢迎 两行，断言它们在清单里是
`controllable:true`，且写隐藏 / 写显示**真的落库**（两张账号表都变）；`NPXS40071` 仍回
`EFIXED` 且一行不动。这样下次再有人拿出厂值反推规则，这一关就先红。

## v0.5.2.160 (CN-FORK)

> 用户原话（`.158`「个性化软件」真机试用之后）：
> 「你排查一下，应用内存保存库 和游戏内容保存库 还是没隐藏」

**排查结论和猜想的方向不一样**：取证（FTP 拉回真机 `app.db` + 面板日志）显示写入确实落库了，
两张账号表的 `visible` 都成了 0；同时 PlayStation Store 与 媒体相册 按同一份库、同一次操作
**真的从主屏消失了**。所以 `visible` 这一列没写错、也不是 WAL / 缓存问题 ——
是主屏上有四类条目**根本不读这一列**：所有应用程序、两个内容保存库、欢迎，
外加「编辑」入口和光碟占位条目（后两组连开关都不该有）。
真问题不是「没隐藏成功」，是**面板对着改不动的条目回了一句「已隐藏」**：
谎报一次，用户之后每一步排查都要从「到底写进去没有」重新开始。

- 后端认一份固定条目名单（`src/app_visibility.c` 的 `AV_FIXED`）：`GET /system_apps`
  每条带 `controllable` 与一句中文 `reason`；`POST /system_app_visible` 对这些
  **回 400 `EFIXED` 且一行都不写**（在打开库之前就拒，不留备份、不进事务）。
- 前端分成两组：「可以隐藏 / 显示的内置软件」照旧给开关；「主屏固定条目」只显示名字 +
  原因 + tid，**不给灰掉的开关** —— 灰开关仍会被读成「能动，只是此刻不能」，而事实是它不归这列管。
- ⚠ 顺带修掉同一类谎的第二条：在册检查原先把「prepare/step 回 BUSY」和「真没这一行」混成
  一句「主机上没有这个内置软件（可能已被卸载）」。真机日志里同一个 tid 两分钟前被这样拒掉、
  两分钟后同样的写又通过 —— 明明只是**那两分钟正忙**。现在忙回 `-2` +「主机正忙或应用数据库读不了」，
  没装才回 `-1`。清单读到一半出错也不再悄悄回空数组冒充「主机上没有可管理的软件」。

**测试钉的是「不许谎报」而不是「能写成功」**：`test/app_visibility_test.c` 加了
`NPXS40071`（游戏内容保存库）进夹具，断言它在列表里带 `controllable:false` + 非空 `reason`、
写它回 `APP_VIS_EFIXED` 且**两张账号表那行还是 1**；再把 `tbl_contentinfo` 删掉，断言写入回 `-2`
且错误里是「读不了」而不是「没有这个内置软件」。

真机验收还没做：`.160` 的包上机后要看见那 4 条变成灰态说明、而 Store / 媒体相册 仍能正常隐藏。

## v0.5.2.159 (CN-FORK)

> 用户原话（指着存档管理 → 远程位置 → 远程存档那一行的右侧空白）：
> 「红框这里加删除按钮，删除远程对应的存档。」

**删的是远端那一份 `<base>/<TID>/<uid>.zip`（连带推送时写的 `<uid>.meta.json`）**，
主机上的本地存档与本地快照一个字节都不碰 —— 同一屏已经有两个垃圾桶
（存档列表那行的删除、快照那行的删除），三个动作的边界必须在确认框里说清，
不然按错一个的代价不对称。

- 传输层 `sr_delete()`（`src/save_remote.c`）、业务层 `game_saves_remote_delete()`
  （配置解析与 `game_saves_remote_info` **逐字同一条口径**：全局优先 → 本游戏回落。
  推/拉/查/删四条路要是各解析各的，会出现「界面上看到的远程在 A 共享，
  删的时候删的是 B 共享」—— 比删不掉危险得多）。
- 路由 `POST /game_save_remote_delete`，body `{tid, save}`。
  与旁边的 `/game_save_delete`（GET）**刻意不同**：不可撤销的删除不该走 GET。
- ⚠ **只有 SMB 会真删。** FTP 要删得靠 curl 的 `QUOTE DELE`，而那条路在 QUOTE 之后
  curl 仍会对 URL 做一次目录列举 —— 桩服务能配合，真机对面是什么服务器不知道，
  **最怕「命令发歪了却回成功」**；HTTP 更没有删除语义。两者一律回 400 + 一句
  带协议名的人话，前端据此**干脆不给按钮**（点了才知道不支持是最差的一种提示）。
- ⚠ 「远端本来就没有」（`SR_ENOTFOUND`）算成功：用户要的结果就是「远程不再有这一份」，
  已经达成却回一句失败，等于逼他再点一次去看错误。

**测试里最值钱的一条不是「删掉了」，是「不许谎称删掉了」**：
`t_delete()` 先真推一份到桩 FTP / HTTP，再点删除，断言三件事 —— 回 -1、
报错里带着协议名、**远端那份文件原样还在**。删除类操作静默「成功」一次，
这个按钮就再也信不过了。另外钉了一条：mode 没填时不许顺着 `else` 落到
「HTTP 不支持」上（写第一版时就真的这么错着，被测出来了）。
SMB 真删只能真机验（主机侧没法桩 SMB server）。

## v0.5.2.158 (CN-FORK)

> 用户原话：「在截图这个模块（设置 → 启动与界面）中加入一个设置项，叫个性化软件，
> 点开后将所有 ps5 内置软件列出来，并加上隐藏开关。」
> 那句「开关关的时候隐藏」和「关掉进行显示」是反的，澄清后按 **开=隐藏、关=显示** 做；
> 范围 **只列索尼内置应用**，不含游戏与自制软件。

### 显隐写的是 `app.db` 的 `tbl_iconinfo_<uid>.visible`

拿真机 `app.db` 逐条对照过主屏：`visible=1` 的正好是主屏看得到的（媒体库 / 商店 /
所有应用程序 / 两个内容保存库），`=0` 的正好是看不到的（Plus、探索、Music Home、
Remote Play、分享游玩、Game Streaming、光盘类、欢迎 / 编辑）。
**这根列是唯一事实来源** —— 所以没有新增配置项，弹窗每次打开都重新读库，不存在两份状态。

- 新模块 `src/app_visibility.c` + `include/app_visibility.h`；
  `GET /system_apps` 列内置应用，`POST /system_app_visible` 改单条显隐
  （**已登记进 `gr_post_limit`**，漏登记的话请求体收不到、客户端只看到连接被关）。
- 多账号：`LIKE 'tbl_iconinfo%'` 发现全部账号表（不硬编码 uid 后缀）；
  **写时拉平所有账号表**，**读时取 AND**（任一账号还显示就算显示）。
- 写前复用 `tile_db_backup_dbs()` 留一份原始备份（原 `static td_backup_dbs()` 提升为公开，
  与磁贴写入走同一条备份链）。
- ⚠ **只认 `NPXS` 前缀**（`av_tid_ok()` 硬门）。面板自己的磁贴是 `NAYE00001`，
  能藏掉它就等于删掉唯一的恢复入口 —— 白名单是护栏，不是偷懒。
  未在 `tbl_contentinfo` 注册的 tid **直接拒绝**，不允许「改了 0 行还算成功」。
- ⚠ **主屏不一定立刻重绘**：`visible` 改动要等重启 / 待机唤醒才见效，弹窗文案与 toast 都写明。

### 前端

设置 → 启动与界面 末尾加一条 `NavRow`「个性化软件」→ `SystemAppsModal.jsx`（`layout="panel"`），
每行一个 `TimingRow`（名称 + TID + 开关）；乐观更新、失败回滚、写入期间整表禁用。
文案 10 个 key 进 `locales-overlay`（`appsys.*`），未动 `locales` 子模块。
`mock-server.js` 用真机那 19 条 NPXS 做了两条端点，可在 `dev:mock` 下点着验。

### 验证

`test/app_visibility_test.c`（27 条断言，已接进 `run_host_tests.sh`）用 sqlite3 C API 造了一个
双账号 `app.db`：钉住了「只出 NPXS」「跨账号 AND」「写后全表拉平」「`tbl_concepticoninfo_*` /
`tbl_group_*` / 无 `visible` 列的表一律不碰」「游戏与自制软件被拒」「注入型 tid 被拒且库完好」
「库缺失时读为空 + 有错误信息、写失败」。PS5 工具链 `-fsyntax-only` 三份改动文件全清，
`check_stack_frames.sh` 与 `check_post_routes.sh` 均绿。

## v0.5.2.157 (CN-FORK)

> 用户真机反馈（`.156` 推上去之后看的）：
> 「游戏管理标签，PC 端，大小正好。ps5 端的太大了，最少还要减少 4 分之一。」

**电视端磁贴再缩 1/4**：`html[data-device="ps5"]` 的 `--tile-w` 由 `17.578125rem`
改成 **`13.125rem`**。

在 PS5 的 15px 根字号下，**实际生效值 = 196.875px**，也就是**与 PC 档同像素**
（PC 是 `12.3046875rem` × 16px = 196.875px）。相对 `.156` 的 263.67px 又缩 **25.3%**。

⚠ **「同像素」不等于「同 rem 值」**：两档根字号不同（电视 15px / PC 16px），
所以电视端的 rem 必须是 PC 的 **16/15 = 1.0667 倍**（`12.3046875 × 1.0667 = 13.125`）。
拿两个 rem 值直接对齐是这里最容易犯的错 —— `13.125rem` 看着比 PC 的 `12.3046875rem`
**大**，实际却同像素。`:root` 与 `html[data-device="ps5"]` 两处注释都写清了这条。

**回归**：`game112` 那条电视端磁贴断言从 `263.67px` 改成 `196.875px`
（消息也改成「与 PC 档同像素」）。这条与桌面档那条是一对 ——
只钉桌面档的话，电视端令牌被改回 375 也照样绿。

## v0.5.2.156 (CN-FORK)

> 用户四条需求（原话）：「存档导入的时候要有游戏判定，不是当前游戏的不让导入，精简所有更新日志，
> 只把现在有的功能简单列出来就可以，作为第一版更新日志。游戏管理的磁贴再缩小4分之一。
> ps5的界面最好多缩小一些。」

### ① garlic 存档导入加「游戏判定」

**问题**：garlic 导出的镜像可以往**任何**游戏里导 —— 选错游戏会把别的游戏的存档塞进去。

**判据**：garlic 的存档目录名是 `<TID>_<存档目录名>`（`_ref/garlic-savemgr/src/main.c` 的
`snprintf(..., "%s_%s", title_id, ent->d_name)`），导出的镜像名沿用它
（`PPSA27836_savedata01_enc`）→ **文件名里就有 TID**。这是唯一能用的判据：
**镜像是加密的，内容里读不出游戏身份**。

- 后端 `game_saves_tid_from_name()`（`src/game_saves.c`，**故意非 static**，理由同
  `ga_freeze_verdict`：留在 static 里宿主零覆盖）：4 字母 + 5 数字，**左右都不能贴着字母数字**
  （否则会在长串中间截出伪 TID），结果归一成大写。
- 判定插在 `gs_gx_merge()` **之前** —— 此刻文件还在暂存目录 `.tmp_gx_*` 里，
  拒绝 = `gs_rmtree(stage)`，**一个字节都没碰过用户的存档目录**。
- ⚠ **认不出来 ≠ 不匹配，一律放行**（用户可能给文件改过名）。按不匹配处理会让正常导入整个失效。
- 前端 `SaveManagerModal.jsx` 的 `tidFromName()` 同一条规则，只为**不让用户白等一次 11MB 上传**；
  真判据在后端。文案 `game.garlic_wrong_game`（中英双语）。

### ② 更新日志精简为「第一版 · 当前功能」

`AboutView.jsx` 的 `CHANGELOG` 从 **46 条逐版历史**换成 **11 条按板块的功能清单**
（Payload 管理 / 自动加载 / 游戏管理 / 存档管理 / 账号工具 / PKG 安装 / 常用入口 /
风扇温控 / 其它 / 界面）。用户要知道的是「这个工具现在能干什么」，不是「哪个版本改了什么」。
逐版细节留在本文件（`CHANGELOG.md`，**保留不动**）。

### ③ 游戏管理磁贴再缩小 1/4（`--tile-w` 两档一起 ×0.75）

| 档 | 改前 | 改后（名义值 @16px 根字号） |
|---|---|---|
| PC / 手机 | 16.40625rem（262.5px） | **12.3046875rem（196.875px）** |
| 电视端 | 23.4375rem（375px） | **17.578125rem（281.25px）** |

⚠ 电视端还有**第二层**：本版把 `html[data-device="ps5"]` 的根字号降到 15px，
rem 会再乘 0.9375 → 电视端磁贴**实际落在 263.67px**。两层是相乘的。

### ④ PS5 端界面整体缩小 6.25%

用**根字号**而不是逐个改令牌：全站的尺寸与字号都走 rem（含 Tailwind 的 `text-*`），
改这一处 = 一次缩全部。

- `html[data-device="ps5"] { font-size: 15px }` —— 16 × 0.9375。
- `html[data-device="ps5"] body { font-size: 1.25rem }` —— **必需**：`body` 的字号是绝对值
  （基础档 16px / ≥768px 那档写死 **20px**），不跟着根字号走。压成 1.25rem 后，
  16px 根字号下正好等于原来的 20px（**桌面档零变化**），15px 根字号下是 18.75px
  —— 与全站其余部分**同比例**缩 6.25%。
- `NavButton.jsx` 的侧栏标签 `text-[16.5px]` → `text-[1.03125rem]`：写死 px 不跟着根字号缩，
  电视端整体缩小后只有这一处不缩、看着比周围大一圈。16.5/16 = 1.03125rem，桌面档取值一字未变。

实测（`game112` 电视端一节）：`nav 15.5 / title 15.8 / body 15 / note 13.1 / tiny 12.2 / micro 11.3`。

### 回归与测试

- **宿主测试 `game_saves` 新增第 ⑬ 节「garlic 导入的游戏判定」**（13 checks）：
  正例（`PPSA27836_savedata01_enc` / `CUSA12345.zip` / `sdimg_PPSA08666` / 小写归一 / 带路径前缀）、
  反例（无 TID / 数字只有 4 位 / 右边贴字母 / 左边贴字母 / 5 位混字母 / 空串 / NULL）、
  端到端三条（别的游戏 → 拒 + 回话里带那个 TID + `nf == 0`；同 TID → 放行；认不出 → 放行）。
  ⑪ 原有两条夹具被迫改名（`sdimg_CUSA12345` → `sdimg_PPSA012345`）—— 那是新功能的正确行为。
  ⚠ 正向用例必须用**夹具里已经存在游戏目录**的真 9 位 TID：`gs_root_or_create()` 只 ensure 到
  `savedata_prospero` 父目录、**不建游戏那一级**（真机上那是游戏自己建的），
  随便编一个没存过的 TID 会栽在「写入存档目录失败（磁盘满？）」。`TID_PS5 = "PPSA012345"`
  还是 **10 位**，`gs_tid_from_name` 认不出。
- **前端回归**：`game112` **294**（`.155` 的 291 + 新增 3 条磁贴宽度断言）、`nav112` 20、
  `acct111` 23、`avatar109` 19、`cropfix` 75 —— 全绿。
- **电视端那条字号断言按新基线改了**（`≥16px` → `≥15px`）：它钉的是 `.155` 之前的基准，
  而本版是有意把电视端整体缩小。**别把它改回 16** —— 那等于把用户的缩小要求又取消掉，
  而症状只是「某条断言红了」。

## v0.5.2.155 (CN-FORK)

> **`.154` 的收尾**：真机实测发现「自动加载页延时自我倍增」只修掉了一半 ——
> 两个载荷名相邻时仍会被凭空塞回一条延时行。本版补完，并给这段逻辑补上宿主测试。
>
> `.154` 已出包并推真机，八条需求全部可用；`.155` 只动 `config.c` 里一处判定。

### ⑤′ 自动加载延时：`.154` 只修了一半（真机实测出来的）

`.154` 的修法只按「上一条写出去的**是不是**延时行」判断，于是：

* token `!1000` 不再被补成两行 ✅（`.154` 已修，真机确认）
* 但**两个载荷名相邻**时（用户把条目挪到一起、或手删掉中间那条延时行），
  仍会补一条 ❌ —— 列表与文件对不上，**存一次就被塞回一条**。

真机复现（`192.168.1.214`，写 `AUTOLOAD_LIST` 再读回）：

```
写入 : !1000,kstuff-lite_v1.11.elf,shadowmountplus.elf
读回 : !1000,kstuff-lite_v1.11.elf,!1000,shadowmountplus.elf   ← 凭空多一条
```

用户体感正是「自动加载页面自己多次增加 1 秒延时」。

**现在的判据**：只给**旧文件里从没出现过**的载荷名补延时行 —— 那种名字只可能来自
「用户刚从可用列表里加进来」，它前面天然没有延时；**已经存在于旧文件的载荷名一律不补**，
用户删了就是删了、挪了就是挪了。于是列表与文件严格一一对应，来回存多少次都不变。

`old_delays[]`（原先用来「查历史延时值」）随之删掉 —— 既然只剩「新载荷补 `!1000`」
这一种补法，历史延时值就没有消费者了。

### 宿主测试（`test/config_autoload_test.c`，11 checks）

这段逻辑两轮都只能靠真机复现，因为 `autoload.txt` 的路径写死在 `pldmgr.h` 里。
本版给 `AUTOLOAD_CONFIG_PATH` / `PLDMGR_CONFIG_PATH` / `ARCHIVE_GLOBAL_PATH` /
`BASE_DATA_DIR` 四个宏加了 `#ifndef` 保护（**默认值一字未改**），宿主就能把它们指到
`/tmp` 下穷举。测试走的是真入口 `config_handle_set_json()`，不是重写一份逻辑。

覆盖：相邻载荷名不补行 / 连存三次逐字节一致 / `!1000` 不翻倍 / 新载荷才补 /
删了不塞回 / 老配置裸载荷兜底 / 空 token 与 BOM 容错 / 换行结尾恒定。

**变异验证**：把判定退回 `.154` 的「相邻就补」，测试红 2 条（`overall: FAIL`）；
恢复后全绿 —— 说明这条测试抓得住，不是摆设。

## v0.5.2.154 (CN-FORK)

> **八条需求一次做完**：运行中禁推拉 / 快照改「退出时」并删掉定时 / 拉取前比新旧 /
> 刷新后停在当前页 / 修自动加载延时自我倍增 / 新增「关于」页 / garlic 兼容导入 / 出包。
>
> 用户口径（原文）：
> 「游戏进行中，拉取和推送存档的动作不让做。每次游戏结束后，在本地快照存一份存档，
> 本地自动存储快照删除（定时的功能不要了）。远程插曲游戏的时候，要做一个判定，如果
> 本地存档比远程存档要更新，则不拉取，本地存档比远程存档旧，可以直接拉取覆盖。
> 所有页面刷新后停留在当前页面。自动加载 页面，自己多次增加1秒延时需要修复。
> 在设置下边增加一个菜单叫关于 把更新日志 和支持作者的内容全部放到关于里面。
> 在存档管理页面，添加一个兼容导入按钮兼容garlic的存档。同时解释一下garlic 的加密
> 和解密代表什么。改完打包成elf。」

### ① 游戏运行中：推 / 拉存档一律不让做

* **后端**（`game_auto.c` 的 `game_auto_sync_action()`）：手动推 / 拉前先当场探一次
  「这个游戏在不在跑」，在跑就 400 拒拦，人话只说一句「请先退出游戏再推送/拉取存档」。
* **前端**（`SaveManagerModal.jsx`）：`st.running` 时两颗按钮置灰，**并在按钮下面写一行
  理由** —— 只挂 `title` 不行，电视端遥控器用户没有 hover，永远看不到。
* 为什么不是「只是麻烦」：推 / 拉都是**整份替换**存档目录，而游戏此刻正把存档读在内存里、
  退出时还要写回去。这时候动它最容易把进度搞丢。
* 判据用 `auto_state.running`（后端**当场探测**），与那颗「游戏运行中」芯片同源；
  回归里专门有一条「**在跑的是别的游戏时不误拦**」—— 误拦比不拦更烦人，而且
  「恒灰」的实现能让所有正向断言都过。

### ② 本地快照改成「每次游戏退出后」，定时守护整条删掉

* **删掉**（`game_saves.c`）：`gs_snap_cfg_t` / `gs_snap_due()` / `gs_snap_sweep()` /
  `gs_snap_main()` / `game_saves_snap_start_daemon()` / `GS_SNAP_TICK` 等一整条定时守护；
  `main.c` 里那次 `start_daemon()` 调用同步去掉。
* **新增触发点**：`game_auto_plan()` 从 6 参扩到 7 参（加 `prev_snap`），`out[2]` → `out[3]`，
  步数 0~3，顺序恒为 **快照 → 推送 → 拉取**（先留底再动远程，任何一步失败都还有退路）。
  退出结算只等一次 `GA_EXIT_SETTLE`（`settled` 标志），不是每一步各等一次。
* **新增查询** `game_saves_snap_enabled()`（全局优先，与内部 `gs_snap_cfg` 同口径）——
  引擎退出时先问一句，开着才备份。**本地快照不跟随远程总开关**，它是存档的最后一层兜底。
* **UI 同步**：`ArchiveConfigModal` 删掉「间隔（分钟）」输入框；`SaveManagerModal` 的只读行
  从「间隔（分钟）」换成「备份时机 / 每次游戏退出后」。**留一个填了没人读的参数比没有更坏。**
  `EMPTY_CFG.snap.interval` 与 `normalizeCfg()` 里的 `interval` **刻意保留**（旧全局配置要能
  原样往返、后端 `gs_snap_cfg()` 还在解析它），只是界面上不再可编辑。

### ③ 拉取前先比新旧：本地更新就不覆盖

* **纯函数** `ga_pull_needed(local_have, local_mtime, remote_have, remote_mtime)`，
  四条出口写死在头文件注释里，总原则是「**说不清一律按该拉处理**」。
* **取数**：`game_saves_local_mtime()`（`gs_walk` 取目录里所有普通文件的最大 mtime ——
  目录自身的 mtime 只反映「有没有增删条目」，游戏原地改写 `sdimg_` 时它根本不变）。
* **`sr_stat()` 补齐 FTP / HTTP**（`.153` 及以前这两档一律回 -1 = 永远「说不清」，
  判定等于完全失效）：读推送时一并写下的 `<uid>.meta.json`。
  ⚠ **只有 SMB 会回 `SR_ENOTFOUND`，FTP / HTTP 一律回 -1** —— 因为「meta.json 不在」
  分不清是「远端没存档」还是「`.145` 之前推的老存档没写 meta」，
  而回 `SR_ENOTFOUND` 会让老存档用户的自动拉取**整个失效**。
* 结果落到日志与通知：`… · 本地更新 %d（未覆盖）`；`note` 里 `newer` 分支**必须排在
  `skipped` 之前**（顺序写反了，用户看到的会是「跳过」而不是「本地更新」）。

### ④ 刷新后停在当前页（`App.jsx`）

`localStorage['pldmgrView']` + `RESTORABLE_VIEWS` 白名单（`NAV_ITEMS` 全部 id + `sources`）。
**`move_from_usb` 刻意不在白名单**：它靠只在内存里的路径，刷新后留在那页只会是空白。
读不出来（隐私模式 / 老 WebKit 抛错）一律回落 `storage`。

### ⑤ 自动加载页「延时自己翻倍」的真因

`/get_config` 把 `autoload.txt` 每一行（含 `!1000` 这类延时行）逗号拼接回传，旧写回逻辑
给**每个 token** 都补一个默认延时 → 延时行自己翻倍。修法：写回前记 `prev_is_delay`，
`!` 开头的行原样写、且不再给它前面补默认值（`src/config.c`）。

### ⑥ 设置下面新增「关于」页

* 新建 `AboutView.jsx`：支持作者（QR / 链接，`isPS5` 分流）、关于本 Fork（只读
  `__APP_VERSION__`）、参考与致谢（原 6 条 + **新增 earthonion / garlic-savemgr**）、更新日志。
* 更新日志从 `SettingsView` 里那个 46 条的数组 + `<Modal>` 搬过来，改成页内 `<Section>`
  —— 内容长，弹窗里滚不如直接看。
* `NAV_ITEMS` 加 `about`（紧跟 settings 之后，侧栏与手机底部栏一处定义两处渲染）；
  `DonateView.jsx` **删除**（内容全部并入）。
* i18n：新增 `app.nav.about` / `app.nav.about_mobile` / `about.*` 共 7 键，
  并就地改写 4 条快照文案（去掉「按下面的间隔」，补上「每次游戏退出后自动生成」）。

### ⑦ garlic 兼容导入（`POST /game_save_import_garlic`）

**「加密 / 解密」到底指什么**（用户问的就是这个）：

| | garlic 的说法 | 实际做的事 | 我们做不做 |
| --- | --- | --- | --- |
| **解密** | Decrypt | 用主机密钥把**加密的 PFS 镜像**挂载成明文目录，好让 PC 端能读里面的文件 | ✗ 不做 |
| **加密** | Encrypt | 在**主机上新建**一个加密 PFS 镜像，把明文目录塞回去 | ✗ 不做 |

两者都要 `/dev/pfsmgr` 的 ioctl + `libSceFsInternalForVsh` + 进程提权，属于**内核级**动作
（本项目 `.54` 有整机死机的前科），不适合顺手加进来。
**所以我们只接受「已经加密好」的镜像** —— 也就是 garlic 的
**「Download Encrypted」**（PS5：裸的 `sdimg_<名>` 一个文件；PS4：一个 zip，里面是
`sdimg_<名>` + 配套的 `<名>.bin` 密封密钥）。它与我们自己的「下载存档」是同一层东西
（`<uid>/savedata_prospero/<TID>/` 里的文件），**纯文件操作、零内核调用**。

* **语义与「上传存档」刻意不同**：上传 = **整份替换**（整个 `<TID>` 目录换掉）；
  导入 = **逐文件合并**（只覆盖同名的那几个镜像）。因为一个存档目录里除了当前镜像还有
  主机自己的记账（`sdimg_sce_bu_<名>` 备份、`sdimg_sce_sdmemory` 系统存档），
  整份替换会把它们一起抹掉，主机很可能就认不出这份存档了 —— 而 garlic 导出的只是**一个**镜像。
* **`name` 走 query 是硬契约**：裸镜像没有后缀、内容也不是 zip，后端只能靠这个原始文件名
  决定落盘叫什么（主机认的就是这个名字，我们不去猜）。
* ⚠ **先落暂存目录（`.tmp_gx_<pid>_<ts>`，在存档根**父目录**下、同一文件系统）、整包成功才
  rename 合并**：`zip_extract` 是「遇到坏条目才整包拒收」的，直接往存档目录里写时，
  包里第 N 个条目名字不合法时前 N-1 个已经躺在用户存档目录里了 —— 那会让主机把这份存档
  认成损坏的。失败整包 `gs_rmtree`，一个字节都不进用户目录。
* 路径安全：`gs_gx_cb` **只取 basename**（`../evil` 被压平成 `evil`），`gs_slot_ok` 挡掉
  空名 / 点开头 / 含分隔符 / 控制字符。
* 前端：`SaveManagerModal` 的「存档列表」块底部**单独一块**（用发丝线隔开，不挤进标题栏 ——
  并排放在「上传存档」旁边，用户会以为「都是上传，选一个就行」）；独立 file input，
  **刻意不写 `accept`**（garlic 导出的就是无后缀的 `sdimg_*`，写 `.zip` 会在文件选择器里
  直接把它藏起来）；多用户时才出现目标用户选择器；导入前走确认弹窗。

### ⑧ 测试 / 变异 / 前端回归

| 关卡 | 结果 |
| --- | --- |
| `game_saves` | **183** checks（新增 ⑪ garlic 导入 6 段 / ⑫ 快照只剩退出时触发） |
| `save_remote` | **94** checks（新增 `sr_stat` 两协议断言块） |
| `game_auto` | **152** checks（新增 `t_pull_gate` 9 条 + `t_pull_gate_e2e` 端到端 4 段 + 运行中拒拦 5 条） |
| `test/mutation_check_freeze.sh` | **7 个变异全部 MUTATION OK**（新增 `gate` / `snap` / `runblock`） |
| `game112`（前端） | **291**（原 272；新增 5f 节 19 条：运行中禁推拉 7 + garlic 导入 12） |
| `nav112` / `acct111` / `avatar109` / `cropfix` / `pack125` | 20 / 23 / 19 / 75 / 15 全绿 |

变异脚本的三处判据踩坑（都记在脚本注释里）：`grep -ac "A\|B"` 与 `grep -ac "A"` 计数可能不同
（**依赖交替模式的计数不可靠**）；`FAIL` 打印的是**断言说明**而不是期望值，所以判据词必须
从断言说明里取；`snap` 那个变异命中 5 条而期望 6 条，是因为有一条 FAIL 文案里没有「快照」二字。

---

## v0.5.2.153 (CN-FORK)

> **`.152` 的 SIGSTOP 主方案在真机上**根本没跑起来** —— 修「读进程状态失败」与
> 「验证不可用就回落」这两条。**
> 用户口径：「远程拉取成功后，并没有将游戏拉到前台」。

### ① 真机证据（`192.168.1.214`，`.152` 运行中）

```
[23:22:28] saves: 读不到 pid=582 的进程状态，进程级冻结跳过
[23:22:30] saves: PPSA27836 已暂停（LncUtil 挂起）· 拉取完成后自动恢复
[23:22:32] saves: 恢复 PPSA27836：挂起时 app_id=24601，此刻 GetAppIdOfRunningBigApp=-1
[23:22:32] saves: PPSA27836 已恢复运行（app_id=24601）
```

**枚举找到了 pid=582**（`KERN_PROC_PROC` 是好的），但**按 pid 单查那一步失败了** ——
于是 `ga_sigstop_freeze()` 在**发出 SIGSTOP 之前**就 `return -1`，整条主方案作废、
回落 LncUtil，游戏照样跳后台。用户看到的「没拉到前台」就是这么来的。

### ② 根因两条（一条机制、一条策略）

* **机制**：`ga_proc_stat()` 用的是 `KERN_PROC_PID`（按 pid 单查），真机上直接失败；
  而 `KERN_PROC_PROC`（整表枚举）是**真机验过的** —— `ga_find_eboot_pid()` 就是靠它
  找到 pid 的。**同一台机器上一条能走一条不能走**，那就换成能走的那条。
* **策略**（更该记住的一条）：**「验证不可用」被当成了「冻结失败」**。
  「冻住后 CPU 时间不再前进」只是**加分项**，不是前提 —— 读不到进程状态、
  或进程本来就没在烧 CPU（对照样本不动）时，验证只是**说不清**，
  绝不该因此放弃 SIGSTOP。这一格就是 `.152` 真机栽的地方。

### ③ 修法

* `ga_proc_stat()` 改走**整表枚举**（`KERN_PROC_PROC` + 按 `ki_pid` 匹配），
  与 `ga_find_eboot_pid()` 同一条路；失败时把 `errno` 记进日志。
* 判定抽成**纯函数 `ga_freeze_verdict(have_ctl, ctl_adv, post_ok, post_adv)`**
  （`include/game_auto.h` 里导出，理由见下）：
  `1` = 确认冻住 · **`0` = 无法验证（按已冻处理，绝不回落）** · `-1` = 确认没冻住。
  只有「对照推进了、冻结后**还在**推进」才返回 -1。
* 一次性把「`kinfo_proc` 编译期大小 vs 内核报的 `ki_structsize`」记进日志 ——
  `.152` 那个「单查读不到」怀疑是 SDK 头与固件内核的结构体版本错配，有这两个数
  下次一眼可判。
* 新增日志：`冻结验证不可用（pid=… 的进程状态读不到，或 CPU 对照未推进）· 按 SIGSTOP 已投递处理`。

**为什么把判定**导出**成纯函数**：`.152` 栽的是**策略**错，而真机那段代码整段在
`#ifdef GA_HOST` 里 —— 宿主根本编不到，等于这个坑零覆盖。抽成纯函数之后，
宿主可以把 16 种输入组合全部穷举（同 `.150` 导出 `game_auto_tick()` 的理由）。

### ④ 宿主回归：118 checks（原 113）+ 变异扩到四个

* 新增第 ⑨ 节：判定表 **16 种组合穷举**，外加三条**带名字**的策略断言
  （「读不到进程状态时必须按已冻处理，不许回落」等）。
* `test/mutation_check_freeze.sh` 加第四个变异 `verdict` —— 把「验证不可用」那格
  改成 `return -1`（= 把 `.152` 的 bug 原样放回去）：**恰好 3 条红**
  （判定表 1 条 + 两条策略断言）。其余三个变异原样通过。

| 变异 | 期望 |
| --- | --- |
| `order`：把 freeze 挪到 `ga_do_inner` 之后 | 恰好 2 条红 |
| `handle`：冻结后不把凭据交上去 | 恰好 1 条红 |
| `kind`：不把机制代号交上去 | 恰好 1 条红 |
| `verdict`：验证不可用时 return -1（= `.152` 的 bug）| 恰好 3 条红 |

完整宿主套件 `overall: PASS`（`game_auto` 118/0、栈帧守卫、POST 路由守卫都过）。

### ⑤ 真机验收**通过**（`192.168.1.214`，用户连开三次游戏）

三次**全部**走 `进程级 SIGSTOP`，一次都没回落 LncUtil：
```
kinfo_proc 编译期 1088 字节 · 内核报 1096 字节 · 共 88 条
冻结验证 pid=596：CPU 200ms +60089 us → +0 us · stat 2→4
PPSA27836 已暂停（进程级 SIGSTOP）· 拉取完成后自动恢复
远程拉取 结束 · 成功 1 / 跳过 0 / 失败 0 · 用时 3559 ms
PPSA27836 已恢复运行（pid=596）
```

* **`KERN_PROC_PID` 失败的真因钉死**：`kinfo_proc` **编译期 1088 字节、内核报 1096 字节**
  —— SDK 头比固件内核**小 8 字节**。FreeBSD 的 `sysctl_out()` 要求 `oldlen >= ki_structsize`，
  给 1088 < 1096 → 直接 `ENOMEM`。整表枚举不受影响（缓冲按内核报的总长 `malloc`）。
* **验证是真的**：对照 `+60089 us / 200ms`（真在烧 CPU），SIGSTOP 后 `→ +0 us`。
* **`stat 2→4` = `SRUN` → `SSTOP`** —— 内核自己承认进程被停住，比 CPU 时间更硬。
* 冻结总时长 ≈4.4 秒（拉取 3.0–3.6 s + 采样 0.6 s）；恢复后游戏又正常玩了 21 秒才退出。

## v0.5.2.152 (CN-FORK)

> **把「拉取期间挂起游戏」换成进程级 SIGSTOP —— 顺手把「恢复后不回前台」这个死结绕开。**
> `.150`/`.151` 的挂起是有效的（拉取期间游戏确实读不到存档），但它有个真机副作用：
> `sceLncUtilSuspendApp` 会让系统把游戏换到后台（跳回主屏），而 `ResumeApp`
> **只解冻、不切前台** —— 用户必须手动点一下才回得去。
> 用户口径：「点击开始，游戏会自动进后台，出现图片这个画面后，手动点击按钮会恢复，
> 但是不自动恢复」。

### ① 先用探针把「切回前台」这条路**证伪**（不是猜的）

`_scratch/lnc_probe.c`（独立 ELF，崩了只死自己，而且挂起之后立刻恢复）在真机上逐个
试了候选 API，结果**全部返回 0 却毫无效果**：

```
sceLncUtilResumeApp(57369)               = 0x00000000
sceLncUtilSetAppFocus(57369, 0)          = 0x00000000   ← 成功但无效
sceLncUtilSetAppFocus(57369, 1)          = 0x00000000   ← 成功但无效
sceLncUtilSetControllerFocus(57369, 0)   = 0x00000000   ← 成功但无效
sceLncUtilSetControllerFocus(57369, 1)   = 0x00000000   ← 成功但无效
sceLncUtilContinueApp(57369)             = 0x80020010   ← EBUSY
```

结论：**display 归 ShellUI 的 scene manager 管，不是这几个 LncUtil 调用能撬的**。
公开资料里也查不到它们的签名（SDK 的 `libSceSystemService.so` 是桩库、所有符号同址
`0xaae4`；shadPS4 里是空壳；psdevwiki / grep.app / LightningMods 的 daemon writeup
全都只有名字没有参数）。

**为什么不直接猜一个签名塞进主流程**：恢复跑在守护线程上，猜错让进程崩掉时游戏**正被
挂起**，那就变成「游戏永久冻死、只能强退重进」。这个失败模式不能拿去赌 —— 能测就先测。

### ② 换思路：**别让游戏离开前台**

`SIGSTOP` 只把游戏进程停住，系统状态一点没变，所以「切回前台」这个问题**根本不存在**。
它同时更贴近用户原话「暂停游戏」：用户看到的是游戏画面冻住一两秒然后继续，而不是被
踢回主屏。

* 进程判据 `comm == "eboot.bin"`（与 ps-game-state-lib 同），枚举手法抄
  `src/ps5_launcher.c` 的 `get_pid_by_name` —— 那边真机给 SceDiscPlayer 发过 SIGKILL，
  `sysctl(KERN_PROC)` 枚举 + 跨用户发信号都是实证过的。
* **冻结要验证**，不是「`kill()` 返回 0 就算冻住了」：`kill()` 返回 0 只说明信号发出去了。
  做法是先量 200ms 的 CPU 时间做**对照**（运行中的游戏一定在涨），SIGSTOP 之后再量两次，
  要求 `ki_runtime` 不再前进。日志留一行证据：
  `冻结验证 pid=543：CPU 200ms +… us → +0 us · stat …→…`。
  停不住就**立刻 SIGCONT 放行**（绝不把半死的进程留在那儿），再回落 LncUtil 挂起 ——
  慢一点、会跳主屏，但至少有效。
* `GA_TICK` / `GA_BOOT_DELAY` / `GA_EXIT_SETTLE` 与「只用于自动拉取」的口径都没变。

### ③ 冻结/放行接口从「挂起/恢复」改名，凭据变成**不透明 handle**

* `ga_susp_fn` / `ga_resm_fn` → `ga_frz_fn` / `ga_thaw_fn`，`game_auto_set_suspender`
  → `game_auto_set_freezer`；签名多一个 `kind`（`GA_FREEZE_SIGSTOP` / `GA_FREEZE_LNC`）。
  放行必须知道冻的时候用的是哪套 —— 用 `ResumeApp` 去解一个被 SIGSTOP 停住的进程
  = 它永远不会被放行。
* `.150` 那条不变量原样保留：**放行必须用冻结时报的那个凭据，不许当场重新解析**
  （SIGSTOP 那边凭据是 pid，重找会拿到新进程，而被冻住的那个就没人放了）。

### ④ 宿主回归：113 checks（原 112）

* 新增「放行该收到冻结时报的机制代号」。
* 顺序那四条、凭据那一条、手动拉取不冻结、冻不住仍拉取……文案跟着改成冻结/放行，
  断言一条没少。

### ⑤ 变异对照扩到三个（`test/mutation_check_freeze.sh`）

| 变异 | 期望 |
| --- | --- |
| `order`：把 freeze 挪到 `ga_do_inner` 之后 | 恰好 2 条红（顺序那两条）|
| `handle`：冻结后不把凭据交上去 | 恰好 1 条红 |
| `kind`：不把机制代号交上去 | 恰好 1 条红 |

### ⑥ `-DGA_NO_LNCUTIL` 改名 `-DGA_HOST`

`.152` 起它挡的已经不只是 LncUtil，还挡 `sysctl(KERN_PROC)` + `struct kinfo_proc`
（Linux 的 `<sys/user.h>` 是另一回事，根本没有 `kinfo_proc`）。名字得跟实际含义对上，
否则下一个人会以为宿主上只是没有 LncUtil。

## v0.5.2.151 (CN-FORK)

> **`.150` 真机首轮的收尾修复：恢复时不许再解析 app_id。** `.150` 的挂起本身是对的
> （真机日志里 `已挂起 → 远程拉取 开始 → 远程拉取 结束` 三行齐全、拉取成功），
> 但**恢复失败**，而且**下一秒**引擎就报「已退出」：
> ```
> 22:40:11  已挂起 · 拉取完成后自动恢复
> 22:40:11  远程拉取 开始 · PPSA27836 · 目标=SMB · 192.168.1.107/ps5save
> 22:40:13  远程拉取 结束 · 成功 1 / 跳过 0 / 失败 0 · 1 个文件 / 15728640 字节 · 用时 1846 ms
> 22:40:13  !!! 恢复失败 —— 请手动切回游戏（或退出重进）
> 22:40:14  已退出 · 跳过推送：「退出游戏时回传」关着
> ```
>
> **「恢复失败」的下一秒就是「已退出」——这不是游戏真的退了，是我们自己丢了句柄。**

### ① 根因：挂起后 `GetAppIdOfRunningBigApp()` 返回 `0xffffffff`

* 应用被挂起后，系统认为「**没有正在运行的大应用**」（挂起本来就是把它换出去）。
* `.150` 的 `ga_resm_default()` 却在恢复时**又解析了一次** app_id（`ga_running_app_id()`）：
  解析拿不到 → `return -1` → **`sceLncUtilResumeApp` 根本没发出去** → 游戏一直冻着。
* 于是下一个 tick 的探测也拿到 -1 → `cur=""` → 日志写「已退出」。
  **游戏并没有退出，是冻在那儿**；用户只能强退重进（真机日志里每轮的间隔
  7 秒 / 45 秒 / 25 秒不规则，就是这个原因）。

### ② 修法：挂起时记下 app_id，恢复直接用它，**绝不再解析**

* `ga_susp_fn` 改成 `int (*)(const char *tid, unsigned int *app_id_out)` ——
  挂起成功时**必须**把 app_id 写出来；`ga_resm_fn` 改成 `int (*)(const char *tid,
  unsigned int app_id)`，收到的就是它。这条不变量在宿主上有断言。
* `ga_freeze_for_pull(tid, &app_id_out)` 返回「冻没冻住」+ app_id；
  `ga_do` 用 `frozen_id` 传给 `ga_thaw_after_pull(tid, frozen_id)`。
* **两个返回码都进日志**（这是下一轮不用再猜的关键）：
  * 挂起失败：`sceLncUtilSuspendApp(%d) 返回 0x%08x`
  * 恢复：`恢复 %s：挂起时 app_id=%u，此刻 GetAppIdOfRunningBigApp=%d` ——
    这一行是「挂起后系统就不认这个大应用了」的**直接证据**；
    再失败还有 `sceLncUtilResumeApp(%u) 返回 0x%08x`。
* 成功路径的日志也带上 app_id（`已挂起（app_id=%u）` / `已恢复运行（app_id=%u）`），
  两行能对上。

### ③ 宿主回归：新增「恢复拿到的 app_id == 挂起报的那个」

`game_auto` 106 → **112 项断言**（0 失败）：

* ① 正常路径新增 `CHECK(g_resm_id == FAKE_APP_ID, …)` —— 桩在挂起时交出 `0x10007`，
  恢复必须收到同一个值。
* 新增 ⑤ **恢复失败**：日志 `恢复失败` 恰好 1 条、**屏幕通知**恰好 1 条、日志带 tid、
  且**不影响拉取本身**（存档该拉回来还是拉回来）。

### ④ 变异对照扩成两个（`test/mutation_check_freeze.sh`）

* `order`：把 freeze 挪到 `ga_do_inner` 之后 → **恰好 2 条红**（顺序那两条）。
* `appid`：去掉挂起时 `*app_id_out = app_id` 那一行（= 真机首轮那个 bug 的等价形态）
  → **恰好 1 条红**（app_id 那条）。
* 两种都不接进 `run_host_tests.sh`（要重编两遍）；跑法 `bash test/mutation_check_freeze.sh [order|appid]`。

### ⑤ `.150` 那段「待真机验证」的结论

`.150` 的**顺序与挂起**是对的（真机三行日志齐全、拉取 15 MB 只用 1.8 秒）；
错的只有「恢复」这一步。`.151` 只动恢复那一条路径，顺序、时机、扫描周期（`GA_TICK 1`）
全部沿用 `.150`。

### ⑥ 真机验收通过（`192.168.1.214`，已推 `-me`）

完整一轮（日志原文）：
```
22:48:16  PPSA27836/1a1d3627 已从快照 20261008-191114 还原   ← 先换成旧存档，好验证拉取生效
22:48:26  PPSA27836 已挂起（app_id=24601）· 拉取完成后自动恢复
22:48:26  远程拉取 开始 · PPSA27836 · 触发=自动（启动游戏时） · 目标=SMB · 192.168.1.107/ps5save
22:48:28  upload ok -> /user/home/1a1d3627/savedata_prospero/PPSA27836 (1 files, 15728640 bytes)
22:48:28  远程拉取 结束 · 成功 1 / 跳过 0 / 失败 0 · 1 个文件 / 15728640 字节 · 用时 1903 ms
22:48:28  恢复 PPSA27836：挂起时 app_id=24601，此刻 GetAppIdOfRunningBigApp=-1
22:48:29  PPSA27836 已恢复运行（app_id=24601）
22:49:00  PPSA27836 已退出 · 跳过推送：「退出游戏时回传」关着
```

* **顺序逐字对上用户口径**：游戏启动 → 监测到启动 → 暂停游戏 → 拉取存档 →
  拉取成功 → 恢复游戏。
* **根因得到实证**：`此刻 GetAppIdOfRunningBigApp=-1` —— 挂起后系统确实不认这个
  大应用了（与 `.150` 的推断一致）。而用**挂起时记下的** `app_id=24601` 恢复**成功**。
* **游戏没有被冻死**：恢复后用户又玩了 **31 秒**才正常退出（`.150` 时是「恢复失败」
  的下一秒就报「已退出」）。

## v0.5.2.150 (CN-FORK)

> **「启动游戏时拉取」的时序竞态修复：拉取前先暂停游戏，拉完再恢复。** 用户口径：
> 「偶尔成功，偶尔失败，是不是启动时间的问题，如果是，分两步考虑，第一把监听时间间隔
> 调整为 1 秒，第二，拉取时暂停游戏，等拉取完再启动游戏」，随后确认了顺序：
> **「游戏启动 → elf 监测到启动信息 → 暂停游戏 → 拉取存档 → 拉取成功 → 恢复游戏」**，
> 并补一句「同步把监测时间也改成 1 秒」。
>
> `.149` 修的是**必然失效**（`gs_swap_dir` 换掉存档目录 inode，跑着的游戏永远看不到新内容）。
> 这一轮修的是**剩下那条竞态**：拉取是把盘上存档整份换掉，而游戏启动时会把存档读进内存 ——
> 如果那一下发生在我们的拉取**之前**，换盘上的文件对它毫无影响（它用内存里那份，
> 之后还会把旧的写回去）。所以「启动时拉取」本来是场赛跑：**快启动的游戏输、慢启动的赢**，
> 这就是用户看到的「偶尔」。冻住游戏之后它读不到存档，等我们拉完再放行，竞态就没有了。

### ① 拉取前挂起正在启动的那款游戏，拉完恢复（后端 · 头号改动）

**改了什么**（`src/game_auto.c` / `include/game_auto.h`）

* `ga_do()` 新增最后一参 `int freeze_game`。为 1 且动作是**拉取**时，在拉取之前
  `ga_freeze_for_pull(tid)`、拉取成功之后 `ga_thaw_after_pull(tid)`。
  **只有守护线程的自动拉取传 1**（`ga_tick` → `ga_do(..., 1)`）；手动拉取传 0。
* 顺序（也就是日志里那四行的顺序，逐字对上用户口径）：
  ```
  已挂起 → 远程拉取 开始 → 远程拉取 结束 → 已恢复
  ```
  「已挂起」放在「开始」**之前** —— 「开始」这行说的确实是「存档真的要开始搬了」，
  而不是「我打算搬、先冻两秒」。
* 挂起/恢复的接口刻意收在 **tid** 上（不是 app_id）：app_id 每次启动都不一样、宿主上
  也没法造，而 tid 是测试本来就有的东西。tid → app_id 的解析（`ga_running_app_id()`）
  放在默认实现里，真机那一步才做。
* 默认实现：`sceLncUtilSuspendApp(app_id)` → `sleep(2)` → 拉取 → `sceLncUtilResumeApp(app_id)`。
  `sleep(2)` 与 `src/ps5_launcher.c` 的 `ps5_kill_disc_player` **同一口径**（Suspend 是
  **请求**，返回时应用未必真的冻住）。**没有**用 `sceLncUtilIsAppSuspended` 轮询 ——
  那个符号虽然也在 `libSceSystemService.so` 里，但**没有头文件、签名是猜的**，
  为了省一两秒去赌一个原生调用不值得。
* **`sceLncUtilResumeApp` 是这轮新确认存在的**：与 `SuspendApp` 成对，都在真机 SDK 的
  `libSceSystemService.so` 里（`strings` 扫出来的）。**没有 SDK 头文件**，与
  `GetAppIdOfRunningBigApp` 一样靠 `extern` 声明。没有它这一步就做不成（挂起了回不去
  = 把用户正在玩的游戏挂死），所以先确认再动手。
* **三条失败路径都有明确收场**：
  * **挂不住**（SuspendApp 返回非 0）→ **仍然继续拉取**，打一行
    「挂起失败，仍继续拉取（游戏可能已经读进旧存档，这次拉取未必生效）」+ 弹一条通知。
    不阻断是因为那只是退回 `.149` 的老行为（有竞态、但比不拉强），而「配了自动存档却
    什么都不做」比「拉了一次可能不生效」更难排查。
  * **恢复失败** → 日志 `!!! %s 恢复失败 —— 请手动切回游戏` + **屏幕通知**。这条必须让人
    知道：游戏会一直停在冻结状态，用户看到的是「点了一下就死机了」。
  * **没冻过就绝不恢复**：`frozen` 只在真的冻住时为 1，避免把用户自己挂起的游戏平白叫醒。
* **只冻目标那款**：探测到跑的是别的游戏时不动它（人家跟我们这份存档没关系）。
* **只用在自动拉取**：手动拉取时游戏早就把存档读进内存了，冻它改变不了内存里那份，
  只会平白把用户正在看的画面冻一下。宿主有一条断言专门防「顺手给手动也加上」。
* 计时**从冻住之后开始**：`sleep(2)` 若算进「拉取用时」，每次自动拉取都会凭空多两秒，
  看着像网络慢 —— 排查时最忌讳这种被污染的指标。

### ② 扫描周期 2 秒 → 1 秒（后端 · 用户口径第一条）

* `src/game_auto.c` 的 `#define GA_TICK 2` → `1`。
* 一轮的成本只有一次 `sceLncUtilGetAppIdOfRunningBigApp()`（游戏没换的时候连读配置那步
  都走不到），可以忽略；探测越早，越可能在游戏读存档之前把它冻住。
* 代码里**只有这一处**是「监测游戏启动」的间隔：`GS_SNAP_TICK 30` 是本地快照守护
  （按每款各自的 interval 判定）、`FAN_INTERVAL` 是风扇温控，都不是这条链路。

### ③ 把扫描那一轮导出成 `game_auto_tick()`，让宿主能钉住顺序（测试基建）

* `static void ga_tick(void)` → `void game_auto_tick(void)`（进 `include/game_auto.h`）。
  守护线程照旧调它。
* **导出是刻意的**：挂起那段逻辑**只在这条路上**（`ga_do` 的 `freeze_game` 只有这里传 1），
  不导出就等于**零覆盖** —— 而这恰恰是唯一动到「游戏运行链路」的改动。
* 宿主新增 `game_auto_set_suspender(susp, resm)` 钩子（与既有的 `game_auto_set_probe`
  同一手法，`-DGA_NO_LNCUTIL` 关掉真实现）。

### ④ 宿主回归 ⑦：顺序、失败路径、以及「手动不挂起」（`test/game_auto_test.c`）

`game_auto` 从 70 项断言增至 **106 项**（新增 36 项，0 失败）：

* ① 正常：挂起 → 拉取 → 恢复，桩各调一次、参数是那个 tid、存档确实被拉回来；
  日志顺序断言 **`已挂起` 是这条链的第一行**、且 `挂起 < 开始 < 结束 < 恢复`。
* ② 挂不住：`susp_stub` 返回 -1 → **仍然拉取**（存档确实回来了）、**不恢复**、
  且留下一行「挂起失败」。
* ③ 游戏在 `game_auto_tick` 与 `ga_do` 之间退出（用「只在前 N 次探测里报在跑」的探针
  造这个窗口）→ 不挂起、不恢复、拉取照跑、日志里**没有**「已挂起」。
* ④ 手动拉取（游戏在跑）→ 不挂起、不恢复。

### ⑤ 变异对照：`test/mutation_check_freeze.sh`（新）

把 `int frozen = ...` 那一行从「拉取前」挪到 `ga_do_inner` **之后**（= 先拉后冻），
重编再跑 —— 实测**恰好 2 条红**（「已挂起」该是第一行 + 挂起必须在「开始」之前），
无连带红。

**为什么值得单列一条脚本（本工程第四次为「测试有效性」写对照）**：这个修复的全部价值
就在**顺序**上，而顺序恰恰是「测试照样绿、行为已经错」的重灾区 —— 桩被调到了、日志里
那几行都在、调用次数也对，一个只断言「挂起过一次 / 恢复过一次」的用例在**错误顺序**下
照样全绿。所以变异必须做在**位置**上，验证顺序断言真的会咬人。

### ⑥ 真机首轮结果（已推 `192.168.1.214`，见 `.151`）

* **对的部分**：`已挂起 → 远程拉取 开始 → 远程拉取 结束` 三行齐全、顺序正确；
  拉取本身成功（1 个文件 / 15,728,640 字节 / 用时 1.8 秒）。
* **错的部分**：**恢复失败**，紧接着引擎报「已退出」（其实是游戏被冻着没退）。
  根因与修法见 **`v0.5.2.151`** —— 恢复时不许再解析 app_id。
* 风险提示（`.151` 仍成立）：`sceLncUtilResumeApp` 是首次在本工程使用
  （`SuspendApp` 之前只在 `ps5_kill_disc_player` 里用过、且后面跟的是 Kill 不是 Resume）。
  若某个游戏恢复失败，日志里会有 `!!! 恢复失败` 且屏幕上会弹通知 ——
  那种情况退一步把「启动游戏时拉取」关掉即可。

## v0.5.2.149 (CN-FORK)

> **「游戏启动时拉取提示成功、但游戏里还是老存档」的根因修复。** 用户口径：
> 「我现在手动拉取是成功的，完事游戏启动时拉取提示成功，但还是老存档」。
>
> 手动拉取好用、自动拉取没用 —— 两者**走的是同一段代码**（`game_auto.c` 的 `ga_do`），
> 差别只有一条：**那一刻游戏在不在跑**。而 `game_saves.c` 的 `gs_swap_dir()` 是
> `rename(target, old)` —— 它把**整个存档目录**换成一个新 inode。游戏启动时已经把
> 存档目录绑住了，换掉 inode = 正在跑的游戏永远看到那个被改名走的旧目录。
>
> 真机实测（FTP 的 `MLSD` `unique` 字段就是 inode）：
> ```
> 拉取前 /user/home/<uid>/savedata_prospero/PPSA27836  unique=0.5b0
> 拉取后 同一条路径                                      unique=0.5ab
> ```
> 目录被整个换掉了。改成「目录不动、只换里面的内容」后这个值必须不变。

### ① `gs_swap_dir()` 改成「保住目录本身，只换内容」（后端 · 头号根因）

**改了什么**（`src/game_saves.c`）

* 原来无条件 `rename(target, old)` + `rename(src, target)`；现在 **target 已存在时不动
  目录本身**（inode 不变），只搬它里面的条目：
  * ① `target` 的条目 → `old/`（回滚：把 `old/` 里的全搬回 `target`）
  * ② `src` 的条目 → `target/`（回滚：先把 `target` 里刚搬进来的全退回 `src`，
    再把 `old/` 搬回 `target`）
  * 两步的回滚都**无歧义** —— 这是刻意选的顺序，别改成「一个名字换一个名字」那种
    交错写法：那样回滚要先记住哪些名字搬过，出错路径就成了最容易写错的地方。
* 新增 `gs_move_entries(from, to)`（不递归 rename 一层条目，**自己不做回滚**，
  顺序由调用方定）。
* `target` 不存在时（全新拉取 / 主机上还没有这份存档）**没有 inode 要保**，
  仍走原来的直接 `rename(src, target)`。这条分支保留着 —— 它是「换目录」唯一还正确的场合。
* **代价**：①② 之间 `target` 会短暂为空（几个 `rename` 的时间）。正在读存档的游戏
  理论上可能撞上这个窗口，但窗口是微秒级；而「换掉 inode」对跑着的游戏是**必然**失效 ——
  两害相权取前者。
* `gs_name_is_temp()` 收进 `.old_`：`old/` 建在存档目录的父目录下，正常在同一次调用里
  就 `rmtree` 了，但那一下要是崩了，它就一直躺在 `/user/home/<uid>/savedata_prospero`
  下面 —— 原来只有 `gs_sweep_temp` 能收拾它，而它只认 `.tmp_` / `.rst_`。
  一小时新鲜度门槛（`GS_TMP_STALE_SEC`）保证不会误删正在用的那一份。
* **三条路一起受益**：远程拉取、上传存档、快照还原共用这个函数（这也是当初把它们
  合到一起的理由 —— 各写一份的话，早晚有一边忘了回滚）。

### ② 回归守卫（宿主 + 真机，两个判据都盯 `st_ino`）

* **宿主**（`test/game_saves_test.c` 新增 5 条，142 → **147**）：上传与快照还原两条路上，
  记录目标存档目录的 `st_ino`，操作后必须**一模一样**；外加「父目录不留
  `.old_` / `.tmp_` / `.rst_` 半成品」。新增 `ino_of_dir()` / `count_temp_in()` 两个辅助。
* **变异对照**（`test/mutation_check_saves_inode.sh`，新脚本）：把 `gs_swap_dir` 换回
  `rename(target, old)` 旧写法 → **恰好 2 条红，就是那两条 inode 断言**（上传 + 还原），
  没有连带红。这条对照是必须做的 —— 宿主上如果只断言「内容对不对」，旧写法**照样全绿**
  （内容确实换对了，只是换的是另一个 inode），不把修复掐掉就不知道守卫是不是空的。
* **真机**（`_ftp_probe/verify149_inode.py`）：拉取前后用 FTP `MLSD` 的 `unique` 字段
  比存档目录的 inode，必须相等。
* 一个踩到的坑记一笔：变异脚本第一版**没造夹具**，于是 `game_saves` 全部用例因为
  「存档根都找不到」一起变红 —— 70 条假阳性，看起来像「变异成功」，其实是脚本坏了。
  现在脚本自己先 `make_game_fixture.py`，并且**只认那两条**（红的总数 > 4 就当脚本坏了）。
  另一个坑：判定用的 `grep -c` 在中文输出上被当成二进制、只数到 1 次，得加 `-a`。

**验证**

* 宿主全套 `bash test/run_host_tests.sh` → `==== overall: PASS ====`
  （`zip_io` 37/0、`media_scan` 178/0、`game_saves` **147/0**、`save_remote` 76/0、
  `game_auto` 82/0、`game_mgr` 100/0、`check_post_routes` 13 == 13、
  `check_stack_frames` 全部函数栈帧 ≤ 32768 B）。
* 无新增 POST 路由，`gr_post_limit()` 不用动。

### ③ 顺带查清的一件事（`.148` 那条链的补记）

`/game_saves` 里每个存档的 `mtime` 取的是**存档目录**的 `lstat`
（`game_saves_list_json` 里的 `roots[i].path`），**不是文件本身的 mtime**。
所以拉取后它显示的是「目录刚被改过的时间」= 拉取时刻，而真正被 `utimes()` 的**文件**
mtime 才是包里的时间 —— 两者会不一致。**排查日期类问题时别信面板那个 mtime，
去 FTP 打 `MDTM` 看文件本身。**

---



> **一轮真机数据完整性事故的根因修复**。用户口径：「你再帮我看一下，远程拉取回来的，
> 显示拉取成功，但是存档不对。」+「7 点多中的时候，还是好用的」（= 明确指认为回归）。
>
> 查下来**存档内容一个字都没错**（整条链上 sha256 全程 `5b531b74…`），错的是**存档文件的
> mtime**：每跑一轮「推送 → 拉取」，主机上那份存档的日期就往回退**一个月**，而且是**累积**的
> （真机实测 10-08 → 09-08 → 08-08）。用户看到的「存档不对」就是文件管理器 / 系统里那个日期。
>
> 根因在 `src/zip_io.c` 的 `zx_dos_time()`：把 `struct tm` 里 **0-based** 的 `tm_mon`
> 直接当成 DOS 日期字段里 **1-based** 的月份写进去，而读侧（`zip_extract`）按标准做
> `tm_mon = 字段 - 1` —— **写读不互逆**。存档拉取的落盘链恰好是「解包 + 把包内 mtime
> `utimes()` 到存档文件上」，于是错月份顺着这条链一路印到主机文件系统上。
>
> 顺带修掉第二条（`src/game_saves.c`）：`/game_save_remote_info` **恒返回 0 长度响应**
> （HTTP 200、body 空），前端逐用户远程信息**一次都没渲染出来过**。
>
> 三处改动：① `src/zip_io.c` 月份 `+1`（含兜底日期改合法）② `src/game_saves.c`
> `game_saves_remote_info()` 返回值语义对齐头文件契约 ③ `src/game_auto.c` 补两行
> 「本该动作、却被开关挡住」的留痕日志（上一轮用户反馈「关闭（游戏）时的推送没有 debug」的
> 正解 —— 推送 debug 是齐的，那台机器全局配置 `on_exit:false`，**没动作这件事本身没有痕迹**）。
> 新增回归守卫 `test/zip_io_test.c`（37 checks，**不依赖 python3**，所以永远不会被 SKIP）。

### ① `zx_dos_time()` 的 DOS 月份 off-by-one（后端 · 头号根因）

**改了什么**（`src/zip_io.c`）

```c
-    *dd = (uint16_t)(((y - 1980) << 9) | (tmv.tm_mon << 5) | tmv.tm_mday);
+    *dd = (uint16_t)(((y - 1980) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
```

* DOS 日期字段的位布局是 `year = ((dd>>9)&0x7F)+1980`、`month = (dd>>5)&0x0F`（**1~12**）、
  `day = dd & 0x1F`；DOS 时间字段 `hour = (dt>>11)&0x1F`、`min = (dt>>5)&0x3F`、
  `sec = (dt&0x1F)*2`（2 秒粒度，这部分原来就是对的）。
* 原来的写法：10 月（`tm_mon=9`）在包里变成月份字段 9 = **9 月**；11 月写成 10 月；
  1 月（`tm_mon=0`）干脆写成「**0 月**」。
* **读侧不动**（`zip_extract()` 的 `tm_mon = 字段 - 1` 是标准做法，是对的）。要改的是写侧。
  这条注释写进了代码里，防止以后有人「修反」。
* **兜底日期也一起修**：原来是 `*dd = 0x21`（月份字段 0、日 1），读回来 `tm_mon = -1`，
  `mktime` 会把它规整成**上一年 12 月**。改成 `1980-01-01` 的合法编码。

**为什么「只错日期」却让人以为「存档不对」**

存档拉取的完整链是 `sr_finish_pull()` → `game_saves_import_file()` → `gs_upload_buf()`
→ `zip_extract(gs_up_cb)`，而 `gs_up_cb()` 里有一句 `gs_up_set_mtime()`，会把**包内条目的
mtime `utimes()` 到刚解出来的存档文件上**（`src/game_saves.c:1936-1972`）。所以包里的错月份
不是「显示问题」，是**真的写进了主机文件系统**，一轮退一个月、逐轮累积。

**附带伤害**：媒体包（磁贴图标 / 背景 / 视频）走的也是同一个编码器（`md_dos_time` 早已并进
`zip_io.c`），所以**导出的媒体包里日期也是错月份**，对 Windows 资源管理器 / 7-Zip / Python
`zipfile` 一视同仁地错。这次一起修好。

**验证 ①（真机受控实验，内容刻意保持不变）**

| 环节 | 远端 MDTM | 包内 zip 月份字段 | 内容 sha256 前 8 |
|---|---|---|---|
| 基线 | `20260908134158`（9 月 8 日）| 9 | `5b531b74` |
| 推送后 | `20260908134158`（**不变**）| **8** ← 写侧把 `tm_mon`(8=9月) 当月份 | `5b531b74` |
| 拉取后 | `20260808134158` ← **又掉一个月** | — | `5b531b74` |

* **内容 sha256 全程一字不差** —— 这就是「显示成功、但看着不对」的完整解释。
* 真机现场修回：把远端 `ps5save/PPSA27836/1a1d3627.zip` 的**本地头（+12）与中央目录（+14）
  两个日期字段**从 8 月补到 10 月，再拉一次 → 主机 live 存档
  `MDTM 20260808134158 → 20261008134158`（真值），sha256 仍是 `5b531b74…`。
  备份留在 `_ftp_probe/evidence147/remote_before_fixdate.zip`。
  ⚠️ 修远端 zip 时踩到的坑记一笔：**中央目录里 `date` 在 +14、`time` 在 +12**（跟本地头
  的 10/12 不是一个偏移），第一次改错字段被自己的 `assert` 拦住没写进去。
  另外真机 FTPSRV **不支持改 mtime**（`MFMT` / `SITE UTIME` 全回 `502 Command not recognized`），
  所以只能改包内字段再拉一次 —— 这也是为什么要走这条路而不是直接 `MFMT`。

**变异对照（mutation check）**：把 `+ 1` 用 `sed` 抹掉重跑 → **15 条判据变红**，
且报错数字正好是 `-2592000 秒`（= 30 天）；1 月那组报 `2025-12 vs 2026-01`（因为旧代码
写出「0 月」）。修复后 **37/37 全过**。这条对照证明测试真的咬得住这个 bug。

### ② `/game_save_remote_info` 恒 0 长度响应（后端）

**改了什么**（`src/game_saves.c`）

* `game_saves_remote_info()` 的 4 条成功路径**全部** `return 0` → 改成 `return n`
  （写进 buf 的字节数）；参数不合法从「写个 `{"error":...}` 再回 0」改成**直接回 `-1`**。
* `include/game_saves.h` 的契约注释（「返回写入长度，<0 出错」）**本来就是这样写的**，
  实现向头文件对齐；`src/http_server.c` 的 `ROUTE_GAME_SAVE_REMOTE_INFO` 分支
  （`rn < 0` → 400，否则当长度建响应）**保持不动**。
* 现象：`MHD_create_response_from_buffer((size_t)rn, …)` 拿到 `rn == 0` → 建出**0 长度响应**，
  **HTTP 200 但 body 是空的**。前端 `getRemoteSaveInfo()` → `request()` 拿到空串，
  `JSON.parse` 直接抛，「远程位置」卡里的逐用户远程信息**从 `.145` 上线起一次都没渲染出来过**。
* 之所以藏了三个版本：**日志里 `GET /game_save_remote_info` 全是 200**，从状态码上完全看不出
  问题；前端那侧又是 `try/catch` 逐用户吞掉（一个失败不影响别的用户）。
* 顺带确认 mock 的盲区：`frontend/mock-server.js:1083` 的 `/game_save_remote_info`
  **永远返回一份存在的远程存档**（`{configured:true,exists:true,mode:'smb',…}`，且不吃
  `globalArchive()`），所以这个 bug 在 mock 里**结构性地看不出来**。

### ③ 「本该动作、却被开关挡住」也要留痕（后端 · 上一轮用户反馈的正解）

**改了什么**（`src/game_auto.c`，`ga_tick()` 里 `game_auto_plan()` 之后）

* 两侧**各自独立**判断（不是 `else-if`）：`prev[0] && (!pe || !px)` → 记「已退出 · 跳过推送：
  自动存档总开关关着 / 「退出游戏时回传」关着」；`cur[0] && (!ce || !cl)` → 记「已启动 ·
  跳过拉取：…」。条件与 `game_auto_plan()` 里那两条一一对应，所以**动作真的发生时这里一声不响**，
  不会出现「说要推、结果也推了」的矛盾。
* **只写日志、不弹屏幕通知**：通知是给「真的动了」准备的，开关关着时每启动/退出一次就弹一条，
  很快变成没人看的噪声。要看就来面板「日志」页。
* 起因：用户说「只有拉取有 debug，关闭（游戏）时的推送没有 debug 信息」。真机日志显示
  **推送自己的 debug 是齐的**（手动推送四条都在），真因是那台机器的全局配置里
  `on_exit: false` —— 引擎按配置办事，没触发自然没有 debug。**问题不在推送，在于「没动作」
  这件事没有任何痕迹**：日志里只有一串「拉取」，用户没法区分「游戏压根没退出过」和
  「退出回传那个开关关着」，而这两件事该做的事完全不同。

### ④ 新增回归守卫 `test/zip_io_test.c`（不需要 python3）

* 纯 C，**不依赖 python3**，所以**永远不会被 SKIP** —— 数据完整性守卫不该被环境跳过。
* 五段：① 10 月主用例（真机观测到的月份）② 1 / 2 / 11 / 12 月边界
  ③ **负向对照**（包内月份字段**必须不等于** `tm_mon`）④ 多条目（两条各自日期互不串）
  ⑤ 1980 年份下限。
* 直接读**本地头偏移 12 的原始 DOS 日期位**（不解包），这样测的是编码器本身而不是解码器：
  ```c
  static uint16_t lh_dos_date(const char *path) { /* 读 30 字节头，校验 PK\003\004，取 [12]|[13]<<8 */ }
  ```
* 负向对照那条是防「修反」的：
  ```c
  CHECK(field != want.tm_mon,
        "neg: 月份字段又变回 tm_mon(%d) 了 —— 就是 `.148` 修掉的 off-by-one", want.tm_mon);
  ```
* 已接进 `test/run_host_tests.sh`（放在 `check_stack_frames` 之后、`slot_resolve` 之前）：
  ```bash
  run_one zip_io src/zip_io.c \
      -Iinclude -Isrc/psaccounts/third_party \
      src/inflate.c src/game_job.c src/json_helpers.c src/utils.c
  ```

### ⑤ 清账

* 仓库根那 4 个杂散的 `.su`（`clang -fstack-usage` 的残渣，从仓库根手跑量栈帧留下的）
  清掉，并给 `.gitignore` 补三条：`*.su`、`delivery-*/`、`pldmanager-delivery-*.zip`。
  （`tools/check_stack_frames.sh` 本身用的是 `mktemp -d`，不会往仓库里落东西。）

**验证（宿主全套）**

* `bash test/run_host_tests.sh` → `==== overall: PASS ====`。各关卡：
  `zip_io: 37/0`（**新增**）、`media_scan: 178/0`、`game_saves: 142/0`、`save_remote: 76/0`、
  `game_auto: 82/0`、`game_mgr: 100/0`、`param_rename: RESULT: PASS`、
  `check_post_routes` **13 == 13**、`check_stack_frames` 全部函数栈帧 ≤ 32768 B。
* 三处改动**没有新增 POST 路由**，所以 `gr_post_limit()` 不用动（守卫 13 == 13 也是这么报的）。

⚠️ **给用户的提醒**：真机上已经被拉歪的存档日期，**必须重新推送一次**才会带上正确日期 ——
远端 zip 里现在存的还是错的月份，光升包不重推，拉回来照样是旧月份。

---



> 四件事：① 「存档管理」弹窗第一行三栏重排 + **三栏等高**（本地快照一行没碰）；
> ② 远程推 / 拉两个动作的**开始与结束各弹一条 PS5 屏幕通知 + 各打一条日志**；
> ③ 撤掉「自动存档」卡里那颗「远程游戏」徽标（顺带修掉 1280–1660px 窄窗的字行问题）；
> ④ **出包前清账**：mock 谎报全局配置（改回 `null`）+ `game_saves` 关卡 `sr_stat` 链接不过
> （补替身）。④ 的两条都是 `.146` 那批**未提交 WIP** 留下的，不是本轮新引入。
>
> 用户口径：①「本地快照部分不用改。直接把第一行的内容改了就可以」（附一张手改的版式图）
> + 「自适应模式让这三个高度一致保持一样，不管哪个变高，另外两个同步变高」；
> ②「远程拉取和推送远程的时候，两个动作的开始和结束都要 debug：到屏幕信息」，
> 并指明「屏幕信息**就跟这个一样**」= `main.c` 的 `pldmgr_notify("NayePlmTools v%s Ready!…")`，
> 即 **PS5 系统通知弹窗**（不是面板的「日志」页）；
> ③ 贴图指认那颗蓝色描边徽标 →「把这个标签去掉」；④「出包吧」。
>
> **出包**：提交 `032b505` → `rm -rf frontend/dist` → `make frontend-build`（带 autocrlf env，
> 打印的标题行 `NayePlmTools v0.5.2.147 by naye (032b505, built at 2026-10-08 13:33:41 UTC)`，
> **短哈希是真哈希、不是 DEV**）→ `build_tiles.sh`。
> 两包各 **10,999,176 字节**，titleId 自检 NAYE00001（-me 媒体栏）/ NAYE00004（-ga 游戏栏）通过；
> 内嵌网页段与 `frontend/dist/index.html`（943,146 B）**逐字节一致**（偏移 +2,186,864）；
> 上一版 `0.5.2.146` 残留 0 处。核对脚本 `~/.workbuddy-ai/binaries/node/workspace/pkg_147_verify.sh`
> （六条全过）。⚠️ **未推真机**（连不上 `192.168.1.214`）。

### ① 存档弹窗第一行三栏重排（前端）

* **第一行第三栏由「远程存档」换成「存档列表」**：`存档列表` 从「满宽另起一行」提到并排的第三栏；
  它原来是满宽时内容贴左、按钮贴右、中间一大段空白。并排后与远程信息挨在同一屏。
* **「远程存档」并进「远程位置」卡**：卡内新增一个小标题「远程存档」+ 逐用户远程信息行，
  数据来源不变（`/game_save_remote_info` 的远端真实元数据）。它和「存档列表」是**同一批用户的
  两份列表**，各占一张 1/3 宽的卡反而看不出对应关系。
* **三栏等高**：去掉 grid 上的 `items-start`（默认 `items-stretch`）—— 三张卡跟着**最高**那张
  一起长、底边对齐；谁变高另外两个同步变高。`.146` 的 `items-start` 是各长各的、三栏参差。
* **本地快照（⑤）完全没动**：仍是单独一行满宽铺开（也不参与上面那条等高）。
* 纯前端改动，只碰 `frontend/src/components/game/SaveManagerModal.jsx` 一个文件（无新 i18n 键、
  无新 POST 路由、无后端改动）。

**验证 ①**
- 1920×1080 量尺：三栏各 **503×266px**（左 353 / 868 / 1384，**高完全一致**），
  本地快照 **1534px 满宽**（不参与等高）；
  `存档列表` 每行 `scrollWidth - clientWidth = 0`（无横向溢出）、元信息行**单行不折行**、3 个操作按钮齐全。
- 出图 `D:/pldmanager/.shots/saves147/`（`panel.png` / `full.png` / `panel_w1440|1366|1280|1100.png`），
  脚本 `~/.workbuddy-ai/binaries/node/workspace/shot_saves_147.mjs`。
- 回归：`game112` **270 / 272**、`nav112` **20 / 20**。
  两条 FAIL 已证明**与本次改动无关**（是工作树里 `saveConfig.js` 那份未提交 WIP 造成的）——
  用探针在**暂存版（`.146`）**上单独跑那两条判据，结果完全一样：
  ① 远程摘要现在渲染成 `SMB · 192.168.1.10/ps5save`，而断言要求 `/FTP/`；
  ② 快照间隔是 `60` 分钟，而断言要求 `/30/`。两处都是 `saveConfig.js` 的默认值变了。

### ② 远程推 / 拉的「开始 / 结束」debug 行（后端）

**改了什么**（`src/game_auto.c`）

* `ga_do()` 拆成 **包装层 + `ga_do_inner()`**：包装层只做三件事 —— 读一次配置、发「开始」、
  调 inner、发「结束」。`inner` 里原来的汇总日志删掉（被「结束」那条取代）。
* **两个通路一起打**，因为它们解决的不是同一件事：
  * `pldmgr_notify()` —— **屏幕上的 PS5 系统通知弹窗**（用户要的「屏幕信息」，同 `main.c`
    的 `Ready!`）。当场看得见，不用切页面；
  * `pldmgr_log()` —— **事后查得到**（面板「日志」页的环形缓冲 + `pldmgr.log` 留档，
    通知一划走、界面定住、断电后照样有）。`pldmgr_notify` 内部还会补一条 `[Notify] …`
    进日志 —— 那正好是「通知确实发出去了」的证据。

屏幕上那两条（通知正文，3 行）：

```
远程推送 开始
PPSA01234 · 触发=手动 · 范围=全部用户
目标=SMB · 192.168.1.107/pc/share

远程推送 结束
PPSA01234 · 成功 2 / 跳过 0 / 失败 0
15.0 MB · 用时 812 ms
```

日志里那两条（字段顺序**按重要性倒着放**：环形缓冲每行只有 `MAX_LOG_LINE_LEN` = **256** 字节，
「目标」放最后 → 长 host/share/sub 最多把自己截掉，不会把「是推还是拉 / 谁触发 / 范围」带走）：

```
[PLDMGR] saves: 远程推送 开始 · PPSA01234 · 触发=手动 · 范围=全部用户 · 目标=SMB · 192.168.1.107/pc/share
[PLDMGR] saves: 远程推送 结束 · PPSA01234 · 成功 2 / 跳过 0 / 失败 0 · 15.0 MB · 用时 812 ms
```

* 新增 `ga_target_desc()`：日志用的一句话目标描述，**绝不含密码**，口径与前端
  `utils/saveConfig.js` 的 `remoteSummary()` 逐字一致（界面写 SMB，日志里也写 SMB）。
* **手动与自动两条路都覆盖**：`触发=手动`（`POST /game_save_sync`）/
  `触发=自动（启动游戏时）` / `触发=自动（退出游戏时）`（守护线程）。
* 计时用 `CLOCK_MONOTONIC`（同 `game_media.c`）。手动点一下正常几百毫秒到几秒；
  真卡在网络、或卡在 `g_sync_mu` 上等另一次同步时，「用时」那一项自己说话。
* 推送的结束那条**只报字节数、不报文件数** —— `sr_push` 不填 `files`，照着拉取那样打会变成
  「0 个文件 / 2648 字节」这种自相矛盾的一行，排查时反而带偏。
* 失败原因在通知里**另起一行**打：通知是给人扫一眼的，混在计数那一行里最容易被当成背景噪声划过去。

**为什么「开始」放在包装层而不是 inner 里**：inner 有三条**提前返回**的分支
（读不到配置 / 协议不支持 / 没有可同步的用户），把开始那条放进去必然出现「有结束没开始」
或「有开始没结束」—— 而用户点完按钮最想知道的恰恰是「它到底有没有开始跑」。
放包装层则天然成对，失败路径也照弹。这条已被用例钉住。

**验证 ②**

* 新增 `test/game_auto_test.c` 第 ⑥ 节 `t_log()`（**+37 条**）：`game_auto` 由 45 → **82 checks,
  0 failures**。两个桩都抓：`pldmgr_log`（原来只在 `SAVES_TEST_VERBOSE` 下打印、不落任何地方）
  **和 `pldmgr_notify`**（真机走 `sceKernelSendNotificationRequest`，宿主必须打桩 ——
  而它恰恰是本次的**主要产物**，桩而不抓 = 主要产物零覆盖）。钉四件事：
  ① 成功的一对、且「开始」在「结束」之前（日志与通知**各钉一遍**）；② 开始那条带触发来源 /
  目标 / 范围；③ 结束那条带计数与用时，且推送不报文件数、拉取报；④ **提前返回的失败路径
  也必须成对**（「协议不支持」那条 + 「读不到配置」那条）。
* 实际跑出来的（`SAVES_TEST_VERBOSE=1`，本机 HTTP 桩、真搬字节）通知正文：
  `远程推送 开始 / PPSA012345 · 触发=手动 · 范围=全部用户 / 目标=HTTP · http://127.0.0.1:42185/ps5save`
  → `远程推送 结束 / PPSA012345 · 成功 1 / 跳过 0 / 失败 0 / 2648 字节 · 用时 1 ms`；
  失败路径 `目标=webdav（未知协议）` → `结束 · 失败 1 · … · 当前 SDK 不支持 webdav 传输`。
* ⚠️ **真机未验**（本环境连不上 `192.168.1.214`）。`clock_gettime(CLOCK_MONOTONIC)` 在 PS5 侧
  已有先例（`game_media.c` / `log_server.c`），但「真机日志页里真的能看到这两行」还差一次推送确认。
* 守护线程那条路（`触发=自动（…）`）**宿主测不到** —— `game_auto_start_daemon` 先睡 20 秒，
  装探针也来不及。只做了静态覆盖（改动就是一处实参），真机验收时顺带看一眼。

### ③ 撤掉「自动存档」卡里的「远程游戏」徽标（前端）

用户贴图指认那颗蓝色描边徽标（`Chip tone="info"` → 「远程游戏」），要求去掉。

* **徽标本身删掉**：`.146` 加它的用意是「让用户一眼知道这款游戏接了远程位置」，
  但同一张卡里已经有「远程位置」板块（第二栏）写明了 `SMB · 192.168.1.10/ps5save`，
  信息重复；且它占着右半边、把三行只读状态挤到半宽。
* **连带撤掉为它让位的那层 `md:grid-cols-2`**：那层左右分栏（`.146`）唯一的作用就是给徽标
  腾右半边。徽标一走，分栏只剩副作用 —— 三行状态被压到半宽。撤掉后回到**满宽单栏**，
  三行文字起点不变（标签列仍是 `RO_COLS` 的 124px）。
* **顺带修掉了「遗留」里那条 1280–1660px 的窄窗问题**：值列从二十几像素恢复到正常宽度，
  「未启用」不再一个字一行。证据图 `panel_w1280.png` 重出后三行都是单行。
* 死键清理：`game.remote_game` / `game.remote_game_hint` 在 `zh-CN.json` / `en-US.json`
  两份语言包里一并删除（全仓已无引用，留着是死重量）。**其余 i18n 键一个没动。**
* 只碰 3 个文件：`SaveManagerModal.jsx` + 两份 `locales-overlay/*.json`。

**验证 ③**
- 量尺（1920×1080，重跑 `shot_saves_147.mjs`）：三栏仍各 **503×266px** 等高，本地快照 **1534×329** 满宽；
  `存档列表` 两行仍 `overflowX = 0`、元信息单行、3 按钮齐全。
- 两份语言包 `JSON.parse` 通过；`grep -rn "remote_game" frontend/src/` **零命中**。
- `Chip` 的 import **保留**（同卡里还有「游戏运行中」和「已启用/未启用」两颗在用）。
- 无测试断言这颗徽标（`game112.mjs` 只断言三行状态文案），删它不动任何用例。

### ④ 出包前清账：mock 谎报全局配置 + `game_saves` 关卡链接不过（两处既有 WIP 遗留）

出包前把两件挡路的事收了。**都不是本轮新引入的，是 `.146` 那批未提交 WIP 留下的。**

**(a) mock 的 `globalArchive()` 在「没配过」时返回了一份假的全局 SMB 配置 —— 已改回 `null`。**
`.146` 加它是「方便预览『远程游戏』区块」，但它**违背了 `.145` 定的核心契约**：全局生不生效，
判据是「那份配置**写过没有**」，没写过就必须回落到 per-game。假默认一在，`autoFor()` 就把全局
盖在 per-game 上，mock 里**永远走不到回落分支** —— 于是 `game112` 里两条断言变成假红：

| 断言 | 期望 | 假默认下实际渲染 |
|---|---|---|
| 远程位置是只读摘要：来自本游戏已存 | `FTP · 192.168.1.10:2121/ps5save/<TID>` | `SMB · 192.168.1.10/ps5save`（假全局的） |
| 只读状态行显示的是生效值 | 间隔 **30** 分钟（后端 `GAME_SAVES_SNAP_INTERVAL_DEF = 1800`） | **60** 分钟（假全局里的 3600 秒） |

改回 `null` 后 **`game112` 272/272，两条断言一行没改** —— 这反过来证明**是 mock 在说谎、
不是代码有问题**（若真是契约变了，改 mock 是修不好的）。而且它并不必要：「远程游戏」区块的数据
来自 `/game_save_remote_info`（那份在 mock 里是硬编码的，不吃 `globalArchive()`），per-game 默认
本身也带一份配好的远程，该预览的一样看得到。

**(b) `game_saves` 关卡 `undefined reference to 'sr_stat'` —— 已用替身补上。**
`.146` 给 `game_saves.c` 加了 `game_saves_remote_info()`（调 `sr_stat()`，真身在 `save_remote.c`），
而 `game_saves` 是**纯文件系统**那一关，编译行刻意不带 `save_remote.c` / curl → 链接不过，
`run_host_tests.sh` 的 `overall` 恒 FAIL。

新增 `test/stub_sr_stat.c`（+ 加进该关卡编译行）。**为什么不选「把 `save_remote.c` + `-lcurl`
加进这一关」**：① 这一关现在**只依赖 python3**，链上 curl 后会变成「没有 libcurl 的机器整段
SKIPPED」，白丢一层覆盖；② 本关**没有任何用例碰远程元数据**（它测本地存档的枚举 / 打包 / 快照），
真身进来一行也跑不到；③ 同族的 `test/stub_archive_global.c` 就是这么处理
`config_read_archive_global` 的，口径一致。
替身**故意不模拟任何成功路径**（一律回 -1 + 人话，并往 stderr 打一行「替身被调用了」）——
平时零输出，哪天有人在本关加了远程用例会立刻看见，而不是拿到一份看着正常的假数据。

**验证 ④**
- `run_host_tests.sh` **`overall: PASS`**（`.146` 起第一次）：`game_saves` **142/0**、
  `save_remote` **76/0**、`game_auto` **82/0**、`media_scan` 178/0、`game_mgr` 100/0、
  `check_post_routes` 13==13、`check_stack_frames` 全部 ≤32768 B。
- `game112` **272 / 272**（`.145` 以来的首次全绿）。

**遗留**
- ~~视口 1280–1660px 区间「未启用」一个字一行~~ —— **已被 ③ 顺带修掉**（撤掉内层
  `md:grid-cols-2` 后三行回到满宽）。重出的 `panel_w1280.png` 已确认三行都是单行。
- ~~`run_host_tests.sh` 的 `overall` FAIL（`sr_stat` 链接不过）~~ —— **已被 ④(b) 修掉**。
- ⚠️ **真机未验**（本环境连不上 `192.168.1.214`）：通知弹窗、`CLOCK_MONOTONIC`、
  守护线程那条路（`game_auto_start_daemon` 先睡 20 秒，宿主测不到）都只做了静态覆盖。

---

## v0.5.2.146 (CN-FORK)

> **版本号按构建序号规则 +1（接 .145）。本次为增量改动 + 1 个编译修复，全部已重新 `make` 出 ELF。**
>
> 用户口径：「为什么没按要求升版本」——`include/pldmgr.h` 的 `MENU_VERSION` 注释规定
> 「构建序号…每次调整构建自动 +1」，上次多次 `make` 出 ELF 却一直停在 145，未升号，已修正。

* **SMB 配置项优化**：补「端口」字段、调整字段顺序（host→port→user→password→share→sub）、修正后端落盘。
* **FTP / HTTP 按钮暂时禁用**：协议选择器仅保留 SMB，FTP/HTTP 悬停提示「后续开发，暂不可用」；默认协议改为 `smb`。
* **远程游戏标签显示真实远程信息（方案 A）**：推送时写 `<uid>.meta.json`；新增只读接口 `GET /game_save_remote_info`；前端逐用户展示 用户·PS5·大小·文件数·时间。FTP/HTTP 的 `sr_stat` 仍为 stub（属「暂不可用」范围）。
* **存档管理弹窗三板块并排**：「自动存档」「远程位置」「远程存档」在宽屏上排成一行，各占 1/3；存档列表与本地快照依次满宽铺开。
* **编译修复**：`include/save_remote.h` 补 `#include <time.h>`（`sr_stat` 声明用到 `time_t`；`save_remote.c` 先 include 头、后 include time.h，头需自包含）。
* ELF 产物：`NayePlmTools_0.5.2.146.elf`（已构建校验）。

---

## v0.5.2.145 (CN-FORK)

> **存档配置彻底收口：自动存档与本地快照也搬进「设置 → 存档配置」，三件事全部全局生效；
> 单游戏弹窗里一个可编辑控件都不剩。**
>
> 用户口径：「你前端 按上一版我改那个安排 … 这个保留」+「这两块都放在存档配置里，要全局生效」
> （附「自动存档」「本地快照」两块截图）。

> ### 为什么要再搬一次
>
> `.144` 只把**远程位置**搬到了全局，理由写的是「开关与时机是每个游戏各自不同的策略」。
> 用户看完实际界面后要的是**全都要全局**：同一台主机上的存档备份策略本来就该一致，
> 每款游戏各配一遍同样是重复劳动。
> 于是 `.145` 把「启用自动存档 + 启动时拉取 + 退出时回传」和「启用本地快照 + 间隔 + 保留份数」
> 一起搬进设置页。**一个控件只有一个家**这条原则不变，只是「家」从「每个游戏」换成了「全局」。

> ### 关键：这次的「全局生效」是后端真的读它，不只是 UI 显示
>
> `.144` 的「全局生效」是前端在保存时把全局的远程段**合进 per-game 请求** —— 用户不进那个
> 游戏的弹窗点一次保存，就还是不生效。`.145` 改成**引擎直接读全局**：
> - **`config.c` / `http_server.c`**：新增全局存档配置的落盘与读取。`/set_config` 里
>   `ARCHIVE_GLOBAL_CONFIG` 用 `cJSON` 取值（**不能**用老的 `strstr` 找引号：这个值是内容
>   为 JSON 的 JSON **字符串**，里面全是 `\"`，手扫会在第一个内层引号处截断），落到
>   **单独一份文件** `ARCHIVE_GLOBAL_PATH`（`/data/nayeplmtools/archive_global.json`）——
>   `nayeplmtools_config.txt` 是 `key=value` 行式、行缓冲只有 256 B，六百多字节的 JSON 写进去
>   会被截断成半个（静默失效）。`/get_config` 把它原样发出去（前端要用它渲染只读状态）。
> - **`game_auto.c`**：抽出 `ga_fill_cfg(json, out)`，`ga_load_cfg()` 改成**全局优先** ——
>   全局文件在就用它，没有才回落到 per-game，再没有才用内置默认。
> - **`game_saves.c`**：`gs_snap_cfg()` 同样全局优先；`gs_snap_enabled_games()` 在全局开启时
>   **改成扫存档目录枚举游戏**（新增 `gs_all_tids()`）—— 因为「要快照的游戏」里绝大多数
>   在 per-game 配置里**根本没有条目**，照旧从配置枚举的话全局开了也扫不到任何一款。
>
> 优先级统一为「**全局写过 → 全局说了算；没写过 → per-game（老装机行为不变）**」，
> 判据是「那份配置写过没有」而不是「填了几个字段」：后者会让「用户把主机清空保存（本意是
> 别同步了）」悄悄回落到这个游戏很久以前存的旧主机上继续推。
> 这条规则前端（`saveConfig.js` 的 `hasGlobalArchive()` / `resolveAutoForGame()`）与后端
> 用的是同一个判据，所以**界面显示什么，引擎就按什么跑**。

> ### 前端
>
> - **`ArchiveConfigModal.jsx`** 新增「自动存档」与「本地快照」两块，现在是三块并存：
>   远程位置（+测试连接）/ 自动存档 / 本地快照，各带「保存设置」。
> - **`SaveManagerModal.jsx` 再收敛**：删掉自动存档总开关与两个时机开关、快照的
>   启用/间隔/保留输入框、以及所有「保存设置」；改为**只读状态行**（`GridRow`）
>   显示生效值，保留「立即拉取 / 立即推送 / 立即备份」、`auto_state` 状态行、存档列表与快照列表。
>   现在这个弹窗里**没有任何 switch、没有任何输入框**。
> - **`parts.jsx`**：`TimingRow` 从 `SaveManagerModal` 提到共用零件表（两个弹窗都要用）。
> - **`saveConfig.js`**：新增 `hasGlobalArchive(config)` / `resolveAutoForGame(config, perGameAuto)`；
>   `resolveRemoteForGame` 改成同一个判据（原先按「全局填了目标没有」判，两种口径会打架）。
> - i18n：新增 `settings.auto_enable_hint_global` / `settings.snap_enable_hint_global` /
>   `game.snap_global_hint`，改写 `settings.archive_config_hint` / `game.remote_global_hint`，
>   删掉已无用的 `settings.archive_pergame_hint`。

> ### mock
>
> - `autoFor()` 拆成 `pgAutoFor()`（per-game 原始，**写入路径必须用它**）+ `autoFor()`
>   （读路径，全局优先合并）—— 合并后返回的是副本，往副本上写会静默丢。
> - `globalArchive()` 读 `mockConfig.ARCHIVE_GLOBAL_CONFIG`，与后端
>   `config_read_archive_global()` 同语义（空串 / 坏 JSON 都当「没配过」）。

> ### 测试
>
> - 新增 `test/stub_archive_global.c`：宿主测试里全局配置的替身（默认「没有」，即老行为），
>   并提供 `stub_archive_global_set/clear` 供用例测「全局优先」。`run_host_tests.sh` 里
>   `game_saves` / `save_remote` / `game_auto` 三条补上它（否则 link 不过）。
> - 回归：第 5 节加**反面断言**（0 个 switch / 无「保存设置」/ 无任何输入框）、5b 改为验只读
>   状态行、5c 去掉「保存设置可用」、**5d 重写**（全局三项同时对单游戏生效，且分「显示面」与
>   「引擎面」两面 —— 只看显示面的话「界面对了但引擎没跟上」照样全绿）、**新增 5e**
>   （设置页存档配置弹窗本身：三块都在 + 保存真写回 `ARCHIVE_GLOBAL_CONFIG` + 重开仍在）。

## v0.5.2.144 (CN-FORK)

> **存档配置搬家：远程位置改到「设置」里配一次、全局生效；单个游戏只留「拉取 / 推送」。**
>
> 用户口径：「我刚刚把共享设置的功能在设置里增加了，把配置功能迁移过去，配置一个全局生效。
> 单个游戏包开启拉取和推送按钮。」

> ### 为什么要搬
>
> `.142` 之前，**每一款游戏**的「存档管理」里都有一整块远程位置配置（协议三选一 +
> host / 端口 / 共享名 / 子路径 / 账号 / 密码）。而现实里**所有游戏用的是同一台 NAS /
> 同一个 FTP** —— 于是同一份主机地址要在几十个游戏里各填一遍，改一次就要改几十处。
> 「远程位置」本质上是**跨游戏共享的那一份配置**，就该有一个全局的家。
>
> 相对的，「自动存档开关 / 启动拉取 / 退出回传 / 本地快照间隔」是**每个游戏各自不同**的
> 策略（有的游戏想每次退出都传，有的不想），仍归各自的「存档管理」。
> **一个控件只有一个家** —— 这就是这次搬家的原则。

> ### 前端
>
> - **新增 `frontend/src/utils/saveConfig.js`**：把 `MODES` / `MODE_FIELDS` / `FIELD_LABEL` /
>   `EMPTY_CFG` / `toLocalCfg` / `toRemoteCfg` 从 `SaveManagerModal` 里抽出来，供两个弹窗共用；
>   新增 `GLOBAL_KEY` / `loadGlobalCfg` / `normalizeCfg` / `isRemoteConfigured` / `remoteOf` /
>   `resolveRemoteForGame` / `remoteSummary`。
> - **新增 `frontend/src/components/views/ArchiveConfigModal.jsx`**：设置页「存档配置」弹窗，
>   管**远程位置**（协议 + 字段 + 测试连接），存主配置键 `ARCHIVE_GLOBAL_CONFIG`（JSON 串）。
> - **`SaveManagerModal.jsx` 收敛**：删掉远程位置那整块编辑 UI（Segmented / 字段 / 测试连接 /
>   密码框），只留 ① 自动存档总开关 + 两个时机；② 立即拉取 / 立即推送 + `auto_state` 状态行；
>   ③ 本地快照。远程位置改成**只读摘要**：优先全局，否则回退到本游戏已存的那一段，并标注
>   来源（`（来自全局设置）` / `（本游戏已配置）`）。
> - **保存时把解析出的远程段合进请求**（`resolveRemoteForGame`）：全局配了就按全局存，
>   这才让「全局生效」不只是显示、而是真的落到 `POST /game_save_auto` 上。
> - **`config` 逐层传下去**（`App → GameManageView → GameSection → SaveManagerModal`），
>   单游戏弹窗要读 `config.ARCHIVE_GLOBAL_CONFIG`；`App` 进 `game_manage` 视图时补一次
>   `refreshConfig()`（全局配置若是被别的途径改的，进页就能拿到最新的）。
> - i18n：新增 `game.remote_location` / `game.remote_from_global` / `game.remote_from_game` /
>   `game.remote_unset` / `game.remote_global_hint` / `settings.archive_saved` /
>   `settings.archive_pergame_hint`（两份 overlay 都加）。
>
> ### mock
>
> - `/reset` 补清 `ARCHIVE_GLOBAL_CONFIG`（新增的 mock 状态必须一起清，否则上一节设的
>   全局远程会串到下一节的单游戏弹窗）。
>
> ### 回归
>
> `game112` 第 5 节按新形态重写（**断言旧 UI 已删干净**：没有「远程位置类型 / 测试连接 /
> 共享名 / 上传地址 / 访问令牌 / password 输入框），新增 **5d 节**验「全局配置对单游戏生效」
> （全局配 HTTP → 单游戏摘要显示它 + 标注来源 + 保存后 per-game `mode` 真的变成 `http`）。
> 5c（老配置 `mode=smb`）改为验证回退分支读对协议。第 10 节（英文包）同步跟上新词条。
> 结果：`game112` **249/249**、`nav112` 20/20、`acct111` 23/23、`avatar109` 19/19、
> `cropfix` 75/75、`pack125` 15/15。

## v0.5.2.143 (CN-FORK)

> **SMB 接过 `libsmb2`：.142 那句「SDK 不支持 SMB」被推翻，SMB 成为能用的一档。**
>
> `.142` 的推理是「SDK 那份 `libcurl` 没编 `smb` → SMB 做不了」。**错在把「libcurl 这条路
> 不通」当成了「PS5 做不了」**：PS5 上 SMB 走的是 `libsmb2`（pacbrew 的
> `ps5-payload-libsmb2`，上游 `ps5-payload-dev/websrv` 就在用），已用本仓库同一套工具链
> 编成 `libsmb2.a` 并链进 ELF，代价 +约 241 KB。

> ### 后端
>
> - **`src/save_remote.c`（+459）**：新增 `mode=="smb"` 传输分支（`smb2_open(O_CREAT)` /
>   `smb2_write` / `smb2_fsync` / `smb2_unlink` / `smb2_mkdir` / `smb2_rename`），与
>   ftp / http 平权；`include/save_remote.h`（+50）补接口与文件头说明。
> - `src/game_auto.c` / `src/game_saves.c`：SMB 相关接线。
> - `Makefile`（+11）：链 `third_party/libsmb2/libsmb2.a`。
> - `third_party/libsmb2/`：`build_ps5.sh` + `include/` + `libsmb2-6.2.tar.gz` + `libsmb2.a`。
>
> ### 前端 / mock
>
> - `SaveManagerModal` 的 SMB 一档**解禁**（`.142` 的「看得见、点不动」作废），判据从
>   「那一档标了 disabled」改成「认不认得出这个协议名」（认不出才禁字段与保存）。
> - `mock-server.js`：`/game_save_sync` 对 `smb` 与其它两档一视同仁（不再回 400）。
>
> ### 测试
>
> - 宿主：`save_remote` 76 / `game_auto` 47（`test/save_remote_test.c` +60、
>   `test/game_auto_test.c` +14）；`tools/check_stack_frames.sh` / `test/run_host_tests.sh` 同步。
> - `game112` 5c 节验「老配置 `mode=smb`」这条路。

## v0.5.2.142 (CN-FORK)

> **存档三层计划的第 3 层：自动存档执行引擎 + 远程推拉（FTP / HTTP）。**
>
> 在这之前，`saves_auto.json` 里的 `enabled` / `on_launch` / `on_exit` 三个开关
> **只被读写、没有任何人在跑** —— 界面上的「启动游戏时拉取 / 退出游戏时回传」
> 是三个存下来就再也没人看的字段，远程那一段（smb/ftp/http）只用于「测试连接」。
> 这一版把它整条链补齐，并把手动推 / 拉一并放出来。

> ### 一个必须先说的硬约束：SDK 的 curl 没有 SMB
>
> `libcurl 8.18.0` 编译时带的协议是 `http/https/ftp/file/dict/imap/ldap/pop3/smtp`，
> **没有 `smb`**。而 `smb` 恰恰是老版本的默认值 —— 也就是说每个新用户默认配的是
> 一个「保存得下去、传不出去」的协议。
>
> ⚠️ **读到这里别顺手得出「PS5 做不了 SMB」的结论**（本条当时的措辞容易引向这个）。
> 2026-10-07 晚复核过一遍：**SMB 是能做的** —— `libsmb2` 是 SDK 生态的正式包，
> 已实测能用本仓库同一套工具链交叉编译并链进 payload（写路径符号齐，代价 +241 KB /
> 约 +2.2%）。所以准确的说法是**「当前这个构建没有编入 SMB」**。
> 证据与接入步骤写在 `include/save_remote.h` 文件头和 HANDOVER §6 那条里。
>
> 处理：默认值前后端 + mock 一起改成 `ftp`；UI 上 SMB 那一档做成
> **看得见、点不动、旁边写明原因**（而不是从选项表里删掉 —— 老用户存的就是
> `mode:"smb"`，选项没了会变成「一个都没选中」，用户根本不知道发生了什么）。

> ### 远端布局（推与拉必须逐字一致）
>
> ```
> <base>/<TID>/<uid>.zip
> ```
>
> - ftp 的 base = `ftp://<host>:<port><path>`；http 的 base = `<url>`。
> - **刻意不做「一个游戏一个文件」**：真机实测一个游戏常有两个用户各存一份
>   （PPSA01650）。一个用户一份，互不覆盖。
> - **文件名不带时间戳**，恒定 `<uid>.zip`、每次整份覆盖 —— 要历史版本有本地快照兜着。
> - 每个用户的候选集来自 `game_saves_uids()`（主机上存过这个游戏的 uid）。

> ### 拉取时的用户枚举：本地没了才最需要它
>
> `game_saves_uids()` 是靠「`<uid>/savedata_prospero/<TID>` 这个目录在不在」枚举的，
> 而**拉取要救的恰恰是本地那份已经没了的情况**（换机、误删、重装）—— 那时本地枚举
> 必然是 0 条，用它就永远拉不回来，成了一条死路。所以新增
> `game_saves_user_ids()`：本地枚举为空时退化成「主机上全部用户目录」，逐个去远端试
> `<uid>.zip`，远端没有的算**跳过**而不是失败。用户目录是主机账号，不会因为删了
> 游戏存档而消失。
>
> 代价是主机上用户多时会多几次 404 —— 一次局域网往返，和「救不回存档」不是一个量级。

> ### 引擎（`src/game_auto.c`）
>
> - **探测**用 `sceLncUtilGetAppIdOfRunningBigApp()`（返回 `0xffffffff` = 没有大应用在跑）
>   + `sceLncUtilGetAppTitleId()`。**刻意不用** `/mnt/shadowmnt/<TID>` 挂载点判据 ——
>   那只对 ShadowMount 挂出来的游戏成立，内置 PKG 没有那个目录。
> - `game_auto_plan()` 是**纯函数**（状态机全在这一处，宿主上穷举比在真机上试靠谱得多）：
>   同款连着跑 → 0 步；上一款在跑且开了 `on_exit` → 推；当前款在跑且开了 `on_launch` → 拉。
>   **换游戏时顺序恒为先推后拉**，写死才有一份可预期的日志。
> - **第一轮只记状态、不动作**：面板可能是玩到一半才起来的，补一次拉取会用远程
>   盖掉用户正在玩的存档。
> - **退出后先等 5 秒再推**，给主机收尾写盘的时间；只对推送等（拉取发生在游戏刚起来时，
>   那时存档目录还没被写过）。
> - `g_sync_mu` 保证同一时刻只有一次同步 —— **这不是性能优化，是正确性要求**：
>   「整份替换」是「旧的改名 → 换上去 → 删旧的」，两次并发跑同一个目录会互相把对方
>   刚改名的目录当成垃圾删掉。守护线程（退出推送）与用户手点「立即推送」完全可能同时发生。
> - 线程显式 `pthread_attr_setstacksize(1MB)`：这条线程要跑 curl + 打包，真机 pthread
>   默认栈只有 64KB 上下（`.141` 的教训）。

> ### 「远端还没有」不是失败
>
> `SR_ENOTFOUND (-2)` 把「远端还没有这个文件」与真失败分开。首次使用必然遇到，
> 给用户一条红报错是不对的（前端回 `ok:true` + 「远程还没有这个游戏的存档」）。
> FTP 上有两种表现都要归进来：`CURLE_REMOTE_FILE_NOT_FOUND`，以及
> **`CURLE_REMOTE_ACCESS_DENIED`** —— 实测轨迹是 `CWD ps5save → 550 → CWD <TID> → 550`，
> 远端根目录不存在时 libcurl 报的是后者。代价写在注释里：原始 FTP 回复都是 550，
> 服务器自己也不区分。

> ### 顺带修掉的三个真问题
>
> ① **`ga_do` 的判定顺序**：协议与配置先判。否则配了 smb 的用户点推送，只会看到
> 「主机上还没有这个游戏的存档」—— 真正的原因被一条无关信息盖住。
>
> ② **存档弹窗的字段标签一直是写死的中文**（`MODE_FIELDS` 里的字面量），英文包下
> 「主机地址 / 端口 / 远程路径」整片漏中文。回归当时只钉了 `game.secret_saved`
> 那条提示，而它「碰巧」盖住了这个洞 —— 默认协议换成 ftp 之后那条提示不再渲染，
> 断言变成永假，洞才露出来。现在标签走 `t()`，并把「密码框 placeholder」直接按
> 属性断言。
>
> ③ **mock 的 `/reset` 漏清 `snaps`**（v0.5.2.140 加快照时没同步改），也没关掉
> `/loadpayload:` 起过的 10101 桩 —— 同一个 mock 进程跑第二遍 `game112` 必红
> （5b 那节「还没做过快照时列表是空的」、1a 那节「10101 必须真的没人监听」）。
>
> ### 一个静默失败的教训：重复的 i18n 键
>
> 新增 `game.f_path`（远程路径）时**撞上了 `GameDetailModal` 已有的 `game.f_path`
> （物理路径）** —— JSON 与 i18next 都取最后一个，前面那条被无声吃掉，游戏详情里
> 那一行当场变成「远程路径」。这一条是回归抓到的（`详情有「物理路径」` 变红），
> 但抓得很偶然。所以新增一条静态守卫：**两份语言包都不许有重复键**。
> 同时把存档的字段键挪进独立命名空间 `game.sf_*`。

> ### 新增文件 / 接口
>
> | 文件 | 作用 |
> |---|---|
> | `include/save_remote.h` + `src/save_remote.c` | 传输层：FTP / HTTP 推与拉（~400 行） |
> | `include/game_auto.h` + `src/game_auto.c` | 状态机 + 守护线程 + 手动同步入口（~450 行） |
> | `src/curl_init.c` | `pldmgr_curl_init()` 单独一个 TU（见下） |
> | `test/stub_servers.{c,h}` | 自己实现的极小 FTP / HTTP 服务器（真协议，不是打桩） |
>
> - `POST /game_save_sync {tid, action:"push"|"pull", uid?}` → `{ok, action, uids, files, size, failed, note}`
> - `GET /game_saves` 新增 `auto_state:{running, last:{action, ok, at, message}|null}`
>
> **`pldmgr_curl_init()` 不能放 `utils.c`**：`utils.c` 被一大票**纯文件系统**的宿主测试
> 链着（game_saves / media_scan / game_mgr），`curl_global_init` 一进 `utils.o` 它们立刻
> `undefined reference` —— 等于把「存档的宿主测试不依赖 curl」这条设计从另一个方向拆掉。
> 收进 `src/curl_init.c`，谁用谁链。
>
> ### 测试
>
> - 宿主新增两条：`save_remote` **63 项 0 失败**（含真 FTP 协议往返：轨迹确认
>   `USER/PASS → PWD → CWD ps5save → MKD ps5save → CWD <TID> → MKD <TID> → EPSV → TYPE I → STOR`）、
>   `game_auto` **45 项 0 失败**（`game_auto_plan` 全组合穷举 + 手动推拉端到端）
> - 宿主全套：`game_saves` 142、`media_scan` 178、`game_mgr` 100、
>   `check_post_routes` **13==13**、`check_stack_frames` 全绿、`overall: PASS`
> - 前端：`game112` **232 → 248 项 0 失败**（新增 SMB 禁用 / 立即推拉 / 老配置 smb /
>   语言包重复键 / 英文包字段标签）；`nav112` 20、`acct111` 23、`avatar109` 19、
>   `cropfix` 75、`pack125` 15 全绿
> - 出包：`NayePlmTools_0.5.2.142-{me,ga}.elf` 各 10,818,640 B，titleId 自检 OK，
>   内嵌标题 `(c73ac9a)` 非 DEV，包内网页与 `frontend/dist/index.html` 逐字节一致

> ### 真机验收（192.168.1.214，已推 `.142`）
>
> 真机 `/version` = `0.5.2.142`；`/game_saves` 已带 `auto_state`（新代码在跑）。
>
> **① `device142_sync.py` —— 18/18 PASS**（本机起一个极小 HTTP 桩当远端，PUT 存 / GET 取）：
>
> - 远端为空时 `pull` → **200 + note「远程还没有这个游戏的存档」**（`SR_ENOTFOUND` 不是失败）
> - 配 `mode=smb` 时 `push` → **400 + 「当前 SDK 不支持 smb 传输」**（不是「没有存档」）
> - `push` → 远端真出现 `saves/PPSA27836/1a1d3627.zip`（15,728,894 B，
>   条目 `PPSA27836/sdimg_SAVESERVICE-LINE-0-1Slot`）
> - `pull` → `ok:true`；本地仍 1 文件 / 15,728,640 字节
> - 每一步之后 `/version` 都是 `0.5.2.142`（面板没被打死）
>
> **② `device142_pull_restore.py` —— 13/13 PASS**（专测 `.142` 新写的**拉取兜底**）：
>
> - 先用 FTP 把本地存档整个备份（记 sha256）→ `push` → **删掉本地存档** → `pull`
> - `pull` 此时本地 0 条，必须走 `game_saves_user_ids()` 兜底 → `ok:true`，
>   note `已拉取 1 份（1 个文件 / 15728640 字节）`
> - 恢复后的文件 **sha256 与原文件逐字节一致**
>
> **③ 其它真机回归（确认没碰坏）**：`device141_snap.py` **PASS**（快照 run/restore/delete 三条都不打死面板）、
> `device129_detail.mjs` **29/29**、`device127_badges.mjs` **17/17**、`device125.mjs` **PASS**。
>
> 验收脚本在 `_ftp_probe/device142_sync.py` / `_ftp_probe/device142_pull_restore.py`（不受版本管理）。

## v0.5.2.141 (CN-FORK)

> **`.140` 的快照功能在真机上一按就把面板打死 —— 根因是栈溢出。**
>
> `.140` 出包推送后：`GET /game_saves` 三款游戏全部正确，但一按「立即备份」
> （`POST /game_save_snap {"tid":"PPSA27836","action":"run"}`）→ `curl exit 52`（空回复）
> → `/version` 变 **000**，面板整个没了。
>
> ### 取证：快照目录里留下的半个现场
>
> FTP 上去看 `/data/nayeplmtools/saves_snap/PPSA27836/1a1d3627/`：
>
> ```
> .tmp_226_1791374835/      ← 建出来了，但是**空的**
> ```
>
> 时间戳目录没出现（说明 `rename` 没走到），临时目录里一个文件都没有（说明
> `gs_copy_file` 连目标文件都没建出来就死了）。而源目录里是**一个 15 MB 的
> `sdimg_SAVESERVICE-LINE-0-1Slot`**。
>
> ### 根因：整条链要 ~55KB 栈，而连接线程只有 64KB
>
> MHD 起的是 `MHD_USE_THREAD_PER_CONNECTION` —— 每条连接一条 pthread 线程，栈走
> **pthread 默认值**，真机上那个默认值只有 64KB 上下。这条路径原来是：
>
> | 帧 | 字节 |
> |---|---|
> | `http_on_request` | 13,080 |
> | `gr_dispatch_post` | 3,368 |
> | `game_saves_snap_run_now`（`gs_root_t roots[16]`） | 11,928 |
> | `gs_snap_take` | 5,000 |
> | `gs_copy_tree`（每层 `char sp[1024]+dp[1024]`） | 2,280 |
> | **`gs_copy_file`（`char buf[32*1024]`）** | **32,888** |
> | 合计 | **~55 KB** |
>
> 再叠上 MHD 自己那几帧就顶穿了。宿主上用「小栈线程」量出来的临界点非常干净：
> **≤56KB 一律 SIGSEGV，≥64KB 才活**。
>
> ⚠ 这个 bug **宿主测试 136 项全绿** —— 宿主跑在主线程的 8MB 栈上，这点数组
> 根本不构成压力。而且**同样的坑 `.116` 已经栽过一次**（改磁贴图标/背景死机，
> `tt_apply` 里一个 `unsigned char[65536]`），当时的结论是「别在栈上开大数组」，
> 但只改了那一个函数，没留下任何守卫。
>
> ### 修复
>
> **① 这条路径上的大块内存全部走堆**（精确修复）
>
> - `gs_copy_file` 的 32KB 缓冲 → 挂在 `gs_cp_t` 上，由调用方 `malloc` **一次**、
>   整条调用链共用（`GS_CP_IO` 64KB）。顺带把缓冲从 32KB 提到 64KB：syscall 少一半
> - `gs_copy_tree` 的每层路径缓冲 → 改成按 `depth` 从 `gs_cp_t.path` 里取槽位，
>   **栈占用与递归深度彻底脱钩**（`GS_SNAP_MAX_DEPTH=24` 现在只是纯堆开销）
> - `gs_root_t roots[GS_MAX_ROOTS]`（一个 712 B，16 个 11.4 KB）出现在 5 个函数里，
>   新增 `gs_roots_find()` 统一走堆：`game_saves_root` / `gs_root_by_uid` /
>   `game_saves_count` / `game_saves_snap_run_now` / `gs_snap_sweep` / `game_saves_list_json`
>
> 结果：整条链 **~55KB → ~22KB**。`gs_copy_file` 32,888 → **120**、
> `gs_copy_tree` 2,280 → **232**、`gs_snap_run_now` 11,928 → **536**、
> `gs_snap_main`（守护线程）14,216 → **2,840**。
>
> **② 连接线程的栈一次性给足 1MB**（系统性兜底）
>
> `main.c` 的 `http_daemon_start()` 加 `MHD_OPTION_THREAD_STACK_SIZE = 1MB`。
> 与其等第三次，不如把这一类问题整个消掉。**带回落**：万一这个 SDK 的 MHD 不认
> 这个选项（`start_daemon` 会直接返回 NULL = 面板起不来），去掉选项重试一次，
> 宁可退回默认栈也不能把服务弄没。快照守护线程是 `pthread_create` 出来的、
> 不受这个选项管，所以在创建处单独 `pthread_attr_setstacksize(1MB)`。
>
> **③ 两条守卫，防止第三次**（这是这一轮最该留下的东西）
>
> - `test/game_saves_test.c` ⑩：**在 32KB 线程栈上跑完整条快照链**
>   （备份 + 列表 + 一键还原）。带负向对照 —— 拿 `.140` 的 `game_saves.c` 跑，
>   它**必 SIGSEGV(139)**；修好后 138 项 0 失败。这是真机那个 bug 的宿主等价物
> - `tools/check_stack_frames.sh`（新）：任何函数栈帧不得超 **32KB**，
>   已接进 `run_host_tests.sh`。这是道**粗网**，抓「单个函数吃掉几十 KB」；
>   精确的那道是上面那条小栈用例
>
> 另外把 `game_saves_list_json` 里那句**错的注释**改掉了 —— 它写的是「线程栈是
> pthread 默认值（通常 512KB~1MB）」，正是这个错误假设让 32KB 缓冲上了栈。
>
> ### 测试
>
> - 宿主：`game_saves` **136 → 138 项 0 失败**、`media_scan` 178、`game_mgr` 100、
>   `check_post_routes` 12==12、`check_stack_frames` 全绿、`overall: PASS`
> - 负向对照：`check_stack_frames 20000` 能报出 `config.c` 等 4 个 25~28KB 的
>   历史帧并 exit 1（它们都在 32KB 之下，且现在也被 1MB 的连接线程栈覆盖）
> - 前端未改动；回归见下方出包记录

## v0.5.2.140 (CN-FORK)

> **存档三层计划的第 2 层：本地定时快照（不依赖网络的兜底）。**
>
> `.139` 修的是根因（存档从来没接上过）。这一轮做的是用户拍板的第 2 层：
> 远程同步（SMB/FTP/HTTP）要网络、要对面在线，是「多机之间搬存档」的活；
> 本地快照不依赖任何外部条件 —— 守护线程按每款游戏自己的间隔，把存档整份复制到
> 主机 `/data` 下，超保留数自动删最旧。
>
> 第 3 层（自动存档执行引擎 + 传输层）仍未开工。
>
> ### 落点与配置
>
> ```
> /data/nayeplmtools/saves_snap/<TID>/<uid>/<YYYYmmdd-HHMMSS>/
> ```
>
> 配置**跟着游戏走**（存在 `saves_auto.json` 同一个 tid 对象里的 `snap` 键，与
> `smb`/`ftp`/`http` 并排）—— 用户「对着这个游戏一起配」的心智模型不变。
> 能力 = 列表 + 一键还原 + 删除。
>
> ### 三个刻意为之的设计
>
> | 设计 | 为什么 |
> |---|---|
> | **内容没变就不新增** | 判据 `(文件数, 字节数, 最大 mtime)` 三元组。主机的存档绝大部分时间是同一份，30 分钟一拷的话一天 48 份一模一样的东西，白占 `/data` |
> | **源目录最近 20 秒内被写过就跳过本轮** | 存档正写到一半时拷出来是撕裂的，还原回去可能让主机认不出来 |
> | **复制到 `.tmp_*` 成功才 `rename`** | 半截的快照永远不以正式名字出现 |
>
> 「内容没变不新增」的代价是「点立即备份没反应」—— 前端专门为它写了一句话
> （「存档没有变化，未新增快照」）并进了回归：这条最容易被当成 bug。
>
> ### 细节
>
> - 复制**保留 mtime**（`utimes`）：既是去重判据成立的前提，也让还原回主机后的时间线不变
> - 时间戳目录名固定 15 字符，**字典序 = 时间序** —— 排序 / 找最旧 / 找最新都不用解析时间
> - 裁剪两道：每 `(tid,uid)` 留最近 `keep` 份；另有全局字节预算 4 GB 从**全局最旧**淘汰
>   （只按 keep 裁的话，15 款 × 5 份 × 18 MB = 1.3 GB，主机 `/data` 顶不住）
> - **上传的整份替换抽成了 `gs_swap_dir()`**，快照还原与上传共用同一条「旧的先改名留一手、
>   失败退回去」—— 两条路各写一份的话，早晚有一边忘了「退回去」（`.139` 刚修过这个）
> - 接口只新增**一条** `POST /game_save_snap`（`action = run|restore|delete`）；列表不另开 GET，
>   快照跟着 `/game_saves` 一起回（`snapshots[]` + `snap_used`）
> - `interval` / `keep` 的归一化在**落盘、回吐、守护线程读**三处共用同一个函数。
>   **坑**：不能拿读端口的 `gs_get_port`（夹 1~65535）读间隔 —— 用户填「一天」（86400）
>   会被判越界、静默回落成 30 分钟。专门加了 `gs_get_int` 并写断言钉住
> - `uid` 与 `ts` 是快照路径的最后两段，在**拼路径之前**就校验
> - safe_mode 下守护线程整条跳过（与风扇、名字自愈两条守护同一原则）
>
> ### 测试
>
> - `game_saves_test` **95 → 136 项 0 失败**：立即备份按用户数出份数 / 内容没变不新增 /
>   只改哪个用户就只新增哪个 / keep 裁剪 / 一键还原且不牵连别的用户 / 删除且再删一次必须失败 /
>   uid 与 ts 的路径穿越一律拒 / 86400 不被回落
> - `game112` **215 → 232/232**（新增 17 条：块在、默认关、列表空、按用户数出份数、
>   没变不新增、还原与删除各自的确认弹窗文案、英文包不漏中文）
> - 宿主测试新增 `-DGS_SNAP_QUIET_SEC=0` 与 `-DGAME_SAVES_SNAP_DIR`。
>   不置 0 的话夹具（刚造出来，mtime 就是「现在」）会让每一份快照都被「正在被写」挡掉，
>   整段等于没测
> - **测试顺序坑**：把「删除快照」用例放在「只该新增 1 份」之前，删掉之后那个用户就没有
>   「最近一份」可比、下一轮会重新生成，断言从 1 变 2。删快照后守护线程补回来是**对的**
>   行为 —— 所以改的是用例顺序，不是代码

## v0.5.2.139 (CN-FORK)

> **存档功能从来没接上过 —— 根因是根目录扫错了。**
>
> 用户报「存档功能都没生效」。真机取证（FTP + 面板接口）之后发现：
> **不只是自动存档没生效，手动存档也一直是死的。**
>
> ### 根因：PS5 存档在 `savedata_prospero`，代码只找了 `savedata`
>
> `src/game_saves.c` 的 `game_saves_root()` 只拼 `<uid>/savedata/<TID>`：
>
> ```
> /user/home/<uid>/savedata/          ← 代码只扫这里
>     sce_backup2/   sce_backup4/     ← 真机上这两个是空的系统备份槽
> /user/home/<uid>/savedata_prospero/ ← 存档真正在这里
>     PPSA01650/
>         sdimg_STARBOARD            6 MB
>         sdimg_sce_sdmemory         3 MB
>         sdimg_sce_bu_sdmemory      3 MB
>         sdimg_sce_bu_STARBOARD     6 MB
> ```
>
> 真机实测（`.138` 上跑）：
>
> | 请求 | 返回 |
> |---|---|
> | `GET /game_saves?tid=PPSA01650` | `local_path:""`，`saves:[]` |
> | `GET /game_saves?tid=PPSA21567` | `local_path:""`，`saves:[]` |
> | FTP `savedata_prospero/PPSA01650/` | 4 个 `sdimg_*`，约 18 MB |
>
> `game_saves_root()` 回 -1 → **列表、下载、上传、删除全部建立在它上面，整条链一起死**。
>
> ### 为什么一直没发现：夹具造的是错的布局
>
> `test/make_game_fixture.py`、`frontend/mock-server.js`、`test/game_saves_test.c`
> **全都按 `<uid>/savedata/<TID>/<槽>` 造数据** —— 测试全绿，功能全死。
> 这是「mock 抹掉了真机的前提」这一族里最贵的一次：夹具把错的布局**证实**成了对的。
> 教训写进夹具头注释：**夹具必须跟着真机走，不能跟着代码走。**
>
> ### 参照 garlic-savemgr（earthonion）取的经
>
> 用户点名让先看 https://github.com/earthonion/garlic-savemgr。它是 PS5 存档
> 解密/加密/浏览工具，`src/main.c` 5560 行，同时扫 `savedata_prospero` 与 `savedata`
> —— **`savedata_prospero` 这条路就是从它的 `scan_saves` 拿的**。另外三条：
> 每条存档带 `uid` 保留多用户维度、跳过 `.bin` 密封密钥伴随文件、
> `sdimg_sce_bu_*` 标记为备份。它**没有**自动存档引擎（纯手动五个页签），
> 那块没得抄。
>
> ### 粒度改成「一个用户一条」
>
> 一个 `<uid>/<TID>` 目录 = 一份完整存档，里面的 `sdimg_*`（含 `_bu_` 备份、
> PS4 的 `.bin` 密封密钥）一起打包/还原 —— 少一样都可能让主机认不出来。
> API 里的 `save` 参数从「槽名」变成**用户目录名**（真机形如 `1a1d3627`）。
>
> 老代码「取体积最大的那个」是为了绕开多用户问题，但那是把两个用户的存档
> **拍扁成一个，会互相盖**。真机实测 `PPSA01650` 在两个用户下都有存档，
> 所以现在返回的是列表：**每个用户各一条，互不覆盖**（`gs_user_ids` 按 uid 升序
> 排过，列表顺序稳定）。
>
> `kind`（PS5/PS4）改由**命中目录**决定，不再按 tid 前缀猜。
>
> ### 顺带修掉的两处
>
> - **上传整份替换改成可回退**：旧的先 `rename` 成 `.old_*`，换成功才删。
>   原来是 `rm -rf` 再 `rename` —— 中途任何一步失败都等于把用户原来的存档删了。
> - **临时目录挪到存档根的父目录**：`<uid>/savedata_prospero/.upl_*`，
>   换上去是同文件系统内的 `rename`，不再跨目录拷贝。
>
> ### 测试
>
> - 夹具 `test/make_game_fixture.py` 整个重写为真机布局，逼出五条规则：
>   只有系统备份槽的用户不算「有存档」/ 多用户各一条 / 两处都有时 PS5 优先 /
>   PS4 走 `savedata/` 且 `.bin` 计入 / `<GS_HOME>` 下的隐藏目录不是用户。
> - `test/game_saves_test.c` 断言跟着改：**95 项，0 失败**。
> - `game112.mjs` 断言「本地存档路径」改成 `savedata_prospero`，
>   新增一条「每行标出是哪个用户的存档」：**215 / 215 通过**（原 214 + 1）。
>
> 版本号 `0.5.2.138` → `0.5.2.139`。
>
> **尚未做**（用户已确认要做，分后续版本）：自动存档执行引擎（`on_launch` /
> `on_exit` 目前仍只存配置不执行）、本地定时快照。已查明 **SDK 里的 curl 8.18.0
> 没有编 SMB**（`lib/smb.c` 是空壳、无 libsmb2），所以自动存档的传输只能做
> **FTP + HTTP**；而用户现有配置恰好是 SMB。

## v0.5.2.138 (CN-FORK)

> **修掉 `.137` 的致命漏登记：`POST /game_title_sync` 在真机上根本不工作。**
>
> 现象（真机实测，`.137` 推上 192.168.1.214 后第一件事就是验它）：调
> `/game_title_sync` 得到的是**连接被直接关掉**（python 侧 `RemoteDisconnected`），
> 不是 404、也不是 500 —— 而 payload 本身活得好好的（`/version` 照常回）。
>
> **根因**：`src/http_server.c` 里有两张**分开写**的表 ——
> `gr_post_limit()` 决定「这条 POST 收不收请求体」（返回 0 = 不归我们管），
> `gr_dispatch_post()` 决定「收到之后怎么处理」。`.137` 只加了后者、漏了前者
> → 那个 `PostStatus` 累加器根本不建 → `gr_dispatch_post` 永不触发 →
> 客户端看到连接被直接关掉。真机上极难从现象反推回这里。
>
> **为什么三道门全放行**：`gr_post_limit` 要 `microhttpd.h`，主机套件编不了它；
> mock 里也没有「收体闸门」这一层 —— 所以 `game112` 的 214 项照样全绿。
> 又一次「mock 抹掉了真机的前提」（`.136` 那课的同族）。
>
> **`.137` 并不是全废**：启动自愈线程是 `main.c` 直接调的、不走 HTTP，
> 真机上确实跑了 —— 日志实测
> `[PLDMGR] title: 启动自愈完成（回填 8 款：主机索引 8、名字源 5、失败 0）`，
> 且 15 款面板名与 SMP `title_name` 全部一致（含 3 款 ffpfsc 镜像类）。
> 坏的只有那颗手动按钮。
>
> **修法**：`gr_post_limit()` 补上 `ROUTE_GAME_SYNC`（1 MB 档，与其他 JSON 路由同档）。
>
> **新增静态守卫** `tools/check_post_routes.sh`（已接进 `test/run_host_tests.sh` 最前面，
> 毫秒级）：解析 `gr_dispatch_post` 与 `gr_post_limit` 两个函数体，断言前者分发的
> 每条路由都在后者登记过。**负向对照**：拿一份把 `.137` 那条漏登记还原回去的副本跑，
> 立刻点名 `ROUTE_GAME_SYNC` 未登记并非零退出。
>
> 版本号 `0.5.2.137` → `0.5.2.138`。

## v0.5.2.137 (CN-FORK)

> **面板里改过的中文名，现在会自己补回主机 —— ShadowMount 与电视主屏不再显示英文原名。**
>
> 用户截图：面板列表写「宇宙机器人 2」「最终幻想7：重置」，而 ShadowMount 后台与电视主屏
> 还是 `ASTRO BOT` / `FINAL FANTASY VII REMAKE`。逐款重改一次名谁也不愿意，于是加了回填入口。
>
> **根因是时序错位，不是代码写错：**
>
> - 用户那 8 次改名发生在 **10:59–11:17**（`name.txt` 的 MDTM 证据）；
> - 写**游戏源** `sce_sys/param.json` 的能力 **13:57** 才提交（`09c44d6` = `.132`）；
> - 源文件自安装起**从未被写过**（PPSA21567=9/8、PPSA32786=9/18、PPSA18463=7/30、PPSA26873=7/31）；
> - 之后 SMP 重扫按源重建 `app.db`，中文名被冲回英文原名。
>
> `.132` 那条「SMP 重扫不回退」的真机闭环其实是用 PPSA27836 做的、验完还**还原回了原名**
> （源 mtime 14:49），所以当时没暴露这条时间差。
>
> **真机三个实验把「SMP 到底读哪一层」钉死了：**
>
> 1. 改 `/user/app/<TID>/sce_sys/param.json` 对 SMP **无效**（PPSA20447 定点替换后 60 秒
>    纹丝不动，文件也没被覆盖）→ 这条路是死的；
> 2. 改**系统 `app.db`** 对 SMP **有效**，且**镜像类也立刻生效**（`persisted:false` 但
>    `title_name` 立刻变中文）；
> 3. `POST /api/v1/scan` **不会**重建 app.db 的名字（中文名 120 秒内活着）→ `.132` 的
>    「SMP 重扫会冲掉」结论归因错了。
>
> 所以回填的主力是 **app.db 那一层**，不是游戏源。
>
> **修法：**
>
> - **后端**（`src/game_title.c` 新增 `game_title_sync_all()` + `game_title_sync_start_daemon()`）：
>   - 遍历 `/data/nayeplmtools/titles/<TID>/`，**只处理有 `name.txt` 的款**（绝不替用户
>     决定没改过的游戏）；逐款 ① 写主机索引（`tile_db_set_name` → app.db 平铺名字列 +
>     appinfo.db）② folder 类再写一次源 `param.json`（镜像 / pkg 写不了源，如实计入
>     `persisted` 的差）；
>   - 幂等：`tile_db.c` 的 `td_count_differ` 先 COUNT/SUM 比一遍，已一致则一次写事务都不开；
>   - 启动自愈线程（detached，延迟 25 秒起，避开启动高峰），SAFE MODE 下跳过；
>   - 新增路由 `POST /game_title_sync`，返回 `{ok,scanned,applied,persisted,failed,note}`。
> - **前端**（`GameSection.jsx` + `gameApi.js` + 语言包）：
>   - 板块右上角新增「同步名字到主机」按钮（`Tag` 图标，跑的时候 `animate-pulse`）；
>   - 跑完 `addToast` 如实报口径（`已回填 N 款：主机索引 X、名字源 Y、失败 Z`），
>     `failed > 0` 按 error 弹；并 `load(true)` **强制重拉**（后端改的是主机 app.db，
>     而列表吃 60s 快照 —— 不重拉用户点完看不到变化，会以为按钮没反应）。
> - **mock**：`gameState` 新增 `hostNames`（= 主机 app.db 名字列，与 `renamed` = name.txt
>   **刻意分开**），`/game_rename` 同时写两份；新增 `POST /game_title_sync` 与两个测试钩子
>   `GET /_host_names`、`POST /_host_drift`（模拟 SMP 重扫把主机名字丢掉）。
> - **测试**：`game112.mjs` 新增第 7.7 节 11 条断言 —— 断言的是「漂了 → 点同步 → 补回来」，
>   而不是「按钮存在」（只断言后者的话这条链在 mock 里没有任何可观察效果，等于没测）。
>   `param_rename_test.c` 补一个 `pldmgr_utils_safe_mode` 桩（`game_title.c` 新增的启动
>   自愈线程引了这个符号，而该测试把整个 `game_title.c` 拉进同一翻译单元，链接期就要落处）。
> - **真机已修好（手动补一次）**：`reapply_renames.py` 用产品自己的 `POST /game_rename`
>   把 8 款 name.txt 重放一遍，**15 款全部一致，7 款被纠正**（含 3 款镜像类）。
>
> 宿主测试：`game_mgr` 100/100、`param_rename` PASS、overall PASS；变异测试 7/7 全红。
> 回归：`game112` **214/214**（原 203，新增 11 条）。
>
> 版本号 `0.5.2.136` → `0.5.2.137`。
>
> ⚠ **本包的 `POST /game_title_sync` 在真机上不可用**（漏登记 `gr_post_limit`，
> 请求体不收 → 连接被直接关掉），`.138` 修的就是这一条。面板层与启动自愈都正常 ——
> 真机日志实测自愈已把 8 款回填完、15 款面板名与 SMP 全一致，坏的只是那颗按钮。

## v0.5.2.136 (CN-FORK)

> **磁贴上的体积不再说谎。** 之前磁贴显示的是 app.db 的 `size` 列 —— 一款 132 GB 的
> 游戏在磁贴上写 1.22 GB，跟同一个弹窗里的「体积」并排一看就矛盾。
>
> 用户截图：详情弹窗 `体积 132 GB` / `app.db 1.22 GB`，而右边同一款游戏的磁贴写 `1.22 GB`。
>
> **根因是今天三个提交叠出来的回归：**
>
> - `.134`（`9959b4a`）为了「列表秒开」，列表模式置 `skip_du=1` → folder 游戏 `size=0`
>   → `formatSize(0)` 返回空串 → 磁贴没体积标签；
> - `.135`（`2fbcb43`）为了让标签回来，前端改成 `formatSize(g.size || g.appdb_size)` ——
>   标签是回来了，但显示的是 **app.db 的注册体积**，不是游戏体积。
>
> **为什么预览里没发现**：`frontend/mock-server.js` 的列表直接给 folder 游戏塞了真实
> `size`，且只有 PKG 才有 `appdb_size` —— 「列表不扫盘、size=0」这个前提被整个抹掉了，
> 于是这条兜底分支在本地永远是绿的，只在真机翻车。
>
> **修法（用户拍板「后台预热」）：**
>
> - **后端**（`src/game_mgr.c`）：
>   - 体积缓存拆成 `gm_dir_size_peek`（只读缓存、不扫盘）与 `gm_dir_size_cached`
>     （未命中才真 du 并写回），缓存表新增 `g_du_mtx` 自保（预热线程也会写它）；
>   - `gm_fmt_size` / `game_enum_dump` 在列表模式（`skip_du=1`）只 peek：命中显示真实
>     体积，未命中留 0，并把根目录攒进 `g_warm_roots`（上限 64，去重，只收 folder/lz4）；
>   - `build_game_json` 结束时 `gm_warm_kick()` 起一个 detached 线程去 du 这些目录
>     （`g_warm_running` 受 `g_game_mtx` 保护，同一时刻只允许一个），扫完填缓存并
>     **作废 60s 快照**；列表 JSON 尾部新增 `"warming":true`；
>   - 锁序恒为 `g_game_mtx → g_du_mtx`，worker 收尾只取 `g_game_mtx`、不套 `g_du_mtx`，无环；
>     worker 扫盘在锁外做，不阻塞列表/详情读。
> - **前端**（`GameSection.jsx`）：
>   - `sizeLabel = formatSize(g.size || (g.fmt === 'pkg' ? g.appdb_size : 0))` ——
>     `app.db` 的 size 列只给「没有物理源可扫」的内置 PKG 兜底，folder/lz4/image 一律不用；
>   - 响应带 `warming` 时，每 4 秒自动重拉一次（上限 15 次 ≈ 1 分钟），预热一结束就停。
>     **不是轮询**：只在预热期间临时重试，且重试走普通 `load()`（吃快照，不带 `force=1`）。
> - **mock**：照抄真机时序（folder/lz4 列表初始 `size=0` + `warming`，3 秒后补上真实体积
>   并作废快照；PKG 带 `appdb_size`），`/reset` 一并清预热状态 —— 让这条路径在本地能复现。
> - **测试**：`make_appdb_fixture.py` 的裸目录源从 `/mnt/ext1/...`（主机上不存在）改成
>   夹具根下的**真实目录树**（4096 + 1000 字节，含一个 `.hidden` 钉住「跳过点开头条目」的口径），
>   `game_mgr_test.c` 新增 6 条断言：冷缓存下 folder 体积必须是 0（**不是** appdb_size）、
>   必须带 `warming`、预热后必须拿到真实字节数且不再报 `warming`、内置 PKG 的 `appdb_size` 仍在。
>   `mutation_check_game_mgr.sh` 新增 3 个变异体（`appdb` / `peek0` / `nodu`），三个都真变红。
>
> 宿主测试：`game_mgr` 100/100（原 94），overall PASS。回归：`game112` 203/203。
>
> 版本号 `0.5.2.135` → `0.5.2.136`。

## v0.5.2.133 (CN-FORK)

> **刷新按钮现在真从主机重新读，不再等 60 秒快照自然过期。**
>
> `.132` 及更早，游戏列表有一个 60 秒快照缓存（`/game_list` 的全库 `app.db` 读取 +
> 一次递归 `du` 是重 IO，缓存是为了治「游戏列表载入超级慢」）。进页面那次 `load()`
> 吃缓存是对的，但刷新按钮也走同一条路 —— 用户连点十次刷新，看到的还是那份快照，
> 体感就是「刷新按钮没反应」。
>
> **根因实测（4 次连续调用，中间用 FTP 直写 `name.txt` 改主机真相）**：
>
> ```
> #1 进页面 load   → 鬼武者 Way of the Sword    (15 games)
> #2 立即再刷      → 鬼武者 Way of the Sword    (字节完全一致)
>    └ FTP 写 name.txt = "外部FTP直写_列表应缓存住旧值"
> #3 立即再刷      → 鬼武者 Way of the Sword    ← 主机真相变了，列表没跟上
> #4 等 61 秒后    → 外部FTP直写_列表应缓存住旧值 ← 重建才读到新值
> ```
>
> 产品自身的改名 / 换图标 / 换背景 / 删游戏都会调 `game_mgr_invalidate()`，
> 所以那些操作完刷新立刻生效 —— 问题只在**外部工具**（FTP、SMP 自己扫进新游戏）
> 改了 `name.txt` / `app.db` 之后，用户手动刷新永远看不到。
>
> **修法：加一个 `force=1` 参数，只给刷新按钮用。**
>
> - 后端 `game_mgr_list_json(buf, size, int force)`：`force != 0` 时跳过快照、
>   直接重建、刷新缓存时间戳。进页面那次 `load()` 仍然吃缓存（快），
>   手动刷新才付一次全库读的成本 —— 手点刷新本来就是显式意图，不是轮询。
> - 前端刷新按钮 `onClick={() => load(true)}`，请求带 `?force=1&_=${Date.now()}`；
>   `useEffect` 里的进页面 `load()` 不带 force（缓存命中）。
> - mock 同步加了 60 秒快照缓存，否则这条断言在本地永远绿。
>   同时补齐了 mock 侧 5 条失效路径（rename / set_icon / set_bg / reset_icon / reset_bg）
>   —— 后端每条都调 `game_mgr_invalidate()`，mock 之前一条都没作废缓存。
>
> **新增诊断出口 `game_mgr_cache_ts()`**：返回当前快照时间戳（0 = 空），
> 仅供 `test/game_mgr_test.c` ⑪ 块断言时间戳递进。seed_stack 播种的重建字节
> 可能恒同（两次播种的垃圾一样），所以 `memcmp != 0` 不能当判据 ——
> 改用时间戳：invalidate 后 ts=0，force=1 必须刷成非 0；紧跟的 force=0
> 必须走缓存（ts 不动、字节恒同）。

### 改动清单

| 文件 | 改动 |
|---|---|
| `include/pldmgr.h` | 版本 `0.5.2.133` |
| `include/game_mgr.h` | `game_mgr_list_json` 加 `int force` 参数；新增 `game_mgr_cache_ts()` 声明 |
| `src/game_mgr.c` | `game_mgr_list_json` 加 force 分支；新增 `game_mgr_cache_ts()` 实现 |
| `src/http_server.c` | `/game_list` 解析 `?force=1` 传给 `game_mgr_list_json` |
| `frontend/src/components/views/GameSection.jsx` | `load` 接受 `force` 参数；刷新按钮 `load(true)` |
| `frontend/mock-server.js` | 加 60 秒快照缓存 + `/_game_list_cache` 测试钩子；补齐 5 条失效路径 |
| `frontend/src/utils/gameApi.js` | 契约注释更新 `GET /game_list[?force=1]` |
| `test/game_mgr_test.c` | ⑪ 块：force=1 时间戳递进断言 |
| `test/game_mgr_test.c` | 两处 `game_mgr_list_json` 调用补 `0` 参数 |

### 回归结果

- **主机单元测试**：`game_mgr: 94 checks, 0 failures`，`overall: PASS`
- **前端回归** `game112.mjs`：203/203 通过（含 7.6 段 5 条断言）
- **版本握手**：`pldmgr.h` / mock `/version` 均为 `0.5.2.133`



> **改名不再回退：这次改到了名字真正的来源。**
>
> `.131` 及更早只改得了两层**缓存**（面板的 `name.txt`、主机的 `app.db` / `appinfo.db`），
> ShadowMount 一重扫就用自己的源把两层全重建一遍，于是「改完名、重启又回去了」。
> 用户明确说这样不行，要找到游戏包里控制名字的那个文件并直接处理。
>
> **侦察结论（全部只读，动文件之前先拿证据）**：
> * 名字源**不是** `NPXS.json` —— 全盘 `/user/app/` 递归扫描，`NPXS.json` 与 `.sig` 都是 **0 个**。
> * 真正的来源是 **`sce_sys/param.json`** 里的 `localizedParameters.<语言>.titleName`
>   （一款 1～10 个语言键，仁王 3 有 10 个）。
> * **ShadowMount 完全不缓存名字**：`image_index.bin`（2.44 MB）里只存镜像路径 + TID，
>   按「Onimusha / Nioh / 鬼武者 / 仁王 / Way of the Sword」逐个字符串搜索全部 **0 命中**；
>   `config.ini` 只有 8 行；`/user/data/PPSA01650/` 根本不存在。
>   → **改源就是终点**，不需要常驻守护进程、不需要重扫后再补一刀。
> * 签名验证**不存在**：这些是 AMPR 已解密的包（有 `fakelib/`、96 字节的 `keystone`、
>   `eboot.bin.orig`、`*index.crc`），没有校验链，改写 `param.json` 不会让游戏起不来。
> * FTP 写权限已在三处实测通过（`STOR` → `RETR` 字节一致 → `DELE`）。
>
> **游戏分三类，只有第一类能改源**：
>
> | 类别 | 数量 | 源 | 结论 |
> |---|---|---|---|
> | folder | 10 | `mount.lnk` → 磁盘目录 | `sce_sys/param.json` 是普通文件，**可写** |
> | image | 4 | `mount_img.lnk` → 单个 `.ffpfsc`（28.9～171.5 GB） | 名字锁在镜像文件系统内部，**不可行** |
> | pkg | — | 无 lnk | 系统内置安装，没有可写的游戏源 |
>
> 后两类**如实报告不适用**，不再假成功。
>
> **用定点替换而不是 cJSON 整包重写**：真机 `param.json` 是 CRLF + 2 空格缩进的美化格式
> （约 2.9 KB），整包重写会把它压成单行 LF，几千字节的改动面全是为了改一个字符串。
> `titleName` 在 schema 里只出现在 `localizedParameters` 的语言对象下，
> 所以全局替换所有 `"titleName": "..."` 是精确命中 —— 一次改完全部语言，
> 不会遇到「英文改了日文没改」这种一半生效的怪现象。

### 🔍 侦察工具（`_ftp_probe/`，全部只读）

| 脚本 | 作用 | 结论 |
|---|---|---|
| `recon_names.py` | 递归 `LIST` + 定向 `RETR`，找 `NPXS.json` / `.sig` | 全树 0 个 |
| `recon_param.py` | 读全部 `mount.lnk` / `mount_img.lnk`，拉 `param.json` | 定位到 `localizedParameters` |
| `recon_source.py` | 比对源与挂载点的 `sce_sys/` 文件清单 | SMP 是**拷贝者**不是绑定者，两处清单一致 |
| `recon_smp.py` | 遍历 SMP 数据目录 | 无 per-TID 目录 |
| `recon_db.py` | 拉 `config.ini` + `image_index.bin`，抽字符串搜索游戏名 | **5 个名字全部 0 命中** |
| `verify_write.py` | 三处目标写探针 + 往返校验 + 清理 | 全通过 |

### ✨ Added

* **`game_mgr_source_paths()`**（`include/game_mgr.h` + `src/game_mgr.c`）：
  只读 `/user/app/<TID>/` 下那两个 `.lnk` 文件解析物理源，**毫秒级**。
  * 与既有的 `gm_fmt_size()` **共用 `gm_ext_is_image()` / `gm_lnk_root()`**，
    分类口径不可能漂移（folder / image / pkg 的判据只有一份实现）。
  * 为什么不复用 `game_mgr_source()`：那是全量 `app.db` 读 + 目录扫描（几十到几百毫秒），
    对 `/game_delete` / `/game_detail` 这类读接口合适，**放在写路径上会让用户改个名卡一下**。

* **`tt_param_rename()`**（`src/game_title.c`）：逐字节拷贝 + 命中就换值，
  缩进 / CRLF / 其他字段一字节不改。
  * **契约**：`hits > 0` → 换缓冲区；`hits == 0` → **调用方的缓冲区原样保留**。
    第二条是硬要求 —— 没命中时那个拷贝是白费的，而且调用方传的可能是**栈数组**，
    `free` 它会直接崩。`test/param_rename_test.c` 拿一个残缺的 `titleName` 边界输入钉住了这条。
  * `TT_PARAM_MAX = 512 KB` 防呆天花板（实测 1～10 KB）。
  * 名字转义只处理 `"` 与 `\` —— `game_title_set_name()` 已经剥光了 `<0x20` 与 `0x7F`。

* **`tt_rename_in_param()`**：读入 → **先备份原厂件**（`<GAME_TITLE_DIR>/<TID>/orig/param.json`，
  仅首次）→ 定点替换 → 原子写回。
  * **备份顺序有讲究**：必须在改写内存缓冲**之前**备份，否则存进去的是我们自己改过的内容，
    用户的回退点就没了。备份 best-effort，失败只记日志。
  * **原子写**：同目录 `.tmp` + `rename()`，失败退化成直接写 —— 避免写一半断电留下截断的
    `param.json`。

* **改名现在会连带改源**（`game_title_set_name()`）：folder 游戏改完 `name.txt` + 主机索引后，
  再改 `<源>/sce_sys/param.json`，并 best-effort 同步挂载点那份副本
  （路径相同时跳过，免得对自己重复改写）。
  * **源写失败不把整个改名打成失败**：面板层已经生效，用户不该因为源是镜像就白改一次名；
    状态如实回给前端。

### 🔌 API

`POST /game_rename` 增加两个字段：

```json
{"ok":true,"name":"新名字","persisted":false,"persist_note":"镜像游戏：名字锁在镜像文件内部，无法持久修改"}
```

* `persisted` —— 名字**源**也改成功了吗。只有它才是真的不回退。
* `persist_note` —— `persisted=false` 时说明原因（镜像游戏 / 系统内置 / 源写失败）。
* **`persisted=false` 不是错误**。面板层与主机索引层已经生效，改名本身是成功的，
  只是重启 SMP 之后电视主屏会回到原名。

### 🖥️ 前端

`RenameModal.jsx` 增加一个**常驻**提示（`Note` + `Check` / `AlertTriangle`）。
不用 toast —— toast 几秒就没了，而「重启 SMP 之后会不会又变回去」是用户下次重扫才撞上的问题，
只在成功那一刻弹一下等于没说。`persist` 状态在弹窗打开时重置，不跨款残留。

### 🧪 测试

* **`test/param_rename_test.c`**（新增，主机侧）—— 这是全仓库唯一会**写进用户游戏包**的逻辑，
  改坏 `param.json` 不可逆，而它在真机上要到「改完名 + SMP 重扫」才暴露，反馈链太长。
  夹具是真机 FTP 拉下来的真实 `param.json`（238 B～3.2 KB，1 / 3 / 10 个语言键），
  用 `#include` 把被测源文件拉进同一翻译单元以触及 `static` 函数。**43 项断言**：
  * 替换处数与独立统计一致；
  * **除 `titleName` 的值之外逐字节一致**（缩进 / CRLF / 其他字段都没动）；
  * 转义后的新名字出现次数 == 语言数；
  * 改写结果经**真 cJSON 解析器**校验仍是合法 JSON，且每个语言键的 `titleName` 读回来都等于新名字；
  * CRLF 行数不变；无 `titleName` 的文件返回 0 且内容一字节未改；
  * 残缺输入（`"titleName": ` 后就没了）返回 0、不越界。
  * 顺带钉住一个 schema 事实：`localizedParameters` 里除了 `<lang>: {...}` 还有
    `defaultLanguage: "..."` 这类**字符串**子节点（四份夹具都有）。它不携带 `titleName`，
    所以全文替换不会误伤它 —— 但也别在解析校验时把它当成缺字段的语言键。
* 已接入 `test/run_host_tests.sh`（夹具缺时 SKIP，不半跑）。**全套 PASS**。
* **`game112.mjs` 新增 12 条断言**（改名段 + 新的 7.4 节）：
  * folder 游戏改名成功 → 弹窗内提示「已写入源文件 sce_sys/param.json」，且**不出现**镜像/内置措辞；
  * 镜像（`ffpkg`）与系统内置（`pkg`）两类：保存前无提示 → 保存后改名**仍然成功**（成功提示在）
    **且**弹窗说明源为什么不可写 **且** 不谎报已写入源文件 **且** 列表里的名字真的变了。
    最后这条是防止把 `persisted:false` 当错误、让 UI 报错而用户白改一次名。

### 🎬 真机闭环验证（PS5，192.168.1.214）

目标游戏 **PPSA27836 鬼武者：剑之道**，`source_type=folder`，源
`/mnt/ext1/PPSA27836-鬼武者/sce_sys/param.json`（2902 B，3 个语言键 + `defaultLanguage`）。
四层证据链逐字节核验，**32 项断言全过**（`_ftp_probe/rename_verify.py`）：

| 层 | 路径 | 改名前 | 改名后 |
| --- | --- | --- | --- |
| ① 面板 | `/data/nayeplmtools/titles/PPSA27836/name.txt` | 鬼武者：剑之道 | 鬼武者（改名验证） |
| ③ 源 | `<源>/sce_sys/param.json` | 2902 B，3 语言各自原名 | 2905 B，3 语言全 = 新名 |
| ④ 挂载副本 | `/user/app/PPSA27836/sce_sys/param.json` | 与 ③ 骨架一致 | 已同步 |
| ⑤ 备份 | `/data/nayeplmtools/titles/PPSA27836/orig/param.json` | 不存在 | 2902 B，**== 原厂件** |

接口返回：

```json
{"ok":true,"name":"鬼武者（改名验证）","persisted":true,
 "persist_note":"已写入 /mnt/ext1/PPSA27836-鬼武者/sce_sys/param.json（3 个语言）"}
```

关键结论：`POST /api/v1/scan`（SMP 重扫）→ `{"queued":true}` 之后，
`POST /api/v1/games` 返回的 `title_name` 就是新名字，**没有回退**。这一步在 ≤.131 上
正是名字被冲回去的地方。SMP 是从源重建索引的旁证：重扫后它显示的中文名字来自我们改写的
`localizedParameters`，如果它读的是自己缓存的 `app.db` 副本，仍会显示 `Onimusha: Way of the Sword`。

逐字节核验同时证实了定点替换的安全边界：
* 改写后**骨架与原厂件逐字节一致**（挖掉 `titleName` 的值再比），即只动了这三个值；
* 字节数变化 `+3 B` == 三个 titleName 值长度差之和（2902 → 2905）；
* CRLF 行数 135 不变；顶层 29 个键不变、未被误伤；
* 全角括号正确走 JSON 转义（`\uff08` / `\uff09`），未出现裸字节；
* 顶层没有新增 `titleName` 字段。

另：**还原验证**也走通了同一套路径 —— 用 ja-JP 正规名 `鬼武者 Way of the Sword` 重扫后
SMP 首次轮询即一致，且源文件回到 **2902 B（与原厂件零字节差）**，证明替换是长度无偏的。

### 📝 注意

* **SMP 的 `title_name` 来自它自己的 `app.db`，不是实时读源。** `/game_rename` 之所以让主屏
  立刻变名，是它同时写了源和 `app.db`/`appinfo.db`（`tile_db_set_name`）。因此**外部工具（FTP）
  直接改源，主屏不会跟着变**，得等 SMP 下次自己重建索引。这不是产品缺陷，是给排障留的记录：
  真机改名后若发现「源改了、主屏没变」，先看是不是走了产品之外的路径。
* **所有语言键一起改**。`tt_param_rename` 替换的是 `param.json` 里**每一个** `titleName`，
  多语言游戏会把非主力语言也换成新名字（实测：`ja-JP` 原本就是被覆盖的那个）。这是设计取舍，
  不是 bug —— 但别假设产品会保留其他语言的原文。
* **`persisted=false` 不能当错误**。改名本身是成功的（面板层 + 主机索引层已生效），
  只是源不可写所以重启 SMP 会回退。把它当错误会让用户看到红字、以为改名失败。
* **只有源 `sce_sys/param.json` 需要备份**。备份在首次改名时写入 `<GAME_TITLE_DIR>/<TID>/orig/param.json`，
  与 icon / bg 的 `tt_backup_to_orig()` 同一套目录约定。
* **别把 cJSON 整包重写拿来用**。真机 `param.json` 是 CRLF + 2 空格缩进，整包重写会把它压成单行 LF，
  用户 diff 游戏包时会看到几千字节的无关变化。定点替换的代价只是一个 512 KB 防呆上限和一个
  「值结束位置」扫描器（要处理 `\"` 与 `\\`）。
* **`test/param_rename_test.c` 的夹具是真机数据**（`_ftp_probe/evidence/recon/*.json`），
  不在仓库里。缺夹具时 `run_host_tests.sh` 会 SKIP 这条，不会半跑。

---
## v0.5.2.131 (CN-FORK)

> **两个「点完按钮却还要自己动手」的缺口。**
> ① 「去 ShadowMount 管理」原来是无条件开新标签 —— 而 ShadowMount 很多时候根本没起，
>    打开就是一块加载不出的空白页，用户以为面板坏了。现在**先探活，没起就拉起、
>    在按钮里等探活转活再打开**，与「常用入口」的灰点条目同一套交互。
> ② 多媒体「全量打包下载」/「打包下载选中」，按钮名里写着**下载**，包却打好就停在那儿
>    等人去找那颗「下载打包文件」按钮。现在**打包一完成就自动开始下载**，
>    那颗按钮保留作兜底（PS5 的 WebKit 偶尔拦非手势触发的下载）。
>
> 为此后端新增一条只读端点 `/smp_status`（主机在本机 loopback 10101 上 TCP 探活）。
> 其余后端路由与全部既有 UI 一行未动。
>
> **出包版** `NayePlmTools_0.5.2.131-me.elf` / `-ga.elf`（提交 `9088433`，内嵌标题真实短哈希
> `9088433`），两包各 10,785,872 字节、titleId 自检 NAYE00001/NAYE00004 通过；
> `frontend/dist/index.html` 914,750 字节，与两颗 ELF 内嵌 blob 逐字节一致（`verify131`
> 29/29）。回归：`game112` 186/186、`nav112` 20/20、`acct111` 23/23、`avatar109` 19/19、
> `cropfix` 75/75、`pack125` 15/15。已推真机并确认 `GET /version` → `0.5.2.131`
> （`.130` 的包已过期）。

### ✨ Added

* **`GET /smp_status` → `{"running":bool,"port":10101}`**（`include/pldmgr.h` 的
  `ROUTE_SMP_STATUS` + `src/http_server.c`）：主机在 `127.0.0.1:10101` 上做 TCP 建连
  探活（400ms 收发超时兜底，与 `pkgmanager_is_running()` 同一套实现）。
  * **为什么必须在本机探、不能交给浏览器直连**：SMP 的 Web UI 在很多机器上只监听
    loopback，电脑端浏览器连不通不代表主机自己连不通；「面板能不能拉起它」这件事
    跟浏览器在哪台机器上无关。探测留在主机侧，前端只读一个布尔值。
  * 面板**只读、不拉起也不关闭** SMP：拉起走上游 elfldr 的 `/loadpayload:`，
    关闭属于 SMP 自己的生命周期，交给它自己或用户手动做。
  * 已加入 `is_quiet_route`（拉起期间前端每 700ms 问一次，不算噪声）。

* **游戏详情「去 ShadowMount 管理」改成先探后开**（`GameDetailModal.jsx`）：
  * 点击后先 `/smp_status` 探活；**已起 → 直接打开**，不等。
  * **未起 → 从 `/list_payloads` 按名字解析 ShadowMountPlus 的 ELF 路径**
    （`normName` + 双向前缀匹配，与 CommonEntriesView 同一套；版本号 / 变体差异
    如 `1.7beta3` / `beta4` 由归一化吸收），走上游 `/loadpayload:<path>` 拉起，
    然后在**按钮里**等探活转活，转活了才把标签页指向 10101。
  * **探活期间按钮显示进度**：`正在检查 ShadowMount 是否已启动…` →
    `正在启动 ShadowMount…` → `已发起启动，正在等待端口就绪… 已等 N 秒`，
    按钮置灰 + 转圈，用户知道在等什么、还该不该等。
  * **失败收口**：`/loadpayload:` 失败或约 31 秒（700ms × 45 次）内端口没起来 →
    关掉空白标签 + 按钮上方出一句红色提示，不留打不开的空白页。
  * **本机没装 ShadowMountPlus** → 不猜程序，照旧按普通链接打开，并提示先去安装。
  * 标签仍走 `.84` 那套：**点击手势的同步栈里**先开 `about:blank`（PS5 老内核里
    异步 `window.open` 会被拦），探活通过才 `tab.location.assign(url)`，
    失败 `tab.close()`；`window.open` **仍不带** `'noopener,noreferrer'`。

* **打包一完成就自动下载**（`MediaManagerModal.jsx`）：`watchJob` 的 `st.done && st.ok`
  分支里插一个隐藏 `<a>` → `click()` → 摘掉，指向与手动按钮同一个
  `/game_media_pack_get?job=`。全量与选中两条路都汇聚到 `startPack`，**一处改动两个按钮都生效**。
  同时弹一句 `打包完成，正在下载（N 项）` 的 Toast。界面上那颗「下载打包文件」按钮
  **保留** —— 程序化下载不一定总能成，按钮是兜底，且包会在槽里保留一段时间。

### 🌐 语言包

新增 `game.smp_checking` / `smp_launching` / `smp_waiting` / `smp_waited` /
`smp_start_fail` / `smp_start_timeout` / `smp_not_installed` / `game.pack_auto_downloading`
（中英双包各 8 条，`zh-CN` 404 → 412 条、`en-US` 402 → 410 条）。

### 📝 注意

* **JSX 内联 fallback 同样要改**：这几个新词条的兜底文案写在
  `GameDetailModal.jsx` / `MediaManagerModal.jsx` 的 `t(key, '默认值')` 第二参数里，
  会被编译进产物，语言包**不能**覆盖它们（`.130` 踩过一次坑，见 SKILL 坑 21）。
  本次两边都改了，并用产物级检索确认新串在 `dist/index.html` 里。

---

## v0.5.2.130 (CN-FORK)

> **`.129` 漏掉的一条死链文案，真机验收时抓到的。** 页面底部那句磁贴提示还写着
> 「点磁贴查看游戏详情（**含删除**）」—— `.129` 已经把详情里的删除按钮撤掉了，
> 这句就成了指向不存在功能的说明。同批修掉两处。
>
> 后端与其余 UI 一行未动，纯文案修订。

### 🐛 Fixed

* **`game.tile_hint` 不再说详情「含删除」**（中英双包）：
  「详情（含删除）」→「详情里只能看信息，挂载 / 卸载 / 删除源请到 ShadowMount 后台」，
  后半句「磁贴上的四颗按钮分别是…」保留不动（`.129` 恢复四颗按钮后仍然准确）。
* **`GameSection.jsx` 文件头注释同步**：「磁贴整块可点 → 游戏详情（含删除源 / 卸载 / 挂载）」
  → 「`.129` 起详情只读，挂载 / 卸载 / 删除源全在 ShadowMount」。

### 📌 仍留在语言包里、但**没有渲染**的键

`game.delete_*` 一整簇（`delete_title` / `delete_confirm` / `delete_ok` / `delete_hint` /
`delete_running_hint` …）`.129` 起已经没有任何 JSX 引用 —— 详情弹窗整条删除链路下线了。
后端 `/game_delete` 端点还在，所以这批文案没有删掉，留着以后真要恢复时能直接用；
只是它们现在不参与界面，**改界面时别看这批词条当基准**。

---

## v0.5.2.129 (CN-FORK)

> **改错地方了，回退重来。** `.128` 把**磁贴下面那四颗按钮**下线换成了「去 ShadowMount 管理」，
> 用户反馈「**你改错了，我要的是将游戏详情里面的按钮全部去掉**」—— 目标一直是**游戏详情弹窗**，
> 不是磁贴。`.128` 的磁贴改动整体回退（四颗按钮与 `tile_hint` 恢复原样），这次改的是详情弹窗。
>
> **详情弹窗现在的样子：纯只读的信息面板 + 一颗「去 ShadowMount 管理」。**
> 原来弹窗底部那排操作按钮 —— 只读挂载 / 读写挂载 / 卸载挂载 / 卸载 / 删除源 —— 全部撤掉，
> 连带删除任务的进度面板与「取消任务」一起下线。挂载、卸载、删除源这三类**会动 app.db 注册表**
> 的操作全部交给 ShadowMount 自己做（它自己有 API 也有界面），我们不再碰。
> 理由同 `.128`：那组按钮里「卸载」正是把 `PPSA21567` 变成孤儿的那颗，
> 而我们没有挂载 / 重注册能力，按下去就是单向门。
>
> 存档 / 截图视频 / 名称与背景**没有跟着撤**：那是我们自己做的功能，ShadowMount 不做。
> 存档与多媒体照旧从详情里的「打开存档管理」「打开多媒体管理」两行走；
> 名称与背景从磁贴上的那颗按钮走。

### ✂️ Removals

* **详情弹窗底部五颗操作按钮全部移除**（`frontend/src/components/game/GameDetailModal.jsx`）：
  只读挂载 / 读写挂载 / 卸载挂载 / 卸载 / 删除源，以及 `canManageSource` / `canUninstall` 两个判据。
* **删除任务那一整条链路下线**：`doDelete` / `doRuntime` / `doUninstall` 三个 handler、
  `busy` 与 `job` 两个状态、`watchJob` / `cancelJob` 的轮询、`.122`/`.123` 加的进度面板与
  「取消任务」按钮、`.123` 加的「本机时钟」失联兜底（`tickAt` / `nowMs` / `staleSec`）、
  以及只被它们用到的 `PHASE_LABEL` / `BUSY_REASON` / `deleteErrorText` / `busyErrorText` 四个表。
  这个文件从 515 行收到 253 行，剩下的全是读与显示。
* **「删除源」那条提示删掉**（`game.delete_hint`，它解释的是已经不存在的按钮）。
  `game.pkg_no_source` 改成只陈述事实（「这是已安装的 PKG，没有物理源文件」），
  原来那句「要移除请用『卸载』」指向的按钮已经不在了。

### ✨ Added

* **一颗「去 ShadowMount 管理」按钮**放在详情弹窗底部（`btn-outline` 满宽，`ExternalLink` 图标）：
  * 目标 `http://<当前 host>:10101/`，host 用 `window.location.hostname` 拼 —— 主机上就是
    `127.0.0.1`，电脑上就是局域网地址，两边都不用配。
  * 沿 `.84` 的 `about:blank` 先行手法（PS5 老内核里 `window.open(url)` 不在点击手势的同步栈里
    就会被拦）。**注意：这个 `window.open` 不能带 `'noopener,noreferrer'`** —— 带了它会返回 `null`，
    于是兜底分支再开一个真标签页，用户一次点出两个 tab（第一颗还停在 `about:blank`）。
    这一条是 `.128` 回归实测踩出来的，代价是新 tab 能反向拿到我们的 window，
    但目标只是 ShadowMount 自家后端，与 `.84` 那条 `CommonEntriesView` 的做法一致。
* **弹窗底部补一句去向说明**（`game.smp_hint`），别让用户找已经不存在的按钮。
* **`.128` 磁贴改动整体回退**：`GameSection.jsx` 的 `TILE_ACTIONS` 四元组、`openSmp`、
  `GameDetailModal.jsx` 里那行「打开名称与背景」、语言包的 `tile_hint` 全部恢复到 `.127` 原样。

### 📝 注意

* **调用方同步收掉了三个回调用不上的回调**：`GameSection.jsx` 传给的 `addToast` / `showConfirm` /
  `onChanged` 已删除（详情不再发起任何操作，也不需要通知列表刷新）。
  `showConfirm` 仍由存档、多媒体两个弹窗使用，没有连带删掉。
* **详情弹窗里「存档」「截图 / 视频」两行仍是可点的** —— 它们渲染成 `<button>`，
  但指向的是我们自己的管理弹窗，不是 ShadowMount 那套。如果这两行也要一起收掉，说一声。
* 「待定功能」那颗磁贴按钮**还在**（`.128` 撤掉它了，`.129` 跟着回退），它弹的还是原来那个占位弹窗。

---

## v0.5.2.128 (CN-FORK) — **已回退，未上机**

> ⚠️ 这一版**改错了地方**：把磁贴下的四颗按钮下线了，而用户要的是撤**游戏详情弹窗**里的按钮。
> 磁贴部分的代码在 `.129` 全部回退；`.128` 的包没有推到真机。
> 下面的内容保留，只为记录那条 `sceAppInstUtilAppUnInstall` 的根因分析（`.129` 的撤按钮理由就是它）
> 和 `window.open` 的 `noopener` 陷阱（`.129` 仍在使用同一套跳转写法）。

### 原条目

> 用户口径：「**修bug 我游戏列表，点击卸载，报错了，刷新页面，游戏图标消失了，想挂载回来就没有可以点的地方了**」，
> 听完原因后的决定是「**算了，不弄了，被下边的按钮功能全部取消，只留一个去 shownmount 管理，然后跳转 showmount 后台**」。
>
> **发生了什么（一句话）：卸载会把 ShadowMount 的游戏从注册表里删掉，但我们没有挂载能力，删了就回不来。**
> `sceAppInstUtilAppUnInstall()` 对一款 ShadowMount 的**裸目录**游戏（`PPSA21567` 宇宙机器人）半失败地执行了：
> 它**删掉了 app.db 注册 + `/user/app/<TID>/`（含 `mount.lnk` / `mount_img.lnk`）**，然后返回非 0 错误码。
> 我们的 `game_runtime_uninstall()` 只在 `rc == 0` 时才 `game_mgr_invalidate()`，于是缓存一直留到 60s TTL / 刷新页面
> 才重算 → 图标凭空消失。外置盘上的源（`/mnt/ext1/PPSA21567-宇宙机器人`）完好无损，ShadowMountPlus 那边也
> 还列着它（`installed=false, source_available=true, can_uninstall=false`）—— 但我们这一侧只剩一张孤零零的
> 空注册，**没有任何按钮能把它挂回去**，因为「挂载/重新注册」从来不在我们手上。
>
> **修法（按用户口径）：不做恢复能力，把挂载类的入口全部撤掉，交给 ShadowMount 自己管。**
> 磁贴下原来四颗动作按钮（存档管理 / 多媒体管理 / 名称与背景 / 待定功能）全下线，换成一颗
> **「去 ShadowMount 管理」**，点它直接跳 ShadowMount 后端（`http://<当前 host>:10101/`，
> 那台机器的 ShadowMount Library Web UI 就挂在 10101 根路径，`GET` 200 / 193,606 字节）。
> 挂载 / 卸载 / 重新注册这些会动 app.db 注册表的操作从此只在那边发生，我们不再碰。

### ✂️ Removals

* **磁贴下四颗动作按钮全部移除**（`frontend/src/components/views/GameSection.jsx`）：
  `TILE_ACTIONS` 数组、四个专用图标（`game/GameActionIcons.jsx` 的 import）以及 `openAction` 分发整段删除。
  「待定功能」那颗本来就是占位弹窗，现在连入口都没有了。
* **换成一颗跳 ShadowMount 后台的按钮**：
  * 目标 URL 用 `window.location.hostname` 拼 —— 在主机上从 `127.0.0.1:8911` 开面板时它就是
    `127.0.0.1`，从电脑用 `192.168.x.x` 开就是局域网地址，**两边都不用配**。端口收进常量 `SMP_PORT = 10101`。
  * 仍用 `.84` 那次的 `about:blank` 先行手法（PS5 老内核里 `window.open(url)` 只要不在点击手势的同步栈里
    就被拦）；空窗开不出来再退化成直接开 url。
  * 布局从 `grid grid-cols-4 gap-1.5` 收成单元素直接铺满 `inset-x-1.5 bottom-1.5`（少一层 DOM，
    单列也不写 grid）；`z-10 + pointer-events-auto` 保留 —— 它压在详情热区之上，点按钮不会顺带打开详情。
  * 中英文案：`去 ShadowMount 管理` / `Manage in ShadowMount`（新 key `game.smp_open`）；
    板块提示 `game.tile_hint` 同步改掉「四颗按钮分别是……」这句过时的话。

### 📝 注意

* **存档 / 多媒体 / 名称与背景三个弹窗都在**，只是入口从磁贴搬到了**游戏详情**里：
  详情多加一行 GridRow「名称与背景」（`GameDetailModal` 新增 `onOpenRename` prop），
  与已有的「打开存档管理」/「打开多媒体管理」两行并列。这三个功能跟挂载/卸载毫无关系
  （只改显示名、封面图、背景图，动的是 app.db 的显示字段，不碰注册与挂载），
  ShadowMount 也不提供 —— 不能因为撤了磁贴按钮就连功能一起弄丢。
* **「待定功能」那颗占位弹窗彻底没了入口**（实例还挂着，但 `modal.kind` 永远不会变成
  `'pending'`，等于惰性死代码；真删要连弹窗组件一起摘，不值当）。
* **详情弹窗里的「卸载」按钮这版没动**（用户只说撤磁贴下的按钮）。对 ShadowMount 的镜像 / 裸目录游戏，
  它仍然是那颗「删掉注册却挂不回」的雷；要不要对它一并收掉，留给下一版决定。

---

## v0.5.2.127 (CN-FORK)

> 用户口径：「**这里都标识错误，你再跟 shownmount 对比一下**」（附两张游戏详情弹窗截图），
> 追问后明确：「**格式没错，但是他显示是已安装 pkg 是错的**」。
>
> **一句话：来源类别判错了，判据用错了字段。** 详情弹窗给一款 `.ffpfsc` 镜像游戏
> （`PPSA17599` 光与影：33号远征队）和一款裸目录游戏（`PPSA01773` FF7 REMAKE）
> 都打了「已安装 PKG」，连带一串字段一起错。
>
> **根因：拿 `a.d_src` 当来源类型。** `d_src` 记的是「这条是从 app.db 还是扫描路径
> **枚举**到的」—— 而 ShadowMount 的镜像/裸目录游戏**同样会注册进 app.db**，于是它们的
> `d_src` 也是 `installed`。真机 15 款游戏里 14 款全中招。连带错的有：
> 物理路径（错回 `/user/app/<TID>`）、`mounted`、`installed_pkg`、`can_manage_source`
> （→ 前端不画「删除源」按钮）、以及 `/game_delete` 的 **403 EPERM**（删源被彻底挡死）。
>
> **对齐参照物不是猜的 —— 是设备上真跑着的 ShadowMountPlus 1.7beta4。** 它的 Web UI
> 就挂在 `192.168.1.214:10101`（`/api/v1/games`、`/api/v1/games/info`），字段语义
> 也照 `1.7beta4` 的 `src/sm_api_service.c` / `sm_filesystem.c` 逐一核对过：
>
> | 字段 | ShadowMountPlus 1.7beta4 | 我们（修前） | 我们（修后） |
> |---|---|---|---|
> | `source_type` | `pkg` / `image` / `folder`（按物理源判） | 一律 `pkg` ❌ | 按物理源判 ✓ |
> | `path`（PPSA17599） | `/mnt/ext1/PPSA17599三十三号.ffpfsc` | `/user/app/PPSA17599` ❌ | 同 SMP ✓ |
> | `mounted`（PPSA01773） | `false` | `true` ❌ | `false` ✓ |
> | `installed_pkg` | `false` | `true` ❌ | `false` ✓ |
> | `can_manage_source` | `true` | `false` ❌ | `true` ✓ |
>
> **`mounted` 的判据换了一个**：SMP 是 `is_data_mounted()` =
> `/system_ex/app/<TID>/sce_sys/param.json` 存在。我们原来用「`runtime_path` 存在
> 且是目录」—— 裸目录游戏的 `mount.lnk` 直接指向源目录，那目录**一直都在**，
> 于是「已挂载」对所有裸目录游戏永远亮着（真机上 `PPSA01773` 就是这么被误标的）。
>
> **没动格式命名**（用户明确「格式没错」）：磁贴与详情继续用扩展名
> `ffpkg / exFAT / ffpfs / ffpfsc`。顺带记录一个**旧文档的错误**：`v0.5.2.74` 写的
> 「与 ShadowMountPlus 一致的取值 `ffPKG/exFAT/ffpfs/ffpfsc`」是把**扩展名**当成了
> SMP 的展示名 —— SMP 的 `image_type` 其实是文件系统名（`ufs` / `exfatfs` / `pfs` /
> `pfsc`，见其 `sm_image.c` 的 `image_fs_name()`）。我们的扩展名更好认（就是磁盘上的
> 文件名），所以保留，但**别再声称它与 SMP 一致**。

### 🐛 Fixes

* **来源类别改为按物理源判定**（`src/game_mgr.c` 新增 `gm_class_of_fmt()`）：
  `mount_img.lnk` 命中镜像扩展名 → `image`；`mount.lnk` → `folder`；两者都没有 → `pkg`。
  判据与 `gm_fmt_size()` 完全同源（直接由 `fmt` 推），不会两处漂移。
* **`gm_paths()` 不再因「是 app.db 枚举到的」就回 `/user/app/<TID>`**：物理源优先级改为
  镜像文件 > `mount.lnk` 的裸目录 > 扫描根下的 dump 目录 > app 目录。签名从 `src` 换成
  `cls`（类别）。
* **`mounted` 改用 ShadowMountPlus 的判据**（新增 `gm_is_data_mounted()`）：
  `/system_ex/app/<TID>/sce_sys/param.json` 存在；内置 PKG 恒 `false`。
* **`installed_pkg` / `can_manage_source` 改由类别推**（`installed_pkg = (cls=="pkg")`，
  `can_manage_source = !installed_pkg`）。于是镜像/裸目录游戏重新拿到「删除源」按钮。
* **`/game_delete` 的 EPERM 闸门改判类别**（`src/game_runtime.c`）：原来 `src=="installed"`
  就 403，ShadowMount 的镜像/裸目录游戏**永远删不掉源**；现在只有真·内置 PKG 才 403。
* **`game_mgr_source()` 回的是类别而不是枚举出处**（`src/game_mgr.h` 契约注释同步）。
* **`/user/app` 与 `/system_ex` 做成可覆盖宏**（`GAME_MGR_APP_BASE` / `GAME_MGR_SYSTEM_EX`，
  手法同 `GAME_MGR_APPDB_PATH`）—— 否则这条判定在主机上只走得到「没有 lnk → pkg」那一支，
  而真机炸的正是另外两支。
* **详情兜底安装时间加一跳**：先看物理源 mtime，再退回 app 目录（dump 游戏没有 app 目录，
  别把它的安装时间弄丢）。
* **前端 `GameDetailModal.jsx`**：来源类型改由 `source_type`（后端）判，兜底按 `fmt` 判
  （**不再按 `src`**，否则弹窗打开那 200ms 会先闪一个错的「已安装 PKG」）；
  「没有可删物理源」那句与卸载确认文案改用 `installed_pkg`，不再用 `src`。
* **`frontend/mock-server.js`**：`buildDetail()` 的来源类别 / 物理路径 / 运行时路径 /
  `mounted` / `installed_pkg` / `can_manage_source` 全部跟后端改成同一口径 ——
  桩不跟着改，回归验的就是旧口径，改坏了也看不出来。

### 🧪 Tests

* `test/make_appdb_fixture.py`：夹具从 2 款扩到 **3 款**，刚好覆盖三个类别 ——
  `PPSA00001` 镜像（造 `mount_img.lnk`）、`CUSA00002` 内置 PKG（两个 lnk 都不造）、
  `PPSA00003` 裸目录（造 `mount.lnk`）+ **造 `/system_ex` 挂载点**（唯一 `mounted=true`
  的样本）。顺带把 `<root>` 整棵清掉再建（残留的 lnk 会让断言时红时绿）。
* `test/run_host_tests.sh`：game_mgr 那条加上 `-DGAME_MGR_APP_BASE` /
  `-DGAME_MGR_SYSTEM_EX`。
* `test/game_mgr_test.c`：**65 → 90 条**。新增 ⑤b（内置 PKG）/ ⑤c（裸目录 + 已挂载）
  两块；`game_mgr_source` 那组从「src 该是 installed」改成逐类别断言
  （image / pkg / folder 各自的 `src` 与 `path`）。
* **新增 `test/mutation_check_game_mgr.sh`**（4 个变异体，全过）：
  `srcdsrc`（退回看 `a.d_src`）8 条红 / `imgtype` 2 条 / `pathimg`（掐掉 mount_img.lnk
  那支）2 条 / `mounted`（退回「runtime_path 存在即已挂载」）1 条。
  这一层是专门防「夹具没造那两个 lnk，断言其实是空转」的。
* `game112.mjs`：**177 → 185**。新增 2b（内置 PKG：来源类型 / 那句说明 / 无删除源）
  与 2c（裸目录 + 「已挂载」徽标 + 有删除源）两块；第 2 块改成断言镜像游戏
  **显示「镜像」、有「删除源」、物理路径是镜像文件、且不再谎称「这是已安装的 PKG」**。

### ⚠️ 已知 / 未做

* 磁贴左上角那颗「已安装 / 转储」徽标仍按 `src` 判（= 从哪枚举到的）。ShadowMount 的游戏
  确实注册在 app.db 里，那个语义下「已安装」是对的，且它没声称自己是 PKG —— 保持不动。
* `can_uninstall` 仍取 app.db 的 `uninstallable` 列，未与 SMP 的 `can_uninstall = installed`
  对齐（两者在真机上目前一致）。

## v0.5.2.126 (CN-FORK)

> 用户口径：「**打包的多媒体，压缩包下级文件夹是 游戏编码-游戏名称 这个文件夹里面
> 直接就是截图和视频**」。
>
> **① 压缩包内的布局整个换了。** `.125` 及以前是把 `content_path` 去掉 `/av_contents/`
> 当条目名 —— 也就是原样复刻磁盘那套
> `photo/NPXS40087/PPSA21567/230/20261006_130015_00977626.webm`。那是**我们的实现细节**，
> 对用户是噪音（一堆生成器 ID 与分片十六进制目录），还会把同一批截图拆进不同目录。
> 现在统一是一层：
> ```
> PPSA21567-宇宙机器人/
>   ├─ 宇宙机器人-20261006_125910.png
>   ├─ 宇宙机器人-20261006_130015.webm
>   └─ …
> ```
> 文件夹名 = `游戏编码-游戏名称`（取不到游戏名就只用编码），里面的文件名与
> **单独下载**那份同源（同一个 `md_dlname`）—— 压缩包里外两套名字只会让人对不上号。
>
> **② 摊平就必须承担撞名的风险 —— 所以加了去重。** 下载名只精确到秒
> （`游戏名称-YYYYMMDD_HHMMSS.ext`），原文件名里那截随机数被丢掉了：**同一秒连拍两张
> 必然同名**。而 zip 里两条同名条目在 Windows 资源管理器里解压是**互相覆盖** ——
> 那是静默丢文件，比名字难看严重得多。现在撞了就补 `-2` / `-3`…，只有真的撞上才动手，
> 常见情况仍是干净的名字。
>
> **③ 文件夹名按文件名的规则清一遍。** 游戏名里带冒号是常态（「蜘蛛侠：迈尔斯」），
> 不清就会解出一个奇怪的目录。另按 **UTF-8 边界**兜了长度上限 —— 不能把一个汉字
> 劈成半个，也不能让 `folder + "/" + dlname` 撑爆 `zip_ent_t.zname[512]`。
> 条目名本来就带 UTF-8 标志位（general purpose bit 11，见 `src/zip_io.c`），
> 所以中文目录名在 Windows 上是正常的。
>
> **④ 夹具的 mtime 钉死了。** 条目名用的是文件**真实 mtime**，以前夹具的文件是
> 「写到哪算哪」，于是每次跑出来的名字都不一样（`…-20261007_001728.png` 这种）——
> 「同一秒两张要去重」这条根本没法断言（会时红时绿，比不测更糟）。现在 `put()`
> 会把 mtime 按文件名里那个时刻钉死，顺带让下载名与原始文件名里的时刻终于对得上。

### ✨ 变更

* **压缩包条目名改为 `<游戏编码-游戏名称>/<游戏名称-时间戳>.<ext>`**（`src/game_media.c`
  的 `md_build_entries`）。只有一层目录，截图与录屏直接躺在里面。
* **新增 `md_zname_unique()`**：同一秒的同名条目补 `-2` / `-3`…，杜绝解压时互相覆盖。
* **新增 `md_utf8_cap()`**：按 UTF-8 字符边界截断文件夹名里的游戏名（上限 120 字节），
  防半个汉字与缓冲区截断。

### 🧪 Tests

* **`media_scan_test.c` 163 → 178 条**：新增一组「压缩包内目录结构」断言
  （顶层文件夹出现次数、旧布局 `photo/…` / `video/…` 必须为 0、文件夹下**直接**跟文件名），
  以及一组「同一秒连拍两张必须去重」的断言。
* **`media_verify.py` 的 zip 模式重写**：独立复算一遍条目名（含 `md_sanitize_name` /
  扩展名小写 / mtime 本地时间戳 / 去重补号），并断言结构性质 ——
  每个条目名**恰好一个 `/`**、所有条目**共用同一个顶层文件夹**、**没有重名条目**。
* **夹具新增 `PPSA99012`**（同一秒、不同随机数的两张）+ `put()` 钉 mtime；
  主测试游戏 `PPSA99008` 的条数不变，不动既有计数。
* **变异测试新增两条**：`noplain`（退回平铺布局 → 7 条红）、
  `nodedup`（掐掉去重 → 2 条红）。`==== 变异测试: PASS ====`（5/5）。

### ⚠️ 已知 / 下一步

* 目录结构变了，但**老包不会自动变** —— 已经下过的 zip 保持原样。
* 仍待办：文件直出的 Range 支持、任务槽看门狗（见 `.125` 那两条）。

### ✅ 真机验收（2026-10-07，已通过）

推包（`GET /shutdown` → Python socket 灌 `192.168.1.214:9021` → `/version` 回 `0.5.2.126`）后：

* **成品包结构校验 `device126_pack_check.py` 全 PASS**（走面板自己的接口起任务、取成品包，
  再用标准库 `zipfile` 逐条验）：真机上解压出来就是
  ```
  PPSA21567-宇宙机器人/宇宙机器人-20261006_125910.png
  PPSA21567-宇宙机器人/宇宙机器人-20261006_125918.png
  PPSA21567-宇宙机器人/宇宙机器人-20261006_130015.webm
  ```
  条目数 == 列表条数、**没有重名条目**、每个名字**恰好一个 `/`**、所有条目共用同一个
  顶层文件夹、条目名 == `<编码-名称>/<列表里的 dlname>`（即与单独下载同源）、
  每条内容与 `/game_media_file` **逐字节一致**、CRC 全对。
  包名 `宇宙机器人-20261007_002525.zip`（61,518,706 字节）。
* **UI 回归 `device125.mjs` 12/12 PASS**（视频封面真帧、打包轮询 3 次、下载按钮、下载端点
  200/61,518,706 字节）—— 说明这一轮没碰坏 `.125` 修好的那两件事。
* 出包核对：两个 ELF 各 10,785,872 字节、titleId NAYE00001/NAYE00004、
  内嵌标题 `(7ccc1e5, built at 2026-10-06 16:23:06 UTC)` 非 DEV、
  `.110`–`.125` 残留 0、内嵌页与 `frontend/dist/index.html`（918,824 B）逐字节相同。

## v0.5.2.125 (CN-FORK)

> 起因还是那两句「**打包下载还不好用。视频封面也没有。**」，而且这是第三轮。
> 所以这一版**不再猜第三次**，改成自己去真机取证 —— 走 FTP（`192.168.1.214:2121`）
> 拉日志、拉目录树、拉 `av_content.db`，再用 HTTP 直接拉一次成品包。
> 取证结果把前两轮的判断**推翻了一半**。
>
> **① 「打包下载」的真因不在后端 —— 后端一直是好的。**
> 真机日志（`/data/nayeplmtools/pldmgr.log`）写得很清楚：
> `pack measured 61518012 bytes / 3 files (+1ms)` → `pack writing … .part` →
> `pack job done -> …/宇宙机器人-20261006_231804.zip (61518012 bytes, +1179ms)`。
> **1.2 秒就打完了。** 成品在盘上 61,518,688 字节；用 HTTP 取它，
> `Content-Length` 正确、**0.5 秒完整收全 61,518,688 字节**、`zipfile.testzip()` 无错、
> 三个条目尺寸都对。也就是说「打包」和「下载」这两件事本身**从头到尾都是通的**。
>
> 挂在**前端**：构建产物里 `watchJob` 的最后一行是 `setTimeout(tick, a)`，而 `tick`
> 是一个**悬空标识符**。原因是构建 target 是 `['es2015','safari12']`，es2015 没有
> async，Babel 要把 `async` 降级成 generator 包装；对**自引用的 const 箭头函数**
> （`const tick = async () => { … setTimeout(tick, …) … }`），Babel 会把整个
> `const tick = …` 换成一个**立即调用并丢弃的 IIFE**，却**不重写函数体里的那个自引用**。
> 于是第一次 `/game_job_status` 正常返回，紧接着 `setTimeout` 抛
> `ReferenceError: tick is not defined`，**轮询永久停摆**。
> 界面停在「正在打包… 0%」，「下载打包文件」按钮永远不出现 —— 这正是用户截图里的
> 「一直是正在计算中」，也正好解释了为什么日志里**只有 `status poll #1`、一条
> `pack_get` 都没有**（前端压根没走到点下载那一步）。
>
> 修法：**自引用的 async 函数一律写成函数声明**（名字绑在外层作用域，降级后仍有效）。
> `App.jsx` 里既有的 `async function poll()`（自动加载轮询）本来就是这么写的、真机上
> 一直是好的 —— 这是同一个工程里现成的反证。同类隐患一并修掉：`StorageHub.fetchRemote`
> （`await fetchRemote(true)` 自引用）、`App.refreshPayloads`（失败重试里自引用）。
>
> **② 为什么 177 项回归全绿却漏了它 —— 回归体系的盲区。**
> `game112.mjs` 跑的是 **vite dev server**，发出去的是原生 ESM，`async/await` 不降级；
> 而真机跑的是 Babel 降级后的**构建产物**。这条 bug 只存在于后者。
> 所以这一版补了 `serve_dist.mjs`（dist 静态 + 反代到 mock）与 `pack125.mjs`
> （跑在产物上：断言轮询 ≥2 次、面板进终态、下载按钮出现、下载端点真的回字节）。
> **已验证它能抓住这个 bug**：把修复回退、重新构建后该回归 5 项红，报错正是
> `ReferenceError: tick is not defined`，面板文本停在「正在打包… 正在计算体积… 0%」。
>
> **③ 视频封面：PS5 给的是 JPEG XR，载荷解不了。**
> 真机取证拿到了三样东西：目录树、`av_content.db`、以及封面文件本身。
> 录屏封面在 `/user/av_contents/thumbnails/video/<生成方>/<TID>/<hex>/<名>.webm.jxr`，
> 文件头 `49 49 BC 01` —— **JPEG XR**（1920×1080 24bppRGB，约 75:1，DCT 编码）。
> 载荷里只有 stb_image，**解不了 jxr**，所以三轮都只能回占位图。
> 照片那条路没事，因为照片封面是 `.png.jpeg`（普通 JPEG）。
> 这一版不往载荷里塞 JXR 解码器，改成**让浏览器自己从 webm 取一帧** ——
> 视频格子直接渲染 `<video preload="metadata">`，用户要的本来就是「视频里面某一帧画面」。
> 真机实测（ASTRO BOT 那条 43MB 的 webm）：`readyState=4`、3840×2160，
> 画到 canvas 取像素 avg=57 / max=242 —— **是有画面的首帧，不是黑帧**。
>
> **④ 顺手修掉 mock 的一处「假绿」。** mock 的下载端点用中文文件名直接
> `res.setHeader('Content-Disposition', …)`，Node 遇到非 ASCII 会抛 `ERR_INVALID_CHAR`、
> express 回 500 —— 于是 mock 上「下载」这条路**从来没被真正跑通过**。
> 这不只是测试缺口：真机那个 bug 本来「点一下下载」就能立刻暴露，是被这里先 500 掩盖掉的。
> 改成与后端 `add_disposition` 同口径的 ASCII 兜底 + RFC 5987 `filename*` 双写。
>
> 出包：`NayePlmTools_0.5.2.125-me.elf` / `-ga.elf`（`-Os -Wall` 零警告）。
>
> **⑤ 已实机（PS5）验收 —— 12/12 PASS。** 这是本项目第一次「推包 + 自测 + 出结论」
> 全流程自己走完：`GET /shutdown` 停旧 → Python socket 灌 `192.168.1.214:9021`（elfldr；
> 环境里没有 socat/docker，故不走 `deploy.sh`）→ `/version` 回 `0.5.2.125`。
> 验收脚本 `device125.mjs` **跑在真机内嵌的那份构建产物上**（既含 Babel 降级产物、
> 又接真后端，比 `pack125.mjs` 更接近用户实际看到的东西）：
> - **打包**：轮询 **3 次**（`.124` 是 1 次就停）、面板进终态「打包完成」、出现
>   「下载打包文件（3 项）」；全程 `pageerror` **0**，尤其没有 `tick is not defined`；
>   下载端点 HTTP 200 / `Content-Length=61518688` / 实收 61518688 / `PK` 魔数。
> - **封面**：网格 `video=1 / img=2`，`readyState=4`、`3840×2160`、`duration=7.1`，
>   canvas 采样 `avg=57 / max=242` → **是有画面的真帧**，不是占位图也不是黑帧。
> - 截图：`D:/pldmanager/.shots/device125/{panel,pack}.png`。

### 🐛 Bugfix

* **`watchJob` 的轮询不再在第一次之后停摆。** 自引用的 async 函数改写成函数声明
  （`const tick = async () => …` → `async function tick()`），绕开 Babel 在
  `es2015/safari12` 目标下对自引用 const 箭头函数的降级缺陷。
  症状：打包任务跑完但界面永远停在「正在打包…」、下载按钮不出现。
* **`StorageHub.fetchRemote` / `App.refreshPayloads` 同类隐患一并修掉。**
  两者都在函数体内自引用（前者 `await fetchRemote(true)` 的 24h 过期刷新，
  后者失败重试 `refreshPayloads(retryCount + 1)`），同样写法会在降级后变成悬空标识符。
* **视频封面改为从 webm 取真帧。** 视频格子渲染
  `<video src="/game_media_file?id=…" preload="metadata" muted playsInline>`，
  直接显示首帧；取不到（浏览器不支持该编码 / 文件读不出来）时 `onError` 记下 id，
  退回后端占位图。**刻意不 seek**：载荷的文件直出没有 Range 支持，Chrome 会把该媒体
  判成不可定位（实测 `currentTime` 设不动、`seekable` 为空），写 seek 只是死代码。
* **mock 的 `Content-Disposition` 改为 ASCII + RFC 5987 双写**（新增 `disposition()`），
  与后端 `http_server.c` 的 `add_disposition` 同口径。原先中文名直接 `setHeader`
  会抛 `ERR_INVALID_CHAR` 回 500。

### 🧪 Tests

* **新增 `pack125.mjs`（跑在构建产物上的打包回归）。** 判据：无未捕获页面异常、
  `/game_job_status` 轮询 ≥2 次、面板进入终态、出现「下载打包文件」按钮、
  下载端点真的返回 200 且有字节。**跑法**：`node mock-server.js`（8081）+
  `node serve_dist.mjs`（5200，dist 静态 + 反代）+ `node pack125.mjs`。
  它是本仓库第一条**针对构建产物**的浏览器回归 —— `game112.mjs` 那条路永远看不到
  「构建期改写」引出的问题。
* **新增 `serve_dist.mjs`**：把 `frontend/dist/index.html` 起成可联调的服务，
  其余请求反代到 mock。存在的理由就是上面那条盲区。
* **新增 `device125.mjs`（真机验收）**：打真机 `192.168.1.214:8911`，12 条断言 ——
  真机版本号 / 视频格子渲染 `<video>` 且真的解出帧（`videoWidth>0` + canvas 采样非全黑）/
  打包轮询 ≥2 次 / 无未捕获异常 / 无 `tick is not defined` / 出现下载链接 /
  面板进终态 / 下载端点 200 + 字节数对齐 + `PK` 魔数。**12/12 PASS。**
* `game112.mjs` 177 / 177 通过（本轮改动没有影响既有行为）。

### ⚠️ 已知 / 下一步

* **文件直出没有 Range 支持。** 直接后果：`<video preload="metadata">` 只能显示首帧
  （不可 seek），而且浏览器会把整个录屏拉完（43MB 那条实测如此）。
  加 Range 支持能一次解决三件事：封面能 seek 到更代表的一帧、封面只需拉几百 KB、
  视频预览拖动进度条可用。**本轮没做**，因为它要动 `gr_file_response` 这条
  被打包下载复用的路 —— 刚把下载修好，不宜在同一版里再动它。
* **打包看门狗（真正释放任务槽的那个）仍未实现**，设计留在 HANDOVER §7。
  本轮已确认「打包本身不会卡」，所以它的优先级下降，但「任务槽被一个真卡住的任务占住」
  这条风险还在。

## v0.5.2.124 (CN-FORK)

> 起因还是真机两句话：「**打包下载还不好用。视频封面也没有。**」
> 这是同一对症状的**第三轮**，而且第二轮（`.123`）在封面那条上**做错了**，得先认账。
>
> **① 视频封面 —— `.123` 的两处错，真机表现是「格子全白」（比占位图还差）。**
> `include/game_media.h` §7a 白纸黑字写着「录屏目录层级是否与 photo 同构」**尚未在
> 真机验证过**（当年取证时机器上只有截图），而 `.123` 恰好把封面帧目录**按 id 推**了
> 出来 —— 拿一个没验证的假设当事实用。第二处更隐蔽：只按**扩展名**放行候选，
> 于是可能挑中一个「名字像 JPEG、内容不是」的文件，它一路传到 `stbi_load_from_memory`
> 失败 → 返回 `-2` → HTTP 404 → **格子从「占位图」退化成「一块空白」**。
> 修法：① 先扫 **`dirname(icon_path)`** —— 那是**媒体库自己写进库里的**，指哪儿就是哪儿，
> 是最权威的答案；按 id 推的目录只作兜底。② 按**文件头魔数**认位图（JPEG/PNG/BMP/GIF），
> 扩展名只用来排除 `.jxr`。③ 视频**永不 404**：取不到真封面也必须回占位图。
>
> **② 打包下载 —— 这一条**找到了一个能证实的独立成因**，不是「卡住」那个。**
> `zip_write` 是**直接写成品名**的，写到一半时成品路径上就是一个 `size > 0` 的**残包**；
> 而 `game_media_pack_open` 只看「文件在不在、大小 > 0」—— 一次并发（或用户手快重按
> 下载）就会把半截包当成品发出去，浏览器拿到的是坏 zip。而且失败/取消的任务留下残包后，
> 用它的 job id 照样取得到。修法：zip **先写 `.part` 再 `rename`**（同文件系统内原子，
> 成品路径上要么完整要么没有），并且 `game_media_pack_open` 加一道 **`gj_ready` 门禁**
> —— 任务必须**已成功收尾**才允许取成品。
>
> **③ 「打包卡住」的根因本轮仍未定位，如实记在这里。** `.122` 修了三处、`.123` 修了一处
> （`pldmgr_log` 落盘移出 `log_mutex`），真机上是否还复现**还没有证据**。所以这一版
> 不猜第五次，而是把**取成品这条路上的每一步**也写进日志（见下），让下次复现自己说话。
>
> 出包：`NayePlmTools_0.5.2.124-me.elf` / `-ga.elf`（`-Os -Wall` 零警告）。

### 🐛 Bugfix

* **视频封面帧改从 `icon_path` 所在目录取，并按文件头魔数认位图。**
  新增 `md_magic_image()`（只读 12 字节，认 JPEG/PNG/BMP/GIF）、
  `md_scan_cover_dir()`（在给定目录里找 `<原文件名>.<后缀>` 且**真能解码**的那一份）、
  `md_find_cover(id, icon, …)`（先扫 `dirname(icon_path)`，再兜底按 id 推）。
  另外补掉一个洞：`md_thumb_src` 里 `md_abs()` 失败时**无条件**回 `-1`，视频跟着走了
  404 那条路 —— 现在也按「是不是视频」分流，视频一律回 `-3`（占位图）。
* **成品 zip 改成「先写 `.part`、写完 `rename`」**，`md_pack_run` 里发布；
  打包目录的清理扫描同时收掉 `.zip` 与 `.part`（上一次被打断留下的临时名）。
* **`game_media_pack_open` 加状态门禁（新增 `gj_ready`）。** 返回值由两态细分三态：
  `-1` 任务不存在 → 404；`-2` **还没成功收尾** → 409（以前也回 404，于是「打包途中点
  下载」看起来像「任务凭空消失」）；`-3` 成品没了/0 字节 → 404。
* **占位图的缓存键不再可能和真封面串图。** `md_thumb_src` 最后那个 `-3` 分支
  忘了清 `st`，于是占位图的缓存键用的是**视频文件自己的 mtime**；同一个脚本先后写的
  文件 mtime 相同，占位图与真封面就会互相串（主机测试里当场复现过）。

### 🔍 可诊断性

* **`/game_media_pack_get` 每一步都落日志**：任务没到时写
  `pack_get job=N not ready (state=running/failed/cancelled)`，成品打不开写
  `pack_get job=N open failed: <path>`，正常发出写
  `pack_get job=N serving <N> bytes (<path>)`。
  配上 `.123` 已有的 `pack start / measured / writing / job done` 与 `STALL` 看门狗行，
  这条路上就再没有「看不见的下一步」了。
* **封面诊断按 id 去重。** 媒体面板一打开就给每个格子取缩略图，而「没有可解封面帧的
  视频」每次取都会写三条诊断 —— 不挡一下，足以把日志环形缓冲里真正要紧的 `STALL` 行
  挤掉。现在同一个 id 只报一次（留最近 8 个 id 的哈希；重启 payload 后能再报一次）。

### 🧪 Tests

* **`media_scan` 147 → 163 条。** 四处新增：
  ① 夹具改成**缩略图目录与录屏目录刻意不同构**（缩略图少一层生成器），并先断言
  「按 id 推出来的那个目录**不存在**」—— `.123` 的夹具把两者摆成同一个目录，
  所以它把**错的假设**测成了绿的；
  ② 新增「诱饵」夹具：同目录只有一份**后缀像 JPEG、内容不是**的假图，断言跳过它回占位图
  （不是 404），并断言诊断日志真的写了目录内容；
  ③ 断言封面诊断**同一个 id 只报一次**（否则每个格子每次取图都刷三条，会淹掉 `STALL` 行）；
  ④ 打包：任务跑的同时**高频取包**，断言「未完成必须 `-2`、完成后字节数恒定」，
  且打包目录不留 `.part`。
* **新增 `test/mutation_check_media.sh`（变异测试）。** 把三处关键改动**逐条掐掉**，
  确认测试**真的会红**：掐掉 `icon_path` 那条路 → 2 条断言红；掐掉魔数校验 → 5 条红；
  掐掉 `gj_ready` 门禁 → 1 条红。**这一课是用两轮真机返工换来的** —— 用例只要不断言
  关键性质，就会把错的假设测成绿的。出包前手动跑（不接进 `run_host_tests.sh`，偏慢）。



> 起因还是真机一句话 + 一张截图：「**一直是正在计算中**，同时视频的预览图不对，
> **应该是视频里面某一帧画面**」。两件事分开说：
>
> **① 视频预览图** —— 这条是上一版（`.122`）判据不够。真机缩略图的命名约定是
> `<原名>.<原ext>.<jxr|jpeg>`（`include/game_media.h` 头注释），同一个录屏旁边
> **可能同时躺着两份封面帧**：一份 HDR 的 `.jxr`、一份普通 `.jpeg`。库里 `icon_path`
> 只指其中一份，指到 `.jxr` 时 stb 解不动，`.122` 于是回占位图（深底 + 播放三角）——
> 用户看到的正是「预览图不对」。现在会去**同一个缩略图目录**里翻一份 stb 认得的位图。
>
> **② 「一直是正在计算中」** —— 这一条**本轮没能定位到根因，如实记在这里**。
> `.122` 已经修掉三处「任务自己卡死」的成因（stdout 阻塞 / `zx_write_all` 0 字节自旋 /
> 轮询被回调异常带走），真机上仍然复现，说明还有第四处。截图里的证据是
> `体积 0 B / 58.7 MB`、`文件 0 / 3`、`用时 0 秒`、`正在计算体积…`：总量已经报出来了
> （走过 `gj_set_totals`），但**此后再没有任何一次成功的轮询**——而 `transient` 那句
> 「与主机失联，正在重试…」又没出现，说明轮询不是「一直在失败」，而是**停了**。
> 光靠读代码已经分不出「连接被堵」和「前端定时器没再走」，所以这一版不再猜第二次，
> 改成**让下一次复现自己把话说清楚**（见下）。
>
> 出包：`NayePlmTools_0.5.2.123-me.elf` / `-ga.elf`（`-Os -Wall` 零警告）。

### 🐛 Bugfix

* **视频预览图现在是**视频里的真画面**，不再是占位图。**
  `icon_path` 那份解不动（或压根没给）时，去同一个缩略图目录里按
  `<原文件名>.<后缀>` 找一份 stb 认得的位图（新增 `md_find_cover`，排在「拿原图当
  缩略图」**之前** —— 封面帧本来就是为缩略图准备的，而原图是 4K 整图，解它又慢又占内存）。
  目录由 `id` 推出来，**不依赖库里那几个 `title_id` 列**（列序在固件之间会变），
  而 `id` 本身就是 `content_path`，最可靠。找不到才回占位图 —— HDR-only 的录屏
  仍然走占位图，这条性质没变。
* **`pldmgr_log` 的落盘不再挂在 `log_mutex` 上。** 这是本轮唯一一处**能证明**的隐患：
  原版把 `fopen/fprintf/fflush` 全放在 `log_mutex` 里（注释还写着「调用方已持
  `log_mutex`」），而 `log_mutex` 是所有 `pldmgr_log` 的必经之路，`pldmgr_log` 又长在
  后台打包 worker 的路径上 —— **一次慢盘写 = 全进程的日志一起堵**，打包线程会停在
  某一行日志上不再往下走。这与 `.122` 修掉的 stdout 那条是**同一类**的错：把一件可能
  无限期阻塞的 I/O 放进了别人必须经过的临界区。现在环形缓冲与 SSE 通知留在
  `log_mutex`（纯内存、快），磁盘那份挪到锁外由独立的 `file_mutex` 串行 ——
  最坏只是日志文件落后，绝不再拖住任何人。

### 🔍 可诊断性（本轮重点）

> 三个月的教训是：**「卡住」这种问题，界面能提供的信息量决定了下一轮能不能定位。**
> `.122` 的截图之所以有价值，是因为上面有 `0 B / 58.7 MB` 和 `用时 0 秒`；
> 而这一张只剩「正在计算中」一句，因为**界面定住的时候，界面本身已经不再更新了**。
> 所以这一版把证据挪到**不经过前端轮询**的地方（内存环形缓冲 → 界面「日志」页；
> 以及 `/data/nayeplmtools/pldmgr.log`）。

* **看门狗线程：任务不动就自己往日志里写一行 `STALL`。** 起一条极轻的守护线程
  （5 秒一轮、每轮只取一把锁读几个字段），任务活着时一旦「距上次有进展」超过 15 秒，
  就写 `media: STALL job=<id> phase=<阶段> idle=<n>s bytes=<done>/<total> files=<done>/<total>`，
  同一段无进展每涨 30 秒再报一次（不刷屏）。**下次复现只要把那一行发过来**：
  `phase` 是 `preparing` / `measuring` / `packing` 分别指向三个完全不同的下一步。
  只在第一次真的起了打包任务时才创建，只看看游戏列表的人不会被多起一条线程。
* **阶段日志带上单调毫秒。** `pack start` / `pack measured` / `pack writing` / `pack job done`
  四行各带 `+Nms`（`CLOCK_MONOTONIC`，不受系统改时间影响）。既定位卡在哪一段，
  也顺带分清「慢」与「卡」：扫库 40 秒是慢，扫库 400 秒是卡。
* **轮询心跳。** `/game_job_status` 在 `is_noisy_route()` 里是降噪的（1s 一次不能每发都记），
  于是真机上「界面定住」时日志里**看不出前端到底还在不在轮询**——而这恰恰是最关键的
  分岔。现在按「首个 + 每 20 个」记一条（约 20 秒一行）。
* **前端：面板自己也会说「多久没收到主机的新数据」。** 后端的 `idle` 是**主机那边**的尺子，
  主机时钟一跳（或槽里的时间戳落到未来）`difftime` 就是负数、被钳成 0，界面于是永远
  显示「用时 0 秒、无任何异常」。现在多挂一把**本机时钟**的尺子：记录最近一次**真的
  收到**主机数据的时刻（`transient` 的重试不算），超过 15 秒就显示
  「已 N 秒没有收到主机的新数据」。两把尺子谁先发现谁报。两个任务面板
  （`MediaManagerModal` / `GameDetailModal`）同构 —— `.122` 的教训就是同一个 bug 得修两遍。

### 🧪 Tests

* **`media_scan` 138 → 147 条。** 新增夹具：录屏的封面帧**两份都在**
  （`icon_path` 指 `.jxr`、同目录另有一份可解的 `.jpeg`），断言缩略图落到**真画面**上。
  判据是**画面内容**而不是「解开了」：真封面画的是左红右蓝的渐变，占位图是深底 +
  白色播放三角 —— 两条互斥，且「解开了」这件事给张纯黑图也能过。
  实测取样 `左=(229,23,23) 右=(27,24,229)`。
  HDR-only（没有 `.jpeg` 兄弟）那条仍然钉着**占位图**，两条路都覆盖。


> 起因是真机一句话 + 一张截图：「**截图、视频打包一直卡在这个页面**，并且视频没有
> 预览图片。截图下载和视频下载的命名方式是游戏名称-截图时间。压缩包也是游戏名称-打包时间戳」。
>
> 那张截图里的证据是决定性的：`体积 0 B / 58.7 MB`、`文件 0 / 3`、`用时 0 秒`，
> 进度条是一条约 1/3 宽的脉冲。四个诉求各有确定的成因，本轮逐个修掉。
>
> 出包：`NayePlmTools_0.5.2.122-me.elf` / `-ga.elf`（`-Os -Wall` 零警告）。

### 🐛 Bugfix

* **「打包一直卡住」其实是三处独立成因，逐个修。**
  1. **`zx_write_all` 会死循环。** POSIX 允许 `write()` 在 `n>0` 时返回 0，而
     `b += w; n -= w;` 在 `w==0` 时**两个变量都不动** —— 纯自旋：CPU 打满、
     一个字节不落盘、任务槽永远停在「已开始、0 字节」。常规文件上罕见，但真机是
     索尼自家的文件系统，不能拿 ext4 的行为当保证。现在 `w==0` 直接判失败。
  2. **`pldmgr_log` 的 `printf` 会永久阻塞。** payload 的 stdout 是启动器给的
     socket / 管道，对端不排空它；缓冲一满 `printf` 就不返回。而调用它的线程里
     正好有后台打包线程 —— 于是现象是「任务槽已经报出总量 58.7 MB，此后一个字节
     都不动」，与截图逐字吻合。改为 `poll(POLLOUT, 0)` 先问一句，不可写就丢掉
     stdout 这一份（内存环形缓冲给 `/logs`+SSE、落盘 `pldmgr.log` 都照旧，
     排查能力一点没少）。
  3. **前端轮询会被回调拖死。** `watchJob` 里 `onTick(st)` 裸写在 `try` 里，
     `catch` 里又调一次 —— 第二次再抛就从 async 函数里逃出去，底下那行
     `setTimeout(tick, …)` 再也执行不到，**轮询当场死掉**：界面永远停在第一次 tick
     的那份数字上（`0 B / 58.7 MB`、`用时 0 秒`、进度条定住），而且**一条报错都
     不显示**。现在 `onTick` 一律包一层；另外连续失败累计 60 秒就收工并如实告诉
     用户「与主机失联」，不让面板无限期吊在「正在打包…」上。
* **进度条会「三成满 + 右边写 0%」。** `indeterminate` 挂在 `phase` 上
  （`measuring`/`preparing`），于是总量 58.7 MB 已经知道了、进度条还在来回跑一条
  `w-1/3` 宽的脉冲 —— 截图里那条 1/3 满的条就是它。**一条不反映真实进度的动画
  比一条不动的更误导**。改为只在「总量还不知道」时用 indeterminate。
* **视频没有预览图。** 录屏的封面帧 `icon_path` 是 `.jxr`（HDR），stb 解不了；
  旧代码只看「icon 在不在」就走进解码分支 → 解码失败 → 回 -2 → 404 → 磁贴纯黑。
  改为按「**这是不是视频**」决定回占位图，不再看 icon 是否存在。
* **撤回 `.120` 那个错误的「打包卡住」修法。** `.120` 把 `md_build_entries` 的
  加锁改成「`g_img` 已加载就跳过锁」，理由是与 `game_media_list_json` 互相阻塞。
  两条都不成立：MHD 是 `MHD_USE_THREAD_PER_CONNECTION`、HTTP 层没有任何全局锁，
  串行化解释不了 0%；而且那次「跳过」是一条**真的 use-after-free** ——
  `md_scan_locked` 第一句就是 `md_img_locked()`，它在库的 `(size, mtime)` 变了时
  会 `free(g_img)` 再整份重读，而另一个线程可能正拿着 `g_mu` 走 `g_img`。
  现在恢复为无条件加锁。

### ✨ Features

* **下载命名端到端打通**（用户口径：「截图下载和视频下载的命名方式是游戏名称-截图时间」、
  「压缩包也是游戏名称-打包时间戳」）：

  | 位置 | 名字 |
  |---|---|
  | 列表显示名 | `宇宙机器人-2026-10-06 21:33` |
  | 单个下载落盘 | `宇宙机器人-20261006_213305.png` |
  | 压缩包落盘 | `宇宙机器人-20261006_213306.zip` |

  显示名与落盘名**刻意不同**：文件名里不能有空格与冒号，而显示名要给人看。
  后端 `add_disposition()` 走 RFC 5987 双写（`filename="…"` + `filename*=UTF-8''…`），
  前端 `<a download>` 与 `Content-Disposition` **取同一个来源**（`/game_job_status`
  的 `name`、`/game_media` 的 `dlname`）—— 同源才不会「界面上写 A、落盘叫 B」。
* **`/game_job_status` 新增 `idle`**（距上次有进展多少秒，任务结束后恒 0）。
  一个不动的进度条分不清「在啃 500 MB 录屏」和「线程早死了」；有了它，界面能把
  「正在打包…」补成「已 45 秒没有进展；若长时间不动，可取消任务后重试」，
  并显示当前阶段（正在读取媒体库 / 正在计算体积 / 正在写入压缩包）。
* **打包线程三段相变各留一条日志。** `/game_job_status` 在 `is_noisy_route` 里是
  降噪的，真机出问题时日志里一行都看不到打包走到哪了。现在：没有 `measured`
  就是卡在扫库，没有 `writing` 就是卡在清旧包，有 `writing` 而没 `done` 就是
  `zip_write` —— 一条日志换一次「不用再猜」。

### 🧪 测试

* **补上「大文件跨分块」这条从来没被测过的路。** `zip_write` 每次只读 256 KB，
  而夹具里的录屏原来只有 4 KB —— 一个分块都读不满，`on_bytes` 只回调一次、
  CRC 回填那两行 `lseek` 也只在收尾走一遍。用户报的卡死正好落在这条路上。
  现在夹具里的 v1 是 1536 KiB + 12 字节头（≥6 次分块），新增一条单文件打包断言
  （`bytes_total` 必须等于源文件真实大小 `1572876`），`pack_big.zip` 再交给
  `zipfile` 逐条复算 CRC（分块累积错一位就红）。`media_scan` 130 → **138** 项断言。
* 另用一次性探针 `test/zipbig_probe.c` 在主机上跑过 61 MB / 3 个文件的**真机同构**
  场景：`rc=0`、236 次分块、耗时 <1 秒，产出 `zipfile.testzip()` 全绿 ——
  即**写盘这条路本身没问题**，问题在上面那三处。

---

## v0.5.2.121 (CN-FORK)

> 本轮修两个命名问题。
>
> 出包：`NayePlmTools_0.5.2.121.elf`（`-Os -Wall` 零警告）。

### 🐛 Bugfix

* **媒体项命名**：用户要求截图/视频的命名方式为「游戏名称-截图时间」（如 `宇宙机器人-2026-10-06 12:59`）。修复：`md_scan_t` 添加 `name` 字段，`md_scan_locked` 中通过 `game_title_get_name` 填充，`md_cb_row` 中使用 `游戏名称-截图时间` 格式。
* **压缩包命名**：用户要求压缩包命名为「游戏名称-打包时间戳」（如 `宇宙机器人-20261006_123456.zip`）。修复：`md_pack_start` 中通过 `game_title_get_name` 获取游戏名称，清理特殊字符后与时间戳组合成文件名。

---

## v0.5.2.120 (CN-FORK)

> 本轮修三个后端问题 + 一个前端样式调整。
>
> 出包：`NayePlmTools_0.5.2.120.elf`（`-Os -Wall` 零警告）。

### 🐛 Bugfix

* **视频缩略图不显示。** `md_thumb_src` 只接受 PNG/JPEG/BMP/GIF，视频文件（mp4）被直接拒绝，导致视频永远没有缩略图。修复：视频无 icon 时返回 -3，`game_media_thumb` 生成占位缩略图（深色背景 + 播放按钮三角形）。
* **打包卡住。** `md_build_entries` 每次调用都锁 `g_mu`，与 `game_media_list_json` 互相阻塞。修复：`g_img` 已加载时跳过锁（`md_scan_locked` 只读不写，无需加锁）。
* **列表慢。** `game_media_count` 每次调用都扫全表。修复：加 600 秒缓存 + 版本号失效（删除媒体时递增版本号）。

### 🎨 UI

* **红框高亮。** 用户要求在游戏卡片的名称+体积行加背景突出显示。修复：`GameSection.jsx` 给名称+体积行加半透明背景条（`bg-white/[0.04] rounded-md px-2 py-1`）。

---

## v0.5.2.117 (CN-FORK)

> 起因是真机一句话：「**游戏管理的列表变空了**」。
> 查下来是 `.116` 引入的**静默**缺陷 —— `/game_list` 照样 200，回的却是
> `{"games":[],"count":0}`：不崩、不报错，`make` / 主机关卡 / `npm run lint`
> 三道门**全绿放行**。
>
> 本轮修掉三个，同时把「为什么三道门都拦不住」补上：`src/game_mgr.c` **第一次
> 能在主机上编译**，新增 65 项断言的回归测试，其中「播种栈」那一步是复现条件本身。
>
> 本轮出包：`NayePlmTools_0.5.2.117-me.elf` / `-ga.elf`（`-Os -Wall` 零警告）。

### 🐛 Bugfix

* **游戏列表整个空掉（`/game_list` 回 `count:0`）。** `.116` 给 `enum_arg_t` 加了
  `want`（详情模式：非 NULL 时只认这一个 tid、且一个字节都不写 buf），而
  `build_game_json` 一直沿用逐字段赋值，**漏了它** → `want` 成了栈上垃圾；而
  `gm_append_item` 第一行就是 `if (a->want)`，垃圾非空时**每一个条目都早退**，
  `count` 恒 0。改为 `memset(&a, 0, sizeof(a))` 整体清零。
  症状之所以是「**先好后坏、越用越空**」：栈上那份垃圾恰好为 0 时列表正常；一旦
  跑过一次 `/game_detail`（它走 `gm_lookup`，会 memset 再把 `want` 指向 tid），
  同一 MHD 工作线程的这块栈就留下一个非空指针，此后列表一直空 —— 不是每次都复现。
* **详情页 5 个字段读错列。** `gm_db_row` 花力气按列名在建表 SQL 里现查列序
  （固件间 `tbl_contentinfo` 列序会变），结果存进 `m.c[]` —— 但 `st_cb_row` 取列时
  传的却是 `GM_DB_*` **枚举序号**，把解析结果整个丢掉。于是 `content_id` 读成第 0 列
  （titleId）、`install_time` 读到整型列、`appdb_size` 读到文本列、`can_uninstall`
  永远取不到值。改为按 `r->col[GM_DB_*]` 取。
* **详情 `version` 吐栈上垃圾。** `char version[40]` 只有读到 `param.sfo` 才被写；
  没有 sfo（dump 缺文件 / 权限不足）时一个字节都不会碰它，直接进 JSON —— 乱码还算
  好的，混进控制字节就是非法 JSON，前端 `.json()` 一抛整个详情弹窗白掉。改为显式清空。

### ✨ Features —— 测试基建

* **`src/game_mgr.c` 首次可在主机上编译，新增 `game_mgr` 主机关卡（65 项断言）。**
  这个文件以前卡在 `getmntinfo()` / `MNT_NOWAIT` / `struct statfs::f_mntonname`
  三个 BSD 专有符号上，**游戏管理的核心（列表 + 详情）一条断言都没有** —— 这正是上面
  那个 bug 能穿过三道门的原因。补法是**一个字都不改 `game_mgr.c` 的功能代码**：
  | 新增 | 干什么 |
  |---|---|
  | `test/host_mnt_compat.h` | 仅 `-include`、不进真机；顶掉那三个 BSD 符号，主机上恒返回「0 条挂载」= 找不到沙盒图标，与真机上没这个挂载时语义一致 |
  | `include/game_mgr.h` | `GAME_MGR_APPDB_PATH` 加 `#ifndef` 守卫（与 `game_media.h` / `game_saves.h` 同一手法，**默认值一字未改**） |
  | `test/make_appdb_fixture.py` | 现造最小 `app.db`（列序照真库），让自研的 `st_bwalk` / `st_rec_col` 自己去读 —— 夹具不参与解析 |
  | `test/game_mgr_test.c` | 65 项断言 |
* **测试的核心手法是「主动播种栈」。** 那个 bug 的复现条件本身就是「栈上那段恰好非空」，
  指望进程刚起来那点干净栈碰巧复现等于没有测试 —— 所以 `seed_stack()` 把接下来那次调用
  要占的栈显式写成一个**合法可读的非空指针**（指向 `"SEEDSEED1"` 而不是 `0xAAAA…`，
  为的是让 `strcmp` 读到合法内存，表现为「列表空」而不是 segfault，断言才有意义）。
  **已验证有效**：拿掉 `memset` 那一行，65 项里 8 条立刻红，全部报 `count=0 items=0`。
* 其余覆盖：列表项字段与 `count` 对账、60s 快照「作废后真重建 / 不作废回同一份字节」、
  详情 23 个契约字段**按值**断言（只断言「字段存在」是抓不住读错列的）、
  `game_mgr_source` 与列表共用同一套枚举、`game_mgr_is_scan_path` 的前缀陷阱、
  `game_mgr_icon_path` 真走到兼容头那条分支。

### ⚠️ 待真机验证

- 列表是否恢复正常（多进几次游戏详情再回列表，列表必须一直是全的）。
- 详情页 `content_id` / `install_time` / `last_launch` / `appdb_size` / `can_uninstall`
  五个字段是否变成对的（`.116` 之前它们读的是错位列，主机夹具才把它照出来）。

---

## v0.5.2.116 (CN-FORK)

> 本轮按用户两句话推进：先「把游戏管理模块的后端全部完成」，收尾时又来一句
> 「同步修复预览图片不全，下载失败的问题」。
>
> ① **游戏管理后端补完**：`.115` 只做了多媒体六条 + 两条任务端点，本轮把
>    `gameApi.js` 文件头剩下的**一次做完，共 19 条**（详情/删除、存档六条、
>    改名与磁贴九条、运行期三条）。**前端一行没改** —— `npm.cmd run lint` 仍是
>    基线 28 error / 6 warning，本轮未触碰任何 `frontend/` 文件。
> ② **预览图片只剩顶部一条 + 打包下载失败**：两个症状一个根，见 🐛 Bugfix。
>
> 本轮出包：`NayePlmTools_0.5.2.116-me.elf` / `-ga.elf`（`-Os -Wall` 零警告）。

### ✨ Features

* **游戏管理后端补完：19 条端点按 `gameApi.js` 文件头逐条实现。** 新增六个模块，
  每个都带一份契约头注释（字段名、边界、真机取证结论写在那里面）：

  | 文件 | 管什么 |
  |---|---|
  | `src/game_title.c` / `include/game_title.h` | 改名 + 磁贴两层图（图标 1:1 PNG / 背景 16:9 PNG→1920×1080 DXT1 DDS，外加 PNG 预览与 DDS 原字节两路取回） |
  | `src/game_saves.c` / `include/game_saves.h` | 存档六条：列表 / 自动备份配置 / 远程连通测试 / 下载 zip / 上传 / 删除 |
  | `src/game_runtime.c` / `include/game_runtime.h` | 运行期三条：mount / unmount / uninstall（`sceAppInstUtilAppUnInstall(title_id)` 单参，符号在 `libSceAppInstUtil.so`，**真实 `make` 链接通过**） |
  | `src/game_job.c` / `include/game_job.h` | 任务槽本体（打包与删除**共用一个槽**）+ 进度 / 取消 / CRC32 |
  | `src/zip_io.c` / `include/zip_io.h` | zip 编码器（媒体打包与存档下载**共用同一份**，字段一字未改） |
  | `src/inflate.c` / `include/inflate.h` | 存档上传解压（仓库里没有 zlib，自己解） |

  | 分组 | 端点 |
  |---|---|
  | 详情 / 删除 | `GET /game_detail?tid=`、`POST /game_delete {tid,confirm}` → 202 `{job_id}` / 409 `EBUSY` / 403 `EPERM` / 200 `{source_missing}` |
  | 存档 | `GET /game_saves?tid=`、`POST /game_save_auto`、`POST /game_save_test`、`GET /game_save_download?tid=&save=`、`POST /game_save_upload?tid=&save=`（裸 body，512MB 上限）、`GET /game_save_delete?tid=&save=` |
  | 改名 / 磁贴 | `POST /game_rename`、`POST`+`GET /game_tile_icon?tid=`、`GET /game_tile_icon_reset`、`POST /game_tile_background?tid=`（64MB 上限）、`GET /game_tile_background_reset`、`GET /game_tile_bg`（PNG 预览）、`GET /game_tile_bg_dds`（1036928 B 原字节） |
  | 运行期 | `POST /game_mount {tid,mode}`、`POST /game_unmount {tid}`、`POST /game_uninstall {tid}` |
  | 任务 | `GET /game_job_status?job=`、`GET /game_job_cancel?job=`（`.115` 已接；`.116` 起与删除任务共用同一槽，取文件路径时按 `kind` 过滤） |

* **`game_mgr` 补齐详情与扫描边界**：`game_mgr_detail_json()` 按契约出**逐字对应的 23 个字段**
  （`tid…title_bg_custom`，顺序也一致）；
  `game_mgr_is_scan_path()` 判「这个路径归不归游戏管理扫」（前缀后必须跟 `/`，
  否则 `/a/bc` 会被 `/a/b` 认走）；`game_mgr_invalidate()` 让改名/换图立刻穿过
  `/game_list` 那份 60s 快照 —— 不加这一下，刚设的图标会被列表缓存吞掉一分钟。

* **游戏删除是后台任务**：`measuring → deleting → finalizing` 三段进度，
  按「文件 + 目录 + 软链」数条目，所以进度条的比例与最终删掉的一致
  （`GR_MAX_DEPTH 32` / `GR_MAX_ENTRIES 1000000` 两条硬上限兜住符号链接环）。

* **请求体上限逐条对着 `mock-server.js` 的 `express.raw({limit:…})`**：
  存档包 512MB、磁贴 PNG 64MB、JSON 1MB（`gr_post_limit()`）。
  前端能过、后端拒的后果是用户只看到一句「请求失败」，连为什么都拿不到。

* **新增主机单测 `test/game_saves_test.c` + `test/make_game_fixture.py`（88 项断言）**。
  夹具是两个用户目录（**第一个故意空着**）+ 两种槽形态 + 一个该被跳过的 `.upl_`，
  三个路径宏用 `-D` 指到 `/tmp/pldmgr_saves_test`。钉住的是这几条真机上不好验、
  出错又只在用户操作时才暴露的：
  - 根目录取**体积最大**的命中（空用户目录先扫到就会让整个列表变成「0 字节」）；
  - 一级子项即一个槽、`size`/`files` 递归统计、`.upl_` 不进列表；
  - `auto` 的**明文密码/令牌永远不外泄**，只吐 `has_password` / `has_token`；
  - `port` 认数字也认字符串、留空即不修改（下面 Bugfix 的第 2 条就是它钉住的）；
  - 打包出的 zip 内容对得上、**打完源槽还在**；上传裸字节成新槽；删槽只删该删的。

  测不了的两块写在文件头，免得当成漏了：SFO 里的读名字（`game_mgr_sfo_text` 在
  `game_mgr.c`，那个文件要 `getmntinfo()`、主机上编不过，所以它是整个链接集里唯一
  的桩）与 HTTP 那一层（没有 microhttpd）。

### 🐛 Bugfix

* **`POST /game_save_auto` 取错了 `tid` 的位置 —— 每次点「保存配置」都 400。**
  后端从 **query** 取，而前端 `saveAutoConfig` 是 `body:{tid, ...cfg}`（`mock-server.js`
  也是从 `req.body.tid` 拿），query 里根本没有 → 必然取到 NULL → 回 400「缺少 tid」，
  配置一个字都没写进去。改成从 body 取、query 兜底。
  这是 19 条端点 + 前端 29 处调用逐条对照（方法/路径/状态码/字段名/参数取自哪一侧）
  挑出来的，**其余 8 条 POST 都核过没有同类问题**（delete/mount/unmount/uninstall
  走 body，存档上传与磁贴两层走 query，rename 走 body）。

* **`port` 从不落盘，「测试连接」每次都打到 21。** 前端那个 `<input>` 的
  `onChange` 存的是 `e.target.value` —— 出来**永远是字符串**，`inputMode="numeric"`
  只是键盘提示、不做类型转换，所以请求体里是 `"port":"2121"` 而不是 `2121`。
  后端两处（`game_saves_auto_set` 的落盘、`game_saves_test` 的取值）都只认
  `cJSON_IsNumber`，于是端口整个被丢掉：保存后读回来还是默认 21，「测试连接」
  连的也是 21 —— 用户填的 2121 等于没填，还会以为 FTP 本身坏了。
  改成**数字与数字字符串都认**（`gs_get_port()`），空串仍按「留空即不修改」处理。
  mock 那边是整段 raw 合并、不挑类型，所以这个问题只在真后端上暴露。

* **`auto.http` 补一个 `has_token`**：前端那句「已保存过；留空则保持不变」是按
  `has_${f.key}` 通用拼的（secret 字段 = smb/ftp 的 `password`、http 的 `token`），
  只回契约里的 `token_set` 它永远读不到 `has_token` —— 令牌栏的提示一直不出现，
  而 smb/ftp 两栏都有，用户会以为令牌没存上。两个名一起给，契约不破、前端一行不改。

* **存档那三条怎么防再犯：新增 `test/game_saves_test.c`（88 项断言）。**
  由 `test/run_host_tests.sh` 拨起，夹具 `test/make_game_fixture.py` 现造
  （两个用户目录、**第一个故意空着**；PS5 目录槽 + PS4 单文件槽；一个该被跳过的
  `.upl_`）。它钉住了 **port 的数字/字符串双认** 与 **has_token**，以及根目录取
  体积最大的命中、一级子项即一个槽、`size`/`files` 递归统计、
  **明文密码/令牌永远不外泄**、打包出的 zip 内容与源槽完好、上传成新槽、删槽。
  **唯一没被钉住的是 `tid` 在 query 还是 body** —— 那条在 `http_server.c`，
  主机上没有 microhttpd。那类错编译期看不出来，只能靠对照审计，
  已写进 HANDOVER §2.1.2 当规矩：**加新端点先查参数取自哪一侧**。

* **多媒体「预览图片只显示最顶部的一条」+「打包下载失败」—— 两个症状一个根：
  `MHD_create_response_from_fd64`。**

  排查链：同一进程里**走 `MHD_create_response_from_buffer` 的全好**（缩略图、所有 JSON），
  **走 `MHD_create_response_from_callback` 的也全好**（`/events` 那条 SSE 流真机上一直是好的），
  **唯独走 fd 直出的两条出事** —— `/game_media_file`（原图/录屏预览）与
  `/game_media_pack_get`（打包 zip 下载），正好就是用户报的这两个。
  `Content-Length` 是照 `fstat` 报出去的，字节却少一截：图片解码到断点就停
  （只剩顶部一条），zip 少尾（下载直接判失败）。链接进来的 `libmicrohttpd.a` 里
  导出了 `MHD_send_sendfile_`，说明 fd 直出那条分支会落到内核 `sendfile()` 上，
  而 MHD 自己的文档写着：构建没开 `MHD_FEATURE_RESPONSES_SHARED_FD` 时，
  「同一条响应并发可能把错误数据发给客户端」。

  改法：新增 `gr_file_response()` —— 一个 32KB 块的 `MHD_create_response_from_callback`，
  回调里自己 `lseek(pos) + read()`，整条 `sendfile` 路径绕开，**fd 所有权仍由 free 回调关**。
  换掉的只是「谁来搬字节」，不是「搬多少」：几百 MB 的录屏照样不进堆。
  三处调用点一起换掉 —— `/game_media_file`、`/game_media_pack_get`、以及本轮新写的
  `reply_file()`（存档下载 / 磁贴图标预览 / 磁贴背景预览 / DDS 原字节），
  否则 `.116` 的存档下载会原样继承同一个 bug。

### ⚠️ 待真机验证

- **本轮的 fd→回调改动本身**（`.116` 出包后第一件事就是验这两条）：
  打开一张截图看预览是否整张 → 打包下载一个 zip 看能否正常解开。
  顺带确认没有 fd 泄漏（连着开 50 次预览，`/proc`/`sysctl` 的 fd 数不涨）。
- **`.115` 遗留三项**（见 HANDOVER §7：录屏目录层级、视频时长读哪一列、只删文件后系统媒体库的反应）。

---

## v0.5.2.115 (CN-FORK)

> 本轮口径：接 `.112`/`.113` 的**纯前端**多媒体管理，补**后端**。前端契约（`gameApi.js`
> 文件头）是唯一事实来源，后端照它实现，前端功能一行不改。用户指令原文是
> 「继续，不要中断」—— 没有新的 UI 口径。
>
> **为什么是 `.115` 不是 `.114`**：`.114` 那轮（磁贴再放大 ×1.25 + 改名页背景块改回
> 左右分栏）已在本日 01:47 出过 `-me`/`-ga` 两个正式包，而本轮的 `src/game_media.c`
> 是 09:58 才建的 —— 复用同一个版本号会让两个内容不同的二进制共用一个版本，
> 违反「每次调整构建前 +1」。`.114` 的文档在上一轮收尾时漏写了，本轮一并补上（见下）。
>
> 本轮出包：`NayePlmTools_0.5.2.115-me.elf` / `-ga.elf`（`-Os -Wall` 零警告）。

### ✨ Features

* **游戏管理 → 多媒体管理后端：六条业务端点 + 两条任务端点，全部按 `gameApi.js` 契约实现。**
  新文件 `src/game_media.c` + 契约头 `include/game_media.h`：

  | 端点 | 实现要点 |
  |---|---|
  | `GET /game_media?tid=` | 读 `av_content.db` 过滤 tid → `{tid, items[], count, truncated}` |
  | `GET /game_media_thumb?id=&w=320` | 系统缩略图 → 盒式平均缩放 → JPEG → 落盘缓存 |
  | `GET /game_media_file?id=` | `MHD_create_response_from_fd64` **fd 直出** |
  | `POST /game_media_pack` | `{tid, ids}` → 单 worker 打包任务 → `{job_id}` |
  | `GET /game_media_pack_get?job=` | fd 直出 zip |
  | `GET /game_media_delete?tid=&id=` | 只删文件 → `{ok, message}` |
  | `GET /game_job_status?job=` | `{job_id, active, phase, percent, …}`，1s 一次 |
  | `GET /game_job_cancel?job=` | `{ok:true, reason:"cancelled"}` |

* **数据源是 `/system_data/priv/mms/av_content.db` 的 `tbl_PhotoVideo`，只读。**
  为什么不扫目录：`.jxr`（HDR 截图）的宽高从文件头读不出来（stb_image 不解 JXR），
  而库一次给出 `content_path/icon_path/width/height/duration(_ms)`。读法与
  `game_mgr.c` 读 `app.db` 完全同构 —— 极简 SQLite 只读解析器，非 WAL、页 4096、
  整库 ~180KB 整读进内存。
  * **列序号按列名现数、不写死**（`md_sql_col_index` 数顶层逗号）：固件间列序可能变，
    且 `width`/`icon_width`、`duration`/`duration_ms`、`content_path` 是前缀撞车的
    三对，前后都要挡。
  * **`size` 用 `stat`、`mtime` 用文件的 `localtime`**，不用库里的值：
    库的 `size_on_storage` 是「原图 + 缩略图」之和（真机 4150751 vs 实际 3815723），
    库的 `created_time` 是 UTC，与文件名里的本地时间差 8 小时。

* **打包 = 自研 STORE zip。** SDK 的 `target/lib` 里没有 zlib（curl/mbedtls/
  microhttpd/pthread 有，`-lz` 没有），引第三方压缩库要动构建链；而 png/jxr/mp4
  本身就是压缩过的，再 deflate 一遍只多花 CPU。**always 写 EOCD64 + locator +
  经典 EOCD**，单条目 `size` 或偏移 ≥ 4GiB 就挂 `0x0001` extra —— 两种读法拿到的
  值一致。产出用 Python `zipfile` 逐条验 CRC + 逐字节比内容（主机测试里）。
  * **本地头的 CRC 先写 0 占位、数据流完 `lseek` 回填**：源文件因此只读一遍。
    最初版本为「先算出 CRC 再写头」整读了两遍，录屏几百 MB 时等于白白翻倍 IO。
  * zip 内条目名 = `content_path` 去掉 `/av_contents/`（如 `photo/<生成器>/<TID>/<hex>/…`），
    跨分片唯一且可读。

* **大文件一律 fd 直出**（`MHD_create_response_from_fd64`，fd 由 MHD 关）。
  录屏动辄几百 MB，读进内存就是把面板 OOM 掉。只有缩略图（几十 KB）走内存。
  response 创建失败时**必须自己 `close(fd)`** —— 成功才移交所有权。

### 🐞 本轮抓出来的三个真 bug

前两个是新测试**上线第一跑**抓到的，第三个是对着前端 `watchJob`/`MediaManagerModal`
的消费方式**复读契约**读出来的 —— 编译期三种都看不出来，真机上才炸。

1. **`md_scan_locked` 只 walk 了 page 1。** page 1 是 `sqlite_master`，里面是建表语句、
   不是媒体行。`md_cols_locked` 当时只带回列序、把 `tbl_PhotoVideo` 的 **root page**
   丢了。表现极具迷惑性：**库读到了、7 个列号全对、`count` 却恒为 0**。
   `game_mgr.c` 是两次 walk（page 1 找 root → `m.root` 读数据），我漏了第二跳。
2. **`game_media_pack_start_json` 的 use-after-free。** `ids[i]` 直接指向
   `cJSON_GetArrayItem` 返回的 `valuestring`，而这些字符串挂在 JSON 树上 ——
   我在 `cJSON_Delete(root)` **之后**才把 ids 传给 `md_pack_start`。
   结果是 `md_path_ok` 读已释放内存、`msg` 神秘地回「参数不合法」，
   表现是「按 id 打包恒定失败、全量打包却正常」。现已在 `Delete` 前 `strdup` 副本。
   顺带收紧：`ids` 里混进**非字符串**元素改为直接拒绝（原实现会静默跳过，
   `[1,2]` 会退化成全量打包 —— 比报错危险得多）。
3. **任务状态 JSON 里 `error` 转义了、`reason` 没转。** 两个字段是同一条链
   （`md_pack_fail`）传下来的自由文本，只转一个等于给自己留一个「只要某条失败原因里
   出现引号就整个响应体变成非法 JSON、前端 `.json()` 直接抛」的洞。
   一并把 HTTP 侧状态缓冲 `jbuf` 从 1024 抬到 2048 —— `phase`+`error`+`reason`
   三段的最坏长度已经贴近 1000 字节，截断会表现为「任务莫名其妙变成 404」。

> 一并记下：写夹具时 `def rel(root, *parts)` **没用上 `root` 形参**，导致
> `content_path` 少一段 `photo/`，症状与 bug ① 几乎一样（也是 count=0）。
> 两个问题叠在一起，是靠在 `md_cb_row` 每道筛子后加临时日志才分开的 ——
> 下次遇到「扫到了但一条都不出」，先打 `content_path` 全文再看 stat。

### 🧪 Tests

* `test/make_media_fixture.py` —— 现造夹具：`sqlite3` 造 `tbl_PhotoVideo`（19 列，
  **列的相对顺序照抄真机 66 列 schema**，好让 `width` vs `icon_width`、
  `duration` vs `duration_ms` 这两对前缀撞车真的被测到）+ 6 条行 +
  它引用的真实文件（PNG 用 `zlib` 拼，缩略图 JPEG 由 C 侧 `stb_image_write` 写，
  Python 标准库造不出来）。夹具库设成 `0444`，测试里断言它没被动过。
* `test/media_scan_test.c` —— 98 项断言：列表（条数/kind/分隔符/tid 过滤/非法 tid）、
  定位（含穿越）、原图 fd、缩略图（JPEG 图标解码 + 1920×1080 缩到 200×112 +
  HDR `.jxr` 与 mp4 的 -2 分支）、打包（子集 2 条 / 全量 5 条）、
  删除（tid 不符拒删 + 原图与 `.meta`/`.dat` 消失 + **`av_content.db` 分毫未动**）。
* `test/media_verify.py` —— C 侧没法验 zip（无 zlib），交 Python：`testzip()` 逐条 CRC、
  逐字节比内容、条目名与 `content_path` 的映射、压缩方式必须 `STORED`；
  `json` 模式解析 `list.json` 并校 `count == len(items)`（分隔符写错第一个炸这儿）。
* `test/run_host_tests.sh` 从 3 条增至 4 条，末尾补 `-lpthread -lm`（打包 worker 线程 +
  stb 的 libm）。`log_server.c` 不参与主机链接（它的头要 `microhttpd.h`），
  `pldmgr_log` 由测试文件提供、默认静音，`MEDIA_TEST_VERBOSE=1` 才开。
* **验收状态**：主机测试 `4/4 PASS`（media_scan 98/98 + 三次 zip 校验全对）；
  PS5 ELF 构建 `-Os -Wall` 零警告；前端 `npx eslint` 跑我碰过的
  `gameApi.js` / `MediaManagerModal.jsx` / `format.js` 全绿。
  ⚠️ `npm run lint` **全局仍有 28 个 error**，全在 `App.jsx`、各 `*View.jsx`、
  `vite.config.js` 里（`no-unused-vars` / `react-hooks/set-state-in-effect`）——
  **都是历史遗留，本轮一个都没碰**，但记在这里免得下次以为是新引入的。

### ⚠️ 偏离与已知限制

* **删除只删文件，不写 `av_content.db`。** SDK 里没有 sqlite3，仓库那套极简解析器只会读
  b-tree；手写 b-tree 删除等于拿用户全部截图去赌系统库不被写坏。
  库里的行留给媒体库自己清 —— 它有 `file_status` 列，**是否自动对账未验证**（§7c）。
  本轮用夹具证明了「删完我们自己的库文件分毫未动」，但**真机媒体库的反应仍是黑箱**。
* **`.jxr` 无解码器 → 缩略图 404。** 系统缩略图（`icon_path`）缺失时才落到这一步，
  前端 `<img onError>` 已兜住。真机上系统缩略图始终存在，这条是兜底不是常态。
* **§7 待验证（下一轮拿真机取证，别当已知事实）**：
  * a. 录屏目录层级是否与 photo 同构（`/video/<生成器>/<TID>/<hex>/…`）——
    真机 `video/` 目录当时是空的；
  * b. 视频时长读 `duration` 还是 `duration_ms`（单位是秒还是毫秒）——
    实机没有录屏，两列都是 0；实现按 `duration_ms>0 ? /1000 : duration` 走，
    主机测试把两条分支都钉住了（但**钉的是我们的行为，不是真机语义**）；
  * c. 只删文件后，媒体库再次打开是否会出现幽灵条目。
* `phase` 新增 `packing`（实现在 `game_media.c`，`gameApi.js` 的枚举注释已同步补上）；
  注释里的 `deleting`/`finalizing` 仍是游戏删除任务的占位，**还没有后端**。
* `/game_media` 与 `/game_media_thumb` 里 `.jxr` 的宽高来自**库**，不是文件头 ——
  库与文件不一致时（用户手动放过文件）会显示库的值。这是刻意的取舍。

---

## v0.5.2.114 (CN-FORK)

> ⚠️ **这一轮的包出了、文档漏了** —— 上一轮跑完就收尾了，`CHANGELOG` 与 `HANDOVER`
> 一个字没写。这里由 `.115` 补记。
> 包：`NayePlmTools_0.5.2.114-me.elf` / `-ga.elf`，本日 01:47 出，两包各 **9,957,216 字节**，
> 内嵌标题 `NayePlmTools v0.5.2.114 by naye (91fa4fb, built at 2026-10-05 17:46:55 UTC)`
> （**非 DEV**）、`PLDMGR_VER:0.5.2.114`；`.113` 的包已过期。
>
> 本轮只动前端与版本号，**C 后端功能一行未改**。

### ✨ Features

* **游戏磁贴再放大 1/4（同一根令牌的第三次调档）。** 口径原文：「relative 再大四分之一左右」。

  | 档 | `.101` | `.114` |
  |---|---|---|
  | PC/手机 `--tile-w` | 13.125rem(210px) | **16.40625rem(262.5px)** |
  | 电视 `html[data-device="ps5"]` | 18.75rem(300px) | **23.4375rem(375px)** |

  * 实机反馈：`.113` 的 210px 在电视端正文列里挤得每行放 6~7 块，磁贴上的四颗按钮挨得很近、
    「查看详情」的方图也压得过小，用户还是觉得小。
  * 电视端 375px 下限在正文列约 1470px 时排到 3 列、每列实际 ≈490px —— 回到「三块一排」的
    舒适档（4 块一排时遥控器方向键要按 4~5 下才跨一列）。
  * **`.100` 缩小 → `.101` 放大 → `.114` 再放大**，是同一根 `--tile-w` 的第三次调档；
    `index.css` 两处（`:root` 与 `html[data-device="ps5"]`）都记了完整沿革，
    改这格前先看注释，别再来回拧。

* **改名页「磁贴背景」块改回左右分栏（`.112` 那轮的竖排撤掉）。**
  口径原文：「磁贴背景的按钮和说明放到图片右边」。竖排时预览能开到最大，但整块太高、
  按钮要滚才看得见；改回「图左、按钮说明右」后与上面的图标块结构完全对称，
  两个块并排时视觉一致。

---

## v0.5.2.113 (CN-FORK)

> 用户口径（本轮原文，一条）：
> 「这几个铺满页面的字体太小了，跟其他父级菜单一样大。这里点击也可以上传图片，上传完要有一个应用的
>  动作；这里的缩略图也要调大。太小了，预览都看不清。改把，改完直接打包成elf」
>
> 三件事都由截图指认（`Clipboard_Screenshot.png` = 改名页的「磁贴背景」块；`Clipboard_Screenshot-1.png` =
> 多媒体网格）。本轮**出包**：`NayePlmTools_0.5.2.113-me.elf` / `-ga.elf`。

### ✨ Features / 口径变更

* **① 铺满面板里的字号与侧栏菜单拉平（用户：「跟其他父级菜单一样大」）。**
  * 病灶是**硬编码 px**：游戏管理那五个弹窗里写着 60 处 `text-[13px]/[12px]/[11px]/[10px]`。
    px 不随根字号走，而 `body` 在 ≥768px 被抬到 20px —— 同一块屏幕上侧栏菜单项是 16.5px、
    兄弟页面正文是 `text-sm`(17.5px)，面板里的字却还停在手机档。
  * 解法与 `--ctl-h` / `--tile-w` / `--sidebar-w` 同一手法：在 `index.css` 立四条**语义档**，
    60 处硬编码全部替换（残留 0 处）：

    | 类 | 字号 | 改前 |
    |---|---|---|
    | `.panel-body` | 1rem（16px） | 13px |
    | `.panel-note` | 0.875rem（14px） | 12px |
    | `.panel-tiny` | 0.8125rem（13px） | 11px |
    | `.panel-micro` | 0.75rem（12px） | 10px |

  * **⚠️ 一个必须记下来的量错方式：`rem` 不等于 `body` 的字号。** 本仓库把
    `body { font-size: 20px }` 写在 ≥768px 的媒体查询里，而 **`html` 从未设过字号** ——
    所以 `getComputedStyle(document.documentElement).fontSize` 恒为 **16px**，
    一切 rem 类（Tailwind 的 `text-sm`/`text-xs` 也一样）都按 16 换算，
    `body` 的 20px 只对「自己没有字号的继承元素」生效（例如侧栏按钮本身）。
    第一版按「桌面 rem = 20px」推算，写出的 0.9rem 实际只有 **14.4px**，等于没改；
    是**量目标元素的计算字号**才发现的（`{"root":16,"body":14.4}`）。
    **判断字号有没有真的变大，只能量目标元素，不能推算根字号。**
  * 芯片徽标的高度从写死 `h-[18px]` 改成 `.chip-xs`（`1.25rem`）：字号涨上去之后，
    18px 的框上下只剩 3px，汉字会被裁掉一半。
  * `Block` 的子板块标题改用页面级同一个 `.panel-title`（16.8px），与正文拉开一档 ——
    否则全局替换会让标题变成「和正文一样大」，层级被抹平。

* **② 改名页两层图改成「点预览也能选图 + 应用才生效」（用户：「这里点击也可以上传图片，上传完要有一个应用的动作」）。**
  * **预览图本身就是选图入口**：两块预览各自变成一颗 `<button>`（带 `aria-label="更换磁贴图标"` /
    `"更换磁贴背景"`），悬停浮出「点图片也可以选图」提示。做成真按钮而不是给 `div` 挂 `onClick` ——
    遥控器要能走到它、读屏要知道它是干什么的，这两件事只有真按钮天然具备。
  * **上传与生效拆成两步**：选完文件先在浏览器里缩好、用 `objectURL` 就地预览（标出「待应用」），
    点「应用」才提交后端，另有「取消」丢弃。理由不是洁癖：① 选错图不会先污染主机上的磁贴；
    ② 1920×1080 的 PNG 传到 PS5 要好几秒，传完才发现选错就得再传一次；
    ③「上传」与「生效」本来就是两件事，界面上就该是两步。
  * 按钮行随状态换一套：未选图 = 「上传图片 / 恢复默认」，已选图 = **「应用 / 取消」**（不会同时出现两个入口）。
    toast 文案跟着改口径：**「已更新」→「已应用」**（否则用户点完「应用」看到「已更新」，
    会以为上传那一步就算完成了）。
  * **同一面板里两颗「应用」的可见文字完全相同，是刻意的**（对着自己那一块看没有歧义），
    但**可访问名必须区分**：`aria-label="应用（磁贴图标）"` / `"应用（磁贴背景）"`。
    顺带发现一个会骗过测试的形状：`:has-text("应用")` 是**子串匹配**，
    它同时命中预览图按钮里的「**待**应用」芯片 —— 一切「靠可见文字定位按钮」的自动化都会在这里挑错。
  * `objectURL` 的回收收在**一处**（一条按「上一轮有、这一轮没了」判的同步 effect）：
    换一张 / 应用 / 取消 / 恢复默认 / 换游戏 / 关弹窗六条路径全都不用各自记得回收，少记一处就是漏一处。
    ⚠️ 回收**不能**写在渲染期那段状态重置里 —— `react-hooks/refs` 会报 `Cannot access refs during render`。

* **③ 多媒体缩略图调大（用户：「这里的缩略图也要调大。太小了，预览都看不清」）。**
  * 网格列宽下限原来是写死的 `minmax(112px,1fr)`：112px 的 16:9 只有 **63px 高**，
    在铺满右侧的面板里是一排小豆腐块。现在立 `--thumb-w` 令牌分三档：
    移动 140px / **桌面 200px** / 电视 **320px**（复用 `--tile-w` 不行 —— 磁贴是竖版卡片，
    缩略图是 16:9 一格图，按同一宽度排出来的观感差很远）。
  * 实测桌面档格子 **202×114**（改前 112×63）、电视档 **≥ 320**。
    跟着放大的一并处理：图注字号（10px → 13px）、播放钮（36 → 44px）、
    左上角选中框（`--thumb-check`：24 → 28 → 36px）。
  * 令牌用内联 `style` 而不是 Tailwind 任意值 `grid-cols-[…var(--thumb-w)…]`：
    与 `Modal` 的面板定位同一条理由 —— `var()` 写在任意值里要 JIT 扫到才生成，
    漏生成就是「网格退回单列」这种一眼可见的错。

### 🐛 Bug Fixes

* **升级到 `.113` 后，产物里冒出 3 处 `0.5.2.112` —— 全是手抄的版本号。**
  * `SettingsView.jsx` 的捐赠说明（`settings.donate_desc` 里写死「（当前 v0.5.2.112）」）、
    `DonateView.jsx` 的兜底值 `version || '0.5.2.112'`、`mock-server.js` 的 `/version`
    （旁边还附着一句「与 pldmgr.h MENU_VERSION 保持一致」—— 靠人保持一致的注释就是一颗雷）。
  * 三处都改成**读唯一事实来源**：前两处走构建时烧入的 `__APP_VERSION__`（`vite.config.js`
    从 `include/pldmgr.h` 取），mock 与 `vite.config.js` 同一手法现读头文件。
  * **mock 那处的实际后果值得记**：前端挂载时会拿 `/version` 与 `__APP_VERSION__` 比对
    （`src/main.jsx` 的版本握手），对不上就触发一次「版本已更新」的刷新 ——
    本地联调会莫名其妙地刷页面，而且原因看不出来。
  * 新增一条**回归守卫**（`game112.mjs` 第 11 节）：直接读 `pldmgr.h` 取当前版本，
    再扫 locale / `DonateView` / `mock-server` 四份文件里有没有不等于它的 `0.5.2.x` 字面量。
    扫描前**先剥注释**（本仓库到处是版本沿革说明，「上一版写死 `'0.5.2.112'`」这种句子会把断言反复点亮，
    然后就没人在看它了）。

### ✅ Verification

* **浏览器回归（Playwright + mock 8081 + dev server 5199）**：`game112.mjs` **175 / 175 全绿**（169 → 175）。
  新增断言都是**几何量 / 计算样式 / 接口字段**，不是「写了某个类」：
  * 字号：`panelFonts()` 量计算字号并与**侧栏菜单标签**（16.5px）对比 —— 用户的原话就是
    「跟其他父级菜单一样大」，菜单是那把尺子。并要求 正文 > 说明 > 图注 > 芯片 依次递减且都 ≥ 12px、
    板块标题 > 正文（层级没被压平）。**基准要量菜单标签那颗 `<span>`**，不是 `.nav-item` 按钮本身
    （按钮没有自己的字号，继承 `body` 的 20px，量它会得到 20 而让这条断言永远失败）。
  * 应用流程：选完图**主机上什么都没变**（列表项仍无 icon）→ 暂存态标出「待应用」+ 出现
    「应用/取消」两颗（按 `aria-label` 定位）→ 点应用后版本号 +1、toast 是「已应用」、暂存态消失。
  * 暂存预览**真的渲染出来**：要求 `naturalWidth > 0` 且 `src` 是 `blob:`。
    只断言「出现了待应用芯片」是不够的 —— 本轮就踩到一次：手搓的测试 PNG 是**坏流**
    （PIL 报 `broken data stream`），浏览器不报错、只是图不出来，而「芯片在」让断言照样全绿。
    （`complete` 也不能当判据：坏流下它同样是 `true`。）
  * 取消：暂存态消失、按钮回到「上传图片 / 恢复默认」、**主机版本号不变**、预览退回当前生效的那张（不是空白）。
  * 缩略图：桌面档格宽 ≥ 180px、宽高比仍是 16:9（±0.08）、图注 ≥ 12px、选中框 ≥ 26px；
    在 **1920×1080 电视端**再量一遍（≥ 280px / 框 ≥ 32px / 面板正文 ≥ 16px）——
    令牌的电视档只有在那块屏幕上才绑得住。
  * 版本守卫：`pldmgr.h` 可读、四份文件里无写死的旧版本号、mock 的 `/version` 与头文件一致。
  * ⚠️ 量缩略图要量**那一格图**（`.aspect-video` 的按钮），不是连图注一起的外层 `div`
    —— 后者是「图 + 日期」，宽高比天然不是 16:9（实测 202×137），拿它验「放大没走形」会误判成失败。
* **其余套件同步复跑，全绿**：`nav112.mjs` 20/20、`acct111.mjs` 23/23、`avatar109.mjs` 19/19、`cropfix.mjs` 75/75。
* `npx eslint`：本轮改动的文件（含 `mock-server.js`）**零新增问题**。
  `SettingsView.jsx` 报 1 条 `'Icon' is defined but never used` —— 已用 `git stash` 对照 HEAD 确认是
  **既有误报**（`const NavRow = ({ icon: Icon })` 里的解构重命名，eslint 认不出来），不是本轮引入的。
* `npm run build` 通过：`frontend/dist/index.html` **915,162 字节**；
  产物内近三代版本号只有 `0.5.2.113`（`0.5.2.67` 是应用内更新日志数组里的历史条目，不是「说旧话」）。
* **截图存证**（`.shots/game113/`）：`01_rename_idle` / `02_rename_staged_icon` / `03_rename_staged_both`
  （暂存两层时主机状态实测为空）/ `06_media_thumbs`（缩略图放大后），
  以及浅色主题的 `04_light_staged` / `05_light_applied` / `07_light_saves` / `08_light_media`
  —— 字号与「待应用」芯片在浅档也要读得出来。

### 📦 Release

* **正式出包**：`NayePlmTools_0.5.2.113-me.elf`（NAYE00001 媒体栏）· `NayePlmTools_0.5.2.113-ga.elf`（NAYE00004 游戏栏）。
* 两包各 **9,957,216 字节**；内嵌标题 `NayePlmTools v0.5.2.113 by naye (81c42c1, built at 2026-10-05 17:02:00 UTC)`（**非 DEV**）、
  `PLDMGR_VER:0.5.2.113`；`0.5.2.10x`–`0.5.2.112` 残留 **0 处**；titleId 自检 NAYE00001 / NAYE00004 通过。
* **包内网页与磁盘产物逐字节比对**：两个 ELF 里 `assets_index_html` 数组与 `frontend/dist/index.html`
  **完全一致**（长度也一致：915,162 B；起点按 `dist[:64]` 定位，不用第一个 `<html>` 当结束标记）。
  这条比「尺寸对得上」强得多。包内另抽查了新 CSS 六条与新词条五条，全部命中。
* 契约无变更（本轮未动接口）。后端 C 代码一行未动。

## v0.5.2.112 (CN-FORK)

> 用户口径（本轮原文，两轮）：
> ① 「几个修改：存档管理页面打开后铺满右边整个区域。多媒体管理也是铺满右边。修改名称也是铺满，
>  但是你只把修改磁贴图标放出来了，还要增加一个修改背景的。」
> ② 「刚才铺满的几个页面圆角全部取消，修改名称哪里将磁贴图标与游戏名称放在一行，磁贴背景放在下面，
>  磁贴背景预览图是16:9的dds图片。图标是png图片。另外修复一个历史bug。
>  ps5真机上焦点鼠标移动的方式最好改回跟上游一样，现在这样很别扭。」
> ③ 「自定义背景 把文字说明和和按钮换位置预览图片铺满一行」（第二轮追加）
> 追问确认：「历史 bug」= 焦点/光标那句；回退尺度 = **改回上游作者的处理方式，
> 所有导航都出光圈，全部是鼠标跳到对应的位置**；DDS 由**后端**编码（前端传 PNG）。

### ✨ Features / 口径变更

* **① 存档 / 多媒体 / 名称与背景三个页面改成「铺满右侧」。**
  * `ui/Modal.jsx` 新增 `layout="panel"`：外层由 `inset-0 居中 + p-8` 换成 `inset-y-0 right-0` +
    `left: var(--sidebar-w)`，面板内部改 flex 列 —— **标题栏与操作按钮钉住，只有内容区滚**。
    此前是「整块一起滚」，内容一长标题和按钮都被滚出屏幕。默认仍是 `layout="dialog"`，
    其余十来个调用方**一行未改**。
  * 新增令牌 `--sidebar-w`（`index.css`）+ `html[data-sidebar]`（`App.jsx` 维护）。
    侧栏宽度在 `App.jsx` 里是 Tailwind 字面量 `w-80`/`w-28`，CSS 读不到，所以另立一条能读的值。
    **三档含「窄屏归零」**：<768px 侧栏是 `hidden` → 0，否则手机上面板左边会空出 320px。
  * **`.panel-sheet { margin: 0 !important }` 是必需项，不是保险。** 全站视图根节点统一 `.space-y-6`，
    它的 `> * + *` 会给视图的每一个后续子元素挂 `margin-top` —— **而弹窗正是视图的一个子元素**。
    居中弹窗看不出来，面板是贴边定位的：实测多出 **14.4px** 上边距，「铺满」直接不成立。
    特异性 `.space-y-6 > * + *`(0,2,0) 压过 `.panel-sheet`(0,1,0)，只能 `!important`。
  * 面板形态**不画遮罩**：已盖满右侧，再压一层黑只会把露在外面的侧栏也闷掉，反而让人不敢点侧栏。
  * 三个页面各自按面板宽度重排：存档页「配置 / 存档列表」在 ≥1280px 并排（本来就是一边配置一边对着列看的关系）；
    改名页「图标 / 背景」两块并排（要对着看才分得清谁是谁）。

* **② 磁贴上的图拆成两层：图标 + 背景（用户：「你只把修改磁贴图标放出来了，还要增加一个修改背景的」）。**
  * 上一版把「换封面」当成了「换背景」，后果是**用户一换背景就把游戏的官方封面也丢了**，且没有任何办法只换其中一样。
    现在两层的语义、尺寸与格式都不同，接口也拆开：
    | | 是什么 | 上传格式 | 输出规格 |
    |---|---|---|---|
    | **图标** | 封面那一格方图，默认 `icon0.png` | PNG | 1:1，1024×1024 **PNG** |
    | **背景** | **整张卡片**的底图，图标压在它上面 | PNG | 16:9，1920×1080 **DXT1 DDS** |
  * **背景是 16:9 的 DDS**（用户口径）。选 1920×1080 而不是别的 16:9：仓库里控制台已验证的
    `assets/pic0.dds`（由 `make_pic0.py` 生成）就是 1920×1080 DXT1，规格与它对齐，
    转换那一步不必再缩一次。**DDS 由后端转**（用户选定）：与头像那条路完全一致
    （前端只传图片，`avatar.c` 负责 png→DXT5），前端不自己编 —— PS5 老内核对 1920×1080
    跑一遍压块要慢一拍，而且编码器在后端已经有两份。
  * **图标与背景都编成 PNG**（原来是 JPEG）。图标改 PNG 是必需的：JPEG 会在图标边缘糊出一圈脏边；
    背景跟着一起走 PNG，只为少一条分支，反正后端还要转 DDS。
  * 缩图函数从写死正方形的 `squareCover` 改成 `fitCover(file, w, h, mime)`，两种目标尺寸共用一条路径。
  * 磁贴渲染顺序 = 背景层 → 详情热区 → 内容层（封面 + 徽标 + 四颗按钮）。
    背景层放在热区**之前**，两者都是 `inset-0`，DOM 靠前的先画，热区因此仍稳稳压在上面 ——
    否则点背景那一片就点不开详情了。背景 404 时整层从显示里摘掉：留着那层压暗渐变会变成一块纯黑卡片。
  * 列表项契约新增 `icon`（版本号，number）；`bg` 保留。详情接口新增 `title_icon_custom`。
    **两个字段都要给**：不给的话前端要么对每款游戏白跑两次 404、要么永远显示不出自定义图。
  * 详情弹窗的封面与徽标跟着认这两层（设过自定义图标就显示它，别出现「磁贴和详情不是同一张图」）。

* **③ 三个面板四角改直角**（用户：「刚才铺满的几个页面圆角全部取消」）。
  * 上一版留了 `rounded-l-ps-3xl`，本意是「贴着右边缘的一张卡片」；但面板左边缘正好压在侧栏
    分界线上，那道圆角会被读成「侧栏右下角缺了一块」而不是「一张卡片」。既然要铺满，就铺成干净的直角。
  * 改名页版面重排（用户：「将磁贴图标与游戏名称放在一行，磁贴背景放在下面」）：
    「名称 + 图标」同处一行（内容都少，各占半屏），「背景」另起一行的整幅。
    背景的预览也从「整卡预览」改成 **16:9 预览**，与上传的图比例一致 ——
    上一版把 8:9 的整卡预览配上 16:9 的图，两者对不上。

* **③-b 背景块内部再调：预览独占一行，按钮与说明换位置**（用户：「把文字说明和按钮换位置，
  预览图片铺满一行」）。
  * 不再左右分栏：**预览图独占一整行**（降到 340px 一列是不值得的，这是要判断
    「这张图当底图好不好看」的东西），下面**先按钮、后说明** —— 按钮是动作、说明是注解，
    动作在前更顺手（原来说明压在最上面，用户得先读完一句话才找到按钮）。
  * **预览必须有高度上限**（`max-h-[46vh]`）：16:9 铺满一行时高度 = 宽度 × 9/16，
    在电视上（正文宽约 1500px）就是 844px —— 单它一个就把整块顶出屏幕，按钮和说明得滚动才看得见。
  * 高度上限一旦生效容器就不再是严格 16:9，所以图改走 **`object-contain`**：
    宁可两侧留一点空，也**不能裁** —— 这是给用户判断「这张图当底图好不好看」用的，
    裁掉一半等于给错的信息。**磁贴上那一层仍是 `object-cover`**（它要填满卡片，
    而且用户的注意力在游戏图标上、不在底图构图）。

### 🐛 Bug Fixes

* **④ 【历史 bug】电视端「焦点/光标」那套逻辑整体回退到上游的做法。**
  * 用户口径：「ps5真机上焦点鼠标移动的方式最好改回跟上游一样，现在这样很别扭」，
    追问后明确「改回上游作者的处理方式一样，**所有导航都出光圈，全部是鼠标跳到对应的位置**」。
  * **先查清楚了「上游是什么」**：`frontend/src/utils/spatialNav.js` 是 fork 独有的（`.92` 加的），
    上游 `main` 里**没有任何键盘/指针处理**（上游的 Modal 也只有一颗关闭按钮）。
    所以「改回上游」= 我们**什么都不接管**。这一条是本轮最关键的判断 ——
    按字面「改回上游」去改一处 bug 会走偏，实际上是整层删掉。
  * **删掉的三层**（每一层都在补偿上一层带来的新问题，越补越别扭）：
    | 版本 | 加了什么 | 引出的新问题 |
    |---|---|---|
    | `.92` | 按几何位置**自己搬焦点**（方向键 → 找最近控件 → focus） | 光圈与系统光标开始分离 |
    | `.110` | 「确定键由我们自己点光圈」+ 捕获阶段把 click **改投** | 需要一套 `pointerMode`/时间窗/位移判据去分辨遥控器与鼠标 |
    | `.111` | 遥控器模式下**藏掉系统光标** | 又少了一处真实指示 |
  * **现在的实现只有一件事**：把光圈画在**指针当前压着的那个可操作元素**上。两条路指向同一个
    `.is-focus`（共用同一个 `paint()`，所以同一时刻只有一个环）：
    * **指针路** —— `mousemove`/`pointermove` 时用 `elementFromPoint` 找到指针下的元素，
      **向上找到最近的可操作祖先**给它挂环（指针底下十有八九是 `<img>`/`<span>`，
      而用户心里的「这个控件」是外面那颗按钮）。PS5 浏览器若用方向键驱动光标（发 trusted mousemove），
      这一步自动跟着走 —— **不需要我们做任何按键处理**。
    * **焦点路** —— 键盘 Tab 等原生焦点移动由 `focusin` 挂同一个环。
  * **三处刻意不做**（都是上一版踩过的坑）：
    ① **不动 `document.activeElement`** —— 只画环不搬焦点，就不会出现「指针划过输入框、
       把正在打的字抢走焦点」；不再需要 `pointerMode` / 400ms 时间窗 / 12px 位移判据那一整套。
    ② **不拦方向键**（不 `preventDefault`、不做几何跳转），方向键留给浏览器原生。
    ③ **不留「首次落焦」** —— 没有指针压着任何控件时不画环；光圈的定义就是「指针在哪」，
       凭空造一个位置只会又变成两个「我在哪」。
  * **`.111` 的藏光标一并撤掉**：光圈与系统光标永远重合，不再制造第二个「我在哪」，
    于是光标是真实指示、不该藏（`index.css` 里的 `html.nav-remote` 规则已删，历史留在注释里）。
  * **`pickNext` 与几何代价函数整段删除** —— 连带解决上一版那个「方向键走不进磁贴里四颗按钮」的
    别扭行为（那是自己搬焦点的算法自己造成的，不搬就没有这个问题）。
  * **保留两件**（与光标无关、且是本 fork 的真实缺陷修复）：① 返回键派发 `ps5-back`
    交外壳裁决（遥控器上没有浏览器返回按钮）；② `?navdebug=1` 只读排障面板（PS5 浏览器没有控制台）。
  * 顺带修 **`describe()` 认不出「只有图标的按钮」**：站内大量控件（磁贴上四颗、各处 `icon-btn`）
    没有 textContent，面板上显示成一排 `button.flex.items-center`，排障时等于没有这一栏。
    现在取不到可见文字就退回 `aria-label`。

* **⑤ 弹窗里按「返回」键会连带把整个游戏页退掉（`.92` 起就存在的老问题）。**
  * 根因：`spatialNav.js` 的返回键分支**无条件**派发 `ps5-back`，而它挂在 window 的**捕获**阶段、
    又注册在 App 挂载时；Modal 的 Escape 监听在冒泡阶段。捕获先跑 → App 先收到 `ps5-back`
    把视图栈弹一格（游戏页 → 上一页），Modal 再关掉自己。用户看到的是**关弹窗的同时整页也退了**，
    退回去那页还是空的（视图已切走），看起来像崩了。
  * 修法：返回键分支先看 `document.querySelector('[role="dialog"]')` —— 有浮层就把这次返回留给它。
    **副作用正是想要的行为**：`dismissible={false}` 的浮层（安装 / 删除进行中）因此也拿回了返回键。

* **⑥ 浮层开合后光圈会停在浮层底下的元素上**（视觉上像「环跑到弹窗后面去了」）。
  * 新增一个 `MutationObserver` 盯着 `role="dialog"` 的出现/消失，浮层出现时先清掉环；
    指针一动自然重新落上（浮层里也一样）。只在「有浮层且环还挂着」时才做事，常态几乎零开销。

* **⑦ 电视端：遥控器打开详情后光圈还停在磁贴上。**
  * 根因不在空间导航，在 `Modal` 的 effect 拆分：焦点进出原来和 Escape 监听写在**同一条 effect** 里、
    依赖数组带着 `onClose`，而调用方传的 `onClose` 多是内联函数，父组件每重渲染一次就换一个引用 →
    「cleanup 把焦点还给 opener → effect 再聚焦面板」这组动作在**弹窗还开着的时候**反复执行，
    只要有一次 cleanup 之后 effect 没跟上，焦点就留在外面了。
  * 修法：焦点进出独立成一条**只依赖 `show`** 的 effect，opener 用 `useRef` 记（不参与依赖），
    还原只发生在 `show` 由真变假的那一次 cleanup 上。
  * 顺带修好「关掉弹窗后焦点回不到原处」——之前会掉回 `body`。


### ✅ Verification

* **浏览器回归（Playwright + mock 后端 8081 + dev server 5199）**：
  * `game112.mjs` **142 / 142 全绿**（本轮从 117 条起，扩到 142 条）。
  * **新增 `nav112.mjs` —— 20 / 20 全绿**，**取代已删除的 `nav110.mjs`**
    （那份测的是「确定键由我们点光圈 + 藏系统光标」，已被本轮口径推翻）。
    `nav112.mjs` 分五组：① 光圈跟着指针走（含「压在图上时落到可操作祖先」「移到空白处光圈消失」）；
    ② 我们不再接管方向键（冒泡阶段探针读 `defaultPrevented` 计数为 0）+ 不再藏光标；
    ③ 键盘 Tab 焦点路也出环、且与指针路共用同一个画手（不会各留一个）；
    ④ 返回键：有浮层只关浮层不退页、无浮层才退页；⑤ `?navdebug=1` 面板仍在且报得出落点。
  * 本轮新增/改写的断言（都是**几何量、计算样式或接口字节**，不是「写了某个类」）：
    * 三个面板的 `getBoundingClientRect` 分别对齐**侧栏右边**与**视口右边**、上下贴边（6 条）；
    * 三个面板的 `border-radius` 四个角都是 **0px**（3 条，读计算样式，不看 className）；
    * 改名页的**板块几何关系**：名称与图标顶边对齐且左右相邻、背景在它们下方且横跨整行（2 条）；
    * **背景被真转成 1920×1080 DXT1 DDS**：`magic='DDS '` / `fourCC='DXT1'` / 1920×1080 /
      长度 = `128 + 480*270*8 = 1,036,928` / 头里 `linear` 与 `caps=0x1000`（4 条）；
    * 图标与背景预览**都回 `image/png`**（2 条）；
    * 只换图标时列表项带 `icon` 而**不带** `bg`，再换背景后两个都在（3 条）——「两层互不牵连」的正面证据；
    * 磁贴上同时出现 `/game_tile_icon` 与 `/game_tile_bg` 两张图（2 条）；
    * 两颗「恢复默认」分别清掉各自那一层（2 条）；
    * 在详情里按返回键**只关详情、不退页**（1 条，这是 ⑤ 的看门狗）;
    * 背景块的版面（③-b）：预览容器宽度 == 说明文字宽度（铺满一行）、预览高度 ≤ 面板高的 50%、
      说明之下仍有余量（整块一屏看完）、顺序为「预览 → 按钮 → 说明」（4 条）;
    * 上面这 4 条在 **1920×1080 电视端**再量一遍（高度上限在电视上才是真的绑定：正文宽 1516px，
      16:9 铺满就是 853px），并要求预览图**真的渲染出来了**才判 `object-contain`
      —— 不做这一步 `!img ||` 会让那条断言无条件通过（1 条专门守这个）。
  * **截图存证**：`.shots/game112/` 下 `rename112_panel.png`（改名面板：直角铺满 + 名称/图标一行 + 背景整行 16:9）、
    `saves112_panel.png`、`media112_panel.png`、`nav112_ring.png`（光圈落在磁贴按钮上）、
    `07_two_layers.png`（两层图并存）、`07_5_bg_tv.png`（电视端 1920×1080 下背景块整块一屏看完）。
* **其余套件同步复跑，全绿**：`nav112.mjs` 20/20、`acct111.mjs` 23/23、`avatar109.mjs` 19/19、`cropfix.mjs` 75/75。
  本轮动了 `Modal`（全站弹窗骨架）与 `spatialNav.js`（电视端全部按键与指针），
  这两个文件被多份套件共同依赖，所以都必须复跑。
* **DDS 规格三方对齐**（不只看长度）：把 mock 生成的 DDS **前 128 字节**与
  ① `make_pic0.py` 的 `build_bc1_header(1920,1080)`、② 仓库里控制台已验证的
  `ps5-payload-manager/assets/pic0.dds` 逐字节比对，**三份完全相同**。
  长度也一致（1,036,928 B）。这条比「尺寸对得上」强得多。
* **`mock-server.js` 里真写了一份 PNG 解码 + DXT1 编码**（Node 自带 `zlib`，不引第三方）：
  PNG 走 canvas 只会产出的那种形态（8 位、非隔行、colorType 2/6，逐行 5 种反滤波），
  DXT1 端点色按逐通道 min/max 取（与 `avatar.c` 的 DXT5 同一简化路子）。
  **不真编码就只能断言「前端把 PNG 传上来了」，规格对不对会一路拖到实机才暴露** ——
  写一份之后回归能直接量长度查 fourCC，规格漂了立刻红。
* `npx eslint`：本轮改动的文件（含 `mock-server.js`）**零问题**。
  `App.jsx` 报出 9 条 error / 2 条 warning，**全部落在本轮改动区域之外**。
* `npm run build` 通过：`frontend/dist/index.html` **909,960 字节**；
  产物内只有 `0.5.2.112`；`nav-remote` / 调试 `console.log` 残留 **0 处**
  （本轮开发期往 `Modal.jsx` / `Segmented.jsx` 加过 `console.log`，已全部清掉）。

### 📦 Release

* **本轮只改前端与 mock / 文档，未出包**（后端 C 代码一行未动）。下一轮出包时版本号为 `0.5.2.112`。
* 契约变更（后端照此实现）：
  * `POST /game_tile_icon?tid=` 裸 body（**PNG**，1:1，前端已缩到 1024×1024）→ `{ok, bytes}`
  * `GET /game_tile_icon_reset?tid=` → `{ok}`；`GET /game_tile_icon?tid=` → 图片（没设过 404）
  * `POST /game_tile_background?tid=` 裸 body（**PNG**，16:9，前端已缩到 1920×1080）→ `{ok, bytes}`
  * `GET /game_tile_background_reset?tid=` → `{ok}`
  * `GET /game_tile_bg?tid=` → **PNG 预览**（浏览器显示不了 DDS）
  * `GET /game_tile_bg_dds?tid=` → DDS 原始字节（1920×1080 DXT1，**1,036,928 B**）
  * `GET /game_list` 列表项新增可选 `icon`（number，自定义版本号）
  * `GET /game_detail` 新增 `title_icon_custom`（bool）

## v0.5.2.111 (CN-FORK)

> 用户口径（两条，分两轮给）：
> ① 「现在是按确定确实是点击焦点的那个位置，但是鼠标箭头还是在原来的位置，很违和。第二就是排查一下修改用户名为什么会影响用户ID，正常逻辑是用户ID生成后，只有修改账号ID才修改，改名是不修改ID的。」
> ② 「在调整一下，所有登入 登出 激活动作要，给中文提示，别报代码了，另外去掉反激活功能，激活几次就不允许反激活了，激活成功，按钮隐藏或者禁用。第一次登出会报这个，并且登出失败，第二次才能登出。」

### 🐛 Bug Fixes

* **① 遥控器模式下系统鼠标箭头还杵在原地（「两套我在哪」）。**
  * 现象：按 OK 已经正确落在彩色光圈上了，但系统指针纹丝不动，屏幕上同时存在两个「我在哪」。
  * **前提**：网页**没有任何 API 能移动系统/浏览器指针**（Pointer Lock 只能锁不能搬，合成 `mousemove` 也推不动真光标）。所以「让箭头跟着走」做不到，只能**让箭头消失**。
  * 实现：`spatialNav.js` 加 `setRemote(on)` —— 按方向键进入遥控器模式时给 `<html>` 挂 `nav-remote` 类，`index.css` 里 `html.nav-remote, html.nav-remote * { cursor: none !important }`；真·指针一动（过了位移判据）立刻摘掉这个类，USB 鼠标路径完全不受影响。卸载时也复位。
  * 选这个方案而不是「藏光标 + 自己画一个」，是因为**光标本来就是浏览器画的，藏掉即止**，不需要再维护一套假的指针坐标。

* **② 改名会连带改掉「账号 ID」——ID 的来源本来就选错了。**
  * 现象：界面上那一格 ID 在改过用户名之后会换值，而按用户口径「用户ID生成后，只有修改账号ID才修改，改名是不修改ID的」。
  * 根因链路：`account_obj()` 里未激活账号显示的是 `derivedId`，而它是 `offact_gen_id(name)` —— **按名字算 FNV-1a 哈希**。名字一变，这个值必然变。同时 `fill_status()` 用 `find_slot(name)` 定槽，改名后调用方拿到的可能仍是旧名 → `slot_for_name(旧名) = 0` → 走 `derived` 分支，正好把「按名字派生」这条路暴露到界面上。
  * 修法：新增 `offact_gen_id_for_user(uint32_t uid)` —— 按**本地用户 id** 派生（uid 是这个账号在系统里的稳定身份，改名只写注册表 `+0x200` 的 name，`+0x100` 的 uid 一个字节都不动）。`offact_activate()` 不再自己按名字派生（`if(!id) id = offact_gen_id(name)` 删除，改成 `if(!id) return -1`），默认值一律由 `accounts_action` 显式算好传下去 —— 于是「界面上预览的 `derivedId`」与「激活时真写进注册表的 id」用的是**同一个函数、同一个 uid**，永远一致。

* **③ 第一次点「登出」会报错并失败，第二次才行。**
  * 根因：`fill_status()` 原来**只按名字**找槽位，而名字来自 `sceUserServiceGetUserName()`。UserService 对一个**刚激活、还没进过壳层用户列表**的账号会滞后一帧（首帧取到空串或旧名）→ `find_slot()` 返回 0 → `out->slot = 0` → 登出走进「没有对应的注册表槽位，无法登出」直接失败。第二次点击时 UserService 已经跟上，于是成功。
  * 修法：`fill_status()` 改成 **uid 优先、名字兜底**（新增 `find_slot_by_uid()`，读槽基址 `+0x100`）。选 uid 优先而不是「名字优先、uid 兜底」，是因为**名字是全局唯一的** —— 万一 UserService 返回的是**另一个槽**的名字，名字路径会理直气壮地指向那一格，那比「找不到槽位」更糟（会去动别人的身份）。uid 只可能命中它自己那一格。
  * 顺带把槽里的真名回填进 `out->user_name`：调用方传进来的那个可能滞后或为空，而后面所有提示语、以及登入时写进 `auth.dat`/`config.dat` 的身份都用它。

### ✨ Features / 口径变更

* **④ 所有登入 / 登出 / 激活（以及改名、接口错误）的提示一律改中文，不再报英文诊断串。**
  * `npsignin.c` 的 `out->message`、`npsignin.c` 改名四层诊断（profile 缓存 / user home / user_list.dat 探针）、`accounts.c` 的 `message`、`api.c` 的 `serve_error` 文案、以及 `confirm=online` 那道 409 的说明，全部换成中文。
  * 技术标识保留原样（`auth.dat` / `config.dat` / `online.json` / `user_list.dat` / `login_flag` / 十六进制错误码），因为它们是要拿去核对文件的，不算「报代码」。
  * 顺带受益：`esc_json()` 原样透传 ≥ 0x20 的字节，UTF-8 中文进出无碍；`message` 缓冲 768 字节、JSON 体 4096 字节，中文串远未触顶。

* **⑤ 去掉「反激活」；激活成功后按钮整颗收起。**
  * 界面：原来那颗按钮同时兼任「激活」与「反激活」。现在只剩「激活」一件事 —— `acc.activated` 为真时**整颗不渲染**。选「隐藏」而不是「变灰禁用」，是因为电视端方向键会把灰按钮也算作一个落点：一颗永远点不动的按钮只会让人以为坏了，还白占一次按键。`doSignoutFull()` 随之删除。
  * 服务端 `accounts_action` 的 `deactivate` / `signout-full` 两个动作**保留**，只作为 HTTP 层的退路（它们是单账号退回未激活的唯一手段；删掉就只剩 `/api/safety/restore` 的全量还原）。界面已不再引用。
  * 词条 `acctools.deactivate` / `acctools.deactivate_tip` / `acctools.signout_full_tip` 从两份 overlay 里移除。

### ✅ Verification

* **主机端单测（WSL clang `-Wall -Wextra`，零告警）**：
  * **新增 `test/slot_resolve_test.c` —— 23 / 23 全绿**，专打 ③：名字滞后成空串 / 旧名 / **别人的名字**三种情况下都按 uid 解到正确的槽；`npsignin_signout` 在名字为空时**第一次点就成功**、且不再出现「没有对应的注册表槽位」；未知 uid 仍如实报错（不许假装成功）。C 组是「uid 优先」这条设计的关键性质：传入槽 3 的名字（Bob）配槽 1 的 uid，必须仍解到槽 1。
  * `test/offact_genid_test.c` **13 / 13**（含「预览 == 实际写入」「改名后 derivedId 一字不变」两条硬判据）；`test/safety_origin_test.c` **39 / 39**。
* **浏览器回归（Playwright + 本地 `vite preview`，`**/api/**` 全打桩）**：
  * **新增 `acct111.mjs` —— 23 / 23 全绿**，取代被本轮口径推翻的 `acct107.mjs`（已删除）。断言：未激活=「登入 + 激活」两颗；**已激活未登录 / 已登录都只剩一颗**；板块内不出现「反激活」「登出并反激活」字样；全程**不发出** `/accounts/deactivate` 或 `signout-full`；「登出」仍打 `/accounts/signout`（不带 `-full`）；英文包同样只剩一颗且已译。
  * `nav110.mjs` **16 / 16**（含新增的 ⑦ 藏光标三连：未按方向键不藏 / 按方向键藏 / 真指针一动立刻恢复）、`cropfix.mjs` **75 / 75**、`avatar109.mjs` **19 / 19**。
* `npx eslint src/components/views/AccountToolsSection.jsx`：干净；两份 overlay JSON 解析通过。

### 📦 Release

* **正式出包**：`NayePlmTools_0.5.2.111-me.elf`（NAYE00001 媒体栏）· `NayePlmTools_0.5.2.111-ga.elf`（NAYE00004 游戏栏）。
* 两包各 **9,875,208 字节**；内嵌标题 `NayePlmTools v0.5.2.111 by naye (e908ef5, built at 2026-10-05 09:41:14 UTC)`（**非 DEV**）、`PLDMGR_VER:0.5.2.111`；`0.5.2.110` 残留 0 处。
* sha256：`-me` = `8ddcfde58bda4dea72e7f75c2b51462b19063b9270696e97a81742e7dbfb4682`；`-ga` = `47b2bbc3e29f11278c906e4c8241965e0cb43925a0f5873e320c67d8d5738711`。
* **包内逐条核对**（不看日志，只看二进制内容）：
  * 新增中文串全部在包里且各 1 处 —— `已按实测基线复位` / `该账号还没有激活，请先点「激活」再登入` / `没有对应的注册表槽位` / `本来就是未登录状态` / `未找到该账号的注册表槽位` / `已激活，未登录` / `已还原 %d 条注册表原值`。
  * 旧英文诊断串 **0 处**：`RESTORE INCOMPLETE` / `signed out %s` / `already signed out` / `not activated — activate` / `no registry slot` / `nothing to preview` / `unknown action` / `rejected: new name`。
  * 反向断言 **0 处**：`登出并反激活`（`.108` 起的老判据，继续守住）。
  * **`反激活` 恰好 2 处，是刻意留的**：`已反激活 %s` 与 `反激活失败` —— 服务端 `deactivate` 动作的提示语。该动作按用户口径已从界面撤掉，只作 HTTP 层退路保留；界面路径永远碰不到这两句。
  * 前端产物 `frontend/dist/index.html` 里 `反激活` **0 处**（按钮与词条都已删除）。
  * `ABORTED` 剩 5 处**均非账号提示**：前端 PKG 管理器的状态标签 `ABORTED: "已中断"`、SMB2 系统常量 `SMB2_STATUS_*_ABORTED`、以及自动加载的一行日志 `[Autoload] ABORTED via PS Button.`。
* **坑 #4 再次实拍**：日志里 `-ga` 那趟写 `09:35:58`（第一版）/ 本次 `09:45:xx`，但两包内实际都是 **`-me` 那趟的标题**（`-me` 写 09:41:14）。两个包内容一致、无副作用，**核对永远只看包内实际内容**。
* **`.110` 的包（`dcfb48b`）已过期。**

## v0.5.2.110 (CN-FORK)

> 用户口径：「修一个 ps5 端的 BUG，ps5 端按上下按钮的时候会有一个彩色光圈，但是点击确定的时候依然是鼠标焦点生效，所以，这个功能可以考虑，鼠标也要跟着移动。」

### 🐛 Bug Fixes

* **① 电视端「确定」生效在鼠标指针位置，而不是彩色光圈所在的那个控件上。**
  * 现象：方向键能把焦点环（`.is-focus`）搬来搬去，但按 OK 之后被触发的却是**指针当前压着的那个元素** —— 焦点环形同虚设，用户以为自己在操作 A，实际点到了 B。
  * **为什么「让鼠标跟着走」这条路走不通**：网页里**没有任何 API 能移动系统/浏览器指针**。Pointer Lock 只能把指针锁住、不能搬走它；合成 `mousemove` 也移动不了真光标。所以按用户字面意思去「挪鼠标」不可能实现。
  * **但用户真正要的结果可以做到**：**让 OK 的落点脱离指针**。把 OK 从「浏览器在指针处点一下」改成「我们自己点光圈那个元素」，指针压在哪里就都无关紧要了。这才是这个 BUG 的正解。
  * 实现（全部在**捕获阶段**拦，且只认真实事件 `e.isTrusted`）：
    * **`navTarget` 成为权威**：光圈在哪儿就以哪儿为准，不再读 `document.activeElement`（老内核上它未必真的落在挂环的那个元素上）。`focusTarget()` 末尾记 `navTarget = el`。
    * **Enter/OK 分支自己激活**：`e.preventDefault()` 掉原生动作，直接 `navTarget.click()`。这样无论 OK 是「Enter keydown」「指针处 click」还是两者都发，结果都一致。
    * **`click` 捕获拦截四条规则**：① 我们自己 `el.click()` 发的是 untrusted click → 放走；② 刚由 OK 亲自激活过 → 800ms 内那一发 click **吞掉**（防浏览器在指针位置补的合成 click 造成双触发/点错）；③ `pointerMode`（真指针动过）→ 原样放行；④ 其余 click = 遥控器/键盘的确定在指针处点了一下 → **改投到 `navTarget`**。
  * **方向键/OK 自身带出来的光标事件要滤掉**：如果 PS5 浏览器在方向键或 OK 时顺带挪一下自己的光标（会发 trusted mousemove），就会误把「指针用户」模式打开、规则 ④ 失效。**只靠时间窗不够** —— 「看着屏幕想两秒再按确定」这种最普通的节奏早已超出任何时间窗。所以「是不是指针用户」不看「有没有 mousemove」，而看**指针有没有真的被挪走**，两条一起用：
    * **时间窗**：按过方向键之后的 **400ms** 内收到的 mousemove 一概不算。
    * **位移判据**：方向键后的**第一发** mousemove 只用来立基准（记坐标），从第二发起必须相对基准位移 **> 12px** 才算「用户在动鼠标」。方向键/OK 顺手带出的光标事件是原地或微动，永远过不了这一关；真鼠标是持续事件流，一推就过。
  * **插了 USB 鼠标的人不受影响**：真·指针一推就切 `pointerMode`，click 全程放行，直到下一次按方向键才切回遥控器模式。

### 🧰 Diagnostics

* **`?navdebug=1` 排障面板**：URL 带这个参数时，屏幕顶部中央挂一个只读小面板，实时显示最近一次按键/点击的处理结果 —— `ring`（光圈元素）、`mode`（remote / pointer）、以及 `OK → xxx` / `click 改投 → xxx` / `click 吞掉` 等。**实机上复现不了、又没有控制台，这个小面板就是唯一的取证手段。** 不带参数时零开销（连 DOM 节点都不建）。

### ✅ Verification

* **新增 `nav110.mjs`（Playwright，电视端形态 `?device=ps5`）—— 13 / 13 全绿**：
  * ① 方向键后 `.is-focus` 出现在侧栏按钮上、再按会移动；
  * ② **本 BUG 的直接判据**：指针停在 B、光圈停在 A → 在不移动指针的前提下发一发 trusted click → 断言**生效的是 A，B 一次都没被触发**；
  * ③ 指针模式下点 B 就生效 B（USB 鼠标路径未被打断）；
  * ④ OK 激活 A，且随后窗口内那一发 click 被吞掉（无双触发）；
  * ⑥ **专门打位移判据**：越过 400ms 静默窗后，模拟「OK 顺带带出一发原地 + 一发 7px 微动」的光标事件 → 确定键仍然落在 A 上（若没有位移判据，这一格会退化成 B）；
  * ⑤ `?navdebug=1` 面板挂出并报出 `ArrowDown → …` / `mode : remote`。
  * 手法说明：用 `page.mouse.down()/up()` 在**不移动**指针的前提下发 trusted click，正是实机「OK 在指针处点一下」的样子；在侧栏每颗按钮上挂自己的捕获监听 + `stopImmediatePropagation`，于是「谁被真的激活了」= `window.__fired`，与应用自身副作用无关。
* **既有回归未受影响**：`cropfix.mjs` 75 / 75、`acct107.mjs` 23 / 23、`avatar109.mjs` 19 / 19。
* `npx eslint src/utils/spatialNav.js`：干净。

### 📦 Release

* **正式出包**：`NayePlmTools_0.5.2.110-me.elf`（NAYE00001 媒体栏）· `NayePlmTools_0.5.2.110-ga.elf`（NAYE00004 游戏栏）。
* 两包各 **9,875,208 字节**；内嵌标题 `NayePlmTools v0.5.2.110 by naye (dcfb48b, built at 2026-10-05 08:19:32 UTC)`（**非 DEV**）、`PLDMGR_VER:0.5.2.110`；`0.5.2.109` 残留 0 处；反向断言「登出并反激活」0 处。
* sha256：`-me` = `ec17b3e1ed3295c2ac8d4593e7683e9e2e6ece377850f62b8f4c3e2464678020`；`-ga` = `5c551c943d0a4a7f9619c53105d2a6839cfabdb2801ffb7fe9c646ad7751589c`。
* **包内逐字核对最终版判据**：压缩后的 `spatialNav` 里能读到 `if(n)return n=!1,void(o={x:s,y:r})`（方向键后第一发只立基准）与 `if(e*e+t*t<144)return`（12² = 144，位移判据）—— 确认带位移判据的那一版真的进了包，不是只看日志。
* **`.109` 的包（`b8ea772`）已过期**。

### 📌 未决（待用户定夺）

* 实心按钮对比度方案 A（照 `#F43E06`，3.79:1）/ B（压深 `#D93A00`，4.61:1）/ C（配深色字 `#1A0A02`，5.09:1）。
* 游戏磁贴徽标收敛是否回退。
* PIN 卡的两个小毛刺（重复 Toast / 双定时器）是否顺手清掉。
* `SettingsView.jsx` 内置 `CHANGELOG` 数组停在 `v0.5.2.67`，`.68`–`.110` 未补。

## v0.5.2.109 (CN-FORK)

> 用户口径：「账号管理的提示标签 改的好看一点，太大带占地方了，能不能放到中下边，另外头像上传功能还是不生效。」追问后确认：提示标签 = **点完激活/登入弹出来的那条提示**（Toast），位置要「挪到卡片底部、居中」；头像的现象是**点上传后一直显示「上传中」，不变、不报错、也不提示**，在**主机上（PS5 浏览器开 :8911）**测的。

### 🐛 Bug Fixes

* **① 头像上传「一直转、不报错、也不结束」—— 真根因在后端 `psaccounts_handle` 的流式状态回收判据上。**
  * 旧代码用 `*upload_data_size == 0` 当作「上传结束」的信号，然后 `free` 掉流式状态 + `free(st)` + `*con_cls = NULL`。
  * 但 MHD 的调用序列是：**① size=0 建状态 → ② 若干帧带 body → ③ size=0 收尾**。第 ① 帧的 size 也是 0，可那时 `avatar_upload_request` 只是把文件打开、`*state` 仍指向新建的流式状态，**根本没结束**。于是第 ① 帧就把连接状态扔了 → MHD 重新调用时又拿到 `*con_cls == NULL`、重新分配、又进这个分支、又不消费 body……**请求永远走不到收尾那帧，浏览器就一直等**。这与实机现象逐字吻合。
  * 修法：判据改成 **`st->avatar_upload_state == NULL`** —— `avatar_upload_request` 只在「收到完整 body 已经答完」或「中途出错已经答完」时才把 `*state` 置回 NULL。这也正是**源工程**（`D:\offact-new/accounts/src/websrv.c`）的做法：它压根没有这个 free 块，流式状态一律交给 `MHD_OPTION_NOTIFY_COMPLETED` 回调回收。现在与它对齐。
  * **溯源**：`diff` 过原版 `avatar.c` —— **逐字节一致**。所以问题不在移植的转换/编码逻辑，而在并入主 ELF 时新加的这段回收代码。这类「合并两套 MHD 生命周期」的地方，以后一律以源工程的回收路径为准。
* **② 前端把我们自己的超时误判成「用户取消」，静默吞掉。** `uploadCropped` 的 catch 里旧写法是 `if (err.name === 'AbortError' || ctl.signal.aborted) return` —— 而 `fetchWithTimeout` 30 秒超时触发的**也是 AbortError**，于是超时既没有错误提示、也没有任何交代（`.106` 及更早版本连超时都没有，所以是永久卡住）。现在 `closeCrop()` / 卸载兜底在 `abort()` 前给 controller 打 `userCancelled = true`，catch 里先看这个标记：**用户主动关窗才静默**，超时则明确报「头像上传超时（30 秒没等到主机响应）—— 主机端可能卡在图片转换或写盘，请重试」。三条收场路径现在都有结局：成功 / 后端拒（带原因）/ 超时。
* **③ 本地 mock 的头像是个死胡同。** `mock-server.js` 原先 `/api/avatar` 写死 404、upload 只数字节不落地 —— 于是本地预览里「上传成功但缩略图永远是首字母占位」，**长得和实机那个故障一模一样**，白白把排查方向带偏。现在上传的字节留在内存、`/api/avatar?id=` 原样吐回来，本地就能验完「选图 → 裁剪 → 上传 → 应用 → 缩略图刷新」整条链。

### 🎨 Design & UI Improvements

* **Toast 挪到底部居中 + 整体缩小**（用户：「太大太占地方了，能不能放到中下边」）。
  * 位置：容器从 `fixed top-0 right-0 p-8`（右上角）改为 `fixed inset-x-0 bottom-0` + `flex-col items-center`（**底部居中**）。手机端抬到底部导航栏之上（`calc(var(--bar-h) + env(safe-area-inset-bottom) + 1rem)`）；电视端底部栏是 `hidden`，只留 1.5rem 边距。
  * 体量：内边距 16→10px、圆角 16→12px、图标 24→16px、正文字号压到 **13px**、阴影 `shadow-2xl`→`shadow-lg`；去掉 `font-bold uppercase`（全大写只影响英文，中文夹在中间很吵）。
  * 实测：高 **56px+ → 36px**，水平中心与视口中心偏差 **0.0px**，距底 24px。语义与配色档位（成功 emerald / 错误 red）与驻留时长（4s / 9s）一行未动。

### ✅ Verification

* **新增 `avatar109.mjs`：19 / 19 全绿**（这是本轮两个改动的判据，全部是可证伪的数值断言）：
  * **① Toast 几何**：水平中心偏差 ≤ 4px、距底 ≤ 60px、在上半屏之外（钉死「旧版是右上角」）、高 ≤ 44px、字号 ≤ 14px、`text-transform` 非 uppercase。
  * **② 成功路径**：选图 → 弹窗出现 → 点「确认使用」→ 弹「头像已更新（写入 11 个文件 · …）」且是成功档；缩略图 `src` 带**新的** `_=` 时间戳、且 `naturalWidth > 0`（真的解码出来了）。
  * **③ 后端拒绝**：stub 返回 `{ok:false,error:'decode failed: unknown image type'}` → 必须弹**红档**且**带主机给的原因**。
  * **④ 主机不答（本轮的硬判据）**：stub 让上传请求**永不 fulfill** → 先断言按钮确实停在「上传中…」，再断言 **30 秒内必须弹「头像上传超时…」**、且是红档、且 busy 归零。**旧代码在这一格是静默 return。**
* **既有回归未受影响**：`cropfix.mjs` **75 / 75**、`acct107.mjs` **23 / 23**。
* **mock 闭环实测**：`curl` 三连 —— 未上传 `GET /api/avatar?id=` → **404**；`POST /api/avatar/upload`（2008 字节）→ `{"ok":true,...}`；再 `GET` → **HTTP 200 / image/png / 2008 字节**；无 `id` 的 `GET /api/avatar` → info JSON。
* `npx eslint`：`Toast.jsx` / `AccountToolsSection.jsx` 干净；`App.jsx` 仍是 11 条存量问题（与 HEAD 逐条一致，本轮未新增）。

### 📦 Release

* **正式出包**：`NayePlmTools_0.5.2.109-me.elf`（NAYE00001 媒体栏）· `NayePlmTools_0.5.2.109-ga.elf`（NAYE00004 游戏栏）。
* `.108` 的包（`12e2ba5`）**已过期** —— 它带的是没修上传的那个后端。

### 📌 未决（待用户定夺）

* 实心按钮对比度方案 A（照 `#F43E06`，3.79:1）/ B（压深 `#D93A00`，4.61:1）/ C（配深色字 `#1A0A02`，5.09:1）。
* 游戏磁贴徽标收敛是否回退。
* PIN 卡的两个小毛刺（重复 Toast / 双定时器）是否顺手清掉。
* `SettingsView.jsx` 内置 `CHANGELOG` 数组停在 `v0.5.2.67`，`.68`–`.109` 未补。

## v0.5.2.108 (CN-FORK)

> 修正 `.107` 的账号按钮语义 —— 用户口径（定稿）：「逻辑做错了，登出 就是只登出，反激活 即 登出并反激活，但是按钮文字不要改。」**本轮只改前端 + 语言包 + 文档，后端契约沿用 `.107`。**

### 🔀 Behavior Changes

* **按钮文字回到 `[登出]` + `[反激活]`，一个字都不多。** `.107` 曾把已登录时的第二个按钮改叫「登出并反激活」，用户否掉了：界面里不该出现这个词。现在两种情况**都叫「反激活」**，区别只在动作：
  * `[登出]`（未登录时是 `[登入]`）→ `/api/accounts/signout`：**只退登录态**，离线激活留着，下次点「登入」不用重新激活。
  * `[反激活]` → **已登录时**走 `/api/accounts/signout-full`（**登出 + 反激活一步到位**：会话先收掉、激活再退掉）；**未登录时**走 `/api/accounts/deactivate`（单纯反激活）。
* **不一致状态仍然被避开**：已登录时不再提供"只反激活"那条路 —— 那会让 `account_id` 归零而 signin_flag 还在（界面显示"未激活 / 已登录"），再点「登入」会被服务端以 `not activated` 拒掉。
* 档位不变（`.104` 契约）：撤销类里只有「登出」吃红档，`[反激活]` 恒为中性档。
* 提示语跟着改口径：`acctools.signout_tip` = 「只退出登录；账号保持已激活…」；`acctools.signout_full_tip` = 「已登录时「反激活」= 退出登录 + 清掉离线激活，一步到位」。**删掉不再使用的 `acctools.signout_full` 词条**（中英文都删，避免以后有人误用回那个词）。

### ✅ Verification

* **`acct107.mjs`：23 / 23 全绿**（脚本同步改口径）。新增/改写的断言：
  * 「已登录 · 第二个仍是『反激活』（文字不许改）」+ 「已登录 · **界面不出现『登出并反激活』**」—— 直接钉死用户这条要求。
  * 「已激活未登录 · 点『反激活』打到 `deactivate` **而非** `signout-full`」—— 防止未登录时多退一层。
  * 「点『反激活』打到 `/accounts/signout-full`（已登录 = 登出 + 反激活）」。
  * 英文包断言改成「第二个按钮**不含中文**」（`Sign out | Deactivate`），比原来只判非空更严。
* `npx eslint src/components/views/AccountToolsSection.jsx`：零告警。

### 📦 Release

* **正式出包**：`NayePlmTools_0.5.2.108-me.elf`（NAYE00001 媒体栏）· `NayePlmTools_0.5.2.108-ga.elf`（NAYE00004 游戏栏）。
* `.107` 的包（`8065f19`）**已过期** —— 它内嵌的是被否掉的「登出并反激活」文案。

### 📌 未决（待用户定夺）

* 实心按钮对比度方案 A（照 `#F43E06`，3.79:1）/ B（压深 `#D93A00`，4.61:1）/ C（配深色字 `#1A0A02`，5.09:1）。
* 游戏磁贴徽标收敛是否回退。
* PIN 卡的两个小毛刺（重复 Toast / 双定时器）是否顺手清掉。
* `SettingsView.jsx` 内置 `CHANGELOG` 数组停在 `v0.5.2.67`，`.68`–`.108` 未补。

## v0.5.2.107 (CN-FORK)

> 头像上传/裁剪四项缺陷修复 + 账号「登出」不再连带反激活。用户口径原文：「头像，上传、裁剪功能异常，部分图片会严重变形，上传卡死，需增加拖拽 裁剪。」／「梳理一下 账号登入和激活的关系，为什么登出以后自动反激活了，这个逻辑是不是有问题」。**本轮改前端 + 语言包 + 后端 psaccounts + 文档。**

### 🔀 Behavior Changes

* **「登出」不再连带反激活。**
  * **根因**：登出走的不是"撤销登录"，而是 `safety_restore_scope(uid)` —— **按 uid 整段回滚**。而离线激活写的那 3 个键（`account_id` / `type` / `flags`）和登录态写的 22 个键记在**同一个 uid 名下**，`restore_matching()` 只按 scope 过滤、不看这条记录是谁写的，于是激活被一起退了回去。这是安全模型（"登出 = 把我动过的一切都还原"）的副作用，不是显式调用 `offact_clear()`。
  * **修法**：给备份记录加**来源**字段（`SAFETY_ORIGIN_ACTIVATE` / `SIGNIN` / `RENAME` / `NONE`），`restore_matching()` 在 scope 之上再筛一层来源。两条路：
    * `/api/accounts/signout` → `SAFETY_ORIGIN_M_SIGNOUT_ONLY`（SIGNIN + 未标注的老记录）：**只退登录态，离线激活留着**，下次点「登入」即可，不用重新激活。← 界面上的「登出」
    * `/api/accounts/signout-full` → `M_ALL`：不分来源全退，回到未激活。← 界面上的「反激活」，也就是**原来「登出」的行为**
  * **界面（按钮文字不变）**：按钮行始终是 `[登出]` + `[反激活]`。**「登出」= 只登出；「反激活」= 登出并反激活** —— 已登录时点「反激活」走 `signout-full`，一步把会话收掉、激活也退掉；未登录时点它仍是单纯反激活（走 `deactivate`）。两种情况**文字都是「反激活」**，界面不出现「登出并反激活」。这样也就不存在"已登录状态下单独做反激活"的不一致状态（`account_id` 归零但 signin_flag 还在，界面显示"未激活 / 已登录"，再点「登入」会被服务端以 `not activated` 拒掉）。两档配色沿用 `.104` 的契约：撤销类里只有「登出」吃红档，这一格回中性档。
* **改名（`RENAME`）不被「登出」退掉。** 退掉登录不该把用户改过的名字弹回去；要连名字一起还原就走「完全还原」。
* **老日志兼容**：`reg.idx` 第 6 字段（来源）缺失或越界一律降级成 `NONE`，而 `NONE` **被两种还原都覆盖** —— 宁可多回滚（回滚 = 写回原值，只会更接近原始状态），也绝不能因为格式换代把原值丢掉、永久失去还原能力。
* **`/api/safety` 的 entries 现在带 `"origin"` 字段**（`activate` / `signin` / `rename` / `none`），排查时能直接看出每条待还原记录是谁写的。每条 JSON 预算从 80 字节提到 112。

### ✅ Verification — 账号登出拆分

* **新增主机端单测 `test/safety_origin_test.c`：39 / 39 全绿**（打桩 6 个 `sceRegMgr*`，`clang -Wall -Wextra` 零告警）。全是断言，不是"看着对"：
  * **A. 老日志兼容**：手工造 5 字段（无 origin）与 6 字段 origin=99 的 `reg.idx`，断言两者都降级成 `NONE`、「仅登出」的掩码也覆盖它们、完全还原能把原值退回去。
  * **B. 激活 → 登入 →「仅登出」**：断言退掉 2 条登录态、**`account_id` / `type` / `flags` 原封不动**、日志里剩的正是那 3 条 `M_ACTIVATE`。
  * **C.「完全还原」**：断言 3 条激活记录也退掉、日志清空。
  * **D. 改名不被「仅登出」退掉**（0 条），但「完全还原」会退（1 条）。
  * **E. uid 隔离**：B 的「仅登出」退不掉 A 的记录。
  * 顺带钉死一个真实重叠：槽 1 的 `FLAGS` 就是 `LOGIN_FLAG`，激活和登入都会写它 —— 记录归属**最早那次写入**（`ACTIVATE`），这正是「仅登出」能保住它的原因。
* **新增 Playwright 界面回归 `acct107.mjs`：23 / 23 全绿**。钉死三种状态组合下的按钮文案与档位，以及**接口契约**：点「登出」必须打到 `/api/accounts/signout`（**不带** `-full`）、点「反激活」在已登录时必须打到 `/api/accounts/signout-full`、在未登录时必须打到 `/api/accounts/deactivate` —— 发错一个，用户就又回到「点登出却被反激活」。另外断言**界面里不出现「登出并反激活」这几个字**（按钮文字不许改）。中英文语言包都验。

### 🐛 Bug Fixes

* **① 「部分图片严重变形」——真正根因是 Tailwind preflight，不是状态残留。**
  `img,video{max-width:100%;height:auto}` 是 preflight 的全局规则。裁剪图是**绝对定位**且靠 inline `width/height` 定尺寸的，`max-width:100%` 把它的**布局宽度掐到容器宽 258px**，而高度仍按自然高撑开 —— 3200×1200 的图被压成 `258×1200`，再按 cover 缩放下来只剩 **55.9px 宽**（源图宽高比 2.667 → 渲染 0.215）。图越宽压得越狠，方形图几乎看不出，这正是「部分图片」的由来。
  * 修法：给裁剪 `<img>` 显式写 `maxWidth:'none'` / `maxHeight:'none'`，inline 才压得住 preflight。
  * **定位手段**：写了一套 Playwright 数值回归（见下），第一条断言就是「`<img>` 的 `offsetWidth` 必须等于自然宽」，一跑就红。
* **② 「上传卡死」——三条独立成因，全部堵掉。**
  * **取景框内容区只有 258×258**：全局 `box-sizing:border-box` 让 1px 边框从 260 里吃掉 2px，而 `clampView`/`zoomTo`/导出全按 `CROP_SIZE=260` 算，预览与导出每边差 1px。取景框改 `boxSizing:'content-box'`，内容盒严格等于 `CROP_SIZE`。
  * **弹窗在上传中锁死**：原来 `dismissible={!busy}` + 取消键 `disabled={busy}`，主机一慢用户出不来。改成恒可关闭（叉 / 遮罩 / Esc / 取消都通），关窗即 `abort()` 在途请求并归零 `cropBusy`。
  * **两条 fetch 无超时**：`fetchWithTimeout()` 给上传 30s、应用 20s；`canvasToPngBlob()` 给 `toBlob` 加 4 秒兜底（老 WebKit 的大画布偶发**不回调**，而旧代码整条上传链都写在那个回调里），超时退回 `toDataURL` 手工转 Blob。
* **③ 「需增加拖拽裁剪」——重写整个 `CropModal`。**
  * 拖拽平移（window 级 `pointermove`/`pointerup`，指针移出取景框也跟得住）、滚轮缩放（原生 non-passive 监听，否则弹窗跟着滚）、双指捏合。
  * 三套 down 处理器按能力择一：有 `PointerEvent` 走 pointer（`pointerType==='touch'` 让位给 touch 分支），否则 mouse + touch，**避免双份处理**。
  * **边界夹取**：图片恒盖满取景框，拖不出白边。
  * **初始适配 contain → cover**：contain 会在方框里留白边，导出后是透明条，这不是头像该有的取景方式。
  * 缩放以**取景框中心为锚点**（把「框中心对应的那个图上点」钉住），滑杆走**相对倍数** 1×–4×（绝对 scale 在几千像素的图上只有 0.06 量级，整条滑杆区间挤在头几像素里根本没法调）。
  * **导出边长 260 → 440**：PS5 头像有 64/128/260/440 四档，440 档原先只能由 260 放大，发虚。改用**九参数 `drawImage`** 只光栅化取景框对应的那块源矩形 —— 6000×4000 的照片也只画这一小块，不再整张解码进画布（大图假死的来源）。
  * **换图重置改用 `key={src}` 强制重挂**，而不是在 effect 里 setState：重挂天然把 `nat`/`view`/`minScale`/`failed`/`note` 全部归零，也避开 `react-hooks/set-state-in-effect`。
  * **`nat` 到手之前不渲染 `<img>`**（只显示「正在解码图片…」），从根上杜绝「拿上一张的尺寸布局新图」。
* **④ 顺带：选图改 `URL.createObjectURL` 替代 `readAsDataURL`。** 后者要把整张图 base64 化（体积 ×1.33）再整个塞进 state，手机拍的十几 MB 原图足以卡住老内核。object URL 零拷贝，关窗/卸载时 revoke。
* **⑤ 新增拖放选图**：把图片直接拖到头像框上（PC 浏览器顺手；`dragOver` 时描边高亮）。非图片文件在前端挡下（不弹裁剪窗）。

### ✅ Verification — 头像裁剪

* **新增 Playwright 数值回归 `cropfix.mjs`：75 / 75 全绿。** 不是「看着对」，每条都是可证伪的断言：
  * 5 种源图（超宽 3200×1200 / 超高 900×2700 / 大图 4000×3000 JPEG / 正好方形 260×260 / 极小 64×64）× 13 条断言：自然尺寸、**布局尺寸未被 max-width 压扁**、渲染宽高比 == 源图宽高比（±0.6%）、初始 cover 盖满、取景框内容区 260×260、拖拽改变位移、拖到极限不露底、滑杆 4× 生效、确认键可用、Esc 可关闭、上传体是 440×440 PNG（直读 IHDR）、**导出中心像素 == 源图中心洋红锚点**、确认后弹窗关闭。
  * 连裁回归（连裁 3200×1200 → 900×2700，第二张不许带第一张的尺寸）。
  * 非图片文件被拒。
  * **卡死专项 6 条**：用 `page.route` 让上传请求**永久挂起**，验证挂起期间确认键进入「上传中…」、取消键未被禁用、点取消能关、Esc 能关、关掉后可立即重开且状态干净。
* **真实版本前后对照**：另建 `dist-before`（`git stash` 后构建 `HEAD` 产物）跑同一套用例，把「布局尺寸」量出来对比 —— 修复前 `2400×900` 源图的 `<img>` 布局是 `258×900`（宽高比 0.287），修复后是 `2400×900`（2.667，与源图一致）。对照页：`.shots/crop107/crop-fix-compare.html`。

### 🧹 Maintenance

* 版本号同步补齐：`include/pldmgr.h` → `0.5.2.107`；**另 4 处原先停在 `.102`（`.103`–`.106` 漏同步）一并推到 `.107`** —— `frontend/mock-server.js`、`DonateView.jsx`、`locales-overlay/en-US.json`、`locales-overlay/zh-CN.json`。
* 语言包新增/修订 9 条：`acctools.crop_hint`（不再写「260×260」）、`crop_loading`、`crop_load_fail`、`crop_export_fail`、`crop_export_fail2`、`not_image`、`noblob`（不再是「浏览器不支持 toBlob」）、`avatar_tip`。
* `jget()` 加可选 `opts`（`signal` / `timeoutMs`）；调用方自己 abort 时**原样抛出**，否则会被降级成「接口不可达」，关个窗还弹红条。
* `AccountToolsSection.jsx` 回到 lint 干净（全仓 38 个问题均为存量，本次未新增）。

### 📦 Release

* **正式出包（短哈希 `8065f19`）**：`NayePlmTools_0.5.2.107-me.elf`（NAYE00001 媒体栏）· `NayePlmTools_0.5.2.107-ga.elf`（NAYE00004 游戏栏），两包各 9,875,208 字节。
  * 二进制核对：内嵌标题 `NayePlmTools v0.5.2.107 by naye (8065f19, built at 2026-10-05 07:18:29 UTC)`（非 `DEV`）、`PLDMGR_VER:0.5.2.107`、titleId 分别 NAYE00001/NAYE00004；本轮新标记 `signout-full`×2 / `the offline activation is kept` / `"origin"` / `正在解码图片` 均在包内。
  * **出包踩坑（已记入 HANDOVER §3）**：`build_tiles.sh` 复用已有的 `frontend/dist`，而标题占位符 `[[TITLE_PLACEHOLDER]]` 是**一次性消费**的（注入 recipe 直接 `sed` 改 dist）。第一次出包时包内是 `.107` 的代码、标题却写着上一个提交 `6b51ee7` —— 必须先 `rm -rf frontend/dist` 重建才能让标题跟上。

### 🔎 已核实非缺陷 — 「界面刷新后 PIN 进度条继续走」

用户附带提及（原话：「另外界面刷新后 pin 吗进度一直在走，当然这个不是什么严重的问题」）。**核实结论：良性，且是刻意设计，不改。**

* **配对会话活在主机的 ELF 进程里，不在页面里**：`rp_session_open` + `rp_deadline = time(NULL) + PIN_TTL_SEC`（`accounts.c`，`PIN_TTL_SEC = 180`）。网页只是显示器。
* **刷新走的是「接回」而不是「重开」**：`AccountToolsSection.jsx` 挂载时读 `/api/remoteplay/pin/status`，若 `ok && status === 'waiting' && pinRaw` 就 `startWaiting(s)`，弹一条「接回进行中的配对：还剩 N 秒」。`remaining` 是服务端算的**真实剩余秒数**（`remain = rp_deadline - now`），所以进度条**接着正确的值走，不会重置回 180** —— 手滑刷新不该丢掉已经生成的 PIN，这正是设计意图。
* **180 秒是硬上限**：超时由**进程内看门狗线程**收尾，不看网页还在不在。三条收尾路：配对成功 / 配对失败 / 超时。
* **想提前中止**：点「重新生成」—— `pin_generate_locked()` 开头 `if(rp_session_open) rp_close_locked()`，先收掉旧会话再开新的（与主机菜单行为一致）。
* 顺手记下两个**真实小毛刺**（本轮未改，等定夺）：①「接回进行中的配对」这条 Toast 每次重新挂载都会弹（侧栏切走再切回也重挂）；② `remain` 被两套 1 秒定时器同时驱动（本地 `cdTimer` 递减 + `pollTimer` 用服务端值覆盖），秒数偶发抖动 `178→177→177→176`，功能无影响、纯冗余。

### 📌 未决（待用户定夺）

* 实心按钮对比度方案 A（照 `#F43E06`，3.79:1）/ B（压深 `#D93A00`，4.61:1）/ C（配深色字 `#1A0A02`，5.09:1）。
* 游戏磁贴徽标收敛是否回退。
* `SettingsView.jsx` 内置 `CHANGELOG` 数组停在 `v0.5.2.67`，`.68`–`.107` 未补。

---

## v0.5.2.106 (CN-FORK)

> 子模块标题辨识度上调。用户口径原文：「所有这类子模块的标题辨识度都提高一些，具体有什么措施你自己思考一下」（附三张截图：游戏列表 / 入口清单 / 账号·远程游玩配对）。**本轮只改前端与文档，未出包。**

### 🎨 Design & UI Improvements
* **板块标题辨识度上调**：三处截图都是 `ui/Section.jsx` 的 `.panel-head` 标题，所以**只改 `Section.jsx` + `index.css` 两个文件**，全站所有板块头一次性生效。四条措施：
  * **字号 `text-sm`(0.85rem≈13.6px) → `1.05rem`(≈16.8px)**：最直接的一档；13.6px 在 1080p 电视上本就偏小。
  * **字重 600 → 700**：近黑底上的浅色文字笔画越细越「虚」，加粗一档才立得住。
  * **字距 `+0.01em`**：汉字彼此紧挨时笔画容易连成一片，给一点缝让每个字独立成形。
  * **颜色不动**（仍是 `text-white`）：白字压 `#12151A` 已有约 19:1，瓶颈在字号与字重、不在色值。
* **两处配套**：图标同步 `w-4 h-4`(16px) → `1.15rem`(≈18.4px)（标题变大、图标不跟上会显得小一圈）；计数徽章 `text-zinc-500` → `text-zinc-400`（深底上那颗「共 8 款」原本太灰）。
* **做法**：标题从原子类（`text-sm font-semibold`）收成**骨架层语义类 `.panel-title`**（定义在 `index.css` 的 `.panel-head` 之后）。以后调标题只改这一处，也不会再冒出「某个视图自己写一套」的副本。**浅色主题不用另翻**：`.panel-title` 不写 `color`，浅档仍由 `light-theme.css` 把 `text-white` 反着映射成近黑 `#1f2328`。
* **层级未被压平**：板块内嵌卡的小标题（账号板块的「远程游玩配对 PIN」、温控的「风扇温控」）仍是 `text-[14px]`，落在板块标题（16.8px）下一档；`.panel-head` 另有图标 + 底边线做结构区分。

### 📦 Release
* **正式出包（短哈希 `0e9afb2`）**：`.103`–`.106` 四轮 UI 改动合成一个提交后一次出包。
  * `NayePlmTools_0.5.2.106-me.elf`（NAYE00001 媒体栏）· `NayePlmTools_0.5.2.106-ga.elf`（NAYE00004 游戏栏），两包各 9,875,208 字节。
  * 二进制核对：内嵌标题 `NayePlmTools v0.5.2.106 by naye (0e9afb2, ...)`（非 `DEV`）、`PLDMGR_VER:0.5.2.106`、titleId 分别 NAYE00001/NAYE00004、新前端标记 `pin-bar-fill`/`panel-title`/`#f43e06` 均在包内。

### ✅ Verification
* `npm run build` 通过（`dist/index.html` 818.9 kB / gzip 226.4 kB）。
* `npm run lint` 38 项（32 error / 6 warning），与改动前基线**完全一致**。
* 深/浅两套主题下逐块实拍复核（「改动前」用注入样式还原旧值，非 git stash）。
* **未实机（PS5）验收。**


## v0.5.2.105 (CN-FORK)

> 账号板块两处微调：「同步」按钮补小图标（与「修改」同尺寸同风格）+ PIN 倒计时进度条加厚、炫酷、换色。用户口径原文：「同步按钮也加个小图标，跟修改按钮一样大，一样的风格」「生成 pin 的进度条要高一些，炫酷一些，颜色可以用别的」。**本轮只改前端与文档，未出包。**

### 🎨 Design & UI Improvements
* **「同步」按钮补小图标**：在文字前加 `<RefreshCw className="w-4 h-4" />`，与「修改」钮的 `<Check className="w-4 h-4" />` 同尺寸同结构；间距由骨架层 `.btn > * + *` 给（PS5 老内核 flex 的 `gap` 不生效，不可改成 `gap`）。忙时文案切「处理中…」，与「修改」一致。档位不变（仍是 `.btn-warn` 金档）。
* **PIN 倒计时进度条重做**：从 `h-2`(8px) 纯色条（`bg-ps-blue`，主色换朱橙后成了一条橙条）换成骨架层新组件 `.pin-bar` / `.pin-bar-fill`。
  * **加厚**：高度走新令牌 `--pin-bar-h`（PC 14px / 电视 20px）。它是装饰条，不跟 `--ctl-h` 走，避免顺手改掉按钮/输入框尺寸。
  * **炫酷**：填充段走「青 `#22D3EE` → 蓝 `#3B82F6` → 紫 `#8B5CF6`」冷色渐变 + 外发光，再叠一条循环扫过的白色流光（`::after` + `@keyframes pin-sheen`）。
  * **临界反馈**：剩余 ≤30 秒整条切「琥珀 `#F59E0B` → 红 `#EF4444`/`#DC2626`」渐变、发光转红、并整体脉冲（`@keyframes pin-pulse`）。
  * 宽度过渡移到 `.pin-bar-fill` 的 `transition: width 1s linear`，组件只给宽度。
* **为什么用冷色**：主色已是朱橙（暖色），进度条再走暖色就糊成一片；青→蓝→紫与主色互补，从「橙色主操作」里一眼跳出来，也更像「正在跑的技术感」。用户说「颜色可以用别的」属授权，未再出候选。

### ⚠️ Known Notes
* **两个实现坑**：① 渐变必须写在 `background-image` 上 —— 收口层只映射 `background-color`，用 `bg-*` 类会被翻成中性底；② 轨道不能设 `overflow:hidden`（会裁掉填充段的外发光），需要裁的流光放在 `.pin-bar-fill` 上（`overflow` 只裁后代，不裁元素自身的 `box-shadow`）。

### ✅ Verification
* `npm run build` 通过（`dist/index.html` 818.91 kB / gzip 226.41 kB）。
* `npm run lint` 38 项（32 error / 6 warning），与改动前基线**完全一致**。
* 深/浅两套主题下实拍复核：同步按钮行、账号卡（同步 vs 修改并排）、进度条正常态与临界态。
* **未实机（PS5）验收，未出包。**


## v0.5.2.104 (CN-FORK)

> 账号板块按钮按状态分色 + 动作主色由蓝换朱橙 `#F43E06`。用户口径原文：「登入 和激活 是绿档，登出 是红档，反激活是灰档」「这个按钮（同步）也加颜色」「暂无 PIN…这句废话不要」「这个框缩短，将修改按钮和上边同步按钮对齐」「整体风格没问题，考虑把主色换掉，换成更舒服的颜色」。**本轮只改前端与文档，未出包。**

### 🎨 Design & UI Improvements
* **动作主色由蓝 `#2563EB` 换朱橙 `#F43E06`**（用户给了具体色值）：实心/选中底 `#F43E06`、深档「字与图标」档 `#FF7043`、浅档字与图标档 `#C2330A`；悬停深档 `#FF5A1F` / 浅档 `#D93A00`；薄染 `rgba(244,62,6,.16/.09)`、线 `rgba(244,62,6,.45/.32)`。触点：`tailwind.config.js`（`vividOrange` 整阶重写 + `ps-blue` / `ps-blue-glow` 字面量）、`index.css`（LEGACY `.text-ps-blue` 兜底字面量 + `:root` 六枚令牌）、`light-theme.css`（变量档 + 五组字面量）、`AutoloadOverlay.jsx` 的阴影 rgba。
* **账号板块按钮按状态分色**：新增 `BTN_GRANT`（`.btn-ok`）与 `BTN_REVOKE`（`.btn-danger`）两个常量 —— 登入/激活 → 绿档，登出 → 红档，**反激活 → 中性档**（可随时重做的开关，份量低于登出，不跟它抢红）。
* **「同步」按钮转金档**：`BTN_INFO` → `BTN_SYNC = 'btn btn-warn'`。语义依据：同步的实质是把换算后的 ID 写回注册表，归「写入」档。
* **「账号显示名」输入框缩短并与「同步」对齐**：原来这一行 `flex-1` 铺满整宽、修改按钮吊在卡片右边缘；现套用与 ID 三格同一张 `grid-cols-1 sm:grid-cols-3`，字段落第 1 列、按钮贴本列右缘，与上方「同步」自然对齐（窄屏仍是整宽单列）。
* **游戏磁贴「已安装」徽标由旧主色蓝改绿 `#3fb950`**：它原是 `#58a6ff`（旧主色的字/图标档），主色一换即成无主的遗留蓝；改绿与「已安装 = 成功态」同源。**平台徽标的 PS5 蓝保留**（PlayStation 平台身份色，不是动作主色）。
* **删掉「暂无 PIN，点击下方按钮生成」这句提示**：没有 PIN 时该位置直接渲染 `null`；词条 `acctools.pin_none` 保留在语言包里（与 `.100` 删说明文字同一处理惯例），只是不再渲染。

### ⚠️ Known Notes
* **对比度**：白字压 `#F43E06` 实测 **3.79:1**，低于 WCAG AA 正文线 4.5:1，高于大字号/图形元素的 3:1。一行改法：① 实心底压深到 `#D93A00`（白字 4.61:1）；② 实心档改配深色字 `#1A0A02`（5.09:1）。已把三方案实拍交用户选，**待定**。
* **暖色撞色**：主色朱橙（色相 15°）与危险红 `#F87171`（0°）、警示金 `#D29922`（40°）同处暖色区；实拍下仍可分，若反馈「删除/终止的红不够跳」，把 `--btn-danger-fg` 推深到 `#E5484D` 即可。

### ✅ Verification
* `npm run build` 通过（`dist/index.html` 817.94 kB / gzip 226.09 kB）。
* `npm run lint` 38 项（32 error / 6 warning），与改动前基线**完全一致**（均为既有问题，本轮未新增）。
* 深/浅两套主题下复核管理 Payload / 常用配置（账号板块）/ 游戏管理。
* **未实机（PS5）验收，未出包。**


## v0.5.2.103 (CN-FORK)

> UI 重做：**收敛上色 + 深空黑 + 舒展留白**。用户口径「现代化 简约 高大上」，三个方向逐条确认：按钮配色＝收敛（只留主操作上色）、视觉基调＝深空黑 + 极细线 + 大留白、布局密度＝舒展。**本轮只改前端，未出包。**

### 🎨 Design & UI Improvements
* **按钮不再满屏彩色：全站只有「这一页的主操作」是实心彩块**。作废 `.102` 的「功能六色实心」——一屏六七种高饱和实心块（绿色播放 + 红色删除 + 金色写入 + 青色刷新 + 紫色登出…）与「简约」直接冲突。现在的角色表：`.btn-primary` 实心蓝（唯一上色档）· `.btn-outline` 中性淡底+发丝线（取消/返回/启动/重启/修改）· `.btn-ghost` 无底（低优先级跳转）· `.btn-info` 信息档（刷新/重试/同步/复制）· `.btn-danger` 淡红底+红线+红字（删除/终止）· `.btn-ok` 淡绿（已启用/安装/更新）· `.btn-warn` 淡金（上传/写入/覆盖）。语义一个没丢，噪声降一个数量级。`--fill-*` 七组令牌整组删除，新增 `--btn-*` 中性/语义档令牌，浅色主题各翻一份。
* **图标钮从实心青块改为无底幽灵档**：静息态只有一个中性色图标、悬停才浮出一层底。整页几十颗刷新/返回/折叠/复制钮因此全部安静下来。新增 `.icon-btn.is-danger`：列表行内的删除类图标钮**图标恒为危险色**（原来靠 hover 才显红，而触屏和遥控器根本没有悬停态），已接入存储中心行内删除、自动加载移出序列、来源管理删除来源三处。
* **中性底整体压深为「深空黑」**：画布 `#111317 → #0B0D10`、侧栏 `#1B1F25 → #0E1116`、卡片 `#1B1F25 → #12151A`、发丝线 `#30363D → #1E232A`。层级不再靠色块深浅差，而是靠 1px 发丝线 + 大留白划出来。文字阶一个字节没改，对比度靠更深的底拉出来。浅色档分隔线同步收细（`#D0D7DE → #E4E7EB`）。
* **页头横条去掉实色底**：原来底取 rail 档（比画布亮一档），在近黑画布上形成一条横贯全宽的浅色带。`.102` 起页头已不粘顶、没有内容会压到它，因此不再需要实色底 —— 现在只留底部那根发丝线。
* **舒展档：板块间距与圆角放大**。页面根节点统一是 `space-y-6`，所以只在这一条选择器上把板块间距 `0.9rem → 1.35rem`（≥768px `1.5rem`），一处生效全站。圆角同步：板块 `0.75 → 1rem`、按钮/输入框 `0.5 → 0.625rem`、分段控件 `0.75 → 0.875rem`；板块头与板块体内边距一并放宽；游戏磁贴间距 `0.45 → 0.7rem`。**控件高度令牌（`--ctl-h` 等）一律未动**（那是 `.99` 用户点名降过的档）。
* **游戏磁贴徽标收敛**：原来是「饱和实心底 + 白字」，一屏最多同时出现 8 种饱和色块（平台 2 色 + 格式 7 色 + 来源 2 色）。现在统一成**深色芯片**（压暗底 + 同色字 + 同色 35% 发丝线），色相只由字与线传达 —— GitHub label 的做法，信息密度不减、观感不再像玩具。
* **两处漏网实心块收口**：存储中心仓库列表与已安装行内的「更新」按钮原先是手写的实心 emerald 类（绕过骨架层），改走 `.btn-ok`。确认弹窗的**危险确认仍保留实心红** —— 它是模态闸门后的单次不可逆动作，实心是它应得的份量。
* **浅色主题补「反向豁免」**：浅色档把 `text-white / text-zinc-* / text-ps-blue` 整体翻成深色（权重 0,2,0 + `!important`），会盖掉语义按钮档在 `index.css` 里的 (0,1,1) 定色 —— 红边淡红底上的危险字与图标在浅色下会被洗没。现按 (0,2,1)/(0,3,1) 补回，只在浅色主题生效。

### ✅ Verification
* `npm run build` 通过（`dist/index.html` 817.93 kB / gzip 226.11 kB）。
* `npm run lint` 38 项（32 error / 6 warning），与改动前基线**完全一致**（均为既有问题，本轮未新增）。
* 深/浅两套主题下逐页截图复核：管理 Payload / 自动加载 / 游戏管理 / 活动进程 / 设置。
* **未实机（PS5）验收，未出包。**


## v0.5.2.102 (CN-FORK)

### 🐛 Bug Fixes
* **修掉 `.101` 引入的整进程崩溃**（用户实机：改名弹出 `Failed to fetch` 与「读不到账号列表：Failed to fetch」，随后整个 ELF 挂掉）：根因在 `rename_profile_cache()` 的新文件容量算法 `blen = len + nlen - vlen` —— 它只算了「名字之后那段」，漏了 `firstName` 之前的前缀（头像 URL 等，实测一百多字节）。第三段 memcpy 因此越出 malloc 块尾同样多的字节，砸穿堆元数据把主 ELF 带走；进程一死，前端所有请求一起变成 `Failed to fetch`，看着像网络坏了，其实是内存坏了。现在 `blen = pre + nlen + suf`，三段都算进容量，`-Wall -Wextra` 干净。附带结论：溢出发生在备份登记与写文件**之前**，设备上的 `online.json` 没被写过、无需清理；但①②两层已写成功，注册表里已是新名。

### 🎨 Design & UI Improvements
* **按钮全部转实心、按功能分色**（用户：「图三那种改法不对，不同场景不同功能要不同颜色的实心图标」；选定样张 A + I2）：作废 `.101` 的描边紫档（删 `--color-secondary*` 三枚令牌及 light-theme 对应行），新增 `--fill-{blue,purple,violet,teal,green,gold,red}` 各带 `-hover` 与 `-icon` 一档 + `--fill-fg`，深浅两主题同值（实心底自己保证对比，不再需要翻档）。功能映射：蓝＝本页唯一主操作（与 `.sel-chip` 同值，选中档不动）· 紫＝启动/重启/修改/取消（`.btn-outline`）· 深紫＝幽灵（`.btn-ghost`）· 红＝不可逆（`.btn-danger`）· 新增 `.btn-ok` 绿＝已启用/安装/更新、`.btn-info` 青＝刷新/重试/同步/复制、`.btn-warn` 金＝上传/写入/覆盖。六档底色配白字实测 ≥4.2:1，一律纯色无渐变，组件类名一行未动。
* **I2 图形档**：实心按钮里的图标取同色相提亮一档（`.btn-* > svg { color: var(--fill-*-icon) }`），白字配浅色图形一眼分得开。这几条必须带 `!important`：文件顶部 LEGACY 把 `.text-ps-blue` / `.text-white` 写成 `!important` 字面量给老内核兜底，而组件里的图标普遍挂着这两个类。
* **调用点按功能归位**：温控「已启用」从蓝选中芯片换成绿实心（`.sel-chip` 表达的是「当前选中」而不是「状态」，用错了档）；刷新/重试/同步（温控、PKG 卡片、PKG 内嵌帧、存储中心「重试连接」、进程列表 Retry、账号「同步」）→ 青；PKG「上传更新」与 USB 页 Copy/Overwrite & Copy、存储中心 Move to Internal → 金；进程列表 Retry 原先错挂红档（红＝不可逆），一并纠正。
* **页头不再粘顶**（用户：「所有 head-bar 要跟随页面一起滚动」）：`.head-bar` 去掉 `.94` 加的 `position: sticky / top / z-index` 三行，退回普通块；出血负边距、rail 实色底与那根发丝分界线都保留。


## v0.5.2.101 (CN-FORK)

### 🎨 Design & UI Improvements
* **描边/幽灵/图标/框内复制四类按钮整套转次要紫**（用户：「所有的 btn 都要有颜色，但别用蓝色」）：新增一档令牌 `--color-secondary` / `-tint` / `-line`（深档 `#a371f7`，浅档 `#8250df`——浅底上必须压深一档才够对比），`.btn-outline`、`.btn-ghost`、`.icon-btn`、`.field-btn` 与两处 `.is-copied` 全部改读它。蓝只留给「这一页唯一的主操作（`.btn-primary`）+ 当前选中（`.sel-chip`）」；紫是冷色同族、与蓝一眼分得开，又不占用绿=成功、金=注意、红=危险这三档已有名有姓的语义色。仍然纯色无渐变，组件类名一行未动。
* **游戏磁贴相对 `.100` 再放大四分之一**：`.tile-grid` 的列宽下限 `--tile-w` PC/手机 10.5rem(168px) → 13.125rem(210px)，电视端 15rem(240px) → 18.75rem(300px)。1920 正文列实测回到 4 列 ≈362px，仍是自适应（宽屏多放、窄屏少放），只是每块变大。
* **PKG Manager 开关卡片从设置页搬到「常用配置」页最下面**（用户指令）：`PkgManagerCard` 的挂载点由 `SettingsView` 移到 `CommonEntriesView` 的温控板块之后，成为本页第四个板块；卡片自身无改动，搬过去后自然吃到新的紫档按钮。设置页顺带去掉随之不再引用的 `Package` 图标导入。

### 🐛 Bug Fixes & Feature Changes
* **「修改用户名称」深挖到第三、四层，并把结果做成可读证据**（用户：`.100` 之后主界面用户按钮与切换用户界面仍是旧名）：`.100` 补的 UserService 那层实测回读是 "both verified"，说明那两处 UI 并不拿它当显示源，判据取错了。现在改名按四层尽力而为，每层的结论原样拼进返回消息（Toast 直接看得到）：① SysReg 槽（硬条件，失败即整笔否决）② `sceUserServiceSetUserName` ③ `/system_data/priv/cache/profile/0x<UID>/online.json` 的 `firstName`（换头像写回的就是这个目录且能生效，壳层确实读它）④ `/user/home/<目录>/username.dat`（activity 类工具给用户打标签读的是它）。③ 只认「全文唯一、无转义的普通字符串值」，④ 只在内容去掉行尾 `\r\n\0` 后**正好等于旧名**时才改写——格式对不上就原样不动、只报一句话，不做没有把握的覆盖；两处都写前登记备份（且只在未登记过时登记，避免把新名当原值）、写后回读比对。
* **`/system_data/priv/home/user_list.dat` 只当只读探针**：实测 72 字节、A/B 双份，结构没有文档，写它等于拿全体账号名单去赌。因此一个字节都不动，只在消息里报「旧名/新名的字节在不在里面、住在第几偏移」，供实机判死显示源。`npsignin_result_t.message` 320 → 768，`accounts.c` 的 `emsg` 384 → 896、响应缓冲 `cap` 2048 → 4096（否则会截成半句话甚至非法 JSON）。


## v0.5.2.100 (CN-FORK)

### 🎨 Design & UI Improvements
* **两套主题整套换成参考页（`ps5-remoteplay-pin.html`）的「灰墨 + 蓝主色」**（用户选定「整套照搬（含蓝主色）」，因此解除旧硬约束「动作主色不得为灰或蓝」；只换令牌值，组件类名一行未动）：
  * 深色档：画布 `#04060b` → `#111317`，侧栏/卡片 `#05080f·#0a101d` → `#1b1f25`，内嵌件由「几乎看不见的蓝染」改成参考页的凹底 `rgba(8,10,14,.3)`（压卡片 ≈`#151920`），发丝线 `#1b2438` → `#30363d`；文字阶 `#e6edf3 / #a1a9b3 / #8b949e / 占位 #6e7681`。
  * 动作色橙 → 蓝：`#ff9410` → `#2563eb`，选中芯片与实心主按钮同为 `#2563eb` + 白字（原深墨字 `#1b0e02` 取消）；深档「图标/文字」档取亮蓝 `#58a6ff`（实心蓝压深底做文字只有约 3:1）。状态色跟参考页：绿 `#3fb950` / 金 `#d29922` / 红 `#f87171`。仍然纯色无渐变。
  * 浅色档按同族定制（GitHub light 一族）：画布/卡片纯白、侧栏 `#f6f8fa`、凹底 `rgba(9,30,66,.045)` ≈`#f4f5f6`、线 `#d0d7de`；主文字 `#1f2328`、次要 `#454f59`；实心底与深档同值 `#2563eb` + 白字，文字/图标档另取浅底可读的 `#0969da`；状态文字 `#1a7f37 / #9a6700 / #cf222e`。
  * 结构上重新分档：`.panel-card`（= `ui/Section` 的唯一板块形状）回到 card 档 `#1b1f25`（浮起），行/输入框/徽章留在 inset 档（凹下），与参考页「画布→卡片→内嵌件」三层同构；`.90` 那次「板块与行并为同一档」的取舍在新色板下会失去层次，故按参考页改回（注释里留了一行回退位置）。
* **游戏管理磁贴缩小约三分之一并改成自适应**：`grid grid-cols-2 sm:3 lg:4` 换成骨架层 `.tile-grid` = `repeat(auto-fill, minmax(var(--tile-w), 1fr))`，列宽下限 PC/手机 168px、电视 240px（1920 正文列实测 6 列 ≈245px，原固定 4 列是 ≈355px）。宽屏自然多放、窄屏自然少放，不再钉死四列；用 grid 而非 flex（老内核不支持 flex 的 gap）。

### 🐛 Bug Fixes & Feature Changes
* **「修改用户名称」补上第二层**（用户：简介里改了，切换用户的名单没改、重启也没用）：`npsignin_rename()` 原本只写 SysReg 槽的 `user_name`（17 字节）并回读，壳层「切换用户」读的是 UserService 那一份。现在写完成本表后再调 `sceUserServiceSetUserName`（零填充定长缓冲 + `max_size=17`），并用 Get 回读比对；Set 被拒或回读不一致时不改判定结果，只在消息里如实标明哪一层没跟上。SDK 无原型，按项目既有惯例自声明 extern（`libSceUserService.so` 两个导出符号都在）。
* **账号操作前的确认弹窗全部去掉**（用户：「登入登出 激活等操作的大提示全不要」）：登出原本要过一站内置确认弹窗，现在与 `.99` 的「登入」一样点击即发，结果仍用 Toast 报；`AccountToolsSection` 不再接 `showConfirm`。防误写靠的是服务端那道（`confirm=online` + 写前逐键抄原值 + 登出的备份层），不在弹窗里。
* **账号板块文案与可辨识度**：板块顶部那段说明（`acctools.desc`）与「本工具欠它 N 条原值记录」那句不再渲染；新增骨架层 `.field-edit`，两个可编辑框（十六进制 ID / 账号显示名）字色取 `--color-ps-blue`，与只读换算格（`.field-ro` 虚线边 + 辅助字色）明显分开。


## v0.5.2.99 (CN-FORK)

### 🚀 Features & UX Improvements
* **PS5 Account Tools 整功能并入主 ELF，做成「常用配置」页的原生板块**（源工程 `D:\offact-new` 前后端已实测，本批只做移植，不自创逻辑）：
  * 后端新增 `src/psaccounts/`（accounts / npsignin / offact / avatar / safety / sys_lite / api + cJSON、stb 第三方层 + `np_templates/`）。账号离线激活、假登录（auth.dat / config.dat）、改名、头像四档（64/128/260/440）、注册表逐键备份与还原、远程游玩配对 PIN 全部在主 payload 进程内跑；`Makefile` 补 SRCS 与 `-lkernel_sys -lSceRemoteplay`。
  * 接口挂到自己的 8911：`src/http_server.c` 在最前面加 `psaccounts_wants(url)` 认领块把 `/api/*` 转给 `psaccounts_handle()`。两处集成隐患单独处理 —— ① MHD 是 THREAD_PER_CONNECTION 而移植模块是全局无锁（备份账本、登录计划、libSceRemoteplay 单会话），全部 `/api/*` 串行进 `acct_api_mu`；② `con_cls` 所有权用 magic-tag 结构 + 四处 `MHD_start_daemon` 都注册 `MHD_OPTION_NOTIFY_COMPLETED`，收尾统一走 `http_on_request_completed()`（`(void *)1` 哨兵那一档跳过）。1 秒轮询的 `/api/remoteplay/pin/status` 进日志静音名单。
  * 前端 `views/AccountToolsSection.jsx` 由「第二个进程 + iframe 黑盒」改写为原生 Section 板块（同源 `/api/…`）：前台账号卡、三种格式 ID 与换算、登入/登出/激活/反激活/改名、头像上传裁剪、PIN 卡与倒计时。`vite.config.js` 补 `/api` 代理，`mock-server.js` 按真接口形状实现整套 `/api/*`（状态在内存里真翻转），locales-overlay 补 81 条中英词条。
* **深色主题动作橙转档**（用户指「主页整块橙色按钮发棕」）：`#ff7a2e` → `#ff9410`，色相 21.8°→33.2° 离开砖红/赭橙段、蓝通道 46→16 去掉压在深底上的灰味，仍是纯色满饱和，无渐变。一处令牌改到位：`--color-action` / `-hover` / `-tint` / `-line` / `--color-sel-bg` / `--color-ps-blue` / `-glow`，并同步老内核兜底字面量 `.text-ps-blue`、`tailwind.config.js` 的 `ps-blue` 与 `vividOrange[300~500]`、`AutoloadOverlay` 的阴影 rgba。「4+1 档中性底 + 1 档选中底」契约与浅色档（`#f25c05`，白底要压深才够对比）均未动。橙块 vs 卡片底 8.6:1、块上文字 8.7:1，只升不降。
* **账号板块首轮实测的 9 处修订**：
  * 点「登入」不再走网页侧「只读预览 + 手打 SIGNIN」那道闸，直接执行；服务端 `confirm=online` 这道独立的门保留，写前的逐键抄原值也照旧，去掉的只是弹窗那一步。
  * PIN 有效期 300 秒 → 180 秒（`accounts.c` 的 `PIN_TTL_SEC`，看门狗与接口按它走；mock、界面兜底档、`pin_hint` 文案同步）。
  * 「重命名」按钮文案改「修改」；★ 自动登录芯片从灰档提为暖黄鲜档（`bg-amber-500/20 text-amber-400`，走收口层的 warn 档，不新增第 6 种底）。
  * PIN 卡里那行账号说明原用 `break-all`，会在「账号 ID：」中间被截断换行 → 改 `break-words`，只在分隔符处折行。
  * 复制钮原本吊在卡片右上角、离 PIN 隔一整行 → 挪到数字紧跟处，复制的是 8 位原始码（不含显示用的空格）。
  * 三格 ID 的复制钮全部嵌进框内右端（新增骨架层 `.field-btn` / `.field-btn-pad`），点击后钮本身变动作色 + 图标换成勾，1.4 秒复位。
  * 可编辑与只读分开：换算出来的 Base64 / 十进制两格走新增的 `.field-ro`（虚线边 + 辅助字色），可编辑两格保持实线，并各补一行「· 可编辑」标签。
  * 头像框底部常驻一条相机角标（`.96` 之前只有 `title`，实机浏览器与遥控器都不弹 title，等于没人知道这格能点）。
  * 「同步」按源工程语义核到实处：已激活且与主机不一致时确实 `activate?accountId=0x..` 写回注册表；并补上源工程 `updatePinLine` 的等价行为 —— PIN 卡显示的 Chiaki ID 跟着框内换算结果走，不再各说各话。
* **页面容器全站统一（骨架重构收尾，配色契约仍不动）**：
  * 根节点一律 `<div className="space-y-6">`，去掉各页自己写的 `max-w-5xl/4xl/3xl mx-auto` 与页内 `pb-12/pb-20`；正文吃满 `main` 的内容列，页头恢复默认出血（`PageHead` 的 `bleed` 开关删掉），粘顶分界线才铺到边。涉及设置、常用配置、游戏管理、来源管理、从 USB 导入五页。
  * 「常用入口」页大标题改回与侧栏同词的「常用配置」，长 `desc` 说明去掉；纯文字 `desc` 在窄屏限两行（新增 `.page-desc--clamp`），避免 450px 宽下排到八行把页头撑成屏幕三分之一。自启页的页头从「序列」列首上移到页面级，保存状态改成只在保存/刚保存完时渲染（原来常驻 `h-6` 占位让页头比别的页高一截）。
  * 浮层底必须实色（`.88` 定的约束）：手机底部导航栏 `bg-black/80` → `bg-ps-surface`（浅色主题下它此前是块压不死的黑）、日志页粘顶条、常用入口「正在启动」遮罩同理；`.96` 丢掉的那半句 `border-none` 补回，七个格子不再各画一圈亮边框、格子间分隔线去掉。命中区尺度里的 `5rem` 写死值改读 `--bar-h`。
  * 自启序列的序号从「绝对定位压在行左上角」改成正常流前导列：旧底 `bg-dark` 在项目里没有定义（Tailwind 生成不出规则 → 透明），数字悬空压在 payload 名字上，450px 宽下实测重叠 16×14px。


## v0.5.2.98 (CN-FORK)

### 🐛 Bug Fixes
* **修骨架重构（.92–.97）留下的四处「一点就整站死」**，配色契约一行未动：
  * `PkgManagerCard.jsx` 用了 `cn()` 却没有 import → 一进「设置」页抛 `ReferenceError: cn is not defined`，React 19 卸载整棵根节点，**之后每个页面都点不动**，看起来像整个应用死了。
  * `CommonEntriesView.jsx` 的取消按钮残留 `setMsg(null)`（`.94` 把内联状态行换成 Toast 时删掉的 state）→「常用配置」新增条目再点取消同样整树卸载。改成只 `setAdding(false)`。
  * `utils/spatialNav.js` 的 Escape 分支引用只在 Enter 分支里声明的 `active` → 遥控器「返回」一按就报错。已提到 `onKeyDown` 顶部。
  * `utils/spatialNav.js` 方向键主干：`pickNext()` 读的是候选对象上的 `from.cx/from.cy`，传进去的却是裸 DOM 元素 → 坐标 undefined、代价函数全程 NaN，**电视端方向键从第二次按键起一步不动**（`.92` 上线即失效）。现改为传「候选集合里查到的那一项」；顺带修 `body` 也过得了 `visibleRect` 导致进不去「首次落焦」分支的问题，并把焦点环 `.is-focus` 改由 `focusTarget()` 直接挂（老内核不保证派发 `focusin`，环不出现等于遥控器看不见自己在哪）。
* **数据与文案两处**：`mock-server.js` 的 `/processes_list` 补 `memory` / `is_daemon`（后端每条都带这两个字段；mock 漏了会让 `p.memory.toFixed(1)` 抛 TypeError，勾「显示所有系统进程」即崩）。上游 `zh-CN.json` 的 `manage_sources.remove_title` 值错成「显示所有系统进程」，删除来源的确认框标题跟着错——按覆盖规范只在 `locales-overlay/zh-CN.json` 加「删除来源」，不动 locales 子模块。
* `ui/PageHead.jsx` 的 `desc` 容器从 `<p>` 改 `<div>`：自启页往里放了「保存中」指示块，`<p>` 嵌 `<div>` 是非法 HTML，内核会自己拆标签导致排版错位。
* **预览口子**：`utils/helpers.js` 的 `isPS5` 增加 `?device=ps5` 判定，电脑/手机可直接预览电视端形态（大命中区 + 方向键导航 + 隐藏底部栏）；真机 UA 本来就命中，不看这个参数。
* **验收方式改了**：`.92`–`.97` 六批只跑过 `vite build`，而 esbuild 不做作用域检查，上面这几处编译期全绿、运行期才炸。这批起以 `npm run lint` 的 `no-undef` 为守门项 + 逐页逐控件实测（`mock-server.js` 里 Node `process` 的报错属 ESLint 浏览器 globals 配置的既有误报，未动）。


## v0.5.2.97 (CN-FORK)

### 🚀 Features & UX Improvements
* **骨架重构第 6 批：互斥分段控件收成一个 `<Segmented>`**。同一角色「N 档里选 1 档，选中的就是当前值」此前有四份手写实现：外观三档（自带轨道 + 实心橙 + 白字 + 一圈蓝色光晕）、管理页排序三档（三枚裸 `.btn` + `space-x-2`）、温控档位三档（`.btn` + `.sel-chip`）、自启延时三档（`.btn` + `ml-2`）。轨道有无、圆角、间距、选中表达全不一样。现在一处定义：
  * 新增 `ui/Segmented.jsx`：`role="radiogroup"` + 每档 `role="radio" aria-checked`（读屏会念「共 3 项，当前第 2 项」，遥控器方向键也照旧能走）；轨道一档 `inset` 底 + 一根发丝线，选中档仍是契约里唯一的实心选中 `.sel-chip`，**没有引入第 6 种底**。
  * 轨道用 `grid` + `grid-auto-columns: 1fr` + `gap`，不用 flex `gap`（`.95` 刚清完的老内核坑，grid 的 gap 老内核支持）。
  * 接入四处：`theme/ThemeToggle.jsx`（侧栏收窄态只留图标，整名转 `title` + 读屏）、`StorageHub` 云端排序、`ThermalSection` 风扇档位（整组未启用时改由 `<fieldset disabled>` 一起禁用，读屏能听出「这一组现在不可选」，此前只有三枚按钮各自灰着）、`SettingsView` 自启延时。外观档上那圈 `shadow-[0_0_12px_rgba(0,112,209,0.3)]` 蓝色光晕一并去掉（早于改色时代的遗留，与其他分段控件不一致）。
  * 尺度仍然只在令牌层：每档 `min-height: var(--ctl-h)`（PC 44 / 电视 68px）。

## v0.5.2.96 (CN-FORK)

### 🚀 Features & UX Improvements
* **骨架重构第 5 批：电视端补齐核心操作 + 死代码清账 + 导航收敛为一份菜单表**。配色契约仍然一行未动：
  * **来源管理页在 PS5 上不再只有二维码**：原实现是 `if (isPS5)` 直接 return 一张「去电脑上管」的 QR 页，电视端连看都看不到自己配了哪些源，更不能排序/删除。`.92` 有了方向键导航、`.94` 有了 68px 命中区之后，这个妥协没有理由继续存在 —— 现在三端共用同一份 CRUD（上移 / 下移 / 移除都能用遥控器点到），QR 降级为页尾一个次级板块「也可以在电脑或手机上管理」（新词条 `manage_sources.pc_hint_title`）。顺带删掉了 `manage_sources.ps5_title_1/2` 这条只在电视分支用到的标题分叉。
  * **导航一处定义、两处渲染**：新增 `NAV_ITEMS` / `NAV_GROUPS`（`App.jsx` 模块作用域），侧栏按分组渲染、手机底部栏平铺渲染。此前「有哪些页面」在代码里有两份真相（v0.5.1.64 删「主页」时是两处分别改的），加一页要同步两处；现在只改一处。侧栏折叠钮也归进 `.icon-btn`（原来固定 `p-3`，是整条侧栏里最小的一颗，电视端只有 48px）。
  * **手机底部导航从「横向滚动」改「等宽格子」**：七个入口挤在一条 `overflow-x-auto` + `min-w-max` 的栏里，一屏只露得出五六个，剩下的藏在拇指要横向滑才够到的地方；现按 `NAV_ITEMS.length` 均分列宽（`grid` + `gridTemplateColumns`），每项撑满整条栏的高度（原来只有图标那一小圈可点），并给当前项 `aria-current="page"`。窄屏仍显示短标签，完整页面名转成 `sr-only` 留给读屏。
  * **死代码清账**：删 `ui/PayloadButton.jsx`（仪表盘收藏时代遗留，`v0.5.1.64` 之后无人引用）；`App.jsx` 移除 `favoritePayloads` + `toggleFavorite` / `moveFavorite` + 写 localStorage 的副作用、`isFavoriteEditMode`、`launchHistory` / `refreshHistory`（每次进页和每次启动都白发一次 `/history_list`，拉回来没人消费），以及只出现在 import 行的 `Star` / `CloudDownload` / `isSystemPayload` 引用。
  * 未动：色值与「4 档底 + 1 档选中」契约、后端接口与 `/sources_*` 语义（默认云端仓库的开关仍归 `DEFAULT_REPO_ENABLED`，在设置页改）。

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
## v0.6.0

### 🚀 Features & UX Improvements
* **Repository Browser Overhaul**: Rebuilt **Get Payloads** into a full store experience with a card grid, search, source filtering, and live install status badges.
* **Payload Details**: Added a dedicated details page showing payload README documentation and version info.
* **Cross-Browser Favorites**: Dashboard favorites are now stored on the console, keeping your favorites and custom order synced across all browsers and devices.
* **Controller & History Navigation**: Pressing Back / Circle on your PS5 controller or browser now closes modals, details, and edit modes instead of exiting the browser. Added URL links for direct navigation.
* **UI Polish & Quality of Life**: Refreshed Autoload screen styling with improved auto-saving, redesigned payload launch overlay, support for custom repository payload icons and source badges, and cleaner notification toasts.
* **New Language**:
  * **Dutch** (thanks to JensJ56)
* **Translations**: Updated localization files across all 17 supported languages.

### 🛠️ Backend & Bug Fixes
* **Autoload Network Resilience**: Fixed repository network timeouts on boot delaying the autoload sequence ([#109](https://github.com/itsPLK/ps5-payload-manager/pull/109), thanks to [kianborg](https://github.com/kianborg)).
* **Fresh PS5 Install Fix**: Fixed installer failing on brand new PS5 systems ([#108](https://github.com/itsPLK/ps5-payload-manager/issues/108)).
* **Security & Verification**: Added checksum verification for repository downloads and restricted actions to POST requests with origin validation.
* **Stability Improvements**: Resolved crashes with large repository catalogs, improved memory handling, fixed connection cleanup, and other minor stability improvements and bug fixes.

**Full Changelog**: [v0.5.2...v0.6.0](https://github.com/itsPLK/ps5-payload-manager/compare/v0.5.2...v0.6.0)

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
