/* [CN-FORK] v0.5.2.142 test/game_auto_test.c — src/game_auto.c 的主机回归测试。
 *
 * 覆盖：
 *   ① game_auto_plan 的**全部组合**（这是整条链上唯一有分支逻辑的地方，
 *      而它跑在真机上 —— 宿主上穷举比在真机上试靠谱得多）
 *   ② 手动推 / 拉端到端（走本机 HTTP 桩，真的搬字节）
 *   ③ 远端还没有存档时不算失败（第一次用必然遇到，不能给用户一条红报错）
 *   ④ 不支持的协议（smb）要说清原因
 *   ⑤ 运行期状态：running 跟着探测走、last 记下最后一次动作
 *   ⑥ 推 / 拉的「开始 / 结束」两条 debug 行（`.147`）—— 走 pldmgr_log 进面板「日志」页，
 *      所以是**用户看得见的产物**：成对、有序、带触发来源 / 目标 / 计数 / 用时，
 *      且**提前返回的失败路径也必须成对**
 *   ⑦ 拉取前挂起游戏 / 拉完恢复（`.150`）—— 顺序必须是
 *      「挂起 → 拉取 开始 → 拉取 结束 → 恢复」（用户口径：游戏启动 → 监测到启动 →
 *      暂停游戏 → 拉取存档 → 拉取成功 → 恢复游戏）。挂不住时拉取照跑但不恢复；
 *      游戏在 tick 与 ga_do 之间退出了则不挂不恢复；**手动拉取一律不挂起**；
 *      恢复失败要留日志 + 弹屏幕通知。
 *      ★ 还有一条不变量：**恢复拿到的 app_id 必须是挂起时报的那个**，不许再解析 ——
 *      真机上挂起后 `GetAppIdOfRunningBigApp()` 返回 0xffffffff，再解析必然失败，
 *      于是 ResumeApp 发不出去、游戏一直冻着（`.150` 真机首轮的坑）。
 *      这一节靠 `game_auto_tick()`（`.150` 为此从 static 导出）+ 两个桩驱动。
 *   ⑧ 拉取前的新旧判定（`.154`；`.162` 扩到「本地和远程一模一样就不拉」）——
 *      纯函数穷举 + 端到端「刚推上去的这份不该被原样拉回来」+「老包没有 src_mtime
 *      时不许当成一模一样」。
 *   ⑨ 冻结验证的**判定策略**（`.153`）—— `ga_freeze_verdict()` 的 16 种输入组合
 *      全部穷举。`.152` 真机栽的是**策略**错（读不到进程状态时错误地放弃了
 *      SIGSTOP → 回落 LncUtil → 游戏跳后台切不回来），而真机那段在 `#ifdef`
 *      里、宿主编不到，所以判定抽成纯函数并导出，专供这一节。
 *
 * 探测（sceLncUtilGetAppIdOfRunningBigApp）在宿主上没有那个符号，
 * 用 -DGA_HOST 关掉默认实现，测试自己装探针（game_auto_set_probe）。
 *
 * 用法：由 test/run_host_tests.sh 拨起，也可单独跑。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h> /* utimes：把本地存档的 mtime 拨到过去 / 未来 */
#include <time.h>
#include <unistd.h>

#include "game_auto.h"
#include "game_saves.h"
#include "notification.h" /* 下面那个 pldmgr_notify 桩要跟真声明对得上 */
#include "stub_servers.h"

#include <stdarg.h>

/* [CN-FORK] v0.5.2.147 把日志抓下来。`.147` 给推 / 拉加了「开始 / 结束」两条 debug 行
 * （用户口径：「远程拉取和推送远程的时候，两个动作的开始和结束都要 debug 到屏幕信息」），
 * 它们走 pldmgr_log → 面板「日志」页的环形缓冲 —— 那是**用户看得见的产物**，
 * 宿主上不抓就等于这两行完全没覆盖（同「宿主抹掉了真机前提」那一族）。 */
#define LOG_CAP 64
#define LOG_LEN 512
static char g_log[LOG_CAP][LOG_LEN];
static int g_log_n;

void pldmgr_log(const char *fmt, ...) {
    char line[LOG_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (g_log_n < LOG_CAP) {
        snprintf(g_log[g_log_n], LOG_LEN, "%s", line);
        g_log_n++;
    }
    if (!getenv("SAVES_TEST_VERBOSE")) return;
    fputs(line, stderr);
}

/* 含 needle 的日志条数 / 第一条的下标（-1 = 没有）。 */
static int log_count(const char *needle) {
    int n = 0;
    for (int i = 0; i < g_log_n; i++)
        if (strstr(g_log[i], needle)) n++;
    return n;
}
static int log_at(const char *needle) {
    for (int i = 0; i < g_log_n; i++)
        if (strstr(g_log[i], needle)) return i;
    return -1;
}

/* [CN-FORK] v0.5.2.147 **屏幕通知**（`pldmgr_notify`）也得抓。
 * 真机那条路是 `sceKernelSendNotificationRequest`（宿主没这个符号），所以这里必须是桩；
 * 而它正是这次改动的**主要产物** —— 用户要的「屏幕信息」就是它（口径：「就跟这个一样」，
 * 指 main.c 的 `NayePlmTools v%s Ready!`）。桩而不抓 = 主要产物零覆盖。 */
#define NOTIFY_CAP 32
static char g_notify[NOTIFY_CAP][512];
static int g_notify_n;

void pldmgr_notify(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (g_notify_n < NOTIFY_CAP) {
        snprintf(g_notify[g_notify_n], 512, "%s", line);
        g_notify_n++;
    }
    if (!getenv("SAVES_TEST_VERBOSE")) return;
    fprintf(stderr, "[NOTIFY] %s\n", line);
}

static int notify_count(const char *needle) {
    int n = 0;
    for (int i = 0; i < g_notify_n; i++)
        if (strstr(g_notify[i], needle)) n++;
    return n;
}
static int notify_at(const char *needle) {
    for (int i = 0; i < g_notify_n; i++)
        if (strstr(g_notify[i], needle)) return i;
    return -1;
}

#define ROOT ROOT_DIR
#define HOME ROOT "/home"
#define TID  "PPSA012345"
#define TID2 "PPSA067890"
#define UID  "00000002"
#define SAVE_DIR HOME "/" UID "/savedata_prospero/" TID
#define SAVE_FILE SAVE_DIR "/sdimg_TEST"

static int g_checks, g_fails;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_fails++;                                                       \
            fprintf(stderr, "  FAIL L%d: ", __LINE__);                       \
            fprintf(stderr, __VA_ARGS__);                                    \
            fprintf(stderr, "\n");                                           \
        }                                                                    \
    } while (0)

static void mkdir_p(const char *p) {
    char b[1200];
    snprintf(b, sizeof(b), "%s", p);
    for (char *s = b + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        mkdir(b, 0755);
        *s = '/';
    }
    mkdir(b, 0755);
}

static void put(const char *path, int n, char fill) {
    char parent[1200];
    snprintf(parent, sizeof(parent), "%s", path);
    char *sl = strrchr(parent, '/');
    if (sl) { *sl = 0; mkdir_p(parent); }
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < n; i++) fputc(fill, f);
    fclose(f);
}

static long rd(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    return (long)n;
}

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void rm_rf(const char *p) {
    char cmd[1400];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", p);
    if (system(cmd) != 0) { /* 清不掉也不影响断言 */ }
}

static char g_http_root[600];
static int g_http_port;

static void remote_path(char *out, size_t osz, const char *tid) {
    snprintf(out, osz, "%s/ps5save/%s/%s.zip", g_http_root, tid, UID);
}

/* [CN-FORK] v0.5.2.154 远端那款游戏的整个目录（zip + meta.json 都在里面）。
 * 「远端还没有这份存档」必须**整目录删掉**：`.154` 起拉取前的新旧判定会去读
 * <uid>.meta.json，只删 zip 而留着 meta 的话，判定会拿到一个「比本地旧」的
 * 时间戳、于是正确地跳过拉取 —— 那测的就不是「远端没有」这条路径了。 */
static void remote_dir(char *out, size_t osz, const char *tid) {
    snprintf(out, osz, "%s/ps5save/%s", g_http_root, tid);
}

/* 配置就写在 GAME_SAVES_AUTO_PATH 指的那个文件里 —— 引擎读的就是它。 */
static void write_cfg(const char *mode, const char *url,
                      int enabled, int on_launch, int on_exit) {
    char p[800];
    snprintf(p, sizeof(p), "%s", AUTO_PATH);
    char parent[800];
    snprintf(parent, sizeof(parent), "%s", p);
    char *sl = strrchr(parent, '/');
    if (sl) { *sl = 0; mkdir_p(parent); }

    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f,
            "{\"%s\":{\"enabled\":%s,\"mode\":\"%s\",\"on_launch\":%s,\"on_exit\":%s,"
            "\"ftp\":{\"host\":\"\",\"port\":21,\"path\":\"\",\"user\":\"\",\"password\":\"\"},"
            "\"http\":{\"url\":\"%s\",\"token\":\"\"},"
            "\"smb\":{\"host\":\"\",\"share\":\"\",\"sub\":\"\",\"user\":\"\",\"password\":\"\"}}}",
            TID, enabled ? "true" : "false", mode, on_launch ? "true" : "false",
            on_exit ? "true" : "false", url);
    fclose(f);
}

static void fixture_reset(void) {
    rm_rf(ROOT);
    put(SAVE_FILE, 2000, 'X');
    put(SAVE_DIR "/sdimg_sce_bu_TEST", 300, 'Y');
    mkdir_p(g_http_root);
}

/* ══ ① 状态机（穷举）═══════════════════════════════════════════════════
 * 参数表：game_auto_plan(prev, cur, prev_enabled, prev_on_exit, prev_snap,
 *                        cur_enabled, cur_on_launch, out)
 * `prev_snap` 是 [CN-FORK] v0.5.2.154 加的：本地快照从「定时守护」改成
 * 「游戏退出时触发」，它和推 / 拉同属「退出那一刻要做的事」，所以也进这张表。 */
static void t_plan(void) {
    printf("-- game_auto_plan（穷举）\n");
    ga_step_t s[3];
    int n;

    n = game_auto_plan("", "", 0, 0, 0, 0, 0, s);
    CHECK(n == 0, "都没在跑 → 不该有动作，实得 %d", n);

    /* 启动：把远程那份拉下来 */
    n = game_auto_plan("", TID, 0, 0, 0, 1, 1, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PULL && !strcmp(s[0].tid, TID),
          "启动该拉取，实得 n=%d act=%d", n, n ? s[0].act : -1);
    n = game_auto_plan("", TID, 0, 0, 0, 1, 0, s);
    CHECK(n == 0, "没开 on_launch → 不该拉，实得 %d", n);
    n = game_auto_plan("", TID, 0, 0, 0, 0, 1, s);
    CHECK(n == 0, "总开关没开 → 不该拉，实得 %d", n);

    /* 退出：把主机这份推上去 */
    n = game_auto_plan(TID, "", 1, 1, 0, 0, 0, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PUSH && !strcmp(s[0].tid, TID),
          "退出该推送，实得 n=%d act=%d", n, n ? s[0].act : -1);
    n = game_auto_plan(TID, "", 1, 0, 0, 0, 0, s);
    CHECK(n == 0, "没开 on_exit → 不该推，实得 %d", n);
    n = game_auto_plan(TID, "", 0, 1, 0, 0, 0, s);
    CHECK(n == 0, "总开关没开 → 不该推，实得 %d", n);

    /* [CN-FORK] v0.5.2.154 退出时的本地快照：**排在推送之前**，且只看自己的开关 */
    n = game_auto_plan(TID, "", 1, 1, 1, 0, 0, s);
    CHECK(n == 2, "退出时「快照 + 推送」该有两步，实得 %d", n);
    CHECK(n == 2 && s[0].act == GA_ACT_SNAP && !strcmp(s[0].tid, TID),
          "退出第一步该是本地快照，实得 act=%d", n ? s[0].act : -1);
    CHECK(n == 2 && s[1].act == GA_ACT_PUSH, "退出第二步该是推送");
    /* 远程总开关关着 / on_exit 关着，都不影响本地快照 —— 本地备份不依赖网络，
     * 不该被远程那套连坐（这是刻意的，不是漏判）。 */
    n = game_auto_plan(TID, "", 0, 0, 1, 0, 0, s);
    CHECK(n == 1 && s[0].act == GA_ACT_SNAP,
          "远程开关全关也要做本地快照，实得 n=%d act=%d", n, n ? s[0].act : -1);
    n = game_auto_plan(TID, "", 1, 1, 0, 0, 0, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PUSH, "快照没开 → 只剩推送，实得 %d", n);

    /* 换游戏：**先快照旧的、再推旧的、最后拉新的**，顺序写死才有一份可预期的日志 */
    n = game_auto_plan(TID, TID2, 1, 1, 0, 1, 1, s);
    CHECK(n == 2, "换游戏该有两步，实得 %d", n);
    CHECK(n == 2 && s[0].act == GA_ACT_PUSH && !strcmp(s[0].tid, TID),
          "换游戏第一步该是推旧的");
    CHECK(n == 2 && s[1].act == GA_ACT_PULL && !strcmp(s[1].tid, TID2),
          "换游戏第二步该是拉新的");

    n = game_auto_plan(TID, TID2, 1, 1, 1, 1, 1, s);
    CHECK(n == 3, "换游戏 + 快照该有三步，实得 %d", n);
    CHECK(n == 3 && s[0].act == GA_ACT_SNAP && s[1].act == GA_ACT_PUSH
              && s[2].act == GA_ACT_PULL && !strcmp(s[0].tid, TID)
              && !strcmp(s[2].tid, TID2),
          "换游戏顺序该是 快照(旧) → 推(旧) → 拉(新)");

    n = game_auto_plan(TID, TID2, 0, 0, 0, 1, 1, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PULL, "旧的没开 → 只拉新的，实得 %d", n);
    n = game_auto_plan(TID, TID2, 1, 1, 0, 0, 0, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PUSH, "新的没开 → 只推旧的，实得 %d", n);

    /* 同一款连着两轮 —— 绝大多数轮次走这里。快照开着也一样：**不能每轮都备份一次**
     * （那就是把刚删掉的定时快照换个名字又请回来了）。 */
    n = game_auto_plan(TID, TID, 1, 1, 1, 1, 1, s);
    CHECK(n == 0, "同一款连着跑不该有动作（快照开着也是），实得 %d", n);

    /* NULL 与空串等价 */
    n = game_auto_plan(NULL, NULL, 0, 0, 0, 0, 0, s);
    CHECK(n == 0, "NULL 该安全，实得 %d", n);
    n = game_auto_plan(TID, NULL, 1, 1, 0, 0, 0, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PUSH, "cur=NULL 该当没在跑（推送）");
    n = game_auto_plan(NULL, TID, 0, 0, 0, 1, 1, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PULL, "prev=NULL 该当之前没在跑（拉取）");
    /* prev 为空时快照也不该冒出来（没东西可备份） */
    n = game_auto_plan(NULL, TID, 1, 1, 1, 1, 1, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PULL, "prev=NULL 时不该有快照步骤，实得 %d", n);
}

/* ══ 拉取前的新旧判定（穷举）═══════════════════════════════════════════
 * [CN-FORK] v0.5.2.154 用户口径：「如果本地存档比远程存档要更新，则不拉取；
 * 本地存档比远程存档旧，可以直接拉取覆盖」。
 * [CN-FORK] v0.5.2.162 再加一句：「如果本地存档和远程存档一模一样，则不进行拉取，
 * 也就是说，只有远程的存档比本地的新的时候才进行拉取」。
 * 这个判定的**每一条边界**都在这里钉死 —— 它一边要挡住「用旧存档盖掉新进度」（丢数据），
 * 一边要挡住「推完又原样拉回来」（白跑网络 + 看起来像没生效），错一格都是真事故。
 *
 * 参数顺序：(local_mtime, local_have, remote_mtime, remote_src_mtime, remote_have)。
 * remote_mtime = 远端那个文件**落盘**的时刻（对拉取毫无意义，只是老包的兜底）；
 * remote_src_mtime = 这份包**是从哪个时刻的存档打出来的**（`.162` 起才有）。 */
static void t_pull_gate(void) {
    printf("-- ga_pull_needed（拉取前先比新旧）\n");
    /* 远端没有 → 没得拉 */
    CHECK(ga_pull_needed(100, 1, 0, 0, 0) == 0, "远端没有 → 不该拉");
    CHECK(ga_pull_needed(0, 0, 0, 0, 0) == 0, "两边都没有 → 不该拉");
    /* 本地没有 → 必须拉（换机 / 误删 / 重装，正是拉取要救的场景）。
     * 这条**排在来源判定之前**：本地都空了，哪怕远端那份的 src_mtime 看着比本地
     * 传的怪时间戳还旧，也照样得拉 —— 不然「删了存档想拉回来」会被算成不用拉。 */
    CHECK(ga_pull_needed(0, 0, 100, 0, 1) == 1, "本地没有 → 必须拉");
    CHECK(ga_pull_needed(999999, 0, 100, 50, 1) == 1,
          "本地没有（哪怕传了个怪时间戳、哪怕远程来源更旧）→ 必须拉");
    /* ── `.162` 新口径：认得出「远端就是本地这份推上去的」 ──
     * remote_mtime=200 是推送那一刻落盘的时间，看着比本地新 —— 这正是 `.154`
     * 会白拉一次的原因；src_mtime=100 与本地相等 → 一模一样 → 不许再拉。 */
    CHECK(ga_pull_needed(100, 1, 200, 100, 1) == 0,
          "远程那份就是本地这份推上去的（一模一样）→ 不该拉");
    CHECK(ga_pull_needed(100, 1, 900, 90, 1) == 0, "远程那份的来源比本地旧 → 不该拉");
    CHECK(ga_pull_needed(100, 1, 900, 101, 1) == 1, "远程来源只新 1 秒 → 该拉");
    CHECK(ga_pull_needed(100, 1, 500, 300, 1) == 1,
          "别的机器推了更新的一份（远程来源比本地新）→ 该拉");
    /* ── `.162` 之前推上去的老包没有 src_mtime：退回 `.154` 口径 ──
     * 这里**不许**把「说不清」当「一模一样」—— 老包用户的自动拉取要照常工作。 */
    CHECK(ga_pull_needed(200, 1, 100, 0, 1) == 0, "老包：本地更新 → 不该拉");
    CHECK(ga_pull_needed(101, 1, 100, 0, 1) == 0, "老包：本地只新 1 秒 → 也不该拉");
    CHECK(ga_pull_needed(100, 1, 200, 0, 1) == 1, "老包：远程更新 → 该拉");
    CHECK(ga_pull_needed(100, 1, 100, 0, 1) == 1,
          "老包：认不出来源 → 相等也照拉（宁可多拉一次，不能关掉拉取）");
    /* 时间戳读不到（都是 0）但两边都有 → 该拉：**说不清不能变成不拉** */
    CHECK(ga_pull_needed(0, 1, 0, 0, 1) == 1, "时间戳都读不到 → 该拉");
}

/* ══ 拉取前的新旧判定：端到端（真协议 · HTTP 桩）═══════════════════════════
 * [CN-FORK] v0.5.2.154 上面那一节只测纯函数。这一节把**整条链**跑通，因为链上有
 * 两处纯函数测不到的东西，而它们任何一处错了都会让判定永远「说不清」：
 *   · 远端那份的时间戳**从哪来** —— 推送时一并写下的 <uid>.meta.json；
 *     FTP / HTTP 这条读取路径正是 `.154` 才补上的（原来一律回 -1）；
 *   · 本地那份的时间戳**怎么取** —— gs_walk 的「最晚被写过的时刻」。
 *
 * 为什么非要端到端：真机上「跳过拉取」是个**看不出来**的结果 —— 面板上就是一次
 * 没动静的拉取，没报错、没日志重点。只有把本地文件的内容和 mtime 都钉住，
 * 才能证明「确实一份都没动」。 */
static void set_mtime(const char *p, long long t) {
    struct timeval tv[2];
    tv[0].tv_sec = (time_t)t;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = (time_t)t;
    tv[1].tv_usec = 0;
    utimes(p, tv);
}

static void t_pull_gate_e2e(void) {
    printf("-- 拉取前的新旧判定（端到端 · HTTP 桩）\n");
    char url[600], body[300], out[2048], msg[300];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ps5save", g_http_port);

    fixture_reset();
    write_cfg("http", url, 1, 1, 1);

    /* ① 先推一份上去 —— 远端由此得到 zip + meta.json */
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "端到端：推送失败：%s", msg);

    char want[8192];
    long wn = rd(SAVE_FILE, want, sizeof(want));
    CHECK(wn > 0, "端到端：夹具没造出来");

    char rdir[700], mp[800];
    remote_dir(rdir, sizeof(rdir), TID);
    snprintf(mp, sizeof(mp), "%s/%s.meta.json", rdir, UID);
    CHECK(exists(mp), "推送该在远端一并写下 meta.json：%s", mp);

    long long now = (long long)time(NULL);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"pull\"}", TID);

    /* ② [CN-FORK] v0.5.2.162 刚推上去的那一份，紧接着拉取**该原样跳过**。
     *    这就是用户这一轮要的：「如果本地存档和远程存档一模一样，则不进行拉取，
     *    也就是说，只有远程的存档比本地的新的时候才进行拉取」。
     *
     *    `.154` 那版这一格是**必红**的：远端那个文件的落盘时刻（meta 里的 mtime）
     *    永远比存档本身的写入时刻晚，判定看到「远程更新」就真去拉了一遍 ——
     *    几百 MB 走网络，落回一模一样的字节，用户在面板上看到的却是「已拉取 1 份」。
     *    现在靠包里带的 src_mtime（打包时本地存档的最大 mtime）认得出「就是这份」。
     *
     *    这里**不碰任何 mtime**：本地保持推送那一刻的状态，与远端那份的来源相等。 */
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "端到端：刚推上去的这份紧接着拉取不该算失败：%s", msg);
    CHECK(strstr(out, "已跳过拉取") != NULL,
          "端到端：一模一样该报「已跳过拉取」，实得：%s", out);
    char got[8192];
    long gn = rd(SAVE_FILE, got, sizeof(got));
    CHECK(gn == wn && !memcmp(got, want, (size_t)wn),
          "端到端：刚推上去的不该被原样拉回来（实得 %ld / 期望 %ld 字节）", gn, wn);

    /* ③ 本地比远程新 → 跳过，且**绝不许动本地** */
    put(SAVE_FILE, 100, 'Z'); /* 内容换成一眼认得出来的 */
    set_mtime(SAVE_FILE, now + 3600);
    set_mtime(SAVE_DIR "/sdimg_sce_bu_TEST", now + 3600);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "端到端：本地更新时拉取不该算失败：%s", msg);
    CHECK(strstr(out, "已跳过拉取") != NULL,
          "端到端：该说清是「远程没有比本地更新的一份，已跳过拉取」：%s", out);
    gn = rd(SAVE_FILE, got, sizeof(got));
    CHECK(gn == 100 && got[0] == 'Z',
          "端到端：本地更新时**绝不能**被远程覆盖（实得 %ld 字节）", gn);

    /* ④ 本地比远程旧 → 真的拉下来覆盖 */
    set_mtime(SAVE_FILE, now - 3600);
    set_mtime(SAVE_DIR "/sdimg_sce_bu_TEST", now - 3600);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "端到端：本地较旧时拉取失败：%s", msg);
    gn = rd(SAVE_FILE, got, sizeof(got));
    CHECK(gn == wn && !memcmp(got, want, (size_t)wn),
          "端到端：本地较旧时该被远程覆盖（实得 %ld / 期望 %ld 字节）", gn, wn);

    /* ⑤ 远端元数据读不到（meta.json 被删 = `.145` 之前推的老存档）→ **照旧拉**。
     *    说不清不能变成不拉：老存档用户要是被跳过，等于自动拉取整个失效，
     *    而他们恰恰是最需要拉取的人（换了机 / 重装了游戏）。
     *    ⚠ 这条与 ③ 的区别只在 meta.json 在不在：本地一样新（now+3600），
     *    有元数据时判定是「跳过」，元数据没了就退回「照拉」。 */
    unlink(mp);
    put(SAVE_FILE, 100, 'Z');
    set_mtime(SAVE_FILE, now + 3600);
    set_mtime(SAVE_DIR "/sdimg_sce_bu_TEST", now + 3600);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "端到端：没有 meta.json 时拉取失败：%s", msg);
    gn = rd(SAVE_FILE, got, sizeof(got));
    CHECK(gn == wn && !memcmp(got, want, (size_t)wn),
          "端到端：没有 meta.json 时必须照旧拉（实得 %ld 字节）", gn);

    /* ⑥ [CN-FORK] v0.5.2.162 老包没有 src_mtime（`.162` 之前推上去的）：meta 在、
     *    但里面只有 files/size/mtime → 判定**不许**把「认不出来源」当成「一模一样」，
     *    得退回 `.154` 那条只防「用旧盖新」的口径。这里把 meta 手写成旧格式，
     *    并把本地拨成「比远端落盘时刻旧」—— 远程看着更新 → 该拉。 */
    {
        FILE *mf = fopen(mp, "wb");
        if (mf) {
            fprintf(mf, "{\"files\":2,\"size\":%ld,\"mtime\":%lld}", wn, now + 7200);
            fclose(mf);
        }
        CHECK(exists(mp), "端到端：旧格式 meta.json 没写进去");
        put(SAVE_FILE, 100, 'Z');
        set_mtime(SAVE_FILE, now - 7200);
        set_mtime(SAVE_DIR "/sdimg_sce_bu_TEST", now - 7200);
        out[0] = msg[0] = 0;
        CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
              "端到端：旧格式 meta 的拉取失败：%s", msg);
        gn = rd(SAVE_FILE, got, sizeof(got));
        CHECK(strstr(out, "已拉取") != NULL,
              "端到端：老包（没有 src_mtime）认不出来源时该照拉，实得：%s", out);
        CHECK(gn == wn && !memcmp(got, want, (size_t)wn),
              "端到端：老包该照常拉回来（实得 %ld / 期望 %ld 字节）", gn, wn);
    }
}

/* ══ 探针（宿主上没有 sceLncUtil）══════════════════════════════════════ */
static const char *g_fake_running = NULL;
static int probe_fn(char *out, size_t osz) {
    if (!g_fake_running) {
        if (out && osz) out[0] = 0;
        return 0;
    }
    snprintf(out, osz, "%s", g_fake_running);
    return 1;
}

/* ══ ②③④⑤ 手动推拉 + 状态 ═══════════════════════════════════════════ */
static void t_sync(void) {
    printf("-- 手动推 / 拉 + 运行期状态\n");
    char url[600], body[300], out[2048], msg[300], st[600], rp[700];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ps5save", g_http_port);

    fixture_reset();
    write_cfg("http", url, 1, 1, 1);
    remote_path(rp, sizeof(rp), TID);

    char want[8192];
    long wn = rd(SAVE_FILE, want, sizeof(want));
    CHECK(wn > 0, "夹具没造出来");

    /* 推送 */
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "手动推送失败：%s", msg);
    CHECK(strstr(out, "\"ok\":true") != NULL, "推送响应该是 ok:true：%s", out);
    CHECK(strstr(out, "\"action\":\"push\"") != NULL, "推送响应该带 action：%s", out);
    CHECK(exists(rp), "远端没收到文件：%s", rp);
    long rn = rd(rp, st, sizeof(st));
    CHECK(rn > 4 && !memcmp(st, "PK\x03\x04", 4), "远端收到的不像 zip");

    /* 拉取：先删本地，验内容逐字节一样 */
    rm_rf(SAVE_DIR);
    CHECK(!exists(SAVE_FILE), "本地没删干净");
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"pull\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "手动拉取失败：%s", msg);
    CHECK(strstr(out, "\"ok\":true") != NULL, "拉取响应该是 ok:true：%s", out);
    char got[8192];
    long gn = rd(SAVE_FILE, got, sizeof(got));
    CHECK(gn == wn && !memcmp(got, want, (size_t)wn), "拉回来的内容跟推上去的不一样");
    CHECK(exists(SAVE_DIR "/sdimg_sce_bu_TEST"), "拉回来少了 _bu_ 备份");

    /* ⑤ 运行期状态 */
    CHECK(game_auto_state_json(TID, st, sizeof(st)) > 0, "状态 JSON 生成失败");
    CHECK(strstr(st, "\"action\":\"pull\"") != NULL,
          "状态该记下最后一次是 pull（最近一次动作）：%s", st);
    CHECK(strstr(st, "\"ok\":true") != NULL, "状态该记 ok:true：%s", st);
    CHECK(strstr(st, "\"running\":false") != NULL, "宿主上没装探针时该是 false：%s", st);

    g_fake_running = TID;
    game_auto_set_probe(probe_fn);
    CHECK(game_auto_state_json(TID, st, sizeof(st)) > 0, "状态 JSON 生成失败");
    CHECK(strstr(st, "\"running\":true") != NULL, "探针报这款在跑，running 该是 true：%s", st);
    g_fake_running = TID2;
    CHECK(game_auto_state_json(TID, st, sizeof(st)) > 0, "状态 JSON 生成失败");
    CHECK(strstr(st, "\"running\":false") != NULL,
          "在跑的是另一款，这款该是 false：%s", st);
    g_fake_running = NULL;
    game_auto_set_probe(NULL);

    /* ⑥ 游戏正在运行时手动推 / 拉一律不做（`.154` 用户口径：「游戏进行中，拉取和
     * 推送存档的动作不让做」）。两条理由都是数据安全：
     *   · 拉取把盘上存档**整份换掉**，而游戏早把存档读进内存了 —— 换完它不知道，
     *     之后存档 / 退出还会把内存那份写回去，等于白换还搅乱了用户刚玩的进度；
     *   · 推送会在游戏**正写盘时**去打包，拷到的可能是撕裂的半截存档，
     *     把一份坏存档留在远端，比不推糟糕得多。
     * 所以这里钉三件事：两条都拦、理由说得清、而且**在碰网络之前就拦下来**
     * （用「开始」日志的条数证明：一次都没尝试过）。 */
    {
        /* 反向对照：在跑的是**另一款**时不能误拦 —— 那是完全正常的操作 */
        g_fake_running = TID2;
        game_auto_set_probe(probe_fn);
        snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
        out[0] = msg[0] = 0;
        CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
              "在跑的是另一款时不该误拦：%s", msg);

        /* 这一行**必须**在对照推送之后取：对照那条会正常留下「开始」日志 */
        int lb = log_count("远程拉取 开始") + log_count("远程推送 开始");

        g_fake_running = TID;
        for (int k = 0; k < 2; k++) {
            const char *a = (k == 0) ? "push" : "pull";
            snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"%s\"}", TID, a);
            out[0] = msg[0] = 0;
            CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
                  "游戏正在运行时 %s 必须被拦", a);
            CHECK(strstr(msg, "正在运行") != NULL,
                  "%s 被拦时要说清是因为在运行：%s", a, msg);
        }
        CHECK(log_count("远程拉取 开始") + log_count("远程推送 开始") == lb,
              "被拦的操作**不该**留下「开始」日志（说明是拦在碰网络之前）");

        g_fake_running = NULL;
        game_auto_set_probe(NULL);
    }

    /* ③ 远端还没有这个文件 —— 不是失败 */
    remote_dir(rp, sizeof(rp), TID);
    rm_rf(rp);
    CHECK(!exists(rp), "远端目录没删干净：%s", rp);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"pull\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "远端没存档时拉取不该算失败：%s", msg);
    CHECK(strstr(out, "\"ok\":true") != NULL, "远端没存档该回 ok:true：%s", out);
    CHECK(strstr(out, "远程还没有") != NULL, "提示要说清是远程还没有：%s", out);
    /* 而且**不能动本地的存档** —— 拉失败把用户的存档弄没是最坏的 */
    CHECK(exists(SAVE_FILE), "拉取失败不该动本地存档");

    /* ④ 不认识的协议（smb 在 .143 起已支持，这里改用真·不认识的协议名测「不支持」） */
    write_cfg("webdav", "192.168.1.10", 1, 1, 1);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "不认识的协议必须失败");
    CHECK(strstr(msg, "不支持") != NULL, "不认识的协议报错要说清不支持：%s", msg);

    /* ④b smb 已支持，但缺主机要在碰网络前就拦下来、且说清是缺主机 */
    write_cfg("smb", "", 1, 1, 1);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "smb 缺主机必须失败");
    CHECK(strstr(msg, "主机") != NULL, "smb 缺主机报错要说清是主机：%s", msg);

    /* 参数不合法 */
    write_cfg("http", url, 1, 1, 1);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"bogus\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "未知 action 必须失败");
    snprintf(body, sizeof(body), "{\"tid\":\"../etc\",\"action\":\"push\"}");
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "带 .. 的 tid 必须失败");
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\",\"uid\":\"a/b\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "带 / 的 uid 必须失败");
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action("{}", 2, out, sizeof(out), msg, sizeof(msg)) == -1,
          "空请求必须失败");
}

/* ══ ⑦ 推 / 拉的「开始 / 结束」两条 debug 行（v0.5.2.147）═════════════════
 * 用户口径：「远程拉取和推送远程的时候，两个动作的开始和结束都要 debug 到屏幕信息」。
 * 这两行走 pldmgr_log → 面板「日志」页的环形缓冲，所以它们是**用户看得见的产物**：
 * 少一条、或者两条不成对，用户点完按钮就还是「不知道到底跑没跑」。
 * 这一节钉四件事：① 成功的一对、且「开始」在「结束」之前；② 开始行带触发来源与目标；
 * ③ 结束行带计数与用时；④ **提前返回的失败路径也必须成对** —— 这正是把「开始」放在
 * 包装层（而不是 ga_do_inner 里）的全部理由，不钉住它会被后来的重构改回去。 */
static void t_log(void) {
    printf("-- 推 / 拉的开始 / 结束 debug 行\n");
    char url[600], body[300], out[2048], msg[300];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ps5save", g_http_port);

    fixture_reset();
    write_cfg("http", url, 1, 1, 1);

    /* ① 推送成功的一对 */
    g_log_n = 0;
    g_notify_n = 0;
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "手动推送失败：%s", msg);
    CHECK(log_count("远程推送 开始") == 1, "推送该有且只有一条「开始」（实得 %d）",
          log_count("远程推送 开始"));
    CHECK(log_count("远程推送 结束") == 1, "推送该有且只有一条「结束」（实得 %d）",
          log_count("远程推送 结束"));
    int ps = log_at("远程推送 开始"), pe = log_at("远程推送 结束");
    CHECK(ps >= 0 && pe > ps, "「开始」必须在「结束」之前（实得 %d / %d）", ps, pe);
    CHECK(ps >= 0 && strstr(g_log[ps], TID) != NULL, "开始行该带 tid：%s",
          ps >= 0 ? g_log[ps] : "(没有开始行)");
    CHECK(ps >= 0 && strstr(g_log[ps], "触发=手动") != NULL, "开始行该标出是手动触发：%s",
          ps >= 0 ? g_log[ps] : "(没有开始行)");
    CHECK(ps >= 0 && strstr(g_log[ps], "范围=全部用户") != NULL, "不传 uid 时范围该是全部用户：%s",
          ps >= 0 ? g_log[ps] : "(没有开始行)");
    CHECK(ps >= 0 && strstr(g_log[ps], "目标=HTTP · ") != NULL && strstr(g_log[ps], url) != NULL,
          "开始行该带配的那个远程 URL：%s", ps >= 0 ? g_log[ps] : "(没有开始行)");
    CHECK(pe >= 0 && strstr(g_log[pe], "成功 1") != NULL, "结束行该报成功 1 份：%s",
          pe >= 0 ? g_log[pe] : "(没有结束行)");
    CHECK(pe >= 0 && strstr(g_log[pe], "失败 0") != NULL, "结束行该报失败 0：%s",
          pe >= 0 ? g_log[pe] : "(没有结束行)");
    CHECK(pe >= 0 && strstr(g_log[pe], "用时 ") != NULL, "结束行该带用时：%s",
          pe >= 0 ? g_log[pe] : "(没有结束行)");
    /* 推送只回字节数（sr_push 不填 files）→ 结束行**不许**报「个文件」，
     * 否则就是「0 个文件 / 2648 字节」这种自相矛盾的一行。 */
    CHECK(pe >= 0 && strstr(g_log[pe], "字节") != NULL && strstr(g_log[pe], "个文件") == NULL,
          "推送的结束行只该报字节数：%s", pe >= 0 ? g_log[pe] : "(没有结束行)");

    /* ①b 同一对也要出现在**屏幕通知**上（用户要的「屏幕信息」就是它） */
    CHECK(notify_count("远程推送 开始") == 1, "推送该有且只有一条「开始」通知（实得 %d）",
          notify_count("远程推送 开始"));
    CHECK(notify_count("远程推送 结束") == 1, "推送该有且只有一条「结束」通知（实得 %d）",
          notify_count("远程推送 结束"));
    int pns = notify_at("远程推送 开始"), pne = notify_at("远程推送 结束");
    CHECK(pns >= 0 && pne > pns, "通知里「开始」也必须在「结束」之前（实得 %d / %d）", pns, pne);
    CHECK(pns >= 0 && strstr(g_notify[pns], TID) != NULL
              && strstr(g_notify[pns], "触发=手动") != NULL
              && strstr(g_notify[pns], "范围=全部用户") != NULL
              && strstr(g_notify[pns], url) != NULL,
          "开始通知该带 tid / 触发 / 范围 / 目标：%s",
          pns >= 0 ? g_notify[pns] : "(没有开始通知)");
    CHECK(pne >= 0 && strstr(g_notify[pne], "成功 1") != NULL
              && strstr(g_notify[pne], "失败 0") != NULL
              && strstr(g_notify[pne], "用时 ") != NULL,
          "结束通知该带计数与用时：%s", pne >= 0 ? g_notify[pne] : "(没有结束通知)");
    CHECK(pne >= 0 && strstr(g_notify[pne], "个文件") == NULL,
          "推送的结束通知也只该报字节数：%s", pne >= 0 ? g_notify[pne] : "(没有结束通知)");

    /* ② 拉取成功的一对，且不能串到「推送」那个动作名上 */
    g_log_n = 0;
    g_notify_n = 0;
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"pull\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "手动拉取失败：%s", msg);
    CHECK(log_count("远程拉取 开始") == 1 && log_count("远程拉取 结束") == 1,
          "拉取该有开始 / 结束各一条（实得 %d / %d）",
          log_count("远程拉取 开始"), log_count("远程拉取 结束"));
    CHECK(log_count("远程推送 开始") == 0, "拉取不该打出「推送」的行");
    int pl = log_at("远程拉取 结束");
    CHECK(pl >= 0 && strstr(g_log[pl], "个文件") != NULL,
          "拉取的结束行该报文件数（拉回来的确实是文件）：%s",
          pl >= 0 ? g_log[pl] : "(没有结束行)");
    CHECK(notify_count("远程拉取 开始") == 1 && notify_count("远程拉取 结束") == 1,
          "拉取的通知也该成对（实得 %d / %d）",
          notify_count("远程拉取 开始"), notify_count("远程拉取 结束"));
    int pln = notify_at("远程拉取 结束");
    CHECK(pln >= 0 && strstr(g_notify[pln], "个文件") != NULL,
          "拉取的结束通知该报文件数：%s", pln >= 0 ? g_notify[pln] : "(没有结束通知)");

    /* ③ 指定 uid 时「范围」要跟着变 */
    g_log_n = 0;
    g_notify_n = 0;
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"pull\",\"uid\":\"%s\"}", TID, UID);
    out[0] = msg[0] = 0;
    (void)game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg));
    int us = log_at("远程拉取 开始");
    CHECK(us >= 0 && strstr(g_log[us], "范围=" UID) != NULL,
          "指定 uid 时范围该是那个 uid：%s", us >= 0 ? g_log[us] : "(没有开始行)");

    /* ④ **提前返回的失败路径也必须成对**（「开始」放包装层的全部理由）。
     * 这里走「协议不支持」那条提前 return。 */
    write_cfg("webdav", "192.168.1.10", 1, 1, 1);
    g_log_n = 0;
    g_notify_n = 0;
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "不认识的协议必须失败");
    CHECK(log_count("远程推送 开始") == 1, "失败路径也要有「开始」（实得 %d）",
          log_count("远程推送 开始"));
    CHECK(log_count("远程推送 结束") == 1, "失败路径也要有「结束」（实得 %d）",
          log_count("远程推送 结束"));
    int fe = log_at("远程推送 结束");
    CHECK(fe >= 0 && strstr(g_log[fe], "失败 1") != NULL, "结束行该报失败 1：%s",
          fe >= 0 ? g_log[fe] : "(没有结束行)");
    CHECK(fe >= 0 && strstr(g_log[fe], "不支持") != NULL, "结束行该带上那句人话错误：%s",
          fe >= 0 ? g_log[fe] : "(没有结束行)");
    CHECK(notify_count("远程推送 开始") == 1 && notify_count("远程推送 结束") == 1,
          "失败路径的通知也要成对（实得 %d / %d）",
          notify_count("远程推送 开始"), notify_count("远程推送 结束"));
    int fne = notify_at("远程推送 结束");
    CHECK(fne >= 0 && strstr(g_notify[fne], "失败 1") != NULL
              && strstr(g_notify[fne], "不支持") != NULL,
          "失败通知该带失败计数与原因：%s", fne >= 0 ? g_notify[fne] : "(没有结束通知)");

    /* ⑤ 连配置都没有时：开始行写「未配置」，不是留空 */
    fixture_reset();
    g_log_n = 0;
    g_notify_n = 0;
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "没有配置时必须失败");
    int ns = log_at("远程推送 开始");
    CHECK(ns >= 0 && strstr(g_log[ns], "目标=未配置") != NULL,
          "读不到配置时开始行该写「未配置」：%s", ns >= 0 ? g_log[ns] : "(没有开始行)");
    CHECK(log_count("远程推送 结束") == 1, "没有配置时也要有「结束」（实得 %d）",
          log_count("远程推送 结束"));
}

/* ══ ⑧ 拉取前冻结游戏 / 拉完放行（v0.5.2.150，机制在 .152 换成 SIGSTOP）════
 * 用户口径的顺序：**游戏启动 → 引擎监测到启动 → 暂停游戏 → 拉取存档 → 拉取成功 → 恢复游戏**。
 *
 * 这条链路在宿主上原来是**零覆盖**的：`ga_tick` 是 static，而冻结那一段只有它走得到。
 * `.150` 把 tick 导出（`game_auto_tick`）就是为了让这里能把**顺序**钉死 ——
 * 冻结必须发生在「远程拉取 开始」之前、放行必须在「远程拉取 结束」之后。
 * 顺序错了整个修复就没意义（先拉后冻 = 游戏照样读到旧存档），而它恰恰是最容易被
 * 后来的重构改坏的地方（比如顺手把 freeze 挪到 ga_do_inner 后面）。
 *
 * 为什么冻结能治「偶尔成功、偶尔失败」：拉取是把盘上存档**整份换掉**，而游戏启动时
 * 会把自己的存档读进内存。谁先谁后决定成败 —— 快启动的游戏输、慢启动的赢，这就是
 * 用户看到的「偶尔」。冻住它之后游戏读不到存档，等我们拉完再放行，竞态就没有了。
 *
 * `.152` 起真机默认走**进程级 SIGSTOP**（不再让游戏离开前台，见 include/game_auto.h）。
 * 但这里断言的东西**与机制无关**，所以一个字都不用改：冻结一次、放行一次、
 * 放行必须拿冻结时报的那个凭据、顺序固定。机制选择是 `ga_frz_default` 内部的事，
 * 宿主上两套真实现都是「直接返回 -1」，本用例用桩替掉整个默认实现。 */

static int g_susp_n, g_resm_n;
static char g_susp_tid[32], g_resm_tid[32];
/* ★ 放行必须收到**冻结时报的那个**凭据。`.150` 它是 app_id，`.152` 起可能只是
 * 一个 pid —— 但「不许当场重新解析」这条不变量一模一样，所以断言照旧。 */
static unsigned int g_susp_id, g_resm_id;
static int g_resm_kind; /* 机制代号也要原样传回（SIGSTOP / LNC） */
static int g_susp_rc, g_resm_rc;

/* 冻结桩：成功时**必须**写出一个凭据（真机上：SIGSTOP 给 pid、LncUtil 给 app_id）。
 * 这里给一个假但稳定的值，好让「放行拿到的是不是同一个」变成可断言的。
 *
 * 这条不变量是 `.150` 真机首轮的直接教训：真机上恢复代码**又解析了一次** app_id，
 * 而应用被挂起后 `sceLncUtilGetAppIdOfRunningBigApp()` 返回 0xffffffff
 * （系统认为没有「在运行的大应用」）→ 解析必然失败 → `ResumeApp` 根本没发出去 →
 * 游戏一直冻着。日志原形：「恢复失败」的下一秒就是「已退出」。
 * SIGSTOP 那条路同理：重找一个 eboot.bin 会拿到新进程，被冻住的那个就没人放了。 */
#define FAKE_APP_ID 0x10007u

static int susp_stub(const char *tid, int *kind_out, unsigned int *handle_out) {
    g_susp_n++;
    snprintf(g_susp_tid, sizeof(g_susp_tid), "%s", tid ? tid : "");
    if (g_susp_rc == 0) {
        if (kind_out) *kind_out = GA_FREEZE_SIGSTOP; /* 真机默认走这条 */
        if (handle_out) *handle_out = FAKE_APP_ID;
    }
    return g_susp_rc;
}
static int resm_stub(const char *tid, int kind, unsigned int handle) {
    g_resm_n++;
    snprintf(g_resm_tid, sizeof(g_resm_tid), "%s", tid ? tid : "");
    g_resm_kind = kind;
    g_resm_id = handle;
    return g_resm_rc;
}

/* 只在最前面几次探测里报「TID 在跑」，之后报「没在跑」——
 * 用来造「tick 探到了、等真去挂起时游戏已经退出」这个窗口。 */
static int g_probe_budget;
static int probe_budget_fn(char *out, size_t osz) {
    if (g_probe_budget > 0) {
        g_probe_budget--;
        snprintf(out, osz, "%s", TID);
        return 1;
    }
    if (out && osz) out[0] = 0;
    return 0;
}

static void t_freeze(void) {
    printf("-- 拉取前冻结游戏 / 拉完放行\n");
    char url[600], body[300], out[2048], msg[300], rp[700];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ps5save", g_http_port);

    fixture_reset();
    /* on_exit=0：这个用例只关心「启动拉取」那条路。退出推送会把
     * GA_EXIT_SETTLE(5s) 的 sleep 拖进来，还会往日志里塞无关的行。 */
    write_cfg("http", url, 1, 1, 0);
    remote_path(rp, sizeof(rp), TID);

    /* 前置：先推一份上去，拉取才有东西可拉 */
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "前置推送失败：%s", msg);
    CHECK(exists(rp), "前置：远端没收到文件：%s", rp);

    game_auto_set_probe(probe_fn);
    game_auto_set_freezer(susp_stub, resm_stub);

    /* ── ① 正常路径：挂起 → 拉取 → 恢复，顺序必须对 ─────────────────────── */
    g_fake_running = NULL;
    game_auto_tick(); /* 让 prev 归零（上一轮没有游戏在跑） */
    rm_rf(SAVE_DIR);  /* 主机上这份存档「丢了」→ 拉回来才有可验证的变化 */
    CHECK(!exists(SAVE_FILE), "前置：本地没删干净");

    g_log_n = 0;
    g_notify_n = 0;
    g_susp_n = g_resm_n = 0;
    g_susp_rc = g_resm_rc = 0;
    g_susp_tid[0] = g_resm_tid[0] = 0;
    g_susp_id = g_resm_id = 0;
    g_resm_kind = -1; /* 没放过行时该是 -1，别让上一轮的残留蒙混过关 */

    g_fake_running = TID;
    game_auto_tick(); /* 这一轮：启动 → 挂起 → 拉取 → 恢复 */

    CHECK(g_susp_n == 1, "该挂起一次（实得 %d）", g_susp_n);
    CHECK(g_resm_n == 1, "该恢复一次（实得 %d）", g_resm_n);
    CHECK(!strcmp(g_susp_tid, TID), "挂起该作用在 %s 上（实得「%s」）", TID, g_susp_tid);
    CHECK(!strcmp(g_resm_tid, TID), "恢复该作用在 %s 上（实得「%s」）", TID, g_resm_tid);
    /* ★ 放行必须用**冻结时报的那个**凭据，不许再解析一次 ——
     * `.150`：真机上挂起后 GetAppIdOfRunningBigApp 返回 0xffffffff，再解析必然
     * 拿不到，于是 ResumeApp 发不出去、游戏一直冻着。
     * `.152`：SIGSTOP 那条路的凭据是 pid，重找会拿到新进程，而被冻住的那个就
     * 没人放了。机制换了，不变量没换。 */
    CHECK(g_resm_id == FAKE_APP_ID,
          "放行必须用冻结时报的凭据（实得 0x%x，期望 0x%x）", g_resm_id, FAKE_APP_ID);
    /* 机制代号同样要原样传回：传错了会拿另一套 API 去放行（SIGSTOP 冻的用
     * ResumeApp 去解 = 游戏一直冻着）。 */
    CHECK(g_resm_kind == GA_FREEZE_SIGSTOP,
          "放行该收到冻结时报的机制代号（实得 %d，期望 %d）", g_resm_kind, GA_FREEZE_SIGSTOP);
    CHECK(exists(SAVE_FILE), "拉取没把存档拉回来");

    int fs = log_at("已暂停"), ps = log_at("远程拉取 开始");
    int pe = log_at("远程拉取 结束"), rs = log_at("已恢复");
    CHECK(fs >= 0, "日志里没有「已暂停」这一行");
    CHECK(rs >= 0, "日志里没有「已恢复」这一行");
    /* ★ 这四条是这个用例存在的全部理由：顺序必须逐字对上用户口径那句话 ——
     *   「游戏启动 → 监测到启动 → 暂停游戏 → 拉取存档 → 拉取成功 → 恢复游戏」。
     *   先拉后冻 = 游戏照样读到旧存档，整个修复就没了意义；而它恰恰是最容易被
     *   后来的重构改坏的地方（比如顺手把 freeze 挪到 ga_do_inner 后面）。 */
    CHECK(fs == 0, "「已暂停」该是拉取这条链上的**第一行**（实得 @%d）", fs);
    CHECK(fs >= 0 && ps >= 0 && fs < ps,
          "冻结必须在「拉取 开始」之前（实得 冻结@%d / 开始@%d）", fs, ps);
    CHECK(ps >= 0 && pe > ps, "「拉取 开始」必须在「拉取 结束」之前（实得 %d / %d）", ps, pe);
    CHECK(pe >= 0 && rs >= 0 && pe < rs,
          "放行必须在「拉取 结束」之后（实得 结束@%d / 放行@%d）", pe, rs);

    /* ── ② 挂不住时：拉取照跑，但**不恢复**（没冻过就不能去放）───────────── */
    g_fake_running = NULL;
    game_auto_tick(); /* 归零 prev（on_exit=0，不会触发推送） */
    rm_rf(SAVE_DIR);
    g_log_n = 0;
    g_notify_n = 0;
    g_susp_n = g_resm_n = 0;
    g_susp_rc = -1;
    g_resm_rc = 0;
    g_fake_running = TID;
    game_auto_tick();

    CHECK(g_susp_n == 1, "该**尝试**挂起一次（实得 %d）", g_susp_n);
    CHECK(g_resm_n == 0, "挂起失败时不该去恢复（实得 %d 次）", g_resm_n);
    CHECK(exists(SAVE_FILE), "挂起失败也必须照样拉取 —— 不能因为冻不住就什么都不做");
    CHECK(log_count("暂停失败") == 1, "冻结失败要留一行痕（实得 %d）",
          log_count("暂停失败"));

    /* ── ③ 探测到 ga_do 之间游戏退出了：不挂起、不恢复，拉取照跑 ─────────── */
    g_fake_running = NULL;
    game_auto_tick(); /* 归零 prev */
    rm_rf(SAVE_DIR);
    g_log_n = 0;
    g_notify_n = 0;
    g_susp_n = g_resm_n = 0;
    g_susp_rc = g_resm_rc = 0;
    g_probe_budget = 1; /* 只有 tick 那一次探到「在跑」，等真要挂起时已经没了 */
    game_auto_set_probe(probe_budget_fn);
    game_auto_tick();

    CHECK(g_susp_n == 0, "游戏已经退出了，不该去挂起（实得 %d）", g_susp_n);
    CHECK(g_resm_n == 0, "没挂起过就不该恢复（实得 %d）", g_resm_n);
    CHECK(exists(SAVE_FILE), "游戏退出了也必须照样拉取");
    CHECK(log_count("已暂停") == 0, "没冻成就不该打「已暂停」（实得 %d）",
          log_count("已暂停"));

    /* ── ④ 手动拉取**不**挂起（防「顺手给手动也加上」的回归）──────────────
     * 手动拉取时游戏早把存档读进内存了，冻它改变不了内存里那份，只会平白把
     * 用户正在看的画面冻一下。 */
    game_auto_set_probe(probe_fn);
    g_fake_running = TID;
    g_susp_n = g_resm_n = 0;
    g_log_n = 0;
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"pull\"}", TID);
    out[0] = msg[0] = 0;
    (void)game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg));
    CHECK(g_susp_n == 0 && g_resm_n == 0,
          "手动拉取不该挂起 / 恢复游戏（实得 %d / %d）", g_susp_n, g_resm_n);
    CHECK(log_count("已暂停") == 0, "手动拉取不该打「已暂停」（实得 %d）",
          log_count("已暂停"));

    /* ── ⑤ 恢复失败：日志 + **屏幕通知** 都必须有 ──────────────────────────
     * 游戏会一直冻着，用户看到的是「点了一下就死机了」。这条不报出来，用户只会
     * 以为面板把游戏搞坏了 —— 而这恰恰是 `.150` 真机首轮真实发生的事。 */
    g_fake_running = NULL;
    game_auto_tick(); /* 归零 prev */
    rm_rf(SAVE_DIR);
    g_log_n = 0;
    g_notify_n = 0;
    g_susp_n = g_resm_n = 0;
    g_susp_rc = 0;
    g_resm_rc = -1;
    g_resm_id = 0;
    g_resm_kind = -1;
    g_fake_running = TID;
    game_auto_tick();

    CHECK(g_susp_n == 1 && g_resm_n == 1, "该挂起一次、恢复一次（实得 %d / %d）",
          g_susp_n, g_resm_n);
    CHECK(log_count("恢复失败") == 1, "恢复失败要留一行日志（实得 %d）",
          log_count("恢复失败"));
    CHECK(notify_count("恢复失败") == 1, "恢复失败必须弹屏幕通知（实得 %d）",
          notify_count("恢复失败"));
    int rf = log_at("恢复失败");
    CHECK(rf >= 0 && strstr(g_log[rf], TID) != NULL, "恢复失败的日志该带 tid：%s",
          rf >= 0 ? g_log[rf] : "(没有这行)");
    CHECK(exists(SAVE_FILE), "恢复失败不影响拉取本身（存档该拉回来还是拉回来）");

    g_resm_rc = 0;
    g_fake_running = NULL;
    game_auto_set_probe(NULL);
    game_auto_set_freezer(NULL, NULL);
}

/* ⑨ [CN-FORK] v0.5.2.153 冻结验证的**判定策略**（纯函数，16 种组合穷举）。
 *
 * 为什么单独立一节：`.152` 真机栽的那一跤不是机制错，是**策略**错 ——
 * 「读不到进程状态」时它 return -1，于是整条 SIGSTOP 主方案作废、回落 LncUtil，
 * 游戏跳回主屏还切不回来（用户口径：「远程拉取成功后，并没有将游戏拉到前台」）。
 * 而真机那段代码在 `#ifdef GA_HOST` 里、宿主根本编不到，所以判定被抽成纯函数
 * `ga_freeze_verdict()` —— 这里把它的 16 种输入组合全部穷举一遍。
 *
 * 判据只有两句话：
 *   · **只有**「对照推进了、冻结后还在推进」才叫「没冻住」（返回 -1，才允许回落）；
 *   · 其余「说不清」的一律算「无法验证」（返回 0），**照冻不误**。 */
static void t_verdict(void) {
    /* 索引 = have_ctl<<3 | ctl_adv<<2 | post_ok<<1 | post_adv。
     * 只有最后两格是「说清了」的：1110 = 确认冻住、1111 = 确认没冻住。 */
    static const int exp[16] = {
        0, 0, 0, 0, /* 0-3   : have_ctl=0 —— 对照都读不到，一律「无法验证」 */
        0, 0, 0, 0, /* 4-7   : have_ctl=1, ctl_adv=0 —— 进程本来就没烧 CPU，同样「无法验证」 */
        0, 0, 0, 0, /* 8-11  : have_ctl=1, ctl_adv=1, post_ok=0 —— 冻结后读不到，还是「无法验证」 */
        0, 0, 1, -1 /* 12-15 : 12/13 是 post_ok=0（同上）；1110 = 确认冻住 · 1111 = 确认没冻住 */
    };
    int bad_n = 0;
    for (int i = 0; i < 16; i++) {
        int got = ga_freeze_verdict((i >> 3) & 1, (i >> 2) & 1, (i >> 1) & 1, i & 1);
        if (got != exp[i]) bad_n++;
    }
    CHECK(bad_n == 0, "16 种组合的冻结判定全对（错 %d 个）", bad_n);

    /* 下面三条是**带名字的**策略断言 —— 判定表那条只说「错了几格」，
     * 说不出错在哪一格；这三条把 `.152` 真机那个坑直接写进用例名里。 */
    CHECK(ga_freeze_verdict(0, 0, 0, 0) == 0,
          "读不到进程状态时必须按「已冻」处理，**不许**回落 LncUtil —— "
          ".152 真机就栽在这一格（游戏被挂起、跳回主屏、切不回来）");
    CHECK(ga_freeze_verdict(1, 0, 0, 0) == 0,
          "对照样本没推进（进程本来就没烧 CPU）也算「无法验证」，同样不许回落");
    CHECK(ga_freeze_verdict(1, 1, 1, 0) == 1, "对照推进、冻结后不推进 = 确认冻住");
    CHECK(ga_freeze_verdict(1, 1, 1, 1) == -1,
          "只有「冻结后 CPU 还在涨」才算没冻住（唯一允许回落的输入）");
}

int main(void) {
    snprintf(g_http_root, sizeof(g_http_root), "%s/http", ROOT);
    if (stub_http_start(g_http_root) != 0) {
        fprintf(stderr, "HTTP 桩起不来\n");
        return 1;
    }
    g_http_port = stub_http_port();

    t_plan();
    t_sync();
    t_log();
    t_freeze();
    t_verdict();
    t_pull_gate();
    t_pull_gate_e2e();

    stub_http_stop();
    printf("game_auto: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
