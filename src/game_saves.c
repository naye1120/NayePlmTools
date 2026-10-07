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

/* 目录名规则：非空、不以 '.' 开头。允许点开头等于能匹配到我们自己写在同一个
 * 父目录里的 .upl_* / .old_* 半成品。**只在「检查归档自带的顶层目录名」时用**
 * —— 那是别人给的字节，规则可以宽一点。 */
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

/* 用户标识（API 里的 save 参数）现在就是 /user/home 下的目录名。真机是 8 位
 * 十六进制（1a1d3627），但不去绑死这个格式 —— 只要是不带路径分隔符、不带点、
 * 不以点开头的单层名字即可。**这一层就是 save 参数的路径穿越闸门**：
 * 拿不到 '/' 也拿不到 ".."，拼出来的路径不可能跳出 <GS_HOME>/<uid>/。 */
static int gs_uid_ok(const char *u) {
    if (!u || !*u || *u == '.') return 0;
    size_t n = strlen(u);
    if (n > 63) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = u[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

/* ── 递归统计（体积 / 文件数），带深度与条目预算 ──────────────────────────
 * 用 lstat 而不是 stat：存档里出现符号链接环时 stat 会跟着走进去，把整个请求拖死。
 * max_mtime 是给本地快照去重用的「最晚被写过的时刻」，只认普通文件。 */
typedef struct { long long bytes; long long files; time_t max_mtime; int over; } gs_stat_t;

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
            if (st.st_mtime > acc->max_mtime) acc->max_mtime = st.st_mtime;
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

/* ── 存档根目录 ───────────────────────────────────────────────────────────
 * 真机实测布局（2026-10，192.168.1.214）：
 *
 *   /user/home/<uid>/savedata_prospero/<TID>/    ← **PS5 存档真正在这里**
 *       sdimg_STARBOARD   sdimg_sce_sdmemory   sdimg_sce_bu_*  …
 *   /user/home/<uid>/savedata/<TID>/             ← PS4 存档位
 *   /user/home/<uid>/savedata/sce_backup2|4/     ← 系统备份槽（空的），不是游戏存档
 *
 * 老代码只找 <uid>/savedata/<TID> —— 真机上那个位置只有两个空的
 * sce_backup2 / sce_backup4，于是每个游戏的 local_path 都是 ""、saves 都是 []，
 * 存档功能**整个等于没接上**（列表空 → 下载/上传/删除全部无从下手）。
 * 现在两个目录都扫，savedata_prospero 优先；同一个用户两处都有时只取 PS5 那份。
 * 这条路径来自 garlic-savemgr（earthonion）的 scan_saves，它是同时扫这两个的。
 *
 * 粒度 =「一个用户一条」：一个 <uid>/<TID> 目录就是一份完整存档，里面的
 * sdimg_*（含 _bu_ 备份、PS4 的 .bin 密封密钥）一起打包。用户不必去分辨
 * 哪个 sdimg 是「当前」哪个是「备份」—— 那是主机自己的记账，照搬整份即可。
 *
 * 一台主机可能有多个用户都存过同一个游戏（实测 PPSA01650 在两个用户下都有），
 * 所以返回的是**列表**：每个用户各一条，互不覆盖。老代码「取体积最大的那个」
 * 正是为了绕开这个多用户问题，但那是把两个用户的存档拍扁成一个，会互相盖。
 */
#define GS_MAX_ROOTS 16

typedef struct {
    char uid[64];   /* 用户目录名，同时也是 API 里的 save 参数 */
    char path[640]; /* 存档根：<GS_HOME>/<uid>/<dir>/<TID> */
    char kind[8];   /* "PS5" / "PS4"，由所在目录决定（不再靠 tid 前缀猜） */
} gs_root_t;

/* readdir 的顺序是文件系统说了算的，而列表顺序必须稳定 —— 否则同一台主机上
 * 「用户 A 的存档」和「用户 B 的存档」谁在前会随机变，前端每刷一次都换位置。
 * 按 uid 升序排一下，顺带让 local_path（取第一条）也是确定的。 */
static int gs_uid_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static int gs_user_ids(char ids[][64], int max) {
    DIR *d = opendir(GS_HOME);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.' || e->d_name[0] == 0) continue;
        char p[512];
        if (snprintf(p, sizeof(p), "%s/%s", GS_HOME, e->d_name) >= (int)sizeof(p)) continue;
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) snprintf(ids[n++], 64, "%s", e->d_name);
    }
    closedir(d);
    if (n > 1) qsort(ids, (size_t)n, 64, gs_uid_cmp);
    return n;
}

static int gs_find_roots(const char *tid, gs_root_t *out, int max) {
    if (!gs_tid_ok(tid) || !out || max <= 0) return 0;
    char ids[16][64];
    int nu = gs_user_ids(ids, 16);
    static const char *const dirs[2]  = {"savedata_prospero", "savedata"};
    static const char *const kinds[2] = {"PS5", "PS4"};
    int n = 0;
    for (int i = 0; i < nu && n < max; i++) {
        for (int d = 0; d < 2 && n < max; d++) {
            char p[640];
            if (snprintf(p, sizeof(p), "%s/%s/%s/%s", GS_HOME, ids[i], dirs[d], tid)
                >= (int)sizeof(p)) continue;
            struct stat st;
            if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            snprintf(out[n].uid, sizeof(out[n].uid), "%s", ids[i]);
            snprintf(out[n].path, sizeof(out[n].path), "%s", p);
            snprintf(out[n].kind, sizeof(out[n].kind), "%s", kinds[d]);
            n++;
            break; /* 这个用户已命中，savedata 那一轮不再看（PS5 优先） */
        }
    }
    return n;
}

/* 兼容入口：第一个命中的存档根，给「只想知道这游戏存档在哪」的调用方。 */
int game_saves_root(const char *tid, char *out, size_t osz) {
    gs_root_t r[GS_MAX_ROOTS];
    if (!out || osz < 64) return -1;
    if (gs_find_roots(tid, r, GS_MAX_ROOTS) <= 0) return -1;
    snprintf(out, osz, "%s", r[0].path);
    return 0;
}

/* 按 uid 找根；uid 为空则退化成第一个（上传没指定用户时用）。 */
static int gs_root_by_uid(const char *tid, const char *uid, gs_root_t *out) {
    gs_root_t r[GS_MAX_ROOTS];
    int n = gs_find_roots(tid, r, GS_MAX_ROOTS);
    if (n <= 0) return -1;
    if (uid && *uid) {
        for (int i = 0; i < n; i++)
            if (!strcmp(r[i].uid, uid)) { *out = r[i]; return 0; }
        return -1;
    }
    *out = r[0];
    return 0;
}

/* 上传要往里写，所以连「这个游戏还没有存档」时也得把根定出来：
 * 指定了 uid 就用它，否则取第一个用户目录，落在 savedata_prospero 下。 */
static int gs_root_or_create(const char *tid, const char *uid,
                             gs_root_t *out, char *err, size_t errsz) {
    if (gs_root_by_uid(tid, uid, out) == 0) return 0;
    if (!gs_tid_ok(tid)) {
        if (err) snprintf(err, errsz, "标题 ID 不合法");
        return -1;
    }
    char ids[16][64];
    int nu = gs_user_ids(ids, 16);
    if (nu <= 0) {
        if (err) snprintf(err, errsz, "%s 下没有用户目录", GS_HOME);
        return -1;
    }
    const char *use = (uid && *uid) ? uid : ids[0];
    if (uid && *uid) {
        int found = 0;
        for (int i = 0; i < nu; i++)
            if (!strcmp(ids[i], uid)) found = 1;
        if (!found) {
            if (err) snprintf(err, errsz, "主机上没有这个用户（%s）", uid);
            return -1;
        }
    }
    snprintf(out->uid, sizeof(out->uid), "%s", use);
    snprintf(out->kind, sizeof(out->kind), "PS5");
    snprintf(out->path, sizeof(out->path), "%s/%s/savedata_prospero/%s", GS_HOME, use, tid);
    char parent[640];
    snprintf(parent, sizeof(parent), "%s/%s/savedata_prospero", GS_HOME, use);
    if (ensure_dir_recursive(parent) != 0) {
        if (err) snprintf(err, errsz, "无法创建存档目录（%s）", strerror(errno));
        return -1;
    }
    return 0;
}

/* ── 列表 ───────────────────────────────────────────────────────────────── */
static void gs_fmt_mtime(time_t t, char *out, size_t osz) {
    struct tm tmv;
    out[0] = 0;
    if (t <= 0 || !localtime_r(&t, &tmv)) return;
    strftime(out, osz, "%Y-%m-%d %H:%M", &tmv);
}

/* 条目数 = 存过这个游戏存档的用户数（通常是 0 或 1）。给 /game_detail 用，
 * 语义就是「这游戏有没有存档」，不再去数里面有几个 sdimg —— 那些是主机自己的
 * 记账（当前 + _bu_ 备份 + sdmemory），数出来只会让人以为有 4 份存档。 */
int game_saves_count(const char *tid) {
    gs_root_t r[GS_MAX_ROOTS];
    return gs_find_roots(tid, r, GS_MAX_ROOTS);
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
    /* v0.5.2.140 本地快照。与远程那三套并排放在同一个 tid 对象里 ——
     * 用户的心智是「对着这个游戏一起配」，不是「全局配一次」。 */
    cJSON *snap = cJSON_AddObjectToObject(o, "snap");
    if (snap) {
        cJSON_AddBoolToObject(snap, "enabled", 0);
        cJSON_AddNumberToObject(snap, "interval", GAME_SAVES_SNAP_INTERVAL_DEF);
        cJSON_AddNumberToObject(snap, "keep", GAME_SAVES_SNAP_KEEP_DEF);
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

/* 端口专用：夹在 1~65535，越界回落到 def。 */
static double gs_field_num(cJSON *o, const char *k, double def) {
    return (double)gs_get_port(o ? cJSON_GetObjectItem(o, k) : NULL, (int)def);
}

/* 普通整数字段（快照间隔 / 保留份数这类）。与 gs_get_port 的区别只有范围：
 * 端口夹在 1~65535，这个交给调用方定上下限。**不能拿 gs_get_port 读间隔** ——
 * 86400（一天）一过 65535 就被判成越界、悄悄回落成默认 30 分钟，用户填了等于没填。
 * 同样认数字字符串：前端 <input> 的 value 出来永远是字符串。 */
static int gs_get_int(cJSON *f, int def, int lo, int hi) {
    double v;
    if (f && cJSON_IsNumber(f)) {
        v = f->valuedouble;
    } else if (f && cJSON_IsString(f) && f->valuestring && f->valuestring[0]) {
        char *e = NULL;
        v = strtod(f->valuestring, &e);
        if (!e || e == f->valuestring || *e != 0) return def;
    } else {
        return def;
    }
    if (v < (double)lo || v > (double)hi) return def;
    return (int)v;
}

/* 快照两个数值字段的归一化。落盘、回吐、守护线程读三处都用它，保证口径一致 ——
 * 分开写的话，UI 显示 5 份、守护线程按 50 份裁，是那种谁也发现不了的不一致。 */
static int gs_snap_clamp_interval(int v) {
    if (v < GAME_SAVES_SNAP_INTERVAL_MIN) return GAME_SAVES_SNAP_INTERVAL_DEF;
    if (v > GAME_SAVES_SNAP_INTERVAL_MAX) return GAME_SAVES_SNAP_INTERVAL_MAX;
    return v;
}
static int gs_snap_clamp_keep(int v) {
    if (v < GAME_SAVES_SNAP_KEEP_MIN) return GAME_SAVES_SNAP_KEEP_DEF;
    if (v > GAME_SAVES_SNAP_KEEP_MAX) return GAME_SAVES_SNAP_KEEP_MAX;
    return v;
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

    cJSON *sns = src ? cJSON_GetObjectItem(src, "snap") : NULL;
    cJSON *snap = cJSON_AddObjectToObject(o, "snap");
    if (snap) {
        gs_put_bool(snap, "enabled", gs_field_bool(sns, "enabled", 0));
        gs_put_num(snap, "interval",
                   (double)gs_snap_clamp_interval((int)gs_get_int(
                       sns ? cJSON_GetObjectItem(sns, "interval") : NULL,
                       GAME_SAVES_SNAP_INTERVAL_DEF, 0, 1000000000)));
        gs_put_num(snap, "keep",
                   (double)gs_snap_clamp_keep((int)gs_get_int(
                       sns ? cJSON_GetObjectItem(sns, "keep") : NULL,
                       GAME_SAVES_SNAP_KEEP_DEF, 0, 1000000000)));
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

/* ══ 本地快照（v0.5.2.140）══════════════════════════════════════════════════
 * 设计见 include/game_saves.h 的「本地快照」一节。这里只记实现上的取舍。
 *
 * 落点：GAME_SAVES_SNAP_DIR/<TID>/<uid>/<YYYYmmdd-HHMMSS>/
 * 时间戳目录名用**定长 15 字符**，字典序即时间序 —— 排序、找最旧、找最新
 * 全都不用解析时间，也就没有时区/夏令时这类幺蛾子。
 */
#define GS_SNAP_MAX_DEPTH 24
#define GS_SNAP_MAX_LIST 64     /* 单个 (tid,uid) 下最多认这么多份 */
#define GS_SNAP_MAX_REFS 512    /* 全局预算淘汰时一次最多收集这么多份 */
#define GS_SNAP_BOOT_DELAY 90   /* 守护线程起来后先等这么久，避开启动期 IO */
#define GS_SNAP_TICK 30         /* 守护线程醒来周期（按每款各自的 interval 判定） */
#define GS_SNAP_MAX_GAMES 64    /* 一次扫描最多处理这么多款（配置里开了快照的） */

/* 源目录最近这么多秒内被写过 → 本轮跳过（正在写的存档拷出来是撕裂的）。
 * 可被 -D 覆盖：宿主测试里夹具是**刚造出来**的，文件 mtime 就是「现在」，
 * 不置 0 的话每一份快照都会被这条挡掉，测不到后面任何东西。 */
#ifndef GS_SNAP_QUIET_SEC
#define GS_SNAP_QUIET_SEC 20
#endif

/* 一把锁串起「守护线程」与「立即备份 / 还原 / 删除」四条路径：
 * 同一时刻只允许一份快照在动，免得两个方向同时往同一个目录里写。 */
static pthread_mutex_t g_snap_mu = PTHREAD_MUTEX_INITIALIZER;

static int gs_snap_ts_ok(const char *ts) {
    if (!ts) return 0;
    if (strlen(ts) != 15) return 0;
    for (int i = 0; i < 15; i++) {
        char c = ts[i];
        if (i == 8) { if (c != '-') return 0; continue; }
        if (c < '0' || c > '9') return 0;
    }
    return 1;
}

static void gs_snap_stamp(time_t t, char *out, size_t osz) {
    struct tm tmv;
    out[0] = 0;
    if (t <= 0 || !localtime_r(&t, &tmv)) return;
    strftime(out, osz, "%Y%m%d-%H%M%S", &tmv);
}

/* 路径拼装。tid / uid 进来之前都已过 gs_tid_ok / gs_uid_ok，ts 已过 gs_snap_ts_ok，
 * 所以这三条拼出来的路径不可能带 '/' 或 ".." 跳出去。 */
static int gs_snap_tid_dir(const char *tid, char *out, size_t osz) {
    return (snprintf(out, osz, "%s/%s", GAME_SAVES_SNAP_DIR, tid) < (int)osz) ? 0 : -1;
}
static int gs_snap_uid_dir(const char *tid, const char *uid, char *out, size_t osz) {
    return (snprintf(out, osz, "%s/%s/%s", GAME_SAVES_SNAP_DIR, tid, uid) < (int)osz) ? 0 : -1;
}
static int gs_snap_path(const char *tid, const char *uid, const char *ts,
                        char *out, size_t osz) {
    return (snprintf(out, osz, "%s/%s/%s/%s", GAME_SAVES_SNAP_DIR, tid, uid, ts) < (int)osz)
               ? 0
               : -1;
}

/* 升序列出 (tid,uid) 下的快照时间戳。非时间戳格式的目录一律不看 ——
 * 这一条同时把 .tmp_* / .old_* 这些我们自己的半成品挡在外面。 */
static int gs_snap_list_one(const char *tid, const char *uid, char ts[][24], int max) {
    char dir[900];
    if (gs_snap_uid_dir(tid, uid, dir, sizeof(dir)) != 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    char p[1024];
    while ((e = readdir(d)) != NULL && n < max) {
        if (!gs_snap_ts_ok(e->d_name)) continue;
        if (snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) >= (int)sizeof(p)) continue;
        struct stat st;
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        snprintf(ts[n++], 24, "%s", e->d_name);
    }
    closedir(d);
    if (n > 1) qsort(ts, (size_t)n, 24, gs_uid_cmp);
    return n;
}

/* ── 目录复制 ───────────────────────────────────────────────────────────── */
typedef struct { long long files; long long bytes; int bad; } gs_cp_t;

static int gs_copy_file(const char *src, const char *dst, gs_cp_t *c, const struct stat *st) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { close(in); return -1; }

    char buf[32 * 1024];
    long long total = 0;
    int rc = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n < 0) { rc = -1; break; }
        if (n == 0) break;
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w <= 0) { rc = -1; break; }
            off += w;
        }
        if (rc != 0) break;
        total += (long long)n;
    }
    close(in);
    close(out);
    if (rc != 0) { unlink(dst); return -1; }

    /* 保留 mtime：去重比的就是「(文件数, 字节数, 最大 mtime)」这个三元组，
     * 快照里那份的 mtime 必须和源一致，否则每一轮都会被判成「变了」而重复拷。
     * 顺带让还原回主机后的时间线和原来一样。 */
    struct timeval tv[2];
    tv[0].tv_sec = st->st_mtime;
    tv[0].tv_usec = 0;
    tv[1] = tv[0];
    utimes(dst, tv);

    c->files++;
    c->bytes += total;
    return 0;
}

/* 递归复制。**只按 lstat 走**，符号链接 / 设备 / 套接字一律跳过：存档里不该有，
 * 跟着它走还可能绕出目录树。返回 0 成功，-1 失败（c->bad 已置位）。 */
static int gs_copy_tree(const char *src, const char *dst, gs_cp_t *c, int depth) {
    if (c->bad) return -1;
    if (depth > GS_SNAP_MAX_DEPTH) { c->bad = 1; return -1; }
    DIR *d = opendir(src);
    if (!d) { c->bad = 1; return -1; }

    struct dirent *e;
    char sp[1024], dp[1024];
    int rc = 0;
    while ((e = readdir(d)) != NULL && !c->bad) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(sp, sizeof(sp), "%s/%s", src, e->d_name) >= (int)sizeof(sp) ||
            snprintf(dp, sizeof(dp), "%s/%s", dst, e->d_name) >= (int)sizeof(dp)) {
            c->bad = 1;
            rc = -1;
            break;
        }
        struct stat st;
        if (lstat(sp, &st) != 0) continue; /* 半路消失的条目跳过，不算失败 */
        if (S_ISDIR(st.st_mode)) {
            if (ensure_dir_recursive(dp) != 0) { c->bad = 1; rc = -1; break; }
            if (gs_copy_tree(sp, dp, c, depth + 1) != 0) { rc = -1; break; }
        } else if (S_ISREG(st.st_mode)) {
            if (c->files >= (long long)GAME_SAVES_SNAP_MAX_ENTRIES ||
                (unsigned long long)c->bytes + (unsigned long long)st.st_size >
                    GAME_SAVES_SNAP_MAX_BYTES) {
                c->bad = 1;
                rc = -1;
                break;
            }
            if (gs_copy_file(sp, dp, c, &st) != 0) { c->bad = 1; rc = -1; break; }
        }
    }
    closedir(d);
    return rc;
}

/* ── 整份替换（上传与快照还原共用）───────────────────────────────────────
 * 把 src 目录换到 target。旧的先改名留一手，换失败还能退回来 ——
 * 直接 rm -rf 再 rename 的话，中途任何一步失败都等于把用户原来的存档删了。
 * 调用方保证 src 与 target 在**同一文件系统**（都是 rename）。
 * 成功 0（src 已被搬走，无需清理）；失败 -1（target 保持原样，src 还在）。 */
static int gs_swap_dir(const char *parent, const char *target, const char *src,
                       char *err, size_t errsz) {
    char old[800];
    snprintf(old, sizeof(old), "%s/.old_%u_%u", parent, (unsigned)getpid(), (unsigned)time(NULL));
    int had_old = 0;
    struct stat ost;
    if (lstat(target, &ost) == 0) {
        gs_rmtree(old);
        if (rename(target, old) == 0) had_old = 1;
    }
    if (rename(src, target) != 0) {
        if (had_old) rename(old, target); /* 退回去 —— 用户原来那份还在 */
        if (err && errsz) snprintf(err, errsz, "无法写入存档目录（%s）", strerror(errno));
        return -1;
    }
    if (had_old) gs_rmtree(old);
    return 0;
}

/* ── 裁剪与预算 ─────────────────────────────────────────────────────────── */
/* 只留最近 keep 份，多的从最旧开始删。 */
static void gs_snap_prune(const char *tid, const char *uid, int keep) {
    keep = gs_snap_clamp_keep(keep);
    char ts[GS_SNAP_MAX_LIST][24];
    int n = gs_snap_list_one(tid, uid, ts, GS_SNAP_MAX_LIST);
    if (n <= keep) return;
    for (int i = 0; i < n - keep; i++) {
        char p[1024];
        if (gs_snap_path(tid, uid, ts[i], p, sizeof(p)) != 0) continue;
        pldmgr_log("[PLDMGR] saves: %s/%s 快照超保留数，删最旧的 %s\n", tid, uid, ts[i]);
        gs_rmtree(p);
    }
}

typedef struct {
    char tid[32];
    char uid[64];
    char ts[24];
    long long bytes;
    long long files;
    time_t when;
} gs_snap_ref_t;

static int gs_snap_ref_asc(const void *a, const void *b) {
    const gs_snap_ref_t *x = (const gs_snap_ref_t *)a, *y = (const gs_snap_ref_t *)b;
    int c = strcmp(x->ts, y->ts);
    if (c) return c;
    c = strcmp(x->tid, y->tid);
    return c ? c : strcmp(x->uid, y->uid);
}
static int gs_snap_ref_desc(const void *a, const void *b) { return -gs_snap_ref_asc(a, b); }

/* 收集某个游戏下的全部快照（跨用户）。新的排前面 —— 列表第一眼该看到最近那份。 */
static int gs_snap_collect_tid(const char *tid, gs_snap_ref_t *out, int max) {
    char tp[900];
    if (gs_snap_tid_dir(tid, tp, sizeof(tp)) != 0) return 0;
    DIR *du = opendir(tp);
    if (!du) return 0;
    int n = 0;
    struct dirent *eu;
    char up[1024];
    while ((eu = readdir(du)) != NULL && n < max) {
        if (!gs_uid_ok(eu->d_name)) continue;
        if (snprintf(up, sizeof(up), "%s/%s", tp, eu->d_name) >= (int)sizeof(up)) continue;
        DIR *ds = opendir(up);
        if (!ds) continue;
        struct dirent *es;
        char sp[1200];
        while ((es = readdir(ds)) != NULL && n < max) {
            if (!gs_snap_ts_ok(es->d_name)) continue;
            if (snprintf(sp, sizeof(sp), "%s/%s", up, es->d_name) >= (int)sizeof(sp)) continue;
            struct stat st;
            if (stat(sp, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            gs_stat_t a = {0, 0, 0, 0};
            unsigned b = GS_MAX_WALK;
            gs_walk(sp, &a, 0, &b);
            snprintf(out[n].tid, sizeof(out[n].tid), "%s", tid);
            snprintf(out[n].uid, sizeof(out[n].uid), "%s", eu->d_name);
            snprintf(out[n].ts, sizeof(out[n].ts), "%s", es->d_name);
            out[n].bytes = a.bytes;
            out[n].files = a.files;
            out[n].when = st.st_mtime;
            n++;
        }
        closedir(ds);
    }
    closedir(du);
    if (n > 1) qsort(out, (size_t)n, sizeof(out[0]), gs_snap_ref_desc);
    return n;
}

/* 总预算兜底：超了就从**全局最旧**开始删，直到降回预算内。
 * 只按每款游戏的 keep 裁剪是不够的 —— 15 款 × 5 份 × 18 MB 就是 1.3 GB。 */
static void gs_snap_evict_total(void) {
    gs_snap_ref_t *refs = (gs_snap_ref_t *)malloc(sizeof(gs_snap_ref_t) * GS_SNAP_MAX_REFS);
    if (!refs) return;
    int n = 0;
    DIR *dt = opendir(GAME_SAVES_SNAP_DIR);
    struct dirent *et;
    char tp[900];
    while (dt && (et = readdir(dt)) != NULL && n < GS_SNAP_MAX_REFS) {
        if (!gs_tid_ok(et->d_name)) continue;
        if (snprintf(tp, sizeof(tp), "%s/%s", GAME_SAVES_SNAP_DIR, et->d_name) >= (int)sizeof(tp))
            continue;
        n += gs_snap_collect_tid(et->d_name, refs + n, GS_SNAP_MAX_REFS - n);
    }
    if (dt) closedir(dt);
    if (n <= 0) { free(refs); return; }

    unsigned long long total = 0;
    for (int i = 0; i < n; i++) total += (unsigned long long)(refs[i].bytes > 0 ? refs[i].bytes : 0);
    if (total <= GAME_SAVES_SNAP_TOTAL_BYTES) { free(refs); return; }

    qsort(refs, (size_t)n, sizeof(refs[0]), gs_snap_ref_asc);
    for (int i = 0; i < n && total > GAME_SAVES_SNAP_TOTAL_BYTES; i++) {
        char p[1200];
        if (gs_snap_path(refs[i].tid, refs[i].uid, refs[i].ts, p, sizeof(p)) != 0) continue;
        pldmgr_log("[PLDMGR] saves: 快照总预算超限，淘汰最旧的 %s/%s/%s\n",
                   refs[i].tid, refs[i].uid, refs[i].ts);
        gs_rmtree(p);
        total -= (unsigned long long)(refs[i].bytes > 0 ? refs[i].bytes : 0);
    }
    free(refs);
}

/* ── 做一份快照 ─────────────────────────────────────────────────────────── */
/* 返回 0 = 新生成一份（out_ts 已填）；1 = 跳过（内容没变 / 空 / 正在被写，note 回原因）；
 * -1 = 失败（note 回人话）。调用方必须持有 g_snap_mu。 */
static int gs_snap_take(const char *tid, const gs_root_t *r, int keep,
                        char *out_ts, size_t osz, char *note, size_t notesz) {
    if (out_ts && osz) out_ts[0] = 0;
    struct stat st;
    if (lstat(r->path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (note) snprintf(note, notesz, "存档目录不存在");
        return -1;
    }

    gs_stat_t src = {0, 0, 0, 0};
    unsigned budget = GS_MAX_WALK;
    gs_walk(r->path, &src, 0, &budget);
    if (src.files == 0) { if (note) snprintf(note, notesz, "存档里没有文件"); return 1; }
    if (src.over) { if (note) snprintf(note, notesz, "存档条目太多，已跳过"); return -1; }

    /* 正在被写就先不动：拷到一半的存档还原回去可能让主机认不出来。
     * 宁可等下一轮 —— 快照的价值在于「有一份能用的」，不在于「有一份」。 */
    time_t now = time(NULL);
    if (src.max_mtime != 0 && now - src.max_mtime < GS_SNAP_QUIET_SEC) {
        if (note) snprintf(note, notesz, "存档刚被写过，等下一轮");
        return 1;
    }

    char udir[900];
    if (gs_snap_uid_dir(tid, r->uid, udir, sizeof(udir)) != 0) return -1;

    /* 内容没变就不新增：主机的存档绝大部分时间是同一份，按 30 分钟一拷的话
     * 一天就是 48 份一模一样的东西，白占用户的 /data。 */
    char ts[GS_SNAP_MAX_LIST][24];
    int nt = gs_snap_list_one(tid, r->uid, ts, GS_SNAP_MAX_LIST);
    if (nt > 0) {
        char last[1200];
        if (gs_snap_path(tid, r->uid, ts[nt - 1], last, sizeof(last)) == 0) {
            gs_stat_t old = {0, 0, 0, 0};
            unsigned b2 = GS_MAX_WALK;
            gs_walk(last, &old, 0, &b2);
            if (old.files == src.files && old.bytes == src.bytes &&
                old.max_mtime == src.max_mtime) {
                if (note) snprintf(note, notesz, "内容与最近一份相同，未新增");
                return 1;
            }
        }
    }

    if (ensure_dir_recursive(udir) != 0) {
        if (note) snprintf(note, notesz, "无法创建快照目录（%s）", strerror(errno));
        return -1;
    }

    /* 时间戳目录名要唯一：同一秒内再来一次就往后挪一秒（跨过 DST 也不会重复，
     * 因为我们是拿拼好的名字去 lstat 试，而不是算差值）。 */
    char tss[24];
    tss[0] = 0;
    for (int i = 0; i < 120; i++) {
        char cand[24];
        gs_snap_stamp(now + i, cand, sizeof(cand));
        if (!cand[0]) break;
        char probe[1200];
        if (gs_snap_path(tid, r->uid, cand, probe, sizeof(probe)) != 0) break;
        struct stat ps;
        if (lstat(probe, &ps) != 0) { snprintf(tss, sizeof(tss), "%s", cand); break; }
    }
    if (!tss[0]) { if (note) snprintf(note, notesz, "时间戳冲突，稍后再试"); return -1; }

    /* 先复制到 .tmp_*，成功才改名 —— 半截的快照永远不以正式名字出现 */
    char tmp[1000];
    snprintf(tmp, sizeof(tmp), "%s/.tmp_%u_%u", udir, (unsigned)getpid(), (unsigned)now);
    gs_rmtree(tmp);
    if (ensure_dir_recursive(tmp) != 0) {
        if (note) snprintf(note, notesz, "无法创建临时目录（%s）", strerror(errno));
        return -1;
    }

    gs_cp_t c = {0, 0, 0};
    if (gs_copy_tree(r->path, tmp, &c, 0) != 0) {
        gs_rmtree(tmp);
        if (note) snprintf(note, notesz, "%s", c.bad ? "复制失败（可能超过大小上限）" : "复制失败");
        return -1;
    }

    char dst[1200];
    if (gs_snap_path(tid, r->uid, tss, dst, sizeof(dst)) != 0) { gs_rmtree(tmp); return -1; }
    if (rename(tmp, dst) != 0) {
        gs_rmtree(tmp);
        if (note) snprintf(note, notesz, "快照落盘失败（%s）", strerror(errno));
        return -1;
    }

    gs_snap_prune(tid, r->uid, keep);
    gs_snap_evict_total();
    if (out_ts && osz) snprintf(out_ts, osz, "%s", tss);
    if (note) snprintf(note, notesz, "已生成 %s", tss);
    pldmgr_log("[PLDMGR] saves: %s/%s 本地快照 %s（%lld 文件 / %lld 字节）\n",
               tid, r->uid, tss, c.files, c.bytes);
    return 0;
}

/* ── 快照配置读取 ───────────────────────────────────────────────────────── */
/* 读一款游戏的 snap 配置。缺键 / 越界一律回落默认，调用方不必判 NULL。 */
static void gs_snap_cfg(const char *tid, int *enabled, int *interval, int *keep) {
    int en = 0, iv = GAME_SAVES_SNAP_INTERVAL_DEF, kp = GAME_SAVES_SNAP_KEEP_DEF;
    pthread_mutex_lock(&g_auto_mu);
    cJSON *all = gs_auto_load();
    cJSON *cur = all ? cJSON_GetObjectItem(all, tid) : NULL;
    cJSON *s = cur ? cJSON_GetObjectItem(cur, "snap") : NULL;
    if (s && cJSON_IsObject(s)) {
        en = gs_field_bool(s, "enabled", 0);
        iv = (int)gs_get_int(cJSON_GetObjectItem(s, "interval"),
                             GAME_SAVES_SNAP_INTERVAL_DEF, 0, 1000000000);
        kp = (int)gs_get_int(cJSON_GetObjectItem(s, "keep"),
                             GAME_SAVES_SNAP_KEEP_DEF, 0, 1000000000);
    }
    cJSON_Delete(all);
    pthread_mutex_unlock(&g_auto_mu);
    if (enabled) *enabled = en;
    if (interval) *interval = gs_snap_clamp_interval(iv);
    if (keep) *keep = gs_snap_clamp_keep(kp);
}

/* 列出配置里开了本地快照的全部游戏。 */
typedef struct { char tid[32]; int interval; int keep; } gs_snap_cfg_t;

static int gs_snap_enabled_games(gs_snap_cfg_t *out, int max) {
    int n = 0;
    pthread_mutex_lock(&g_auto_mu);
    cJSON *all = gs_auto_load();
    for (cJSON *it = all ? all->child : NULL; it && n < max; it = it->next) {
        if (!it->string || !gs_tid_ok(it->string) || !cJSON_IsObject(it)) continue;
        cJSON *s = cJSON_GetObjectItem(it, "snap");
        if (!s || !cJSON_IsObject(s)) continue;
        if (!gs_field_bool(s, "enabled", 0)) continue;
        snprintf(out[n].tid, sizeof(out[n].tid), "%s", it->string);
        out[n].interval = gs_snap_clamp_interval((int)gs_get_int(
            cJSON_GetObjectItem(s, "interval"), GAME_SAVES_SNAP_INTERVAL_DEF, 0, 1000000000));
        out[n].keep = gs_snap_clamp_keep((int)gs_get_int(
            cJSON_GetObjectItem(s, "keep"), GAME_SAVES_SNAP_KEEP_DEF, 0, 1000000000));
        n++;
    }
    cJSON_Delete(all);
    pthread_mutex_unlock(&g_auto_mu);
    return n;
}

/* ── 守护线程 ───────────────────────────────────────────────────────────── */
/* 「上一轮是什么时候」只记在内存里。重启后第一次会对所有开着的款各做一轮，
 * 但去重会挡住「内容没变」的那些，所以不会产生重复快照。 */
typedef struct { char tid[32]; time_t last; } gs_snap_mark_t;
static gs_snap_mark_t g_snap_marks[GS_SNAP_MAX_GAMES];
static int g_snap_mark_n = 0;

/* 到期判断 + 记账。必须在 g_snap_mu 里调用（只有守护线程碰它，但保持一致）。 */
static int gs_snap_due(const char *tid, int interval) {
    time_t now = time(NULL);
    for (int i = 0; i < g_snap_mark_n; i++) {
        if (strcmp(g_snap_marks[i].tid, tid) != 0) continue;
        if (now - g_snap_marks[i].last < interval) return 0;
        g_snap_marks[i].last = now;
        return 1;
    }
    if (g_snap_mark_n < GS_SNAP_MAX_GAMES) {
        snprintf(g_snap_marks[g_snap_mark_n].tid, sizeof(g_snap_marks[0].tid), "%s", tid);
        g_snap_marks[g_snap_mark_n].last = now;
        g_snap_mark_n++;
    }
    return 1;
}

static void gs_snap_sweep(void) {
    gs_snap_cfg_t games[GS_SNAP_MAX_GAMES];
    int ng = gs_snap_enabled_games(games, GS_SNAP_MAX_GAMES);
    if (ng <= 0) return;

    pthread_mutex_lock(&g_snap_mu);
    for (int i = 0; i < ng; i++) {
        if (!gs_snap_due(games[i].tid, games[i].interval)) continue;
        gs_root_t roots[GS_MAX_ROOTS];
        int nr = gs_find_roots(games[i].tid, roots, GS_MAX_ROOTS);
        for (int k = 0; k < nr; k++) {
            char ts[24], note[160];
            ts[0] = note[0] = 0;
            gs_snap_take(games[i].tid, &roots[k], games[i].keep,
                         ts, sizeof(ts), note, sizeof(note));
        }
    }
    pthread_mutex_unlock(&g_snap_mu);
}

static void *gs_snap_main(void *arg) {
    (void)arg;
    sleep(GS_SNAP_BOOT_DELAY);
    for (;;) {
        if (!pldmgr_utils_safe_mode()) gs_snap_sweep();
        sleep(GS_SNAP_TICK);
    }
    return NULL;
}

void game_saves_snap_start_daemon(void) {
    /* safe_mode：与风扇、名字自愈两条守护同一原则 —— 死机后要能止血，
     * 后台的自动动作一律不做。想留档就手动点「立即备份」。 */
    if (pldmgr_utils_safe_mode()) {
        pldmgr_log("[PLDMGR] saves: SAFE MODE，跳过本地快照守护\n");
        return;
    }
    pthread_t th;
    if (pthread_create(&th, NULL, gs_snap_main, NULL) == 0) {
        pthread_detach(th);
        pldmgr_log("[PLDMGR] saves: 本地快照守护已启动（首次运行延后 %ds）\n",
                   GS_SNAP_BOOT_DELAY);
    } else {
        pldmgr_log("[PLDMGR] saves: 本地快照守护线程创建失败\n");
    }
}

/* ── 立即备份 / 还原 / 删除 ─────────────────────────────────────────────── */
int game_saves_snap_run_now(const char *tid, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!gs_tid_ok(tid)) { if (msg) snprintf(msg, msgsz, "标题 ID 不合法"); return -1; }

    int keep = GAME_SAVES_SNAP_KEEP_DEF;
    gs_snap_cfg(tid, NULL, NULL, &keep);

    gs_root_t roots[GS_MAX_ROOTS];
    int nr = gs_find_roots(tid, roots, GS_MAX_ROOTS);
    if (nr <= 0) {
        if (msg) snprintf(msg, msgsz, "主机上还没有这个游戏的存档");
        return -1;
    }

    int made = 0, skipped = 0, failed = 0;
    char first[200];
    first[0] = 0;
    pthread_mutex_lock(&g_snap_mu);
    for (int i = 0; i < nr; i++) {
        char ts[24], note[200];
        ts[0] = note[0] = 0;
        int rc = gs_snap_take(tid, &roots[i], keep, ts, sizeof(ts), note, sizeof(note));
        if (rc == 0) made++;
        else if (rc == 1) skipped++;
        else failed++;
        if (rc != 0 && !first[0]) snprintf(first, sizeof(first), "%s", note);
    }
    pthread_mutex_unlock(&g_snap_mu);

    if (made > 0) {
        if (msg) snprintf(msg, msgsz, "已生成 %d 份快照", made);
        return made;
    }
    if (failed > 0) {
        if (msg) snprintf(msg, msgsz, "%s", first[0] ? first : "快照失败");
        return -1;
    }
    if (msg) snprintf(msg, msgsz, "%s", first[0] ? first : "内容没变，未新增");
    return skipped > 0 ? 0 : -1;
}

/* 删掉一份快照。0 成功，-1 失败（msg 回人话）。 */
static int gs_snap_delete(const char *tid, const char *uid, const char *ts,
                          char *msg, size_t msgsz) {
    char p[1200];
    if (gs_snap_path(tid, uid, ts, p, sizeof(p)) != 0) {
        if (msg) snprintf(msg, msgsz, "路径异常");
        return -1;
    }
    struct stat st;
    if (lstat(p, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (msg) snprintf(msg, msgsz, "找不到这份快照");
        return -1;
    }
    pthread_mutex_lock(&g_snap_mu);
    gs_rmtree(p);
    /* 用户目录空了就顺手收掉，免得 <tid>/<uid>/ 一层层留着空壳 */
    char udir[900];
    if (gs_snap_uid_dir(tid, uid, udir, sizeof(udir)) == 0) rmdir(udir);
    pthread_mutex_unlock(&g_snap_mu);
    pldmgr_log("[PLDMGR] saves: 快照已删除 %s/%s/%s\n", tid, uid, ts);
    return 0;
}

/* 把一份快照整份还原回主机。走的是与上传**同一条**「整份替换可回退」逻辑。 */
static int gs_snap_restore(const char *tid, const char *uid, const char *ts,
                           char *msg, size_t msgsz) {
    char src[1200];
    if (gs_snap_path(tid, uid, ts, src, sizeof(src)) != 0) {
        if (msg) snprintf(msg, msgsz, "路径异常");
        return -1;
    }
    struct stat st;
    if (lstat(src, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (msg) snprintf(msg, msgsz, "找不到这份快照");
        return -1;
    }

    gs_root_t r;
    char rerr[256];
    rerr[0] = 0;
    if (gs_root_or_create(tid, uid, &r, rerr, sizeof(rerr)) != 0) {
        if (msg) snprintf(msg, msgsz, "%s", rerr[0] ? rerr : "找不到主机上的存档目录");
        return -1;
    }

    char parent[640];
    snprintf(parent, sizeof(parent), "%s", r.path);
    char *sl = strrchr(parent, '/');
    if (!sl) { if (msg) snprintf(msg, msgsz, "路径异常"); return -1; }
    *sl = 0;

    char tmp[800];
    snprintf(tmp, sizeof(tmp), "%s/.rst_%u_%u", parent, (unsigned)getpid(), (unsigned)time(NULL));

    char err[256];
    err[0] = 0;
    int rc = -1;
    pthread_mutex_lock(&g_snap_mu);
    gs_rmtree(tmp);
    if (ensure_dir_recursive(tmp) != 0) {
        snprintf(err, sizeof(err), "无法创建临时目录（%s）", strerror(errno));
    } else {
        gs_cp_t c = {0, 0, 0};
        if (gs_copy_tree(src, tmp, &c, 0) != 0) {
            snprintf(err, sizeof(err), "复制快照失败");
        } else if (gs_swap_dir(parent, r.path, tmp, err, sizeof(err)) == 0) {
            rc = 0; /* tmp 已被 rename 走，不用清 */
        }
    }
    if (rc != 0) gs_rmtree(tmp);
    pthread_mutex_unlock(&g_snap_mu);

    if (rc != 0) {
        if (msg) snprintf(msg, msgsz, "%s", err[0] ? err : "还原失败");
        return -1;
    }
    pldmgr_log("[PLDMGR] saves: %s/%s 已从快照 %s 还原\n", tid, uid, ts);
    return 0;
}

int game_saves_snap_action(const char *body, size_t len,
                           char *buf, size_t size, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!buf || size < 64) return -1;

    cJSON *b = (body && len) ? cJSON_ParseWithLength(body, len) : NULL;
    if (!b || !cJSON_IsObject(b)) {
        cJSON_Delete(b);
        if (msg) snprintf(msg, msgsz, "JSON 解析失败");
        return -1;
    }
    char tid[32] = "", action[16] = "", uid[64] = "", ts[24] = "";
    cJSON *f;
    if ((f = cJSON_GetObjectItem(b, "tid")) && cJSON_IsString(f) && f->valuestring)
        snprintf(tid, sizeof(tid), "%s", f->valuestring);
    if ((f = cJSON_GetObjectItem(b, "action")) && cJSON_IsString(f) && f->valuestring)
        snprintf(action, sizeof(action), "%s", f->valuestring);
    if ((f = cJSON_GetObjectItem(b, "uid")) && cJSON_IsString(f) && f->valuestring)
        snprintf(uid, sizeof(uid), "%s", f->valuestring);
    if ((f = cJSON_GetObjectItem(b, "ts")) && cJSON_IsString(f) && f->valuestring)
        snprintf(ts, sizeof(ts), "%s", f->valuestring);
    cJSON_Delete(b);

    if (!gs_tid_ok(tid)) { if (msg) snprintf(msg, msgsz, "标题 ID 不合法"); return -1; }

    if (!strcmp(action, "run")) {
        int made = game_saves_snap_run_now(tid, msg, msgsz);
        if (made < 0) return -1;
        int w = snprintf(buf, size, "{\"ok\":true,\"made\":%d}", made);
        return (w > 0 && (size_t)w < size) ? 0 : -1;
    }
    /* 还原与删除都要定位到「具体哪一份」，uid 与 ts 都必须合法 ——
     * 这两个值是路径的最后两段，校验不过就绝不去拼路径。 */
    if (!gs_uid_ok(uid) || !gs_snap_ts_ok(ts)) {
        if (msg) snprintf(msg, msgsz, "参数不合法");
        return -1;
    }
    int rc = strcmp(action, "delete") == 0 ? gs_snap_delete(tid, uid, ts, msg, msgsz)
             : strcmp(action, "restore") == 0 ? gs_snap_restore(tid, uid, ts, msg, msgsz)
                                              : -2;
    if (rc == -2) { if (msg) snprintf(msg, msgsz, "未知的 action"); return -1; }
    if (rc != 0) return -1;
    int w = snprintf(buf, size, "{\"ok\":true}");
    return (w > 0 && (size_t)w < size) ? 0 : -1;
}

int game_saves_list_json(const char *tid, char *buf, size_t size) {
    if (!gs_tid_ok(tid) || !buf || size < 128) return -1;

    char etid[32], eroot[700];
    pldmgr_json_escape(tid, etid, sizeof(etid));

    gs_root_t roots[GS_MAX_ROOTS];
    int nr = gs_find_roots(tid, roots, GS_MAX_ROOTS);
    pldmgr_json_escape(nr > 0 ? roots[0].path : "", eroot, sizeof(eroot));

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

    /* saves 单独走堆：条目本身不多，但每条带名字，挂在栈上没必要冒这个险。
     * 线程栈是 pthread 默认值（通常 512KB~1MB），放不下 128KB 的局部数组。 */
    const size_t scap = 128u * 1024u;
    char *saves = (char *)malloc(scap);
    if (!saves) return -1;
    size_t spos = 0;
    int n = 0;
    if (gs_app(saves, scap, &spos, "[") != 0) { free(saves); return -1; }

    for (int i = 0; i < nr; i++) {
        gs_stat_t acc = {0, 0, 0, 0};
        unsigned budget = GS_MAX_WALK;
        gs_walk(roots[i].path, &acc, 0, &budget);

        char mt[40];
        mt[0] = 0;
        struct stat st;
        if (lstat(roots[i].path, &st) == 0) gs_fmt_mtime(st.st_mtime, mt, sizeof(mt));

        /* id 与 name 都是 uid：id 是下载/上传/删除要回传的 save 参数，
         * name 是给人看的。前端自己加「用户」这类本地化前缀 —— 后端不碰 i18n。 */
        char en[200];
        pldmgr_json_escape(roots[i].uid, en, sizeof(en));

        if (gs_app(saves, scap, &spos,
                   "%s{\"id\":\"%s\",\"name\":\"%s\",\"kind\":\"%s\","
                   "\"mtime\":\"%s\",\"size\":%lld,\"files\":%lld}",
                   n ? "," : "", en, en, roots[i].kind, mt,
                   acc.bytes, acc.files) != 0)
            break;
        n++;
    }
    gs_app(saves, scap, &spos, "]");

    /* snapshots 同样走堆。**新的在前** —— 列表第一眼该看到最近那份。 */
    const size_t kcap = 64u * 1024u;
    char *snaps = (char *)malloc(kcap);
    if (!snaps) { free(saves); return -1; }
    size_t kpos = 0;
    unsigned long long snap_used = 0;
    int nk = 0;
    if (gs_app(snaps, kcap, &kpos, "[") != 0) { free(snaps); free(saves); return -1; }

    gs_snap_ref_t *refs = (gs_snap_ref_t *)malloc(sizeof(gs_snap_ref_t) * GS_SNAP_MAX_LIST * 4);
    int nref = refs ? gs_snap_collect_tid(tid, refs, GS_SNAP_MAX_LIST * 4) : 0;
    for (int i = 0; i < nref; i++) {
        snap_used += (unsigned long long)(refs[i].bytes > 0 ? refs[i].bytes : 0);
        char mt[40], eu[200];
        gs_fmt_mtime(refs[i].when, mt, sizeof(mt));
        pldmgr_json_escape(refs[i].uid, eu, sizeof(eu));
        if (gs_app(snaps, kcap, &kpos,
                   "%s{\"uid\":\"%s\",\"ts\":\"%s\",\"mtime\":\"%s\","
                   "\"size\":%lld,\"files\":%lld}",
                   nk ? "," : "", eu, refs[i].ts, mt, refs[i].bytes, refs[i].files) != 0)
            break;
        nk++;
    }
    free(refs);
    gs_app(snaps, kcap, &kpos, "]");

    int w = snprintf(buf, size,
                     "{\"tid\":\"%s\",\"local_path\":\"%s\",\"auto\":%s,\"saves\":%s,"
                     "\"snap_used\":%llu,\"snapshots\":%s}",
                     etid, eroot, autoj, saves, snap_used, snaps);
    free(snaps);
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

    /* v0.5.2.140 本地快照：与远程那三个子对象同一套合并规则 —— 没传的保持原值。
     * enabled 只在**真的是布尔**时才覆盖（缺键 / null 一律不动）。 */
    cJSON *sn = cJSON_GetObjectItem(b, "snap");
    if (sn && cJSON_IsObject(sn)) {
        cJSON *dst = cJSON_GetObjectItem(cur, "snap");
        if (!dst || !cJSON_IsObject(dst)) {
            dst = cJSON_CreateObject();
            if (dst) cJSON_AddItemToObject(cur, "snap", dst);
        }
        if (dst) {
            cJSON *e = cJSON_GetObjectItem(sn, "enabled");
            if (e && cJSON_IsBool(e)) gs_put_bool(dst, "enabled", cJSON_IsTrue(e));
            int iv = gs_get_int(cJSON_GetObjectItem(sn, "interval"), -1, 0, 1000000000);
            if (iv >= 0) gs_put_num(dst, "interval", (double)gs_snap_clamp_interval(iv));
            int kp = gs_get_int(cJSON_GetObjectItem(sn, "keep"), -1, 0, 1000000000);
            if (kp >= 0) gs_put_num(dst, "keep", (double)gs_snap_clamp_keep(kp));
        }
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

/* save = uid。整份存档打包：<uid>/<TID>/ 下所有东西（含 sdimg_sce_bu_* 备份
 * 与 PS4 的 .bin 密封密钥）一起进 zip —— 还原时缺一样都可能让主机认不出来。
 * zip 内的顶层目录用 **tid** 而不是 uid：包本身该像「这个游戏的存档」，
 * 是哪个用户的由 URL 上的 save 参数决定，上传时也按它落位。 */
int game_saves_pack(const char *tid, const char *save, const char *out_path,
                    char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!gs_tid_ok(tid) || !gs_uid_ok(save)) {
        if (msg) snprintf(msg, msgsz, "参数不合法");
        return -1;
    }
    gs_root_t r;
    if (gs_root_by_uid(tid, save, &r) != 0) {
        if (msg) snprintf(msg, msgsz, "主机上还没有这个游戏的存档目录");
        return -1;
    }

    gs_pack_t t;
    memset(&t, 0, sizeof(t));
    t.max = (int)GAME_SAVES_MAX_ENTRIES;
    t.prefix = tid;
    gs_pack_walk(r.path, "", 0, &t);

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
    if (save && *save && !gs_uid_ok(save)) {
        if (msg) snprintf(msg, msgsz, "用户标识不合法");
        return -1;
    }
    if (!body || len == 0) { if (msg) snprintf(msg, msgsz, "文件是空的"); return -1; }
    if (len > GAME_SAVES_MAX_BYTES) { if (msg) snprintf(msg, msgsz, "文件太大"); return -1; }

    gs_root_t r;
    char rerr[256];
    rerr[0] = 0;
    if (gs_root_or_create(tid, save, &r, rerr, sizeof(rerr)) != 0) {
        if (msg) snprintf(msg, msgsz, "%s", rerr[0] ? rerr : "找不到主机上的存档目录");
        return -1;
    }

    /* 临时目录放在存档根的**父目录**下：换上去是同文件系统内的 rename。
     * 放到别处再拷回来，大存档要多花一倍时间，还可能半路失败留下残档。 */
    char parent[640];
    snprintf(parent, sizeof(parent), "%s", r.path);
    char *psl = strrchr(parent, '/');
    if (!psl) { if (msg) snprintf(msg, msgsz, "路径异常"); return -1; }
    *psl = 0;

    char tmp[800];
    snprintf(tmp, sizeof(tmp), "%s/.upl_%u_%u", parent, (unsigned)getpid(), (unsigned)time(NULL));
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

    /* 剥掉归档自带的那层目录（下载出来的包是 <TID>/…，传回去不该变成 <TID>/<TID>/…） */
    char sub[256];
    sub[0] = 0;
    int has_sub = gs_single_subdir(tmp, sub, sizeof(sub));
    char src[900];
    if (has_sub) snprintf(src, sizeof(src), "%s/%s", tmp, sub);
    else snprintf(src, sizeof(src), "%s", tmp);

    /* 整份替换（旧的先改名留一手，失败能退回来）—— 与快照还原共用 gs_swap_dir，
     * 两条路各写一份的话，早晚有一边忘了「退回去」。 */
    if (gs_swap_dir(parent, r.path, src, err, sizeof(err)) != 0) {
        rc = -1;
        goto done;
    }
    /* src == tmp 时 tmp 已被整体搬走；只搬走子目录的话剩下的壳要清掉，
     * 否则 .upl_* 会一直躺在存档目录里（用户看不到也删不掉）。 */
    if (strcmp(src, tmp) != 0) gs_rmtree(tmp);
    rc = 0;

    pldmgr_log("[PLDMGR] saves: %s upload ok -> %s (%lld files, %lld bytes)\n",
               tid, r.path, u.files, u.bytes);

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
/* save = uid。删掉的是**这个用户在这个游戏上的整份存档**
 * （<uid>/<dir>/<TID>/ 整个目录）—— 与「列表一行 = 一份完整存档」对齐。 */
int game_saves_remove(const char *tid, const char *save, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!gs_tid_ok(tid) || !gs_uid_ok(save)) {
        if (msg) snprintf(msg, msgsz, "参数不合法");
        return -1;
    }
    gs_root_t r;
    if (gs_root_by_uid(tid, save, &r) != 0) {
        if (msg) snprintf(msg, msgsz, "找不到这个用户的存档");
        return -1;
    }
    struct stat st;
    if (lstat(r.path, &st) != 0) {
        if (msg) snprintf(msg, msgsz, "找不到这个用户的存档");
        return -1;
    }
    if (S_ISDIR(st.st_mode)) gs_rmtree(r.path);
    else if (unlink(r.path) != 0) {
        if (msg) snprintf(msg, msgsz, "删除失败（%s）", strerror(errno));
        return -1;
    }
    pldmgr_log("[PLDMGR] saves: %s removed %s (%s)\n", tid, r.uid, r.path);
    return 0;
}
