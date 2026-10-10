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
/* [CN-FORK] v0.5.2.154 本地快照改由「游戏退出」触发（原来是定时守护，已删）。
 * 它和推 / 拉走同一条扫描线程，所以也做成一个 step —— 这样「退出那一刻该做哪几件
 * 事、按什么顺序」全在一张表里（game_auto_plan），宿主能穷举，不用真机试。 */
#define GA_ACT_SNAP 3

/* 一步动作：act + 作用在哪个游戏上。 */
typedef struct {
    int act;
    char tid[32];
} ga_step_t;

/* 纯函数（宿主可穷举）：上一轮在跑 prev、这一轮在跑 cur 时该做什么。
 * 返回步数（0~3），顺序恒为「先本地快照、再推 prev、最后拉 cur」。
 *
 * prev / cur 为空串 = 那一轮没有游戏在跑。`*_enabled` 是那款游戏的自动存档
 * 总开关，`*_on_exit` / `*_on_launch` 是两个时机的开关，`prev_snap` 是本地快照
 * 总开关（**独立于自动存档总开关** —— 本地快照不依赖网络，不该被远程那套连坐）。
 * 单独抽出来是因为这是整条链上**唯一有分支逻辑**的地方，而它跑在真机上 ——
 * 宿主能把组合全部跑一遍，比在真机上试靠谱得多。 */
int game_auto_plan(const char *prev, const char *cur,
                   int prev_enabled, int prev_on_exit, int prev_snap,
                   int cur_enabled, int cur_on_launch,
                   ga_step_t out[3]);

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

/* ── [CN-FORK] v0.5.2.150 拉取期间冻住游戏 ────────────────────────────────────
 * [CN-FORK] v0.5.2.152 冻的方式从「LncUtil 挂起」换成「进程级 SIGSTOP」。
 *
 * ── 它补的是哪条竞态 ─────────────────────────────────────────────────────
 * 拉取 = 把盘上存档**整份换掉**。而游戏启动时会把它的存档读进内存 —— 如果那一下
 * 发生在我们的拉取**之前**，换盘上的文件对它毫无影响：它照样用内存里那份，
 * 之后（存档 / 退出）还会把旧的写回去，顺手把刚拉下来的覆盖掉。
 *
 * 所以「游戏启动时拉取」本来是一场**竞态**：赢不赢取决于我们的拉取有没有赶在
 * 游戏读存档之前。用户口径「偶尔成功，偶尔失败」就是这个 —— 快启动的游戏输、
 * 慢启动的游戏赢。`.149` 修的「必然失效」（目录 inode 被换掉）跟它不是一回事。
 *
 * 冻住它把竞态变成**没有竞态**：这段时间它读不到存档，我们慢慢拉，拉完再放行。
 *
 * ── 为什么是 SIGSTOP，不是 LncUtil 的 SuspendApp ────────────────────────────
 * `.150`/`.151` 用的是 `sceLncUtilSuspendApp` / `ResumeApp`。冻结本身是有效的，
 * 但真机验收暴露出一个**副作用**：`SuspendApp` 会让系统把游戏换到后台（跳回主屏），
 * 而 `ResumeApp` **只解冻、不切回前台** —— 用户必须手动点一下游戏才回得去
 * （用户口径：「点击开始，游戏会自动进后台……手动点击按钮会恢复，但是不自动恢复」）。
 *
 * `.152` 用 `lnc_probe` 探针（`_scratch/lnc_probe.c`）在真机上逐个试过「切回前台」
 * 的候选 API，**全部失败**，而且失败方式很值得记一笔 —— 它们**返回 0 却毫无效果**：
 *
 *     sceLncUtilResumeApp(57369)               = 0x00000000
 *     sceLncUtilSetAppFocus(57369, 0)          = 0x00000000   ← 成功但无效
 *     sceLncUtilSetAppFocus(57369, 1)          = 0x00000000   ← 成功但无效
 *     sceLncUtilSetControllerFocus(57369, 0)   = 0x00000000   ← 成功但无效
 *     sceLncUtilSetControllerFocus(57369, 1)   = 0x00000000   ← 成功但无效
 *     sceLncUtilContinueApp(57369)             = 0x80020010   ← EBUSY
 *
 * 结论：**display 归 ShellUI 的 scene manager 管，不是这几个 LncUtil 调用能撬的**。
 * 公开资料里也查不到它们的签名（SDK 的 libSceSystemService.so 是桩库、所有符号同址
 * 0xaae4；shadPS4 里是空桩；psdevwiki / grep.app / LightningMods 的 daemon writeup
 * 全都只有名字没有参数），继续猜下去性价比太低。
 *
 * 于是换思路：**别让游戏离开前台**。`SIGSTOP` 只把游戏进程停住，系统状态一点没变，
 * 所以「切回前台」这个问题**根本不存在**。它同时更贴近用户的原话「暂停游戏」——
 * 用户看到的是游戏画面冻住一两秒然后继续，而不是被踢回主屏。
 *
 * 冻结/放行**都要验证**（`ki_stat` + `ki_runtime`，见 `src/game_auto.c`），不是
 * 「kill 返回 0 就算冻住了」——`kill()` 返回 0 只说明信号发出去了。
 * SIGSTOP 走不通时回落到原来的 LncUtil 挂起（慢、会跳主屏，但至少有效）。
 *
 * ── 为什么只用在「自动拉取」───────────────────────────────────────────────
 * 那一刻游戏还在启动、**还没读到存档** —— 冻住它才有效果。
 * 手动拉取时游戏早就把存档读进内存了，冻不冻都改变不了它内存里那份（白冻一场，
 * 还会平白把用户正在看的画面冻住）；真要生效得让游戏重读存档，那是另一件事。
 *
 * 宿主测试用 `-DGA_HOST` 关掉真实现（宿主上既没有 `sceLncUtil*`，也没有 PS5 的
 * `sysctl(KERN_PROC)` + `kinfo_proc`），再装桩。产品代码不要调 setter。
 *
 * ── ⚠ 放行**必须**用冻结时记下的那个凭据（`.150` 真机首轮踩的坑）────────────
 * LncUtil 那条路：应用被挂起后 `sceLncUtilGetAppIdOfRunningBigApp()` 返回
 * `0xffffffff` —— 系统认为「没有正在运行的大应用」（挂起本来就是把它换出去）。
 * 所以恢复时**再去解析一次 app_id 必然拿不到**，`ResumeApp` 根本发不出去，
 * 游戏会一直冻着。真机日志的原形：`恢复失败` 的**下一秒**就是 `已退出` ——
 * 那不是游戏真的退了，是我们自己丢了句柄（用户只能强退重进）。
 *
 * SIGSTOP 那条路同理：凭据是 **pid**，重找会拿到一个新进程（游戏重启过的话），
 * 而真正被冻住的那个还停在那儿没人放。
 *
 * 所以 `ga_frz_fn` 成功时**必须**把凭据写出来，`ga_thaw_fn` 收到的就是它。
 * 这条不变量在宿主上有断言（放行拿到的 handle == 冻结报的那个）。 */
typedef int (*ga_frz_fn)(const char *tid, int *kind_out, unsigned int *handle_out);
typedef int (*ga_thaw_fn)(const char *tid, int kind, unsigned int handle);
void game_auto_set_freezer(ga_frz_fn frz, ga_thaw_fn thaw);

/* `kind_out` 的取值（与 src/game_auto.c 里那份保持一致）。 */
#define GA_FREEZE_NONE 0    /* 没冻 */
#define GA_FREEZE_SIGSTOP 1 /* handle = pid */
#define GA_FREEZE_LNC 2     /* handle = app_id */

/* ── [CN-FORK] v0.5.2.153 冻结验证的**纯决策**（宿主上穷举 16 种组合）─────────
 * 返回：1 = 确认冻住 · **0 = 无法验证（按已冻处理，绝不回落 LncUtil）** ·
 *       -1 = 确认没冻住（放行 + 回落）。
 * 参数见 `src/game_auto.c` 的定义处。**导出是刻意的**：`.152` 真机栽的那一跤
 * 是**策略**错（验证读不到数据时错误地放弃了 SIGSTOP → 回落 → 游戏跳后台切不回来），
 * 而真机那段代码在 `#ifdef GA_HOST` 里、宿主编不到 —— 不导出就等于这个坑零覆盖。 */
int ga_freeze_verdict(int have_ctl, int ctl_adv, int post_ok, int post_adv);

/* ── [CN-FORK] v0.5.2.154 拉取前的「谁更新」判定（纯函数，宿主穷举）──────────
 *
 * 起因（用户口径）：「远程拉取游戏的时候要做一个判定，如果本地存档比远程存档要
 * 更新，则不拉取；本地存档比远程存档旧，可以直接拉取覆盖」。
 *
 * 为什么必须判：拉取是**整份覆盖**。用户昨晚玩到很晚但没推上去，今天开机自动拉取
 * 就把昨晚的进度用远程那份（更旧）盖掉了 —— 这是数据丢失，比「拉不动」严重得多。
 *
 * [CN-FORK] v0.5.2.162 追加的口径（用户原话）：「如果本地存档和远程存档一模一样，
 * 则不进行拉取，也就是说，只有远程的存档比本地的新的时候才进行拉取」。
 *
 * ⚠ `.154` 那一版**做不到**这句话，根因不在判定而在**比的是哪个时间**：
 *   `remote_mtime` 是远端那个文件**落盘的时刻**（SMB 取 zip 的文件 mtime，
 *   FTP/HTTP 直接写 time(NULL)），而 `local_mtime` 是存档**被游戏写下的时刻**。
 *   后者永远更早 —— 于是「刚推上去的这份」在对面看起来永远比本地新，
 *   自动拉取会原样把它拉回来。把判据从「>」改成「>=」也治不了，两者根本不相等。
 *   修法是让远端那份带上 `src_mtime`（打包时本地存档的最大 mtime，见 sr_stat），
 *   这样比的才是同一种量；拉回来的文件落盘时包里时间会被 utimes 还原，
 *   所以「推上去 → 拉回来」之后本地仍是 src_mtime，下一轮就判成一模一样、不再动。
 *
 * 参数（`*_have` = 那一侧到底有没有这份存档）：
 *   local_have      = game_saves_local_mtime() 的返回值
 *   remote_have     = 远端 <uid>.zip 是否存在（sr_stat 拿到了元数据）
 *   remote_src_mtime= 远端那份包的来源时刻；**0 = 说不清**（`.162` 之前推的老包）
 * 返回 1 = 该拉 · 0 = 跳过。
 *
 * 两条出口各自的条件：
 *   有 src_mtime（>0）→ **严格更新才拉**：src_mtime > local_mtime 才拉，
 *     相等（就是本地这份推上去的）和更小（远程那份更旧）都不拉。
 *   没有 src_mtime（==0）→ 退回 `.154` 口径：只有能确定「本地比远程新」才跳过。
 *
 * **「说不清」一律按「该拉」处理**：时间戳是拿来做优化判断的，不是安全边界。
 * 老包没有 src_mtime、meta.json 读不到时不能因此不拉 —— 那会让拉取功能在这些
 * 存档上直接失效，而用户真正想要的是「别用旧盖新 / 别做重复下载」，不是「宁可不拉」。
 * 这条与 `.153` 冻结验证那条同一个道理：**验证不可用 ≠ 该放弃**。 */
int ga_pull_needed(long long local_mtime, int local_have,
                   long long remote_mtime, long long remote_src_mtime,
                   int remote_have);

/* 跑一轮扫描。守护线程每秒调一次（`GA_TICK`）。
 *
 * **导出是刻意的**：拉取前挂起游戏那条逻辑只在这条路上（`ga_do` 的 freeze 参数
 * 只有这里传 1），不导出就等于零覆盖 —— 而这正是本轮唯一动到「游戏运行链路」的
 * 改动，最需要宿主先跑一遍。宿主测试直接调它，配合 `game_auto_set_probe` /
 * `game_auto_set_freezer` 就能把「冻住 → 拉取 → 放行」的**顺序**钉死。 */
void game_auto_tick(void);

#endif
