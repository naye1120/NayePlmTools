/* [CN-FORK] v0.5.2.116 存档后端。契约与「存档到底落在哪」见 include/game_saves.h。 */

#include "game_saves.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "game_mgr.h"
#include "inflate.h"
#include "json_helpers.h"
#include "pldmgr.h"
#include "utils.h"
#include "zip_io.h"

/* 存档根的父目录。做成 -D 可覆盖是为了主机单测（WSL 下根本没有 /user/home），
 * 与 GAME_SAVES_AUTO_PATH / GAME_SAVES_PACK_DIR 同一套写法。 */
#ifndef GS_HOME
#define GS_HOME "/user/home"
#endif
#define GS_MAX_DEPTH 16
#define GS_MAX_WALK 200000u

/* ── 基本校验 ──────────────────────────────────────────────────────────────
 * tid 与槽名都要过这一关：拿不到 '/' 也拿不到 '.'，路径穿越在函数入口就被挡死。 */
static int gs_tid_ok(const char *tid) {
    if (!tid) return 0;
    size_t n = strlen(tid);
    if (n < 6 || n > 16) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = tid[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

/* 槽名就是存档目录的目录名，所以规则要和目录名一致 —— 非空、不以 '.' 开头。
 * 允许点开头等于能匹配到我们自己写在同一个父目录里的 .upl_* 半成品。 */
static int gs_slot_ok(const char *s) {
    if (!s || !*s || *s == '.') return 0;
    size_t n = strlen(s);
    if (n > 128) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '/' || c == '\\' || c < 0x20 || c == 0x7F) return 0;
    }
    return 1;
}

/* ── 递归统计（体积 / 文件数），带深度与条目预算 ──────────────────────────
 * 用 lstat 而不是 stat：存档里出现符号链接环时 stat 会跟着走进去，把整个请求拖死。 */
typedef struct { long long bytes; long long files; int over; } gs_stat_t;

static void gs_walk(const char *dir, gs_stat_t *acc, int depth, unsigned *budget) {
    if (depth > GS_MAX_DEPTH || *budget == 0 || acc->over) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[1024];
    while ((e = readdir(d)) != NULL && *budget > 0 && !acc->over) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        (*budget)--;
        if (snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) >= (int)sizeof(p)) continue;
        struct stat st;
        if (lstat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            gs_walk(p, acc, depth + 1, budget);
        } else if (S_ISREG(st.st_mode)) {
            acc->bytes += (long long)st.st_size;
            acc->files++;
        }
    }
    if (*budget == 0) acc->over = 1;
    closedir(d);
}

/* 递归删除。**只按 lstat 走**，符号链接直接 unlink，绝不跟着它进去。 */
static void gs_rmtree(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) { unlink(dir); return; }
    struct dirent *e;
    char p[1024];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) >= (int)sizeof(p)) continue;
        struct stat st;
        if (lstat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) gs_rmtree(p);
        else unlink(p);
    }
    closedir(d);
    rmdir(dir);
}

/* ── 存档根目录 ─────────────────────────────────────────────────────────── */
static int gs_user_homes(char homes[][256], int max) {
    DIR *d = opendir(GS_HOME);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.' || e->d_name[0] == 0) continue;
        char p[512];
        if (snprintf(p, sizeof(p), "%s/%s", GS_HOME, e->d_name) >= (int)sizeof(p)) continue;
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) snprintf(homes[n++], 256, "%s", p);
    }
    closedir(d);
    return n;
}

int game_saves_root(const char *tid, char *out, size_t osz) {
    if (!gs_tid_ok(tid) || !out || osz < 64) return -1;
    char homes[16][256];
    int nh = gs_user_homes(homes, 16);

    char best[640];
    long long bestsz = -1;
    for (int i = 0; i < nh; i++) {
        char p[640];
        if (snprintf(p, sizeof(p), "%s/savedata/%s", homes[i], tid) >= (int)sizeof(p)) continue;
        struct stat st;
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        gs_stat_t acc = {0, 0, 0};
        unsigned budget = GS_MAX_WALK;
        gs_walk(p, &acc, 0, &budget);
        if (acc.bytes > bestsz) { bestsz = acc.bytes; snprintf(best, sizeof(best), "%s", p); }
    }
    if (bestsz < 0) return -1;
    snprintf(out, osz, "%s", best);
    return 0;
}

/* 上传要往里写，所以连「一个存档都还没有」时也得把根目录定出来：取第一个用户目录。 */
static int gs_root_or_create(const char *tid, char *out, size_t osz) {
    if (game_saves_root(tid, out, osz) == 0) return 0;
    if (!gs_tid_ok(tid)) return -1;
    char homes[16][256];
    int nh = gs_user_homes(homes, 16);
    if (nh <= 0) return -1;
    snprintf(out, osz, "%s/savedata/%s", homes[0], tid);
    return ensure_dir_recursive(out) == 0 ? 0 : -1;
}

/* ── 列表 ───────────────────────────────────────────────────────────────── */
static const char *gs_kind(const char *tid) {
    /* 与 mock 同一套口径：CUSA/PPJB 是 PS4 本体，其余按 PS5 算。 */
    return (!strncmp(tid, "CUSA", 4) || !strncmp(tid, "PPJB", 4)) ? "PS4" : "PS5";
}

static void gs_fmt_mtime(time_t t, char *out, size_t osz) {
    struct tm tmv;
    out[0] = 0;
    if (t <= 0 || !localtime_r(&t, &tmv)) return;
    strftime(out, osz, "%Y-%m-%d %H:%M", &tmv);
}

/* 槽显示名：优先 SFO 的 SAVEDATA_TITLE（PS5 布局），再 TITLE，都没有就用目录名。
 * PS4 那种 sdimg_<TID> 单文件没有 SFO，直接落到目录名 —— 不去猜
 * 「单文件一定对应哪种平台」这种没有依据的规则。 */
static void gs_slot_name(const char *path, int is_dir, const char *fallback,
                         char *out, size_t osz) {
    snprintf(out, osz, "%s", fallback);
    if (!is_dir) return;
    char sfo[1100];
    static const char *const keys[2] = {"SAVEDATA_TITLE", "TITLE"};
    for (int i = 0; i < 2; i++) {
        if (snprintf(sfo, sizeof(sfo), "%s/sce_sys/param.sfo", path) < (int)sizeof(sfo) &&
            game_mgr_sfo_text(sfo, keys[i], out, osz) == 0 && out[0])
            return;
        if (snprintf(sfo, sizeof(sfo), "%s/param.sfo", path) < (int)sizeof(sfo) &&
            game_mgr_sfo_text(sfo, keys[i], out, osz) == 0 && out[0])
            return;
    }
    snprintf(out, osz, "%s", fallback);
}

int game_saves_count(const char *tid) {
    char root[640];
    if (game_saves_root(tid, root, sizeof(root)) != 0) return 0;
    DIR *d = opendir(root);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (gs_slot_ok(e->d_name)) n++;
    }
    closedir(d);
    return n;
}

/* ── 自动备份配置 ───────────────────────────────────────────────────────── */
static pthread_mutex_t g_auto_mu = PTHREAD_MUTEX_INITIALIZER;

static cJSON *gs_auto_load(void) {
    char *txt = NULL;
    size_t sz = 0;
    cJSON *root = NULL;
    if (read_file_text(GAME_SAVES_AUTO_PATH, &txt, &sz) == 0 && txt)
        root = cJSON_Parse(txt);
    free(txt);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    return root;
}

static int gs_auto_save(cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    if (!s) return -1;
    int rc = write_file_text(GAME_SAVES_AUTO_PATH, s, strlen(s));
    if (rc != 0) pldmgr_log("[PLDMGR] saves: 写自动备份配置失败 (%s)\n", strerror(errno));
    free(s);
    return rc;
}

/* 默认值。**字段集合必须与 gs_auto_json 吐出去的那份一致**，否则前端读到 undefined。 */
static cJSON *gs_auto_defaults(void) {
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddBoolToObject(o, "enabled", 0);
    cJSON_AddStringToObject(o, "mode", "smb");
    cJSON_AddBoolToObject(o, "on_launch", 1);
    cJSON_AddBoolToObject(o, "on_exit", 1);
    cJSON *smb = cJSON_AddObjectToObject(o, "smb");
    if (smb) {
        cJSON_AddStringToObject(smb, "host", "");
        cJSON_AddStringToObject(smb, "share", "");
        cJSON_AddStringToObject(smb, "sub", "");
        cJSON_AddStringToObject(smb, "user", "");
        cJSON_AddStringToObject(smb, "password", "");
    }
    cJSON *ftp = cJSON_AddObjectToObject(o, "ftp");
    if (ftp) {
        cJSON_AddStringToObject(ftp, "host", "");
        cJSON_AddNumberToObject(ftp, "port", 21);
        cJSON_AddStringToObject(ftp, "path", "");
        cJSON_AddStringToObject(ftp, "user", "");
        cJSON_AddStringToObject(ftp, "password", "");
    }
    cJSON *http = cJSON_AddObjectToObject(o, "http");
    if (http) {
        cJSON_AddStringToObject(http, "url", "");
        cJSON_AddStringToObject(http, "token", "");
    }
    return o;
}

/* 取字段（越界一律回落到默认），避免整条链上到处判 NULL。 */
static const char *gs_field_str(cJSON *o, const char *k, const char *def) {
    cJSON *f = o ? cJSON_GetObjectItem(o, k) : NULL;
    if (f && cJSON_IsString(f) && f->valuestring) return f->valuestring;
    return def ? def : "";
}
/* 端口字段：JSON 数字和 "2121" 这种**数字字符串都认**。
 *
 * 前端 SaveManagerModal 那个 <input> 的 onChange 存的是 `e.target.value` ——
 * 出来**永远是字符串**，`inputMode="numeric"` 只是手机键盘提示，不做类型转换。
 * 所以请求体里就是 `"port":"2121"` 而不是 `"port":2121`。原先只认
 * cJSON_IsNumber，于是：端口永远存不进配置文件（读回来还是默认 21）、
 * 「测试连接」也每次打到 21 —— 用户填的 2121 等于没填，还会以为 FTP 坏了。
 * mock 那边是整段 raw 合并、不挑类型，所以这个问题只在真后端上暴露。 */
static int gs_get_port(cJSON *f, int def) {
    if (f && cJSON_IsNumber(f)) {
        double v = f->valuedouble;
        return (v > 0 && v < 65536) ? (int)v : def;
    }
    if (f && cJSON_IsString(f) && f->valuestring && f->valuestring[0]) {
        char *e = NULL;
        double v = strtod(f->valuestring, &e);
        if (e && e != f->valuestring && *e == 0) return (v > 0 && v < 65536) ? (int)v : def;
    }
    return def;
}

/* 本文件里只有 port 一处用它，所以直接并到同一条兼容逻辑上。 */
static double gs_field_num(cJSON *o, const char *k, double def) {
    return (double)gs_get_port(o ? cJSON_GetObjectItem(o, k) : NULL, (int)def);
}
static int gs_field_bool(cJSON *o, const char *k, int def) {
    cJSON *f = o ? cJSON_GetObjectItem(o, k) : NULL;
    if (!f) return def;
    return cJSON_IsTrue(f) ? 1 : (cJSON_IsBool(f) ? 0 : def);
}

/* 覆盖式写字段：先删再加，不就地改 valuestring —— 就地改要么漏 free、
 * 要么把 cJSON 的类型位（bool 是靠 type 标志区分的）改坏，两种都会在后续打印时炸。 */
static void gs_put_str(cJSON *o, const char *k, const char *v) {
    if (!o) return;
    cJSON_DeleteItemFromObject(o, k);
    cJSON_AddStringToObject(o, k, v);
}
static void gs_put_num(cJSON *o, const char *k, double v) {
    if (!o) return;
    cJSON_DeleteItemFromObject(o, k);
    cJSON_AddNumberToObject(o, k, v);
}
static void gs_put_bool(cJSON *o, const char *k, int v) {
    if (!o) return;
    cJSON_DeleteItemFromObject(o, k);
    cJSON_AddBoolToObject(o, k, v ? 1 : 0);
}

/* 存一份该 tid 的配置。结构与 /game_saves 的 auto 同形，只是**多存两个明文字段**
 * （smb.password / http.token）—— 保存就得存下来，否则下次「测试连接」连不上；
 * 但对外只吐 has_password / token_set（见 gs_auto_json），明文永远不出这个文件。 */
static void gs_auto_json(cJSON *src, char *buf, size_t size) {
    buf[0] = 0;
    cJSON *o = cJSON_CreateObject();
    if (!o) return;

    gs_put_bool(o, "enabled", gs_field_bool(src, "enabled", 0));
    gs_put_str(o, "mode", gs_field_str(src, "mode", "smb"));
    gs_put_bool(o, "on_launch", gs_field_bool(src, "on_launch", 1));
    gs_put_bool(o, "on_exit", gs_field_bool(src, "on_exit", 1));

    cJSON *ss = src ? cJSON_GetObjectItem(src, "smb") : NULL;
    cJSON *smb = cJSON_AddObjectToObject(o, "smb");
    if (smb) {
        gs_put_str(smb, "host", gs_field_str(ss, "host", ""));
        gs_put_str(smb, "share", gs_field_str(ss, "share", ""));
        gs_put_str(smb, "sub", gs_field_str(ss, "sub", ""));
        gs_put_str(smb, "user", gs_field_str(ss, "user", ""));
        gs_put_bool(smb, "has_password", gs_field_str(ss, "password", "")[0] != 0);
    }

    cJSON *fs = src ? cJSON_GetObjectItem(src, "ftp") : NULL;
    cJSON *ftp = cJSON_AddObjectToObject(o, "ftp");
    if (ftp) {
        gs_put_str(ftp, "host", gs_field_str(fs, "host", ""));
        gs_put_num(ftp, "port", gs_field_num(fs, "port", 21));
        gs_put_str(ftp, "path", gs_field_str(fs, "path", ""));
        gs_put_str(ftp, "user", gs_field_str(fs, "user", ""));
        gs_put_bool(ftp, "has_password", gs_field_str(fs, "password", "")[0] != 0);
    }

    cJSON *hs = src ? cJSON_GetObjectItem(src, "http") : NULL;
    cJSON *http = cJSON_AddObjectToObject(o, "http");
    if (http) {
        int tok = gs_field_str(hs, "token", "")[0] != 0;
        gs_put_str(http, "url", gs_field_str(hs, "url", ""));
        gs_put_bool(http, "token_set", tok); /* 契约里写的是这个名（gameApi.js 文件头） */
        /* 但前端 SaveManagerModal 那句「已保存过；留空则保持不变」是按 `has_${f.key}`
         * 通用拼出来的（secret 字段 = smb/ftp 的 password、http 的 token），只给
         * token_set 它永远读不到 has_token，于是令牌栏的提示一直不出现 ——
         * smb/ftp 的密码都有、就这一栏没有，用户会以为令牌没存上。两个名一起给：
         * 契约不破，前端也不用改一行。 */
        gs_put_bool(http, "has_token", tok);
    }

    char *s = cJSON_PrintUnformatted(o);
    if (s) snprintf(buf, size, "%s", s);
    free(s);
    cJSON_Delete(o);
}

/* 追加一小段到缓冲，**永远按「剩余容量」写并检查返回值**。
 * 直接 `pos += snprintf(...)` 是这类代码的经典坑：snprintf 返回的是「本来要写多长」，
 * 缓冲一旦截断 pos 就会跳到数组外面，下一次写就是越界。 */
static int gs_app(char *b, size_t cap, size_t *pos, const char *fmt, ...) {
    if (*pos >= cap) return -1;
    va_list ap;
    va_start(ap, fmt);
    size_t rem = cap - *pos;
    int w = vsnprintf(b + *pos, rem, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= rem) { b[*pos] = 0; return -1; }
    *pos += (size_t)w;
    return 0;
}

int game_saves_list_json(const char *tid, char *buf, size_t size) {
    if (!gs_tid_ok(tid) || !buf || size < 128) return -1;

    char etid[32], eroot[700];
    pldmgr_json_escape(tid, etid, sizeof(etid));
    char root[640];
    root[0] = 0;
    int has = (game_saves_root(tid, root, sizeof(root)) == 0);
    pldmgr_json_escape(has ? root : "", eroot, sizeof(eroot));

    char autoj[4096];
    pthread_mutex_lock(&g_auto_mu);
    cJSON *all = gs_auto_load();
    cJSON *cur = all ? cJSON_GetObjectItem(all, tid) : NULL;
    cJSON *fallback = NULL;
    if (!cur || !cJSON_IsObject(cur)) {
        fallback = gs_auto_defaults();
        cur = fallback;
    }
    gs_auto_json(cur, autoj, sizeof(autoj));
    cJSON_Delete(fallback);
    cJSON_Delete(all);
    pthread_mutex_unlock(&g_auto_mu);
    if (!autoj[0]) snprintf(autoj, sizeof(autoj), "{}");

    /* saves 单独走堆：512 条槽 × ~200B 就上百 KB，挂在栈上会把工作线程的栈顶穿。
     * 线程栈是 pthread 默认值（通常 512KB~1MB），放不下 128KB 的局部数组。 */
    const size_t scap = 128u * 1024u;
    char *saves = (char *)malloc(scap);
    if (!saves) return -1;
    size_t spos = 0;
    int n = 0;
    if (gs_app(saves, scap, &spos, "[") != 0) { free(saves); return -1; }

    if (has) {
        DIR *d = opendir(root);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL && n < 512) {
                if (e->d_name[0] == '.') continue; /* .upl_* 临时目录不进列表 */
                if (!gs_slot_ok(e->d_name)) continue;
                char p[700];
                if (snprintf(p, sizeof(p), "%s/%s", root, e->d_name) >= (int)sizeof(p)) continue;
                struct stat st;
                if (lstat(p, &st) != 0) continue;
                int isdir = S_ISDIR(st.st_mode);
                if (!isdir && !S_ISREG(st.st_mode)) continue;

                gs_stat_t acc = {0, 0, 0};
                unsigned budget = GS_MAX_WALK;
                if (isdir) gs_walk(p, &acc, 0, &budget);
                else { acc.bytes = (long long)st.st_size; acc.files = 1; }

                char name[256], en[600], mt[40];
                gs_slot_name(p, isdir, e->d_name, name, sizeof(name));
                pldmgr_json_escape(name, en, sizeof(en));
                gs_fmt_mtime(st.st_mtime, mt, sizeof(mt));

                if (gs_app(saves, scap, &spos,
                           "%s{\"id\":\"%s\",\"name\":\"%s\",\"kind\":\"%s\","
                           "\"mtime\":\"%s\",\"size\":%lld,\"files\":%lld}",
                           n ? "," : "", e->d_name, en, gs_kind(tid), mt,
                           acc.bytes, acc.files) != 0)
                    break;
                n++;
            }
            closedir(d);
        }
    }
    gs_app(saves, scap, &spos, "]");

    int w = snprintf(buf, size,
                     "{\"tid\":\"%s\",\"local_path\":\"%s\",\"auto\":%s,\"saves\":%s}",
                     etid, eroot, autoj, saves);
    free(saves);
    if (w < 0 || (size_t)w >= size) return -1;
    return w;
}

/* ── POST /game_save_auto ───────────────────────────────────────────────── */
int game_saves_auto_set(const char *tid, const char *body, size_t len,
                        char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!gs_tid_ok(tid)) { if (msg) snprintf(msg, msgsz, "标题 ID 不合法"); return -1; }
    if (!body || len == 0) { if (msg) snprintf(msg, msgsz, "请求体为空"); return -1; }

    cJSON *b = cJSON_ParseWithLength((const char *)body, len);
    if (!b || !cJSON_IsObject(b)) {
        cJSON_Delete(b);
        if (msg) snprintf(msg, msgsz, "JSON 解析失败");
        return -1;
    }

    int rc = -1;
    pthread_mutex_lock(&g_auto_mu);
    cJSON *all = gs_auto_load();
    if (!all) { pthread_mutex_unlock(&g_auto_mu); cJSON_Delete(b); return -1; }
    cJSON *cur = cJSON_GetObjectItem(all, tid);
    if (!cur || !cJSON_IsObject(cur)) {
        cur = gs_auto_defaults();
        if (cur) cJSON_AddItemToObject(all, tid, cur);
    }

    /* 三个子对象逐字段合并：**没传的保持原值**。密码/token 是「留空即不修改」
     * —— 键没出现就保持，出现了且非空就覆盖，出现了但是空串就清掉。 */
    static const char *const modes[3] = {"smb", "ftp", "http"};
    for (int i = 0; i < 3; i++) {
        cJSON *in = cJSON_GetObjectItem(b, modes[i]);
        if (!in || !cJSON_IsObject(in)) continue;
        cJSON *dst = cJSON_GetObjectItem(cur, modes[i]);
        if (!dst || !cJSON_IsObject(dst)) {
            dst = cJSON_CreateObject();
            if (dst) cJSON_AddItemToObject(cur, modes[i], dst);
        }
        if (!dst) continue;
        static const char *const fields[] = {"host", "share", "sub", "user", "path", "url"};
        for (size_t k = 0; k < sizeof(fields) / sizeof(fields[0]); k++) {
            cJSON *f = cJSON_GetObjectItem(in, fields[k]);
            if (f && cJSON_IsString(f)) gs_put_str(dst, fields[k], f->valuestring);
        }
        /* 空串/缺键 → 不动（留空即不修改）；填了才覆盖，数字字符串照样认。 */
        int prt = gs_get_port(cJSON_GetObjectItem(in, "port"), -1);
        if (prt > 0) gs_put_num(dst, "port", (double)prt);
        /* 这两个用 pf 不用 f：f 是下面那个 cJSON *f 的，被我的 port 那行提前用掉了。 */
        cJSON *pf = cJSON_GetObjectItem(in, "password");
        if (pf && cJSON_IsString(pf)) gs_put_str(dst, "password", pf->valuestring);
        pf = cJSON_GetObjectItem(in, "token");
        if (pf && cJSON_IsString(pf)) gs_put_str(dst, "token", pf->valuestring);
    }

    cJSON *f;
    if ((f = cJSON_GetObjectItem(b, "enabled"))) gs_put_bool(cur, "enabled", cJSON_IsTrue(f));
    if ((f = cJSON_GetObjectItem(b, "on_launch"))) gs_put_bool(cur, "on_launch", cJSON_IsTrue(f));
    if ((f = cJSON_GetObjectItem(b, "on_exit"))) gs_put_bool(cur, "on_exit", cJSON_IsTrue(f));
    if ((f = cJSON_GetObjectItem(b, "mode")) && cJSON_IsString(f) && f->valuestring &&
        (!strcmp(f->valuestring, "smb") || !strcmp(f->valuestring, "ftp") ||
         !strcmp(f->valuestring, "http")))
        gs_put_str(cur, "mode", f->valuestring);

    rc = gs_auto_save(all);
    cJSON_Delete(all);
    pthread_mutex_unlock(&g_auto_mu);
    cJSON_Delete(b);

    if (rc != 0) {
        if (msg) snprintf(msg, msgsz, "保存失败（%s）", strerror(errno));
        return -1;
    }
    pldmgr_log("[PLDMGR] saves: %s 自动备份配置已更新\n", tid);
    return 0;
}

/* ── POST /game_save_test：真去连一次 ───────────────────────────────────── */
static int gs_connect(const char *host, int port, int timeout_ms, int *out_fd, int *out_ms) {
    *out_fd = -1;
    *out_ms = 0;
    if (!host || !*host) return -1;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    int fd = -1;
    for (struct addrinfo *ai = res; ai && fd < 0; ai = ai->ai_next) {
        int s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0) continue;
        int fl = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, fl | O_NONBLOCK);
        int rc = connect(s, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno != EINPROGRESS) { close(s); continue; }
        if (rc != 0) {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(s, &w);
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (long)(timeout_ms % 1000) * 1000;
            if (select(s + 1, NULL, &w, NULL, &tv) <= 0) { close(s); continue; }
            int err = 0;
            socklen_t el = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el);
            if (err) { close(s); continue; }
        }
        fcntl(s, F_SETFL, fl); /* 还原阻塞，后面还要读写 */
        fd = s;
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;
    gettimeofday(&t1, NULL);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;
    *out_ms = ms < 0 ? 0 : (int)ms;
    *out_fd = fd;
    return 0;
}

/* 读一行（带超时）。返回读到的长度。 */
static int gs_read_line(int fd, char *buf, int cap, int timeout_ms) {
    int n = 0;
    while (n < cap - 1) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(fd, &r);
        struct timeval t;
        t.tv_sec = timeout_ms / 1000;
        t.tv_usec = (long)(timeout_ms % 1000) * 1000;
        if (select(fd + 1, &r, NULL, NULL, &t) <= 0) break;
        char c;
        ssize_t k = read(fd, &c, 1);
        if (k <= 0) break;
        buf[n++] = c;
        if (c == '\n') break;
    }
    buf[n] = 0;
    return n;
}

/* 从 URL 抠 host / port：http://1.2.3.4:8080/x → 1.2.3.4:8080。
 * 没写端口按 scheme 给 80/443 —— 用户多半只填地址，逼人家非写端口很不友好。 */
static int gs_url_host(const char *url, char *host, size_t hsz, int *port) {
    if (!url || !*url) return -1;
    int def = 80;
    const char *p = strstr(url, "://");
    if (p) {
        if (!strncmp(url, "https://", 8)) def = 443;
        p += 3;
    } else {
        p = url;
    }
    const char *e = p;
    while (*e && *e != '/' && *e != ':' && *e != '?' && *e != '#') e++;
    if (e == p || (size_t)(e - p) >= hsz) return -1;
    memcpy(host, p, (size_t)(e - p));
    host[e - p] = 0;
    *port = def;
    if (*e == ':') {
        int v = atoi(e + 1);
        if (v > 0 && v < 65536) *port = v;
    }
    return 0;
}

int game_saves_test(const char *body, size_t len, char *buf, size_t size) {
    if (!buf || size < 64) return -1;
    cJSON *b = cJSON_ParseWithLength(body ? (const char *)body : "", len);
    if (!b) return -1;

    const char *mode = gs_field_str(b, "mode", "smb");
    char host[256];
    int port = 0;
    if (!strcmp(mode, "ftp")) {
        cJSON *o = cJSON_GetObjectItem(b, "ftp");
        snprintf(host, sizeof(host), "%s", gs_field_str(o, "host", ""));
        port = (int)gs_field_num(o, "port", 21);
    } else if (!strcmp(mode, "http")) {
        cJSON *o = cJSON_GetObjectItem(b, "http");
        char url[512];
        snprintf(url, sizeof(url), "%s", gs_field_str(o, "url", ""));
        if (gs_url_host(url, host, sizeof(host), &port) != 0) host[0] = 0;
    } else {
        mode = "smb";
        cJSON *o = cJSON_GetObjectItem(b, "smb");
        snprintf(host, sizeof(host), "%s", gs_field_str(o, "host", ""));
        port = 445;
    }
    cJSON_Delete(b);

    /* 与 mock 同一条失败入口：空地址、0.0.0.0 一律报连不上 —— 前端的失败样式靠它验。 */
    if (!host[0] || !strcmp(host, "0.0.0.0")) return -1;

    int fd = -1, ms = 0;
    if (gs_connect(host, port, 3000, &fd, &ms) != 0) return -1;

    /* 只看 TCP 通不通的话，端口填错（21 填成 22）照样绿，这个按钮就等于没测。
     * 所以再往下走一步：ftp 看 220 横幅、http 发个 HEAD 看状态行。 */
    char note[192];
    int ok = 1;
    snprintf(note, sizeof(note), "TCP 连接成功（%s:%d）", host, port);
    if (!strcmp(mode, "ftp")) {
        char line[256];
        if (gs_read_line(fd, line, sizeof(line), 3000) <= 0) {
            ok = 0;
            snprintf(note, sizeof(note), "连上了但没收到 FTP 横幅");
        } else if (strncmp(line, "220", 3) != 0) {
            ok = 0;
            snprintf(note, sizeof(note), "对端不是 FTP 服务");
        } else {
            snprintf(note, sizeof(note), "FTP 横幅正常");
        }
    } else if (!strcmp(mode, "http")) {
        char req[600];
        snprintf(req, sizeof(req),
                 "HEAD / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
        if (write(fd, req, strlen(req)) < 0) {
            ok = 0;
            snprintf(note, sizeof(note), "发不出请求");
        } else {
            char line[256];
            if (gs_read_line(fd, line, sizeof(line), 4000) <= 0) {
                ok = 0;
                snprintf(note, sizeof(note), "连上了但没回 HTTP 状态行");
            } else if (strncmp(line, "HTTP/1.", 7) != 0) {
                ok = 0;
                snprintf(note, sizeof(note), "对端不是 HTTP 服务");
            } else {
                char *sp = strchr(line, ' ');
                if (sp) {
                    char *nl = strpbrk(sp, "\r\n");
                    if (nl) *nl = 0;
                    snprintf(note, sizeof(note), "HTTP%s", sp);
                }
            }
        }
    }
    close(fd);
    if (!ok) return -1;

    char en[400];
    pldmgr_json_escape(note, en, sizeof(en));
    int w = snprintf(buf, size, "{\"ok\":true,\"latency_ms\":%d,\"note\":\"%s\"}", ms, en);
    return (w > 0 && (size_t)w < size) ? 0 : -1;
}

/* ── 下载：打包 ─────────────────────────────────────────────────────────── */
typedef struct {
    zip_ent_t *e;
    int n, cap, max;
    const char *prefix; /* zip 内的顶层目录 = 槽名 */
    int bad;
} gs_pack_t;

static int gs_pack_add(gs_pack_t *t, const char *abs, const char *rel) {
    if (t->n >= t->max) { t->bad = 1; return -1; }
    if (t->n == t->cap) {
        int nc = t->cap ? t->cap * 2 : 64;
        if (nc > t->max) nc = t->max;
        zip_ent_t *ne = (zip_ent_t *)realloc(t->e, (size_t)nc * sizeof(zip_ent_t));
        if (!ne) { t->bad = 1; return -1; }
        t->e = ne;
        t->cap = nc;
    }
    zip_ent_t *ent = &t->e[t->n++];
    memset(ent, 0, sizeof(*ent));
    snprintf(ent->abs, sizeof(ent->abs), "%s", abs);
    if (rel[0]) snprintf(ent->zname, sizeof(ent->zname), "%s/%s", t->prefix, rel);
    else snprintf(ent->zname, sizeof(ent->zname), "%s", t->prefix);
    struct stat sb;
    ent->size = (stat(abs, &sb) == 0) ? (long long)sb.st_size : 0;
    return 0;
}

static void gs_pack_walk(const char *dir, const char *rel, int depth, gs_pack_t *t) {
    if (depth > GS_MAX_DEPTH || t->bad) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[1024], r[512];
    while ((e = readdir(d)) != NULL && !t->bad) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) >= (int)sizeof(p)) { t->bad = 1; break; }
        if (snprintf(r, sizeof(r), "%s%s%s", rel, rel[0] ? "/" : "", e->d_name) >= (int)sizeof(r)) {
            t->bad = 1;
            break;
        }
        struct stat st;
        if (lstat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) gs_pack_walk(p, r, depth + 1, t);
        else if (S_ISREG(st.st_mode)) gs_pack_add(t, p, r);
    }
    closedir(d);
}

int game_saves_pack(const char *tid, const char *save, const char *out_path,
                    char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!gs_tid_ok(tid) || !gs_slot_ok(save)) {
        if (msg) snprintf(msg, msgsz, "参数不合法");
        return -1;
    }
    char root[640];
    if (game_saves_root(tid, root, sizeof(root)) != 0) {
        if (msg) snprintf(msg, msgsz, "主机上还没有这个游戏的存档目录");
        return -1;
    }
    char target[780];
    if (snprintf(target, sizeof(target), "%s/%s", root, save) >= (int)sizeof(target)) return -1;

    struct stat st;
    if (lstat(target, &st) != 0) {
        if (msg) snprintf(msg, msgsz, "找不到这个存档槽");
        return -1;
    }

    gs_pack_t t;
    memset(&t, 0, sizeof(t));
    t.max = (int)GAME_SAVES_MAX_ENTRIES;
    t.prefix = save;

    if (S_ISREG(st.st_mode)) {
        gs_pack_add(&t, target, "");
    } else if (S_ISDIR(st.st_mode)) {
        gs_pack_walk(target, "", 0, &t);
    } else {
        if (msg) snprintf(msg, msgsz, "这个存档槽的类型不支持打包");
        return -1;
    }
    if (t.bad || t.n == 0) {
        free(t.e);
        if (msg) snprintf(msg, msgsz, t.n == 0 ? "这个存档是空的" : "存档里的文件太多（上限 4096）");
        return -1;
    }

    if (ensure_dir_recursive(GAME_SAVES_PACK_DIR) != 0) {
        free(t.e);
        if (msg) snprintf(msg, msgsz, "无法创建临时目录（%s）", strerror(errno));
        return -1;
    }
    const char *err = "打包失败";
    int rc = zip_write(out_path, t.e, t.n, NULL, &err);
    free(t.e);
    if (rc != 0) {
        if (msg) snprintf(msg, msgsz, "%s", err);
        return -1;
    }
    pldmgr_log("[PLDMGR] saves: %s/%s packed -> %s\n", tid, save, out_path);
    return 0;
}

/* ── 上传 ───────────────────────────────────────────────────────────────── */
typedef struct {
    char base[700];
    long long files;
    long long bytes;
    int bad; /* 0 正常，2 = 写不进去/超限 */
} gs_up_t;

static void gs_up_set_mtime(const char *path, time_t mt) {
    if (mt <= 0) return;
    struct timeval tv[2];
    tv[0].tv_sec = mt;
    tv[0].tv_usec = 0;
    tv[1] = tv[0];
    utimes(path, tv);
}

/* zip_extract / tar 共用的落盘回调。返回非 0 → 中止。 */
static int gs_up_cb(void *user, const char *name, const unsigned char *data,
                    size_t len, time_t mt) {
    gs_up_t *u = (gs_up_t *)user;
    if (u->bad) return 1;
    if (u->files >= (long long)GAME_SAVES_MAX_ENTRIES ||
        (unsigned long long)u->bytes + len > GAME_SAVES_MAX_BYTES) {
        u->bad = 2;
        return 1;
    }
    char path[1400];
    if (snprintf(path, sizeof(path), "%s/%s", u->base, name) >= (int)sizeof(path)) {
        u->bad = 2;
        return 1;
    }
    char parent[1400];
    snprintf(parent, sizeof(parent), "%s", path);
    char *sl = strrchr(parent, '/');
    if (sl) {
        *sl = 0;
        if (ensure_dir_recursive(parent) != 0) { u->bad = 2; return 1; }
    }
    if (write_file_text(path, (const char *)data, len) != 0) { u->bad = 2; return 1; }
    gs_up_set_mtime(path, mt);
    u->files++;
    u->bytes += (long long)len;
    return 0;
}

/* ── tar（ustar）：上传的 .tar / .tgz 要能吃 ────────────────────────────── */
static unsigned long gs_tar_oct(const char *p, int n) {
    unsigned long v = 0;
    for (int i = 0; i < n && p[i]; i++) {
        if (p[i] < '0' || p[i] > '7') break;
        v = (v << 3) | (unsigned long)(p[i] - '0');
    }
    return v;
}

static int gs_tar_extract(const unsigned char *buf, size_t len, gs_up_t *u) {
    size_t off = 0;
    while (off + 512 <= len && !u->bad) {
        const unsigned char *h = buf + off;
        int allz = 1;
        for (int i = 0; i < 512 && allz; i++) if (h[i]) allz = 0;
        if (allz) break; /* 结束块 */
        char name[101];
        memcpy(name, h, 100);
        name[100] = 0;
        char prefix[156];
        memcpy(prefix, h + 345, 155);
        prefix[155] = 0;
        unsigned long sz = gs_tar_oct((const char *)h + 124, 12);
        char type = (char)h[156];
        unsigned long mt = gs_tar_oct((const char *)h + 136, 12);

        char full[300];
        if (prefix[0]) snprintf(full, sizeof(full), "%s/%s", prefix, name);
        else snprintf(full, sizeof(full), "%s", name);

        off += 512;
        if (type == '0' || type == 0) {
            if (sz > GAME_SAVES_MAX_TAR_FILE || off + sz > len) { u->bad = 2; break; }
            gs_up_cb(u, full, buf + off, (size_t)sz, (time_t)mt);
        } else if (type == '5') {
            char dir[1400];
            if (snprintf(dir, sizeof(dir), "%s/%s", u->base, full) < (int)sizeof(dir))
                ensure_dir_recursive(dir);
        }
        /* 其它类型（符号链接、设备…）直接跳过：存档里不该有，有也不该由我们去创建 */
        off += (size_t)((sz + 511u) & ~511u);
    }
    return u->bad ? -1 : 0;
}

/* temp 目录里若**只有一个顶层目录**，它就是归档自带的那一层：
 * 下载出来的包是 <槽>/…，传回去时必须剥掉，否则会变成 <槽>/<槽>/…。 */
static int gs_single_subdir(const char *dir, char *out, size_t osz) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int n = 0, isdir = 0;
    char found[256];
    found[0] = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        n++;
        if (n == 1) {
            snprintf(found, sizeof(found), "%s", e->d_name);
            char p[900];
            struct stat st;
            snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
            isdir = (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
        }
    }
    closedir(d);
    if (n != 1 || !isdir || !gs_slot_ok(found)) return 0;
    snprintf(out, osz, "%s", found);
    return 1;
}

int game_saves_upload(const char *tid, const char *save,
                      const unsigned char *body, size_t len,
                      long long *out_files, long long *out_size,
                      char *msg, size_t msgsz) {
    if (out_files) *out_files = 0;
    if (out_size) *out_size = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!gs_tid_ok(tid)) { if (msg) snprintf(msg, msgsz, "标题 ID 不合法"); return -1; }
    if (save && *save && !gs_slot_ok(save)) {
        if (msg) snprintf(msg, msgsz, "存档槽名不合法");
        return -1;
    }
    if (!body || len == 0) { if (msg) snprintf(msg, msgsz, "文件是空的"); return -1; }
    if (len > GAME_SAVES_MAX_BYTES) { if (msg) snprintf(msg, msgsz, "文件太大"); return -1; }

    char root[640];
    if (gs_root_or_create(tid, root, sizeof(root)) != 0) {
        if (msg) snprintf(msg, msgsz, "找不到主机上的存档目录（%s 下没有用户目录）", GS_HOME);
        return -1;
    }

    char tmp[800];
    snprintf(tmp, sizeof(tmp), "%s/.upl_%u_%u", root, (unsigned)getpid(), (unsigned)time(NULL));
    gs_rmtree(tmp);
    if (ensure_dir_recursive(tmp) != 0) {
        if (msg) snprintf(msg, msgsz, "无法创建临时目录（%s）", strerror(errno));
        return -1;
    }

    gs_up_t u;
    memset(&u, 0, sizeof(u));
    snprintf(u.base, sizeof(u.base), "%s", tmp);

    char err[256];
    err[0] = 0;
    int rc = -1;

    if (len >= 2 && body[0] == 0x1F && body[1] == 0x8B) {
        /* .gz / .tgz：先 gunzip，解开是 tar 就继续解 tar，否则当单文件收下 */
        unsigned char *raw = NULL;
        size_t rawn = 0;
        if (gunzip_alloc(body, len, &raw, &rawn, err, sizeof(err)) != 0) {
            rc = -1;
        } else if (rawn >= 262 && !memcmp(raw + 257, "ustar", 5)) {
            rc = gs_tar_extract(raw, rawn, &u);
            if (rc != 0 && !err[0]) snprintf(err, sizeof(err), "tar 解包失败");
        } else if (rawn > 0) {
            rc = gs_up_cb(&u, "data.bin", raw, rawn, 0);
            if (rc != 0) snprintf(err, sizeof(err), "写入失败");
        } else {
            snprintf(err, sizeof(err), "解压后是空的");
            rc = -1;
        }
        free(raw);
    } else if (len >= 4 && !memcmp(body, "PK\x03\x04", 4)) {
        rc = zip_extract(body, len, gs_up_cb, &u, err, sizeof(err));
        if (rc == -2) { snprintf(err, sizeof(err), "解包被中止"); rc = -1; }
        if (rc == 0 && u.bad) {
            snprintf(err, sizeof(err), "写入失败或超过大小限制");
            rc = -1;
        }
    } else if (len >= 4 && !memcmp(body, "PK\x05\x06", 4)) {
        snprintf(err, sizeof(err), "这个 zip 里没有任何文件");
        rc = -1;
    } else if (len >= 262 && !memcmp(body + 257, "ustar", 5)) {
        rc = gs_tar_extract(body, len, &u);
        if (rc != 0 && !err[0]) snprintf(err, sizeof(err), "tar 解包失败");
    } else {
        /* 既不是 zip 也不是 tar/gzip —— 文件选择器允许选任意文件，那就整包存成一个 */
        rc = gs_up_cb(&u, "data.bin", body, len, 0);
        if (rc != 0) snprintf(err, sizeof(err), "写入失败");
    }

    if (rc == 0 && u.files == 0) {
        snprintf(err, sizeof(err), "这个文件里没有任何可写入的内容");
        rc = -1;
    }
    if (rc != 0) goto done;

    /* 定槽位：包自带的那层目录名优先（下载→上传就该回到同一个槽） */
    char sub[256];
    sub[0] = 0;
    int has_sub = gs_single_subdir(tmp, sub, sizeof(sub));
    char slot[160];
    if (save && *save) {
        snprintf(slot, sizeof(slot), "%s", save);
    } else if (has_sub) {
        snprintf(slot, sizeof(slot), "%s", sub);
    } else {
        slot[0] = 0;
        for (int i = 1; i <= 999 && !slot[0]; i++) {
            char cand[64], p[900];
            snprintf(cand, sizeof(cand), "save%d", i);
            snprintf(p, sizeof(p), "%s/%s", root, cand);
            struct stat st;
            if (stat(p, &st) != 0) snprintf(slot, sizeof(slot), "%s", cand);
        }
        if (!slot[0]) { snprintf(err, sizeof(err), "槽位编号用尽"); rc = -1; goto done; }
    }
    if (!gs_slot_ok(slot)) { snprintf(err, sizeof(err), "存档槽名不合法"); rc = -1; goto done; }

    char target[900];
    if (snprintf(target, sizeof(target), "%s/%s", root, slot) >= (int)sizeof(target)) {
        snprintf(err, sizeof(err), "路径太长");
        rc = -1;
        goto done;
    }
    struct stat tst;
    if (lstat(target, &tst) == 0) gs_rmtree(target); /* 覆盖 = 整个槽换掉（与前端提示一致） */

    char src[900];
    if (has_sub) snprintf(src, sizeof(src), "%s/%s", tmp, sub);
    else snprintf(src, sizeof(src), "%s", tmp);

    if (rename(src, target) != 0) {
        snprintf(err, sizeof(err), "无法写入存档目录（%s）", strerror(errno));
        rc = -1;
        goto done;
    }
    /* tmp 已被整体搬走（src == tmp）或只搬走了它的子目录，剩下的壳要清掉，
     * 否则它会在列表里以 .upl_* 的身份永远躺着（列表按 '.' 过滤，用户看不到也删不掉）。 */
    if (strcmp(src, tmp) != 0) gs_rmtree(tmp);
    rc = 0;

    pldmgr_log("[PLDMGR] saves: %s upload ok -> %s (%lld files, %lld bytes)\n",
               tid, slot, u.files, u.bytes);

done:
    if (rc != 0) gs_rmtree(tmp);
    if (rc != 0) {
        if (msg) snprintf(msg, msgsz, "%s", err[0] ? err : "上传失败");
        return -1;
    }
    if (out_files) *out_files = u.files;
    if (out_size) *out_size = u.bytes;
    return 0;
}

/* ── 删除 ───────────────────────────────────────────────────────────────── */
int game_saves_remove(const char *tid, const char *save, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!gs_tid_ok(tid) || !gs_slot_ok(save)) {
        if (msg) snprintf(msg, msgsz, "参数不合法");
        return -1;
    }
    char root[640], target[900];
    if (game_saves_root(tid, root, sizeof(root)) != 0) {
        if (msg) snprintf(msg, msgsz, "主机上还没有这个游戏的存档目录");
        return -1;
    }
    if (snprintf(target, sizeof(target), "%s/%s", root, save) >= (int)sizeof(target)) return -1;
    struct stat st;
    if (lstat(target, &st) != 0) {
        if (msg) snprintf(msg, msgsz, "找不到这个存档槽");
        return -1;
    }
    if (S_ISDIR(st.st_mode)) gs_rmtree(target);
    else if (unlink(target) != 0) {
        if (msg) snprintf(msg, msgsz, "删除失败（%s）", strerror(errno));
        return -1;
    }
    pldmgr_log("[PLDMGR] saves: %s removed slot %s\n", tid, save);
    return 0;
}
