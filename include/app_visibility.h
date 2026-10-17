#pragma once
/* [CN-FORK] v0.5.2.158 「个性化软件」—— PS5 内置软件的显示 / 隐藏。
 *
 * 状态就在 app.db 的 `tbl_iconinfo_<uid>.visible` 这一列里，本模块只做按 titleId
 * 把它写成 0 / 1 的事。刻意**不**新增配置键：再抄一份到 nayeplmtools_config.txt
 * 只会多出「两边不一致时听谁的」这个问题。
 *
 * ⚠ **`.158` 当初的判据只对了一多半，v0.5.2.160 用真机快照纠正如下**
 * （192.168.1.214，2026-10-10，取证脚本 _ftp_probe/probe_vis159*.py）：
 *   - 这一列**对普通内置软件有效**：PlayStation Store / 媒体相册 写成 0 之后，
 *     用户实看是「没了」。
 *   - 但 `游戏内容保存库` / `应用程序内容保存库` 库里同样已经是 0，主屏**照旧画**；
 *     同一次快照里上面那两条已经消失，所以不是「主屏没重绘」，是这两条不听这一列。
 *   - ⚠ `.160` 当时把 `所有应用程序` / `欢迎` 也算进这四条，**是错的**（`.161` 按用户
 *     真机口径撤掉：「所有应用程序、欢迎 是可以编辑显示和隐藏的」）。错在拿**出厂值**
 *     反推规则 —— `NPXS40056` 在快照里本来就是 `visible=1`，画出来正是这一列在起作用；
 *     `欢迎` 出厂 0，但「主屏到底画不画它」从没被肉眼核对过。
 *     **只认「写了 0、再看主屏」这一种实验**，别拿静态快照的 0/1 分布当判据。
 *   - `编辑` **出厂就是 0** 却仍在主屏上 —— 所以「=0 就看不到」这条
 *     判据从一开始就不能反过来用（`.158` 就是拿 10-01 那份原件的 0/1 分布
 *     当「看得到的正好=1」的证据，错在这里）。
 *   - 找不出能区分两组的列：entHidden 全 0，dispLocation / pinnedStatus /
 *     categoryType / AppInfoJson 都没有一致差异。
 * 因此下面那张固定名单把「面板改不动」的条目**显式承认**下来：读的时候标出来，
 * 写的时候直接拒 —— 以前那种「写了 0、UI 报已隐藏、主屏照旧」是最坏的一种结果。
 *
 * 范围只认 NPXS 前缀（索尼内置应用）。自制软件与游戏一律拒写 —— 否则一个构造
 * 出来的请求能把 NayePlmTools 自己的磁贴藏掉，而恢复入口就在那块磁贴上。 */

#ifndef APP_VISIBILITY_H
#define APP_VISIBILITY_H

#include <stddef.h>

/* 与 game_mgr.h / tile_db.h 一样是**同一个文件**，各自持宏是为了让宿主测试能用
 * -DAPP_VIS_APPDB_PATH 指到临时副本上（真机原件不能被测试写坏）。 */
#ifndef APP_VIS_APPDB_PATH
#define APP_VIS_APPDB_PATH "/system_data/priv/mms/app.db"
#endif

/* 内置软件的 titleId 前缀，也是本模块唯一的写入白名单。 */
#define APP_VIS_PREFIX "NPXS"

/* GET /system_apps 的响应体（v0.6.0.179 起加三个字段）：
 *   {"apps":[{"tid":"NPXS40037","name":"PlayStation Plus","hidden":true,
 *             "pinned":true,"custom_icon":0,"custom_bg":1,"controllable":true},
 *            {"tid":"NPXS40071","name":"游戏内容保存库","hidden":true,
 *             "pinned":false,"custom_icon":0,"custom_bg":0,
 *             "controllable":false,"reason":"主屏固定入口：…"}, ...],
 *    "count":19,"accounts":1,"error":""}
 * 按 name 的拼音/字典序排不好做（中文标题），所以按 titleId 升序 —— 稳定且免排序。
 * `controllable` 是 `.160` 加的：假的那几条**面板改不动**，免得再出现「UI 说已隐藏、
 * 主屏照旧画」。`reason` 只在 false 时出现，是一句人话。
 * ⚠ `.161` 起这几条**照旧返回、但前端整组不显示**（用户口径：改不了的别摆出来）。
 * 名单留在后端而不是前端：前端自己抄一份必然和后端漂，漂了就又回到谎报。
 *
 * v0.6.0.179：
 *   · `pinned` 改成**读主机**的固定列表（tbl_group_<uid> 的 MyHome 组，跨账号取并集）
 *     —— 见 src/app_pin.c 的头注释：`.164` 起它读的是我们自己的 JSON，主机压根
 *     不知道，用户看到的就是「置顶没生效」；
 *   · `custom_icon` / `custom_bg`：面板里有没有存过自定义的图标 / 背景（1 = 有）。
 *     前端拿它决定「恢复默认」是否可点，以及给 /app_icon 打 ?v= 破缓存。
 * 库打不开时回 {"apps":[],"count":0,"error":"…"}（不是空数组冒充成功）。
 * 返回写入 out 的字节数（不含 '\0'），缓冲不够回 -1。 */
int app_vis_list_json(char *out, size_t size);

/* 这一条是不是「面板改不动」的固定条目（主屏固定入口 / 系统占位）。
 * 是 → 返回 1，并把一句人话写进 *why（指向静态字符串，别 free）；不是 → 返回 0。
 * 名单的依据见文件头那段真机快照；tid 不合法也返回 0（由调用方的校验去管）。 */
int app_vis_fixed_reason(const char *tid, const char **why);

/* app_vis_set 对固定条目的返回码。单独一个码是因为 HTTP 层要能给出
 * 「这条不该由面板动」而不是笼统的 EINVAL，宿主测试也要钉住它**没动过库**。 */
#define APP_VIS_EFIXED (-3)

/* 写一行的 visible。hidden=1 隐藏、0 显示。
 * 返回 0 = 至少改了一张账号表（或本来就一致）；
 *      -1 = 参数不合法 / tid 不是内置软件 / 库打不开 / 一行都没匹配到；
 *      -2 = 尝试写了但 SQL 失败，**或主机正忙读不到应用清单**（忙 ≠ 「没有这个软件」，
 *           `.160` 之前这两种混成一句，害得排查的人去找「为什么没装」）；
 *      APP_VIS_EFIXED(-3) = 固定条目，**一个字节都没写、备份也没留**。
 * msg 给人话（成功也可能留空）。首次写入前会走 tile_db 那份「原始备份只留一次」。 */
int app_vis_set(const char *tid, int hidden, char *msg, size_t msgsz);

/* [CN-FORK] v0.5.2.164 应用置顶。
 * v0.6.0.179 起状态在**主机**：app_pin_set 写 app.db 的 MyHome 固定列表
 * （groupName='MyHome'，一次写所有账号表），app_pin_get 也读主机。
 * 下面这份 JSON 退化成「面板这一侧的意图记录」，只给开机对账用。
 *
 * out_accounts / out_idx 由 tile_group_set_pin 原样带出来（写了几个账号的库、
 * 置顶后的位置），/system_app_pin 回给前端用 —— 与 /tile_group_pin 同形。
 * 返回 0 = 至少写进一个账号；-1 = 参数不合法；-2 = 主机一条都没写成（msg 说明）。 */
int app_pin_set(const char *tid, int pinned, char *msg, size_t msgsz,
                int *out_accounts, int *out_idx);
int app_pin_get(const char *tid);
/* 写下意图记录 {"pins":{"<tid>":true|false, ...}}。空 = {"pins":{}}。 */
int app_pin_list_json(char *out, size_t size);

/* ── [CN-FORK] v0.6.0.179 开机对账（「关机开机后被重置」的修法）─────────────
 *
 * 现象（用户口径）：「有些按钮关机开机后会被重置」——两边都有：
 *   · 隐藏：主机自己会重写 tbl_iconinfo_<uid> 那几行（真机三次快照里账号表集合
 *     从 1 张变 4 张又变 2 张，新出现的行默认 visible=1）；列表用跨账号 AND 语义，
 *     于是「藏好的」自己翻回来；
 *   · 置顶：`.164` 起压根没写主机（见 src/app_pin.c 头注释），重启后主屏顺序
 *     完全由主机决定。
 * 修法沿用这个仓库已有的那条路（game_title_sync_all：改名被 SMP 冲回原名 → 开机重放）：
 * **用户的选择留在面板自己的文件里，每次开机重放一遍**。一致时一次写都不发，
 * 所以可以无条件跑。
 *
 * 为什么不是「读到什么就显示什么」：那样用户每次开机都得重新设一遍，
 * 而且面板显示的与主机的会长期对不上 —— 用户要的是设置活下来，不是被告知它没了。
 *
 * ⚠ 代价（必须让用户知道）：面板里设过的条目，开机时会被**重新对齐到用户当初的选择**。
 * 如果之后在主机自己的界面里改过同一条，这里的旧账会把它改回去。所以只对
 * 「从本面板改过」的条目生效（没动过的条目在意图文件里根本没有记录）。
 *
 * app_vis_sync_all：重放隐藏意图（app_vis_wanted.json）。
 * 返回 0（含「没有需要对齐的」），-1 = 意图文件读不了。
 * checked / applied / failed 三个计数可为 NULL。 */
int app_vis_sync_all(char *msg, size_t msgsz, int *checked, int *applied, int *failed);

/* 重放置顶意图（app_pins.json 里 true 的那些）。语义与返回码同上。 */
int app_pin_sync_all(char *msg, size_t msgsz, int *checked, int *applied, int *failed);

/* 起一条 detached 线程，等启动期那阵 IO 过去后把上面两条 + app_tile_sync_all
 * （自定义图标/背景的列指针）各跑一遍。safe_mode 下整条跳过（写主机数据库
 * 与装 app 同级别）。实现与 main.c 里那条 game_title_sync_start_daemon 同形。 */
void app_personalize_sync_start_daemon(void);

#endif /* APP_VISIBILITY_H */
