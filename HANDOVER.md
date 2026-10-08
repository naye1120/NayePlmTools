# HANDOVER — PS5 Payload Manager (CN-FORK)

> 单一事实来源。功能已定稿，本文档面向"新接手的资深工程师"，只保留继续维护所需的骨架与关键约定。
> 详细历史背景见 `FORK-CN.md` / `DEVELOPMENT.md` / `CUSTOM_REPOSITORIES.md`。

## 0. 当前定稿状态
- 分支：`cn-fork`（upstream = `https://github.com/itsPLK/ps5-payload-manager`，已合并上游 v0.5.2，merge 提交 `97bb4dc`）。
> - 版本：`MENU_VERSION = 0.5.2.153`（`include/pldmgr.h`）。fork 规则 = 上游基线 + 构建序号，每次调整构建前 +1。
- 工作树：`.88`–`.91` 是 `.87` 契约内的观感微调，`.92`–`.97` 是**骨架/交互重构六批**（配色令牌一行未动），`.98` 是这批重构的**运行期崩溃清账**（四处整站死 + mock 缺字段 + 一处上游错词条），`.99` 是**账号工具（PS5 Account Tools）源码并入主 ELF + 动作橙转档 + 账号板块 9 处实测修订**（正式出包版 `NayePlmTools_0.5.2.99-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `aca3852`），`.100` 是**整套换参考页配色（灰墨 + 蓝主色）+ 改名补第二层 + 去操作前确认弹窗 + 磁贴自适应**（正式出包版 `NayePlmTools_0.5.2.100-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `f831e98`，两包各 9,858,824 字节、titleId 自检 NAYE00001/NAYE00004 通过），`.101` 是**磁贴再放大 1/4 + PKG 卡搬到常用配置底部 + 次要按钮档转紫 + 改名逐层取证**（正式出包版 `NayePlmTools_0.5.2.101-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `2dc3088`，两包各 9,858,824 字节、titleId 自检 NAYE00001/NAYE00004 通过）。`.100` 的包、`.99` 的包与更早的现行包 `NayePlmTools_0.5.2.87-me.elf` / `-ga.elf`（哈希 `87e6ad3`）都已过期。`.85`/`.86` 是中间态，从未单独出包。`.102` 是**修掉 `.101` 改名取证代码的堆溢出（整 payload 崩）+ 按钮按功能转实心六色（样张 A + I2）+ 页头去粘顶**（正式出包版 `NayePlmTools_0.5.2.102-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `be7fa8f`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过），`.101` 的包已过期。`.103`–`.105` 是**纯前端/文档改动、从未单独出包**（`.103` 收敛上色 + 深空黑 + 舒展留白；`.104` 账号板块按钮状态分色 + 主色换朱橙 `#F43E06`；`.105` 同步按钮补图标 + PIN 进度条重做），`.106` 把这四轮**一次性出包**（正式出包版 `NayePlmTools_0.5.2.106-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `0e9afb2`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过），`.102` 的包已过期。`.107` 是**头像上传/裁剪四项缺陷修复**（变形 / 卡死 / 拖拽裁剪 / 拖放选图）+ **账号「登出」不再连带反激活**（备份层加来源维度，按钮 `[登出]` = 只登出、`[反激活]` = 登出并反激活，文字不变）—— 详见 §2.1.1 与 §2.2.1。正式出包版 `NayePlmTools_0.5.2.107-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `8065f19`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过，`.106` 的包已过期。`.108` 是**账号按钮语义修正** —— 用户否掉了 `.107` 的文案（「逻辑做错了，登出 就是只登出，反激活 即 登出并反激活，**但是按钮文字不要改**」）：按钮行恒为 `[登出]` + `[反激活]`，已登录时 `[反激活]` 走 `signout-full`（登出 + 反激活一步到位），未登录时走 `deactivate`；后端契约沿用 `.107`，本轮只动前端/语言包/文档。正式出包版 `NayePlmTools_0.5.2.108-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `12e2ba5`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过，`.107` 的包已过期。`.109` 是**修掉「头像上传一直显示上传中、不报错也不结束」**（真根因在 MHD 流式状态回收的判据，见 §2.2.2；前端同时把「超时」与「用户取消」区分开）+ **通知条挪到底部居中并缩小**（见 §2.2.3）+ mock 里头像改成真闭环。正式出包版 `NayePlmTools_0.5.2.109-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `b8ea772`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过，`.108` 的包已过期。`.110` 是**修掉电视端「确定」生效在鼠标指针位置、而不是彩色光圈所在的那个控件上**（见 §2.2.4；结论：网页无法移动系统鼠标指针，改为让 OK 的落点脱离指针）+ 新增 `?navdebug=1` 排障面板。正式出包版 `NayePlmTools_0.5.2.110-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `dcfb48b`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过，`.109` 的包已过期。`.111` 是**四件事一起做**：① 遥控器模式下藏掉系统光标（见 §2.2.4 延伸 —— 网页无法移动指针，只能让它消失）；② **改名不再影响账号 ID**（ID 改由 uid 派生，见 §2.1.1）；③ **登入/登出/激活/改名/接口错误全部中文化**，不再报英文诊断串；④ **去掉「反激活」** —— 激活成功后那颗按钮整颗收起（见 §2.2.5）；⑤ 修掉**「第一次登出失败、第二次才成功」**（`fill_status()` 改为 uid 优先解槽位，见 §2.1.1）。正式出包版 `NayePlmTools_0.5.2.111-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `e908ef5`，两包各 9,875,208 字节、titleId 自检 NAYE00001/NAYE00004 通过，`.110` 的包已过期。`.112` 是**游戏管理三个面板改「铺满右侧」+ 磁贴图拆成图标/背景两层 + 电视端焦点那套逻辑整体回退到上游做法**（用户口径两轮：「存档管理页面打开后铺满右边整个区域。多媒体管理也是铺满右边。修改名称也是铺满，但是你只把修改磁贴图标放出来了，还要增加一个修改背景的」/「刚才铺满的几个页面圆角全部取消，修改名称哪里将磁贴图标与游戏名称放在一行，磁贴背景放在下面，磁贴背景预览图是16:9的dds图片。图标是png图片。另外修复一个历史bug。ps5真机上焦点鼠标移动的方式最好改回跟上游一样，现在这样很别扭」）。具体：① `Modal` 新增 `layout="panel"`、四角直角、`--sidebar-w` 令牌 + `html[data-sidebar]`、`.panel-sheet` 的 `margin:0!important`（躲 `.space-y-6` 的 `> * + *`）—— 见 §2.2.6；② 磁贴两层图（**图标 1:1 PNG / 背景 16:9 1920×1080 DXT1 DDS，DDS 由后端转**，`/game_tile_icon` 与 `/game_tile_background` 拆分，`game_list` 新增 `icon` 版本号字段）；③ **电视端「光圈/光标」整层回退**：删掉自己搬焦点、抢确定键、藏光标三层，改成光圈跟着指针走 —— 见 §2.2.7；④ 修掉 `.92` 起就存在的「弹窗里按返回键连带退页」—— 见 §2.2.8；⑤ `Modal` 焦点进出独立 effect —— 见 §2.2.9。**本轮为纯前端/mock/文档改动，未出包**（后端 C 代码一行未动）。`.113` 是**三件事 + 一处版本号清账**（用户口径：「这几个铺满页面的字体太小了，跟其他父级菜单一样大。这里点击也可以上传图片，上传完要有一个应用的动作；这里的缩略图也要调大。太小了，预览都看不清。改把，改完直接打包成elf」）：① 铺满面板里的字号与侧栏菜单拉平 —— 立 `.panel-body` / `.panel-note` / `.panel-tiny` / `.panel-micro` 四条语义档，替掉 60 处硬编码 `text-[13px]` 之类的 px 字号（见 §2.2.6 末尾）；② 改名页两层图改「**点预览也能选图 + 应用才生效**」（预览本身是按钮、选图先暂存并本地预览、点「应用」才提交，另有「取消」）；③ 多媒体缩略图列宽下限由写死的 112px 改成 `--thumb-w` 令牌（移动 140 / 桌面 200 / 电视 320px）；④ 顺手清掉三处**手抄的版本号**（设置页捐赠说明、支持作者页兜底值、mock 的 `/version`）并加回归守卫。正式出包版 `NayePlmTools_0.5.2.113-me.elf` / `-ga.elf`，内嵌标题真实短哈希 `81c42c1`，两包各 9,957,216 字节、titleId 自检 NAYE00001/NAYE00004 通过，`.112` 的包已过期。`.114` 是**磁贴再放大 1/4（`--tile-w` 这根令牌的第三次调档：PC/手机 210px → 262.5px、电视 300px → 375px）+ 改名页「磁贴背景」块从竖排改回左右分栏**（正式出包版 `NayePlmTools_0.5.2.114-me.elf` / `-ga.elf`，短哈希 `91fa4fb`，两包各 9,957,216 字节，`.113` 的包已过期；⚠️ 这一轮**包出了文档漏了**，到 `.115` 才补上 CHANGELOG），`.115` 是**游戏管理 → 多媒体管理（截图/录屏）后端**——新增 `src/game_media.c` + `include/game_media.h`、八条路由（六条 `/game_media*` + `/game_job_status` / `/game_job_cancel`），主机关卡从 3 条增至 4 条（新 `media_scan` 98 项断言），详见 §2.1.0 与 CHANGELOG `.115`。`.116` 是**游戏管理后端一次补完 + 修掉「预览只剩顶部一条 / 打包下载失败」** —— 新增 19 条端点与六个模块（`game_title` / `game_saves` / `game_runtime` / `game_job` / `zip_io` / `inflate`，详见 §2.1.2），并把**文件直出从 `MHD_create_response_from_fd64` 换成自写 32KB 回调 `gr_file_response()`**（fd 直出那条分支在真机上会把 Content-Length 报全、字节发短，导致图片只剩顶部一条、zip 下载判失败；三个调用点一起换），出包版 `NayePlmTools_0.5.2.116-me.elf` / `-ga.elf`（`-Os -Wall` 零警告），`.115` 的包已过期。`.117` 是**修真机报的「游戏管理的列表变空了」** —— 根因是 `.116` 给 `enum_arg_t` 加了 `want`，而 `build_game_json` 仍逐字段赋值**漏了它**，`want` 成栈上垃圾、`gm_append_item` 第一行 `if (a->want)` 把每个条目都早退、`count` 恒 0（症状「先好后坏」，跑过一次 `/game_detail` 才把非空指针留在栈上）；同轮另修**详情页 5 个字段读错列**（`st_cb_row` 拿 `GM_DB_*` 枚举序号当列号，把 `gm_sql_col_index` 的解析结果整个丢掉）与 **`version` 吐栈上垃圾**。同时**把「三道门为什么拦不住」补上**：`src/game_mgr.c` 第一次能在主机上编译（`test/host_mnt_compat.h` 顶掉 `getmntinfo` 三个 BSD 符号 + `GAME_MGR_APPDB_PATH` 加 `#ifndef` + 合成 app.db 夹具），新增 `game_mgr` 主机关卡 **65 项断言**，核心手法是 `seed_stack()` **主动把栈播种成非空指针**（见 §2.1.3）。出包版 `NayePlmTools_0.5.2.117-me.elf` / `-ga.elf`（`-Os -Wall` 零警告），`.116` 的包已过期。**构建产物不入库**：根目录 `NayePlmTools_*.elf`、`include/assets_*.h`、`frontend/dist/` 均为本地产物，只在本地留存；仓库内唯一二进制是 `third_party/pkg-manager.elf`（唯一 `git add -f` 例外）。。`.128` **改错了地方，已回退**（用户：「退回上一版，你改错了，我要的是将游戏详情里面的按钮全部去掉，改为『去 ShadowMount 管理』」）——它删的是**磁贴下**那四颗动作按钮（存档管理 / 多媒体管理 / 名称与背景 / 待定功能），而那四颗是我们自己的功能，必须留。`.129` 把磁贴恢复到 `.127` 的样貌，把手术做到**游戏详情弹窗**里：`GameDetailModal.jsx` 515 → 250 行，改成**纯只读信息面板 + 一颗「去 ShadowMount 管理」按钮**——只读挂载 / 读写挂载 / 卸载挂载 / 卸载 / 删除源 五颗按钮全部下线，连带整条删除任务链（`busy`/`job` state、`watchJob`/`cancelJob` 轮询、删除进度面板、「取消任务」按钮、`PHASE_LABEL`/`BUSY_REASON`、`deleting`/`stopWatch`）一起删干净，不留半死代码（否则 `no-unused-vars` 与维护陷阱）。动机同 `.128` 那次：ShadowMount 挂的镜像/裸目录游戏调 `sceAppInstUtilAppUnInstall` 会删掉 app.db 注册与 `/user/app/<TID>/`（连带 `mount.lnk`）后返回非零，而 `game_runtime_uninstall()` 只在 rc==0 时清缓存，于是图标先还在、刷新后消失且**无从挂回**——外部源 `/mnt/ext1/PPSA21567-宇宙机器人` 完好、SMP 侧仍列着，但我们没有挂载/重注册能力，所以干脆撤掉挂载类入口，交给 ShadowMount 自己管；**这次详情里那颗「卸载」也一起撤了**，不再有「留着以后决定」的欠账。SMP 按钮的行为沿用 `.128` 那套（回归已验证）：点击走 `.84` 的老套路——手势内 `window.open("about:blank", "_blank")` 占位再 `win.location.replace(url)`，**目标 `http://${location.hostname}:10101/`**（用页面自己的 host，主机上 `127.0.0.1`、PC 上 `192.168.x.x` 零配置都对）。**`window.open` 第三个参数千万别加 `noopener,noreferrer`**——加了返回 `null`，兜底分支再开一个真标签页，用户看到两个标签而 `about:blank` 那个还挂着；`catch` 里要 `win.close()` 再兜底。存档 / 多媒体 两条 `GridRow`（`打开存档管理` / `打开多媒体管理`）**保留在详情里**，同时磁贴上四颗按钮也在，所以两条路都能到；「名称与背景」现在**只能从磁贴进**（`.128` 给详情加的那行 `打开名称与背景` 随回退一起没了），「待定功能」那颗空壳按钮也回来了。`GameSection.jsx` 的 `GameDetailModal` 调用点顺势收掉 `addToast` / `showConfirm` / `onChanged` 三个回调（详情不再发操作，它自己的 SMP 按钮不经过我们）；**`showConfirm` 在存档 / 多媒体两个弹窗里仍在用，别误删**。正式出包版 `NayePlmTools_0.5.2.129-me.elf` / `-ga.elf`（提交 `ead334d`，内嵌标题 `ead334d`），两包各 10,785,872 字节、titleId 自检 NAYE00001/NAYE00004 通过，`frontend/dist/index.html`（910,858 字节）与 ELF 内嵌 blob 在偏移 2007072 **逐字节一致**，`0.5.2.128` / `.127` / `.126` 残留各 0 处、`cancelJob` / `watchJob` 各 0 处；`.128` 的包已过期。回归：`game112` 171/171、`nav112` 20/20。**断言口径翻转了**：磁贴上必须**有**四颗（`length === 4` 且磁贴上没有 SMP 按钮），详情里必须**只有一颗** SMP 按钮，且挂载/卸载/删除源/取消任务**一颗都不剩**（用正则扫弹窗内所有 `<button>` 的 aria-label + text——这条反面断言是刻意加的，防止以后把按钮偷偷加回来还静默通过；`.128` 只有正向断言，就是它的教训）。负向对照：把 `GameDetailModal.jsx` checkout 回 `HEAD~1`，6 条断言立刻全红，恢复后全绿。`.129` 出包版 `NayePlmTools_0.5.2.129-me.elf` / `-ga.elf`（提交 `ead334d`），已推真机 `192.168.1.214` 验收。

`.130` 是**真机验收 `.129` 时抓到的一条死链文案**：`GameSection.jsx:321` 渲染的磁贴提示还写着
「点磁贴查看游戏详情（**含删除**）」，而 `.129` 已经把详情里的删除按钮撤掉了。改 `game.tile_hint`
（中英双包）+ 文件头注释，后端与其余 UI 一行未动。
**这里踩到一个 i18next 的坑**：`t(key, default)` 的第二个参数是**内联兜底**，它直接打进 JS bundle，
改 `locales-overlay/*.json` **盖不住**它 —— 第一次只改了 JSON，`dist` 里「详情（含删除）」仍然出现 1 次
（`verify130.py` 抓出来的），补改 JSX 内联兜底才归零。**改了任何 `t(key, '中文默认值')` 的文案，
必须同时改 JSON 和 JSX 两处。** `game.delete_*` 一整簇词条（`delete_title` / `delete_confirm` /
`delete_ok` / `delete_hint` / `delete_running_hint` …）`.129` 起已无任何 JSX 引用，但后端 `/game_delete`
端点还在，所以留着没删 —— **改界面时别拿这批词条当基准**。
出包版 `NayePlmTools_0.5.2.130-me.elf` / `-ga.elf`（提交 `34bf4f8`，内嵌标题 `34bf4f8`），两包各
10,785,872 字节、titleId 自检 NAYE00001/NAYE00004 通过，`dist` 910,984 字节与 ELF 偏移 2007072
处 blob 逐字节一致，`含删除` / `cancelJob` / `watchJob` 各 0 处；`.129` 的包已过期。
已推真机，`device129_detail.mjs` **29/29 PASS**（`WANT_VER=0.5.2.130`），
`game112` 171/171、`nav112` 20/20 复跑通过。

`.131` 是**两个「点完按钮还得自己动手」的缺口**（用户口径：「去 shownmount 管理的做法参考
快速入口的做法，如果没启动则先将 shownmount 启动并，在按钮中等待探活，探活成功后，进行访问，
探活过程给提示。并且多媒体的全量打包下载和下载打包选中，点完按钮后打包完应该自动下载」）：
① 游戏详情「去 ShadowMount 管理」改成**先探后开**，与 CommonEntriesView 的灰点条目同一套；
② 多媒体打包一完成**自动下载**，全量与选中两条路共用一个钩子。**后端只新增一条只读端点**
`GET /smp_status` → `{"running":bool,"port":10101}`（`ROUTE_SMP_STATUS`，`src/http_server.c`
里 `smp_is_running()` 在 `127.0.0.1:10101` 做 TCP 探活、400ms 收发超时，与
`pkgmanager_is_running()` 同一套；面板只读、不拉起也不关闭 SMP —— 拉起走上游 `/loadpayload:`，
关闭属于 SMP 自己的生命周期）。其余后端路由与全部既有 UI 一行未动。
关键取舍：**探活必须留在主机侧**，不能交给浏览器直连 10101 —— SMP 的 Web UI 在很多机器上
只监听 loopback，电脑端连不通不代表主机自己连不通；「面板能不能拉起它」跟浏览器在哪台机器上无关。
`.131` 沿用 `.84` 的标签手法（点击手势同步栈里先 `window.open('about:blank')`，探活通过才
`tab.location.assign(url)`，失败 `tab.close()`；`window.open` **仍不带** `'noopener,noreferrer'`）。
`dist` 914,696 字节（`vite build` 产物；`make frontend-build` 的标题注入后为 **914,750**）；新增
 8 条词条（`game.smp_checking` / `smp_launching` / `smp_waiting` / `smp_waited` /
`smp_start_fail` / `smp_start_timeout` / `smp_not_installed` /
`game.pack_auto_downloading`，中英双包，`zh-CN` 404→412、`en-US` 402→410）。
**`.130` 那个 i18next 内联兜底的坑这轮又照做了一遍**：这几个新词条的兜底文案同时写在 JSX 的
`t(key, '默认值')` 第二参数里（会编译进产物、语言包盖不住），JSON 与 JSX 两处都改了，
并用产物级检索确认新串在 `dist/index.html` 里（`smp_status` ×1、`打包完成，正在下载` ×2 等）。
**出包版** `NayePlmTools_0.5.2.131-me.elf` / `-ga.elf`（提交 `9088433`，内嵌标题短哈希
`9088433`，**不是 DEV**），两包各 10,785,872 字节、titleId 自检 NAYE00001/NAYE00004 通过，
`dist` 914,750 字节与两颗 ELF 内嵌 blob **逐字节一致**（`verify131.py` **29/29**，
`cancelJob` / `watchJob` / `含删除` / `0.5.2.130` 残留各 0 处）。
回归：`game112` **186/186**（新增 1a 节，见 §7）、`nav112` 20/20、`acct111` 23/23、
`avatar109` 19/19、`cropfix` 75/75、`pack125` **15/15**（新增自动下载断言，见 §7）。
已推真机 `192.168.1.214`，`GET /version` → `0.5.2.131`（**`.131` 确实在主机上跑起来了**）。
`device129_detail.mjs` 与 SMP 拉起 / 自动下载的真机实测**未跑完** —— 推包后主机 userland
整体掉线（8911 / 9021 / 2121 全拒连，但 ping 通），需物理接入重启后再验。

`.132` 是**改名不再回退**（用户口径：「这样肯定是不行的，我分析是游戏包里哪个文件控制的，
有没有办法处理」—— `.131` 及更早只改两层缓存，ShadowMount 一重扫就用自己的源重建两层，
于是「改完名、重启又回去了」）。

**先只读侦察、动文件之前拿证据**（`_ftp_probe/recon_*.py`，6 个脚本全部零写入）：
* 名字源**不是** `NPXS.json` —— `/user/app/` 全树递归 `LIST` + 定向 `RETR`，`NPXS.json` 与
  `.sig` 都是 **0 个**。真正的来源是 **`sce_sys/param.json`** 的
  `localizedParameters.<语言>.titleName`（一款 1～10 个语言键，仁王 3 有 10 个）。
* **ShadowMount 完全不缓存名字**：`image_index.bin`（2,441,352 B）只存镜像路径 + TID，
  按「Onimusha / Nioh / 鬼武者 / 仁王 / Way of the Sword」逐个字符串搜索 **5 个全部 0 命中**；
  `config.ini` 只有 8 行；`/user/data/PPSA01650/` 根本不存在。
  → **改源就是终点**，不需要常驻守护、不需要重扫后再补一刀。这条结论把方案从复杂变简单。
* **签名验证不存在**：这些是 AMPR 已解密的包（有 `fakelib/`、96 B `keystone`、
  `eboot.bin.orig`、`*index.crc`），没有校验链，改写 `param.json` 不会让游戏起不来。
* FTP 写权限三处实测通过（`STOR` → `RETR` 字节一致 → `DELE`）。
* SMP 对 folder 游戏是**拷贝者不是绑定者**：源 `sce_sys/` 与 `/user/app/<TID>/sce_sys/` 清单
  与大小逐条一致（PPSA27836 两边各 25 项）。SMP 读**源**建库，`/user/app` 那份是它的**产物**。

**游戏分三类，只有第一类能改源**：folder（`mount.lnk` → 磁盘目录，×10，**可写**）/
image（`mount_img.lnk` → 单个 `.ffpfsc` 28.9～171.5 GB，×4，名字锁在镜像文件系统内部，
**不可行**）/ pkg（无 lnk，系统内置安装，没有可写源）。后两类**如实报告不适用**，不再假成功。

**用定点替换而不是 cJSON 整包重写**：真机 `param.json` 是 CRLF + 2 空格缩进的美化格式（约 2.9 KB），
整包重写会压成单行 LF，几千字节的 diff 面全为了改一个字符串。`titleName` 在 schema 里只出现在
`localizedParameters` 的语言对象下，所以全局替换所有 `"titleName": "..."` 是精确命中 ——
一次改完全部语言，不会遇到「英文改了日文没改」。

新增 `game_mgr_source_paths()`（只读两个 `.lnk`，**毫秒级**；与 `gm_fmt_size()` 共用
`gm_ext_is_image()` / `gm_lnk_root()`，分类口径只有一份实现，不会漂移）+
`tt_json_str_end()` / `tt_json_escape_name()` / `tt_param_rename()` / `tt_write_atomic()` /
`tt_rename_in_param()`（读入 → **先备份原厂件再改内存** → 定点替换 → `.tmp` + `rename()` 原子写）。

`tt_param_rename()` 的**缓冲区契约**值得单独记：**`hits > 0` 才换缓冲区；`hits == 0` 时调用方的
缓冲区原样保留**。没命中时那个拷贝是白费的，而调用方传的可能是**栈数组**，`free` 它会直接崩 ——
这条是 `test/param_rename_test.c` 的残缺输入用例钉出来的（真崩溃：`free(): invalid pointer`），
生产代码已修。

`/game_rename` 增加 `persisted` + `persist_note`。**`persisted=false` 不是错误** —— 面板层与
主机索引层已经生效，改名本身是成功的，只是重启 SMP 会回退。把它当错误会让用户看到红字、
以为白改一次名。前端用**常驻提示**（`Note` + `Check` / `AlertTriangle`）而不是 toast：
toast 几秒就没了，而「会不会又变回去」是下次重扫才撞上的问题，只在成功那一刻弹一下等于没说。
`persist` 状态在弹窗打开时重置，不跨款残留。

新增 `test/param_rename_test.c`（**43 项断言**，主机侧）：夹具是真机 FTP 拉下来的真实
`param.json`（238 B～3.2 KB，1 / 3 / 10 个语言键），用 `#include` 把被测源文件拉进同一翻译单元
以触及 `static` 函数。断言：替换处数与独立统计一致；**除 `titleName` 值之外逐字节一致**；
转义后新名字出现次数 == 语言数；改写结果经**真 cJSON 解析器**校验仍是合法 JSON 且每个语言键
的 `titleName` 读回来都等于新名字；CRLF 行数不变；无 `titleName` 返回 0 且一字节未改；
残缺输入不越界。顺带钉住一个 schema 事实：`localizedParameters` 里除了 `<lang>: {...}` 还有
`defaultLanguage: "..."` 这类**字符串**子节点（四份夹具都有）—— 它不携带 `titleName`，
全文替换不会误伤它，但解析校验时**别**把它当成缺字段的语言键（`.132` 第一版断言就这么错的）。
已接入 `run_host_tests.sh`（夹具缺时 SKIP）。

回归：`game112` **198/198**（新增 12 条：folder 成功提示 + 7.4 节镜像/内置两类的四连断言）、
`nav112` 20/20、`acct111` 23/23、`avatar109` 19/19、`cropfix` 75/75、`pack125` 15/15。
三个 C 文件 `-Wall -Wextra -Wshadow` 零警告（`http_server.c` 的 2 条 MHD 回调 unused param、
`game_mgr.c` 的 `getmntinfo` 是既有平台项，见 §6）。

**真机闭环已验完（PS5 192.168.1.214，PPSA27836 鬼武者：剑之道，folder 类）**：脚本
`_ftp_probe/rename_verify.py`（`--dry` 只读），**32 项断言全过**。改名后
`persisted:true`，`persist_note` = `已写入 /mnt/ext1/PPSA27836-鬼武者/sce_sys/param.json（3 个语言）`；
四层逐字节核验：① `name.txt` 新名、③ 源 2902→2905 B 且**骨架与原厂件逐字节一致**
（`+3 B` 正好是三个 titleName 值长度差之和）、CRLF 135 行不变、顶层 29 键不变、
全角括号走 `\uXXXX` 转义、⑤ `orig/param.json` 备份 == 原厂件。**关键断言**：
`POST /api/v1/scan` → `{"queued":true}` 之后 `POST /api/v1/games` 的 `title_name` 就是新名字，
没有回退 —— 这一步在 ≤.131 上正是名字被冲回去的地方。SMP 从源重建索引的旁证：重扫后它显示的
中文名来自我们改写的 `localizedParameters`，读自己缓存的 `app.db` 副本的话仍会是英文名。

三条**只在真机上才看得出来**的结论：

1. **SMP 的 `title_name` 来自它自己的 `app.db`，不是实时读源。** 产品改名之所以主屏立刻变名，
   是因为 `/game_rename` 同时写了源和 `app.db`/`appinfo.db`（`tile_db_set_name`）。所以**外部工具
   （FTP）直接改源，主屏不会跟着变**，得等 SMP 下次自己重建索引。排障时若见到「源改了、主屏没变」，
   先确认是不是走了产品之外的路径。
2. **所有语言键一起改**，包括非主力语言。实测 ja-JP（原本 `鬼武者 Way of the Sword`）就是被覆盖的
   那个。多语言游戏想保留其他语言原文，得另做按语言写 —— 当前设计是「一次改完全部语言」。
3. **还原要走产品路径，别用 FTP。** 用产品写 ja-JP 正规名后源回到 **2902 B、与原厂件零字节差**，
   证明替换长度无偏；而 FTP 直接写源虽然字节对，但 SMP 缓存不刷新，主屏名字挂着不改。

`.133` 是**刷新按钮现在真从主机重新读**（用户口径：「刷新就事从主机重新读」—— `.132` 及更早
刷新按钮和进页面那次 load 走同一条路，都吃 60 秒快照缓存。产品内部改名 / 换图标等会调
`game_mgr_invalidate()` 立刻作废，但**外部工具**（FTP 直写 `name.txt`、SMP 自己扫进新游戏）
改了源之后，用户连点十次刷新也看不到新值，只会等 60 秒自然过期）。

**根因实测**：4 次连续 `/game_list` 调用，中间 FTP 直写 `name.txt`：
#1 进页面 → `鬼武者 Way of the Sword`（15 games）；#2 立即再刷 → 字节一致（走缓存）；
FTP 写 `name.txt` = `"外部FTP直写_列表应缓存住旧值"`；#3 立即再刷 → 仍是旧名（主机真相变了，
列表没跟上）；#4 等 61 秒 → 读到新名（重建才读到）。

**修法**：加 `?force=1` 参数，只给刷新按钮用。后端 `game_mgr_list_json(buf, size, int force)`：
`force != 0` 跳过快照直接重建、刷新时间戳。进页面那次 `load()` 仍然吃缓存（快），
手动刷新才付一次全库读成本。前端 `load(force)` 接受参数，刷新按钮 `load(true)`。

**新增诊断出口 `game_mgr_cache_ts()`**：返回当前快照时间戳（0 = 空），仅供
`test/game_mgr_test.c` ⑪ 块断言。seed_stack 播种的重建字节可能恒同（两次播种的垃圾一样），
所以 `memcmp != 0` 不能当判据 —— 改用时间戳递进：invalidate 后 ts=0，force=1 必须刷成非 0；
紧跟的 force=0 必须走缓存（ts 不动、字节恒同）。

**mock 同步补齐**：60 秒快照缓存 + `/_game_list_cache` 测试钩子 + 5 条失效路径
（rename / set_icon / set_bg / reset_icon / reset_bg 全部作废缓存）—— 后端每条都调
`game_mgr_invalidate()`，mock 之前一条都没作废。

回归：主机单元测试 `game_mgr: 94 checks, 0 failures` / `overall: PASS`；
前端 `game112` **203/203**（含 7.6 段 5 条断言）。

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

> **`.120`（2026-10-06，正式出包版）**
> - **视频缩略图不显示**：`md_thumb_src` 只接受 PNG/JPEG/BMP/GIF，视频文件（mp4）被直接拒绝，导致视频永远没有缩略图。修复：视频无 icon 时返回 -3，`game_media_thumb` 生成占位缩略图（深色背景 + 播放按钮三角形）。
> - **打包卡住**：~~`md_build_entries` 每次调用都锁 `g_mu`，与 `game_media_list_json` 互相阻塞。修复：`g_img` 已加载时跳过锁（`md_scan_locked` 只读不写，无需加锁）。~~
>   ⚠️ **这条在 `.122` 被推翻**：① 成因判错了 —— MHD 是 `MHD_USE_THREAD_PER_CONNECTION`、HTTP 层没有任何全局锁，串行化解释不了「0 B / 58.7 MB」；真机复现也证明没修掉。② 那次「跳过锁」是一条**真的 use-after-free**：`md_scan_locked` 第一句就是 `md_img_locked()`，它在库的 `(size, mtime)` 变了时会 `free(g_img)` 再整份重读，而另一个线程可能正拿着 `g_mu` 走 `g_img`。`.122` 已恢复无条件加锁，真正的成因见 `.122` 那一段。
> - **列表慢**：`game_media_count` 每次调用都扫全表。修复：加 600 秒缓存 + 版本号失效（删除媒体时递增版本号）。
> - **红框高亮**：用户要求在游戏卡片的名称+体积行加背景突出显示。修复：`GameSection.jsx` 给名称+体积行加半透明背景条（`bg-white/[0.04] rounded-md px-2 py-1`）。
> - **验证口径**：`make all` 通过（`-Os -Wall` 零警告）；`npm run lint` 基线 28 errors / 6 warnings，**零新增**。
> - **出包**：`NayePlmTools_0.5.2.120.elf`，内嵌标题 `NayePlmTools v0.5.2.120 by naye`，`PLDMGR_VER:0.5.2.120`。
>   **仍未实机（PS5）验收。**

> **`.121`（2026-10-06，正式出包版）**
> - **媒体项命名**：用户要求截图/视频的命名方式为「游戏名称-截图时间」（如 `宇宙机器人-2026-10-06 12:59`）。修复：`md_scan_t` 添加 `name` 字段，`md_scan_locked` 中通过 `game_title_get_name` 填充，`md_cb_row` 中使用 `游戏名称-截图时间` 格式。
> - **压缩包命名**：用户要求压缩包命名为「游戏名称-打包时间戳」（如 `宇宙机器人-20261006_123456.zip`）。修复：`md_pack_start` 中通过 `game_title_get_name` 获取游戏名称，清理特殊字符后与时间戳组合成文件名。
> - **验证口径**：`make all` 通过（`-Os -Wall` 零警告）；`npm run lint` 基线 28 errors / 6 warnings，**零新增**。
> - **出包**：`NayePlmTools_0.5.2.121.elf`，内嵌标题 `NayePlmTools v0.5.2.121 by naye`，`PLDMGR_VER:0.5.2.121`。
>   **仍未实机（PS5）验收。**

> **`.122`（2026-10-06，正式出包版）**
> - **起因**：真机一句话 + 一张截图 ——「截图、视频打包一直卡在这个页面，并且视频没有预览图片。
>   截图下载和视频下载的命名方式是游戏名称-截图时间。压缩包也是游戏名称-打包时间戳」。
>   **截图里的数字是决定性的**：`体积 0 B / 58.7 MB`、`文件 0 / 3`、`用时 0 秒`，
>   而进度条是一条约 1/3 宽的脉冲。逐项反推：`bytes_total` 已报出 → 走过了
>   `gj_set_totals`；`elapsed == 0` 且 `rate == 0` → 这是 `watchJob` 的**第一次 tick**
>   （它同步执行、不等 1s）；1/3 宽的脉冲 → `ProgressBar` 的 `indeterminate` 分支
>   （`w-1/3` 恰是 33.3%），即 phase ∈ {preparing, measuring}。三条一起锁定：
>   **界面停在了第一次 tick 的那份快照上，此后再没更新过。**
> - **「打包一直卡住」= 三处独立成因，逐个修。**
>   ① **`zx_write_all` 会死循环。** POSIX 允许 `write()` 在 `n>0` 时返回 0，而
>      `b += w; n -= w;` 在 `w==0` 时两个变量都不动 —— 纯自旋：CPU 打满、一个字节不落盘、
>      任务槽永远停在「已开始、0 字节」。常规文件上罕见，但真机是索尼自家的文件系统，
>      **不能拿 ext4 的行为当保证**。现在 `w==0` 直接判失败。
>   ② **`pldmgr_log` 的 `printf` 会永久阻塞。** payload 的 stdout 是启动器给的
>      socket / 管道，对端不排空它；缓冲一满 `printf` 就不返回。而调用它的线程里正好有
>      后台打包线程 —— 现象就是「任务槽已经报出总量 58.7 MB，此后一个字节都不动」，
>      与截图逐字吻合。改为 `poll(POLLOUT, 0)` 先问一句，不可写就丢掉 stdout 这一份
>      （内存环形缓冲给 `/logs` + SSE、落盘 `pldmgr.log` 都照旧，排查能力一点没少）。
>      **这一条是通用加固**：`pldmgr_log` 在每个 HTTP 线程里都会被调用。
>   ③ **前端轮询会被回调拖死。** `watchJob` 里 `onTick(st)` 裸写在 `try` 里、`catch` 里
>      又调一次 —— 第二次再抛就从 async 函数里逃出去，底下那行 `setTimeout(tick, …)`
>      再也执行不到，**轮询当场死掉**，且一条报错都不显示（异常是从轮询器里逃的，
>      不是渲染出来的）。现在 `onTick` 一律包一层；另外连续失败累计 60 秒就收工并如实
>      告诉用户「与主机失联」，不让面板无限期吊在「正在打包…」上。
> - **进度条「三成满 + 右边写 0%」**：`indeterminate` 挂在 `phase` 上
>   （`measuring`/`preparing`），于是总量 58.7 MB 已经知道了、进度条还在来回跑一条
>   `w-1/3` 宽的脉冲。**一条不反映真实进度的动画比一条不动的更误导。** 改为只在
>   「总量还不知道」时用 indeterminate；`MediaManagerModal` 与 `GameDetailModal` 同步。
> - **视频没有预览图**：录屏的封面帧 `icon_path` 是 `.jxr`（HDR），stb 解不了；旧代码只看
>   「icon 在不在」就走进解码分支 → 解码失败 → 回 -2 → 404 → 磁贴纯黑。改为按
>   「**这是不是视频**」决定回占位图，不再看 icon 是否存在。
> - **下载命名端到端打通**（用户口径两条）：
>
>   | 位置 | 名字 |
>   | --- | --- |
>   | 列表显示名 | `宇宙机器人-2026-10-06 21:33` |
>   | 单个下载落盘 | `宇宙机器人-20261006_213305.png` |
>   | 压缩包落盘 | `宇宙机器人-20261006_213306.zip` |
>
>   显示名与落盘名**刻意不同**：文件名里不能有空格与冒号，而显示名要给人看。
>   后端新增 `add_disposition()`（RFC 5987 双写 `filename="…"` + `filename*=UTF-8''…`，
>   `"`/`\`/控制字符做 `%` 转义），接进 `/game_media_file`（仅 `dl=1`）与
>   `/game_media_pack_get`。前端 `<a download>` 与 `Content-Disposition` **取同一个来源**
>   （`/game_job_status` 的 `name`、`/game_media` 的 `dlname`）—— 同源才不会
>   「界面上写 A、落盘叫 B」。
> - **`/game_job_status` 新增 `idle`**（距上次有进展多少秒，任务结束后恒 0，`game_job.c`
>   的 `g_last_progress`）。不动的进度条分不清「在啃 500 MB 录屏」和「线程早死了」；
>   有了它，界面能把「正在打包…」补成「已 45 秒没有进展；若长时间不动，可取消任务后重试」，
>   并显示当前阶段（正在读取媒体库 / 正在计算体积 / 正在写入压缩包）。
> - **打包线程三段相变各留一条日志**。`/game_job_status` 在 `is_noisy_route` 里是降噪的，
>   真机出问题时日志里一行都看不到打包走到哪了。现在：没有 `measured` 就是卡在扫库，
>   没有 `writing` 就是卡在清旧包，有 `writing` 而没 `done` 就是 `zip_write` ——
>   一条日志换一次「不用再猜」。
> - **补上「大文件跨分块」这条从来没被测过的路**：`zip_write` 每次只读 256 KB，而夹具里的
>   录屏原来只有 4 KB —— 一个分块都读不满，`on_bytes` 只回调一次、CRC 回填那两行 `lseek`
>   也只在收尾走一遍。用户报的卡死正好落在这条路上。现在夹具 v1 是 1536 KiB + 12 字节头
>   （≥6 次分块），新增单文件打包断言（`bytes_total` 必须等于源文件真实大小 `1572876`），
>   `pack_big.zip` 再交给 `zipfile` 逐条复算 CRC。`media_scan` 130 → **138** 项断言。
>   另用一次性探针 `test/zipbig_probe.c` 在主机上跑过 **61 MB / 3 个文件的真机同构场景**：
>   `rc=0`、236 次分块、耗时 <1 秒，产出 `zipfile.testzip()` 全绿 —— 即**写盘这条路本身
>   没问题**，问题在上面那三处。
> - **验证口径**：`make all` 通过（`-Os -Wall` 零警告）；主机套件 `overall: PASS`
>   （`media_scan` 138 / `game_saves` 88 / `game_mgr` 65 / 三个 psaccounts 套件）；
>   `npm run lint` 基线 28 errors / 6 warnings，**零新增**。
> - **出包**：`NayePlmTools_0.5.2.122-me.elf` / `-ga.elf`。
>   **仍未实机（PS5）验收。**


> **`.123`（2026-10-06）—— 视频封面帧取真画面 + 「卡住」改为可自证**
> 起因仍是真机一句话 + 一张截图：「**一直是正在计算中**，同时视频的预览图不对，
> **应该是视频里面某一帧画面**」。两件事的成熟度完全不同，分开记：
>
> - **① 视频预览图（已修，判据在主机侧钉死）**：`.122` 的判据不够 —— 它只看
>   `icon_path` 那份能不能解，解不动就回占位图。而真机缩略图的命名约定是
>   `<原名>.<原ext>.<jxr|jpeg>`（`include/game_media.h` 头注释），**同一个录屏旁边
>   可能同时躺着两份封面帧**：一份 HDR 的 `.jxr`、一份普通 `.jpeg`；库里 `icon_path`
>   只指其中一份。指到 `.jxr` 时 stb 解不动 → 回占位图（深底 + 播放三角）→
>   用户看到的就是「预览图不对」。新增 `md_find_cover()`：icon 那份解不动（或压根没给）
>   时，去**同一个缩略图目录**里按 `<原文件名>.<后缀>` 找一份 stb 认得的位图，
>   排在「拿原图当缩略图」**之前**（封面帧本来就是为缩略图准备的，原图是 4K 整图，
>   解它又慢又占内存）。目录由 `id` 推出来、**不依赖库里那几个 `title_id` 列**
>   （列序在固件之间会变，见 `md_sql_col_index` 的注释），`id` 本身就是 `content_path`。
>   前缀匹配要求 `de->d_name[blen] == '.'`，否则 `..._001.mp4` 会被 `..._001.mp4extra`
>   之类误命中。找不到才回占位图 —— HDR-only 的录屏性质不变。
>   **测试**：新增夹具「封面帧两份都在」（`icon_path` 指 `.jxr`、同目录另有一份可解的
>   `.jpeg`），断言落到**真画面**。判据刻意用**画面内容**而不是「解开了」：真封面画的是
>   左红右蓝渐变，占位图是深底 + 白色三角，两条互斥；而「解开了」这件事给张纯黑图也能过。
>   实测取样 `左=(229,23,23) 右=(27,24,229)`。`media_scan` 138 → **147** 条。
>
> - **② 「一直是正在计算中」（本轮未定位到根因，如实记账）**：`.122` 修掉三处
>   「任务自己卡死」的成因（stdout 阻塞 / `zx_write_all` 0 字节自旋 / 轮询被回调异常带走）
>   后真机仍复现，说明还有第四处。截图证据：`体积 0 B / 58.7 MB`、`文件 0 / 3`、
>   `用时 0 秒`、`正在计算体积…`。反推：总量已报出（走过 `gj_set_totals`），
>   但**此后再没有一次成功的轮询**；而 `transient` 那句「与主机失联，正在重试…」
>   又没出现 → 轮询不是「一直失败」，是**停了**。停在 `measuring` 说明 worker 卡在
>   `gj_set_totals` 与 `md_set_phase("packing")` 之间（即 `pack measured` 那行日志、
>   清旧包、或 `md_set_phase` 本身）。**光读代码已分不出「连接被堵」和「前端定时器没再走」**，
>   所以本轮不再猜第二次，改成让下一次复现自己把话说清楚：
>   - **修掉唯一一处能证明的隐患**：`log_append_file` 的 `fopen/fprintf/fflush` 原来跑在
>     `log_mutex` 里，而 `log_mutex` 是所有 `pldmgr_log` 的必经之路、`pldmgr_log` 又长在
>     打包 worker 的路径上 —— **一次慢盘写 = 全进程日志一起堵**，打包线程会停在某一行日志上。
>     这与 `.122` 修的 stdout 是**同一类**错（把可能无限期阻塞的 I/O 放进别人必经的临界区）。
>     现在环形缓冲 + SSE 通知留在 `log_mutex`（纯内存），磁盘那份挪到锁外由独立 `file_mutex` 串行。
>   - **看门狗线程**（`md_watchdog`，`game_media.c`，首次起打包任务时创建）：任务活着时
>     每 5 秒看一次 idle，>15 秒就写一行 `media: STALL job=… phase=… idle=…s bytes=…/… files=…/…`，
>     同一段无进展每涨 30 秒再报一次。**日志的两个去处（界面「日志」页 / `/data/nayeplmtools/pldmgr.log`）
>     都不经过前端轮询**，所以界面定住时这一行照样看得到。下次复现只要把那一行发过来：
>     `phase` 是 `preparing`/`measuring`/`packing` 分别指向三个不同的下一步。
>   - **阶段日志带单调毫秒**（`+Nms`，`CLOCK_MONOTONIC`）：既定位卡在哪段，也分清「慢」与「卡」。
>   - **轮询心跳**（`game_media_pack_status`）：`/game_job_status` 是降噪路由，日志里看不出
>     前端还在不在轮询 —— 而这正是最关键的分岔。按「首个 + 每 20 个」记一条（约 20 秒一行）。
>   - **前端多挂一把本机时钟的尺子**：后端 `idle` 是**主机那边**的尺子，主机时钟一跳
>     （或槽里的时间戳落到未来）`difftime` 就是负数、被钳成 0 → 界面永远「用时 0 秒、无任何异常」。
>     现在记录最近一次**真的收到**主机数据的时刻（`transient` 重试不算），超过 15 秒就显示
>     「已 N 秒没有收到主机的新数据」。两个任务面板（`MediaManagerModal` / `GameDetailModal`）
>     同构 —— `.122` 的教训就是同一个 bug 得修两遍。
> - **验证口径**：主机套件 `overall: PASS`（`media_scan` **147** / `game_saves` 88 /
>   `game_mgr` 65 / 三个 psaccounts 套件）；浏览器回归 `game112` **177/177**、
>   `nav112` **20/20**；`game_media.c` / `game_job.c` 在 `-Os -Wall -Wextra` 下零警告
>   （`log_server.c` 主机上编不了，它要 `microhttpd.h`，由出包那次编译覆盖）。
> - **出包**：`NayePlmTools_0.5.2.123-me.elf` / `-ga.elf`。**仍未实机（PS5）验收。**
> - **给下一轮的抓手**：若真机再复现，先看 `pldmgr.log` / 界面日志页里有没有 `STALL` 那一行，
>   以及 `status poll` 有没有继续往上涨 —— 这两条能把「后端卡住」与「前端轮询停了」一刀切开。


> **`.124`（2026-10-06）—— 封面帧改认 `icon_path` + 成品 zip 不再可能被取走半截**
> 起因还是真机两句话：「**打包下载还不好用。视频封面也没有。**」同一对症状的**第三轮**，
> 而且 `.123` 在封面那条上**做错了**，先认账再修。
>
> - **① 封面：`.123` 错在两处，真机表现是「格子全白」（比占位图还差）**：
>   - **目录不该由 `id` 推。** `include/game_media.h` §7a 白纸黑字写着「录屏目录层级是否与
>     photo 同构」**尚未在真机验证过**（当年取证时机器上只有截图），而 `.123` 恰好把封面帧
>     目录按 `id` 推了出来 —— **拿一个没验证的假设当事实用**。现在先扫 **`dirname(icon_path)`**：
>     那是**媒体库自己写进库里的**，指哪儿就是哪儿，最权威；按 `id` 推的目录只作兜底。
>   - **扩展名不等于能解码。** `.123` 只按扩展名放行候选，可能挑中一个「名字像 JPEG、
>     内容不是」的文件 → `stbi_load_from_memory` 失败 → `-2` → HTTP 404 → **格子从
>     「占位图」退化成「一块空白」**。现在按**文件头魔数**认（JPEG/PNG/BMP/GIF），
>     扩展名只用来排除 `.jxr`。
>   - 另外补一个洞：`md_thumb_src` 里 `md_abs()` 失败时**无条件**回 `-1`，视频跟着走了 404。
>     现在按「是不是视频」分流，视频一律 `-3`（占位图）—— **视频永不 404**。
>   - **`.123` 的测试是自证空转的**：它的夹具把「`icon_path` 所在目录」与「按 id 推出来的
>     目录」摆成**同一个目录**，所以它把错的假设测成了绿的。`.124` 的夹具刻意让两者
>     **不同构**（缩略图目录少一层生成器），并先断言「按 id 推出来的那个目录**不存在**」。
> - **② 打包下载：找到了一个能证实的独立成因（不是「卡住」那个）**：
>   `zip_write` **直接写成品名**，写到一半时成品路径上就是一个 `size > 0` 的**残包**；
>   而 `game_media_pack_open` 只看「文件在不在、大小 > 0」→ 一次并发（或用户手快重按下载）
>   就把半截包当成品发走，浏览器拿到坏 zip。失败/取消的任务留下残包后，用它的 job id
>   照样取得到。**修法**：zip 先写 **`.part`** 再 `rename`（同文件系统内原子，成品路径上
>   要么完整要么没有），并给 `game_media_pack_open` 加 **`gj_ready` 门禁**（任务必须
>   **已成功收尾**才允许取成品，见 `include/game_job.h`）。返回值由两态细分三态：
>   `-1` 任务不存在 → 404；**`-2` 还没成功收尾 → 409**（以前也回 404，于是「打包途中点
>   下载」看起来像「任务凭空消失」）；`-3` 成品没了 / 0 字节 → 404。
>   顺带修掉：占位图的缓存键可能和真封面**串图**（`md_thumb_src` 最后那个 `-3` 分支忘了清
>   `st`，占位图用上了视频文件自己的 mtime；同脚本先后写的文件 mtime 相同，当场复现）。
> - **③ 「打包卡住」的根因本轮仍未定位**，如实记账（`.122` 修三处、`.123` 修一处，
>   真机是否还复现**还没有证据**）。这一版不猜第五次，而是把**取成品这条路上每一步**
>   也写进日志：`pack_get job=N not ready (state=…)` / `open failed: <path>` /
>   `serving <N> bytes (<path>)`。配上 `.123` 的 `pack start / measured / writing /
>   job done` 与 `STALL` 看门狗行，这条路上再没有「看不见的下一步」。
> - **④ 新增变异测试 `test/mutation_check_media.sh`（出包前手动跑）**：把三处关键改动
>   逐条掐掉，确认测试**真的会红** —— 掐掉 `icon_path` 那条路 → 2 条断言红；掐掉魔数校验
>   → 4 条红；掐掉 `gj_ready` 门禁 → 1 条红。**这一课是用两轮真机返工换来的**：用例只要
>   不断言关键性质，就会把错的假设测成绿的。注意脚本会把被变异的文件**顶掉** `SRCS` 里的
>   原件（否则重复定义）。
> - **验证口径**：主机套件 `overall: PASS`（`media_scan` **161** / `game_saves` 88 /
>   `game_mgr` 65 / 三个 psaccounts 套件）；变异测试三项全红（`==== 变异测试: PASS ====`）；
>   浏览器回归 `game112` / `nav112`；payload 编译 `-Os -Wall` 零警告。
> - **出包**：`NayePlmTools_0.5.2.124-me.elf` / `-ga.elf`。**仍未实机（PS5）验收。**
> - **给下一轮的抓手**：若真机再报封面，先看日志里有没有
>   `media: cover dir MISSING <path>` / `media: cover dir <path> has N entries [names…]` /
>   `media: no decodable cover for <id>` —— 这三行会**直接告诉真机上录屏缩略图到底在哪个
>   目录**，§7a 那条「待验证」就能一次问清（已经猜错两轮了，别再猜第三次）。


> **`.125`（2026-10-06）—— 打包的真因在前端构建产物里；封面是 JPEG XR**
> 起因仍是「**打包下载还不好用。视频封面也没有。**」，第三轮。这一版**不再猜**，
> 改成自己去真机取证：FTP（`192.168.1.214:2121`，anonymous）拉日志 / 目录树 /
> `av_content.db`，再用 HTTP 直连 8911 拉一次成品包。**取证把前两轮的判断推翻了一半。**
>
> - **① 「打包下载」的真因在后端之外 —— 后端从头到尾都是好的。**
>   真机日志（`/data/nayeplmtools/pldmgr.log`）：
>   `pack measured 61518012 bytes / 3 files (+1ms)` → `pack writing … .part` →
>   `pack job done -> …/宇宙机器人-20261006_231804.zip (61518012 bytes, +1179ms)`。
>   **1.2 秒打完。** 成品在盘上 61,518,688 字节；HTTP 取它 `Content-Length` 正确、
>   **0.5 秒完整收全 61,518,688 字节**、`zipfile.testzip()` 无错、三个条目尺寸都对。
>   → 「打包」与「下载」本身**没有任何问题**。
> - **② 真因：前端构建产物里 `watchJob` 的 `setTimeout(tick, …)` 中 `tick` 是悬空标识符。**
>   构建 target 是 `['es2015','safari12']`，es2015 没有 async，Babel 要把 `async` 降级成
>   generator 包装；对**自引用的 const 箭头函数**（`const tick = async () => { …
>   setTimeout(tick, …) … }`），Babel 会把整个 `const tick = …` 换成一个**立即调用并丢弃的
>   IIFE**，却**不重写函数体里的那个自引用**。于是第一次 `/game_job_status` 正常返回，
>   紧接着 `setTimeout` 抛 `ReferenceError: tick is not defined`，**轮询永久停摆**。
>   界面停在「正在打包… 0%」、下载按钮永不出现 —— 正是用户报的「一直是正在计算中」，
>   也正好解释日志里**只有 `status poll #1`、一条 `pack_get` 都没有**。
>   **修法：自引用的 async 函数一律写成函数声明**（名字绑在外层作用域，降级后仍有效）。
>   同一个文件里既有的 `async function poll()`（App 的自动加载轮询）本来就是这么写的、
>   真机一直正常 —— 现成的反证。同类隐患一并修：`StorageHub.fetchRemote`（24h 过期刷新
>   自引用）、`App.refreshPayloads`（失败重试自引用）。
> - **③ 为什么 177 项回归全绿却漏了 —— 回归体系的盲区（本轮最重要的方法论收获）。**
>   `game112.mjs` 跑的是 **vite dev server**，发出去的是原生 ESM，`async/await` 不降级；
>   真机跑的是 Babel 降级后的**构建产物**。这条 bug 只存在于后者。
>   → 新增 `serve_dist.mjs`（`frontend/dist` 静态 + 其余反代到 mock，5200）与
>   `pack125.mjs`（**跑在产物上**：断言无未捕获异常、`/game_job_status` 轮询 ≥2 次、
>   面板进终态、出现「下载打包文件」、下载端点真回字节）。
>   **已用回退验证它能抓住这个 bug**：把修复 stash 掉重新构建后该回归 **5 项红**，
>   报错正是 `ReferenceError: tick is not defined`，面板停在「正在打包… 正在计算体积… 0%」。
> - **④ 封面：PS5 给的是 JPEG XR，载荷解不了（三轮封面失败的真正原因）。**
>   真机取证拿到目录树 + `av_content.db` + 封面文件本身：录屏封面在
>   `/user/av_contents/thumbnails/video/<生成方>/<TID>/<hex>/<名>.webm.jxr`，
>   文件头 `49 49 BC 01` = **JPEG XR**（1920×1080 24bppRGB，约 75:1，DCT 编码）。
>   载荷里只有 stb_image，**解不了 jxr**，所以怎么改都只能回占位图。
>   照片那条路没事：照片封面是 `.png.jpeg`（普通 JPEG）。
>   → 不往载荷里塞 JXR 解码器，改成**让浏览器从 webm 取帧**：视频格子直接渲染
>   `<video preload="metadata" muted playsInline>`。真机实测（ASTRO BOT 那条 43MB 的 webm）：
>   `readyState=4`、3840×2160，画到 canvas 取像素 avg=57 / max=242 —— 有画面的首帧。
>   **刻意不 seek**：文件直出没有 Range 支持，Chrome 判该媒体不可定位
>   （实测 `currentTime` 设不动、`seekable` 为空），写 seek 只是死代码。
> - **⑤ 顺手修掉 mock 的一处「假绿」**：mock 的下载端点用中文文件名直接
>   `res.setHeader('Content-Disposition', …)`，Node 遇到非 ASCII 抛 `ERR_INVALID_CHAR`、
>   express 回 500 —— mock 上「下载」这条路**从来没被真正跑通过**。这不只是测试缺口：
>   真机那个 bug 本来「点一下下载」就能立刻暴露，是被这里先 500 掩盖掉的。
>   改成与后端 `add_disposition` 同口径的 ASCII 兜底 + RFC 5987 `filename*` 双写。
> - **验证口径**：主机套件 `overall: PASS`（`media_scan` 163 / `game_saves` 88 /
>   `game_mgr` 65）；变异测试三项全红（`==== 变异测试: PASS ====`）；
>   浏览器回归 `game112` **177/177**、`pack125` **10/10**（连跑三次稳定）；
>   两个 ELF 各 10,785,872 字节、titleId NAYE00001/NAYE00004、内嵌页与
>   `frontend/dist/index.html` **逐字节相同**、内嵌标题 `(7d5540d, …)` 非 DEV、
>   `.122`–`.124` 版本号残留 0；payload 编译 `-Os -Wall` 零警告。
> - **出包**：`NayePlmTools_0.5.2.125-me.elf` / `-ga.elf`。
> - **✅ 已实机（PS5）验收 —— 12/12 PASS（2026-10-06 深夜，本轮第一次「推包 + 自测 +
>   出结论」全流程自己做）**。推包：`GET /shutdown` 停旧 → Python socket 灌
>   `192.168.1.214:9021`（elfldr，环境无 socat/docker 故不走 `deploy.sh`）→ `/version`
>   回 `0.5.2.125`。验收脚本 `~/.workbuddy-ai/binaries/node/workspace/device125.mjs`，
>   **跑在真机内嵌的那份构建产物上**（既含 Babel 降级产物、又接真后端，比 `pack125.mjs`
>   更接近用户实际看到的东西）：
>   - **① 打包**：轮询 **3 次**（`.124` 是 1 次就停）、面板进终态「打包完成」、
>     出现「下载打包文件（3 项）」；全程 `pageerror` **0**，尤其没有 `tick is not defined`；
>     下载端点 HTTP 200 / `Content-Length=61518688` / 实收 **61518688** / `PK` 魔数 /
>     `Content-Disposition` 双写正确（`宇宙机器人-20261007_001142.zip`）。
>   - **② 封面**：网格 `video=1 / img=2`，`readyState=4`、`3840×2160`、`duration=7.1`，
>     画到 canvas 采样 `avg=57 / max=242` → **是有画面的真帧**，不是占位图也不是黑帧。
>   - 截图：`D:/pldmanager/.shots/device125/{panel,pack}.png`。
> - **给下一轮的抓手**（这两条已当场验过，留作回归口径）：
>   ① 打包完**有没有出现「下载打包文件」按钮**（`.124` 及以前永远不出现）；
>   ② 视频格子**是不是真画面**（`.124` 及以前是深色占位图）。
>   若封面仍是占位图，那就是 **PS5 的浏览器解不了 webm**，届时再评估给载荷加 JXR 解码器
>   （`third_party` 里塞 jxrlib）——**那是另一个量级的工程，不要顺手做**。
> - **明确没做（见 CHANGELOG 同名小节）**：文件直出的 **Range 支持**。它能一次解决三件事
>   （封面可 seek 到更代表的一帧、封面只需拉几百 KB、视频预览可拖进度条），
>   但它要动 `gr_file_response` 这条**被打包下载复用**的路 —— 刚把下载修好，
>   不宜在同一版里再动它。另外 `<video>` 走 `preload="metadata"` 在没有 Range 的服务器上
>   会把整个录屏拉完（43MB 那条实测如此），这一点在加 Range 之前无法避免。


> **`.126`（2026-10-07）—— 压缩包改成「游戏编码-游戏名称/」一层，并为摊平带来的撞名补去重**
> 用户口径：「**打包的多媒体，压缩包下级文件夹是 游戏编码-游戏名称 这个文件夹里面
> 直接就是截图和视频**」。
>
> - **① 布局整个换了。** `.125` 及以前条目名 = `content_path` 去掉 `/av_contents/`，
>   也就是原样复刻磁盘那套 `photo/NPXS40087/PPSA21567/230/…` —— 那是**我们的实现细节**，
>   对用户是噪音，还把同一批截图拆进不同目录。现在统一一层：
>   ```
>   PPSA21567-宇宙机器人/
>     ├─ 宇宙机器人-20261006_125910.png
>     ├─ 宇宙机器人-20261006_130015.webm
>     └─ …
>   ```
>   文件夹名 = `游戏编码-游戏名称`（取不到游戏名就只用编码），里面的文件名与
>   **单独下载**那份**同源**（同一个 `md_dlname`）—— 压缩包里外两套名字只会让人对不上号。
>   改在 `src/game_media.c` 的 `md_build_entries`（新 `md_zname_unique` / `md_utf8_cap`）。
> - **② 摊平就必须承担撞名的风险 —— 所以加了去重（本轮最容易被漏掉的一条）。**
>   下载名只精确到秒（`游戏名称-YYYYMMDD_HHMMSS.ext`），原文件名里那截随机数被丢掉了：
>   **同一秒连拍两张必然同名**。zip 里两条同名条目在 Windows 资源管理器里解压是
>   **互相覆盖** —— 那是静默丢文件，比名字难看严重得多。撞了就补 `-2` / `-3`…
>   （只有真撞上才动手，常见情况仍是干净名字）。
> - **③ 文件夹名按文件名的规则清一遍**（游戏名里带冒号是常态：「蜘蛛侠：迈尔斯」），
>   并按 **UTF-8 边界**兜长度上限（120 字节）—— 不能把汉字劈成半个，也不能让
>   `folder + "/" + dlname` 撑爆 `zip_ent_t.zname[512]`。条目名带 UTF-8 标志位
>   （general purpose bit 11，见 `src/zip_io.c`），所以中文目录名在 Windows 上正常。
> - **④ 夹具的 mtime 必须钉死（这条是踩出来的）。** 条目名用文件**真实 mtime**，
>   而夹具以前是「写到哪算哪」→ 每次跑出来的名字都不一样，
>   「同一秒两张要去重」这条**根本没法断言**（时红时绿，比不测更糟）。
>   现在 `make_media_fixture.py` 的 `put()` 按**文件名里那个时刻**钉 mtime，
>   顺带让下载名与原始文件名里的时刻终于对得上。**写任何依赖 mtime 的断言之前先看这条。**
> - **验证口径**：主机套件 `overall: PASS`（`media_scan` **163 → 178** / `game_saves` 88 /
>   `game_mgr` 65）；变异测试 **5/5 红**（新增 `noplain` 7 条红、`nodedup` 2 条红）。
>   `media_verify.py` 的 zip 模式**重写**：独立复算条目名（含清名 / 扩展名小写 /
>   mtime 本地时间戳 / 去重补号），并断言结构性质 —— 每个条目名**恰好一个 `/`**、
>   所有条目**共用同一个顶层文件夹**、**没有重名条目**。
> - **出包**：`NayePlmTools_0.5.2.126-me.elf` / `-ga.elf`。
> - **✅ 已实机（PS5）验收 —— 通过（2026-10-07）**。推包后跑 `device126_pack_check.py`
>   （走面板自己的接口起任务、取成品包，再用标准库 `zipfile` 逐条验）：真机上解压出来就是
>   ```
>   PPSA21567-宇宙机器人/宇宙机器人-20261006_125910.png
>   PPSA21567-宇宙机器人/宇宙机器人-20261006_125918.png
>   PPSA21567-宇宙机器人/宇宙机器人-20261006_130015.webm
>   ```
>   条目数 == 列表条数、**没有重名条目**、每个名字**恰好一个 `/`**、所有条目共用同一个
>   顶层文件夹、条目名 == `<编码-名称>/<列表里的 dlname>`（与单独下载同源）、
>   每条内容与 `/game_media_file` **逐字节一致**、CRC 全对。包 61,518,706 字节。
>   同时 `device125.mjs` **12/12 PASS** —— 这一轮没碰坏 `.125` 修好的那两件事。
>   出包核对：两 ELF 各 10,785,872 字节、titleId NAYE00001/NAYE00004、内嵌标题
>   `(7ccc1e5, …)` 非 DEV、`.110`–`.125` 残留 0、内嵌页与 dist 逐字节相同。
> - **老包不会自动变**：已经下过的 zip 保持原样，不必回改。


> **`.127`（2026-10-07）—— 游戏详情的「来源类型」整片错标成「已安装 PKG」；根因是拿枚举来源当了来源类型**
> 用户口径：两张游戏详情截图（`光与影：33号远征队` / `FINAL FANTASY VII REMAKE`）「**这里都标识错误，
> 你再跟 shownmount 对比一下**」，追问后钉死范围：「**格式没错，但是他显示是已安装 pkg 是错的**」
> —— 即**只修来源分类，不动格式词表**（`ffpkg`/`exfat`/`ffpfs`/`ffpfsc` 这些扩展名照旧）。
>
> - **① 真根因：`source_type` 取了 `a.d_src`。** `d_src` 是「这行是从哪个枚举来源来的」
>   （app.db 还是扫描路径），**不是物理来源类型**。ShadowMount 挂出来的镜像/文件夹游戏
>   **同样会注册进 app.db**，所以它们的 `d_src` 也是 `"installed"` → 14 个游戏全被打成
>   `已安装 PKG`，并连带 `installed_pkg:true` / `can_manage_source:false` / `/game_delete` 返 403 EPERM。
>   **修法**：新增 `gm_class_of_fmt(fmt)`，只看**物理来源**——`pkg→"pkg"`、`folder|lz4→"folder"`、
>   其余（`ffpkg/exfat/ffpfs/ffpfsc`）`→"image"`；`source_type` / `image_type` / `installed_pkg` /
>   `mounted` / `can_manage_source` / `gm_paths()` / `game_mgr_source()` 全部改由它派生。
>   `game_mgr_source()` 的 `src` 出参语义随之改为**类别**（`"pkg"|"image"|"folder"`），
>   `game_runtime.c` 的 EPERM 闸门从「`src=="installed"`」改成「`src=="pkg"`」。
> - **② 对照物不是猜的，是设备自己跑着的 ShadowMountPlus 1.7beta4。** 真机 `192.168.1.214:10101`
>   就是 SMP 的 Web UI + JSON API（全部路由是 **POST + JSON body**）。`POST /api/v1/version` →
>   `{"shadowmount_version":"1.7beta4","api_version":1,…}`。于是「跟 shownmount 对比」变成逐字段实测：
>   从 GitHub 取 `1.7beta4` 源码（`docs/api.md` / `src/sm_api_service.c` / `src/sm_filesystem.c` /
>   `src/sm_image.c`）读字段定义，再对 `PPSA17599` / `PPSA01773` / `PPSA99101` / `PPSA01650`
>   逐个跑 `games/info` 比对我们 `/game_detail` 的每一项。**以后再有「跟 SMP 对齐」的需求，直接打这个 API，别照文档猜。**
>   | 字段 | SMP 1.7beta4 的语义 | 我们 `.126` 的错值 | 我们 `.127` |
>   | --- | --- | --- | --- |
>   | `source_type` | `image`（读得到 `/user/app/<TID>/mount_img.lnk`）/ `folder` / `pkg` | 恒 `pkg` | 按 `gm_class_of_fmt` |
>   | `image_type` | **文件系统名**（`sm_image.c` `image_fs_name()`）：`.ffpkg→ufs`、`.exfat→exfatfs`、`.ffpfs→pfs`、`.ffpfsc→pfsc` | 恒空（因为被当 pkg） | 仅 image 类给 `fmt` |
>   | `is_data_mounted` | `stat("/system_ex/app/<TID>/sce_sys/param.json")` 存在 | 用 `runtime[0] != 0` | 同 SMP 规则 |
>   | `is_installed` | `/user/app/<TID>` 存在 | 同 | 同 |
>   | `managed` | 读得到 `/user/app/<TID>/mount.lnk` | 未单独暴露 | 未单独暴露 |
>   | `installed_pkg:true` 的连带 | `managed:false` / `mounted:false` / `path=app.pkg` / `runtime_path=""` / `can_manage_source:false` | 全错（真源被藏） | 一致 |
> - **③ `mounted` 判据换了，这是本轮唯一的行为性修正。** 原来是 `runtime_path` 非空就算挂载 —— 但
>   文件夹类游戏的源目录**永远存在**，等于恒真。改走 SMP 那条 `/system_ex/app/<TID>/sce_sys/param.json`。
>   实测：`PPSA99101`（飞牛音乐，folder）SMP 报 `mounted:true` 且 FTP 里能看到 `/system_ex/app/PPSA99101/sce_sys`；
>   `PPSA01773` SMP 报 `mounted:false` 且 FTP 里该路径**不存在** —— 恰好一致，但这是**规则一致**，不是巧合对齐。
> - **④ 顺带纠一条老文档。** `CHANGELOG.md:1803` 与 `game_mgr.c:412` 的注释都写「与 ShadowMountPlus 一致的
>   取值：`ffPKG / exFAT / ffpfs / ffpfsc`」——**错的**，那是**扩展名**，SMP 的显示名是
>   `ufs / exfatfs / pfs / pfsc`。已改注释与 CHANGELOG，并注明「别再声称这串与 SMP 一致」。
>   按用户口径**词表本身不改**（格式没错），只是不再谎称它与 SMP 同名。
> - **⑤ 可测性：把两个绝对根提成编译期宏。** `GAME_MGR_APP_BASE` / `GAME_MGR_SYSTEM_EX`（`#ifndef` 兜底，
>   默认 `/user/app` 与 `/system_ex`），主机测试用 `-D` 指到 `/tmp/pldmgr_gm_test/` 下——
>   与既有的 `GAME_MGR_APPDB_PATH` / `GAME_MEDIA_ROOT` 同一手法。夹具 `make_appdb_fixture.py` 重写：
>   造 3 个游戏 + `/user/app` / `/system_ex` 两棵树（PS5 镜像类 / PS4 真 PKG / 文件夹类，后者带 `param.json` 验 `mounted:true`）。
> - **⑥ 前端与 mock 同步。** `GameDetailModal.jsx` 的来源判断从 `g.src === 'installed'` 改为
>   `g.installed_pkg === true || (undefined && g.fmt === 'pkg')`，`sourceType` 优先取后端 `g.source_type`；
>   `doUninstall()` 与「无物理源」提示都改判 `installed_pkg`。`mock-server.js` 的 `buildDetail()` 整个从 `fmt` 反推
>   （不再看 `src`）。`gameApi.js` 文件头补 `.127` 契约块，写清 `src` / `source_type` / `image_type` / `path` /
>   `mounted` / `installed_pkg` / `can_manage_source` 七个字段各自的含义。
> - **验证口径**：主机套件 `overall: PASS`（**`game_mgr` 65 → 90** 条；`media_scan` 178 / `game_saves` 88 未动）；
>   变异测试 **`mutation_check_game_mgr.sh` 4/4 红**（`srcdsrc` 8 红 / `imgtype` 2 / `pathimg` 2 / `mounted` 1），
>   `mutation_check_media.sh` 5/5 红照旧；前端回归 `game112.mjs` **177 → 185 PASS**（新增 pkg 类与 folder 类两条取样）。
> - **出包**：`NayePlmTools_0.5.2.127-me.elf` / `-ga.elf`，各 10,785,872 字节、titleId 自检
>   NAYE00001/NAYE00004 通过、内嵌标题真实短哈希 `952e0b7`（非 `DEV`）、内嵌页与 `frontend/dist`
>   逐字节一致。
> - **✅ 已实机（PS5）验收 —— 通过（2026-10-07）**。两条通道都验了：
>   - **接口逐字段对照**（`_ftp_probe/badge_verify.py`）：真机 15 款**全部与 SMP 1.7beta4 的
>     `/api/v1/games` 一致**，`source_type` 不符 **0/15**。推包前是 3/3 不符（`PPSA17599`
>     `pkg`→`image`、`PPSA01773` `pkg`→`folder` 且 `mounted` 由 `true` 改成 `false`、`PPSA99101`
>     `pkg`→`folder`）；`path` 也对了（`PPSA17599` 由 `/user/app/PPSA17599` 变成
>     `/mnt/ext1/PPSA17599三十三号.ffpfsc`）。真·内置 PKG 样本 `PPSA01650` 仍判 `pkg` /
>     `installed_pkg:true` / `can_manage_source:false`，未被误伤。
>   - **界面级**（`device127_badges.mjs`，打真机 8911、跑的是真机内嵌的那份构建产物）
>     **17/17 PASS**：版本 0.5.2.127、15 张磁贴、`PPSA17599` 来源类型=**镜像** + 有「删除源」、
>     `PPSA01773` =**文件夹** + 无「已挂载」+ 有「删除源」、`PPSA01650` =**已安装 PKG** + 有
>     「没有可删除的物理源」提示，以及**全列表除 `PPSA01650` 外 0 款**显示「已安装 PKG」。
> - **刻意没改的两处**：
>   - 磁贴列表右上角那个角标**仍按 `src` 上色**（`GameSection.jsx:81`
>     `const installed = g.src === 'installed'`）—— **这个是「已安装 / 转储」，不是「已安装 PKG」**：
>     它回答「这个游戏在列表里是**怎么被找到的**」，已注册进 app.db = 绿「已安装」，
>     只躺在扫描路径上的 dump = 黄「转储」。ShadowMount 挂出来的游戏**也会注册进 app.db**，
>     所以真机上 15 款 `src` 全是 `installed`、15 个角标全亮「已安装」—— **这是对的，不是本 bug**。
>     用户问过「这里都是已安装 对吗」，答案就是对的；**别拿这个角标去改，也别为了「跟详情一致」
>     把它换成按 `source_type` 上色**（那是三个不同的问题：注册与否 / 物理源形态 / 是否真 PKG）。
>   - `can_uninstall` 仍读 app.db 的 `uninstallable` 列（与来源分类无关）。
> - **与 SMP 刻意保留的一处偏差**：真·内置 PKG 的 `path`，SMP 给 `/user/app/<TID>/app.pkg`
>   （那个文件），我们给 `/user/app/<TID>`（app 目录）。**判据与徽标完全一致**（`source_type`
>   都是 `pkg`），只是路径指向粒度不同 —— 对「这个游戏的物理源在哪」而言 app 目录更直读，
>   所以不改。要改的话只动 `gm_paths()` 的 `pkg` 分支（`appdir` 后拼 `/app.pkg`），别再动别处。

`.136` 是**修「磁贴上的体积在说谎」**——用户截图：详情弹窗 `体积 132 GB` / `app.db 1.22 GB`，
而右边同一款游戏的磁贴写 `1.22 GB`。这是 `.134`/`.135` 两个提交叠出来的回归：`.134` 为了
「列表秒开」在列表模式置 `skip_du=1`（folder 游戏 `size=0` → 标签消失），`.135` 用
`formatSize(g.size || g.appdb_size)` 把标签找回来，但显示的是 **app.db 的注册体积**，不是游戏体积
（132 GB 的游戏显示 1.22 GB，比不显示更糟）。**预览里发现不了**是因为 mock 的列表直接给 folder
游戏塞了真实 `size`，「列表不扫盘」这个前提被整个抹掉。修法按用户拍板走**后台预热**：
① 体积缓存拆 `gm_dir_size_peek`（只读、不扫盘）/ `gm_dir_size_cached`（未命中才 du 并写回），
缓存表新增 `g_du_mtx` 自保；② 列表模式只 peek，未命中留 0 并把根目录攒进 `g_warm_roots`
（上限 64、去重、只收 folder/lz4）；③ `build_game_json` 末尾 `gm_warm_kick()` 起 detached 线程
去 du，扫完填缓存 + **作废 60s 快照**，列表 JSON 加 `"warming":true`；④ 前端在 `warming` 期间
每 4 秒自动重拉一次（上限 15 次），且 `app.db` 的 size 列只给内置 PKG 兜底
（`g.size || (g.fmt === 'pkg' ? g.appdb_size : 0)`）。锁序恒为 `g_game_mtx → g_du_mtx`，
worker 扫盘在锁外、收尾只取 `g_game_mtx`，无环。mock 照抄这条时序（folder/lz4 初始 `size=0` +
`warming`，3 秒后补齐；`/reset` 清预热状态），夹具的裸目录源改成**真实目录树**，
`game_mgr` 断言 94 → **100**，`mutation_check_game_mgr.sh` 加 3 个变异体（`appdb` / `peek0` /
`nodu`，三个都真变红）。回归 `game112` **203/203**。**本轮为纯源码/mock/测试改动，未出包。**

`.137` 是**修「面板里改过的中文名，SMP 与电视主屏还显示英文原名」**（用户口径：「我现在需要让
前端读到的和 SMP 读到的都一样，另外镜像类的也考虑」）。根因是**时序错位**、不是代码写错：
用户那 8 次改名发生在 **10:59–11:17**（`name.txt` 的 MDTM 证据），而写**游戏源**
`sce_sys/param.json` 的能力 **13:57** 才提交（`.132`），源文件自安装起从未被写过；之后 SMP
重扫按源重建 `app.db`，中文名被冲回原名。`.132` 那条「SMP 重扫不回退」的真机闭环其实是用
PPSA27836 做的、验完还还原回了原名，所以当时没暴露这条时间差。**真机三个实验把「SMP 到底读
哪一层」钉死了**：① 改 `/user/app/<TID>/sce_sys/param.json` 对 SMP **无效**（定点替换后 60 秒
纹丝不动、文件也没被覆盖）→ 这条路是死的；② 改**系统 `app.db`** 对 SMP **有效**，且**镜像类
也立刻生效**（`persisted:false` 但 `title_name` 立刻变中文）；③ `POST /api/v1/scan` **不会**
重建 app.db 的名字（中文名 120 秒内活着）→ `.132` 的「SMP 重扫会冲掉」结论归因错了。所以回填
的**主力是 app.db 那一层**，不是游戏源。修法：新增 `game_title_sync_all()`（遍历
`/data/nayeplmtools/titles/`，**只处理有 `name.txt` 的款**、绝不替用户决定没改过的游戏；
逐款先写主机索引 `tile_db_set_name`，folder 类再写一次源 `param.json`，幂等靠 `td_count_differ`
先 COUNT/SUM 比一遍）+ **启动自愈线程**（detached、延迟 25 秒、SAFE MODE 跳过）+
路由 `POST /game_title_sync` + 前端板块右上角「同步名字到主机」按钮（跑完如实报口径 +
强制重拉列表）。mock 把 `renamed`（name.txt）与 `hostNames`（主机 app.db）**拆成两份**，
加 `GET /_host_names` / `POST /_host_drift` 两个钩子模拟 SMP 重扫，`game112` 第 7.7 节
**11 条**断言断言的是「漂了 → 点同步 → 补回来」。回归 `game112` **214/214**。

`.138` 是**修 `.137` 的致命漏登记**：`POST /game_title_sync` 在真机上**连接被直接关掉**
（`RemoteDisconnected`，不是 404/500，payload 本身活着）。根因是 `http_server.c` 里
**两张分开写的表** —— `gr_post_limit()` 决定「收不收请求体」（返回 0 = 不归我们管），
`gr_dispatch_post()` 决定「收到之后怎么处理」；`.137` 只加了后者、漏了前者，于是那个
`PostStatus` 累加器根本不建、`gr_dispatch_post` 永不触发。**三道门全放行**是因为
`gr_post_limit` 要 `microhttpd.h`（主机套件编不了）、mock 里也没有「收体闸门」这一层，
214 项前端回归照样全绿。修法：限长表补 `ROUTE_GAME_SYNC`（1 MB 档）。**新增静态守卫**
`tools/check_post_routes.sh`（接进 `run_host_tests.sh` 最前面）：解析两个函数体，断言
`gr_dispatch_post` 分发的每条路由都在 `gr_post_limit` 登记过；负向对照（还原那条漏登记）
立刻点名并非零退出。**`.137` 不是全废** —— 启动自愈不走 HTTP、真机上确实跑了：
`[PLDMGR] title: 启动自愈完成（回填 8 款：主机索引 8、名字源 5、失败 0）`，15 款面板名与
SMP `title_name` 全部一致（含 3 款 `ffpfsc` 镜像类），坏的只有那颗按钮。

**出包（2026-10-07）**：`NayePlmTools_0.5.2.138.elf` / `-me.elf`（NAYE00001）/ `-ga.elf`
（NAYE00004），三包各 **10,785,872** 字节，内嵌标题短哈希 **`8e310b1`**、`PLDMGR_VER:0.5.2.138`；
`frontend/dist/index.html` 917,991 字节，与 ELF 内嵌 blob 在偏移 **2,008,880** 逐字节一致；
旧版本 `0.5.2.137` 残留 0 处。`.137` 的两个包**别用**（`/game_title_sync` 断连，见上）。
真机（192.168.1.214）验收：`POST /game_title_sync` →
`{"ok":true,"scanned":8,"applied":8,"persisted":5,"failed":0}`，两次调用口径相同、
15 款面板名与 SMP `title_name` 全部一致；日志里 `启动自愈完成（回填 8 款：主机索引 8、
名字源 5、失败 0）` 与 40 条「本来就一致（app.db 写 0 行、appinfo 写 0 行）」
（后者证明 `td_count_differ` 短路生效、没有重复写事务）。

---

**`.139`（2026-10-07）存档功能从来没接上过 —— 根因是根目录扫错了。**
用户报「存档功能都没生效」，真机取证后确认**手动存档也一直是死的**，不只是自动存档。
`game_saves_root()` 只拼 `<uid>/savedata/<TID>`，而**真机 PS5 存档在
`<uid>/savedata_prospero/<TID>/`**；`savedata/` 下只有空的 `sce_backup2`/`sce_backup4`。
真机实测 `GET /game_saves?tid=PPSA01650` → `local_path:""`、`saves:[]`，而 FTP 上
`savedata_prospero/PPSA01650/` 明明躺着 4 个 `sdimg_*`（约 18 MB）。`game_saves_root()`
回 -1 → **列表 / 下载 / 上传 / 删除全部建立在它上面，整条链一起死**。
**为什么一直没发现**：`test/make_game_fixture.py`、`frontend/mock-server.js`、
`test/game_saves_test.c` 全都按错的布局造数据 —— 夹具把错的布局「证实」成了对的，
测试全绿而功能全死。**夹具必须跟着真机走，不能跟着代码走。**
路径规则取自用户点名的参考项目 garlic-savemgr（earthonion）的 `scan_saves`
（它同时扫这两个目录）；另外三条经也一并取了：每条存档带 `uid`、跳过 `.bin` 伴随文件、
`sdimg_sce_bu_*` 是备份。**它没有自动存档引擎**（纯手动五页签），那块没得抄。
粒度改成**一个用户一条**（一个 `<uid>/<TID>` 目录 = 一份完整存档，含 `_bu_` 备份与
PS4 的 `.bin` 密封密钥一起打包），API 的 `save` 参数从「槽名」变成**用户目录名**
（真机形如 `1a1d3627`）；老代码「取体积最大的那个」会把多用户拍扁成一个、互相盖，
真机上 `PPSA01650` 两个用户都有存档，所以现在返回列表、每用户一条、按 uid 升序稳定。
`kind` 改由命中目录决定，不再按 tid 前缀猜。顺带修掉两处：上传整份替换改成**可回退**
（旧的先 `rename` 成 `.old_*`，换成功才删 —— 原来 `rm -rf` 再 `rename`，中途失败等于
把用户存档删了）、临时目录挪到存档根父目录下（同文件系统 `rename`，不跨目录拷贝）。
测试：夹具整个重写为真机布局并逼出五条规则，`game_saves_test` **95 项 0 失败**，
`game112` **215/215**（原 214 + 新增「每行标出是哪个用户的存档」）。
**尚未做**（用户已确认要做，分后续版本）：自动存档执行引擎（`on_launch`/`on_exit`
目前仍只存配置不执行）、本地定时快照。已查明 **SDK 里 curl 8.18.0 没编 SMB**
（`strings -n 3` 才看得见 3 字符的协议名，用默认 `strings` 会误判 —— 第一遍就是这么
误判了 FTP 也不存在）。**注意这里当时把「libcurl 没有 SMB」错当成了「PS5 做不了 SMB」**：
2026-10-07 复核后确认 **SMB 可行**（`libsmb2` 是 pacbrew 正式包，已实测能交叉编译并
链进 payload，+241 KB），详见 §6 那条与 `include/save_remote.h` 的文件头。
`.142` 交付时**先做的是 FTP + HTTP**，而用户现有配置恰好是 SMB —— 那一档在 UI 上标
「当前构建未启用」。

---

**`.140`（2026-10-07）存档三层计划的第 2 层：本地定时快照。**
用户上一轮拍板的范围是「根因 + 引擎 + 本地快照」，`.139` 交付了根因，本轮交付第 2 层
（第 3 层「自动存档执行引擎 + 传输层」已由 `.142` 交付，见下）。设计上按用户的选择走：**配置跟着游戏走**
（存在 `GAME_SAVES_AUTO_PATH` 同一个 tid 对象里的 `snap` 键，与 `smb`/`ftp`/`http` 并排，
用户「对着这个游戏一起配」的心智模型不变），**能力 = 列表 + 一键还原 + 删除**。

落点 `/data/nayeplmtools/saves_snap/<TID>/<uid>/<YYYYmmdd-HHMMSS>/`。守护线程
（`game_saves_snap_start_daemon`，`main.c` 在名字自愈之后启动，safe_mode 整条跳过）
启动先等 90 秒避开启动期 IO，之后每 30 秒醒来，按**每款游戏自己的 interval** 判定是否到点。
「上一轮是什么时候」只记在内存里（`g_snap_marks`），重启后第一次会对所有开着的款各做一轮，
但去重会挡住「内容没变」的那些，所以不会产生重复快照。

三个刻意为之的设计（写在 `include/game_saves.h` 与 `src/game_saves.c` 的注释里）：

1. **内容没变就不新增**。判据是 `(文件数, 字节数, 最大 mtime)` 三元组，拿源目录与**最近一份
   快照**比。主机的存档绝大部分时间是同一份，按 30 分钟一拷的话一天就是 48 份一模一样的东西，
   白占用户的 `/data`。代价是「点立即备份没反应」—— 所以前端专门为它写了一句话
   （「存档没有变化，未新增快照」）并进了回归，这条最容易被当成 bug。
2. **源目录最近 20 秒内被写过就跳过本轮**（`GS_SNAP_QUIET_SEC`）。存档正写到一半时拷出来是
   撕裂的，还原回去可能让主机认不出来 —— 宁可等下一轮。
3. **复制到 `.tmp_*` 成功才 `rename` 成时间戳目录**。半截的快照永远不以正式名字出现。

复制保留 mtime（`utimes`）—— 这既是去重判据成立的前提，也让还原回主机后的时间线不变。
时间戳目录名固定 15 字符（`YYYYmmdd-HHMMSS`），**字典序 = 时间序**，排序/找最旧/找最新全都不用
解析时间。裁剪两道：每 `(tid,uid)` 只留最近 `keep` 份（`gs_snap_prune`），另有一道全局字节预算
`GAME_SAVES_SNAP_TOTAL_BYTES`（4 GB）从**全局最旧**开始淘汰（`gs_snap_evict_total`）——
只按 keep 裁的话，15 款 × 5 份 × 18 MB 就是 1.3 GB，主机 `/data` 顶不住。

**顺手把上传的整份替换抽成了 `gs_swap_dir()`**，快照还原与上传共用同一条「旧的先改名留一手、
失败退回去」的逻辑。两条路各写一份的话，早晚有一边忘了「退回去」——`.139` 刚修过这个。

接口只新增**一条** POST `/game_save_snap`（`action = run|restore|delete`），列表不另开 GET：
快照跟着 `/game_saves` 一起回（`snapshots[]` + `snap_used`），弹窗一次拉全。`auto.snap` 的
`interval`/`keep` 归一化（`gs_snap_clamp_*`）在**落盘、回吐、守护线程读**三处共用同一个函数 ——
分开写的话，UI 显示 5 份、守护线程按 50 份裁，是那种谁也发现不了的不一致。**这里有个坑**：
不能拿读端口的 `gs_get_port`（夹 1~65535）去读间隔，否则用户填「一天」（86400）会被判越界、
静默回落成默认 30 分钟 —— 专门加了 `gs_get_int` 并写了断言钉住。
`uid` 与 `ts` 是快照路径的最后两段，`gs_uid_ok` / `gs_snap_ts_ok` 在**拼路径之前**就拒。

测试：夹具没动（快照不依赖夹具新增内容，它复制的是已有存档），`game_saves_test` 从 95 项涨到
**136 项 0 失败**（新增：立即备份按用户数出份数 / 内容没变不新增 / 只改哪个用户就只新增哪个 /
keep 裁剪 / 一键还原且不牵连别的用户 / 删除且再删一次必须失败 / uid 与 ts 的路径穿越一律拒 /
86400 不被回落）；`game112` **232/232**（原 215 + 新增 17 条）。宿主测试新增
`-DGS_SNAP_QUIET_SEC=0`（夹具是刚造出来的，文件 mtime 就是「现在」，不置 0 的话每一份快照都会被
「正在被写」那条挡掉，整段等于没测）与 `-DGAME_SAVES_SNAP_DIR`。
**测试里踩过一个顺序坑**：把「删除快照」用例放在「只该新增 1 份」之前，删掉之后那个用户就没有
「最近一份」可比，下一轮会给他重新生成 —— 断言从 1 变成 2。删快照后守护线程补回来是**对的**
行为，所以改的是用例顺序，不是代码。

---

**`.141`（2026-10-07）快照一按就把面板打死 —— 栈溢出，第三次栽在同一个坑上。**

`.140` 推真机后 `GET /game_saves` 三款游戏全部正确，但一按「立即备份」
（`POST /game_save_snap {"tid":"PPSA27836","action":"run"}`）→ `curl exit 52`（空回复）
→ `/version` 变 **000**，面板整个没了。

**真机取证**（FTP，`/data/nayeplmtools/saves_snap/PPSA27836/1a1d3627/`）：
`.tmp_226_1791374835/` 建出来了**但是空的** —— 时间戳目录没出现（`rename` 没走到）、
临时目录里一个文件都没有（`gs_copy_file` 连目标文件都没建出来就死了）。源是
**一个 15 MB 的 `sdimg_SAVESERVICE-LINE-0-1Slot`**。
> 附带一条排查经验：`pldmgr.log` 里**没有** `Request: POST /game_save_snap` 这一行是
> **正常的** —— 那条 `Request: %s %s` 日志（`http_server.c:1196`）在 POST 分发块**之后**，
> POST 走的是 `gr_dispatch_post` 那条分支、提前 return 了。别拿它的缺席当证据。

**根因**：MHD 起的是 `MHD_USE_THREAD_PER_CONNECTION` —— 每条连接一条 pthread 线程，
栈走**真机 pthread 默认值**，而那个默认值只有 **64KB 上下**。这条路径原来是：

| 帧 | 字节 |
|---|---|
| `http_on_request` | 13,080 |
| `gr_dispatch_post` | 3,368 |
| `game_saves_snap_run_now`（`gs_root_t roots[16]`，一个 712 B） | 11,928 |
| `gs_snap_take` | 5,000 |
| `gs_copy_tree`（每层 `char sp[1024]+dp[1024]`） | 2,280 |
| **`gs_copy_file`（`char buf[32*1024]`）** | **32,888** |
| **合计** | **~55 KB** |

再叠上 MHD 自己那几帧就顶穿了。**量法**：宿主上把 `game_saves_snap_run_now` 放进一条
指定栈大小的 pthread 里跑，扫一遍栈大小 —— **≤56KB 一律 SIGSEGV(139)、≥64KB 才活**，
临界点非常干净。这个量法后来直接变成了回归用例（见下）。

**为什么宿主 136 项全绿**：宿主跑在主线程的 8MB 栈上，这点数组根本不构成压力。
**同一个坑 `.116` 已经栽过一次**（改磁贴图标/背景死机，`tt_apply` 里一个
`unsigned char[65536]`，见 §2.2.6），当时结论就是「别在栈上开大数组」，
但**只改了那一个函数、没留下任何守卫** —— 所以 `.141` 又踩了一遍。

**修复三件套**：

1. **这条路径上的大块内存全部走堆**。`gs_copy_file` 的 32KB 缓冲挪进 `gs_cp_t`
   （由调用方 `malloc` **一次**、整条链共用，顺带提到 64KB 少一半 syscall）；
   `gs_copy_tree` 的每层路径缓冲改成按 `depth` 从 `gs_cp_t.path` 里取槽位，
   **栈占用与递归深度彻底脱钩**（`GS_SNAP_MAX_DEPTH=24` 现在只是纯堆开销）；
   `gs_root_t roots[GS_MAX_ROOTS]`（11.4KB）在 5 个函数里各出现一次，新增
   `gs_roots_find()` 统一走堆。
   结果：整条链 **~55KB → ~22KB**，`gs_copy_file` 32,888 → **120**、
   `gs_copy_tree` 2,280 → **232**、`gs_snap_run_now` 11,928 → **536**、
   `gs_snap_main`（守护线程，原来 14,216）→ **2,840**。
2. **连接线程的栈一次性给足 1MB**（`MHD_OPTION_THREAD_STACK_SIZE`，`main.c`）。
   与其等第三次，不如把这一类问题整个消掉。**带回落**：万一这个 SDK 的 MHD 不认这个
   选项（`MHD_OPTION_THREAD_STACK_SIZE` 会让 `start_daemon` 直接返回 NULL = 面板起不来），
   去掉选项重试一次 —— 宁可退回默认栈也不能把服务弄没。
   **坑**：这个选项的参数类型是 `size_t`（MHD 那边 `va_arg(ap, size_t)`），
   传 `unsigned int` 在 x86-64 变参下高 32 位不保证是 0 —— 那就成了一个天文数字的栈大小，
   `start_daemon` 失败、正好落进上面那条回落分支，**这个优化静默失效、栈还是 64KB**。
   快照守护线程是 `pthread_create` 出来的、不受 MHD 这个选项管，在创建处单独
   `pthread_attr_setstacksize(1MB)`。
3. **两条守卫**（这才是这一轮最该留下的东西）：
   - `test/game_saves_test.c` ⑩：**在 32KB 线程栈上跑完整条快照链**（备份 + 列表 + 一键还原）。
     带负向对照 —— 拿 `.140` 的 `game_saves.c` 跑它**必 SIGSEGV(139)**；修好后 138 项 0 失败。
     这是真机那个 bug 的**宿主等价物**，不必再等真机反馈。
   - `tools/check_stack_frames.sh`（新）：任何函数栈帧不得超 **32KB**，已接进
     `test/run_host_tests.sh`。这是道**粗网**（抓「单个函数吃掉几十 KB」），
     精确的那道是上面那条小栈用例；没有 PS5 SDK 头就自己跳过。

顺手把 `game_saves_list_json` 里那句**错的注释**改掉了 —— 它写的是「线程栈是 pthread
默认值（**通常 512KB~1MB**）」，而真相是 **64KB 上下**。这个错误假设正是 32KB 缓冲上栈的
直接原因。**大块内存一律走堆，别再拿「默认栈很大」当理由。**

出包版 `NayePlmTools_0.5.2.141-me.elf` / `-ga.elf`（提交 `f1eb8b8`，内嵌标题 `f1eb8b8`），
两包各 **10,802,256** 字节、titleId 自检 NAYE00001/NAYE00004 通过，
`dist` 928,223 字节与两个 ELF 内嵌 blob 在偏移 **2,011,312** 逐字节一致，
`0.5.2.139` / `0.5.2.140` 残留各 0 处；`.140` 的包**别用**（快照一按就死）。

**`.142`（2026-10-07）存档第 3 层：自动存档执行引擎 + 远程推拉（FTP / HTTP）。**
在这之前 `saves_auto.json` 的 `enabled` / `on_launch` / `on_exit` **只被读写、没有任何人在跑** ——
界面上的三个开关是「存下来就再也没人看」的字段，远程那一段只用于「测试连接」。这一版补齐整条链。
四条硬约定（都写进了各自文件头，改之前先读）：

① **传输只做 FTP + HTTP**。SDK 那份 `libcurl 8.18.0` 编了 `http/https/ftp/file/dict/imap/ldap/pop3/smtp`，
**没有 `smb`** —— 而 `smb` 恰恰是老版本的默认值，等于每个新用户默认配了一个「存得下、传不出」的协议。
默认值前后端 + mock 一起改成 `ftp`；UI 上 SMB 那一档**看得见、点不动、写明原因**（不从选项表里删 ——
老用户存的就是 `mode:"smb"`，删了会变成「一个都没选中」，用户不知道发生了什么）。

② **远端布局 `<base>/<TID>/<uid>.zip`，推与拉逐字一致**。一个用户一份（真机实测一个游戏常有两个用户
各存一份），**不做「一个游戏一个文件」**；文件名**不带时间戳**、每次整份覆盖（要历史版本有本地快照兜着）。

③ **拉取时的用户枚举要能「本地已经没了」**。`game_saves_uids()` 靠「`<uid>/savedata_prospero/<TID>`
目录在不在」枚举，而拉取要救的恰恰是本地那份没了的情况（换机/误删/重装）——那时它必然是 0 条，
成了一条死路。所以 `game_auto.c` 在 `PULL` 且本地为空时退化成 `game_saves_user_ids()`
（主机上**全部用户目录**）逐个去远端试，远端没有的算**跳过**而不是失败。用户目录是主机账号，
不会因为删了游戏存档而消失。代价是多几次 404 —— 一次局域网往返，和「救不回存档」不是一个量级。

④ **同一时刻只允许一次同步**（`g_sync_mu`）。**这不是性能优化，是正确性要求**：「整份替换」是
「旧的改名 → 换上去 → 删旧的」，两次并发跑同一个目录会互相把对方刚改名的目录当成垃圾删掉。
守护线程（退出推送）与用户手点「立即推送」完全可能同时发生。同族的还有 `ga_do()` 的**判定顺序**：
协议与配置先判，否则配了 smb 的用户点推送只会看到「主机上还没有这个游戏的存档」，真因被无关信息盖住。

引擎另外三条刻意的设计（`include/game_auto.h` 文件头有完整理由）：**探测**用
`sceLncUtilGetAppIdOfRunningBigApp()` + `sceLncUtilGetAppTitleId()`（**不用** `/mnt/shadowmnt/<TID>`
挂载点判据 —— 那只对 ShadowMount 挂出来的游戏成立，内置 PKG 没有那个目录）；**第一轮只记状态不动作**
（面板可能玩到一半才起来，补一次拉取会用远程盖掉正在玩的存档）；**退出后先等 5 秒再推**，只对推送等。
`SR_ENOTFOUND (-2)` 把「远端还没有」与真失败分开 —— FTP 侧两种表现都要归进来：
`CURLE_REMOTE_FILE_NOT_FOUND`，以及 **`CURLE_REMOTE_ACCESS_DENIED`**（远端根目录不存在时
`CWD` 得到 550，libcurl 报的是后者）。

顺带修掉三个真问题：**① 存档弹窗的字段标签一直是写死的中文**（`MODE_FIELDS` 里的字面量），
英文包下「主机地址/端口/远程路径」整片漏中文 —— 回归当时只钉了 `game.secret_saved` 那条提示，
而它「碰巧」盖住了这个洞；默认协议换成 ftp 后那条提示不再渲染，断言变永假，洞才露出来。
**② mock 的 `/reset` 漏清 `snaps`**（`.140` 加快照时没同步改），也没关掉 `/loadpayload:` 起过的
10101 桩 —— 同一个 mock 进程跑第二遍 `game112` 必红。**③ 重复的 i18n 键是静默失败**：
新增 `game.f_path`（远程路径）撞上 `GameDetailModal` 已有的 `game.f_path`（物理路径），
JSON 与 i18next 都取最后一个，详情里那一行当场变成「远程路径」。存档字段键已挪进独立命名空间
`game.sf_*`，并加了一条静态守卫「**两份语言包都不许有重复键**」（`game112` 第 11 节）。

`pldmgr_curl_init()` **不能放 `utils.c`** —— 那会把 `curl_global_init` 带进 `utils.o`，
而 `utils.c` 被一大票**纯文件系统**的宿主测试链着（game_saves / media_scan / game_mgr），
它们立刻 `undefined reference`，等于把「存档的宿主测试不依赖 curl」这条设计从另一个方向拆掉。
收进 `src/curl_init.c`，谁用谁链。宿主测试要 curl 头时**只把 SDK 的 `curl/` 单独软链出来**
（`mkdir -p /tmp/pldmgr_curl_inc && ln -sfn /opt/ps5-payload-sdk/target/include/curl /tmp/pldmgr_curl_inc/curl`），
**绝不能直接 `-I` 整个 SDK include** —— 那底下有整套 FreeBSD 头，会把 glibc 的 `stdio.h` 盖掉，
链接期一片 `undefined reference to __error / __stderrp / _CurrentRuneLocale`。

出包版 `NayePlmTools_0.5.2.142-me.elf` / `-ga.elf`（提交 `c73ac9a`，内嵌标题 `c73ac9a`），
两包各 **10,818,640** 字节、titleId 自检 NAYE00001/NAYE00004 通过，
`dist` 932,607 字节与两个 ELF 内嵌 blob 逐字节一致，`0.5.2.139` / `.140` / `.141` 残留各 0 处。
宿主新增 `save_remote` 63 项 + `game_auto` 45 项（都是 0 失败），前端 `game112` 232 → 248 项。

**已推真机 192.168.1.214 验收通过**（`/version` = `0.5.2.142`）。两条新写的验收脚本
（`_ftp_probe/device142_sync.py` **18/18**、`_ftp_probe/device142_pull_restore.py` **13/13**）
在本机起一个极小 HTTP 桩当远端（PUT 存 / GET 取），端到端验了：
远端为空时 `pull` 回 **200 + note「远程还没有」**（`SR_ENOTFOUND` 不是失败）；
配 `smb` 时 `push` 回 **400 +「当前 SDK 不支持」**（不是「没有存档」）；
`push` 后远端真出现 `saves/<TID>/<uid>.zip`（15,728,894 B）；
**删掉本地存档再 `pull`** → 走 `game_saves_user_ids()` 兜底恢复，**恢复的文件 sha256 与原文件逐字节一致**。
每一步之后 `/version` 都还是 `0.5.2.142`。顺带复跑 `device141_snap.py` PASS、
`device129_detail.mjs` 29/29、`device127_badges.mjs` 17/17、`device125.mjs` PASS。

**`.143`（2026-10-07）SMB 接过 `libsmb2`——`.142` 那句「SDK 不支持 SMB」被推翻。**
`.142` 的推理是「SDK 那份 `libcurl` 没编 `smb` → SMB 做不了」，**错在把「libcurl 这条路不通」
当成了「PS5 做不了」**。PS5 上 SMB 走的是 `libsmb2`（pacbrew 的 `ps5-payload-libsmb2`，
上游 `ps5-payload-dev/websrv` 就在用），已用本仓库同一套工具链编成 `libsmb2.a` 并链进 ELF，
代价 +约 241 KB（空程序 110,736 B → 352,040 B）。后端：`src/save_remote.c`（+459）新增
`mode=="smb"` 分支（`smb2_open(O_CREAT)` / `smb2_write` / `smb2_fsync` / `smb2_unlink` /
`smb2_mkdir` / `smb2_rename`），`include/save_remote.h`（+50）补接口与文件头说明，
`Makefile`（+11）链 `third_party/libsmb2/libsmb2.a`（`build_ps5.sh` + `include/` +
`libsmb2-6.2.tar.gz` + `libsmb2.a`）。前端把 SMB 那一档**解禁**（`.142` 的「看得见、点不动」作废），
判据从「那一档标了 disabled」改成「认不认得出这个协议名」；`mock-server.js` 的 `/game_save_sync`
对 `smb` 与其余两档一视同仁（不再回 400）。宿主测试：`save_remote` 76 / `game_auto` 47。

**`.144`（2026-10-08）存档配置搬家：远程位置改到「设置」里配一次、全局生效；单个游戏只留「拉取 / 推送」。**
用户口径：「我刚刚把共享设置的功能在设置里增加了，把配置功能迁移过去，配置一个全局生效。
单个游戏包开启拉取和推送按钮。」**为什么要搬**：`.142` 之前**每一款游戏**的「存档管理」里都有
一整块远程位置配置（协议三选一 + host / 端口 / 共享名 / 子路径 / 账号 / 密码），而现实里所有游戏
用的是**同一台 NAS / 同一个 FTP** —— 同一份主机地址要在几十个游戏里各填一遍。远程位置本质上是
**跨游戏共享的那一份配置**，就该有一个全局的家；相对地，「自动存档开关 / 启动拉取 / 退出回传 /
本地快照间隔」是**每个游戏各自不同**的策略，仍归各自的「存档管理」。**一个控件只有一个家。**
前端：新增 `frontend/src/utils/saveConfig.js`（抽出 `MODES`/`MODE_FIELDS`/`FIELD_LABEL`/`EMPTY_CFG`/
`toLocalCfg`/`toRemoteCfg`，新增 `GLOBAL_KEY`/`loadGlobalCfg`/`normalizeCfg`/`isRemoteConfigured`/
`remoteOf`/`resolveRemoteForGame`/`remoteSummary`）；新增 `views/ArchiveConfigModal.jsx`
（设置页「存档配置」，管远程位置 + 测试连接，存主配置键 `ARCHIVE_GLOBAL_CONFIG` 的 JSON 串）；
`SaveManagerModal.jsx` 删掉远程位置整块编辑 UI，只留 ① 自动存档总开关 + 两时机 ② 立即拉取/推送 +
`auto_state` 状态行 ③ 本地快照，远程位置改成**只读摘要**（优先全局、否则回退本游戏已存，并标注
`（来自全局设置）`/`（本游戏已配置）`），保存时用 `resolveRemoteForGame` 把解析出的远程段合进
`POST /game_save_auto`（这才让「全局生效」真的落到后端）；`config` 逐层传下去
（`App → GameManageView → GameSection → SaveManagerModal`），`App` 进 `game_manage` 视图时补一次
`refreshConfig()`。mock 的 `/reset` 补清 `ARCHIVE_GLOBAL_CONFIG`。回归：`game112` **249/249**
（第 5 节按新形态重写并**断言旧 UI 已删干净**，新增 5d 节验「全局 → 单游戏生效」：全局配 HTTP →
摘要显示它 + 标注来源 + 保存后 per-game `mode` 真的变成 `http`）、`nav112` 20/20、`acct111` 23/23、
`avatar109` 19/19、`cropfix` 75/75、`pack125` 15/15。**本轮纯前端 / mock / 文档改动，后端 C 一行未动，
未出包、未推真机**（真机验收待做）。

**`.145`（2026-10-08）存档配置彻底收口：三块全部全局化（自动存档 + 本地快照 + 引擎侧全局优先）。**
用户口径：「把这三个功能都做成全局的」。`.144` 只把**远程位置**搬进了设置页，`.145` 把
**自动存档开关与时机**、**本地快照开关/间隔/保留**也一起搬了过去 —— 唯一配置入口 =
设置页「存档配置」（`views/ArchiveConfigModal.jsx`），单游戏「存档管理」退化成
**只读状态 + 动作按钮**（0 个 switch、0 个输入框、无「保存设置」）。**判据 = 「全局那份写过没有」**
（不是「填了几个字段」）：前端 `hasGlobalArchive()`、后端看 `ARCHIVE_GLOBAL_PATH` 在不在；
**写过 → 全局说了算，没写过 → per-game（老装机行为不变）**，四处同口径
（`resolveAutoForGame`/`resolveRemoteForGame` ↔ `ga_load_cfg()`/`gs_snap_cfg()`）。
全局配置落**单独一份文件** `/data/nayeplmtools/archive_global.json`（不能塞进
`nayeplmtools_config.txt`：行缓冲 256 B 会把 JSON 截断），`config_handle_set_json()` 取
`ARCHIVE_GLOBAL_CONFIG` **必须用 cJSON**（值是内容为 JSON 的 JSON 字符串，`strstr` 手扫会在
第一个内层引号处截断）。快照枚举在全局模式下改成**扫存档目录**（`gs_snap_enabled_games()` +
`gs_all_tids()`），否则全局开了也一个都扫不到。`src/game_saves.c` / `src/game_auto.c` 的
`gs_snap_cfg()` / `ga_load_cfg()` 都加了「全局优先」。宿主测试新增替身
`test/stub_archive_global.c`（只为补 `config_read_archive_global` 一个符号，默认「没有全局配置」
= `.144` 老路径，几十条既有用例一条不用改）。提交 `3e46fa7`，出包版
`NayePlmTools_0.5.2.145-me.elf` / `-ga.elf`（两包各 10,999,176 字节），**未推真机**。

**`.146`（2026-10-08）SMB 配置项优化 + FTP/HTTP 暂时禁用 + 「远程游戏」标签显示真实远程信息。**
① SMB 补「端口」字段、字段顺序改成 host→port→user→password→share→sub、修正后端落盘；
② 协议选择器**只留 SMB**，FTP/HTTP 悬停提示「后续开发，暂不可用」，默认协议 `ftp` → `smb`
（`saveConfig.js` 的 `MODES` / `EMPTY_CFG.mode` / `remoteOf` / `toLocalCfg` 四处）；
③ 「远程游戏」标签显示真实远程信息（方案 A）：推送时一并写 `<uid>.meta.json`，新增只读接口
`GET /game_save_remote_info`（`ROUTE_GAME_SAVE_REMOTE_INFO`），前端逐用户展示
用户 · PS5 · 大小 · 文件数 · 时间；FTP/HTTP 的 `sr_stat` 仍为 stub（属「暂不可用」范围）；
④ 「存档管理」弹窗三板块并排（「自动存档」「远程位置」「远程存档」各占 1/3）；
⑤ 编译修复：`include/save_remote.h` 补 `#include <time.h>`（`sr_stat` 声明用 `time_t`，
且 libsmb2 要求先包含）。
⚠️ **`.146` 的改动只暂存、从未提交**（HEAD 一直停在 `.145` 的 `3e46fa7`），
且它留下两处会让套件变红的尾巴 —— 都在 `.147` 一并清掉了（见下）。`MENU_VERSION` 曾 bump 到
`.146` 并出过 ELF，但那不是一次干净的发布。

**`.147`（2026-10-08）存档弹窗版式收尾 + 远程推/拉的屏幕通知 + 出包前清账。**
① **第一行三栏重排 + 三栏等高**：第三栏由「远程存档」换成「存档列表」，「远程存档」并进
「远程位置」卡；去掉 `.146` 的 `items-start`（回默认 `items-stretch`）→ 三张卡跟着**最高**那张
一起长、底边对齐（用户口径：「不管哪个变高，另外两个同步变高」）。本地快照一行未碰。
② **远程推 / 拉的「开始 / 结束」各弹一条 PS5 屏幕通知 + 各打一条日志**（用户口径「到屏幕信息」，
指的是 `pldmgr_notify()` 那个系统通知弹窗，与 `main.c` 的 `Ready!` 同一个，**不是**面板「日志」页）。
做法：`src/game_auto.c` 的 `ga_do()` 拆成**包装层 + `ga_do_inner()`** —— 包装层只做四件事
（读一次配置 → 发「开始」→ 调 inner → 发「结束」），于是「开始/结束」**天然成对，失败提前返回
也照弹**（放进 inner 里必然出现「有结束没开始」）。新增 `ga_target_desc()`（**不含密码**，
口径与前端 `remoteSummary()` 逐字一致）。计时用 `CLOCK_MONOTONIC`。**推送不报文件数**
（`sr_push()` 不填 `files`，照拉取那样打就是「0 个文件 / 2648 字节」这种自相矛盾的一行）。
③ **撤掉「自动存档」卡里那颗「远程游戏」徽标**，并**连带撤掉为它让位的那层 `md:grid-cols-2`**
—— 徽标一走，左右分栏只剩副作用（三行状态被压到半宽）。顺带修掉了 1280–1660px 窄窗下
「未启用」一个字一行的老问题（根因就是那层内层拆栏）。死键 `game.remote_game` /
`game.remote_game_hint` 一并从两份语言包删掉。
④ **出包前清账（两处都是 `.146` WIP 遗留，不是本轮新引入）**：
  - **mock 的 `globalArchive()` 在「没配过」时返回了一份假的全局 SMB 配置** → 改回 `null`。
    它**违背 `.145` 的核心契约**（没写过必须回落 per-game），`autoFor()` 会把全局盖在 per-game 上，
    mock 里**永远走不到回落分支** → `game112` 两条断言假红（远程摘要渲染成假全局的 SMB 而不是
    per-game 的 FTP；快照间隔读成假全局里的 3600 秒 = 60 分钟，而后端默认是
    `GAME_SAVES_SNAP_INTERVAL_DEF = 1800`）。改回 `null` 后 **272/272 且断言一行没改** ——
    反过来证明是 mock 说谎、不是契约变了。「远程游戏」区块的数据来自硬编码的
    `/game_save_remote_info`，不吃 `globalArchive()`，所以预览能力没有损失。
  - **`game_saves` 关卡 `undefined reference to 'sr_stat'`** → 新增 `test/stub_sr_stat.c` 补符号。
    **不选「把 `save_remote.c` + `-lcurl` 加进这一关」**：那会让这一关从「只依赖 python3」
    变成「没有 libcurl 就整段 SKIPPED」，白丢一层覆盖；而本关没有任何用例碰远程元数据。
    替身**故意不实现任何成功路径**（一律 -1 + 人话 + stderr 一行提示），不会静默给假数据。
    同族做法见 `test/stub_archive_global.c`。
**验证**：`run_host_tests.sh` **`overall: PASS`**（`game_saves` 142/0、`save_remote` 76/0、
`game_auto` **82/0**（第 ⑥ 节 `t_log()` 新增 37 条，`pldmgr_log` 与 `pldmgr_notify` **两个桩都抓**）、
`media_scan` 178/0、`game_mgr` 100/0、`check_post_routes` 13==13、`check_stack_frames` 全过）；
`game112` **272/272**；PS5 工具链单编 `game_auto.c` 通过。**未推真机**。

**`.148` / `.149` / `.150` / `.151` / `.152` / `.153`（2026-10-08）存档远程同步的六连修 —— 一次真机事故一轮。**
详细口径分别见 **§2.1.4**（zip 的 DOS 时间戳必须写读互逆）、**§2.1.5**（整份替换不许换掉
存档目录本身）、**§2.1.6**（拉取前冻住游戏、拉完放行 —— 含 `.151` 的「恢复不许再解析
app_id」、`.152` 的「LncUtil 换 SIGSTOP」与 `.153` 的「验证不可用 ≠ 放弃冻结」）。
一句话串起来：`.148` 修「拉一次日期退一个月」；`.149` 修「游戏启动时拉取
提示成功、但游戏里还是老存档」（目录 inode 被换掉，**必然失效**）；`.150` 修 `.149`
之后剩下的**「偶尔成功、偶尔失败」**（游戏在拉取完成前就把旧存档读进了内存 —— 竞态），
做法是拉取前挂起正在启动的那款游戏、拉完 `sceLncUtilResumeApp` 放行，同时把扫描周期
`GA_TICK` 由 2 秒改成 1 秒，并把 `ga_tick` 导出成 `game_auto_tick()` 让宿主能把**顺序**
钉死（+ 变异对照 `test/mutation_check_freeze.sh`）；`.151` 修 `.150` 真机首轮暴露的
「**恢复失败**」—— 挂起后 `GetAppIdOfRunningBigApp()` 返回 `0xffffffff`，恢复时再解析
app_id 必然失败，必须用**挂起时记下的那个**；`.152` 修 `.151` 之后暴露的
「**恢复成功但游戏留在后台、必须手点才回前台**」—— 探针证伪六个「切回前台」候选 API
（`SetAppFocus`/`SetControllerFocus` 返回 0 却无效、`ContinueApp` EBUSY），
**改成进程级 `SIGSTOP`**：游戏压根不离开前台，「切回前台」问题不存在；
`.153` 修 `.152` 真机首轮暴露的「**SIGSTOP 主方案压根没跑起来**」——
`KERN_PROC_PID` 单查在真机上失败 → 主方案在发信号前就放弃 → 回落 LncUtil → 游戏又跳后台。
出包版 `NayePlmTools_0.5.2.153-me.elf` / `-ga.elf`（`.153` 已真机验收通过）。

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
- `game_mgr.c` 游戏磁贴扫描、格式探测（直读 `mount.lnk`/`mount_img.lnk`）与来源分类（`source_type` 只看物理来源，判据见 §2.1.2）、deeplink 枚举（app.db 列序按建表 SQL 实时定位）；`thermal.c` 温控只读状态；`fan_control.c` 风扇温控（模型 A + 常驻守护线程）。
- `quicklinks.c` 常用入口探活 + app.db 磁贴链接按需同步（stat+IP 闸门 + `src`/`hidden` 字段；收录时 127.0.0.1 改写为本机 LAN IP）；`history_mgr.c` 会话历史；`notification.c` 启动通知；`log_server.c` 日志；`config.c` 配置持久化；`sha256.c`/`json_helpers.c`/`utils.c` 基础设施。
- `src/psaccounts/`（`.99` 并入）账号工具：注册表 16 槽扫描、离线激活、NP 假登录（auth.dat/config.dat）、头像写回、安全备份台账、`libSceRemoteplay` 配对 PIN（单会话 + 看门狗线程，退出前 `accounts_pin_shutdown()`）。路由全部在 `http_server.c` 的 `/api/*` 分支，**串行化 + con_cls 所有权**两条约定见 §0 的 `.99` 条目。
- **铁律**：fork 不改 C 后端的路由/配置键/文件路径，保证升级上游时行为一致。（例外：`.99` 新增的 `/api/*` 账号路由，纯增量，不动上游路由。）

#### 2.1.0 多媒体管理后端（`.115`，游戏管理 → 截图 / 录屏）

- **契约在前端**：`frontend/src/utils/gameApi.js` 文件头是唯一事实来源，后端照它实现；
  实现侧的存储布局 / 铁律 / §7 待验证项写在 `include/game_media.h` 头注释，两边是同一份。
- **八条路由**（宏集中在 `include/pldmgr.h`，分发在 `http_server.c`）：

  | 路由 | 形态 | 说明 |
  |---|---|---|
  | `GET /game_media?tid=` | → JSON | `{tid, items[], count, truncated?}` |
  | `GET /game_media_thumb?id=&w=320` | → JPEG | 系统缩略图 → 盒式平均缩放 → 落盘缓存 |
  | `GET /game_media_file?id=` | → 二进制 | **fd 直出** `MHD_create_response_from_fd64` |
  | `POST /game_media_pack {tid,ids}` | → `{job_id}` | 走 `PostStatus`，体上限 512KB，409=`EBUSY` |
  | `GET /game_media_pack_get?job=` | → zip | fd 直出 |
  | `GET /game_media_delete?tid=&id=` | → `{ok,message}` | 逻辑失败回 **404 带 message**，前端 toast 吃这个 |
  | `GET /game_job_status?job=` | → JSON | 1s 一次轮询，已在 `is_noisy_route` 里降噪 |
  | `GET /game_job_cancel?job=` | → `{ok,reason}` | |

- **`id` 就是 `content_path`**（如 `/av_contents/photo/NPXS40087/PPSA99008/18c/xxx.png`），**不是 rowid**。
- **数据源 `/system_data/priv/mms/av_content.db` 的 `tbl_PhotoVideo`，只读整读**（镜像缓存按
  size+mtime 判失效、上限 64MB、非 WAL、页 4096）；**列序号按建表 SQL 现数，不写死** ——
  `width`/`icon_width`、`duration`/`duration_ms`、`content_path` 是三对前缀撞车，两头都要挡。
- **铁律①：绝不写 `av_content.db`。** SDK 没有 sqlite3，仓库那套极简解析器只会读 b-tree。
  删除 = 只删文件（原图 + 系统缩略图 + `.meta` + `.dat` + 本地缩略图缓存）。删完我们自己的
  列表靠 `stat` 认出「盘上没了」从而不再列出 —— 但**系统媒体库会不会留下幽灵条目未验证**（见 §7）。
- **铁律②：大文件绝不进内存。** 录屏几百 MB —— 原图 / 录屏 / zip 一律把 fd 交给 MHD
  （MHD 负责 `close`；**response 创建失败要自己关 fd**，否则漏一个 fd）。只有缩略图走内存。
- **打包 = 自研 STORE zip**：SDK 的 `target/lib` 没有 zlib。EOCD64 + locator **永远写**；
  本地头的 CRC 先写 0 占位、数据流完 `lseek` 回填，**源文件只读一遍**；条目名 = `content_path`
  去掉 `/av_contents/`。产出的 CRC 与内容由 `test/media_verify.py` 用 Python `zipfile` 逐字节验。
- **打包是单 worker 任务**，与安装/删除任务同构（同一时刻只跑一个，冲突回 409 `EBUSY`）。
- **路径白名单是唯一的越权闸门**：`md_path_ok`（必须以 `/av_contents/` 开头、无 `..`、无 `//`、
  无反斜杠、段数 4..9）→ `md_abs`（拼 `GAME_MEDIA_USER_PREFIX`，并要求最终落在四个媒体根之下）。
  **任何新增「按 id 取/删文件」的入口都必须先过这两步**，否则就是一条任意文件读写。
- **五个路径宏可 `-D` 覆写**（`GAME_MEDIA_ROOT` / `DB_PATH` / `THUMB_DIR` / `PACK_DIR` /
  `USER_PREFIX`），主机回归测试正是靠它们把整套逻辑指到 `/tmp` 的夹具上。
- **回归测试（改这块必跑）**：`test/media_scan_test.c`（98 项断言）+ `test/media_verify.py`
  （zip CRC / 条目 / 逐字节内容 + `list.json` 可解析性），入口仍是 `test/run_host_tests.sh`。

#### 2.1.1 账号工具的三个概念（`.107` 定稿 / `.111` 改口径，改之前必读）

**离线激活 → 登入 → 登出，是三个独立的状态，不是一件事。**

| 动作 | 写什么 | 来源标记 | 界面按钮 |
|---|---|---|---|
| 离线激活 | 注册表 3 键：`account_id` / `type="np"` / `flags` | `SAFETY_ORIGIN_ACTIVATE` | 激活（**激活成功后整颗收起**，`.111`） |
| 登入 | 22 个注册表键 + `auth.dat` + `config.dat` | `SAFETY_ORIGIN_SIGNIN` | 登入 |
| 登出 | 按备份日志逐键回写原值（+ 回灌两个 NP 文件） | 按掩码筛 | 登出（只登出） |
| ~~反激活~~ | 同上，但不分来源全退 | 按掩码筛 | **`.111` 已从界面撤掉**（服务端动作保留，见下） |

- **顺序是硬的**：`npsignin_signin()` 开头 `if(slot == 0 || account_id == 0)` → 报「该账号还没有激活，请先点「激活」再登入」。没激活就登不进去。激活 = 发身份证，登入 = 把身份证交给系统认。
- **`safety` 层按「scope(uid) + origin(来源)」两级筛**。`safety_restore_scope_mask(uid, mask)` 才是唯一入口，`safety_restore_scope()` 是 `mask = M_ALL` 的别名。
  - `SAFETY_ORIGIN_M_SIGNOUT_ONLY = M_NONE | M_SIGNIN` → **「登出」**：只退登录态，激活留着。这是 `.107` 的行为变更（以前登出 = 全退）。
  - `SAFETY_ORIGIN_M_ALL` → **「完全还原」**：不分来源全退，回到未激活。`.111` 起**界面上没有按账号触发它的按钮了** —— 只剩 `/api/safety/restore` 的全量还原（`accounts_safety_json("restore")`），那是不分账号的一键清账。
  - **`NONE` 必须被两种还原都覆盖**：它是老日志（本字段出现之前记的）和"调用方没设来源"的取值。宁可多回滚（回滚 = 写回原值，只会更接近原始状态），也绝不能因为格式换代把原值丢掉 —— 那等于永久失去还原能力。`reg.idx` 第 6 字段缺失或越界都降级成 `NONE`。
- **真实重叠（别踩）**：槽 1 的 `KEY_FLAGS`(125831168) **就是** `REG_KEY_FLAG_2` = `LOGIN_FLAG`，`KEY_ID`/`KEY_TYPE` 同理。所以激活和登入会写同一批键 —— `safety_backup_reg()` 的 `find_rec()` 去重会保留**最早**那份原值，记录的 origin 也就归**最早那次写入**（`ACTIVATE`）。这正是「仅登出」能保住 `account_id` 的原因。
- **改名（`RENAME`）**单独一类：退掉登录不该把用户改过的名字弹回去，所以「登出」不退它，只有全量还原才退。

##### `.111` 的两条硬规则（都栽过一次，别再犯）

- **① 槽位一律按 uid 解，名字只作兜底 —— 这是「第一次登出失败、第二次才行」的正解。**
  - 现象：`fill_status()` 原来**只按名字**找槽位（`find_slot()`），而名字来自 `sceUserServiceGetUserName()`。UserService 对一个**刚激活、还没进过壳层用户列表**的账号会滞后一帧（首帧取到空串或旧名）→ `find_slot()` 返回 0 → `out->slot = 0` → 登出走「没有对应的注册表槽位，无法登出」**直接失败**。第二次点击时 UserService 已经跟上，于是成功。
  - 修法：`fill_status()` = **`find_slot_by_uid()` 优先、`find_slot()` 兜底**，并把槽里的真名回填进 `out->user_name`。
  - **为什么不能反过来（名字优先、uid 兜底）**：名字是**全局唯一**的。万一 UserService 返回的是**另一个槽**的名字，名字路径会理直气壮地指向那一格 —— 那比「找不到槽位」更糟（会去读写别人的身份）。uid 只可能命中它自己那一格。`test/slot_resolve_test.c` 的 C 组就是钉这条。
  - 相关常量：槽基址 `125829120`(0x07800000)、步长 `65536`；`+0x100` = uid（`REG_KEY_USER_ID`）、`+0x200` = name（`REG_KEY_USERNAME`，STR17）、`+0x500` = account_id、`+0x800` = login_flag。
- **② 账号 ID 由 uid 派生，不许由名字派生。**
  - 用户口径：「用户ID生成后，只有修改账号ID才修改，改名是不修改ID的。」
  - 旧做法 `offact_gen_id(name)`（按名字算 FNV-1a）→ 名字一变 ID 就变，界面上那一格跟着跳。
  - 现在：`offact_gen_id_for_user(uint32_t uid)`；`offact_activate()` **不再自己派生**（`if(!id) return -1`），默认值一律由 `accounts_action` 显式算好传下去 —— 于是「界面预览的 `derivedId`」与「激活时真写进注册表的 id」用的是**同一个函数、同一个 uid**，永远一致。
  - **回归测试**：`test/offact_genid_test.c`（13 条，含「预览 == 实际写入」与「改名后 derivedId 一字不变」两条硬判据）。
- **回归测试（改这块必跑）**：`test/safety_origin_test.c`（39 条）+ `test/slot_resolve_test.c`（23 条，专打上面①）+ `acct111.mjs`（界面与接口契约，23 条）。

#### 2.1.2 游戏管理后端补完（`.116`，19 条端点 + 六个模块）

契约是 `frontend/src/utils/gameApi.js` 的**文件头**（`.112` 那轮只做前端时把它写死的），
后端照它实现；`frontend/mock-server.js` 是可执行的形状参考（响应字段、body 上限、
状态码都从它抄）。**前端本轮一行没改**（lint 仍是基线 28 error）。

| 模块 | 负责 | 落点 |
|---|---|---|
| `game_title.c` | 改名 + 磁贴两层图（1:1 图标 PNG / 16:9 背景 PNG→1920×1080 DXT1 DDS，另出 PNG 预览与 DDS 原字节两路） | `POST /game_rename`、`/game_tile_icon`、`/game_tile_background`、`*_reset`、`/game_tile_bg`、`/game_tile_bg_dds` |
| `game_saves.c` | 存档六条（列表 / 自动备份配置 / 连通测试 / 下载 zip / 上传 / 删除） | `GET /game_saves`、`POST /game_save_auto|test|upload`、`GET /game_save_download|delete` |
| `game_runtime.c` | 运行期三条 | `POST /game_mount|unmount|uninstall` |
| `game_job.c` | **任务槽本体**（打包与删除共用一个槽）+ 进度 / 取消 / CRC32 | `/game_job_status`、`/game_job_cancel` |
| `zip_io.c` | zip 编码器（媒体打包与存档下载**共用同一份**） | — |
| `inflate.c` | 存档上传解压（无 zlib） | — |

- **两个槽位共用一条路由**：`/game_job_status` 与 `/game_job_cancel` 对打包和删除
  一视同仁；但 `game_media_pack_open()` **必须按 `kind` 过滤** —— 删除任务的 id 同样
  查得到，它的 `aux` 装的是待删源路径而不是 zip，不挡这道就能拿删除任务的 id 把
  `aux` 当文件路径 open。
- **改名/换图必须 `game_mgr_invalidate()`**：`/game_list` 有 60s 快照，不失效的话
  刚设的图标会被吞一分钟。**读路径故意不 invalidate**（会自锁）。
- **`sceAppInstUtilAppUnInstall(const char *)` 是单参**：本仓库自己的
  `app_installer.c:52` 与 flatz / ps4_remote_pkg_installer 都这么调；符号在
  `libSceAppInstUtil.so`，**真实 `make` 链接通过**。
- **body 上限对着 mock 的 `express.raw({limit:…})`**：存档包 512MB、磁贴 PNG 64MB、
  JSON 1MB（`gr_post_limit()`）。前端能过、后端拒 → 用户只看到「请求失败」。
- **19 条端点做过一轮「参数取自哪一侧」的对照审计**（方法/路径/状态码/字段名 +
  tid/save/job 到底在 query 还是 body），方法是把 `gameApi.js` 的 29 处调用与
  `mock-server.js` 的 handler 并排读。**踩到两个坑**：`/game_save_auto` 的 tid
  原来从 query 取（前端塞在 body 里 → 必 400）；`auto.http` 只回 `token_set`，
  而前端按 `has_${f.key}` 拼提示，读不到 `has_token` 就永远不显示「已保存过」。
  **以后加端点先查这一侧** —— 这类错编译期看不出来，只有真机点一下才会暴露。
  审计后又追加了第三条：`port` 后端只认 JSON 数字，而前端 `<input>` 出来的
  **永远是字符串**（`inputMode` 只是键盘提示），于是端口从不落盘、「测试连接」
  每次都打到默认的 21。
  **现在有了回归覆盖**：`.116` 新增 `test/game_saves_test.c`（88 项断言，
  `test/run_host_tests.sh` 拨起，夹具 `test/make_game_fixture.py`），
  把 port 的双认、`has_token`、根目录取体积最大的命中、递归计数、
  **明文不外泄**、打包/上传/删除一起钉住。
  **仍然测不到的是「参数取自 query 还是 body」** —— 那一层要 microhttpd，
  主机上没有；加新端点时只能照上面那套方法把 `gameApi.js` 和 `mock-server.js`
  并排读一遍。

##### 来源分类（`source_type`）的判据（`.127`）

`/game_detail` 的 `source_type` **只看物理来源**，由 `gm_class_of_fmt(a.d_fmt)` 派生，
**绝不能用 `a.d_src`** —— `d_src` 是「这行从哪个枚举来源来」（app.db / 扫描路径），
而 ShadowMount 挂出来的镜像与文件夹游戏**同样会注册进 app.db**，`d_src` 也是 `"installed"`，
一用就把真源全打成「已安装 PKG」（`.127` 的 bug 原样）。

| `fmt` | `source_type` | `installed_pkg` | `can_manage_source` | `path` | `runtime_path` |
|---|---|---|---|---|---|
| `pkg` | `pkg` | `true` | `false` | `/user/app/<TID>` | `""` |
| `folder` / `lz4` | `folder` | `false` | `true` | `mount.lnk` 首行 → 扫描路径 → `/user/app/<TID>` | 源目录 |
| `ffpkg`/`exfat`/`ffpfs`/`ffpfsc` | `image` | `false` | `true` | `mount_img.lnk` 首行 | `/mnt/shadowmnt/<TID>_xxxxxxxx` |

- `image_type` **只在 image 类非空**，取 `a.d_fmt` 本身（即扩展名词表，用户口径「格式没错」）。
- `mounted` = `stat("/system_ex/app/<TID>/sce_sys/param.json")` 存在（**不是** `runtime_path` 非空 ——
  文件夹类游戏的源目录永远存在，那样判等于恒真）。规则与 ShadowMountPlus 1.7beta4 一致。
- `gm_paths()` / `game_mgr_source()` / `game_runtime.c` 的 `/game_delete` EPERM 闸门
  （`src=="pkg"` 才拦）全部读同一个 `cls`，别各写一套。
- **判据来源**：设备 `192.168.1.214:10101` 就是 ShadowMountPlus 的 API（POST + JSON body）。
  要对齐 SMP 就打它的 `games/info`，别照文档猜。
- **两个绝对根是编译期宏**：`GAME_MGR_APP_BASE` / `GAME_MGR_SYSTEM_EX`（默认 `/user/app`、`/system_ex`），
  主机测试用 `-D` 指到 `/tmp/pldmgr_gm_test/` 下。**以后写任何依赖这两个路径的主机测试都要带上 `-D`。**

##### `gr_file_response()` —— 文件直出为什么不走 `MHD_create_response_from_fd64`

**现象**（用户实测）：多媒体**原图预览只画出图片最顶上一条**、**打包 zip 下载失败**。

**排查链**：同一进程里走 `MHD_create_response_from_buffer` 的全好（缩略图、所有 JSON），
走 `MHD_create_response_from_callback` 的也全好（`/events` 的 SSE 流真机上一直是好的），
**唯独 fd 直出那两条出事** —— 正好就是用户报的这两个。`Content-Length` 照 `fstat` 报，
字节却少一截：图片解码到断点就停（只剩顶部一条），zip 少尾（下载判失败）。
`libmicrohttpd.a` 里导出了 `MHD_send_sendfile_`，说明 fd 直出会落到内核 `sendfile()`
上；MHD 自己的文档写着，构建没开 `MHD_FEATURE_RESPONSES_SHARED_FD` 时
「同一条响应并发可能把错误数据发给客户端」。

**改法**：`gr_file_response(size, fd)` = 一个 32KB 块的 `MHD_create_response_from_callback`，
回调里自己 `lseek(pos) + read()`，整条 `sendfile` 路径绕开；**fd 所有权在 free 回调里关**，
创建失败时回调不会被调用，必须自己收尸。换掉的只是「谁来搬字节」，不是「搬多少」
—— 几百 MB 的录屏照样不进堆。三个调用点：`/game_media_file`、`/game_media_pack_get`、
`reply_file()`（存档下载 / 磁贴图标预览 / 背景预览 / DDS）。
**回归顺序：出包后第一件事就是验这两条**（见 §7）。

#### 2.1.3 `game_mgr` 的主机测试（`.117`，写新测试前先读这个）

`.116` 出去之后真机报「游戏管理的列表变空了」，而 `make` / 主机关卡 / `npm run lint`
**三道门全绿** —— 因为 `src/game_mgr.c` 根本编不进主机测试（`getmntinfo()` /
`MNT_NOWAIT` / `struct statfs::f_mntonname` 三个 BSD 专有符号）。游戏管理的核心
（列表 + 详情）在 `.117` 之前**一条断言都没有**，这才是真正要补的洞。

**手法：功能代码一个字不改。**
- `test/host_mnt_compat.h` —— `run_host_tests.sh` 里 `-include`，**只在主机生效**
  （真机构建不带这个参数）。顶掉那三个符号，`getmntinfo` 恒返回 0 条挂载。
  ⚠️ 这个头的注释里**绝不能出现 `*/`** —— 写成 `<TID>_*/app0` 会把块注释提前结束，
  后半段全变代码，报出来的是一堆「unknown type name」，极难联想到是注释问题。
- `include/game_mgr.h` 的 `GAME_MGR_APPDB_PATH` 加 `#ifndef` —— 与 `game_media.h` /
  `game_saves.h` 同一手法，**默认值一字未改**，别去掉守卫。
- `test/make_appdb_fixture.py` —— 现造 `tbl_contentinfo`。列序是硬约束：
  `col0 = titleId`（恰好 9 位 + 过 `gm_tid_is_game`）、`col2 = titleName`；
  另 5 列按 `GM_DB_COLNAMES` 的**列名**给（`gm_db_row` 按名现查，顺序无所谓）。

**`seed_stack()` 是复现条件本身，不是花架子。** 「列表空掉」的根因就是栈上垃圾，
不播种等于指望进程刚起来那点干净栈碰巧复现。它把接下来那次调用要占的栈
（8 KB/层，`enum_arg_t` 自己就有 6.7 KB）写成指向 `"SEEDSEED1"` 的指针 ——
**已验证有效**：拿掉 `build_game_json` 里的 `memset`，65 项里 8 条立刻红、
全部 `count=0 items=0`（正是真机症状）；加回去则全绿。
调 `game_mgr_list_json` 前**必须先 `game_mgr_invalidate()`**，否则读的是 60s 快照，
断言恒绿等于没测。

**这一轮顺带照出来的两个真 bug**（都由 `.116` 引入，主机上一条断言都没有）：
`st_cb_row` 拿 `GM_DB_*` **枚举序号**当列号用（把 `gm_sql_col_index` 现查出来的
列序整个丢掉 → 详情页 `content_id`/`install_time`/`last_launch`/`appdb_size`/
`can_uninstall` 五格全错位）、以及 `version[40]` 没有 `param.sfo` 时吐栈上垃圾。
**教训：只断言「字段存在」抓不住读错列** —— 错位了字段照样在，只是值来自另一列。
那四条必须按**夹具里写死的值**断言。

#### 2.1.4 zip 的 DOS 时间戳必须写读互逆（`.148`，动 `zip_io.c` 前必读）

`.147` 上真机报「远程拉取显示成功，但存档不对」。查下来**存档内容一个字节都没错**
（整条链 sha256 全程 `5b531b74…`），错的是**存档文件的 mtime**：每跑一轮「推送 → 拉取」
就往回退**一个月**，而且是**累积**的（10-08 → 09-08 → 08-08）。

**根因：`src/zip_io.c` 的 `zx_dos_time()` 月份 off-by-one。**
`struct tm.tm_mon` 是 **0-based**，DOS 日期字段的月份是 **1-based（1~12）**。写侧原来直接
`tmv.tm_mon << 5`，读侧（`zip_extract`）按标准做 `tm_mon = 字段 - 1` —— **写读不互逆**。
- 10 月（`tm_mon=9`）在包里变成月份字段 9 = **9 月**；11 月写成 10 月；1 月（`tm_mon=0`）
  干脆写成「**0 月**」（读回来 `tm_mon = -1`，`mktime` 规整成上一年 12 月）。
- **为什么「只错日期」会变成「存档不对」**：拉取落盘链是
  `sr_finish_pull()` → `game_saves_import_file()` → `gs_upload_buf()` → `zip_extract(gs_up_cb)`，
  而 `gs_up_cb()` 里 `gs_up_set_mtime()` 会把**包内条目的 mtime `utimes()` 到刚解出来的
  存档文件上**（`src/game_saves.c`）。所以错月份是**真的写进了主机文件系统**，不是显示问题。
- **修法：只改写侧**（`(tmv.tm_mon + 1) << 5`）。读侧的 `- 1` 是**对的**（标准就是 1-based），
  **别把这条「修反」**。兜底日期也从 `0x21`（月 0）改成合法的 `1980-01-01` 编码。
- **媒体包走同一个编码器**（`md_dos_time` 早已并进 `zip_io.c`），所以导出的媒体包里日期
  也一样错 —— 这一条一起修好了。对 Windows 资源管理器 / 7-Zip / Python `zipfile` 一视同仁地错。

**守卫（两层，都要留着）**
- `test/zip_io_test.c`（37 checks，**纯 C、不需要 python3，所以永远不会 SKIP**）：
  10 月主用例 / 1·2·11·12 月边界 / **负向对照**（包内月份字段必须 `!= tm_mon`）/ 多条目 /
  1980 下限。它直接读**本地头偏移 12 的原始 DOS 日期位**（不解包），测的是编码器本身。
- 独立第三方交叉验证（`_cache/zipmonth_check.py`，一次性探针）：把我们打的包交给
  **Python `zipfile`** 读回来，与宿主 `mtime` 逐秒比对；同时编一个**变异版**（`sed` 掉 `+ 1`）
  做对照 —— 修复版必等、变异版必差一个月。**自测读写两边都是我们自己的代码，写读一起错
  也能自洽全绿，所以第三方实现才是真判据。**
- 变异对照实测：`sed` 掉 `+ 1` → `zip_io_test` **15 条红**（报错数字正好 `-2592000 秒` = 30 天；
  1 月那组报 `2025-12 vs 2026-01`）。

**`.148` 的另一条：路由返回值必须是「写进 buf 的字节数」。**
`game_saves_remote_info()` 四条成功路径原来全 `return 0`，而 `http_server.c` 拿它当**长度**
建响应 → 恒 **0 长度响应**（HTTP 200、body 空）。前端 `JSON.parse` 必抛，「远程位置」卡的
逐用户远程信息**从 `.145` 起一次都没渲染出来过**，而日志里 `GET /game_save_remote_info`
全是 200，**从状态码上完全看不出问题**（这是它藏了三个版本的原因）。头文件契约本来就是
「返回写入长度，<0 出错」，实现向它对齐即可；参数不合法回 `-1`（路由映射 400）。
**教训：`MHD_create_response_from_buffer` 的长度参数如果来自一个「顺便当状态码用」的返回值，
就会静默产出空响应 —— 这类端点的返回值语义必须在头文件里写死并被守卫盯着。**

**真机验收（`.148`，`_ftp_probe/verify148_cycle.py` → `evidence148/cycle_result.txt`）**
同一轮「推送 → 拉取」，修复前 `10-08 → 09-08 → 08-08`，修复后：
```
基线   live MDTM=20261008111258  sha=b747e7eb   remote zip 本地头/中央目录 month=10
推送后 live 不变                   remote zip month=10（写侧不再退一个月）
拉取后 live MDTM=20261008111258（与拉取前完全相同）  sha=b747e7eb（不变）
```
⚠️ **已经被拉歪的存档日期，必须重新推送一次才会带上正确月份** —— 远端 zip 里存的是旧月份，
光升包不重推，拉回来照样是旧的。`.148` 现场已把远端 zip 的日期字段修回（真机 FTPSRV
**不支持 `MFMT` / `SITE UTIME`**，全回 `502 Command not recognized`，只能改包内字段再拉一次）。

**一个顺带看清的语义（不是 bug，但会误导）**：`/game_saves` 里每个存档的 `mtime` 取的是
**存档目录**的 `lstat`（`src/game_saves.c` 的 `game_saves_list_json`，`roots[i].path`），
不是文件本身的 mtime。所以拉取后它显示的是「目录刚被改过的时间」= 拉取时刻，而真正被
`utimes()` 的**文件** mtime 才是包里的时间 —— 两者会不一致。排查日期类问题时**别信面板
那个 mtime，去 FTP 打 `MDTM` 看文件本身**。

#### 2.1.5 整份替换**不许换掉存档目录本身**（`.149`，动 `gs_swap_dir` 前必读）

真机症状：**「手动拉取是成功的，完事游戏启动时拉取提示成功，但还是老存档」**。
手动与自动**走的是同一段代码**（`game_auto.c` 的 `ga_do` → `sr_pull`），
差别只有一条：**那一刻游戏在不在跑**。而 `game_saves.c` 的 `gs_swap_dir()` 原来是
`rename(target, old)` —— 它把**整个存档目录**换成一个新 inode。

游戏启动时已经把存档目录绑住了（PS5 起游戏时就把存档目录建好/挂好），
**换掉 inode = 正在跑的游戏永远看到那个被改名走的旧目录**。于是：
盘上内容是对的（`sha256` 一直对），游戏里是旧的，引擎还报「成功」。

**判据：盯目录的 `st_ino`，不是盯内容。** 内容对不对是另一回事 ——
宿主上只断言内容的话，旧写法**照样全绿**（内容确实换对了，只是换的是另一个 inode）。
- 真机：FTP `MLSD` 的 `unique` fact 就是 inode。实测同一条路径
  `/user/home/<uid>/savedata_prospero/PPSA27836` 拉取前 `0.5b0`、拉取后 `0.5ab`。
  `_ftp_probe/verify149_inode.py` 连拉两轮，这个值必须一直不变。
- 宿主：`test/game_saves_test.c` 的 ⑧' 与 ⑨ 各记一次 `ino_of_dir()`，操作后必须相等；
  外加父目录不留 `.old_` / `.tmp_` / `.rst_` 半成品。
- **变异对照 `test/mutation_check_saves_inode.sh`**：把 `gs_swap_dir` 换回旧写法 →
  **恰好 2 条红**（上传 + 还原那两条 inode 断言），无连带红。

**新写法（两步，每步回滚都无歧义 —— 这是刻意选的顺序，别改成「一个名字换一个名字」
那种交错写法：那样回滚要先记住哪些名字搬过，出错路径就成了最容易写错的地方）**
```
① target 的条目 → old/        回滚：把 old/ 里的全搬回 target
② src   的条目 → target/      回滚：先把 target 里刚搬进来的全退回 src，再把 old/ 搬回 target
```
- `target` 不存在时（全新拉取 / 主机上还没有这份存档）没有 inode 要保，仍走直接
  `rename(src, target)` —— 那是「换目录」唯一还正确的场合。
- **代价**：①② 之间 `target` 会短暂为空（几个 `rename` 的时间）。正在读存档的游戏
  理论上可能撞上这个窗口，但窗口是微秒级；而「换掉 inode」对跑着的游戏是**必然**失效。
- 三条路共用这一个函数：**远程拉取 / 上传存档 / 快照还原**。各写一份的话早晚有一边忘了回滚。
- `.old_` 已收进 `gs_name_is_temp()`，所以崩在中间留下的备份会被 `gs_sweep_temp()` 清掉
  （一小时新鲜度门槛保证不误删正在用的那份）。

⚠️ **这条限制仍然在**：如果游戏**已经把存档读进内存**了，任何外部写盘都救不回来。
`.149` 修的是「游戏绑着目录、看得见却没看见」，不是「游戏早就读完了」。
剩下那半条竞态（游戏在拉取**完成之前**就把存档读走）由 `.150` 用「拉取前挂起游戏」
处理 —— 见 §2.1.6。

> 顺带一条排查口径：`/game_saves` 里每个存档的 `mtime` 取的是**存档目录**的 `lstat`
> （`game_saves_list_json` 的 `roots[i].path`），不是文件本身。拉取后它显示的是
> 「目录刚被改过的时间」= 拉取时刻。**排查日期类问题别信面板那个 mtime，去 FTP 打 `MDTM`。**

#### 2.1.6 拉取前挂起游戏，拉完恢复（`.150`/`.151`，动 `ga_do` / 挂起链路前必读）

真机症状：`.149` 修掉「必然失效」之后，用户报的是**「偶尔成功，偶尔失败」**。

**为什么还会有「偶尔」**：拉取是把盘上存档**整份换掉**，而游戏启动时会把它的存档
**读进内存**。谁先谁后决定成败 —— 快启动的游戏输、慢启动的赢。这就是「偶尔」。
`.149` 管的是「游戏绑着目录、看得见却没看见」，管不了「游戏早就读完了」。

**做法**：拉取前把**正在启动的那款**游戏冻住，拉完再放行。冻住 = 游戏读不到存档，
于是「谁先谁后」这个竞态不存在了。用户口径的顺序（也是日志里那四行的顺序）：
```
游戏启动 → 引擎监测到启动 → 暂停游戏 → 拉取存档 → 拉取成功 → 恢复游戏
已挂起   → 远程拉取 开始   → 远程拉取 结束 → 已恢复
```

**关键事实**
- `sceLncUtilResumeApp` **存在**，与 `sceLncUtilSuspendApp` 成对，都在真机 SDK 的
  `libSceSystemService.so` 里（`strings` 扫 `target/lib/*` 得到）。**SDK 里没有头文件**，
  与 `GetAppIdOfRunningBigApp` 一样靠 `extern` 声明。
  **没有它这一步就做不成**（挂起了回不去 = 把用户正在玩的游戏挂死）—— 所以 `.150` 的
  第一件事是确认它存在，而不是直接写代码。
- `SuspendApp` 是**请求**，返回时应用未必真的冻住。所以 `Suspend → sleep(2) → 拉取`，
  与 `src/ps5_launcher.c` 的 `ps5_kill_disc_player` 同一口径。
  **刻意不用 `sceLncUtilIsAppSuspended` 轮询**：签名是猜的，为省一两秒赌一个原生调用不值得。
- 接口收在 **tid** 上（不是 app_id）：app_id 每次启动都不一样、宿主上没法造；tid 是测试
  本来就有的东西。tid → app_id 的解析在默认实现 `ga_running_app_id()` 里。
- **只用在自动拉取**（`ga_tick` → `ga_do(..., freeze_game=1)`）。手动拉取传 0：
  那时游戏早把存档读进内存了，冻它改变不了内存里那份，只会平白冻住用户正在看的画面。
- **只冻目标那款**：探测到跑的是别的游戏时不动它。
- **三条失败路径都有收场**：挂不住 → 仍继续拉取（退回 `.149` 老行为）+ 留痕 + 弹通知；
  恢复失败 → 日志 `!!!` + **屏幕通知**（游戏会一直冻着，用户会以为死机）；
  **没冻过就绝不恢复**（`frozen` 只在真冻住时为 1，避免把用户自己挂起的游戏叫醒）。
- 计时**从冻住之后开始**：`sleep(2)` 算进「拉取用时」会让每次自动拉取凭空多两秒，像网络慢。

**测试口径（这是本轮最该照抄的部分）**
- 扫描那一轮从 `static void ga_tick()` 导出成 **`void game_auto_tick()`**。挂起逻辑
  **只在这条路上**，不导出 = 零覆盖。
- 宿主钩子 `game_auto_set_suspender(susp, resm)`（同 `game_auto_set_probe` 手法）。
- `test/game_auto_test.c` ⑦：**顺序**（`已挂起` 必须是第一行，且 `挂起 < 开始 < 结束 < 恢复`）、
  挂不住时仍拉但不恢复、tick 与 ga_do 之间游戏退出则不挂不恢复、**手动拉取不挂起**、
  **恢复失败要有日志 + 屏幕通知**。
- **变异对照 `test/mutation_check_freeze.sh`**（两个变异，`[order|appid]` 可单独跑）：
  * `order` —— 把 freeze 那行挪到 `ga_do_inner` **之后** → **恰好 2 条红**（顺序那两条）。
    顺序是「测试照样绿、行为已经错」的重灾区（调用次数、日志行都在，只有次序错了），
    所以变异必须做在**位置**上 —— 只断言「挂起过一次」的用例在错误顺序下照样全绿。
  * `appid` —— 去掉挂起时 `*app_id_out = app_id` 那一行 → **恰好 1 条红**。

**回归**：宿主套件 `overall: PASS`，`game_auto` 112 项断言（由 70 → 106 → 112）。

##### ⚠ `.151` 追加：恢复**必须**用挂起时记下的 app_id，不许再解析（真机踩出来的）

`.150` 真机首轮：`已挂起 → 远程拉取 开始 → 远程拉取 结束` 三行齐全、拉取成功
（15,728,640 字节 / 1.8 秒），但**恢复失败**，而且**下一秒**引擎就报「已退出」：
```
22:40:11  已挂起 · 拉取完成后自动恢复
22:40:13  远程拉取 结束 · 成功 1 / 跳过 0 / 失败 0 · 用时 1846 ms
22:40:13  !!! 恢复失败 —— 请手动切回游戏（或退出重进）
22:40:14  已退出 · 跳过推送：「退出游戏时回传」关着
```

**「恢复失败」的下一秒就是「已退出」—— 那不是游戏真的退了，是我们自己丢了句柄。**

- **根因**：应用被挂起后 `sceLncUtilGetAppIdOfRunningBigApp()` 返回 `0xffffffff`
  —— 系统认为「没有正在运行的大应用」（挂起本来就是把它换出去）。而 `.150` 的恢复代码
  又调 `ga_running_app_id()` **解析了一次**：解析拿不到 → 直接 `return -1` →
  **`sceLncUtilResumeApp` 根本没发出去** → 游戏一直冻着。
  下一个 tick 的探测也拿到 -1 → `cur=""` → 日志写「已退出」。
  **游戏没退，是冻在那儿**；用户只能强退重进（真机日志里每轮间隔 7s / 45s / 25s
  不规则，就是这个）。
- **修法**：`ga_susp_fn` 成功时**必须**把 app_id 写出来
  （`int (*)(const char *tid, unsigned int *app_id_out)`），`ga_resm_fn` 收到的就是它
  （`int (*)(const char *tid, unsigned int app_id)`），**恢复路径里一次解析都不做**。
  宿主上有断言钉死这条不变量（`g_resm_id == FAKE_APP_ID`）。
- **两个返回码都进日志**（下一轮不用再猜）：
  `sceLncUtilSuspendApp(%d) 返回 0x%08x`、
  `恢复 %s：挂起时 app_id=%u，此刻 GetAppIdOfRunningBigApp=%d`（**这一行就是
  「挂起后系统不认这个大应用」的直接证据**）、`sceLncUtilResumeApp(%u) 返回 0x%08x`。
- **教训**：「挂起」这类 API 的句柄（app_id）**必须在挂起的那一刻记下来**。
  不要指望「稍后还能再查一次」—— 状态一变，查询口径可能就查不到了，
  而失败的表现（游戏冻着）离原因（拿不到 app_id）隔了三层，光看现象根本猜不到。

**`.151` 真机验收通过（`192.168.1.214`）**：日志里 `恢复 PPSA27836：挂起时
app_id=24601，此刻 GetAppIdOfRunningBigApp=-1` 那一行是**根因的实证** ——
挂起后系统确实不认这个大应用了；而用记下的 `app_id=24601` 恢复**成功**，
用户接着又玩了 31 秒才正常退出（`.150` 时是「恢复失败」的下一秒就报「已退出」）。
完整顺序：`已从快照还原 → 已挂起 → 远程拉取 开始 → upload ok → 远程拉取 结束 →
恢复…此刻 GetAppIdOfRunningBigApp=-1 → 已恢复运行`。

##### ⚠ `.152` 换实现思路：不再「切回前台」，而是**压根不让游戏离开前台**（进程级 SIGSTOP）

**`.151` 之后的症状**（用户口径）：「点击开始，游戏会自动进后台，出现图片这个画面后，
手动点击按钮会恢复，但是不自动恢复」—— 机制本身跑通了（`ResumeApp` 返回 0），
但**恢复之后游戏留在后台**（PS5 主屏 + 底部游戏画），必须手动点一下才回前台。

**先探针证伪，再定案。** 一次性真机探针 `_scratch/lnc_probe.c`（独立进程、逐步试、
每步 fsync 落盘 + 屏幕通知，崩了只死自己且尽力 `ResumeApp`+`SIGCONT`）逐个试候选 API，
结果**六个全废**：
```
app_id=57369 · 标题=PPSA27836 (rc=0) · eboot.bin pid=543
sceLncUtilSuspendApp(57369)          = 0x00000000
sceLncUtilResumeApp(57369)           = 0x00000000
sceLncUtilSetAppFocus(57369, 0)      = 0x00000000   ← 返回 0，毫无效果
sceLncUtilSetAppFocus(57369, 1)      = 0x00000000   ← 返回 0，毫无效果
sceLncUtilSetControllerFocus(57369,0)= 0x00000000   ← 返回 0，毫无效果
sceLncUtilSetControllerFocus(57369,1)= 0x00000000   ← 返回 0，毫无效果
sceLncUtilContinueApp(57369)         = 0x80020010   （EBUSY）
kill(543, SIGSTOP) … kill(543, SIGCONT) …           ← 有效
```
- **结论**：display 归 ShellUI 的 **scene manager** 管，**不是 LncUtil 能撬的**。
  公开资料也查不到签名 —— SDK 的 `.so` 是桩库（所有符号同址 `0xaae4`）、shadPS4 是空桩、
  psdevwiki / grep.app / LightningMods 的 writeup 只有名字没有签名。
  **没有猜签名进主流程**：赌错一个原生调用而崩在挂起中 = 游戏**永久冻死**，代价不对等。
- **换方案**：改用**进程级 `SIGSTOP`** —— 游戏**压根不离开前台**，「切回前台」这个问题
  根本不存在；也更贴合用户原话里的「**暂停游戏**」。前台判据（供参考，本轮没用到）：
  `SetControllerFocus(<appid>)` 命中 = 前台，来自 `StonedModder/ps-game-state-lib` 的 `f_state.c`
  （Shell/Home = `0x7`）。

**实现（`src/game_auto.c`，全在 `#ifndef GA_HOST` 内）**
- `ga_find_eboot_pid()`：`sysctl(KERN_PROC_PROC)` 枚举进程，`comm == "eboot.bin"` 取**最大 pid**。
  手法抄 `src/ps5_launcher.c` 的 `get_pid_by_name`（那条路真机给 SceDiscPlayer 发过 SIGKILL，实证过）。
  缓冲一律 `malloc`（连接线程栈只有 1MB）。
- `ga_proc_stat()`：`sysctl(KERN_PROC_PID)` 读 `struct kinfo_proc` 的 `ki_stat` + `ki_runtime`
  （累计 CPU 微秒）。
- **冻结要「验证」，不是「kill 返回 0 就算冻住」**：先量 200ms 做**对照**（运行中的游戏一定用掉
  CPU）→ `SIGSTOP` → 再量两次，要求 `ki_runtime` **不再前进**；一旦还在涨立刻 `SIGCONT` 并回落
  LncUtil。日志那行就是证据：`冻结验证 pid=543：CPU 200ms +… us → +0 us · stat …→…`。
- **不透明 handle 不变量**：SIGSTOP 的凭据是 **pid**、LncUtil 的是 **app_id** —— 冻住时用哪套、
  放行时就用哪套，**不许当场重新解析**（`.151` 已经栽过「再解析一次拿不到」）。所以策略层多带一个
  **机制代号**：`GA_FREEZE_NONE / SIGSTOP / LNC`。
- **回落链**：`ga_frz_default()` 先试 SIGSTOP，失败再试 LncUtil；`ga_thaw_default()` 按代号分发。
  两套都失败 → 仍继续拉取（退回 `.149` 老行为）+ 留痕 + 弹通知。
- 接口随之改名：`game_auto_set_suspender` → **`game_auto_set_freezer`**；
  `ga_susp_fn/ga_resm_fn` → **`ga_frz_fn/ga_thaw_fn`**
  （`int (*)(const char *tid, int *kind_out, unsigned int *handle_out)` /
  `int (*)(const char *tid, int kind, unsigned int handle)`）。
- **宏改名 `-DGA_NO_LNCUTIL` → `-DGA_HOST`**：宿主上现在不只挡 LncUtil，还要挡
  `sysctl(KERN_PROC)` / `struct kinfo_proc`（Linux 的 `<sys/user.h>` 是另一回事，编不过）。
- **`#include <stdint.h>` 必须排在 `<sys/sysctl.h>` / `<sys/user.h>` 之前** ——
  `sys/user.h` 不自包含（它链到的 `x86/fpu.h` 直接用 `uint8_t` 却不带 stdint.h），
  探针上刚踩过，`game_auto.c` 里加了同样的注释与顺序。

**测试口径**
- `test/game_auto_test.c` 第 ⑧ 节（70 → **113 checks**）：桩改签名带回 `kind_out`/`handle_out`，
  新增断言钉死「放行收到的机制代号 == 冻结时报的那个」（`g_resm_kind == GA_FREEZE_SIGSTOP`）。
- **变异对照 `test/mutation_check_freeze.sh` 扩到三个**（共用 `inject_drop_line()`，`[order|handle|kind]` 可单独跑）：
  `order`（把 freeze 那行挪到 `ga_do_inner` **之后**）→ **恰好 2 红**；
  `handle`（去掉 `if (handle_out) *handle_out = handle;`）→ **恰好 1 红**；
  `kind`（去掉 `if (kind_out) *kind_out = kind;`）→ **恰好 1 红**。
- 完整宿主套件 `test/run_host_tests.sh`：**`overall: PASS`**（`game_auto` 113 checks、栈帧守卫、POST 路由守卫都过）。

**真机验收要看的三行新日志**（`/data/nayeplmtools/` 引擎日志）：
```
已暂停（进程级 SIGSTOP）· 拉取完成后自动恢复
冻结验证 pid=…：CPU 200ms +… us → +0 us · stat …→…
已恢复运行（pid=…）
```
重点是**肉眼确认游戏全程留在前台**（画面冻住但没跳主屏）—— 这一点只有真机能验，
探针只能证明「候选 API 无效」。**`.152` 尚未推真机。**

##### ⚠ `.153`：`.152` 的 SIGSTOP **压根没跑起来** —— 「验证不可用」被当成了「冻结失败」

**`.152` 真机首轮**（`.152` 已在设备上跑，用户口径：「远程拉取成功后，并没有将游戏
拉到前台」）：
```
[23:22:28] saves: 读不到 pid=582 的进程状态，进程级冻结跳过
[23:22:30] saves: PPSA27836 已暂停（LncUtil 挂起）· 拉取完成后自动恢复
[23:22:32] saves: 恢复 PPSA27836：挂起时 app_id=24601，此刻 GetAppIdOfRunningBigApp=-1
[23:22:32] saves: PPSA27836 已恢复运行（app_id=24601）
```
**枚举找到了 pid=582**（`KERN_PROC_PROC` 是好的），但**按 pid 单查那一步失败了** ——
于是 `ga_sigstop_freeze()` 在**发出 SIGSTOP 之前**就 `return -1`，主方案作废、回落
LncUtil，游戏照样跳后台。

**两条根因，一条机制一条策略 —— 策略那条更该记住**
- **机制**：`ga_proc_stat()` 用 `KERN_PROC_PID`（按 pid 单查），真机上直接失败；
  而 `KERN_PROC_PROC`（整表枚举）是**真机验过的**（`ga_find_eboot_pid()` 就靠它找到 pid）。
  **同一台机器上一条能走、一条不能走** → 换成能走的那条。
- **策略**：**「验证不可用」被当成了「冻结失败」**。「冻住后 CPU 不再前进」只是**加分项**，
  不是前提。读不到进程状态、或进程本来就没烧 CPU（对照样本不动）时，验证只是**说不清**，
  绝不该因此放弃 SIGSTOP。**这一格就是 `.152` 真机栽的地方。**

**修法**
- `ga_proc_stat()` 改走**整表枚举**（`KERN_PROC_PROC` + 按 `ki_pid` 匹配），
  与 `ga_find_eboot_pid()` 同一条路；失败时把 `errno` 记进日志。
- 判定抽成**纯函数**并导出（`include/game_auto.h`）：
  ```c
  int ga_freeze_verdict(int have_ctl, int ctl_adv, int post_ok, int post_adv);
  /*  1 = 确认冻住 · 0 = 无法验证（**按已冻处理，绝不回落**） · -1 = 确认没冻住 */
  ```
  只有「对照推进了、冻结后**还在**推进」才返回 -1（唯一允许回落的输入）。
- 一次性把「`kinfo_proc` 编译期大小 vs 内核报的 `ki_structsize`」记进日志 ——
  `.152` 的「单查读不到」怀疑是 SDK 头与固件内核的**结构体版本错配**，有这两个数
  下次一眼可判。
- 新日志：`冻结验证不可用（pid=… 的进程状态读不到，或 CPU 对照未推进）· 按 SIGSTOP 已投递处理`。

**为什么导出纯函数**：`.152` 栽的是**策略**错，而真机那段代码整段在 `#ifdef GA_HOST` 里
—— 宿主根本编不到，等于这个坑零覆盖。抽出来之后宿主能把 16 种输入组合全部穷举
（同 `.150` 导出 `game_auto_tick()` 的理由）。

**测试口径**
- `test/game_auto_test.c` 新增第 ⑨ 节（113 → **118 checks**）：判定表 **16 种组合穷举**
  （错几个只报一行，附计数）+ 三条**带名字**的策略断言。
- `test/mutation_check_freeze.sh` 加第四个变异 `verdict`（把「验证不可用」那格改成
  `return -1` = 把 `.152` 的 bug 原样放回去）→ **恰好 3 条红**（判定表 1 + 策略断言 2）。
  其余三个变异原样通过。
- 完整宿主套件 `overall: PASS`。

**真机验收要看的新日志**（除 `.152` 那三行之外）：
```
kinfo_proc 编译期 … 字节 · 内核报 … 字节 · 共 … 条
冻结验证不可用（pid=… 的进程状态读不到，或 CPU 对照未推进）· 按 SIGSTOP 已投递处理
```
第一行是**结构体版本错配**的判据；第二行若出现，说明验证仍然拿不到数据，
但**游戏应该留在前台了**（这正是本轮的目的）。

**`.153` 真机验收通过（`192.168.1.214`，用户连开三次游戏）**：三次**全部**走
`进程级 SIGSTOP`，一次都没回落 LncUtil。
```
[23:34:31] saves: kinfo_proc 编译期 1088 字节 · 内核报 1096 字节 · 共 88 条
[23:34:32] saves: 冻结验证 pid=596：CPU 200ms +60089 us → +0 us · stat 2→4
[23:34:32] saves: PPSA27836 已暂停（进程级 SIGSTOP）· 拉取完成后自动恢复
[23:34:36] saves: 远程拉取 结束 · 成功 1 / 跳过 0 / 失败 0 · 用时 3559 ms
[23:34:36] saves: PPSA27836 已恢复运行（pid=596）
[23:34:57] saves: PPSA27836 已退出        ← 恢复后又正常玩了 21 秒，没被冻死
```
- **`KERN_PROC_PID` 失败的真因就此钉死**：`kinfo_proc` **编译期 1088 字节、内核报 1096 字节**
  —— SDK 头比固件内核**小 8 字节**。FreeBSD 的 `sysctl_out()` 要求 `oldlen >= ki_structsize`，
  给 1088 < 1096 → 直接 `ENOMEM`。**整表枚举不受影响**（缓冲按内核报的总长 `malloc`）。
  那 8 字节在**结构体末尾**：`ki_stat` / `ki_runtime` 的偏移都是对的（`stat 2→4` 与 CPU 值都合理）。
- **验证是真的，不是空转**：对照样本 `+60089 us / 200ms`（真在烧 CPU），SIGSTOP 后 `→ +0 us`。
  三次分别 60 ms / 187 ms / 179 ms。
- **`stat 2→4` = `SRUN` → `SSTOP`** —— 内核自己承认进程被停住了，比 CPU 时间更硬的证据。
- 拉取 15,728,640 字节 / 3.0–3.6 秒（`.152` 那次 1.8 秒，SMB 波动）；**冻结总时长 ≈4.4 秒**
  （含 600 ms 采样）。**用户肉眼确认「留在前台，跟想象的一幕一样」** —— 画面冻住三四秒然后继续，
  不跳主屏。至此 `.150` 起的这条链（挂起 → 拉取 → 恢复 → 不跳前台）**闭环**。
  用户没觉得卡，所以「省掉 400 ms 双采样」的优化**没做**，留作 backlog
  （`ki_stat == SSTOP` 本身就是终局判据，真要做可从那里下手）。
- **端到端也验了**：用户先还原旧快照 `20261008-191114` 再进游戏，确认**游戏里用的就是
  拉取回来的那份存档**。所以 `.148`→`.153` 的「远程存档同步」整链**真机验收通过**：
  拉取成功 → 游戏被冻住 → 盘上存档换掉 → 放行 → 游戏读到新存档 → 全程留在前台。

### 2.2 React 前端（`frontend/src/`）
- `App.jsx` 布局/路由 + **`NAV_ITEMS`/`NAV_GROUPS`（一份菜单表驱动侧栏与手机底部栏）**；`components/views/*` 各功能页（GameManageView 游戏管理、GameSection 游戏宫格、ThermalSection 温控、**AccountToolsSection 账号工具（`.99`，常用配置板块：登录/激活/改名/PIN/头像/账号 ID 换算）**、StorageHub、Autoload、ActiveProcesses、LogViewer、Settings、ManageSources、CommonEntries、MoveFromUsb、Donate、PkgManagerFrame...）。
- `i18n.js` 加载 locale（子模块）+ **合并 `locales-overlay/<lang>.json`（后者优先）**；完成度 <70% 的语言不注册。
- `components/ui/` 共用件：`Section`（板块 = `.panel-card` + 板块头 + body，`headless`/`bodyPad` 供自带头的卡片复用）、`PageHead`（页头 + 粘顶，`back`/`bleed`）、`EmptyState`、`LoadingState`、`Modal`、`Toast`、`Switch`（唯一开关）、`Segmented`（唯一互斥分段）、`NavButton`、`PayloadName`。**新页面一律用这些件，不要再手写板块容器 / 空态 / spinner。**
- `utils/spatialNav.js`：仅 `isPS5` 挂载的遥控器空间导航。
- `theme/`：`light-theme.css`（暗→亮类名重映射，零改上游组件）、`cn-typography.css`（中文排版）、`useTheme.js`（localStorage `pldmgrTheme`）、`ThemeToggle.jsx`。

#### 2.2.1 头像裁剪弹窗（`.107` 重写，`AccountToolsSection.jsx` 的 `CropModal`）

四条**互相独立**的踩坑记录，改这个组件前务必先读：

1. **Tailwind preflight 的 `img,video{max-width:100%;height:auto}` 会掐扁绝对定位的图片。**
   裁剪图靠 inline `width/height` 定尺寸，`max-width:100%` 把布局宽度压到容器宽（260 框 → 258），高度仍按自然高撑开 → 3200×1200 的图被压成 258×1200。**图越宽压得越狠，方形图几乎看不出**，所以症状是「部分图片严重变形」。凡是用 inline `width/height` 给 `<img>` 定尺寸的地方，都要显式写 `maxWidth:'none'` / `maxHeight:'none'`。
2. **全局 `box-sizing:border-box` 会吃掉取景框的边框宽度。** 取景框写 `width:260px` + 1px 边框 → 可视内容只有 258×258，而 `clampView`/`zoomTo`/导出全按 `CROP_SIZE=260` 算，预览比导出每边少 1px。取景框必须 `boxSizing:'content-box'`。
3. **弹窗绝不能在 busy 期间锁死。** 曾用 `dismissible={!busy}` + 取消键 `disabled={busy}`，主机一慢用户就被关在弹窗里出不来。现在的约定：恒可关闭，关窗即 `abort()` 在途请求并归零 `cropBusy`；`fetchWithTimeout` 给上传 30s / 应用 20s，`canvasToPngBlob` 给 `toBlob` 4 秒兜底（老 WebKit 大画布偶发**不回调**）。
4. **换图重置用 `key={src}` 强制重挂，不要在 effect 里 setState。** 重挂天然归零全部内部 state，也避开 `react-hooks/set-state-in-effect`。另外 `nat` 到手前不渲染 `<img>`，否则新图会先按上一张的尺寸布局一帧。

其余设计要点：初始适配 **cover**（contain 会留白边、导出带透明条）；缩放以**取景框中心为锚点**；滑杆走**相对倍数** 1×–4×（绝对 scale 在几千像素的图上只有 0.06 量级，整条区间挤在头几像素）；导出边长 **440**（覆盖 PS5 头像四档里最大的一档）；导出用**九参数 `drawImage`** 只光栅化取景框那块源矩形，大图不再整张进画布。

**回归测试**：`cropfix.mjs`（75 条数值断言，见 `CHANGELOG v0.5.2.107`）+ `avatar109.mjs`（19 条，见 `CHANGELOG v0.5.2.109`）。改这个组件后必跑 —— `cropfix.mjs` 的第一条断言「`<img>` 的 `offsetWidth` == 自然宽」就是坑 1 的看门狗。

#### 2.2.2 头像「上传不生效」——坑在 MHD 流式状态回收，不在图像代码（`.109`）

**症状**：主机上点头像 → 选图 → 裁剪 → 点「确认使用」→ **一直显示「上传中」，不变、不报错、也不提示**。

**排查顺序（省时间）**：
1. 先 `diff` 原版 `D:\offact-new/accounts/src/avatar.c` —— **逐字节一致**。所以转换/编码/写盘那套移植逻辑没问题，**别去读 DCT 编码器**。
2. 差异只可能在「并入主 ELF」时新加的那层。逐个比对 `src/psaccounts/api.c` 与原版 `websrv.c` 的 `websrv_on_request`。

**真根因**：`api.c` 里判「上传结束」用的是 `*upload_data_size == 0`。但 MHD 的调用序列是 **① size=0 建状态 → ② 若干帧带 body → ③ size=0 收尾** —— 第 ① 帧 size 也是 0，可 `avatar_upload_request` 那时只是把文件打开、`*state` 仍非 NULL。旧代码在第 ① 帧就把流式状态 + `st` 一起 `free` 并 `*con_cls = NULL`，于是 MHD 重新调用时又拿到 NULL、重新分配、又进这个分支、又不消费 body —— **请求永远走不到收尾那帧**。

**修法与铁律**：
- 判据必须用 **`st->avatar_upload_state == NULL`**（`avatar_upload_request` 只在「答完了」才把 `*state` 置回 NULL）。
- **流式状态一律交给 `MHD_OPTION_NOTIFY_COMPLETED` 回调回收**，handler 里不自行 `free` —— 源工程就是这么做的，它与「并入主 ELF」是两套 MHD 生命周期，合并时以**源工程**的回收路径为准。
- `psaccounts_conn_completed()` 已用 magic 头认自家状态、且 `http_on_request_completed` 有 `(void *)1` 哨兵守卫，不会误放别人的内存。

**前端配套（同一症状的第二层）**：`fetchWithTimeout` 超时触发的**也是 `AbortError`**，与「用户关窗」无法区分。`closeCrop()` / 卸载兜底现在会先给 controller 打 `userCancelled = true` 再 `abort()`，catch 里先看这个标记：**用户主动关窗才静默，超时一律报出来**。约定：上传的三条收场路径**都必须有明确结局**（成功 / 后端拒并带原因 / 超时），不许有「静默 return」。

**本地验收**：`mock-server.js` 的头像已改成真闭环（上传字节留在内存、`/api/avatar?id=` 原样吐回）。以前那里写死 404，本地预览「上传成功但缩略图不变」与实机故障**长得一模一样**，会把排查方向带偏。

#### 2.2.3 通知条（Toast）的位置与体量（`.109` 定稿）

用户口径：「提示标签 改的好看一点，太大带占地方了，能不能放到中下边」（指点了激活/登入后弹出来的那条提示）。

- **位置：底部居中**。容器在 `App.jsx`：`fixed inset-x-0 bottom-0 flex flex-col items-center`。手机端要抬到底部导航栏之上 `calc(var(--bar-h) + env(safe-area-inset-bottom) + 1rem)`；**电视端底部栏是 `hidden`**，只留 `pb-6`（`isPS5` 分支）。
- **体量：高 36px**（旧版 56px+）。`px-3 py-2` / `rounded-xl` / 图标 16px / 正文 13px，**去掉 `font-bold uppercase`**。
- **语义与档位不动**：成功 emerald、错误 red，驻留 4s / 9s。
- 改这两个文件（`Toast.jsx` + `App.jsx` 容器）之后跑 `avatar109.mjs` 的第 ① 组断言（6 条几何硬判据，含「在上半屏之外」这条防回退的看门狗）。

#### 2.2.4 电视端「确定」的落点：**曾经**是「光圈是权威」（`.110` / `.111`）—— `.112` 已整层回退

> **⚠️ 本节是历史记录，其中的实现已经全部删除。改电视端交互前先读 2.2.7（现行契约）。**
> 保留这一节是因为其中两个判断本身仍然成立、以后还会遇到，但它们导向的**解法**已被用户否掉。

**仍然成立的两条前提**：
1. **网页无法移动系统/浏览器指针**。Pointer Lock 只能锁不能移，合成 `mousemove` 也移动不了真光标。
   所以「让鼠标跟着走」这类字面需求永远做不成，**要先把口径翻译成「用户要的结果」再选路**。
2. **「是不是指针在动」不能只看有没有 `mousemove`。** PS5 浏览器在方向键/OK 时可能顺带挪一下自己的光标。
   当年用的判据是「两道闸」：方向键后 400ms 时间窗内一概不算 + 第一发只立基准、之后位移需 > 12px。
   这条判据本身是对的、可复用的，只是现在不需要了（不再分辨遥控器与鼠标，见 2.2.7）。

**已被否掉的解法（`.110` / `.111`，不要再做）**：
- `.110` 把 OK 从「浏览器在指针处点一下」改成「我们自己点 `navTarget`（光圈元素）」，
  再在捕获阶段拦 click 做四件事：放走 untrusted / 吞掉 OK 后 800ms 内的那一发 /
  指针用户原样放行 / 其余改投到 `navTarget`。
- `.111` 进一步发现「光圈与系统光标各走各的」会让屏幕上有两个「我在哪」，于是**把系统光标藏起来**
  （`html.nav-remote`）。
- **用户实测后否掉整条路线**（`.112` 原文）：「ps5真机上焦点鼠标移动的方式最好改回跟上游一样，
  现在这样很别扭」，追问后明确「改回上游作者的处理方式一样，所有导航都出光圈，
  全部是鼠标跳到对应的位置」。
- **教训 —— 值得记住的不是解法，而是这个形状**：那三层每一层都在补偿上一层带来的新问题
  （自己搬焦点 → 确定键点错位置 → 抢确定键 → 两个「我在哪」→ 藏光标），越补越别扭。
  **当一串补丁开始互相补救时，先回头看第一层该不该加**，而不是继续补第五层。
  完整链条见 2.2.7 的表格。

**唯一从这一段留下来的东西**：`?navdebug=1` 只读排障面板。PS5 浏览器没有控制台，
实机复现不了的时候它是唯一取证手段（不带参数零开销，连 DOM 都不建）。现在仍在，见 2.2.7。

**验收脚本**：`nav110.mjs` **已删除**，由 `nav112.mjs` 取代（见 2.2.7）。
#### 2.2.5 账号按钮的契约（`.111` 定稿，`AccountToolsSection.jsx`）

用户口径：「**去掉反激活功能**，激活几次就不允许反激活了，**激活成功，按钮隐藏或者禁用**。」

- 那一格原来同时兼任「激活」与「反激活」。现在只剩「激活」：`acc.activated` 为真时**整颗不渲染**。
- **选「隐藏」而不是「变灰禁用」**：电视端方向键会把灰按钮也算作一个落点 —— 一颗永远点不动的按钮只会让人以为坏了，还白占一次按键。这条判断以后遇到同类取舍可直接复用。
- 三种状态的按钮条数（`acct111.mjs` 逐条钉死）：
  | 状态 | 按钮行 |
  |---|---|
  | 未激活、未登录 | `[登入]` + `[激活]`（两颗，激活为绿档） |
  | 已激活、未登录 | `[登入]`（一颗） |
  | 已激活、已登录 | `[登出]`（一颗，红档） |
- `doSignoutFull()` 已删除；板块内**不出现**「反激活」「登出并反激活」字样；全程**不发出** `/accounts/deactivate` 或 `/accounts/signout-full`。
- **服务端 `deactivate` / `signout-full` 两个动作保留**，只作 HTTP 层退路 —— 它们是单账号退回未激活的唯一手段，删掉就只剩 `/api/safety/restore` 的全量还原。若将来要彻底禁用，改 `accounts_action` 即可，前端无需再动。
- 词条 `acctools.deactivate` / `acctools.deactivate_tip` / `acctools.signout_full_tip` 已从两份 overlay 移除；`acctools.signout_full_tip` 只在历史 CHANGELOG 里还查得到。
- **回归测试**：`acct111.mjs`（23 条）。它**取代**了 `acct107.mjs`（那份测的「登出 / 反激活」二分法已被本轮口径推翻，脚本已删）。


#### 2.2.6 「铺满右侧」的面板形态（`.112` 新增，`ui/Modal.jsx` 的 `layout="panel"`）

用户口径：「存档管理页面打开后铺满右边整个区域。多媒体管理也是铺满右边。修改名称也是铺满。」

- **做法**：`Modal` 加一个 `layout` 属性。`layout="panel"` 时外层由 `inset-0 居中 + p-8` 换成
  `inset-y-0 right-0` + `left: var(--sidebar-w)`，面板内部改 flex 列：**标题栏与 footer 钉住、只有内容区滚**
  （原来是整块一起滚，内容一长标题和按钮都被滚走）。默认仍是 `layout="dialog"`，其余十来个调用方一行没改。
- **两条硬约束**：
  - **`--sidebar-w` 是必需的令牌，不能写死**。侧栏宽度在 `App.jsx` 里是 Tailwind 字面量 `w-80` / `w-28`，
    CSS 读不到类名，所以 `index.css` 另立一条，由 `App.jsx` 维护的 `html[data-sidebar]` 切换。
    三档的**归零条件**最容易漏：窄屏（<768px）侧栏是 `hidden` → 必须 0，否则手机上面板左边会空出 320px。
  - **`.panel-sheet { margin: 0 !important }` 不是保险，是必需**。全站视图根节点统一是 `.space-y-6`，
    它的 `> * + *` 会给视图的**每一个**后续子元素挂 `margin-top` —— 而弹窗正是视图的一个子元素。
    居中弹窗看不出来，面板是贴边定位的：实测多出 **14.4px** 上边距，上下各缩一截，「铺满」直接不成立。
    特异性上 `.space-y-6 > * + *` 是 (0,2,0) 压过 `.panel-sheet` 的 (0,1,0)，所以只能 `!important`。
    **判据要用几何量**（`getBoundingClientRect` 对齐侧栏右边与视口右边），别用 `className` ——
    后者只能证明「写了那个类」，证明不了 `--sidebar-w` 真的被读到。
- **面板形态不画遮罩**：面板已盖满右侧，再压一层黑只会把露在外面的侧栏也闷掉，反而让人不敢点侧栏。
- **四角全平**（`.112` 追加）：上一版留了 `rounded-l-ps-3xl`，本意是「贴着右边缘的一张卡片」，
  但面板左边缘正好压在侧栏分界线上，那道圆角会被读成「侧栏右下角缺了一块」。
  用户口径：「刚才铺满的几个页面圆角全部取消」。**判据读计算样式的 `border-radius`，别看 className** ——
  后者只能证明「类名里有 `rounded-none`」，证明不了 `rounded-l-ps-3xl` 这类没被一起清掉。
- **面板内部：预览图这类「看的东西」占整行、动作与说明按「先动作后注解」排**（`.112` 第二轮）。
  用户口径：「把文字说明和按钮换位置，预览图片铺满一行」。改名页背景块就是这么排的：
  预览独占一行 → 按钮 → 说明。
  **铺满一行必须配高度上限**：16:9 铺满时高度 = 宽度 × 9/16，电视上（正文宽 1516px）就是 853px，
  单它一个就把整块顶出屏幕。用了 `max-h-[46vh]`，并且图随之改 `object-contain`
  —— 上限生效后容器不再是严格 16:9，`cover` 会裁图，而这个图是给用户判断「当底图好不好看」的，
  **裁掉一半等于给错的信息**。磁贴上那一层仍是 `cover`（它要填满卡片，注意力在游戏图标上）。
- **面板内部的字号：四条语义档，尺寸写在 CSS 一处**（`.113`）。
  用户口径：「这几个铺满页面的字体太小了，**跟其他父级菜单一样大**」。
  病灶是**硬编码 px** —— 游戏管理那五个弹窗里原本有 60 处 `text-[13px]/[12px]/[11px]/[10px]`，
  px 不随根字号走，而 `body` 在 ≥768px 被抬到 20px：同一块屏幕上侧栏菜单项 16.5px、
  兄弟页面正文 `text-sm`(17.5px)，面板里的字却还停在手机档。现在：

  | 类 | 字号 | 改前 | 用在 |
  |---|---|---|---|
  | `.panel-body` | 1rem（16px） | 13px | 正文基准：列表项主行、开关名、名称输入框 |
  | `.panel-note` | 0.875rem（14px） | 12px | 次级说明：与兄弟页面的 `text-sm` 同源 |
  | `.panel-tiny` | 0.8125rem（13px） | 11px | 提示行 / 图注 / 脚注 |
  | `.panel-micro` | 0.75rem（12px） | 10px | 芯片徽标、时长角标、`›` 这类记号 |

  - **⚠️ 量字号时最容易踩的一条：`rem` 不等于 `body` 的字号。** 本仓库把
    `body { font-size: 20px }` 写在 ≥768px 的媒体查询里，而 **`html` 从未设过字号** ——
    于是 `getComputedStyle(document.documentElement).fontSize` **恒为 16px**，
    一切 rem 类（Tailwind 的 `text-sm`/`text-xs` 也一样）都按 16 换算；
    `body` 的 20px 只对「自己没有字号的继承元素」生效（例如侧栏按钮本身）。
    第一版按「桌面 rem = 20px」推算，写出的 0.9rem 实际只有 **14.4px**，等于没改 ——
    是量目标元素的计算字号才发现（`{"root":16,"nav":20,"body":14.4}`）。
    **判断字号有没有真的变大，只能量目标元素，不能推算根字号。**
  - **基准要量侧栏菜单标签那颗 `<span>`（16.5px），不是 `.nav-item` 按钮本身**：
    按钮没有自己的字号，继承 body 的 20px，拿它当基准会让「面板字号 ≥ 菜单档」永远失败，
    而失败原因跟面板一点关系都没有。用户说的「父级菜单」是眼睛看到的那排字。
  - **芯片高度必须跟着字号走**（`.chip-xs`，1.25rem）：原来是写死 `h-[18px]`，
    字号涨到 12px 之后上下只剩 3px，汉字会被裁一半。
  - **子板块标题改用页面级同一个 `.panel-title`**（16.8px）：全局替换会把标题一起变成 16px，
    和正文平齐 —— 层级就没了。所以替换时这两处（`Block` 的 h4、芯片）是**单独处理**的。
- **「上传」与「生效」是两步，界面就该是两步**（`.113`，改名页两层图）。
  用户口径：「这里点击也可以上传图片，**上传完要有一个应用的动作**」。
  现在的形状：**预览图本身就是选图入口**（两块预览各自是带 `aria-label` 的 `<button>`，
  悬停浮出「点图片也可以选图」）→ 选完图先缩好、用 `objectURL` 就地预览并标出「待应用」→
  点「应用」才提交后端，另有「取消」。理由不是洁癖：选错图不会先污染主机上的磁贴；
  1920×1080 的 PNG 传到 PS5 要好几秒，传完才发现选错就得再传一次。
  - **同一面板里两颗「应用」的可见文字刻意相同**（对着自己那一块看没有歧义），
    但**可访问名必须区分**：`aria-label="应用（磁贴图标）"` / `"应用（磁贴背景）"`。
  - ⚠️ **`locator(':has-text("应用"))` 会一起命中「待应用」芯片**（子串匹配）——
    任何「靠可见文字定位按钮」的自动化都会在这里挑错，必须按 `aria-label` 定位。
  - `objectURL` 的回收**只写一处**（一条「上一轮有、这一轮没了」就 revoke 的同步 effect），
    覆盖 换一张 / 应用 / 取消 / 恢复默认 / 换游戏 / 关弹窗 六条路径 —— 少记一处就是漏一处。
    它**不能**写在渲染期那段状态重置里：`react-hooks/refs` 会报 `Cannot access refs during render`。
- **为什么不做成路由页**：这三个入口都是「针对某一款游戏的临时工作区」（进来干完就关），
  做成路由会把「当前是哪个游戏」塞进 URL，返回键与遥控器的语义都得跟着改。

#### 2.2.7 电视端的「我在哪」：光圈跟着指针走（`.112` **回退定稿**，`utils/spatialNav.js`）

**这一节是回退记录。先记住结论：不要再往这个文件里加「自己搬焦点 / 抢确定键 / 藏光标」的逻辑。**

用户口径（`.112`）：「ps5真机上焦点鼠标移动的方式最好改回跟上游一样，现在这样很别扭」，
追问后明确「改回上游作者的处理方式一样，**所有导航都出光圈，全部是鼠标跳到对应的位置**」。

- **先确认「上游是什么」**：`spatialNav.js` 是 fork 独有的（`.92` 加的），上游 `main` 里
  **没有任何键盘/指针处理**（上游的 `Modal` 也只有一颗关闭按钮）。所以「改回上游」= 我们**什么都不接管**。
  按字面去「修一处 bug」会走偏，这一步是整个回退里最关键的判断。
- **删掉的三层，以及每一层引出的新问题**（越补越别扭的完整链条）：

  | 版本 | 加了什么 | 引出什么 |
  |---|---|---|
  | `.92` | 按几何位置自己搬焦点（方向键 → `pickNext` 找最近控件 → focus） | 光圈与系统光标开始分离 |
  | `.110` | 「确定键由我们点光圈元素」+ 捕获阶段把 click **改投** | 需要 `pointerMode` + 400ms 时间窗 + 12px 位移判据去分辨遥控器与鼠标 |
  | `.111` | 遥控器模式下**藏掉系统光标** | 又少了一处真实指示 |

- **现在只做一件事**：把光圈画在**指针当前压着的那个可操作元素**上。两条路共用同一个 `paint()`，
  所以同一时刻只有一个环：
  - **指针路**：`mousemove`/`pointermove` 时 `elementFromPoint` → **向上找最近的可操作祖先**。
    PS5 浏览器若用方向键驱动光标（发 trusted mousemove），这一步自动跟着走 —— **不需要任何按键处理**。
  - **焦点路**：键盘 Tab 等原生焦点移动由 `focusin` 挂同一个环。
- **三处刻意不做**（都是上一版踩过的坑，别再加回来）：
  ① **不动 `document.activeElement`**（只画环不搬焦点）—— 否则会出现「指针划过输入框、把正在打的字抢走焦点」；
  ② **不拦方向键**（不 `preventDefault`、不做几何跳转）；
  ③ **不留「首次落焦」** —— 没有指针压着控件时不画环，凭空造位置只会又变成两个「我在哪」。
- **`pickNext` / 几何代价函数整段删除**。连带解决了上一版那个「方向键走不进磁贴里四颗按钮」的别扭行为 ——
  那是自己搬焦点的算法自己造成的，不搬就没有这个问题。
- **`.111` 的藏光标一并撤掉**：光圈与系统光标永远重合，不再制造第二个「我在哪」。
  `index.css` 的 `html.nav-remote` 规则已删，**历史留在注释里**（别再加回来）。
- **浮层开合后清环**：`MutationObserver` 盯 `role="dialog"` 的出现/消失，出现时清掉环
  （否则指针没动过，环会停在浮层底下的元素上，视觉上像「环跑到弹窗后面去了」）。
- **保留的两件**（与光标无关、且是本 fork 的真实缺陷修复）：
  - **返回键派发 `ps5-back`** 交外壳裁决（遥控器上没有浏览器返回按钮，弹窗关不掉、页面退不回去）。
    但**屏上有浮层时把这次返回留给浮层** —— 见 2.2.8。
  - **`?navdebug=1` 只读排障面板**（PS5 浏览器没有控制台，实机取证只能靠它）。
    `describe()` 取不到可见文字时**退回 `aria-label`** —— 站内大量控件只有图标（磁贴上四颗、
    各处 `icon-btn`），否则面板上一排 `button.flex.items-center` 谁是谁都分不出。
- **回归**：`nav112.mjs`（20 条，**取代已删除的 `nav110.mjs`**）。改这个文件后必跑。


#### 2.2.8 弹窗里按「返回」键会连带退页（`.92` 起的老 bug，`.112` 修）

- **根因是监听阶段错位**：`spatialNav.js` 的返回键分支挂在 window 的**捕获**阶段、又注册在 App 挂载时，
  而 Modal 的 Escape 监听挂在冒泡阶段 —— 捕获先跑。于是「在详情里按返回键」会先派发一次 `ps5-back`
  （App 收到就把视图栈弹一格，从游戏页退回上一页），Modal 再关掉自己。
  用户看到的是**关弹窗的同时整页也退了**，退回去那页还是空的（视图已切走），看起来像崩了。
- **修法**：返回键分支先看 `document.querySelector('[role="dialog"]')` —— 有浮层就把这次返回留给它。
  **副作用正是想要的行为**：`dismissible={false}` 的浮层（安装 / 删除进行中）因此也拿回了返回键。
- 判 `role=dialog` 而不是维护一份「谁开着」的名单：全站只有 `Modal` 与多媒体预览用这个 role，
  判它等于判「有没有东西该先处理这次返回」，不需要任何跨层通信。


#### 2.2.9 Modal 的焦点进出（`.112` 修，`ui/Modal.jsx`）

- **必须是独立的、只依赖 `show` 的 effect**。原来把它和 Escape 监听写在同一条里、依赖数组带着 `onClose`；
  而调用方传的 `onClose` 多是内联函数（`GameDetailModal` 的 `close`），父组件每重渲染一次就换一个引用 →
  「cleanup 把焦点还给 opener → effect 再聚焦面板」这组动作在**弹窗还开着的时候**反复执行，
  只要有一次 cleanup 之后 effect 没跟上，焦点就留在外面了。
- **症状**：电视端遥控器打开详情后光圈/焦点还停在磁贴上，方向键一动就把弹窗绕过去。
- **修法**：opener 用 `useRef` 记（不参与依赖），还原只发生在 `show` 由真变假的那一次 cleanup 上。
  不还焦点的后果不是「多按几下」—— 下一次落焦会从全屏最左上重新开始，用户直接被甩进侧栏菜单。


#### 2.2.10 存档配置的「全局 vs 单游戏」切分（`.145` 定稿，改存档 UI 前必读）

- **原则：一个控件只有一个家。** 存档相关的**配置**全部归**设置页**「存档配置」
  （`views/ArchiveConfigModal.jsx`），三块：① 远程位置（协议 / 主机 / 共享 / 账号）；
  ② 自动存档（总开关 + 启动时拉取 + 退出时回传）；③ 本地快照（总开关 + 间隔 + 保留份数）。
  单游戏弹窗（`game/SaveManagerModal.jsx`）里**只剩只读状态 + 动作按钮**
  （立即拉取 / 立即推送 / 立即备份 + 存档列表 + 快照列表 + `auto_state` 状态行），
  **一个 switch、一个输入框都没有**。
  `.142` 之前这三块都在每个游戏弹窗里；`.144` 先搬了远程位置；`.145` 把另外两块也搬了。
- **「全局生效」的判据是「那份配置写过没有」**，不是「填了几个字段」。前端
  `saveConfig.js` 的 `hasGlobalArchive(config)`（主配置里 `ARCHIVE_GLOBAL_CONFIG` 是
  非空串）与后端读 `ARCHIVE_GLOBAL_PATH` 那个文件在不在，是**同一个判据**。
  反过来（按内容判）会出现一种很难解释的行为：用户在设置页把主机清空保存（本意是
  「别同步了」），却悄悄回落到这个游戏很久以前存的旧主机上继续推。
- **优先级：全局写过 → 全局说了算；没写过 → per-game（老装机行为不变）**。
  前端 `resolveAutoForGame(config, perGameAuto)` / `resolveRemoteForGame(config, perGameAuto)`，
  后端 `game_auto.c` 的 `ga_load_cfg()` / `game_saves.c` 的 `gs_snap_cfg()` ——
  **四处必须同一口径**，否则就是「UI 说开着、引擎没跑」这种最难查的不一致。
- **后端真的读全局，这是 `.145` 与 `.144` 最大的区别**。`.144` 的「全局生效」是前端在保存
  时把全局合进 per-game 请求 —— 用户不进那个游戏点一次保存就还是不生效。`.145` 改成
  引擎直接读全局文件，于是**用户不进任何游戏弹窗、引擎也按全局跑**。
  - 全局配置**单独一份文件**（`ARCHIVE_GLOBAL_PATH` = `/data/nayeplmtools/archive_global.json`），
    不塞进 `nayeplmtools_config.txt`：那是 `key=value` 行式、`config_upsert_value` 的行缓冲
    只有 256 B，六百多字节的 JSON 写进去会被**截断成半个**（静默失效）。
  - `config_handle_set_json()` 里必须用 **`cJSON`** 取 `ARCHIVE_GLOBAL_CONFIG`，**不能**用
    老的 `strstr` 找引号：这个值是内容为 JSON 的 JSON **字符串**，里面全是 `\"`，
    手扫会在第一个内层引号处截断。
  - `/get_config` 要把它发出去，否则前端每次刷新就丢，用户填的全局配置看起来像没保存。
- **快照枚举在全局模式下换口径**：`gs_snap_enabled_games()` 在全局开启时**扫存档目录
  枚举游戏**（`gs_all_tids()`），而不是从 per-game 配置里挑 —— 因为「该快照的游戏」里
  绝大多数在配置里**根本没有条目**，照旧枚举的话全局开了也一个都扫不到。
- **`config` 要传到单游戏弹窗**（`App → GameManageView → GameSection → SaveManagerModal`），
  且 `App` 进 `game_manage` 视图时补一次 `refreshConfig()`：全局配置是被**另一条路**改的
  （FTP / 另一台设备）时，进页要能拿到最新的。
- **全局配置的密码与 per-game 不同**：全局没有「后端不回传密码」的限制（它是我们存进
  `/set_config` 的 JSON），原样保存 / 回显；per-game 那份后端永不回明文。
- **mock 状态要跟着清**：`/reset` 必须清 `ARCHIVE_GLOBAL_CONFIG`，否则上一节设的全局远程
  会串到下一节的单游戏弹窗（显示「来自全局设置」而不是「本游戏已配置」）→ 假红。
  mock 里 `pgAutoFor(tid)` 是 per-game 原始那份（**写入路径必须用它**），`autoFor(tid)`
  是全局优先合并后的**副本**（往副本上写会静默丢）。
- **回归怎么盯**：`game112` 第 5 节有一组反面断言专门盯「旧 UI 有没有删干净」
  （0 个 `button[role=switch]`、无「保存设置」、无 `input[type=text|password|number]`）；
  5d 验「全局三项同时对单游戏生效」，分**显示面**与**引擎面**两面；
  5e 验设置页那个弹窗本身（三块都在 + 保存真写回 `ARCHIVE_GLOBAL_CONFIG` + 重开仍在）。
  改这块 UI 后**这五节必须一起过**。


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

**主机端单测（一条命令跑齐）**：
```bash
wsl bash -c "cd /mnt/d/pldmanager/ps5-payload-manager && bash test/run_host_tests.sh"
# slot_resolve 23/23 + offact_genid 13/13 + safety_origin 39/39
# + check_post_routes 静态守卫（.138 起，最前面）
# + media_scan 178/178 + game_saves 88/88 + game_mgr 100/100（.127：65 → 90 → .136：100）
# + param_rename（真机夹具，缺夹具则 SKIP）
# + media_verify 的 json 与 4 次 zip 校验（子集 2 / 全量 5 / 大文件跨分块 / 同秒去重）
# clang -Wall -Wextra 零告警
# 注：game_mgr 的用例现在覆盖三类来源（pkg / image / folder），夹具见 test/make_appdb_fixture.py，
#     它同时造 /user/app 与 /system_ex 两棵树，编译期用 -DGAME_MGR_APP_BASE / -DGAME_MGR_SYSTEM_EX
#     把这两个根指到 /tmp/pldmgr_gm_test/ 下（见 run_host_tests.sh），否则会去 stat 真机的绝对路径。
```

**构建产物上的浏览器回归（`.125` 起，出包前跑一次）**：
```bash
cd frontend && node mock-server.js &          # mock 8081
node serve_dist.mjs &                          # frontend/dist 静态 + 其余反代到 mock，5200
node pack125.mjs                               # 断言：无未捕获异常 / 轮询 ≥2 次 / 进终态 / 出下载按钮 / 下载端点真回字节
```
> **为什么必须另起这一套**：`game112.mjs` / `nav112.mjs` 跑的是 **vite dev server**，
> 发出去的是原生 ESM（`async/await` 不降级）；而真机跑的是 Babel 降级后的**构建产物**。
> `.125` 那个「打包完但下载按钮永不出现」的 bug **只存在于产物里**，
> 177 项回归全绿也照样漏。凡是「构建期改写」引出的行为，都必须在产物上验。
> 两个脚本都在 `~/.workbuddy-ai/binaries/node/workspace/`（与 `game112.mjs` 同目录）。
> 本机环境挂着 `HTTP_PROXY`，跑浏览器回归要给 Chrome 加 `--no-proxy-server`、
> 给 node 设 `NO_PROXY=127.0.0.1,localhost`，否则连 127.0.0.1 都会被绕出去。

**真机取证 / 推包 / 验收（`.125` 起）**：
```bash
# 取证三条通道（都通，别只靠用户转述）
#   FTP  192.168.1.214:2121  anonymous / guest@example.com，被动、binary
#        → _ftp_probe/pull_evidence.py，产物落 _ftp_probe/evidence/
#          （pldmgr.log / av_content.db / av_contents 目录树 / 封面文件本身）
#   HTTP 192.168.1.214:8911  面板自己的接口，可直接拉成品 zip 校验
#   HTTP 192.168.1.214:10101 ShadowMountPlus 1.7beta4 自己的 Web UI + JSON API
#        —— 全部路由是 **POST + JSON body**（GET 会 400）。`POST /api/v1/games`
#        空 body 即返回全量真值（source_type / image_type / path / runtime_path /
#        mounted / installed_pkg）。**任何「跟 SMP 对齐」的需求都打这个接口逐字段比，
#        别照文档猜**（`.127` 就是这么定位 + 验收的，`_ftp_probe/badge_verify.py`）。

# 推包：环境里没有 socat / docker，deploy.sh 跑不了，用 _ftp_probe/push_elf.py
#        （① GET /shutdown 置 http_keep_running=0 → 轮询等 8911 关闭
#          ② Python socket 把 ELF 灌进 :9021（elfldr），对端回话
#             "[PLDMGR] Starting NayePlmTools v<ver> on port 8911..."
#          ③ 轮询 GET /version 确认）
python _ftp_probe/push_elf.py <elf路径> 0.5.2.127

# 验收：跑在真机内嵌的构建产物上
node device125.mjs                             # 12 条断言：版本 / 视频封面取真帧 / 打包轮询≥2 / 终态 / 下载按钮 / 下载端点真回字节
node device127_badges.mjs                      # 17 条断言：版本 / 15 张磁贴 / 镜像类=镜像+删除源 / 文件夹类=文件夹+无已挂载 / 真PKG=已安装PKG+无源提示 / 全列表反面判据
```
> **推包前必须 `/shutdown`**，否则旧 payload 仍占着 8911，新的起来绑不上。
> 两个磁贴变体（`-me` NAYE00001 媒体栏 / `-ga` NAYE00004 游戏栏）**都深链到同一个
> `http://127.0.0.1:8911/`**，功能等价，推哪个都行。
> `device125.mjs` / `device127_badges.mjs` 与 `pack125.mjs` / `serve_dist.mjs` 同目录
> （`~/.workbuddy-ai/binaries/node/workspace/`）；给 node 的 `NO_PROXY`
> 要带上 `192.168.1.214`。
> **推包脚本的 stdout 会被终端缓冲吞掉**：`python push_elf.py` 若被超时杀掉会看不到任何输出，
> 但**推送其实已经成功**。判断成功只认一件事 —— 再打一次 `GET /version` 看版本号有没有变。
> 想让它别被杀，用后台方式跑（`run_in_background`）或加 `python -u`。

**变异测试（`.124` 起，出包前手动跑一次）**：
```bash
wsl bash -c "cd /mnt/d/pldmanager/ps5-payload-manager && bash test/mutation_check_media.sh"
# 把五处关键改动逐条掐掉，确认 media_scan 真的会红：
#   iconpath（掐掉认 icon_path 那条路）→ 期望 >=2 条红
#   magic   （掐掉文件头魔数校验）    → 期望 >=4 条红
#   nolgate （掐掉 gj_ready 门禁）    → 期望 >=1 条红
#   noplain （压缩包退回平铺布局）    → 期望 >=4 条红（.126，实测 7）
#   nodedup （掐掉同秒撞名去重）      → 期望 >=1 条红（.126，实测 2）
# 不接进 run_host_tests.sh：它要跑好几遍完整编译，偏慢。
# ⚠️ 脚本会把被变异的那个文件**顶掉** SRCS 里的原件，否则重复定义（game_job.c 就在 SRCS 里）。
# ⚠️ 改过 game_media.c 里被 sed 当锚点的那几行之后，记得同步更新本脚本的表达式

wsl bash -c "cd /mnt/d/pldmanager/ps5-payload-manager && bash test/mutation_check_game_mgr.sh"
# 游戏来源分类那一套（.127 新增），四刀全红才算数：
#   srcdsrc（把 source_type 退回 a.d_src）→ 期望 >=6 条红（实测 8）
#   imgtype（把 image_type 恒置空）      → 期望 >=1 条红（实测 2）
#   pathimg（掐掉 mount_img.lnk 那条路）  → 期望 >=1 条红（实测 2）
#   mounted（mounted 退回 runtime[0]!=0）→ 期望 >=1 条红（实测 1）
```
#    （锚点没了它会直接报「补丁没打上」，不会静默放过）。
```
- 前三个测试**不链接 PS5 SDK**，只打桩 `sceRegMgr*` / `sceUserService*`，所以主机 clang 直接编。
- `safety_origin_test.c` 必须带 `-DSAFETY_DIR='"/tmp/safety_origin_test"'` —— 真实的 `safety.c` 会真的去建目录写文件，不改这个宏主机上跑不了。脚本里已经带上了。
- **第四个 `media_scan`（`.115`）比前三个重**：要 python3 现造夹具（`test/make_media_fixture.py`，
  用标准库 `sqlite3` 造一个**列序照抄真机**的 `av_content.db` + 它引用的真实文件，库设成 `0444`），
  产出的 zip 还要 python3 复验（`test/media_verify.py`）。**没有 python3 就整段跳过，不半跑。**
  五个路径宏用 `-D` 指到 `/tmp/pldmgr_media_test`，与 `-DSAFETY_DIR` 同一个手法。
- `log_server.c` **不参与主机链接**（它的头要 `microhttpd.h`，主机上没有），`pldmgr_log` 由
  `test/media_scan_test.c` 自己提供、默认静音，要看日志 `export MEDIA_TEST_VERBOSE=1`。
- Windows 侧调用要用 `MSYS_NO_PATHCONV=1`（或 `wsl bash -c "…"` 把整条命令括起来），否则 Git Bash 会把 `/mnt/...` 当 Windows 路径改写掉。
- ⚠️ **`npm.cmd run lint` 的既有基线是 28 个 error**（`App.jsx`、各 `*View.jsx`、`vite.config.js` 的
  `no-unused-vars` / `react-hooks/set-state-in-effect`，`mock-server.js` 里 Node `process` 的
  `no-undef` 属 globals 配置的既有误报）。**验收线是「本次碰过的文件零报错」**，不是「全局绿」——
  拿 `npx.cmd eslint <改动的文件>` 看，别被那 28 条历史遗留吓退。

**出包三个坑（`.107`/`.108`/`.110` 实拍）**：

1. **`build_tiles.sh` 会复用已有的 `frontend/dist`，而标题占位符是「一次性消费」的。** 标题注入发生在 `$(ASSET_HEADER)` 这条 recipe 里（`Makefile:90-100`），它 `sed` 的是 `frontend/dist/index.html` **本身** —— 第一次跑完 `[[TITLE_PLACEHOLDER]]` 就没了。所以**改了代码要重出包时，必须先 `rm -rf frontend/dist` 再 `npm run build`**，否则 `make` 里那句 `sed` 是空操作，标题会**停留在上一次的短哈希**（`.107` 就撞上过：包内是 `.107` 的代码，标题却写着上一个提交 `6b51ee7`）。
   - **同一次 `build_tiles.sh` 内部也会撞上这条**：`[1/3]` 出 `-me` 时把占位符消费掉，`[2/3]` 出 `-ga` 前虽然 `rm -f include/assets_*.h` 强制重建头，但 `sed` 已经找不到占位符了 —— 于是 **`-ga` 包里的标题（含短哈希与时间戳）就是 `-me` 那次注入的**（`.108` 实拍：日志里 `-ga` 写 `07:33:06`，包内实际是 `07:31:39`）。同一次运行里两个包本来就该同名，无害；**但两趟构建之间如果跨了提交，`-ga` 会带着 `-me` 的旧哈希**。核对时别只看日志。
2. **必须先提交、再出包。** 标题里的短哈希取自 `git rev-parse --short HEAD`，未提交的改动会让标题指向错误的提交。（`git diff --quiet || COMMIT="DEV"` 这道闸在 WSL 里常被 CRLF 归一化绕过，所以别指望它拦住你。）
3. **尺寸相同 ≠ 旧包。** ELF 按 16 KB 页对齐，不同版本常是同尺寸（`.102`/`.106`/`.107`/`.108` 都是 9,875,208）。必须从二进制内容核对：内嵌标题短哈希、`PLDMGR_VER:`、titleId、以及本轮新增的字符串标记。**`.108` 还多一条「反向断言」**：`grep -ac 登出并反激活 xxx.elf` 必须是 **0** —— 这轮改的就是「界面不许出现这个词」，只在源码里删掉不够，得确认它没被别的语言包/旧 dist 带进包里。

- 防缓存：`cache.appcache` 每次注入构建时间戳触发 AppCache 更新。
- 主 ELF 内嵌版本签名 `PLDMGR_VER:<版本>`（`src/main.c`），可用于核对任意产物的实际版本。
- 约定：界面/功能改动**定稿确认后**再出正式构建包；中途验证走 dev-server 预览。
- **出包前必做（否则标题烙成 `DEV`）**：Makefile 用 `git diff --quiet || COMMIT="DEV"` 判脏树，而 WSL 里的 git 没继承 Windows 的 `core.autocrlf=true`，会把 78 个 CRLF 工作区文件看成已修改 —— 哪怕刚提交完、Windows 侧 `git status` 全干净，WSL 照样判脏。构建命令前加一次性归一化（**不要写进 git 配置**）：
  ```bash
  export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=true
  ```
  出包后从二进制核对：`grep -ao 'v<ver> by naye ([0-9a-f]*' xxx.elf | tr -cd '[:print:]\n'` 应为真实短哈希，出现 `DEV` 就得重出。
- **加了新 POST 路由，必须同时在 `gr_post_limit()` 登记**（`.137`/`.138` 的教训）：
  `src/http_server.c` 里「收不收请求体」（`gr_post_limit`）与「收到之后怎么处理」
  （`gr_dispatch_post`）是**两张分开写的表**。只加后者不加前者的后果**不是** 404/500，
  而是请求体压根不收 → `gr_dispatch_post` 永不触发 → **客户端连接被直接关掉**
  （`RemoteDisconnected`），真机上极难反推。mock 里没有这一层、主机套件也编不了
  （要 `microhttpd.h`），所以**必须**靠静态守卫兜住 —— 已接进 `test/run_host_tests.sh`
  最前面（毫秒级），也可单独跑：`bash tools/check_post_routes.sh`。
- 预览电视端形态：`http://localhost:5173/?device=ps5`（`utils/helpers.js` 的 `isPS5` 认这个 query）。命中后与真机同一套分支 —— 侧栏常显、底部栏隐藏、`html[data-device="ps5"]` 抬命中区尺度、方向键空间导航接上。真机靠 UA 命中，不看该参数。
- **前端改动的验收线是 `npm.cmd run lint` + 逐页实测，不是 `vite build`**：esbuild 不做作用域检查，漏 import / 残留已删 state / 引用越界变量在构建期全绿、运行期才炸（`v0.5.2.98` 修的四起整站死全是这类）。`mock-server.js` 里 Node `process` 的 `no-undef` 报错属 globals 配置的既有误报。

## 4. 版本号同步（全链路）
`include/pldmgr.h` 的 `MENU_VERSION` 是**唯一真源**；`make` 会把它注入前端标题与产物文件名，运行时 `/version` 路由、启动通知与通知文案都读同一宏。**改版本号只改这一个地方。**
> 早期文档列的「仍需手动同步的 4 处」（mock 的 `/version`、`DonateView.jsx` 兜底串、两份 `locales-overlay/*.json` 的 `settings.donate_desc`）**自 `.113` 起已全部改成自动派生**，不必再手抄：
> `mock-server.js` 现读 `pldmgr.h` 的 `#define MENU_VERSION`；`DonateView.jsx` 用 `__APP_VERSION__`（`vite.config.js` 从同一个宏取）；两份语言包的 `settings.donate_desc` 用 `{{version}}` 模板。**再看到有人手抄版本号，就是回退。**
**出正式包入口在构建脚本层，勿手改单个产物。**

## 5. Fork 维护约定
1. 所有 fork 专属改动打 `// [CN-FORK]` 注释标记；中文措辞/新增 key 只进 `locales-overlay/`，**绝不改 locales 子模块**。
2. UI 配色分两层，都不改上游组件类名：**先改令牌层**（`tailwind.config.js` 的 `theme.extend.colors` + `index.css` 的 `:root` CSS 变量）——类名保留、值替换，全站一次生效；**再补重映射**（`theme/light-theme.css`）把上游写死的暗色工具类映射为浅色语义色。骨架层（`.nav-group-title`/`.nav-item`/`.page-head`/`.head-bar`/`.btn-*`/`.icon-btn`/`.row-link`/`.field`/`.field-ro`/`.field-edit`/`.field-btn`/`.switch-*`/`.segmented-*`/`.stack-x`/`.sel-chip`/`.tile-grid`，定义在 `index.css` 末尾）颜色一律走 CSS 变量，浅色只在 `light-theme.css` 翻变量，不写 `!important`。**命中区与行高不写死 px**：`--ctl-h`、`--row-h`、`--tile-w`、`--switch-*` 在 `:root` 与 `html[data-device="ps5"]` 各一档，组件只引用令牌。**动作主色自 `.104` 起是朱橙 `#F43E06`**（深档文字/图标档 `#FF7043`、浅档 `#C2330A`）—— 历史沿革：`.85` 亮橙 → `.100` 蓝 `#2563EB`（用户选定「整套照搬含蓝主色」，当时明确解除了「主色不得为灰或蓝」那条旧约束）→ `.104` 按用户给的色值换回朱橙。**主色只允许出现在两处**：`.btn-primary`（这一页唯一的主操作）与 `.sel-chip`（当前选中）。**状态类按钮（同一颗按钮随状态换语义）照 `.104` 的账号板块做法**：建立状态 = 绿档、撤销状态 = 红档（可随时重做的开关回中性档），见 `AccountToolsSection.jsx` 的 `BTN_GRANT` / `BTN_REVOKE`。**其余按钮自 `.103` 起一律中性/淡染档**（`.btn-outline` 中性、`.btn-ghost` 无底、`.btn-info` 信息、`.btn-danger` 淡红、`.btn-ok` 淡绿、`.btn-warn` 淡金，令牌 `--btn-*`），用户口径：「只留主操作上色」；`.101` 的 `--color-secondary` 与 `.102` 的 `--fill-*` 六色实心档**都已删除**，加按钮请照 `.103` 的角色表走。
3. 升级上游：`git fetch upstream && git checkout main && git merge --ff-only upstream/main`，再 `git checkout cn-fork && git rebase main`。冲突集中在带 `[CN-FORK]` 的局部插入块。
4. third_party 基线 ELF 需 `git add -f`；`include/assets_*.h`、`frontend/dist/`、根目录 `*.elf` 属构建产物，始终不入库。
5. Commit 规范：`[CN-FORK] v<版本号>: <描述>`；**只本地提交，不配置 remote、禁止 push**。

## 6. 关键约定 / 易踩坑（速查）
- **别把大块内存放在栈上（安全红线，已栽三次）**：真机的 HTTP 是 `MHD_USE_THREAD_PER_CONNECTION` —— 每条连接一条 pthread 线程，栈走 **真机 pthread 默认值，只有 64KB 上下**（宿主上量出来的临界点：整条 `http_on_request → …` 的链超过 **56KB** 就 SIGSEGV）。`.116`（`tt_apply` 的 `unsigned char[65536]`，改磁贴图标/背景死机）、`.141`（`gs_copy_file` 的 `char buf[32*1024]` + `gs_root_t roots[16]` 11.4KB，点「立即备份」打死面板）都是这个。**两次都只在真机上炸、宿主测试全绿** —— 宿主跑在主线程的 8MB 栈上，这点数组不构成压力。规矩：**任何函数栈帧不许超 32KB**，大缓冲一律 `malloc` 一次、整条链共用（参考 `src/game_saves.c` 的 `gs_cp_t`）。两条守卫：`test/game_saves_test.c` ⑩（**在 32KB 线程栈上跑完整条快照链**，带负向对照）+ `tools/check_stack_frames.sh`（静态上限 32KB，已接进宿主套件）。连接线程栈已在 `main.c` 提到 1MB（`MHD_OPTION_THREAD_STACK_SIZE`，**参数是 `size_t` 不是 `unsigned int`**，传错会静默退回默认栈），但**自己 `pthread_create` 的守护线程不受它管**，要单独 `pthread_attr_setstacksize`。**新写这类「只在真机上炸」的路径时，先写一条小栈线程用例**，别等真机反馈。

- **存档的远程同步目前只做 FTP + HTTP；但「SMB 做不到」是错的（2026-10-07 复核）**：SDK 那份 `libcurl 8.18.0` 确实没编 `smb`（协议表：`http/https/ftp/file/dict/imap/ldap/pop3/smtp`）—— 那只是说**走 libcurl 的这条路**不通。**PS5 上 SMB 是可以做的**：`libsmb2`（SMB2/3 用户态客户端）是 `ps5-payload-dev/pacbrew-repo` 里的正式包 `ps5-payload-libsmb2`，上游 `ps5-payload-dev/websrv` 就在用。已实测：用本仓库同一套工具链（`prospero-clang` + `prospero.cmake`）能把 `libsmb2 6.2` 编成 `libsmb2.a` 并链进 PS5 ELF，写路径符号齐（`smb2_write`/`smb2_open(O_CREAT)`/`smb2_unlink`/`smb2_mkdir`/`smb2_rmdir`/`smb2_rename`/`smb2_fsync`），协议覆盖 SMB 2.0.2–3.1.1。代价 **+约 241 KB**（约 +2.2%）。所以正确说法是**「当前构建没编入 SMB」**，不是「SDK 不支持」。默认值已前后端 + mock 一起改成 `ftp`；UI 上 SMB 一档是**看得见、点不动、写明原因**（不从选项表里删 —— 老用户存的就是 `mode:"smb"`，删了会变成「一个都没选中」）。远端布局 `<base>/<TID>/<uid>.zip`（一个用户一份、文件名恒定、整份覆盖），推与拉**逐字一致**。改传输层前先读 `include/save_remote.h` 与 `include/game_auto.h` 的文件头（四条硬约定 + 三条刻意设计）。
- **同一时刻只能有一次存档同步**：`src/game_auto.c` 的 `g_sync_mu`。**这是正确性要求不是性能优化** —— 「整份替换」= 「旧的改名 → 换上去 → 删旧的」，并发跑同一目录会互相把对方刚改名的目录当垃圾删掉。守护线程（退出推送）与手点「立即推送」完全可能同时发生。
- **「远端还没有这个文件」不是失败**：`SR_ENOTFOUND (-2)` 单独一类。FTP 侧两种表现都要归进来 —— `CURLE_REMOTE_FILE_NOT_FOUND` 与 **`CURLE_REMOTE_ACCESS_DENIED`**（远端根目录不存在时 `CWD` 得到 550，libcurl 报的是后者）。首次使用必然遇到，给用户一条红报错是不对的。
- **curl 的全局初始化在 `src/curl_init.c`，别搬回 `utils.c`**：`utils.c` 被一堆纯文件系统的宿主测试链着（game_saves / media_scan / game_mgr），`curl_global_init` 一进 `utils.o` 它们立刻 `undefined reference`。宿主测试要 curl 头时**只软链 SDK 的 `curl/` 一个目录**（`-I/tmp/pldmgr_curl_inc`），**直接 `-I` 整个 SDK include 会把 glibc 的 `stdio.h` 盖掉**，链接期一片 `undefined reference to __error / __stderrp / _CurrentRuneLocale`。
- **i18n 键重名是静默失败（`.142` 栽过）**：JSON 与 i18next 都取最后一个，前面那条被无声吃掉 —— 新增 `game.f_path`（远程路径）撞上详情弹窗已有的 `game.f_path`（物理路径），详情里那一行当场变成「远程路径」，而当时所有断言几乎全绿。存档的字段键在独立命名空间 `game.sf_*`；`game112` 第 11 节有一条「两份语言包都不许有重复键」的静态守卫。
- **`Segmented` 支持 `disabled`（`.142`）**：选项对象上的 `disabled: true` 让那一档点不动、且不显示选中档；`title` 给原因。用来表达「这个选项存在但当前环境用不了」，比从选项表里删掉好。
- **mock 的 `/reset` 必须把「会串场的东西」清干净（`.142` 补齐）**：`snaps`（快照，`.140` 加的时候漏了）、`autoState` / `remoteFiles`（`.142`），以及 `/loadpayload:` 起过的 10101 程序桩。漏了的话，同一个 mock 进程跑第二遍 `game112` 必红（5b「还没做过快照时列表是空的」、1a「10101 必须真的没人监听」）—— 而这两条恰恰是各自那节立足点的前置。
- **风扇温控（安全红线）**：只通过 `/dev/icc_fan` autoservo ioctl 设「目标温度」交固件闭环（GET `0xC01C8F08` / SET `0xC01C8F07`，28 字节对象，byte[2]=selector、byte[5]=目标 °C）。必须 GET → 只改 byte5 → SET 整个对象 → 回读校验，**绝不全零 SET**；目标夹 60~85 °C。进游戏/待机会复位目标，靠常驻守护线程（`FAN_INTERVAL` 默认 7s）拉回；`safe_mode` 短路。
- **自启**：`autoload.txt` 延时行 `!` 前缀 + 前导空行需自愈规范化；安装 payload 后同步登记到 `ps5_autoloader`（全局覆盖）。
- **网络**：用户态代码用 socket 前必须 `sceNetCtlInit` + `sceUserServiceInitialize(256)`。
- **传感器/硬件**：`sceKernelIccGet*` 会 crash、Sony 传感器 API 非可重入 —— 这是 `.64` 下线硬件信息的根因；`/dev/dce` ioctl 默认停用 + `safe_mode` 哨兵文件用于防死机。
- **提权**：官方范式只改 authid/caps 且用完还原（勿改 uid/prison/rootdir）。**已知偏差（待裁决）**：`.99` 移植的 `src/psaccounts/sys_lite.c` 里 `escalate_creds()` 沿用 `D:\offact-new` 已测行为——一次性提满 root + jailbreak 且不还原，因为写 SysReg 与 `/system_data/priv` 头像路径都需要它。这是移植保真，不是本 fork 的新增风险；若要收口成官方范式，必须先实测账号写入是否仍能成功。
- **前端**：Vite 未代理的 API 会被 SPA fallback 吞成 index.html → 页面转圈；`!text-white` important 变体、tailwind 字面色需单独映射。**`.131` 的教训**：新端点只要不属于既有代理前缀（`'/game_'`、`'/api'`…），**必须**在 `vite.config.js` 里补一条代理 —— 漏了不报错，`res.json()` 抛错被 `.catch(() => null)` 静默吃掉，探活永远读到 `undefined`，表现是「点按钮没反应」。自查：`curl http://127.0.0.1:5199/<新端点>` 看回的是 JSON 还是 `<!doctype html>`；改完 **必须重启 vite**（`--strictPort` 那个进程），热更新不覆盖 proxy 表。
- **浏览器回归里定位按钮别用会变的 `aria-label`**（`.131` 踩到）。`GameDetailModal` 那颗 SMP 按钮的 `aria-label` 是 `smpStarting ? smpPhase : '去 ShadowMount 管理'` —— 点击后 React 一重渲染，`button[aria-label="去 ShadowMount 管理"]` 就解析到 **0 个元素**，`innerText()` / `isDisabled()` 全抛错被 `.catch()` 吞掉，断言报「整个等待窗口都没看到进度文案」，看起来像产品没渲染，实际是选择器脱锚了（同批的 10101 跳转断言照样全绿）。修法：用**稳定 class**（`button.btn-outline.w-full`，详情弹窗里只有它 + 右上关闭的 `.icon-btn`），再单加一条断言校验初始文案确实是「去 ShadowMount 管理」。同理：`window.open` 的 `popup` 事件比 React 状态重渲染**早到**，t=0 读一次读到的还是旧值 —— 要在那 ~2.5s 拉起窗口里轮询采样。
- **PS5 老内核三条硬约束**：① flex 盒的 `gap` 不生效（`grid` 的 `gap` 生效）——横排/竖排间距一律用 `* + *` 外边距或 `.stack-x`；② 不支持 `@layer`，`index.css` 顶部那批 `LEGACY FALLBACKS` 是给老内核兜底的关键工具类，别删；③ `animate-in / slide-in-from-*` 之类是死类名（`tailwindcss-animate` 未安装），别指望它做动画。
- **网页无法移动系统/浏览器鼠标指针**（Pointer Lock 只能锁不能移，合成 `mousemove` 也移动不了真光标）。凡是「让鼠标跟着某某走」的需求，先翻译成用户要的**结果**再选路。
- **电视端导航：我们不接管键盘，光圈跟着指针走**（`.112` 定稿）。方向键**不** preventDefault、不做几何搬焦点；`.is-focus` 挂在**指针当前压着的可操作元素**上（指针压在图上就向上找最近的按钮祖先）。**不要再加「自己搬焦点 / 抢确定键 / 藏系统光标」的逻辑** —— 那三层 `.92`→`.110`→`.111` 越补越别扭，`.112` 已整层回退，见 §2.2.7 的链条表。返回键例外：仍由我们派发 `ps5-back`，但**屏上有 `[role="dialog"]` 时留给浮层**（见 §2.2.8）。
- **账号的稳定身份是 uid，不是名字**。名字会变（改名）、会滞后（`sceUserServiceGetUserName` 对刚激活的账号首帧取到空串或旧名）、而且**全局唯一**（拿错就会指到别人的槽）。所以两处一律 uid 优先：① 解槽位 `find_slot_by_uid()` 优先、`find_slot()` 兜底（见 §2.1.1 的①，这是「第一次登出失败」的正解）；② 派生账号 ID 用 `offact_gen_id_for_user(uid)` 而不是 `offact_gen_id(name)`（见 §2.1.1 的②）。栽过两次，别再犯。
- **`rem` 不等于 `body` 的字号**：本仓库 `body { font-size: 20px }` 写在 ≥768px 的媒体查询里，
  而 **`html` 从未设过字号** —— `getComputedStyle(document.documentElement).fontSize` 恒为 16px，
  一切 rem 类（含 Tailwind 的 `text-sm`/`text-xs`）都按 16 换算。所以**判断字号有没有真的变大，
  只能量目标元素的 `getComputedStyle().fontSize`，不能推算根字号**；`.113` 第一版就是按「rem = 20px」
  推算，写出的 0.9rem 实际 14.4px，等于没改。同理，量「菜单档」要量菜单标签那颗 `<span>`（16.5px），
  不是 `.nav-item` 按钮本身（它继承 body 的 20px）。见 §2.2.6 末尾。
- **弹窗 / 面板内部字号走四条语义档，不再写 `text-[13px]` 这类硬编码 px**：
  `.panel-body`(1rem) > `.panel-note`(0.875rem) > `.panel-tiny`(0.8125rem) > `.panel-micro`(0.75rem)，
  定义在 `index.css` 一处；芯片高度配 `.chip-xs`（跟字号走，写死 18px 会把汉字裁一半）。
  加新弹窗时**直接用这四个类**，别再造一套 px。
- **「上传」与「生效」是两步时，界面上就写成两步**（`.113` 改名页两层图）：选图 → 暂存（本地 `objectURL`
  预览 + 标「待应用」）→ 点「应用」才提交，另有「取消」。两颗同名按钮的**可访问名必须带层名**
  （`应用（磁贴图标）` / `应用（磁贴背景）`）；⚠️ 自动化别用 `:has-text("应用")` 定位，
  它会一起命中「**待**应用」芯片（子串匹配）。
- **画面上出现的版本号一律不要手抄**：读 `pldmgr.h` 的 `MENU_VERSION`（前端经 `__APP_VERSION__`，
  mock 与 `vite.config.js` 同一手法现读头文件）。`.113` 就是升版本时冒出 3 处手抄的旧版本号
  （其中 mock 的 `/version` 对不上会让本地联调**莫名其妙地刷页面**）。回归里有一条守卫钉着，
  见 `game112.mjs` 第 11 节。
- **`!el || <断言>` 是给自己留的后门**，本项目已踩三次（光圈落点、预览 `object-fit`、
  暂存预览的坏 PNG）。写法：先**另加一条**断言确认 `el` 真的在（图要 `naturalWidth > 0`，
  `complete` 不算 —— 坏图它也是 `true`），判它属性的那条才有效。
- **C 字符串**：分段字符串字面量不要拆断 JSON key（会导致设置页开关失灵）。
- **读系统 SQLite（`game_mgr.c` 与 `game_media.c` 共用的算法，`.115` 栽过一次）**：
  - **一张表要走两遍**：page 1 是 `sqlite_master`（只有建表语句），数据在 `rootpage` 那一页。
    拿到列序就直接 walk page 1 的话，症状是**「库读到了、列号全对、`count` 却恒为 0」**。
  - **TEXT 是奇数 serial type、BLOB 是偶数**，所以 `kind = (st & 1) ? 2 : 1`（2=字符串）
    看着像写反、其实对。实测：`TEXT 'hello'`→23、`TEXT 'x'*100`→213、`BLOB b'\x01\x02'`→16。
    **别顺手「修正」它** —— `game_mgr.c` 读 app.db 同靠这一条，改了两个模块一起哑。
  - **序列长度不是 `st`**：只有 1..4 是「长度==类型号」，5 是 6 字节、6/7 是 8 字节。
- **cJSON 的字符串随树死**：`valuestring` 挂在解析出来的树上，`cJSON_Delete()` 之后再用就是
  use-after-free（`.115` 的 `ids` 就是这么坏的，症状是「按 id 打包恒定失败、全量打包却正常」）。
  **要在 Delete 之后用的，一律先 `strdup` 副本再释放树。**
- **任务状态 JSON 里每个自由文本字段都要 `pldmgr_json_escape`**（`error` / `reason` / `phase`）。
  只转一个等于留一个「某条失败原因里出现引号 → 整个响应体非法 → 前端 `.json()` 直接抛」的洞。
  状态缓冲也别太小：三段加起来最坏贴近 1000 字节，截断会让 `*_status` 返回 -1、
  路由当成「任务不存在」回 404 —— 界面上表现为一个凭空消失的任务。
- **删系统媒体库引用的东西，只删文件、不写库**（`game_media.h` 铁律①）：SDK 里没有 sqlite3，
  手写 b-tree 删除等于拿用户全部截图去赌系统库不被写坏。库里的行留给媒体库自己清。
- **大文件一律 fd 交给 MHD**（铁律②）：录屏几百 MB，读进内存就是把面板 OOM 掉；
  response 创建失败时**要自己 `close(fd)`**，所有权只在创建成功后才移交。
- **SDK 的 `target/lib` 没有 zlib**（有 curl/mbedtls/microhttpd/pthread）。要压缩就得动构建链 ——
  所以 `.115` 的打包走自研 STORE zip（png/jxr/mp4 本身已压缩，deflate 只多花 CPU）。
- **`write()` 返回 0 不是错误，但也不推进**（`.122`）：`while (n > 0) { w = write(...); b += w; n -= w; }`
  在 `w == 0` 时**两个变量都不动**，就是一条纯自旋的死循环 —— CPU 打满、一个字节不落盘、
  任务槽永远停在「已开始、0 字节」。常规文件上罕见，**但真机是索尼自家的文件系统，不能拿 ext4
  的行为当保证**。凡 `read`/`write` 的推进循环，`w <= 0` 两条都要显式挡住（`EINTR` 只对 `w < 0` 重试）。
- **`printf` 到 payload 的 stdout 会永久阻塞**（`.122`，代价最大的一条）：stdout 是启动器给的
  socket / 管道，**对端不排空它**；缓冲一满 `printf` 就不返回。而 `pldmgr_log` 会在每个 HTTP 线程
  与后台打包线程里被调用 —— 一条日志卡死一个后台任务。修法是 `poll(POLLOUT, 0)` 先问一句，
  不可写就丢掉 stdout 这一份（内存环形缓冲 + 落盘 `pldmgr.log` 都还在，排查能力一点没少）。
  **`log_append_file()` 原来是在 `log_mutex` 里写盘的** —— `.123` 已把它挪到锁外
  （独立的 `file_mutex`）。**判据：`log_mutex` 是所有 `pldmgr_log` 的必经之路，而
  `pldmgr_log` 长在后台打包 worker 的路径上 → 一次慢盘写就等于全进程的日志一起堵，
  worker 会停在某一行日志上不再往下走。** 凡是「可能无限期阻塞的 I/O」都不许放进
  别人必须经过的临界区（stdout 那条是同一个错的另一副面孔）。
- **`setTimeout` 在 async 函数末尾 = 最脆弱的轮询写法**（`.122`）：`tick()` 里 `try { … onTick(st) … }
  catch { onTick(…) }`，**只要调用方的回调抛一次异常，第二次调用再抛就逃出 async 函数**，
  底下那行重排 `setTimeout` 永远执行不到 —— 轮询当场死掉，界面**永远停在第一次 tick 的快照上**，
  而且**一条报错都不显示**（异常是从轮询器里逃的，不是渲染出来的）。轮询器里调用外部回调
  一律先包一层 try/catch；另外「连续失败」要有尽头（本项目 60 秒），否则主机失联时面板会无限期吊住。
- **`ProgressBar` 的 `indeterminate` 语义是「总量还不知道」**，不是「阶段属于前期」（`.122`）：
  挂在 `phase ∈ {preparing, measuring}` 上的旧写法，会在总量 58.7 MB 已经知道之后仍然来回跑
  一条 `w-1/3` 宽的脉冲，右边百分比写着 0% —— 用户截图里那条「三成满」的条就是它。
  **一条不反映真实进度的动画比一条不动的更误导**；判据用 `!job.bytes_total`。
- **「界面卡住」这类反馈，先反推截图里的数字**（`.122` 的方法，比读代码快得多）：
  `bytes_total` 有值 → 已经过了 `gj_set_totals`；`elapsed == 0` → 这是**第一次** tick
  （`watchJob` 是同步执行的，不等 1s）；进度条 1/3 宽 → 走的是 `indeterminate`（`w-1/3` = 33.3%）
  → phase 是 `preparing`/`measuring`。三条一起就把范围压到「首次 tick 之后界面再没更新过」，
  而不是「后端在某个 syscall 里卡住」—— 这两者的排查方向完全相反。
- **视频的封面帧可能有两份，`.jxr` 那份解不动**（`.123`，`game_media.h` 头注释）：
  真机缩略图的命名约定是 `<原名>.<原ext>.<jxr|jpeg>` —— **同一个录屏旁边可能同时躺着
  一份 HDR 的 `.jxr` 和一份普通 `.jpeg`**，而库里 `icon_path` 只指其中一份。
  指到 `.jxr` 时 stb 解不动 → 回占位图 → 用户看到「预览图不对」。所以
  **「icon_path 能不能解」不能当作「有没有封面帧」的判据**：要在同一个缩略图目录里
  按 `<原文件名>.<后缀>` 再翻一份 stb 认得的位图（`md_find_cover`）。目录由 `id` 推，
  别用库里那几个 `title_id` 列（列序在固件之间会变）。占位图只在**真的找不到**时才用。
- **判断「界面停住了」要用两把尺子**（`.123`）：后端的 `idle`/`elapsed` 是**主机那边**的尺子，
  主机时钟一跳（或槽里的时间戳落到未来）`difftime` 就是负数、被钳成 0 → 界面永远显示
  「用时 0 秒、无任何异常」，正是用户报的「一直是正在计算中」。前端必须**另挂一把本机时钟**
  的尺子（记录最近一次真的收到主机数据的时刻，`transient` 重试不算），两把都不走才叫真卡住。
- **诊断信息要放在「界面定住也看得到」的地方**（`.123`）：界面卡住时界面本身已不再更新，
  所以证据必须走**不经过前端轮询**的通路 —— 内存环形缓冲（界面「日志」页）+ 落盘
  `pldmgr.log`。这就是 `md_watchdog` 那行 `STALL`（含 phase 与全部计数）与
  `game_media_pack_status` 的轮询心跳存在的理由：前者答「卡在哪一段」，后者答
  「前端还在不在轮询」—— 这两问一刀切开「后端堵住」与「前端停了」。
- **`icon_path` 是权威，别拿 `id` 去推目录**（`.124`，`.123` 栽在这上面）：`.123` 按
  `GAME_MEDIA_ROOT/thumbnails/<id 去掉前缀>` 推封面帧目录 —— 可 `game_media.h` §7a
  白纸黑字写着「录屏目录层级是否与 photo 同构」**尚未在真机验证过**，等于**拿一个没验证
  的假设当事实用**。而 `icon_path` 是**媒体库自己写进库里的**，指哪儿就是哪儿。
  凡「库里有字段直接指向某物」的地方，先信字段、再推路径；推出来的只作兜底。
- **扩展名不等于能解码**（`.124`）：`.123` 只按 `md_is_stb_image(md_mime_of(...))` 放行候选，
  于是可能挑中一个「名字像 JPEG、内容不是」的文件 → `stbi_load_from_memory` 失败 →
  返回 `-2` → HTTP 404 → **界面从「占位图」退化成「一块空白」**（比不修还差）。
  认位图要读**文件头魔数**（`md_magic_image()`，只读 12 字节）；扩展名只用来做粗筛。
- **视频（或任何「有兜底显示」的东西）永不 404**（`.124`）：`md_thumb_src` 里
  `md_abs()` 失败时**无条件**回 `-1`，视频跟着走了 404 —— 而视频本该回占位图。
  凡是「取不到真数据也有兜底」的分支，**先判类型再决定 404 还是兜底**，
  别让一条通用的 early-return 把兜底那条路堵死。
- **成品文件一律「先写 `.part`、写完 `rename`」**（`.124`）：`zip_write` 原来**直接写成品名**，
  写到一半时成品路径上就是一个 `size > 0` 的**残包** —— 而取成品的判据只是「文件在不在、
  大小 > 0」，一次并发（或用户手快重按下载）就把半截包当成品发走，浏览器拿到坏 zip。
  `rename` 同文件系统内是原子的：成品路径上要么完整、要么没有。清理扫描要连 `.part` 一起扫。
- **「文件在不在」不能当「任务好了没」的判据**（`.124`）：取成品前要问任务状态
  （新增 `gj_ready()`：id 对上 + kind 对上 + `done && ok`）。另外**「还没好」和「文件没了」
  必须回不同的状态码** —— 前者 409、后者 404。以前两者都回 404，于是「打包途中点下载」
  看起来像「任务凭空消失」，排查时分不清是状态问题还是文件问题。
- **缓存键不能漏掉「来源」这一个维度**（`.124`）：`md_thumb_src` 最后那个 `-3` 分支忘了
  `memset(st)`，于是**占位图**的缓存键用的是**视频文件自己的 mtime** —— 而真封面帧的键用
  封面帧的 mtime；同一个脚本先后写的文件 mtime 相同，两者就互相串图（主机测试里当场复现）。
  凡「同一条 id 可能有多条来源路径」的缓存，键里要么带上来源、要么保证各来源的键空间不相交。
- **测试最容易的失败方式是「自证空转」**（`.124`，这一课是两轮真机返工换来的）：
  - `.123` 的封面用例把「`icon_path` 所在目录」与「按 `id` 推出来的目录」摆成**同一个目录**，
    于是它把**错的假设**测成了绿的 —— 真机上封面照样没有。
  - 判据必须是**性质**，不是「跑通了」：断言封面要断言**画面内容**（左红右蓝渐变），
    因为「解开了」这件事给张纯黑图也能过。
  - **改了关键分支就顺手做一次变异测试**（把那段代码掐掉，确认测试真的会红）——
    `test/mutation_check_media.sh` 就是干这个的。用例不红，等于没写。
- **自引用的 async 函数一律写成函数声明，不要写成自引用的 const 箭头函数**（`.125`，
  这一条是**构建期**的坑，源码看着完全正确、dev server 跑着也完全正确，只有产物坏）：
  ```js
  // ✗ 产物里 setTimeout(tick, a) 的 tick 是悬空标识符 → ReferenceError，轮询永久停摆
  const tick = async () => { …; timer = setTimeout(tick, intervalMs) }
  // ✓ 函数声明的名字绑在外层作用域，降级后自引用仍然有效
  async function tick() { …; timer = setTimeout(tick, intervalMs) }
  ```
  原因：`build.target` 是 `['es2015','safari12']`，es2015 没有 async，Babel 要把 async 降级成
  generator 包装；对**自引用的 const 箭头函数**，Babel 会把整个 `const f = …` 换成一个
  **立即调用并丢弃的 IIFE**，却**不重写函数体里的那个自引用**。产物里留下的是一句
  `setTimeout(tick, a)`，而 `tick` 这个绑定已经不存在了。
  **同一类写法还包括**：`const f = async () => { … await f(true) … }`（递归重试）、
  `const f = async () => { … setTimeout(() => f(n + 1), 1000) … }`（包在嵌套箭头里也算）。
  仓库里已按此修正的三处：`utils/gameApi.js` 的 `tick`、`StorageHub.jsx` 的 `fetchRemote`、
  `App.jsx` 的 `refreshPayloads`。**`App.jsx` 里既有的 `async function poll()` 一直是好的，
  就是现成的对照。** 新增自引用 async 函数时，先按这条自查一遍。
- **回归跑 vite dev server 会漏掉「构建期改写」引出的整类 bug**（`.125`）：
  `game112.mjs` / `nav112.mjs` 打的是 dev server（原生 ESM，`async/await` 不降级），
  真机打的是 Babel 降级后的**构建产物**。`.125` 那个「打包完但下载按钮永不出现」的 bug
  在 dev server 上**根本不存在**，所以 177 项全绿也照样漏。
  **凡是与「构建期改写」有关的行为（async 降级、可选链/空值合并降级、terser 压缩、
  `drop_console`、`define` 注入的常量、单文件内联），都必须在产物上验** ——
  用 `serve_dist.mjs` + `pack125.mjs` 这一套。
- **PS5 的录屏封面是 JPEG XR（`.jxr`），stb_image 解不了**（`.125`，三轮封面失败的真正原因）：
  真机路径 `/user/av_contents/thumbnails/video/<生成方>/<TID>/<hex>/<名>.webm.jxr`，
  文件头 `49 49 BC 01`，1920×1080 24bppRGB，约 75:1（DCT 频域编码）。
  **照片封面是 `.png.jpeg`（普通 JPEG），所以照片那条路一直是好的** ——
  这也是为什么「照片能出图、录屏不能」看起来像路径问题，其实**是格式问题**。
  现在的做法是**不解析**：视频格子直接用 `<video preload="metadata">` 让浏览器从 webm 取帧。
  若哪天要真的解 jxr，那是在 `third_party` 里塞 jxrlib 的工程量，**不要顺手做**。
- **没有 Range 支持时，浏览器会把媒体判成「不可定位」**（`.125`）：
  实测 `<video>` 拿到 `readyState=4`、尺寸正常、能画帧，但 `currentTime` **设不动**
  （`seekable` 为空、赋值后立刻还是 0），而且会把整个录屏拉完（43MB 那条实测如此）。
  所以：① 不要写「seek 到第 1 秒再截图」这种代码，它在当前后端上是**死代码**；
  ② 想省流量、想拖进度条，前提是给文件直出加 `Range` —— 见 §7。
- **Node 的 `res.setHeader` 遇到非 ASCII 头值会抛 `ERR_INVALID_CHAR`**（`.125`）：
  mock 的下载端点原来写 `attachment; filename="<中文名>.zip"`，于是**每次下载都 500** ——
  「下载」这条路在 mock 上从来没被真正跑通过，也因此**掩盖了真机上那个下载按钮的 bug**。
  正确做法与后端 `add_disposition` 同口径：一份 ASCII 兜底（非 ASCII 换 `_`）+
  一份 RFC 5987 的 `filename*=UTF-8''…`。**mock 与后端在「响应头」上也要对齐**，
  不然测试会给出一片假绿。
- **mock 抹掉了真机的前提，等于把那条分支的 bug 藏起来**（`.136`，血淋淋的一课）：
  `.135` 那个「磁贴拿 app.db 的 `size` 列当游戏体积」的 bug，在本地**永远复现不出来** ——
  因为 mock 的 `/game_list` 直接给 folder 游戏塞了真实 `size`，而真机列表是 `skip_du=1`、
  folder 的 `size` 恒 0。「列表不扫盘」这个**前提**被 mock 顺手抹平了，兜底分支就永远是绿的。
  → **改 mock 时要问一句「真机在这条路上的输入是什么」**，凡是被后端策略（`skip_du`、
  缓存命中/未命中、`force=1`、权限、路径不存在）决定的输入，mock 都得照抄那份**时序与边界**，
  而不是给一个「理想值」。同类前科：下载端点的中文文件名（`.125`）、打包的 mtime（`.126`）。
  顺带一条口径：**列表里的 `size` 与详情里的 `体积` 必须是同一个量**；`app.db` 的 `size` 列
  **不是** folder 游戏的体积（真机 132 GB 的游戏那里只有 1.22 GB），它只配给「没有物理源可扫」
  的内置 PKG 兜底。
- **摊平文件层级 = 必须自己承担撞名的风险**（`.126`，压缩包那条）：
  原来条目名是完整 `content_path`，天然唯一；改成「`游戏编码-游戏名称/` + 下载名」之后，
  下载名只精确到秒（原文件名里那截随机数被丢掉了）→ **同一秒连拍两张必然同名**，
  而 zip 里两条同名条目在 Windows 资源管理器里解压是**互相覆盖**（静默丢文件）。
  → 任何「把一批文件摊平到一个目录」的改动，都要顺手问一句「名字还会唯一吗」，
  不唯一就得补号（`-2` / `-3`…），并且**给这个补号路径配一条用例**。
  同类的还有：文件夹名要按文件名规则清（冒号 / 斜杠），并按 UTF-8 边界兜长度。
- **夹具里凡是会被写进「名字 / 时间戳」的字段（mtime！）都要钉死**（`.126`）：
  条目名用的是文件**真实 mtime**，而夹具原来是「写到哪算哪」→ 每次跑出来的名字都不一样，
  「同一秒两张要去重」这条**根本没法断言**（时红时绿，比不测更糟）。
  现在 `make_media_fixture.py` 的 `put()` 按**文件名里那个时刻**钉 mtime。
  **写任何依赖 mtime / 当前时间的断言之前，先把夹具的对应字段钉死。**
- **变异脚本的 sed 锚点会随重构失效**（`.126`）：把 `snprintf(e[n].zname, …)` 重构成
  `md_zname_unique(…)` 之后，原来那条变异表达式就匹配不上了。脚本会明确报
  「补丁没打上 —— 锚点变了，请更新本脚本」（不会静默放过），**改完被锚定的那几行
  记得同步更新 `test/mutation_check_media.sh`**。

## 7. 接手后的下一步（待定，非阻塞）
- **`.142`（存档第 3 层：自动存档执行引擎 + 远程推拉）—— ✅ 已出包、已推真机、真机验收全过**。
- **`.143`（SMB 接过 `libsmb2`）—— ⏳ 代码/宿主测试就绪、走完本地回归，**未提交、未出包、未推真机**。
  （`third_party/libsmb2/` 含 `libsmb2.a` 与 `libsmb2-6.2.tar.gz`，是否入库待定。）
- **`.144`（存档配置搬家：远程位置全局生效 + 单游戏只留推/拉）—— ⏳ 前端/mock/文档 + 本地回归全绿
  （`game112` 249 / `nav112` 20 / `acct111` 23 / `avatar109` 19 / `cropfix` 75 / `pack125` 15 / 宿主 `overall PASS`），
  **未提交、未出包、未推真机**。真机验收需同 LAN 宿主机（本环境连不上 `192.168.1.214`）。
  真机脚本 `_ftp_probe/device142_sync.py` 里「smb 回 400」那条期望要在 `.143`/`.144` 出包后改成「期望 200」。
  见 §0 的 `.142` 段与 §6 的「存档远程同步」条。真机侧已确认：`push` 真把 `<TID>/<uid>.zip`
  写进远端、`pull` 能从空本地靠 `game_saves_user_ids()` 兜底恢复（sha256 逐字节一致）、
  `SR_ENOTFOUND` 与 `smb 不支持` 两条错误路径的返回都对、每一步面板都活着。
  **下次动传输层（`src/save_remote.c`）或引擎（`src/game_auto.c`）时照这几条真机验**：
  1. **协议**：`libcurl` 那条路只有 `http/https/ftp/file/dict/imap/ldap/pop3/smtp`（没 `smb`）；
     新增协议前先 `strings -n 3 /opt/ps5-payload-sdk/target/lib/libcurl.a | grep <proto>` 确认。
     **但 SMB 本身能做** —— 见 §6 那条，`libsmb2` 可交叉编译并链进来（+241 KB）。
     真要接 SMB 的话：`src/save_remote.c` 加 `mode == "smb"` 分支（`smb2_connect_share` +
     `smb2_open(O_CREAT|O_WRONLY)` + `smb2_write`；`smb2_unlink` 做「远端还没有」的清理），
     远端布局沿用 `<base>/<TID>/<uid>.zip`，UI 把那一档 `disabled` 去掉，
     并同步改掉「当前构建未启用 SMB」那句提示。
  2. **远端布局推拉逐字一致**：`<base>/<TID>/<uid>.zip`，文件名恒定、不带时间戳、整份覆盖。
     改任何一边（`sr_build_url` 的拼接 / `sr_pull` 的落点）都要同时改另一边。
  3. **`g_sync_mu` 不能去掉**：「整份替换」= 旧的改名 → 换上去 → 删旧的，并发跑同一目录会
     互相删掉对方刚改名的目录（守护线程退出推送 vs 用户手点「立即推送」完全可能同时）。
  4. **`SR_ENOTFOUND (-2)`**：FTP 的 `CURLE_REMOTE_FILE_NOT_FOUND` **与**
     `CURLE_REMOTE_ACCESS_DENIED`（CWD 到不存在目录 → 550）都要归进来，HTTP 是 404。
     它是「跳过」不是「失败」—— 别让它把 `ok:false` 带出来。
  5. **守护线程栈**：`game_auto` 是 `pthread_create` 出来的，**不受 MHD 那个 1MB 选项管**，
     自己 `pthread_attr_setstacksize(1MB)`（真机连接线程只有 64KB，见 §0 的栈预算红线）。
  6. **验收脚本**：`_ftp_probe/device142_sync.py`（推拉 + 两条错误路径，18 项）、
     `_ftp_probe/device142_pull_restore.py`（删本地 → pull 兜底恢复 + sha256 比对，13 项）。
     两者都在本机起一个极小 HTTP 桩当远端（PUT 存 / GET 取），**不依赖外网**。

- **`.132`（改名写源 `sce_sys/param.json`，SMP 重扫不再回退）—— ✅ 已出包、已推真机、真机闭环验完（32/32）**。
  见 §0 的 `.132` 段。真机侧已确认的：`persisted:true` + 四层逐字节一致 + 备份建成 +
  **SMP 重扫后主屏名字不回退**（这是 `.132` 唯一的目标）。三条真机才看得出的结论也记在 §0
  （SMP 缓存来自 `app.db` 而非实时读源 / 所有语言键一起改 / 还原要走产品路径不能用 FTP）。

  **⏳ 真机侧还欠的两类实测**（都不是 `.132` 的回归，是产品行为的边界，有空再补）：
  1. **镜像类（`.ffpfsc`）与内置 PKG 两类在真机上的常驻提示**：主机侧只验了 folder 一类
     （PPSA27836）。镜像与 PKG 走的是 `persisted:false` 分支，mock 侧已由 `game112` 7.4 节验过，
     但真机要看一眼 PPSA17599（`.ffpfsc`）与 PPSA01650（内置 PKG）弹窗里那句常驻说明是不是
     该出现的内容，且**确认它俩的源确实没被碰**（`mount_img.lnk` 指向的镜像文件 mtime 不变）。
  2. **用户自己第二次改名时的备份语义**：`orig/param.json` 只在**首次**改名时建，之后不动。
     真机上已经用 PPSA27836 建过一次备份（内容 == 原厂件 2902 B），下次任何人再改这款游戏，
     备份不会被覆盖 —— 这个「不覆盖」行为值得在真机上顺手确认一次，因为它决定了用户
     「改坏两次以后能不能回到原厂」的答案。

- **`.131`（SMP 先探后开 + 打包自动下载）—— ✅ 已出包、已推真机（`GET /version` → `0.5.2.131` 已确认）**（见 §0 的 `.131` 句）。
  回归口径（下次动 SMP 入口或多媒体打包时照这几条验）：
  1. **SMP 没起时点「去 ShadowMount 管理」**：按钮转圈 + 文案依次变
     「正在检查 ShadowMount 是否已启动…」→「正在启动 ShadowMount…」→
     「已发起启动，正在等待端口就绪… 已等 N 秒」（N 每秒 +1）；期间按钮 `disabled`。
     探活转活后新开一个标签指向 `http://<host>:10101/`，本面板页不被顶掉。
  2. **SMP 已起时点同一颗按钮**：**不等**，直接开标签（`/smp_status` 一次即回 `running:true`）。
  3. **探活不通过**（约 31 秒 = 700ms × 45 次）：关掉空白标签 + 按钮上方出红色
     `已发起启动，但端口迟迟没有就绪`；本机没装 SMP 则出 `本机没有找到 ShadowMountPlus…`
     并**照旧按普通链接打开**（不猜程序）。
  4. **`/smp_status` 语义**：主机在 `127.0.0.1:10101` 上 TCP 建连探活，400ms 收发超时；
     回 `{"running":bool,"port":10101}`。已在 `is_quiet_route` 降噪（拉起期间 700ms 轮询）。
     mock（`frontend/mock-server.js`）用**真实 TCP 探活**实现，`/loadpayload:` 命中
     ShadowMountPlus 路径会起一个监听 10101 的桩（`MOCK_PROGS` 里 `shadowmountplus:10101`，
     1.5s 延迟模拟「进程起来 → 端口转活」），整条链路可在预览里端到端验证。
  5. **SMP ELF 路径解析**：`/list_payloads` 的 `payloads` 按**目录名与文件名两路**归一化
     （`normName` + 双向前缀，阈值 3）匹配 `ShadowMountPlus`，排除 `nayeplmtools*`（自己），
     **唯一候选才认**。所以 `ShadowMountPlus_1.7beta3.elf` / `beta4` 这类版本差异不用改代码。
  6. **打包自动下载**：`watchJob` 的 `st.done && st.ok` 分支里插隐藏 `<a>` → `click()` → 摘掉，
     指向 `mediaPackUrl(job_id)`，同时弹 `打包完成，正在下载（N 项）`。**全量与选中都汇聚到
     `startPack`，一处改动两个按钮都生效**。界面上那颗「下载打包文件（N 项）」按钮**保留**作
     兜底（PS5 的 WebKit 偶尔拦非用户手势触发的下载）。
  7. **别顺手改的两处**：`game.delete_*` 词条簇仍无 JSX 引用（同 `.129`/`.130`，别当基准）；
     详情的两行 `GridRow` 仍是 `<button>`（它们是导航不是操作）。

  **⏳ 真机侧还欠一轮实测**（`.131` 推上去后主机 userland 掉线，只能物理接入重启后补）：
  1. **`/smp_status` 的 C 实现是真机第一次跑**（本地与 mock 都验过，主机上还没跑）。
     主机上先 `GET /smp_status` 看回 `{"running":false,"port":10101}`，耗时应在 400ms 以内
    （loopback 拒连是即时的）；**不要**因为「电脑浏览器连不通 10101」就判 SMP 没起 ——
     探活是主机在 loopback 上做的，两条路不是一回事。
  2. **SMP 没起时点「去 ShadowMount 管理」**：确认电视上真的看到三态进度 + 秒数递增 +
     按钮转灰，以及 31 秒后是否如预期出红色失败条。**主机上是否装了 ShadowMountPlus 未知**
     —— 没装就应出「本机没有找到 ShadowMountPlus…」并按普通链接打开，这本身就是预期行为，
     别当成 bug。
  3. **打包自动下载在 PS5 的 WebKit 上有没有被拦**：这是本次唯一「非用户手势触发下载」的行为，
     正是 WebKit 最可能拦的一类。本地 Chromium 产物已验过 download 事件真的发出
     （`pack125` 15/15，落盘名 `战神-诸神黄昏-20261007_121143.zip`），但真机要人工确认
     **是否真的有下载开始**；被拦的话，界面上那颗「下载打包文件（N 项）」就是兜底，不用改代码。

  **推包的两个环境细节**：① `push_elf.py` 第 3 步传输末尾常报
  `ConnectionResetError [WinError 10054]` —— 那是 elfldr 收到后开始执行时主动断连，
  **不是失败**；判据只有一条：`GET /version` 返回期望版本。② 主机掉线判定：8911 / 9021 / 2121
  **全拒连但 ping 通**说明 userland 整体没起（不是我们 payload 崩），推包前必须先确认 elfldr
  回来了。

- **`.130`（`.129` 漏掉的一条死链文案）—— ✅ 已出包、已推真机、29/29 验收通过**。
  唯一遗留的坑：`t(key, '中文默认值')` 的**内联兜底**改 JSON 盖不住（见 §0 的 `.130` 句）。
- **`.129`（游戏详情弹窗的操作按钮全部下线，只留一颗「去 ShadowMount 管理」）—— ✅ 已出包**
  （见 §0 的 `.129` 句；`.128` 改的是磁贴、方向错了已回退，`.129` 把这轮手术做到详情里）。
  回归口径（下次动游戏详情或磁贴时照这几条验）：
  1. **磁贴上必须有四颗**动作按钮（`存档管理` / `多媒体管理` / `名称与背景` / `待定功能`），
     且**磁贴上没有**「去 ShadowMount 管理」；
  2. **游戏详情里必须有且只有一颗**跳 SMP 后台的按钮（`aria-label="去 ShadowMount 管理"`），
     点击**新开一个标签页**指向 `http://<host>:10101/`，**不覆盖**当前面板；
     断言时要先在测试环境起一个真监听（`node:http` 桩返回 `SMP_STUB_LOADED` 并断言 body 命中），
     否则浏览器给 `chrome-error://chromewebdata/` 而断言**假红**；
  3. 详情里挂载 / 卸载 / 删除源 / 取消任务 **一颗都不剩**——用正则扫弹窗内**所有** `<button>` 的
     `aria-label` + `textContent`。这条是**刻意加的反面断言**，防以后把按钮偷偷加回来还静默通过
     （`.128` 只有正向断言，就是它的教训）；
  4. 存档 / 多媒体 **两条路都能进**：详情的两行 `GridRow`（`打开存档管理` / `打开多媒体管理`）
     和磁贴按钮各一条；「名称与背景」**只有磁贴一条路**（详情里那行已随 `.128` 回退删掉）；
  5. 后端 `/game_delete` / `/game_uninstall` / `/game_mount` **三个端点没动**，只是没有 UI 再调它们了；
     `.122` / `.123` 那批针对删除源的修复从此**没有前端覆盖**（`game112` 原第 3/4 节已删，
     不要当成回归缺失）；
  6. **别顺手改的两处**：详情的两行 `GridRow` 仍是 `<button>`（它们是**导航**不是操作，
     用户若也想要它们去掉再动）；「待定功能」那颗空壳按钮随 `.128` 回退**回来了**。
  7. **别用 `evaluateAll` 直接回传 DOM 元素**——Playwright 序列化后变 `undefined`，
     `els.filter(...)` 在空集时静默通过、一旦真扫到就 `TypeError`；要在页面内 `map` 成字符串。
- **`.127`（游戏详情来源类型错标成「已安装 PKG」）—— ✅ 已实机验收通过**（见 §0 的 `.127` 块）。
  留两条回归口径（下次动游戏详情时照这几条验）：
  1. **`光与影：33号远征队`（`PPSA17599`，镜像类）**：来源类型 = **镜像**、格式徽标 `FFPFSC`（不变）、
     物理路径 `/mnt/ext1/PPSA17599三十三号.ffpfsc`、有「**删除源**」。
  2. **`FINAL FANTASY VII REMAKE`（`PPSA01773`，文件夹类）**：来源类型 = **文件夹**、
     **不出现「已挂载」**、有「**删除源**」。
  3. **真·内置 PKG**（本机样本 `PPSA01650`）：来源类型 = **已安装 PKG**、`installed_pkg` 为真、
     有「这是已安装的 PKG，没有可删除的物理源」、没有「删除源」（`/game_delete` 该返 403 EPERM）。
  4. **反面判据**：全列表里**除真 PKG 外 0 款**显示「已安装 PKG」（`.126` 及以前 14 款全错标）。
  5. **对照工具**：真机 `192.168.1.214:10101`（SMP 1.7beta4 的 API，POST + JSON body）——
     `POST /api/v1/games` 空 body 即返回全量真值，逐字段比别靠肉眼猜。脚本在 `_ftp_probe/`：
     `badge_verify.py`（接口对照）、`device127_badges.mjs`（界面级，`device127: PASS` 17/17）、
     `push_elf.py`（推包：`/shutdown` → 等 8911 关闭 → socket 灌 :9021 → 轮询 `/version`）。
  6. **别顺手改的两处**：磁贴列表右上角的「已安装/转储」角标（按 `src`，是「怎么找到的」，
     15 款全亮「已安装」是对的）；`can_uninstall` 读 app.db 的 `uninstallable` 列。
- **`.126`（压缩包改成「游戏编码-游戏名称/」一层）—— ✅ 已实机验收通过**（见 §0 的 `.126` 块）。
  留两条回归口径：
  ① 解压出来**只有一层**目录（`<游戏编码>-<游戏名称>/`），截图/录屏**直接**躺在里面，
     不再有 `photo/<生成器>/<TID>/<hex>/` 那套；
  ② **同一秒连拍两张**时后一条要带 `-2`（下载名只精确到秒，不去重的话解压互相覆盖）。
- **`.125` 的两条症状 —— ✅ 已实机验收通过（12/12 PASS，见 §0 的 `.125` 块）**：
  用户报的「打包下载还不好用。视频封面也没有。」**两件都已在真机上确认修好**。
  验收脚本 `device125.mjs`（打真机 8911，跑在真机内嵌的那份构建产物上）。
  下面 0–5 条保留为**回归口径**（下次改动媒体那一块时照这几条验）：
  0. **两条一眼分胜负的判据**：
     - 打包跑完后**必须出现「下载打包文件（N 项）」按钮**。`.124` 及以前**永远不出现**
       （轮询第 1 次之后就抛 `ReferenceError: tick is not defined` 停摆了，见 §6）。
       `.125` 实测：轮询 3 次、面板进终态「打包完成」、按钮出现、下载端点 HTTP 200 /
       61,518,688 字节 / `PK` 魔数。
     - 视频格子**必须是真画面**。`.124` 及以前是「深底 + 播放三角」的占位图。
       `.125` 实测：`readyState=4` / `3840×2160` / canvas 采样 avg=57 max=242。
  1. **视频缩略图必须是视频里的真画面**，不是占位图、更不是**空白**。
     `.122`/`.123`/`.124` 连续三轮都没做成，**真正的原因是格式**：PS5 给录屏生成的封面是
     `.webm.jxr`（**JPEG XR**），而载荷里只有 stb_image，**解不了**；照片封面是普通 JPEG，
     所以只有录屏这一路坏。`.125` 不再解析它，改成视频格子直接 `<video preload="metadata">`
     让浏览器从 webm 取帧 —— **`.125` 真机已通过**（43MB 那条：`readyState=4`、3840×2160、
     帧像素 avg=57/max=242），所以「PS5 的浏览器能解 webm」这条也已被证实。
     **若将来又变成占位图**，先按这个顺序查：① `onError` 有没有把 id 记进 `coverBad`
     （浏览器解不了该编码）；② `/game_media_file?id=…` 是否还回 200。
     真要彻底不依赖浏览器解码，才考虑给载荷塞 jxrlib（`third_party`，工程量是另一个量级）
     或退回「后端出占位图」——**先别急着做**。
  2. **打包进度条必须真的走起来**（体积从 `0 B` 涨、文件 `n/3`、速率有数）。
     ⚠️ 本轮已用真机证据确认**后端 1.2 秒就能把 61MB 的包打完并落盘、HTTP 取包 0.5 秒
     完整收全、zip 校验无错** —— 所以「卡住」如果还出现，**只可能是前端那一侧**，
     先看下面这两条，别再去翻 `zip_write`：
     - `media: status poll #N` 有没有继续往上涨？
       **停在第 1 条不动** → 前端的轮询停了（本轮修的就是这个，若仍复现说明还有第二处）；
       **一直涨** → 轮询是通的，是状态本身没变。
     - 有没有 `media: STALL job=… phase=… idle=…s bytes=…/… files=…/…`？
       **有** → 后端确实卡住，`phase` 指出卡在哪一段
       （`preparing` = 扫库 / `measuring` = 清旧包 / `packing` = `zip_write`）。
  3. **打包下载下来必须是完整 zip**（`.124` 的正面验收）：`.124` 起取成品这条路上每一步
     都有日志 —— `pack_get job=N not ready (state=…)` / `open failed: <path>` /
     `serving <N> bytes (<path>)`。下载失败时先看这几行，再看 HTTP 状态码：
     **409 = 还没打包完**（不是「任务没了」）、**404 = 任务不存在或产出已被清理**。
     另外落盘包名应与下载名一致（`游戏名称-YYYYMMDD_HHMMSS.zip`）。
  4. **下载命名**：单个截图/录屏另存为 `游戏名称-YYYYMMDD_HHMMSS.<ext>`，
     压缩包另存为 `游戏名称-YYYYMMDD_HHMMSS.zip`（都是短横线连的紧凑时间戳，
     与列表上给人看的 `游戏名称-YYYY-MM-DD HH:MM` 刻意不同）。
  5. 界面在长时间没进展时应出现**琥珀色**提示：后端尺子（`idle`）报「已 N 秒没有进展」，
     本机尺子报「已 N 秒没有收到主机的新数据」—— 两条都该能在真机上被触发到。
- **「打包卡住」这条，`.125` 用真机证据结案了：后端从来没卡过。**
  `.122` 修三处、`.123` 修一处、`.124` 修一处，四轮都在后端找 —— 而真机日志显示
  `pack measured (+1ms)` → `pack writing` → `pack job done (+1179ms)`，
  61,518,012 字节的包 1.2 秒打完、HTTP 取包 0.5 秒完整收全、zip 校验无错。
  **「卡住」是前端轮询停摆**（构建产物里 `setTimeout(tick, …)` 的 `tick` 是悬空标识符，
  见 §6），界面因此永远停在 `正在打包… 0%`，看起来就像后端卡死。
  → 所以「先拿到日志再改逻辑」这条纪律是**对**的：日志一到手，四轮猜测立刻收敛。
  `.120`/`.122` 那两次「猜根因」的修法一次是错的（`.120` 还引入了一条 use-after-free），
  这条教训已写进 §6；`.124` 顺手堵掉的「残包可被取走」是**同一条路上另一个独立成因**，
  真实存在、但不是「卡住」。
- **文件直出没有 Range 支持（`.125` 明确记下，本轮有意不做）**：
  现状后果 —— `<video preload="metadata">` 只能显示首帧（`currentTime` 设不动、
  `seekable` 为空），而且浏览器会把整个录屏拉完（43MB 那条实测如此）。
  加 Range 能一次解决三件事：封面能 seek 到更代表的一帧、封面只需拉几百 KB、
  视频预览可拖进度条。
  **没做的理由**：要动 `gr_file_response`（`src/http_server.c`），而它同时被
  **打包成品下载**复用 —— 刚把下载修好，不宜在同一版里再动它。
  真要做的话建议**只给媒体文件那条路加**（新增 `gr_file_response_range(size, fd, start, end)`，
  打包那条继续走原函数），把爆炸半径限制住；主机侧可以用 `media_scan` 加一条
  「带 Range 的请求必须回 206 + 正确的 Content-Range」的断言。
- **`.122` 遗留、至今仍未封的洞（有意留给下一轮，别当成漏改）**：任务槽是**全局单槽**，
  而「取消」只是把 `g_job.cancel` 置 1、等 worker 自己在下一次检查点读到。所以**如果 worker
  真的卡在一个不返回的 syscall 里**（`read`/`write`/`open` 上挂住），`cancel` 永远不会被看到，
  槽就一直被占着 —— 用户看到的是「卡住 + 取消没反应 + 也不能重新发起」，只能重启 payload。
  想做 watchdog 的话，**必须先解决「僵尸 worker 回来覆盖新任务状态」**，否则修一个坑挖一个坑：
  - 给 `gj_*` 的每个变更函数加一个 `id` 参数（`id != g_job.id` 就整个 no-op），
    并把 id 通过 `zip_hooks_t.user` 传进 `zip_io.c`（`user` 现在空着，正好用它）；
  - **不能用一个文件级 static 存 id** —— 僵尸 worker 醒来时那个 static 已经是新任务的 id 了，
    照样覆盖。id 必须是每个 worker 的**局部变量**，一路显式传下去。
  - watchdog 线程（随第一次 `gj_begin` 懒启动）每 5 秒看一次：`active && now - last_progress > 90s`
    就写 `phase="timeout"`、`error="打包超时：90 秒没有任何进展"`、`active=0`，把槽让出来。
  90 秒用的是 **idle**（距上次有进展）而不是总耗时 —— 一个 500 MB 的包合法地跑 5 分钟是正常的，
  但 90 秒没有任何字节推进几乎只可能是真卡住。
  ⚠️ **`.123` 已经建了一条 watchdog 线程**（`game_media.c` 的 `md_watchdog`，随第一次
  `md_pack_start` 懒启动、5 秒一轮），但它**只写日志、不动槽**（`STALL` 那行）。
  要做「超时把槽让出来」就在**同一条线程**上加，别再起第二条；`gj_snapshot()`
  就是为它加的读取口（自带锁，别去解析 `gj_status_json` 的 JSON）。
- 功能已定稿，后续以维护/上游同步为主；如需重启硬件信息，须先解决传感器可重入性与死机哨兵。
- **`.117` 出包后第一件要验的**（真机报的「游戏管理的列表变空了」）：
  - 进游戏管理 → 打开一款游戏的**详情** → 回列表，**来回多切几次，列表必须一直是全的**
    （原症状是「跑过一次详情之后就恒空」，因为非空 `want` 被留在同一个 MHD 工作线程的栈上）。
  - 详情页顺带看 `content_id` / `install_time` / `last_launch` / `appdb_size` /
    `can_uninstall` 五格是不是**变成对的**（`.116` 之前它们读的是错位列）。
- **`.116` 出包后第一件要验的两条**（本轮用户报的两个缺陷，修法见 §2.1.2 末尾）：
  - 打开一张截图看**预览是不是整张**（`/game_media_file`）；再打一个包看
    **zip 能不能正常解开**（`/game_media_pack_get`）。这两条都换了传输方式，
    必须上真机确认 `gr_file_response()` 真的把字节发全了。
  - 顺带确认**没有 fd 泄漏**：连开 50 次预览 / 下载，fd 数不涨
    （fd 现在关在 `gr_file_free()` 里，创建失败那条自己收尸 —— 两处都要盯）。
  - `.116` 的其余 19 条端点只过了语法 + 主机关卡，建议实测顺序：
    列表 → 详情 → 存档列表 → 存档下载/上传 → 改名 → 磁贴图标 → 磁贴背景（含 PNG 预览与
    DDS 下载）→ 挂载/卸载 → 卸载 PKG。
- **`.115` 多媒体后端的三项真机待验证**（主机测试钉的是「我们的行为」，不是「真机的语义」，
  见 `include/game_media.h` §7）——**`.125` 取证后 §7a/§7b 已结案**：
  - **§7a 录屏目录层级 —— 已证实与 photo 同构**（`.125`，FTP 实测）：
    `/user/av_contents/video/NPXS40087/PPSA99101/314/20261006_224245_00574764.webm`
    ↔ 缩略图 `/user/av_contents/thumbnails/video/NPXS40087/PPSA99101/314/…webm.jxr`，
    层级完全一致，`icon_path` 也直接指向它。**当年猜错的方向不是「路径」而是「格式」**
    （见下一条）。
  - **§7b 视频时长 —— 已证实：`duration` 是整秒、`duration_ms` 是毫秒。**
    `.125` 实测两条录屏分别是 `duration=7 / duration_ms=7123` 与 `duration=14 /
    duration_ms=14531`。现实现按 `duration_ms > 0 ? /1000 : duration` 取值，**是对的**。
  - **§7c 只删文件后系统媒体库的反应**：仍未验证。会不会留下幽灵条目、会不会自动对账
    （库里有 `file_status` 列，用途未验证）。**先在真机上删一张不心疼的截图再打开「媒体库」看。**
  - **新增（`.125` 取证得到，供后续参考）**：真机 `av_content.db` 的 `tbl_PhotoVideo`
    是 **67 列**，`content_type` **1 = 照片、2 = 录屏**（早期快照里只有照片，所以只见过 1）。
    录屏封面是 **JPEG XR**（`.webm.jxr`，`49 49 BC 01`），照片封面是 `.png.jpeg`（普通 JPEG）——
    这就是「照片能出图、录屏不能」的全部原因。
- **`.115` 后端整体仍未上过真机**：八条路由只过了语法 + 主机关卡，真机实测顺序建议 ——
  列表 → 缩略图 → 原图/录屏预览 → 删除一张 → 打包下载（大录屏那条单独验一次）。
