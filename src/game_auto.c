/* [CN-FORK] v0.5.2.142 自动存档执行引擎。设计（为什么用 LncUtil 而不是挂载点、
 * 三个刻意为之的设计）见 include/game_auto.h 的文件头。 */

#include "game_auto.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "config.h" /* [CN-FORK] v0.5.2.145 读全局存档配置（设置页「存档配置」） */
#include "game_saves.h"
#include "notification.h" /* [CN-FORK] v0.5.2.147 推 / 拉的「开始 / 结束」屏幕通知 */
#include "pldmgr.h"
#include "save_remote.h"
#include "utils.h"

/* ── 运行中游戏的探测 ───────────────────────────────────────────────────── */
/* 这两个符号只有真机的 SDK 里有（ps5_launcher.c 已经在用）。宿主测试用
 * -DGA_NO_LNCUTIL 把默认探测换成「永远没有游戏在跑」，再自己装一个探针。 */
#ifndef GA_NO_LNCUTIL
extern int sceLncUtilGetAppIdOfRunningBigApp(void);
extern int sceLncUtilGetAppTitleId(unsigned int app_id, char *title_id);
#endif

static int ga_probe_default(char *out, size_t osz) {
    if (out && osz) out[0] = 0;
#ifdef GA_NO_LNCUTIL
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

/* ── 纯函数：状态机 ─────────────────────────────────────────────────────── */
int game_auto_plan(const char *prev, const char *cur,
                   int prev_enabled, int prev_on_exit,
                   int cur_enabled, int cur_on_launch,
                   ga_step_t out[2]) {
    if (!out) return 0;
    const char *p = (prev && prev[0]) ? prev : NULL;
    const char *c = (cur && cur[0]) ? cur : NULL;
    /* 同一款游戏连着两轮都在跑 —— 什么都不做。这是绝大多数轮次的情况。 */
    if (p && c && !strcmp(p, c)) return 0;

    int n = 0;
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
                  const char *src) {
    const char *an = (act == GA_ACT_PUSH) ? "推送" : "拉取";

    /* 只读一次配置：既是 inner 的输入，也让「开始」这行带得上目标。
     * 读不到就传 NULL，inner 自己会填那句人话错误。 */
    ga_cfg_t cfg;
    int have = (ga_load_cfg(tid, &cfg) == 0);
    char tgt[400];
    ga_target_desc(have ? &cfg.rc : NULL, tgt, sizeof(tgt));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    const char *scope = (only_uid && only_uid[0]) ? only_uid : "全部用户";

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

    pldmgr_log("[PLDMGR] saves: 远程%s 结束 · %s · 成功 %d / 跳过 %d / 失败 %d · "
               "%s · 用时 %.0f ms%s%s\n",
               an, tid, res->ok, res->skipped, res->failed, qty, ms,
               res->first_err[0] ? " · " : "", res->first_err);
    /* 屏幕上那一条。失败原因**另起一行**打 —— 通知是给人扫一眼的，
     * 混在计数那一行里最容易被当成背景噪声划过去。 */
    pldmgr_notify("远程%s 结束\n%s · 成功 %d / 跳过 %d / 失败 %d\n%s · 用时 %.0f ms%s%s",
                  an, tid, res->ok, res->skipped, res->failed, qty, ms,
                  res->first_err[0] ? "\n" : "", res->first_err);
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

    ga_res_t res;
    ga_do(tid, act, uid, &res, "手动");
    pthread_mutex_lock(&g_ga_mu);
    ga_mark(tid, act, res.ok > 0 || (res.failed == 0), res.first_err);
    pthread_mutex_unlock(&g_ga_mu);

    if (res.ok == 0 && res.failed > 0) {
        if (msg) snprintf(msg, msgsz, "%s", res.first_err[0] ? res.first_err : "同步失败");
        return -1;
    }

    const char *an = act == GA_ACT_PUSH ? "push" : "pull";
    char note[240];
    if (res.ok == 0 && res.skipped > 0)
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
#define GA_TICK 2        /* 扫描周期：游戏起没起，2 秒一轮（用户口径 —— 启动后要尽快拉取） */
#define GA_EXIT_SETTLE 5 /* 退出后再等这么久才推，给主机收尾写盘的时间 */
#define GA_THREAD_STACK (1u << 20)

static void ga_tick(void) {
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

    ga_cfg_t pc, cc;
    int pe = 0, px = 0, ce = 0, cl = 0;
    if (prev[0] && ga_load_cfg(prev, &pc) == 0) { pe = pc.enabled; px = pc.on_exit; }
    if (cur[0] && ga_load_cfg(cur, &cc) == 0) { ce = cc.enabled; cl = cc.on_launch; }

    ga_step_t steps[2];
    int ns = game_auto_plan(prev, cur, pe, px, ce, cl, steps);
    for (int i = 0; i < ns; i++) {
        /* 退出后先等几秒（见设计 ②）。只对推送等 —— 拉取发生在游戏刚起来时，
         * 那时候存档目录还没被写过，没有「拷到半截」的问题。 */
        if (steps[i].act == GA_ACT_PUSH) sleep(GA_EXIT_SETTLE);
        ga_res_t res;
        ga_do(steps[i].tid, steps[i].act, NULL, &res,
              steps[i].act == GA_ACT_PUSH ? "自动（退出游戏时）" : "自动（启动游戏时）");
        pthread_mutex_lock(&g_ga_mu);
        ga_mark(steps[i].tid, steps[i].act, res.ok > 0 || (res.failed == 0), res.first_err);
        pthread_mutex_unlock(&g_ga_mu);
    }
}

static void *ga_main(void *arg) {
    (void)arg;
    sleep(GA_BOOT_DELAY);
    for (;;) {
        if (!pldmgr_utils_safe_mode()) ga_tick();
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
