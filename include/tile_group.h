#ifndef TILE_GROUP_H
#define TILE_GROUP_H

/* [CN-FORK] v0.5.2.166 主屏固定列表（PS5 主屏「固定 / 排序」的真相载体）。
 *
 * ── 为什么不是我们自己的 JSON ──────────────────────────────────────────────
 * `.164` 的「置顶」把状态写进了 `/data/nayeplmtools/app_pins.json`（后来是
 * `game_prefs.json` 的 pin 字段）。那**只对本面板可见** —— 主机压根不知道有这回事，
 * 所以用户看到的就是「置顶按钮在 PS5 里没有生效」。
 *
 * ── 真相载体：app.db 的 tbl_group_<uid> ────────────────────────────────────
 * 真机取证（`_ftp_probe/evidence/recon3/app.db`，4 个账号）：
 *
 *   CREATE TABLE tbl_group_0438122025 (
 *     itemId TEXT NOT NULL, groupName TEXT NOT NULL, type TEXT NOT NULL,
 *     idx INT, lastModifiedDate TEXT, PRIMARY KEY(itemId, groupName))
 *
 *   ('NPXS40037', 'MyHome', 'title', 1, NULL)     ← 系统条目
 *   ('NPXS40047', 'MyHome', 'title', 2, NULL)
 *   ('PPSA32786', 'MyHome', 'title', 3, NULL)     ← 用户自己固定的《仁王 3》
 *   ('WKAL00001', 'MyHome', 'title', 4, NULL)     ← 用户自己固定的 WebKit Autoloader
 *   ('NPXS40071', 'MyHome', 'title', 5, NULL)
 *
 * 对照账号 0438122023 只有 3 行（NPXS40037 / NPXS40047 / NPXS40071）—— 两个「多出来」
 * 的条目正是用户自己固定在主屏上的那两个，**idx 就是主屏位置**。这就是可写的载体。
 *
 * 同一份取证里另外两条被**排除**的候选，记在这里免得下次又去试：
 *   · `tbl_iconinfo_<uid>.pinnedStatus` —— 内部软件恒 2、游戏恒 NULL，与「谁被固定」
 *     无关（`.160` 的排除法在这里被再确认一次）；
 *   · `tbl_iconinfo_<uid>.dispLocation` —— 内部软件与游戏一律 138，是个类别枚举。
 *
 * ── 写哪些账号 ────────────────────────────────────────────────────────────
 * **扫 sqlite_master 拿到全部 `tbl_group_%` 表，每张都写**。这与 tile_db.c 写名字列、
 * app_visibility.c 写 visible 的口径完全一致（都是「per-account 表靠列/名前缀发现，
 * 绝不硬编码 uid 后缀」）。只写「当前用户」需要另接一套 uid 解析，而多账号下
 * 「名字对所有账号生效、固定只对一个账号生效」反而更让人困惑。
 *
 * ── 改完什么时候看得见 ────────────────────────────────────────────────────
 * 与改名同档：Shell 有自己的内存视图，**重启主机或待机唤醒**后才重绘主屏。
 * 后端只负责把「写没写进去」说清楚（host_ok / note），不假装实时。
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 主屏固定列表的组名（真机上唯一出现的那个）。 */
#define TILE_GROUP_NAME "MyHome"

/* 读主机全部账号的 MyHome 组，合成一份「去重 + 按 idx 升序」的列表。
 * 输出（写进 out，调用方给够空间，建议 4KB 以上）：
 *   {"ok":true,"group":"MyHome","accounts":4,
 *    "items":[{"itemId":"NPXS40037","idx":1},...]}
 * 库打不开时仍回 200 结构，ok=false 并带 note —— 前端据此显示「读不到主机列表」，
 * 而不是把整个游戏页打断。返回写入的字节数，<0 = out 太小。 */
int tile_group_list_json(char *out, size_t size);

/* 置顶 / 取消置顶。pinned=1 → 把 tid 插到组内最前（其余条目依次后移，重新编号 1..N）；
 * pinned=0 → 把 tid 从组里删掉，剩下的重新编号。
 * 返回 0 = 至少写进了一个账号；-1 = 参数不合法（msg 给人话）；
 *      -2 = 库打不开 / 一条都没写成（msg 说明原因）。
 * out_accounts = 写了几个账号的库；out_idx = 置顶后的位置（取消置顶时回 0）。 */
int tile_group_set_pin(const char *tid, int pinned, char *msg, size_t msgsz,
                       int *out_accounts, int *out_idx);

/* 按给定顺序重排**已经在组里**的条目。
 *
 * 语义（刻意保守）：只有出现在 tids 里、且当前就在 MyHome 组里的条目会被动；
 * 它们占据的那几个 idx **槽位**不变，只是按 tids 给出的先后重新分配。
 * 没被提到的条目（系统条目、别的游戏）一个字节都不动。
 *
 * 为什么不做「整体重写 idx」：那等于把 tids 里没列到的游戏也一起固定进主屏，
 * 而 MyHome 是个有语义的短列表，塞进四十款游戏是什么后果没人验证过。
 * 「已固定的项之间随便换位」+「想固定用置顶」这个分工既满足需求又不越界。
 *
 * 返回 0 = 成功（可能一个槽位都没变）；-1 = 参数不合法；-2 = 库打不开 / 写失败。
 * out_moved = 真的换了位置的条目数（跨账号去重后）。 */
int tile_group_reorder(const char *const *tids, int n, char *msg, size_t msgsz,
                       int *out_moved);

#ifdef __cplusplus
}
#endif

#endif /* TILE_GROUP_H */
