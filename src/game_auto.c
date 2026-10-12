/* [CN-FORK] v0.5.2.142 自动存档执行引擎。设计（为什么用 LncUtil 而不是挂载点、
 * 三个刻意为之的设计）见 include/game_auto.h 的文件头。 */

#include "game_auto.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* [CN-FORK] v0.5.2.163 ga_tid_is_homebrew 用 stat(2) */
#include <time.h>
#include <unistd.h>

#ifndef GA_HOST
/* sys/user.h **不是自包含的**（它链到的 x86/fpu.h 直接用 uint8_t 却不带 stdint.h），
 * 上面那个 <stdint.h> 必须排在它前面 —— lnc_probe 探针上刚踩过这个坑。 */
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

#include "cJSON.h"
#include "config.h" /* [CN-FORK] v0.5.2.145 读全局存档配置（设置页「存档配置」） */
#include "game_saves.h"
#include "notification.h" /* [CN-FORK] v0.5.2.147 推 / 拉的「开始 / 结束」屏幕通知 */
#include "pldmgr.h"
#include "save_remote.h"
#include "utils.h"

/* ── 运行中游戏的探测 ───────────────────────────────────────────────────── */
/* 这两个符号只有真机的 SDK 里有（ps5_launcher.c 已经在用）。
 *
 * `-DGA_HOST` = **宿主构建**：既没有 `sceLncUtil*` 符号，也没有 PS5 那套
 * `sysctl(KERN_PROC)` + `struct kinfo_proc`（Linux 的 `<sys/user.h>` 是另一回事，
 * 根本没有 `kinfo_proc`）。所以凡是碰「真机进程 / 应用状态」的地方一律用它挡住，
 * 宿主测试再自己装桩。
 *
 * 名字是 `.152` 从 `GA_NO_LNCUTIL` 改过来的：这一轮起它挡的已经不只是 LncUtil，
 * 还挡进程枚举 —— 名字得跟实际含义对上，否则下一个人会以为宿主上只是没有 LncUtil。 */
#ifndef GA_HOST
extern int sceLncUtilGetAppIdOfRunningBigApp(void);
extern int sceLncUtilGetAppTitleId(unsigned int app_id, char *title_id);
#endif

/* [CN-FORK] v0.5.2.163 自制应用屏蔽：自动推 / 拉 + 本地快照 都跳过 homebrew。
 *
 * 判据：游戏物理源落在 `/data/homebrew/` 或 `/data/etaHEN/games/` 下 —— 这是
 * game_mgr.c 里那两条 scan path（已用来枚举游戏）。如果是夹在这些目录里，
 * 即使 TID 是合法 9 位（PS5 homebrew 可以是 PPSA 前缀的伪造 TID），也按 homebrew 跳过。
 *
 * `ga_tid_is_homebrew` 只查 `stat(2)` —— 不读文件，不开 fd，**所以**路径
 * 缓冲直接放在栈上（256B 远低于 32KB 栈帧红线）。探测失败 = 当作不是 homebrew。 */
static int ga_tid_is_homebrew(const char *tid) {
    if (!tid || !tid[0]) return 0;
    char p1[256], p2[256];
    snprintf(p1, sizeof(p1), "/data/homebrew/%s", tid);
    snprintf(p2, sizeof(p2), "/data/etaHEN/games/%s", tid);
    struct stat st;
    if (stat(p1, &st) == 0) return 1;
    if (stat(p2, &st) == 0) return 1;
    return 0;
}

static int ga_probe_default(char *out, size_t osz) {
    if (out && osz) out[0] = 0;
#ifdef GA_HOST
    return 0;
#else
    int app_id = sceLncUtilGetAppIdOfRunningBigApp();
    if (app_id == -1) return 0; /* 0xffffffff：没有大应用在跑 */
    char t[24];
    t[0] = 0;
    if (sceLncUtilGetAppTitleId((unsigned int)app_id, t) != 0) return 0;
    if (!t[0]) return 0;
    snprintf(out, osz, "%s", t);
    return 1;
#endif
}

static ga_probe_fn g_probe = ga_probe_default;

void game_auto_set_probe(ga_probe_fn fn) { g_probe = fn ? fn : ga_probe_default; }

/* ── 冻结 / 放行（拉取期间冻住正在启动的那款游戏）─────────────────────────── */
/* 设计（为什么需要、为什么最终落在进程级 SIGSTOP 上）见 include/game_auto.h 文件尾。
 *
 * 接口刻意收在 **tid** 上而不是 app_id / pid 上：那两个每次启动都不一样、宿主上
 * 也没法造，而 tid 是测试本来就有的东西。tid → 具体进程 / 应用 的解析放在默认
 * 实现里（真机那一步）。
 *
 * `handle` 是**不透明的冻结凭据**：SIGSTOP 方案下它是 pid，回落方案下它是 app_id。
 * 放行时**必须原样交回**，不许当场重新解析 —— `.150` 真机首轮就是栽在
 * 「恢复时又解析一次 app_id」上（见头文件）。
 *
 * `GA_FREEZE_*` 三个取值定义在 include/game_auto.h 里（宿主测试也要用）。 */

/* ── 冻结验证的**纯决策**（宿主可穷举）──────────────────────────────────────
 * 把「能不能算冻住了」从真机代码里摘出来，是因为 `.152` 真机栽的那一跤恰好是
 * **策略**错、不是机制错：验证读不到数据时它选择了「放弃 SIGSTOP」→ 回落 LncUtil
 * → 游戏跳回主屏、还切不回来（用户口径：「远程拉取成功后，并没有将游戏拉到前台」）。
 * 而真机那整段代码在 `#ifdef GA_HOST` 里、宿主上根本编不到，所以决策必须是
 * 独立、可穷举的纯函数 —— 否则同一个坑下次还会踩。
 *
 *   have_ctl  读到「冻结前」的对照样本了吗
 *   ctl_adv   对照那 200ms 里 CPU 时间**有没有前进**（0 = 进程本来就没烧 CPU，
 *             这时「冻住后不动」什么也说明不了）
 *   post_ok   冻结后两次采样都读到了吗
 *   post_adv  冻结后 CPU 时间**还在前进**吗
 *
 * 返回： 1 = 确认冻住 · 0 = 无法验证（**按已冻处理，绝不回落**）· -1 = 确认没冻住 */
int ga_freeze_verdict(int have_ctl, int ctl_adv, int post_ok, int post_adv) {
    if (!have_ctl || !ctl_adv) return 0; /* 对照不可用 → 验证不可用，但照冻 */
    if (!post_ok) return 0;              /* 冻结后读不到 → 同上 */
    return post_adv ? -1 : 1;
}

#ifndef GA_HOST
extern int sceLncUtilSuspendApp(unsigned int app_id);
extern int sceLncUtilResumeApp(unsigned int app_id);

/* 枚举进程表找游戏主进程。判据 `comm == "eboot.bin"`（与 ps-game-state-lib 同）。
 * 手法抄 src/ps5_launcher.c 的 get_pid_by_name —— 那边真机给 SceDiscPlayer 发过
 * SIGKILL，`sysctl(KERN_PROC)` 枚举 + 跨用户发信号都是实证过的。
 * 取**最大 pid**：游戏重启过时旧进程可能还在收尾，pid 大的才是当前那个。 */
static pid_t ga_find_eboot_pid(void) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t sz = 0;
    void *buf;
    pid_t best = -1;

    if (sysctl(mib, 4, NULL, &sz, NULL, 0) != 0 || sz == 0) return -1;
    buf = malloc(sz); /* 大缓冲一律 malloc：连接线程栈只有 1MB，别往栈上放 */
    if (!buf) return -1;
    if (sysctl(mib, 4, buf, &sz, NULL, 0) != 0) {
        free(buf);
        return -1;
    }

    for (char *p = (char *)buf; p < (char *)buf + sz;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        if (ki->ki_structsize < (int)sizeof(struct kinfo_proc)) break;
        if (strncmp(ki->ki_comm, "eboot.bin", 9) == 0 && ki->ki_pid > best)
            best = ki->ki_pid;
        p += ki->ki_structsize;
    }

    /* 一次性把「编译期 struct 大小」与「内核报的 ki_structsize」记进日志。
     * `.152` 真机就是「按 pid 单查读不到状态」而回落 LncUtil 的（怀疑 SDK 头与
     * 固件内核的 kinfo_proc 版本对不上）—— 有这两个数，下次一眼就能判定是不是
     * 版本错配、以及 ki_runtime 到底读在不在正确偏移上。 */
    {
        static int layout_logged = 0;
        struct kinfo_proc *first = (struct kinfo_proc *)buf;
        if (!layout_logged && sz >= sizeof(struct kinfo_proc) && first->ki_structsize > 0) {
            layout_logged = 1;
            pldmgr_log("[PLDMGR] saves: kinfo_proc 编译期 %u 字节 · 内核报 %d 字节 · 共 %u 条\n",
                       (unsigned)sizeof(struct kinfo_proc), first->ki_structsize,
                       (unsigned)(sz / (size_t)first->ki_structsize));
        }
    }

    free(buf);
    return best;
}

/* 读某个进程的「状态字 + 累计 CPU 时间（微秒）」。拿不到返回 -1。
 * 这两样是**冻结验证**的全部依据：冻住之后 CPU 时间必须不再前进。
 *
 * ⚠ 实现走的是**整表枚举**，不是 `KERN_PROC_PID`。`.152` 真机实证：按 pid 单查
 * 那一路在 PS5 上**直接失败**（整表能列出来、单查查不到），于是 `ga_sigstop_freeze`
 * 在发 SIGSTOP **之前**就 return -1，整条主方案作废、回落 LncUtil，游戏又跳后台。
 * 枚举这条路的可用性是**真机验过的**（`ga_find_eboot_pid` 就是靠它找到 pid 的），
 * 所以验证也走它。代价是每次 O(进程数) + 一次 malloc，一轮冻结最多调 4 次，可以接受。 */
static int ga_proc_stat(pid_t pid, int *state_out, unsigned long long *cpu_out) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t sz = 0;
    void *buf;
    int found = -1;

    if (sysctl(mib, 4, NULL, &sz, NULL, 0) != 0 || sz == 0) {
        pldmgr_log("[PLDMGR] saves: 进程表探测失败，errno=%d\n", errno);
        return -1;
    }
    buf = malloc(sz);
    if (!buf) return -1;
    if (sysctl(mib, 4, buf, &sz, NULL, 0) != 0) {
        pldmgr_log("[PLDMGR] saves: 读进程表失败，errno=%d\n", errno);
        free(buf);
        return -1;
    }

    for (char *p = (char *)buf; p < (char *)buf + sz;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        if (ki->ki_structsize < (int)sizeof(struct kinfo_proc)) break;
        if (ki->ki_pid == pid) {
            if (state_out) *state_out = (int)ki->ki_stat;
            if (cpu_out) *cpu_out = (unsigned long long)ki->ki_runtime;
            found = 0;
            break;
        }
        p += ki->ki_structsize;
    }

    free(buf);
    return found;
}
#endif

/* 把 tid 解析成「正在跑的那个大应用」的 app_id；不是这款在跑就返回 -1。
 * 返回 int（而不是 unsigned）是因为 0xffffffff 这个「没有应用在跑」的哨兵值
 * 在 ps5_launcher.c 里就是按 int -1 比的。
 *
 * 整个函数包在 `#ifndef` 里：宿主构建下它没人用，留着只会换来一条
 * -Wunused-function。 */
#ifndef GA_HOST
static int ga_running_app_id(const char *tid) {
    int app_id = sceLncUtilGetAppIdOfRunningBigApp();
    if (app_id == -1) return -1; /* 0xffffffff：没有大应用在跑 */
    char t[24];
    t[0] = 0;
    if (sceLncUtilGetAppTitleId((unsigned int)app_id, t) != 0) return -1;
    if (!t[0] || strcmp(t, tid) != 0) return -1;
    return app_id;
}
#endif

/* ── 主方案：进程级冻结（SIGSTOP）─────────────────────────────────────────
 * 调用方（ga_freeze_for_pull）已经确认「正在跑的就是 tid 这一款」，所以这里
 * 找到的那个 eboot.bin 就是它，不必再比一次 tid。
 *
 * ⚠ **验证不可用 ≠ 放弃冻结**（`.152` 真机踩出来的）：
 * 「冻住之后 CPU 时间不再前进」只是**加分项**，不是前提。读不到进程状态、
 * 或进程本来就没在烧 CPU（对照样本不动）时，验证只是**不可用** ——
 * 绝不能因此 return -1，那会回落 LncUtil，游戏又跳后台。
 * 判据集中在 `ga_freeze_verdict()`（纯函数，宿主上 16 种组合穷举）。 */
static int ga_sigstop_freeze(const char *tid, unsigned int *handle_out) {
#ifdef GA_HOST
    (void)tid;
    (void)handle_out;
    return -1;
#else
    pid_t pid = ga_find_eboot_pid();
    if (pid <= 0) {
        pldmgr_log("[PLDMGR] saves: 找不到 %s 的游戏主进程（eboot.bin），进程级冻结跳过\n", tid);
        return -1;
    }

    /* 先量一个**对照**：运行中的游戏在 200ms 里通常用掉 CPU。有这个对照，
     * 「冻住后 CPU 不动」才说明得了问题 —— 否则会被「它本来就在 loading、
     * 也没在烧 CPU」蒙混过去。但对照拿不到时只是**验证不可用**，照冻不误。 */
    int st_pre = 0, st_post = 0;
    unsigned long long c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    int have_ctl = (ga_proc_stat(pid, &st_pre, &c0) == 0);
    int ctl_adv = 0;
    if (have_ctl) {
        usleep(200000);
        if (ga_proc_stat(pid, &st_pre, &c1) == 0 && c1 > c0) ctl_adv = 1;
    }

    if (kill(pid, SIGSTOP) != 0) {
        pldmgr_log("[PLDMGR] saves: kill(%d, SIGSTOP) 失败，errno=%d\n", (int)pid, errno);
        return -1;
    }

    int post_ok = 0, post_adv = 0;
    if (have_ctl && ctl_adv) {
        usleep(200000);
        post_ok = (ga_proc_stat(pid, &st_post, &c2) == 0);
        if (post_ok) {
            usleep(200000);
            post_ok = (ga_proc_stat(pid, &st_post, &c3) == 0);
            if (post_ok) post_adv = (c3 != c2);
        }
    }

    int v = ga_freeze_verdict(have_ctl, ctl_adv, post_ok, post_adv);
    if (v < 0) {
        /* 冻不住就**立刻放行**，绝不把一个半死的进程留在那儿，然后让调用方
         * 走 LncUtil 回落 —— 那条路慢一点、会跳主屏，但至少是有效的。 */
        pldmgr_log("[PLDMGR] saves: SIGSTOP 后 pid=%d 的 CPU 仍在涨（+%llu us）→ 冻结无效，"
                   "改走 LncUtil 挂起\n", (int)pid, c3 - c2);
        kill(pid, SIGCONT);
        return -1;
    }

    if (v == 1) {
        /* 这行是**证据行**：留档之后，「SIGSTOP 到底有没有把这个进程停住」不用再猜。
         * stat 那两个数也一并记下来 —— PS5 的 SSTOP 具体是几，目前只有这一处能告诉我们。 */
        pldmgr_log("[PLDMGR] saves: 冻结验证 pid=%d：CPU 200ms +%llu us → +0 us · stat %d→%d\n",
                   (int)pid, c1 - c0, st_pre, st_post);
    } else {
        /* 验证不可用（读不到进程状态 / 对照样本没推进）。**仍然算冻住** ——
         * SIGSTOP 是内核级停止，`kill()` 返回 0 就说明信号已投递。 */
        pldmgr_log("[PLDMGR] saves: 冻结验证不可用（pid=%d 的进程状态读不到，或 CPU 对照未推进）"
                   "· 按 SIGSTOP 已投递处理\n", (int)pid);
    }
    if (handle_out) *handle_out = (unsigned int)pid;
    return 0;
#endif
}

static int ga_sigstop_thaw(const char *tid, unsigned int handle) {
#ifdef GA_HOST
    (void)tid;
    (void)handle;
    return -1;
#else
    pid_t pid = (pid_t)handle;
    if (pid <= 0) return -1;
    if (kill(pid, SIGCONT) != 0) {
        pldmgr_log("[PLDMGR] saves: kill(%d, SIGCONT) 失败，errno=%d\n", (int)pid, errno);
        return -1;
    }
    /* 顺手确认进程还在 —— 不在了说明游戏在我们冻着它的时候被系统清掉了，
     * 那是完全另一类问题，得让它出现在日志里而不是被静悄悄吞掉。 */
    if (ga_proc_stat(pid, NULL, NULL) != 0)
        pldmgr_log("[PLDMGR] saves: 放行后 pid=%d 已不在进程表里\n", (int)pid);
    return 0;
#endif
}

/* ── 回落方案：LncUtil 挂起 / 恢复 ────────────────────────────────────────
 * 只在 SIGSTOP 走不通时用（找不到 eboot.bin / 发不出信号 / 停不住）。
 *
 * ⚠ 它的代价是真机上**看得见**的：SuspendApp 会让系统把游戏换到后台（跳主屏），
 * 而 `ResumeApp` **只解冻、不切回前台** —— `.152` 的探针把这件事钉死了：
 * `SetAppFocus` / `SetControllerFocus` 两个候选都**返回 0 却毫无效果**，
 * `ContinueApp` 直接 EBUSY(0x80020010)。所以走这条路时用户必须手动点一下游戏。
 * 记在这里，免得以后有人以为「返回 0 就是成功了」。 */
static int ga_lnc_freeze(const char *tid, unsigned int *handle_out) {
#ifdef GA_HOST
    (void)tid;
    (void)handle_out;
    return -1;
#else
    int app_id = ga_running_app_id(tid);
    if (app_id < 0) return -1;
    int rc = sceLncUtilSuspendApp((unsigned int)app_id);
    if (rc != 0) {
        pldmgr_log("[PLDMGR] saves: sceLncUtilSuspendApp(%d) 返回 0x%08x\n", app_id,
                   (unsigned)rc);
        return -1;
    }
    /* 把 app_id 交出去 —— **恢复只能用它**（挂起后 GetAppIdOfRunningBigApp
     * 会返回 0xffffffff，见头文件）。 */
    if (handle_out) *handle_out = (unsigned int)app_id;
    /* SuspendApp 是**请求**，返回时应用未必真的冻住了。与 ps5_launcher.c 的
     * `ps5_kill_disc_player` 同一口径（那里也是 Suspend → sleep(2) → 下一步）。
     * 这里没有轮询 IsAppSuspended：那个符号虽然也在 libSceSystemService.so 里，
     * 但**没有头文件、签名是猜的**，为了省一两秒去赌一个原生调用不值得。 */
    sleep(2);
    return 0;
#endif
}

static int ga_lnc_thaw(const char *tid, unsigned int handle) {
#ifdef GA_HOST
    (void)tid;
    (void)handle;
    return -1;
#else
    /* **不再解析 app_id**，直接用挂起时那个（头文件里有真机证据）。
     * 顺带把「此刻 GetAppIdOfRunningBigApp 报什么」记进日志 —— 这一行是
     * 「挂起后系统就不认这个大应用了」的直接证据，下次再出问题不用猜。 */
    pldmgr_log("[PLDMGR] saves: 恢复 %s：挂起时 app_id=%u，此刻 GetAppIdOfRunningBigApp=%d\n",
               tid, handle, sceLncUtilGetAppIdOfRunningBigApp());
    int rc = sceLncUtilResumeApp(handle);
    if (rc != 0)
        pldmgr_log("[PLDMGR] saves: sceLncUtilResumeApp(%u) 返回 0x%08x\n", handle, (unsigned)rc);
    return rc == 0 ? 0 : -1;
#endif
}

/* 默认冻结：**先试进程级 SIGSTOP，不行再回落 LncUtil 挂起**。
 *
 * 顺序不能反。SIGSTOP 只把游戏进程冻住，**游戏压根没离开前台** —— 于是
 * 「拉完怎么把游戏切回前台」这个问题根本不存在。而 LncUtil 那条路会跳主屏、
 * 又切不回来（`.152` 探针实证），是实打实的体验倒退。 */
static int ga_frz_default(const char *tid, int *kind_out, unsigned int *handle_out) {
    unsigned int h = 0;

    if (ga_sigstop_freeze(tid, &h) == 0) {
        if (kind_out) *kind_out = GA_FREEZE_SIGSTOP;
        if (handle_out) *handle_out = h;
        return 0;
    }
    if (ga_lnc_freeze(tid, &h) == 0) {
        if (kind_out) *kind_out = GA_FREEZE_LNC;
        if (handle_out) *handle_out = h;
        return 0;
    }
    return -1;
}

static int ga_thaw_default(const char *tid, int kind, unsigned int handle) {
    if (kind == GA_FREEZE_SIGSTOP) return ga_sigstop_thaw(tid, handle);
    if (kind == GA_FREEZE_LNC) return ga_lnc_thaw(tid, handle);
    return -1;
}

static ga_frz_fn g_frz = ga_frz_default;
static ga_thaw_fn g_thaw = ga_thaw_default;

void game_auto_set_freezer(ga_frz_fn frz, ga_thaw_fn thaw) {
    g_frz = frz ? frz : ga_frz_default;
    g_thaw = thaw ? thaw : ga_thaw_default;
}

/* 拉取前把正在跑的**这款**游戏冻住。返回 1 = 冻住了（`*kind_out` 是用了哪套机制、
 * `*handle_out` 是那个不透明凭据，拉完必须原样交回去放行）、0 = 没冻。
 *
 * 只冻目标那款：跑的是别的游戏时冻它是纯粹的伤害（人家跟我们这份存档没关系）。
 * 冻不住**不阻断拉取** —— 那只是退回到 `.149` 之前的老行为（有竞态、但比不拉强），
 * 而且「配了自动存档却什么都不做」比「拉了一次可能不生效」更难排查。 */
static int ga_freeze_for_pull(const char *tid, int *kind_out, unsigned int *handle_out) {
    if (kind_out) *kind_out = GA_FREEZE_NONE;
    if (handle_out) *handle_out = 0;

    char run[32];
    run[0] = 0;
    /* 这里**再探一次**：ga_tick 探测到 ga_do 之间隔着读配置、可能还有别的一步，
     * 游戏完全可能已经退出（尤其换游戏那一轮，先推后拉之间还睡了 GA_EXIT_SETTLE）。 */
    if (!g_probe(run, sizeof(run)) || strcmp(run, tid) != 0) return 0;

    int kind = GA_FREEZE_NONE;
    unsigned int handle = 0;
    if (g_frz(tid, &kind, &handle) != 0) {
        pldmgr_log("[PLDMGR] saves: %s 正在启动 · 暂停失败（进程级与 LncUtil 都失败），仍继续拉取"
                   "（游戏可能已经读进旧存档，这次拉取未必生效）\n", tid);
        pldmgr_notify("远程拉取：无法暂停 %s\n存档可能不生效", tid);
        return 0;
    }
    pldmgr_log("[PLDMGR] saves: %s 已暂停（%s）· 拉取完成后自动恢复\n", tid,
               kind == GA_FREEZE_SIGSTOP ? "进程级 SIGSTOP" : "LncUtil 挂起");
    if (kind_out) *kind_out = kind;
    if (handle_out) *handle_out = handle;
    return 1;
}

/* 拉完放行。**恢复失败必须让人知道**：游戏会一直停在冻结状态，用户看到的是
 * 「点了一下就死机了」，而真正的原因在面板日志里 —— 所以屏幕通知也要弹。
 *
 * `kind` / `handle` 是**冻结时记下的那一对**，不是当场重新解析的（`.150` 真机首轮
 * 就是栽在「恢复时又解析一次 app_id」上：挂起后系统不认这个大应用了，解析必然失败）。
 * 这条不变量对两套机制都成立 —— SIGSTOP 那边 handle 是 pid，**同样不许重找**：
 * 游戏重启过一次的话，重找会拿到一个新进程，而真正被冻住的那个还停在那儿没人放。 */
static void ga_thaw_after_pull(const char *tid, int kind, unsigned int handle) {
    const char *kn = (kind == GA_FREEZE_SIGSTOP) ? "pid" : "app_id";
    if (g_thaw(tid, kind, handle) == 0) {
        pldmgr_log("[PLDMGR] saves: %s 已恢复运行（%s=%u）\n", tid, kn, handle);
        return;
    }
    pldmgr_log("[PLDMGR] saves: !!! %s 恢复失败（%s=%u）—— 请手动切回游戏（或退出重进）\n",
               tid, kn, handle);
    pldmgr_notify("!!! 远程拉取：%s 恢复失败\n请手动切回游戏", tid);
}

/* ── 纯函数：状态机 ─────────────────────────────────────────────────────── */
int game_auto_plan(const char *prev, const char *cur,
                   int prev_enabled, int prev_on_exit, int prev_snap,
                   int cur_enabled, int cur_on_launch,
                   ga_step_t out[3]) {
    if (!out) return 0;
    const char *p = (prev && prev[0]) ? prev : NULL;
    const char *c = (cur && cur[0]) ? cur : NULL;
    /* 同一款游戏连着两轮都在跑 —— 什么都不做。这是绝大多数轮次的情况。 */
    if (p && c && !strcmp(p, c)) return 0;

    int n = 0;
    /* [CN-FORK] v0.5.2.154 退出那一刻的顺序：**先本地快照、再推送、最后拉下一款**。
     *
     * 本地快照排在最前是因为它是**不依赖任何外部条件**的那一层（网络断了、对面没开、
     * 密码错了都不影响它）—— 先把这份稳稳落盘，再去折腾网络。它与推送读的是同一份
     * 源目录，谁先谁后在数据上没差别，但「先本地后远程」出问题时更容易解释。
     *
     * 快照只看 `prev_snap`（本地快照自己的开关），**不跟 `prev_enabled` 联动**：
     * 那是远程同步的总开关，本地备份不该被它连坐。 */
    if (p && prev_snap) {
        out[n].act = GA_ACT_SNAP;
        snprintf(out[n].tid, sizeof(out[n].tid), "%s", p);
        n++;
    }
    if (p && prev_enabled && prev_on_exit) {
        out[n].act = GA_ACT_PUSH;
        snprintf(out[n].tid, sizeof(out[n].tid), "%s", p);
        n++;
    }
    if (c && cur_enabled && cur_on_launch) {
        out[n].act = GA_ACT_PULL;
        snprintf(out[n].tid, sizeof(out[n].tid), "%s", c);
        n++;
    }
    return n;
}

/* [CN-FORK] v0.5.2.154 拉取前的「谁更新」判定。纯函数，理由与全部边界见 game_auto.h。
 *
 * 一句话：**只有能确定「远程那一份确实比本地新」时才拉**，说不清的一律照拉。
 * 特别注意 `local_have == 0`（本地那份已经没了：换机 / 误删 / 重装）—— 那正是
 * 拉取要救的场景，永远该拉，哪怕时间戳看起来「本地更新」（本地都没有，哪来的新）。
 *
 * [CN-FORK] v0.5.2.162 用户追加的口径：「本地和远程一模一样就别拉，只有远程比本地
 * 新才拉」。`.154` 那版做不到 —— remote_mtime 是**远端文件落盘的时刻**，比存档本身
 * 的写入时刻晚，所以「刚推上去的这份」永远算「远程更新」，会被原样拉回来。
 * 这一轮把比的量换成包自己的来源时刻 src_mtime（推送时写进 meta.json），
 * 相等就是「同一份」→ 不拉；src_mtime 缺失（`.162` 之前推的老包）才退回旧口径。 */
int ga_pull_needed(long long local_mtime, int local_have,
                   long long remote_mtime, long long remote_src_mtime,
                   int remote_have) {
    if (!remote_have) return 0;        /* 远端没有 → 没得拉 */
    if (!local_have) return 1;         /* 本地没有 → 必须拉（换机/误删） */
    if (remote_src_mtime > 0) return remote_src_mtime > local_mtime;
    /* 说不清远程那份的来源（老包没有 src_mtime）→ 只能拿落盘时刻比，
     * 那条只用来防「用旧盖新」，不足以断定「一模一样」。 */
    if (local_mtime > remote_mtime) return 0;  /* 本地更新 → 不拿旧的盖新的 */
    return 1;
}

/* ── 配置 ───────────────────────────────────────────────────────────────── */
typedef struct {
    int enabled;
    int on_launch;
    int on_exit;
    sr_cfg_t rc;
} ga_cfg_t;

static void ga_str(cJSON *o, const char *k, char *out, size_t osz) {
    cJSON *f = o ? cJSON_GetObjectItem(o, k) : NULL;
    if (cJSON_IsString(f) && f->valuestring) snprintf(out, osz, "%s", f->valuestring);
}

/* 把一份 auto JSON（per-game 或全局，结构相同）填进 ga_cfg_t。
 * out 必须已 memset + 填好默认（见下面两个调用方）。 */
static void ga_fill_cfg(const char *json, ga_cfg_t *out) {
    cJSON *o = cJSON_Parse(json);
    if (!o || !cJSON_IsObject(o)) {
        cJSON_Delete(o);
        return;
    }

    cJSON *f = cJSON_GetObjectItem(o, "enabled");
    out->enabled = cJSON_IsTrue(f) ? 1 : 0;
    f = cJSON_GetObjectItem(o, "on_launch");
    if (cJSON_IsBool(f)) out->on_launch = cJSON_IsTrue(f) ? 1 : 0;
    f = cJSON_GetObjectItem(o, "on_exit");
    if (cJSON_IsBool(f)) out->on_exit = cJSON_IsTrue(f) ? 1 : 0;
    ga_str(o, "mode", out->rc.mode, sizeof(out->rc.mode));
    /* 端口缺省值**跟协议走**：ftp 21、smb 445。上面那个 21 只是占位，
     * 不在这儿改的话「SMB 但没填端口」会被当成 21 —— 一个不报错、
     * 只表现为连不上的坑（HTTP 不用端口）。 */
    out->rc.port = strcmp(out->rc.mode, "smb") == 0 ? 445 : 21;

    cJSON *sub = cJSON_GetObjectItem(o, out->rc.mode);
    if (sub && cJSON_IsObject(sub)) {
        ga_str(sub, "host", out->rc.host, sizeof(out->rc.host));
        ga_str(sub, "path", out->rc.path, sizeof(out->rc.path));
        ga_str(sub, "share", out->rc.share, sizeof(out->rc.share));
        ga_str(sub, "sub", out->rc.sub, sizeof(out->rc.sub));
        ga_str(sub, "user", out->rc.user, sizeof(out->rc.user));
        ga_str(sub, "password", out->rc.password, sizeof(out->rc.password));
        ga_str(sub, "url", out->rc.url, sizeof(out->rc.url));
        ga_str(sub, "token", out->rc.token, sizeof(out->rc.token));
        /* 端口：数字与数字字符串都认（前端 <input> 的 value 出来永远是字符串），
         * 与 game_saves.c 的 gs_get_port 同一口径。 */
        cJSON *p = cJSON_GetObjectItem(sub, "port");
        int v = 0;
        if (cJSON_IsNumber(p)) v = (int)p->valuedouble;
        else if (cJSON_IsString(p) && p->valuestring && p->valuestring[0])
            v = atoi(p->valuestring);
        if (v > 0 && v < 65536) out->rc.port = v;
    }

    cJSON_Delete(o);
}

/* 读一款游戏的自动备份配置。0 成功（out 已填默认值再覆盖），-1 没配置。
 *
 * [CN-FORK] v0.5.2.145 **全局优先**：设置页「存档配置」里那份（ARCHIVE_GLOBAL_PATH）
 * 存在就用它，没有才回落到这个游戏自己的 per-game 配置。这就是「全局生效」的落点 ——
 * 用户在新装机上设一次，所有没单独配过的游戏就都按它跑，不必逐个游戏进弹窗点保存。
 *
 * 反过来（per-game 优先）会让「全局」变成摆设：per-game 一旦被前端写过一次（哪怕是
 * 默认值），全局就再也影响不到它 —— 用户看到的是「我改了全局但没反应」。
 * 老装机全局文件不存在，走 per-game 分支，行为与 `.144` 完全一致。 */
static int ga_load_cfg(const char *tid, ga_cfg_t *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->rc.mode, sizeof(out->rc.mode), "ftp");
    out->rc.port = 21;
    /* 两个时机的**默认是开**，与 gs_auto_defaults 保持一致 —— 不一致的话
     * 「没配过的游戏」在 UI 上显示开、引擎却按关处理。 */
    out->on_launch = 1;
    out->on_exit = 1;

    char buf[4096];
    /* 全局（设置页「存档配置」）优先。读不到就当没这份，继续往下走。 */
    if (config_read_archive_global(buf, sizeof(buf)) > 0) {
        ga_fill_cfg(buf, out);
        return 0;
    }

    if (game_saves_auto_json(tid, buf, sizeof(buf)) < 0) return -1;
    ga_fill_cfg(buf, out);
    return 0;
}

/* ── 运行时状态（给 /game_saves 的 auto_state）──────────────────────────── */
#define GA_MAX_MARKS 24

typedef struct {
    char tid[32];
    int act;
    int ok;
    time_t when;
    char msg[160];
} ga_mark_t;

static pthread_mutex_t g_ga_mu = PTHREAD_MUTEX_INITIALIZER;
static ga_mark_t g_marks[GA_MAX_MARKS];
static int g_mark_n = 0;
static char g_prev_tid[32] = "";
static int g_first_tick = 1;

/* 每款游戏只留**最新一条**。表小、查起来直接，也不会被玩得多的那几款挤爆。 */
static void ga_mark(const char *tid, int act, int ok, const char *msg) {
    int slot = -1;
    for (int i = 0; i < g_mark_n; i++)
        if (!strcmp(g_marks[i].tid, tid)) { slot = i; break; }
    if (slot < 0) {
        if (g_mark_n < GA_MAX_MARKS) {
            slot = g_mark_n++;
        } else {
            slot = 0;
            for (int i = 1; i < g_mark_n; i++)
                if (g_marks[i].when < g_marks[slot].when) slot = i;
        }
    }
    snprintf(g_marks[slot].tid, sizeof(g_marks[slot].tid), "%s", tid);
    g_marks[slot].act = act;
    g_marks[slot].ok = ok;
    g_marks[slot].when = time(NULL);
    snprintf(g_marks[slot].msg, sizeof(g_marks[slot].msg), "%s", msg ? msg : "");
}

static void ga_fmt_time(time_t t, char *out, size_t osz) {
    struct tm tmv;
    out[0] = 0;
    if (t <= 0 || !localtime_r(&t, &tmv)) return;
    strftime(out, osz, "%Y-%m-%d %H:%M", &tmv);
}

int game_auto_state_json(const char *tid, char *buf, size_t size) {
    if (!buf || size < 64) return -1;
    buf[0] = 0;

    /* running 用**当场探测**而不是守护线程记的那个值：守护线程 GA_TICK 秒才醒一次，
     * 用户刚点了「立即推送」再看界面，滞后的状态会让人以为没生效。 */
    char run[32];
    run[0] = 0;
    g_probe(run, sizeof(run));
    int is_run = (run[0] && tid && !strcmp(run, tid)) ? 1 : 0;

    pthread_mutex_lock(&g_ga_mu);
    const ga_mark_t *m = NULL;
    for (int i = 0; i < g_mark_n; i++)
        if (!strcmp(g_marks[i].tid, tid)) { m = &g_marks[i]; break; }
    char emsg[400], eat[40];
    emsg[0] = eat[0] = 0;
    const char *act = "none";
    int ok = 0;
    if (m) {
        pldmgr_json_escape(m->msg, emsg, sizeof(emsg));
        ga_fmt_time(m->when, eat, sizeof(eat));
        act = m->act == GA_ACT_PUSH ? "push" : (m->act == GA_ACT_PULL ? "pull" : "none");
        ok = m->ok;
    }
    pthread_mutex_unlock(&g_ga_mu);

    int w;
    if (m)
        w = snprintf(buf, size,
                     "{\"running\":%s,\"last\":{\"action\":\"%s\",\"ok\":%s,"
                     "\"at\":\"%s\",\"message\":\"%s\"}}",
                     is_run ? "true" : "false", act, ok ? "true" : "false", eat, emsg);
    else
        w = snprintf(buf, size, "{\"running\":%s,\"last\":null}", is_run ? "true" : "false");
    return (w > 0 && (size_t)w < size) ? w : -1;
}

/* ── 真正干活 ───────────────────────────────────────────────────────────── */
/* 同一时刻只允许一次同步在跑。**这条不是性能优化，是正确性要求**：
 * 「整份替换」是「旧的改名 → 换上去 → 删旧的」，两次并发跑同一个目录
 * 会互相把对方刚改名的目录当成垃圾删掉 —— 那是直接把存档弄没。
 * 守护线程（退出时推送）与用户手点「立即推送」完全可能同时发生。 */
static pthread_mutex_t g_sync_mu = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int ok;       /* 成功份数 */
    int skipped;  /* 远端还没有（不算失败） */
    int failed;
    /* [CN-FORK] v0.5.2.154 本地不比远程新 → 主动跳过（用户口径：本地更新就不拉）。
     * 与 skipped 分开计数：一个是「没得拉」，一个是「不该拉」，日志里混在一起
     * 就分不清「对面没开」和「我这边更新」—— 而这两件事该做的动作完全不同。
     * [CN-FORK] v0.5.2.162 这一格还多盖一种：远程那份**就是**本地这份推上去的
     * （src_mtime 与本地此刻相等），拉回来只是白跑一遍网络。 */
    int newer;
    long long files;
    long long bytes;
    char first_err[200];
} ga_res_t;

static int ga_tid_ok(const char *t) {
    if (!t) return 0;
    size_t n = strlen(t);
    if (n < 6 || n > 16) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = t[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

/* 逐份（每个用户）推 / 拉。只对**主机上已有的用户**做 —— 远端可能存在主机上
 * 没有的 uid，但枚举远端要么靠 FTP LIST、要么靠对面给个列表接口，
 * 两条路都不可靠；而「主机上有存档的用户」永远是自动同步真正要保的那些。
 * 拉取是唯一例外：本地那份没了的时候，候选集退化成主机上全部用户目录（见下）。 */
static void ga_do_inner(const char *tid, int act, const char *only_uid,
                        const ga_cfg_t *cfg, ga_res_t *res) {
    memset(res, 0, sizeof(*res));

    /* 配置与协议先判。这两条不看文件系统状态、也最可操作（「你选的协议这个 SDK
     * 编不出来」比「没有存档」更能告诉用户该干什么），所以排在枚举之前 ——
     * 否则配了 smb 的用户点了推送只会看到「主机上还没有这个游戏的存档」，
     * 而真正的原因被一条无关的信息盖住。
     * 配置由调用方（ga_do）**只读一次**再传进来 —— 那里还要拿它给「开始」那条
     * 日志拼目标，重复读一遍既浪费又可能读到两份不同的东西。 */
    if (!cfg) {
        snprintf(res->first_err, sizeof(res->first_err), "读不到这个游戏的自动存档配置");
        res->failed = 1;
        return;
    }
    if (!sr_mode_supported(cfg->rc.mode)) {
        snprintf(res->first_err, sizeof(res->first_err),
                 "当前 SDK 不支持 %s 传输，请改用 FTP 或 HTTP", cfg->rc.mode);
        res->failed = 1;
        return;
    }

    char uids[16][64];
    int nu;
    if (only_uid && only_uid[0]) {
        snprintf(uids[0], sizeof(uids[0]), "%s", only_uid);
        nu = 1;
    } else {
        nu = game_saves_uids(tid, uids, 16);
        /* 拉取兜底：本地那份存档已经没了（换机 / 误删 / 重装）时，
         * game_saves_uids 必然是 0 条 —— 而那正是最需要拉回来的时候。
         * 用户目录是主机账号，不会因为删了游戏存档而消失，拿它当候选集，
         * 逐个去远端试；远端没有的算「跳过」。见头文件 game_saves_user_ids。 */
        if (nu <= 0 && act == GA_ACT_PULL) nu = game_saves_user_ids(uids, 16);
    }
    if (nu <= 0) {
        snprintf(res->first_err, sizeof(res->first_err),
                 act == GA_ACT_PUSH ? "主机上还没有这个游戏的存档"
                                    : "主机上没有可用的用户目录");
        res->failed = 1;
        return;
    }

    pthread_mutex_lock(&g_sync_mu);
    for (int i = 0; i < nu; i++) {
        char err[256];
        long long bytes = 0, files = 0;
        err[0] = 0;

        /* [CN-FORK] v0.5.2.154 拉取前先比一比新旧（用户口径：「如果本地存档比远程
         * 存档要更新，则不拉取；本地存档比远程存档旧，可以直接拉取覆盖」）。
         * [CN-FORK] v0.5.2.162 又加一句：「本地和远程一模一样就别拉，只有远程比本地
         * 新的时候才拉」—— 判据由此换成远端包自己的来源时刻 src_mtime。
         *
         * 拉取是**整份覆盖**，所以「远程那份其实是旧的」是真正会丢数据的场景：
         * 昨晚玩到很晚没推上去，今天开机自动拉取就把昨晚的进度盖掉了。
         * 而反过来，「远程就是本地这份推上去的」是**纯浪费**：几百 MB 走一遍网络，
         * 盘上落回一模一样的东西 —— 用户看到的「推完又自动拉回来」就是这一格。
         *
         * 四条出口：
         *   SR_ENOTFOUND → 远端还没有这份，算跳过（与原来一致）；
         *   sr_stat == 0 → 拿到了远程元数据，交给纯函数 ga_pull_needed 判；
         *   sr_stat < 0  → 这条协议取不到元数据 → **说不清就照旧拉**，
         *                  不能因为读不到时间戳让功能失效；
         *   src_mtime == 0（`.162` 之前推的老包）→ 判定内部退回 `.154` 口径。
         * 判定纯函数的全部边界见 include/game_auto.h。 */
        if (act == GA_ACT_PULL) {
            long long rsz = 0, rfiles = 0, rsrc = 0;
            time_t rmt = 0;
            char serr[200];
            serr[0] = 0;
            int st = sr_stat(&cfg->rc, tid, uids[i], &rsz, &rmt, &rfiles,
                             &rsrc, serr, sizeof(serr));
            if (st == SR_ENOTFOUND) {
                res->skipped++;
                continue;
            }
            if (st == 0) {
                long long lmt = 0;
                int lhave = game_saves_local_mtime(tid, uids[i], &lmt);
                if (!ga_pull_needed(lmt, lhave, (long long)rmt, rsrc, 1)) {
                    res->newer++;
                    pldmgr_log("[PLDMGR] saves: %s/%s 远程那份不比本地新"
                               "（本地 %lld · 远程落盘 %lld · 远程来源 %lld）"
                               "→ 跳过拉取\n",
                               tid, uids[i], lmt, (long long)rmt, rsrc);
                    continue;
                }
            }
        }

        int rc = (act == GA_ACT_PUSH)
                     ? sr_push(tid, uids[i], &cfg->rc, &bytes, err, sizeof(err))
                     : sr_pull(tid, uids[i], &cfg->rc, &files, &bytes, err, sizeof(err));
        if (rc == 0) {
            res->ok++;
            res->files += files;
            res->bytes += bytes;
        } else if (rc == SR_ENOTFOUND) {
            res->skipped++;
        } else {
            res->failed++;
            if (!res->first_err[0]) snprintf(res->first_err, sizeof(res->first_err), "%s", err);
        }
    }
    pthread_mutex_unlock(&g_sync_mu);
}

/* [CN-FORK] v0.5.2.147 日志用的一句话目标描述（**绝不含密码**）。
 * 口径与前端 `utils/saveConfig.js` 的 `remoteSummary()` 逐字一致 —— 界面上显示的
 * 和日志里打的必须是同一句，否则「界面写 SMB、日志里写 FTP」这种对照根本没法用。
 * 后端不引前端代码，就地写一份最小版；改一边记得改另一边。 */
static void ga_target_desc(const sr_cfg_t *c, char *out, size_t osz) {
    if (!c || !c->mode[0]) {
        snprintf(out, osz, "未配置");
        return;
    }
    if (!strcmp(c->mode, "smb"))
        snprintf(out, osz, "SMB · %s%s%s%s%s",
                 c->host[0] ? c->host : "?",
                 c->share[0] ? "/" : "", c->share[0] ? c->share : "",
                 c->sub[0] ? "/" : "", c->sub[0] ? c->sub : "");
    else if (!strcmp(c->mode, "ftp"))
        snprintf(out, osz, "FTP · %s:%d%s", c->host[0] ? c->host : "?", c->port, c->path);
    else if (!strcmp(c->mode, "http"))
        snprintf(out, osz, "HTTP · %s", c->url[0] ? c->url : "?");
    else
        snprintf(out, osz, "%s（未知协议）", c->mode);
}

/* 推 / 拉的唯一入口：手动（`POST /game_save_sync`）与守护线程都走这里。
 *
 * [CN-FORK] v0.5.2.147 **开始与结束各打一条日志 + 各弹一条屏幕通知**（用户口径：
 * 「远程拉取和推送远程的时候，两个动作的开始和结束都要 debug：到屏幕信息」，
 * 并指明「屏幕信息就跟这个一样」= main.c 的 `NayePlmTools v%s Ready!` 那条
 * `pldmgr_notify`，即 **PS5 系统通知弹窗**）。
 *
 * 两条通路一起打，因为它们解决的不是同一件事：
 *   · `pldmgr_notify` —— **当场看得见**（屏幕右上角的系统通知），不用切页面；
 *   · `pldmgr_log`    —— **事后查得到**（面板「日志」页的环形缓冲 + /data/nayeplmtools/pldmgr.log
 *     留档，界面定住 / 断电后照样有）。
 * 只做前者，通知一划走就没证据了；只做后者，用户点完按钮得自己跑去日志页翻。
 *
 * 为什么「开始」放在这一层、而不是塞进 ga_do_inner：inner 有多条**提前返回**的分支
 * （读不到配置 / 协议不支持 / 没有可同步的用户），把开始行放进去必然出现「有结束没开始」
 * 或「有开始没结束」—— 而用户点完按钮最想知道的恰恰是「它到底有没有开始跑」。
 * 放在包装层则天然成对：inner 无论从哪条路出去，结束行一定跟着来。
 *
 * 用 CLOCK_MONOTONIC 计时（不受系统改时间影响，同 game_media.c）。手动点一下正常是
 * 几百毫秒到几秒；真卡在网络上（或卡在 g_sync_mu 上等另一次同步）时，「用时」自己说话。 */
static void ga_do(const char *tid, int act, const char *only_uid, ga_res_t *res,
                  const char *src, int freeze_game) {
    const char *an = (act == GA_ACT_PUSH) ? "推送" : "拉取";

    /* 只读一次配置：既是 inner 的输入，也让「开始」这行带得上目标。
     * 读不到就传 NULL，inner 自己会填那句人话错误。 */
    ga_cfg_t cfg;
    int have = (ga_load_cfg(tid, &cfg) == 0);
    char tgt[400];
    ga_target_desc(have ? &cfg.rc : NULL, tgt, sizeof(tgt));

    struct timespec t0, t1;

    const char *scope = (only_uid && only_uid[0]) ? only_uid : "全部用户";

    /* [CN-FORK] v0.5.2.150 **拉取前先冻住正在启动的这款游戏。**
     * [CN-FORK] v0.5.2.152 冻的方式换成**进程级 SIGSTOP**（原来走 LncUtil 挂起，
     * 那条路会把游戏换到后台且恢复时切不回前台 —— 详见 include/game_auto.h）。
     *
     * 顺序就是用户口径的那条（也是唯一的正确顺序）：
     *   游戏启动 → 引擎监测到启动 → **暂停游戏** → 拉取存档 → 拉取成功 → **恢复游戏**
     *
     * 放在「开始」那两条**之前**：日志的时间线于是逐字对上那句话
     * `已暂停 → 远程拉取 开始 → 远程拉取 结束 → 已恢复`，
     * 而且「开始」这行说的确实是「存档真的要开始搬了」（不是「我打算搬，先冻两秒」）。
     *
     * `freeze_game` 只有守护线程的自动拉取传 1 —— 手动拉取时游戏早就把存档读进
     * 内存了，冻它没有任何用（理由见 include/game_auto.h）。
     *
     * `frozen_kind` / `frozen_handle` = **冻结时记下的那一对**，放行只能原样交回
     * （不许重新解析 —— `.150` 真机首轮的坑，见头文件）。 */
    int frozen_kind = GA_FREEZE_NONE;
    unsigned int frozen_handle = 0;
    int frozen = (freeze_game && act == GA_ACT_PULL)
                     ? ga_freeze_for_pull(tid, &frozen_kind, &frozen_handle) : 0;

    /* 字段顺序是**按重要性倒着放**的：环形缓冲每行只有 MAX_LOG_LINE_LEN(256) 字节，
     * 目标里可能带着很长的 host/share/sub，超了就被截断。把「目标」放最后，
     * 长路径最多把自己截掉，不会把「是推还是拉 / 谁触发的 / 范围」一起带走。 */
    pldmgr_log("[PLDMGR] saves: 远程%s 开始 · %s · 触发=%s · 范围=%s · 目标=%s\n",
               an, tid, src ? src : "?", scope, tgt);
    /* [CN-FORK] v0.5.2.147 屏幕上那一条 —— PS5 系统通知，与 main.c 的
     * `NayePlmTools v%s Ready!` 是同一个 `pldmgr_notify`（用户口径：「屏幕信息就跟这个一样」）。
     * 通知正文的缓冲有 3075 字节，所以这里放得下与日志同一套字段，两处能对着看。
     * `pldmgr_notify` 内部还会补一条 `[Notify] …` 进日志 —— 那正好是「通知确实发出去了」的证据。 */
    pldmgr_notify("远程%s 开始\n%s · 触发=%s · 范围=%s\n目标=%s",
                  an, tid, src ? src : "?", scope, tgt);

    /* 计时**从冻住之后开始**：挂起固定要 sleep(2)（等应用真的冻住，与
     * ps5_launcher.c 同一口径），把它算进「拉取用时」会让每次自动拉取都凭空
     * 多两秒，看着像网络慢 —— 排查时最忌讳这种被污染的指标。 */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ga_do_inner(tid, act, only_uid, have ? &cfg : NULL, res);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0
                + (double)(t1.tv_nsec - t0.tv_nsec) / 1000000.0;

    /* 量：**推送不报文件数** —— sr_push 只回字节数（res->files 恒 0），
     * 照着拉取那样打就成了「0 个文件 / 2648 字节」这种自相矛盾的一行，
     * 排查时反而会把人带偏。 */
    char qty[96];
    if (act == GA_ACT_PUSH)
        snprintf(qty, sizeof(qty), "%lld 字节", res->bytes);
    else
        snprintf(qty, sizeof(qty), "%lld 个文件 / %lld 字节", res->files, res->bytes);

    /* 「未覆盖」只在真的跳过时露出来 —— 常驻一个 0 只会让这行更难扫。
     * [CN-FORK] v0.5.2.162 措辞从「本地更新」改成「无需拉取」：这一格现在盖两种
     * 情况（远程更旧 / 远程就是本地这份推上去的），后者说「本地更新」是假话。 */
    char newer[56];
    newer[0] = 0;
    if (res->newer > 0)
        snprintf(newer, sizeof(newer), " · 无需拉取 %d（未覆盖）", res->newer);

    pldmgr_log("[PLDMGR] saves: 远程%s 结束 · %s · 成功 %d / 跳过 %d / 失败 %d%s · "
               "%s · 用时 %.0f ms%s%s\n",
               an, tid, res->ok, res->skipped, res->failed, newer, qty, ms,
               res->first_err[0] ? " · " : "", res->first_err);
    /* 屏幕上那一条。失败原因**另起一行**打 —— 通知是给人扫一眼的，
     * 混在计数那一行里最容易被当成背景噪声划过去。 */
    pldmgr_notify("远程%s 结束\n%s · 成功 %d / 跳过 %d / 失败 %d%s\n%s · 用时 %.0f ms%s%s",
                  an, tid, res->ok, res->skipped, res->failed, newer, qty, ms,
                  res->first_err[0] ? "\n" : "", res->first_err);

    /* 拉完了才放行 —— 收尾在「结束」之后，这样用户看到的是
     * 「拉取成功」然后才是「游戏回来了」（用户口径：拉取成功 → 恢复游戏）。
     *
     * 失败也照样走到这儿：`frozen` 只在**真的冻住了**的时候才为 1，
     * 没冻过就绝不会去恢复（否则会平白把用户自己挂起的游戏叫醒）。 */
    if (frozen) ga_thaw_after_pull(tid, frozen_kind, frozen_handle);
}

/* ── 手动推 / 拉（POST /game_save_sync）─────────────────────────────────── */
int game_auto_sync_action(const char *body, size_t len,
                          char *buf, size_t size, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!buf || size < 128) return -1;

    char tid[32] = "", action[16] = "", uid[64] = "";
    cJSON *b = (body && len) ? cJSON_ParseWithLength(body, len) : NULL;
    if (b) {
        cJSON *f = cJSON_GetObjectItem(b, "tid");
        if (cJSON_IsString(f) && f->valuestring) snprintf(tid, sizeof(tid), "%s", f->valuestring);
        f = cJSON_GetObjectItem(b, "action");
        if (cJSON_IsString(f) && f->valuestring) snprintf(action, sizeof(action), "%s", f->valuestring);
        f = cJSON_GetObjectItem(b, "uid");
        if (cJSON_IsString(f) && f->valuestring) snprintf(uid, sizeof(uid), "%s", f->valuestring);
        cJSON_Delete(b);
    }

    if (!ga_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "标题 ID 不合法");
        return -1;
    }
    int act;
    if (!strcmp(action, "push")) act = GA_ACT_PUSH;
    else if (!strcmp(action, "pull")) act = GA_ACT_PULL;
    else {
        if (msg) snprintf(msg, msgsz, "action 只能是 push 或 pull");
        return -1;
    }
    if (uid[0]) {
        for (const char *p = uid; *p; p++)
            if (*p == '/' || *p == '\\' || (unsigned char)*p < 0x20 || *p == '.') {
                if (msg) snprintf(msg, msgsz, "用户标识不合法");
                return -1;
            }
    }

    /* [CN-FORK] v0.5.2.154 游戏正在运行 → 手动推 / 拉一律不做（用户口径：
     * 「游戏进行中，拉取和推送存档的动作不让做」）。
     *
     * 理由两条，都是数据安全：
     *   · 拉取会把盘上存档**整份换掉**，而游戏早把存档读进内存了 —— 换完它不知道，
     *     之后存档 / 退出还会把内存里那份写回去，等于白换，还把用户刚玩的进度弄乱；
     *   · 推送会在游戏**正写盘时**去打包，拷到的可能是撕裂的半截存档，推上去就是
     *     把一份坏存档留在了远端 —— 那比不推糟糕得多。
     *
     * **只拦手动这条路**：自动那条链有它自己的处理 —— 「启动时拉取」发生在游戏
     * 刚起来的瞬间，靠 SIGSTOP 冻住进程再拉（见 ga_do 的 freeze_game）；「退出时
     * 推送」发生在探测到游戏已经退出之后（还额外等 GA_EXIT_SETTLE 秒）。两条都不
     * 走这里，所以不会被这条规则误伤。 */
    {
        char run[32];
        run[0] = 0;
        g_probe(run, sizeof(run));
        if (run[0] && !strcmp(run, tid)) {
            if (msg) snprintf(msg, msgsz,
                              "「%s」正在运行，请先退出游戏再%s存档（运行中操作可能丢失进度）",
                              tid, act == GA_ACT_PUSH ? "推送" : "拉取");
            return -1;
        }
    }

    ga_res_t res;
    /* 最后一参 0 = **不**挂起游戏：手动拉取时游戏早把存档读进内存了，
     * 冻住它改变不了内存里那份，只会平白把用户正在看的画面冻一下。 */
    ga_do(tid, act, uid, &res, "手动", 0);
    pthread_mutex_lock(&g_ga_mu);
    ga_mark(tid, act, res.ok > 0 || (res.failed == 0), res.first_err);
    pthread_mutex_unlock(&g_ga_mu);

    if (res.ok == 0 && res.failed > 0) {
        if (msg) snprintf(msg, msgsz, "%s", res.first_err[0] ? res.first_err : "同步失败");
        return -1;
    }

    const char *an = act == GA_ACT_PUSH ? "push" : "pull";
    char note[240];
    if (res.ok == 0 && res.newer > 0)
        /* 这一条**必须排在 skipped 之前**：本地更新时我们一份都没拉，如果先去判
         * skipped 就会报成「远程还没有这个游戏的存档」—— 与事实正好相反。
         * [CN-FORK] v0.5.2.162 这句话现在盖两种情况：远程那份更旧，**和**远程那份
         * 就是本地这份推上去的（一模一样）。两种都不该说成「失败」，也不该说成
         * 「本地更新」—— 后者在刚推完的场景里是假的。 */
        snprintf(note, sizeof(note),
                 "远程没有比本地更新的一份，已跳过拉取（%d 份未覆盖）", res.newer);
    else if (res.ok == 0 && res.skipped > 0)
        snprintf(note, sizeof(note), act == GA_ACT_PUSH ? "没有可推送的存档"
                                                        : "远程还没有这个游戏的存档");
    else if (act == GA_ACT_PUSH)
        snprintf(note, sizeof(note), "已推送 %d 份（%lld 字节）", res.ok, res.bytes);
    else
        snprintf(note, sizeof(note), "已拉取 %d 份（%lld 个文件 / %lld 字节）",
                 res.ok, res.files, res.bytes);

    char en[600];
    pldmgr_json_escape(note, en, sizeof(en));
    int w = snprintf(buf, size,
                     "{\"ok\":true,\"action\":\"%s\",\"uids\":%d,\"files\":%lld,"
                     "\"size\":%lld,\"failed\":%d,\"note\":\"%s\"}",
                     an, res.ok, res.files, res.bytes, res.failed, en);
    return (w > 0 && (size_t)w < size) ? 0 : -1;
}

/* ── 守护线程 ───────────────────────────────────────────────────────────── */
#define GA_BOOT_DELAY 20 /* 避开启动期 IO（与本地快照守护同一理由） */
#define GA_TICK 1        /* 扫描周期（`.150` 由 2 改 1）。用户口径：「把监听时间间隔调整为
                          * 1 秒」—— 启动时拉取是一场与游戏启动的竞速，探测越早越可能赢。
                          * 一轮的成本只有一次 `sceLncUtilGetAppIdOfRunningBigApp()`
                          * （游戏没换的时候读配置那一步都走不到），可以忽略。 */
#define GA_EXIT_SETTLE 5 /* 退出后再等这么久才推，给主机收尾写盘的时间 */
#define GA_THREAD_STACK (1u << 20)

/* ══ [CN-FORK] v0.5.2.174 退出快照：一个时机，没有静默窗口、没有重试 ═════════
 *
 * 用户口径：「不需要静默窗口和重试，快照和推送时机一起，只用这一个时机和手动」。
 *
 * 历史：`.172` 这里有一整套「延迟重试」队列（`GA_SNAP_RETRY_MAX 40` × `GA_TICK 1`），
 * 唯一目的是绕开 `gs_snap_take` 里那个 20 秒**静默窗口** —— 游戏退出那一刻主机会把
 * 存档写下去，mtime 就是「刚刚」，于是每一次退出都被窗口挡掉、快照永远不生成。
 * `.174` 把**静默窗口本身删了**（`GS_SNAP_QUIET_SEC` 已不存在），重试队列就没有
 * 存在的理由，整条一起删。
 *
 * 现在本地快照只剩两个触发点，其中自动那一个与自动推送**共用同一个时刻**：
 *   ① 检测到游戏退出（`game_auto_plan` 里 SNAP 与 PUSH 相邻，`GA_EXIT_SETTLE`
 *      只等一次，见下面那个 `settled` 变量）；
 *   ② 面板「本地快照」块上的「立即备份」。
 *
 * **去重仍然保留**（`gs_snap_take` 里「内容与最近一份相同就不新增」）—— 那是去重，
 * 不是时间窗：退出时如果存档一个字节都没变，本来就不该多落一份。
 *
 * ── 这条链在日志/界面上长什么样
 * 一次「退出时快照」印两行（真的拍成时，中间还有一行来自 game_saves.c）：
 *   ① `saves: <TID> 已退出 · 本地快照：<note>（本轮生成 N 份 / 本轮没拍 / 本轮失败）`
 *      ← 本文件的 GA_ACT_SNAP 分支。`（…）` 是 `game_saves_snap_run_now` 的返回值，
 *        `.172` 之前这里印的是裸的 `（0）`，光看日志分不清三种含义（`.173` 改的）。
 *   ② `saves: <TID>/<uid> 本地快照 <ts>（N 文件 / M 字节）`
 *      ← `game_saves.c` 的 `gs_snap_take`，**真的拍成那一刻**才印。
 * `<note>` 是 `gs_snap_take` 的 out 参数，同一句话也走 `/game_save_snap` 的
 * `message` 字段回给前端 —— 所以它必须是「用户能读的一行」，不能塞调试信息。
 * 同一批事件也进「快照活动」事件环（`game_saves_snap_ev`，`.174`），
 * 面板上打开存档弹窗就能直接看到，不用去翻日志。 */
void game_auto_tick(void) {
    char cur[32];
    cur[0] = 0;
    g_probe(cur, sizeof(cur));

    pthread_mutex_lock(&g_ga_mu);
    if (g_first_tick) {
        /* 第一轮只记状态、不动作 —— 见 game_auto.h 的设计 ①。 */
        snprintf(g_prev_tid, sizeof(g_prev_tid), "%s", cur);
        g_first_tick = 0;
        pthread_mutex_unlock(&g_ga_mu);
        return;
    }
    char prev[32];
    snprintf(prev, sizeof(prev), "%s", g_prev_tid);
    snprintf(g_prev_tid, sizeof(g_prev_tid), "%s", cur);
    pthread_mutex_unlock(&g_ga_mu);

    if (!strcmp(prev, cur)) return; /* 绝大多数轮次走这里 */

    /* [CN-FORK] v0.5.2.163 自制应用屏蔽：自动存档的 prev（已退出）/ cur（已启动）
     * 如果是 homebrew，**整条动作链都不跑** —— 包括退出推送、启动拉取、本地快照。
     *
     * 为什么整条都不跑而不是只跳推 / 拉：homebrew 的存档目录可能在 /data/homebrew 下
     * 而不是 /user/home/<uid>/savedata_prospero/，自动推拉的文件路径都不对，
     * 跑也白跑；本地快照也会指向一个空目录，徒增一次 IO。
     *
     * 「跳过」也记日志 —— 同一条不让动作发生时没有任何痕迹这条规矩（见下方
     * 「本该动作、却被开关挡住」那条）。 */
    int prev_hb = prev[0] && ga_tid_is_homebrew(prev);
    int cur_hb = cur[0] && ga_tid_is_homebrew(cur);
    if (prev_hb && prev[0])
        pldmgr_log("[PLDMGR] saves: %s 已退出 · 跳过自动存档：检测到自制应用\n", prev);
    if (cur_hb && cur[0])
        pldmgr_log("[PLDMGR] saves: %s 已启动 · 跳过自动存档：检测到自制应用\n", cur);

    ga_cfg_t pc, cc;
    int pe = 0, px = 0, ce = 0, cl = 0;
    if (!prev_hb && prev[0] && ga_load_cfg(prev, &pc) == 0) { pe = pc.enabled; px = pc.on_exit; }
    if (!cur_hb && cur[0] && ga_load_cfg(cur, &cc) == 0) { ce = cc.enabled; cl = cc.on_launch; }
    /* [CN-FORK] v0.5.2.154 本地快照的开关**单独问**（不走 ga_load_cfg）：它跟远程那套
     * 不是一个总开关，本地备份不该被「自动存档关着」连坐。口径与设置页「存档配置」
     * 里的「启用本地快照」一致（全局优先 → per-game 回落，见 gs_snap_cfg）。
     * [CN-FORK] v0.5.2.163 同样跳过 homebrew。 */
    int psnap = (!prev_hb && prev[0]) ? game_saves_snap_enabled(prev) : 0;

    ga_step_t steps[3];
    int ns = game_auto_plan(prev, cur, pe, px, psnap, ce, cl, steps);

    /* [CN-FORK] v0.5.2.148 「本该动作、却被开关挡住」也要留一行痕。
     *
     * 起因是真机反馈：「只有拉取有 debug，关闭（游戏）时的推送没有 debug 信息」。
     * 查下来推送本身的 debug 是齐的（手动推送四条都在：日志 + 通知各两条），
     * 真正的情况是那台机器的全局配置里 `on_exit` 是 false —— 引擎按配置办事，
     * 没触发自然没有 debug。**问题不在推送，在于「没动作」这件事没有任何痕迹**：
     * 日志里只有一串「拉取」，用户没法判断到底是「游戏压根没退出过」还是
     * 「退出回传那个开关关着」，而这两件事该做的事完全不同。
     *
     * 两侧**各自独立**判断（不是 else-if）：A 游戏退出、B 游戏启动那一轮里
     * `prev` 和 `cur` 都有值，两边都该说。条件与 game_auto_plan 里那两条一一对应，
     * 所以「动作真的发生了」时这里一声不响 —— 不会出现「说要推、结果也推了」的矛盾。
     *
     * 只写日志、**不弹屏幕通知**：通知是给「真的动了」准备的，开关关着时每启动/退出
     * 一次游戏就弹一条，很快就变成没人看的噪声。要看就来面板「日志」页。 */
    if (prev[0] && (!pe || !px))
        pldmgr_log("[PLDMGR] saves: %s 已退出 · 跳过推送：%s\n", prev,
                   !pe ? "自动存档总开关关着" : "「退出游戏时回传」关着");
    if (cur[0] && (!ce || !cl))
        pldmgr_log("[PLDMGR] saves: %s 已启动 · 跳过拉取：%s\n", cur,
                   !ce ? "自动存档总开关关着" : "「启动游戏时拉取」关着");

    /* 退出后先等几秒（见设计 ②），给主机收尾写盘的时间。**只等一次**：
     * 退出这一刻可能同时要做「本地快照」与「推送」，各等一遍就变成十秒。 */
    int settled = 0;
    for (int i = 0; i < ns; i++) {
        if ((steps[i].act == GA_ACT_PUSH || steps[i].act == GA_ACT_SNAP) && !settled) {
            sleep(GA_EXIT_SETTLE);
            settled = 1;
        }

        /* [CN-FORK] v0.5.2.154 退出时的本地快照（原来是定时守护，已删）。
         * [CN-FORK] v0.5.2.174 **与自动推送同一个时刻**，没有静默窗口、没有重试
         * （用户口径：「不需要静默窗口和重试，快照和推送时机一起，只用这一个时机
         * 和手动」）。上面那个 `settled` 让两者共用同一次 `GA_EXIT_SETTLE` 等待，
         * 所以「快照 + 推送」是一次退出里同时发生的事，不是两轮。
         * [CN-FORK] v0.5.2.173 日志措辞（用户口径：「这里为什么这么缩链路」）。
         * 原来这行是 `…：%s（%d）`，那个裸的 `（0）` 是 made 的返回值，光看日志
         * 分不清「拍成了 0 份」还是「没拍」还是「失败」。现在把三种结果写成字。 */
        if (steps[i].act == GA_ACT_SNAP) {
            char smsg[200];
            char mtxt[24];
            smsg[0] = 0;
            /* [CN-FORK] v0.5.2.174 事件环也要一条「开始」—— 界面上一条链总得有个头，
             * 否则用户看到的第一行就是「跳过」，不知道是谁触发的。 */
            game_saves_snap_ev(steps[i].tid, GS_SNAP_EV_INFO, "游戏退出，检查本地快照");
            int made = game_saves_snap_run_now(steps[i].tid, smsg, sizeof(smsg));
            if (made > 0) snprintf(mtxt, sizeof(mtxt), "本轮生成 %d 份", made);
            else if (made == 0) snprintf(mtxt, sizeof(mtxt), "本轮没拍");
            else snprintf(mtxt, sizeof(mtxt), "本轮失败");
            pldmgr_log("[PLDMGR] saves: %s 已退出 · 本地快照：%s（%s）\n",
                       steps[i].tid, smsg[0] ? smsg : "已处理", mtxt);
            continue;
        }

        ga_res_t res;
        /* 最后一参 1 = 拉取前挂起游戏（`.150`）。
         * 顺序：监测到启动 → 挂起 → 拉取 → 恢复。只有这条路上传 1 —— 见
         * ga_do 里那段注释与 include/game_auto.h 的设计说明。 */
        ga_do(steps[i].tid, steps[i].act, NULL, &res,
              steps[i].act == GA_ACT_PUSH ? "自动（退出游戏时）" : "自动（启动游戏时）", 1);
        pthread_mutex_lock(&g_ga_mu);
        ga_mark(steps[i].tid, steps[i].act, res.ok > 0 || (res.failed == 0), res.first_err);
        pthread_mutex_unlock(&g_ga_mu);
    }
}

static void *ga_main(void *arg) {
    (void)arg;
    sleep(GA_BOOT_DELAY);
    for (;;) {
        if (!pldmgr_utils_safe_mode()) game_auto_tick();
        sleep(GA_TICK);
    }
    return NULL;
}

void game_auto_start_daemon(void) {
    /* 先把运行期状态接进 /game_saves（在 safe_mode 判定**之前**）：
     * 安全模式下引擎不跑，但界面仍该看到「这个游戏正在运行」这类只读信息。 */
    game_saves_set_state_provider(game_auto_state_json);

    /* safe_mode：死机后要能止血，后台的自动动作一律不做（同本地快照守护）。 */
    if (pldmgr_utils_safe_mode()) {
        pldmgr_log("[PLDMGR] saves: SAFE MODE，跳过自动存档守护\n");
        return;
    }
    /* 栈显式给足 1MB：这条线程要跑 curl + 打包，真机 pthread 默认栈只有 64KB
     * 上下（`.141` 的教训）。setstacksize 失败就退回默认，不因为一个优化项
     * 把整条守护丢掉。 */
    pthread_attr_t at;
    pthread_attr_init(&at);
    int sized = (pthread_attr_setstacksize(&at, GA_THREAD_STACK) == 0);
    pthread_t th;
    int rc = pthread_create(&th, sized ? &at : NULL, ga_main, NULL);
    pthread_attr_destroy(&at);
    if (rc == 0) {
        pthread_detach(th);
        pldmgr_log("[PLDMGR] saves: 自动存档守护已启动（%ds 一轮，栈 %uKB）\n",
                   GA_TICK, (unsigned)(GA_THREAD_STACK >> 10));
    } else {
        pldmgr_log("[PLDMGR] saves: 自动存档守护线程创建失败\n");
    }
}
