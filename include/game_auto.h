#ifndef GAME_AUTO_H
#define GAME_AUTO_H

#include <stddef.h>

/* [CN-FORK] v0.5.2.142 自动存档执行引擎。
 *
 * ── 它补的是什么 ─────────────────────────────────────────────────────────
 * `.140` 之前，「自动存档」在配置里是一堆**只被读写、没有任何人在跑**的字段
 * （enabled / on_launch / on_exit）—— 用户在 UI 上打开开关、填好远程地址、
 * 点保存，然后什么都不会发生。本地快照（`.140`）解决了「不依赖网络的兜底」，
 * 这一层解决「多机之间真的搬」：游戏**启动时**把远程那份拉下来、
 * **退出时**把主机这份推上去。
 *
 * ── 怎么知道游戏起没起 ───────────────────────────────────────────────────
 * `sceLncUtilGetAppIdOfRunningBigApp()`（0xffffffff = 没有游戏在跑）+
 * `sceLncUtilGetAppTitleId()` 解出 TID。这两个 API 在本仓库已经用过
 * （`src/ps5_launcher.c` 的 `ps5_kill_disc_player`），不是猜的。
 *
 * **不用** `/mnt/shadowmnt/<TID>` 那种「挂载点在不在」的判据：那只对
 * ShadowMount 挂出来的镜像/裸目录游戏成立，内置 PKG 游戏根本没有那个目录
 * —— 而那恰恰是最需要自动存档的一类。LncUtil 给的是**系统层面**的
 * 「前台大应用是谁」，与来源类别无关。
 *
 * ── 三个刻意为之的设计 ───────────────────────────────────────────────────
 * ① **守护线程起来后的第一轮只记状态、不动作**。面板可能是「游戏已经在跑」
 *    的时候才起来的（重启 payload、玩到一半才打开面板）。这时候补一次
 *    「启动时拉取」会把正在玩的存档用远程那份盖掉 —— 那是最坏的一种数据丢失。
 * ② **退出后等几秒再推**。app_id 变成 0xffffffff 只说明进程没了，主机可能
 *    还在收尾写盘；立刻打包有可能拷到半截。宁可晚几秒。
 * ③ **换游戏（A→B）时先推 A 再拉 B**，顺序固定。两件事本来就会同时发生，
 *    写死顺序才有一份可预期的日志。
 */

#define GA_ACT_NONE 0
#define GA_ACT_PULL 1
#define GA_ACT_PUSH 2

/* 一步动作：act + 作用在哪个游戏上。 */
typedef struct {
    int act;
    char tid[32];
} ga_step_t;

/* 纯函数（宿主可穷举）：上一轮在跑 prev、这一轮在跑 cur 时该做什么。
 * 返回步数（0~2），顺序恒为「先推 prev、再拉 cur」。
 *
 * prev / cur 为空串 = 那一轮没有游戏在跑。`*_enabled` 是那款游戏的自动存档
 * 总开关，`*_on_exit` / `*_on_launch` 是两个时机的开关。
 * 单独抽出来是因为这是整条链上**唯一有分支逻辑**的地方，而它跑在真机上 ——
 * 宿主能把 2×2×2 组合全部跑一遍，比在真机上试靠谱得多。 */
int game_auto_plan(const char *prev, const char *cur,
                   int prev_enabled, int prev_on_exit,
                   int cur_enabled, int cur_on_launch,
                   ga_step_t out[2]);

/* 守护线程。main.c 启动时调用一次。safe_mode 下整条跳过（与风扇、名字自愈、
 * 本地快照三条守护同一原则：死机后要能止血，后台自动动作一律不做）。 */
void game_auto_start_daemon(void);

/* POST /game_save_sync：手动推 / 拉一次。
 *   {tid, action:"push"|"pull", uid?}   uid 省略 = 这个游戏的全部用户
 * 0 成功（buf 已填完整 JSON 响应体），-1 参数非法或全部失败（调用方回 400，
 * msg 里是给人看的原因）。 */
int game_auto_sync_action(const char *body, size_t len,
                          char *buf, size_t size, char *msg, size_t msgsz);

/* 该游戏此刻的自动存档运行时状态，给 /game_saves 的 auto_state 用：
 *   {"running":bool,"last":{"action":"push","ok":true,"at":"2026-10-07 20:40","message":"…"}}
 * 没有历史动作时 last 是 null。返回写入长度，<0 失败。 */
int game_auto_state_json(const char *tid, char *buf, size_t size);

/* [CN-FORK] 宿主测试专用：换掉「哪个游戏在跑」的探测（真机上走 sceLncUtil，
 * 宿主上根本没有那个符号）。传 NULL 恢复默认。产品代码不要调它。 */
typedef int (*ga_probe_fn)(char *tid, size_t osz);
void game_auto_set_probe(ga_probe_fn fn);

#endif
