/* [CN-FORK] v0.5.2.116 后台任务槽（全局单槽，打包与删除共用）
 * 从 game_media.c 原样搬出来的 `.115` 实现，只做了三件事：
 *   ① 槽里加了 kind —— 没有它，删除任务起来后 /game_job_status 就分不清在报谁的进度；
 *   ② 补上 game_media.c 里漏掉的那次 `pldmgr_json_escape(g_job.reason, …)` ——
 *      原版声明了 rs[160]、写了「reason 也必须转义」的注释，**却没真正调用**，
 *      于是 reason 字段一直是栈上未初始化内存：大多数时候恰好是 0 所以看不出来，
 *      但只要栈里残留一个引号，整个响应体就不是合法 JSON，前端 .json() 直接抛；
 *   ③ 字段尺寸放宽（error 560 / reason 160 / phase 40 / aux 700），够两轮共用。 */

#include "game_job.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "json_helpers.h"
#include "utils.h" /* pldmgr_json_escape */

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static gj_state_t g_job;
static long long g_last_id;
/* [CN-FORK] v0.5.2.122 「最后一次有进展」的时刻。
 * 用户报的是「打包一直卡在这个页面」—— 而一个**不动的**进度条本身不说明任何事：
 * 分不清「正在啃一个 500 MB 的录屏」和「线程早死了」。把「距上次进展多少秒」
 * 交给前端，界面就能从「正在打包…」变成「已 45 秒无进展」，
 * 用户和排查的人都不用再猜。写它的地方只有四处：领槽、报总量、+字节、+文件。 */
static time_t g_last_progress;

void gj_lock(void) { pthread_mutex_lock(&g_mu); }
void gj_unlock(void) { pthread_mutex_unlock(&g_mu); }
gj_state_t *gj_state(void) { return &g_job; }

int gj_begin(int kind, long long *out_id, char *msg, size_t msgsz) {
    pthread_mutex_lock(&g_mu);
    if (g_job.active) {
        pthread_mutex_unlock(&g_mu);
        if (msg) snprintf(msg, msgsz, "已有任务在进行");
        return -1;
    }
    /* id 用时间戳起步：同一秒内连续领槽时 +1，保证严格单调 ——
     * 前端拿 id 当唯一标识，两个任务撞同一个 id 会让轮询串到别人的状态上。 */
    long long id = (long long)time(NULL);
    if (id <= g_last_id) id = g_last_id + 1;
    g_last_id = id;

    memset(&g_job, 0, sizeof(g_job));
    g_job.id = id;
    g_job.kind = kind;
    g_job.active = 1;
    g_job.done = 0;
    g_job.ok = 0;
    g_job.cancel = 0;
    g_job.phase[0] = 0;
    g_job.error[0] = 0;
    g_job.reason[0] = 0;
    g_job.started = time(NULL);
    g_last_progress = g_job.started;

    if (out_id) *out_id = id;
    return 0; /* 仍持锁 */
}

void gj_set_phase(const char *ph) {
    pthread_mutex_lock(&g_mu);
    snprintf(g_job.phase, sizeof(g_job.phase), "%s", ph ? ph : "");
    g_last_progress = time(NULL);
    pthread_mutex_unlock(&g_mu);
}

void gj_fail(const char *reason, const char *msg) {
    pthread_mutex_lock(&g_mu);
    snprintf(g_job.phase, sizeof(g_job.phase), "%s", reason ? reason : "failed");
    snprintf(g_job.error, sizeof(g_job.error), "%s", msg ? msg : "");
    snprintf(g_job.reason, sizeof(g_job.reason), "%s", reason ? reason : "failed");
    g_job.ok = 0;
    g_job.done = 1;
    g_job.active = 0;
    g_job.cancel = 0;
    pthread_mutex_unlock(&g_mu);
}

void gj_ok_done(void) {
    pthread_mutex_lock(&g_mu);
    snprintf(g_job.phase, sizeof(g_job.phase), "done");
    g_job.ok = 1;
    g_job.done = 1;
    g_job.active = 0;
    g_job.error[0] = 0;
    g_job.reason[0] = 0;
    if (g_job.bytes_total > 0) g_job.bytes_done = g_job.bytes_total;
    if (g_job.files_total > 0) g_job.files_done = g_job.files_total;
    pthread_mutex_unlock(&g_mu);
}

void gj_set_totals(long long bytes, long long files) {
    pthread_mutex_lock(&g_mu);
    g_job.bytes_total = bytes;
    g_job.files_total = files;
    g_last_progress = time(NULL);
    pthread_mutex_unlock(&g_mu);
}

int gj_cancelled(void) {
    pthread_mutex_lock(&g_mu);
    int c = g_job.cancel;
    pthread_mutex_unlock(&g_mu);
    return c;
}

void gj_add_bytes(long long n) {
    pthread_mutex_lock(&g_mu);
    g_job.bytes_done += n;
    g_last_progress = time(NULL);
    pthread_mutex_unlock(&g_mu);
}

void gj_add_file(void) {
    pthread_mutex_lock(&g_mu);
    g_job.files_done++;
    g_last_progress = time(NULL);
    pthread_mutex_unlock(&g_mu);
}

void gj_set_error(const char *msg) {
    pthread_mutex_lock(&g_mu);
    snprintf(g_job.error, sizeof(g_job.error), "%s", msg ? msg : "");
    pthread_mutex_unlock(&g_mu);
}

int gj_status_json(long long id, char *buf, size_t size) {
    if (!buf || size < 64) return -1;
    pthread_mutex_lock(&g_mu);
    if (!g_job.id || id != g_job.id) {
        pthread_mutex_unlock(&g_mu);
        return -1;
    }
    double elapsed = g_job.started ? difftime(time(NULL), g_job.started) : 0.0;
    if (elapsed < 0) elapsed = 0;
    double percent = 0.0;
    if (g_job.bytes_total > 0) {
        percent = 100.0 * (double)g_job.bytes_done / (double)g_job.bytes_total;
    } else if (g_job.files_total > 0) {
        percent = 100.0 * (double)g_job.files_done / (double)g_job.files_total;
    } else if (g_job.done) {
        percent = 100.0;
    }
    if (percent > 100.0) percent = 100.0;
    if (g_job.done && g_job.ok) percent = 100.0;
    double rate = elapsed > 0.5 ? (double)g_job.bytes_done / elapsed : 0.0;

    /* 「距上次有进展」多少秒。只在任务还活着时有意义：跑完/失败之后这个数字
     * 只会一直涨，前端拿它当「是不是卡住了」的判据会被自己误导。 */
    long idle = 0;
    if (g_job.active && g_last_progress) {
        double d = difftime(time(NULL), g_last_progress);
        if (d > 0) idle = (long)d;
    }

    /* 三个自由文本字段全部转义。reason 和 error 是同一条链上（gj_fail）传下来的，
     * 只转 error 不转 reason 等于给自己留一个「某条失败原因里出现引号就整个
     * 响应体变成非法 JSON」的洞 —— 这正是 .115 原版漏掉的那个。 */
    char eerr[560], ph[40], rs[160], nm[640];
    pldmgr_json_escape(g_job.error, eerr, sizeof(eerr));
    pldmgr_json_escape(g_job.phase, ph, sizeof(ph));
    pldmgr_json_escape(g_job.reason, rs, sizeof(rs));
    /* [CN-FORK] v0.5.2.122 成品 zip 的文件名（只有打包任务有；删除任务留空）。
     * 前端 `<a download>` 拿它当 download 属性，服务端 Content-Disposition 也用它 ——
     * 两处同源，界面上写什么名字、落盘就叫什么名字。 */
    nm[0] = 0;
    if (g_job.kind == GJ_KIND_PACK && g_job.aux[0]) {
        const char *sl = strrchr(g_job.aux, '/');
        pldmgr_json_escape(sl ? sl + 1 : g_job.aux, nm, sizeof(nm));
    }

    int n = snprintf(buf, size,
                     "{\"job_id\":%lld,\"name\":\"%s\",\"active\":%s,\"phase\":\"%s\",\"percent\":%.1f,"
                     "\"bytes_done\":%lld,\"bytes_total\":%lld,"
                     "\"files_done\":%lld,\"files_total\":%lld,"
                     "\"rate\":%.0f,\"elapsed\":%.1f,\"idle\":%ld,"
                     "\"done\":%s,\"ok\":%s,\"error\":\"%s\",\"reason\":\"%s\"}",
                     g_job.id, nm,
                     g_job.active ? "true" : "false",
                     ph, percent,
                     g_job.bytes_done, g_job.bytes_total,
                     g_job.files_done, g_job.files_total,
                     rate, elapsed, idle,
                     g_job.done ? "true" : "false",
                     g_job.ok ? "true" : "false",
                     eerr, rs);
    pthread_mutex_unlock(&g_mu);
    if (n < 0 || (size_t)n >= size) return -1;
    return n;
}

int gj_cancel(long long id, char *msg, size_t msgsz) {
    pthread_mutex_lock(&g_mu);
    if (!g_job.id || id != g_job.id) {
        pthread_mutex_unlock(&g_mu);
        if (msg) snprintf(msg, msgsz, "任务不存在或已过期");
        return -1;
    }
    if (g_job.active) g_job.cancel = 1;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int gj_aux(long long id, int kind, char *out, size_t osz) {
    if (!out || !osz) return -1;
    pthread_mutex_lock(&g_mu);
    int ok = (g_job.id && id == g_job.id && g_job.kind == kind);
    if (ok) snprintf(out, osz, "%s", g_job.aux);
    pthread_mutex_unlock(&g_mu);
    return ok ? 0 : -1;
}

int gj_kind_now(void) {
    pthread_mutex_lock(&g_mu);
    int k = g_job.active ? g_job.kind : -1;
    pthread_mutex_unlock(&g_mu);
    return k;
}

/* ── CRC32 ────────────────────────────────────────────────────────────────
 * 只有一份实现（原在 game_media.c 里），打包与存档共用，避免两份表不同步。 */
static uint32_t g_crc_table[256];
static int g_crc_ready;

void gj_crc_init(void) {
    if (g_crc_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[i] = c;
    }
    g_crc_ready = 1;
}

uint32_t gj_crc32(uint32_t c, const unsigned char *b, size_t n) {
    if (!g_crc_ready) gj_crc_init();
    c ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = g_crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
