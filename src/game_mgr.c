/* [CN-FORK] 游戏管理模块：枚举「已安装(app.db) + 已存在(扫描 dump)」游戏。
 * SQLite 只读解析器与 param.sfo 解析逐字移植自 v0.5.1.63 src/stats.c（实机验证过的实现），
 * 把原来「按单个 titleId 查一行」改造成「遍历 tbl_contentinfo 收集所有游戏行」。
 * 一次性拉取 + pthread_mutex 串行 + 60s 缓存，绝不轮询（app.db 全库读 + 目录扫描有 IO 成本，
 * 参考 /system_stats 死机教训，频率必须压住）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* [CN-FORK] strcasecmp：镜像扩展名大小写不敏感匹配 */
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h> /* statfs / getmntinfo：沙盒挂载点识别 */
#include <pthread.h>

#include "game_mgr.h"
#include "pldmgr.h" /* pldmgr_log / pldmgr_json_escape（经 utils.h） */

/* [CN-FORK] v0.5.2.116 列表项与详情都要用到这三块，谁都不许自己再抄一份：
 *   game_title  改名覆盖 + 两层自定义图的版本号/标志位
 *   game_saves  详情的 saves_count（存档根在哪只有 game_saves 知道）
 *   game_media  详情的 media_count（同上，av_content.db 只有 game_media 读得动）*/
#include "game_media.h"
#include "game_saves.h"
#include "game_title.h"

#define MAX_GAMES   512

/* [CN-FORK] v0.5.2.127 「来源类别」判定的两个根路径做成可覆盖，手法与
 * GAME_MGR_APPDB_PATH 完全一致（game_media.h / game_saves.h 也已经是这么写的）：
 *   · 真机 = /user/app（ShadowMount 的两个 lnk 落在这里）+ /system_ex（运行时挂载点）；
 *   · 主机测试 = test/run_host_tests.sh 用 -D 指到 /tmp/pldmgr_gm_test 下现造的目录树，
 *     这样「镜像/裸目录/内置 PKG 三种来源分类」在主机上就能真的跑起来。
 * 别去掉 #ifndef，默认值保持原样 —— 写死的话这条判定就只能在真机上验，
 * 而它恰恰是「真机上 14 款游戏全被标成已安装 PKG」那个 bug 的判据。 */
#ifndef GAME_MGR_APP_BASE
#define GAME_MGR_APP_BASE "/user/app"
#endif
#ifndef GAME_MGR_SYSTEM_EX
#define GAME_MGR_SYSTEM_EX "/system_ex"
#endif

/* ── 游戏 titleId：9 位 alnum 且命中已知游戏前缀（PPSA/CUSA/PPJB/EMUA/PAUS）──
 * 长度不足/超长（如 DLC 的 service 后缀）或非 alnum 一律拒，兼作路径穿越防护。 */
static int gm_tid_is_game(const char *t) {
    if (strlen(t) != 9) return 0;
    for (int i = 0; i < 9; i++)
        if (!isalnum((unsigned char)t[i])) return 0;
    return !memcmp(t, "PPSA", 4) || !memcmp(t, "CUSA", 4) || !memcmp(t, "PPJB", 4) ||
           !memcmp(t, "EMUA", 4) || !memcmp(t, "PAUS", 4);
}

/* ══════════════ param.sfo 解析（移植 stats.c sfo_*）══════════════ */
static uint32_t sfo_le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t sfo_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int sfo_str(const uint8_t *d, size_t sz, const char *key, char *out, size_t osz) {
    if (!d || sz < 20 || sfo_le32(d) != 0x46505300u /* "\0PSF" */) return -1;
    uint32_t ko = sfo_le32(d + 8), vo = sfo_le32(d + 12), n = sfo_le32(d + 16);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = d + 20 + i * 20;
        if ((size_t)(e - d) + 20 > sz) return -1;
        uint32_t k = ko + sfo_le32(e), fmt = sfo_le16(e + 8), v = vo + sfo_le32(e + 16);
        if (fmt != 2 || k >= sz || v >= sz) continue;
        if (strcmp((const char *)d + k, key) != 0) continue;
        size_t l = 0;
        while (v + l < sz && l < osz - 1 && d[v + l]) l++;
        memcpy(out, d + v, l);
        out[l] = 0;
        return 0;
    }
    return -1;
}

static int sfo_file_name(const char *path, char *name, size_t nsz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -errno;
    off_t fsz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (fsz <= 20 || fsz > (1 << 20)) { close(fd); return -2; }
    uint8_t *img = (uint8_t *)malloc((size_t)fsz);
    int rc = -2;
    if (img && read(fd, img, (size_t)fsz) == fsz) {
        name[0] = 0;
        if (sfo_str(img, (size_t)fsz, "title", name, nsz) == 0 && name[0]) rc = 0;
        else if (sfo_str(img, (size_t)fsz, "TITLE", name, nsz) == 0 && name[0]) rc = 0;
    }
    free(img);
    close(fd);
    return rc;
}

/* ══════════════ 极简 SQLite 只读解析（移植 stats.c st_*）══════════════ */
#define ST_GVS ((uint64_t)~0ull)

static uint64_t st_gvar(const uint8_t *d, size_t sz, size_t *o) {
    uint64_t v = 0;
    for (int i = 0; i < 9; i++) {
        if (*o >= sz) return ST_GVS;
        uint8_t b = d[(*o)++];
        if (i == 8) return (v << 8) | b;
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) return v;
    }
    return ST_GVS;
}
static uint16_t st_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t st_be32(const uint8_t *p) { return (uint32_t)(st_be16(p) << 16 | st_be16(p + 2)); }

/* 取记录第 idx 列：返回 2=字符串(写 tp/tl), 3=整数(写 num), 0=NULL, -1=错 */
static int st_rec_col(const uint8_t *d, size_t sz, size_t ro, int idx,
                      const uint8_t **tp, size_t *tl, uint64_t *num) {
    size_t o = ro;
    uint64_t hsz = st_gvar(d, sz, &o);
    if (hsz == ST_GVS || hsz == 0 || ro + hsz > sz) return -1;
    size_t bo = ro + hsz;
    for (int i = 0; i <= idx; i++) {
        uint64_t st = st_gvar(d, sz, &o);
        if (st == ST_GVS) return -1;
        size_t len;
        int kind;
        uint64_t small = 0;
        int hassmall = 0;
        if (st == 0) { len = 0; kind = 0; }
        else if (st >= 1 && st <= 7) { len = st == 5 ? 6 : (st >= 6 ? 8 : st); kind = 3; }
        else if (st == 8) { len = 0; kind = 3; small = 0; hassmall = 1; }
        else if (st == 9) { len = 0; kind = 3; small = 1; hassmall = 1; }
        else if (st == 10) { len = 0; kind = 3; small = 48; hassmall = 1; }
        else if (st == 11) { len = 0; kind = 3; small = 1; hassmall = 1; }
        else if (st >= 12) { len = (st - 12) / 2; kind = (st & 1) ? 2 : 1; }
        else { len = 0; kind = 1; }
        if (bo + len > sz) return -1;
        if (i == idx) {
            if (kind == 2) { if (tp) *tp = d + bo; if (tl) *tl = len; return 2; }
            if (kind == 3) {
                if (num) {
                    if (hassmall) *num = small;
                    else { uint64_t v = 0; for (size_t k = 0; k < len; k++) v = v << 8 | d[bo + k]; *num = v; }
                }
                return 3;
            }
            return kind == 0 ? 0 : -1;
        }
        bo += len;
    }
    return -1;
}

typedef int (*st_rec_cb)(const uint8_t *, size_t, size_t, void *);

static int st_bwalk(const uint8_t *d, size_t sz, uint32_t pg, uint32_t pgsz, int depth, st_rec_cb cb, void *arg) {
    uint64_t off64 = (uint64_t)(pg - 1) * pgsz;
    if (!pg || pg > 2000000 || off64 + 120 > sz) return 0;
    size_t off = (size_t)off64;
    size_t hdr = off + (pg == 1 ? 100 : 0);
    uint8_t flags = d[hdr];
    int ncell = st_be16(d + hdr + 3);
    if (ncell > 4096) return 0;
    if (flags != 13 && flags != 5) return 0;
    if (flags == 5 && depth >= 6) return 0;
    size_t cpt = hdr + (flags == 5 ? 12 : 8);
    for (int i = 0; i < ncell; i++) {
        size_t cp = cpt + (size_t)i * 2;
        if (cp + 2 > sz) return 0;
        size_t po = off + st_be16(d + cp);
        if (po + 2 > sz) return 0;
        if (flags == 13) {
            size_t ro = po;
            if (st_gvar(d, sz, &ro) == ST_GVS || st_gvar(d, sz, &ro) == ST_GVS) continue;
            if (cb(d, sz, ro, arg)) return 1;
        } else {
            if (st_bwalk(d, sz, st_be32(d + po), pgsz, depth + 1, cb, arg)) return 1;
        }
    }
    if (flags == 5) {
        uint32_t rm = st_be32(d + hdr + 8);
        if (st_bwalk(d, sz, rm, pgsz, depth + 1, cb, arg)) return 1;
    }
    return 0;
}

/* [CN-FORK] 从建表 SQL 里定位列序号：数第一个 '(' 到列名之间「顶层逗号」个数。
 * 固件间 tbl_contentinfo 列序可能不同，pprDeeplinkUri 这类唯一列名不写死序号；
 * 简化处理未跳过引号内逗号（该表无带逗号的字符串默认值，够用）。 */
static int st_sql_col_index(const char *sql, size_t sl, const char *col) {
    char tmp[4096];
    size_t n = sl < sizeof(tmp) - 1 ? sl : sizeof(tmp) - 1;
    memcpy(tmp, sql, n);
    tmp[n] = 0;
    const char *lp = strchr(tmp, '(');
    if (!lp) return -1;
    const char *cp = lp + 1;
    for (;;) {
        cp = strstr(cp, col);
        if (!cp) return -1;
        if (!isalnum((unsigned char)cp[-1]) && cp[-1] != '_') break; /* 防命中更长列名里的子串 */
        cp++;
    }
    int depth = 0, idx = 0;
    for (const char *p = lp + 1; p < cp; p++) {
        if (*p == '(') depth++;
        else if (*p == ')') { if (--depth < 0) return -1; }
        else if (*p == ',' && depth == 0) idx++;
    }
    return depth == 0 ? idx : -1;
}

/* 在 sqlite_master 里定位 tbl_contentinfo 的根页 + pprDeeplinkUri 列序号（col4=建表 SQL） */
typedef struct { uint32_t root; int deeplink_col; int size_col; } st_master_arg;
static int st_cb_master(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    st_master_arg *m = (st_master_arg *)arg;
    const uint8_t *tp;
    size_t tl;
    uint64_t num = 0;
    if (st_rec_col(d, sz, rec, 1, &tp, &tl, NULL) != 2 || tl != 15 || memcmp(tp, "tbl_contentinfo", 15) != 0) return 0;
    if (st_rec_col(d, sz, rec, 3, NULL, NULL, &num) != 3 || !num) return 0;
    m->root = (uint32_t)num;
    if (st_rec_col(d, sz, rec, 4, &tp, &tl, NULL) == 2 && tl) {
        m->deeplink_col = st_sql_col_index((const char *)tp, tl, "pprDeeplinkUri");
        m->size_col    = st_sql_col_index((const char *)tp, tl, "size");
    }
    return 1;
}

/* ── 收集累加器：把结果边解析边写成 JSON 数组片段，按 tid 去重 ── */
typedef struct {
    char *buf;
    size_t size;
    size_t pos;
    char tids[MAX_GAMES][12];
    int count;
    int overflow;
    /* [CN-FORK] v0.5.2.116 详情模式：want 非 NULL 时只认这一个 tid，结果记在 d_*，
     * 且**完全不写 buf**。同一套枚举（app.db → scan paths，按 tid 去重）两种用法，
     * 免得 /game_detail 另写一份再和列表慢慢漂移 —— 那正是「列表说 folder、详情说
     * image」这类问题的标准来路，而两个字段在同一个弹窗里会并排显示，一漂就看得见。 */
    const char *want;
    char d_name[512];
    char d_src[16];
    char d_fmt[16];
    unsigned long long d_size;
    int d_found;
    /* [CN-FORK] v0.5.2.134 列表模式跳过递归 du。
     * gm_fmt_size 对 folder 游戏会调 gm_dir_size_cached → gm_dir_size（递归 opendir/lstat），
     * 一款完整解包的 PS5 游戏动辄上万文件，15 款就是几十万到上百万次系统调用 —— 这就是
     * 「游戏列表载入超级慢」的主因。列表不需要精确体积（SMP 也一律报 0），跳过 du 后
     * folder 游戏 size=0，前端 formatSize 对 0 返回空串、不显示体积标签。
     * 详情模式（want 非 NULL）不设 skip_du，照旧拿真实体积。
     * v0.5.2.135 补：列表 JSON 也带 appdb_size（app.db 的 size 列），前端在 size=0 时
     * 用 appdb_size 兜底显示，避免磁贴体积标签整个消失。 */
    int skip_du;
    /* tbl_contentinfo 的 size 列在**真实列序**里的位置（由 st_cb_master 从建表 SQL 现查）。 */
    int size_col;
    unsigned long long d_appdb_size;   /* 详情模式的 app.db size */
    /* [CN-FORK] v0.5.2.136 后台预热：列表模式（skip_du=1）下把**体积缓存未命中**的
     * folder/dump 根目录收集到这里，build_game_json 再交给后台线程去 du。
     * 只有列表会填（warm 指向 g_warm_roots）；详情走 gm_lookup，memset 后 warm=NULL，
     * gm_warm_add 直接早退。 */
    char (*warm)[512];
    int   warm_n;
} enum_arg_t;

/* 与 mock / 前端同口径：CUSA、PPJB 是 PS4 本体，其余按 PS5 算。 */
static const char *gm_platform(const char *tid) {
    return (!strncmp(tid, "CUSA", 4) || !strncmp(tid, "PPJB", 4)) ? "PS4" : "PS5";
}

/* 单条 JSON 的最坏长度：name 转义后最多 511 + 固定骨架约 200 = 711。
 * 原来给 640，加了 platform/icon/bg 三个字段后就不够了，取 760 留余量 ——
 * 余量不足的后果是写到一半发现放不下，于是 `overflow` 与**一条被截断的 JSON**
 * 同时产生，前端解析直接失败（比少一款游戏严重得多）。 */
#define GM_ITEM_HEADROOM 800

static void gm_append_item(enum_arg_t *a, const char *tid, const char *name, const char *src, const char *fmt, unsigned long long size, unsigned long long appdb_size) {
    if (a->want) {
        if (a->d_found || strcmp(a->want, tid) != 0) return;
        snprintf(a->d_name, sizeof(a->d_name), "%s", name);
        snprintf(a->d_src, sizeof(a->d_src), "%s", src);
        snprintf(a->d_fmt, sizeof(a->d_fmt), "%s", fmt);
        a->d_size = size;
        a->d_appdb_size = appdb_size;
        a->d_found = 1;
        return;
    }
    if (a->count >= MAX_GAMES) { a->overflow = 1; return; }
    for (int i = 0; i < a->count; i++)
        if (!strcmp(a->tids[i], tid)) return;   /* 去重：已安装优先（先入者胜） */
    if (a->pos + GM_ITEM_HEADROOM >= a->size) { a->overflow = 1; return; }

    /* 改名只落在我们自己那一层（game_title.c），**列表与详情必须走同一个口盖名**：
     * 分别读的话，改完名列表是新名、详情是旧名，用户只会当成「没生效」。 */
    char renamed[512];
    const char *shown = game_title_override_name(tid, name, renamed, sizeof(renamed));

    char esc[512];
    pldmgr_json_escape(shown, esc, sizeof(esc));
    /* icon / bg = 自定义图**版本号**（0 = 没设过，前端据此不去取图，省掉每款两次 404）。
     * 每条都要开一次 .ver 文件 —— 列表 60s 才重建一次，这点 IO 换掉前端 N 次白跑的 404。 */
    long long iver = game_title_icon_ver(tid);
    long long bver = game_title_bg_ver(tid);
    int used = snprintf(a->buf + a->pos, a->size - a->pos,
                        "%s{\"tid\":\"%s\",\"name\":\"%s\",\"src\":\"%s\",\"fmt\":\"%s\",\"size\":%llu,"
                        "\"appdb_size\":%llu,"
                        "\"platform\":\"%s\",\"icon\":%lld,\"bg\":%lld}",
                        a->count ? "," : "", tid, esc, src, fmt, size, appdb_size,
                        gm_platform(tid), iver, bver);
    if (used < 0 || (size_t)used >= GM_ITEM_HEADROOM) { a->overflow = 1; return; }
    a->pos += (size_t)used;
    snprintf(a->tids[a->count], sizeof(a->tids[0]), "%s", tid);
    a->count++;
}

/* [CN-FORK] 前向声明：游戏格式与体积解析（定义见下方 ShadowMount mount.lnk 块），供 app.db 枚举回调使用。 */
static const char *gm_fmt_size(const char *tid, const char *src, unsigned long long *size_out, int skip_du);
static int gm_lnk_root(const char *tid, char *root, size_t rsz);

/* [CN-FORK] v0.5.2.136 后台预热队列上限（见 enum_arg_t.warm）。 */
#define GM_WARM_MAX 64

/* [CN-FORK] v0.5.2.136 记下一个「体积还没算出来」的根目录，交给后台线程补（见 gm_warm_kick）。
 * 去重：同一根目录可能既从 app.db 又从 dump 扫到。超过上限就丢 —— 宁可少数几款体积
 * 晚一轮出现，也不能让这张表溢出踩内存。 */
static void gm_warm_add(enum_arg_t *a, const char *root) {
    if (!a->warm || !root || !root[0] || a->warm_n >= GM_WARM_MAX) return;
    for (int i = 0; i < a->warm_n; i++)
        if (!strcmp(a->warm[i], root)) return;
    snprintf(a->warm[a->warm_n], 512, "%s", root);
    a->warm_n++;
}

/* tbl_contentinfo 每行：col0=titleId, col2=titleName。总返回 0 以便遍历全部行。 */
static int st_cb_enum(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    enum_arg_t *a = (enum_arg_t *)arg;
    const uint8_t *tp;
    size_t tl;
    if (st_rec_col(d, sz, rec, 0, &tp, &tl, NULL) != 2 || tl != 9) return 0;
    char tid[12];
    memcpy(tid, tp, 9);
    tid[9] = 0;
    if (!gm_tid_is_game(tid)) return 0;
    char name[128];
    name[0] = 0;
    if (st_rec_col(d, sz, rec, 2, &tp, &tl, NULL) == 2 && tl && tl < sizeof(name)) {
        memcpy(name, tp, tl);
        name[tl] = 0;
    }
    if (!name[0]) snprintf(name, sizeof(name), "%s", tid);
    unsigned long long gsize;
    const char *fmt = gm_fmt_size(tid, "installed", &gsize, a->skip_du);
    /* 列表模式且体积缓存未命中（gsize=0）→ 记下根目录交后台补算。
     * **不在这里 du**：这款游戏上万文件，列表就卡住了。
     * 只认 folder/lz4：镜像游戏的体积是源镜像文件一次 stat，为 0 只说明文件此刻不在
     * （外置盘没插），去 du 它的运行时挂载点毫无意义 —— 那还会白排一条队列。 */
    if (a->skip_du && !gsize && (!strcmp(fmt, "folder") || !strcmp(fmt, "lz4"))) {
        char wroot[512];
        if (gm_lnk_root(tid, wroot, sizeof(wroot))) gm_warm_add(a, wroot);
    }
    /* app.db 的 size 列（安装包体积）：固件间列序可变，由 st_cb_master 现查 st_sql_col_index。
     * 注意它**不是** folder 游戏的体积（真机 132 GB 的游戏这里只有 1.22 GB），
     * 所以只在前端给「无源可扫」的 pkg 兜底，绝不拿去当 folder 的体积。 */
    unsigned long long appdb_sz = 0;
    if (a->size_col >= 0) {
        uint64_t num = 0;
        if (st_rec_col(d, sz, rec, a->size_col, NULL, NULL, &num) == 3) appdb_sz = (unsigned long long)num;
    }
    gm_append_item(a, tid, name, "installed", fmt, gsize, appdb_sz);
    return 0;
}

static void game_enum_appdb(enum_arg_t *a) {
    int fd = open(GAME_MGR_APPDB_PATH, O_RDONLY);
    if (fd < 0) { pldmgr_log("[PLDMGR] game_list: app.db open failed errno=%d\n", errno); return; }
    off_t fsz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (fsz < 100 || fsz > (64 << 20)) { close(fd); pldmgr_log("[PLDMGR] game_list: app.db bad size %lld\n", (long long)fsz); return; }
    uint8_t *d = (uint8_t *)malloc((size_t)fsz);
    if (!d) { close(fd); return; }
    ssize_t rd = read(fd, d, (size_t)fsz);
    close(fd);
    if (rd < 200 || memcmp(d, "SQLite format 3", 16) != 0) { free(d); return; }
    uint32_t pgsz = st_be16(d + 16);
    if (pgsz == 1) pgsz = 65536;
    if (pgsz < 512 || pgsz > 65536) { free(d); return; }
    st_master_arg m = { 0, -1, -1 };
    st_bwalk(d, (size_t)rd, 1, pgsz, 0, st_cb_master, &m);
    a->size_col = m.size_col;
    if (m.root) st_bwalk(d, (size_t)rd, m.root, pgsz, 0, st_cb_enum, a);
    free(d);
}

/* ── [CN-FORK] deeplink 枚举：供「常用入口」同步 app.db 磁贴链接（pprDeeplinkUri）。
 * 与游戏列表差异：不做游戏前缀过滤（用户自制磁贴 titleId 可能非游戏前缀），
 * col0 只要求非空；uri 必须 http(s):// 开头才回调。 */
typedef struct {
    game_mgr_deeplink_cb cb;
    void *user;
    int col;
    int hits;
} deeplink_arg_t;

static int st_cb_deeplink(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    deeplink_arg_t *a = (deeplink_arg_t *)arg;
    const uint8_t *tp;
    size_t tl;
    char tid[16] = "";
    if (st_rec_col(d, sz, rec, 0, &tp, &tl, NULL) == 2 && tl && tl < sizeof(tid)) {
        memcpy(tid, tp, tl);
        tid[tl] = 0;
    }
    if (!tid[0]) return 0;
    char name[128];
    name[0] = 0;
    if (st_rec_col(d, sz, rec, 2, &tp, &tl, NULL) == 2 && tl && tl < sizeof(name)) {
        memcpy(name, tp, tl);
        name[tl] = 0;
    }
    if (!name[0]) snprintf(name, sizeof(name), "%s", tid);
    char uri[512];
    uri[0] = 0;
    if (st_rec_col(d, sz, rec, a->col, &tp, &tl, NULL) == 2 && tl && tl < sizeof(uri)) {
        memcpy(uri, tp, tl);
        uri[tl] = 0;
    }
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0) return 0;
    a->hits++;
    return a->cb(tid, name, uri, a->user);
}

int game_mgr_enum_deeplinks(game_mgr_deeplink_cb cb, void *user) {
    if (!cb) return -1;
    int fd = open(GAME_MGR_APPDB_PATH, O_RDONLY);
    if (fd < 0) return -1;
    off_t fsz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (fsz < 100 || fsz > (64 << 20)) { close(fd); return -1; }
    uint8_t *d = (uint8_t *)malloc((size_t)fsz);
    if (!d) { close(fd); return -1; }
    ssize_t rd = read(fd, d, (size_t)fsz);
    close(fd);
    if (rd < 200 || memcmp(d, "SQLite format 3", 16) != 0) { free(d); return -1; }
    uint32_t pgsz = st_be16(d + 16);
    if (pgsz == 1) pgsz = 65536;
    if (pgsz < 512 || pgsz > 65536) { free(d); return -1; }
    st_master_arg m = { 0, -1, -1 };
    st_bwalk(d, (size_t)rd, 1, pgsz, 0, st_cb_master, &m);
    if (!m.root) { free(d); return -1; }              /* 连表都没有：异常库，交给调用方下次重试 */
    if (m.deeplink_col < 0) { free(d); return 0; }     /* 库正常但无该列：视为「无磁贴链接」 */
    deeplink_arg_t a;
    a.cb = cb;
    a.user = user;
    a.col = m.deeplink_col;
    a.hits = 0;
    st_bwalk(d, (size_t)rd, m.root, pgsz, 0, st_cb_deeplink, &a);
    free(d);
    return a.hits;
}

/* ── 扫描 ShadowMount 式 scan paths，发现「已存在但未安装」的 dump 目录 ──
 * 一级子目录名即 TITLE_ID，判 <path>/<TID>/sce_sys/param.sfo 存在才算一个游戏。 */
static const char *const GM_SCAN_PATHS[] = {
    "/data/homebrew", "/data/etaHEN/games",
    "/mnt/ext0", "/mnt/ext1", "/mnt/shadowmnt", "/mnt/shadowmnt/pfsc",
    "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3",
    "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
};

/* ── [CN-FORK] ShadowMount+ 格式探测（实机 FTP 校准，非猜测）：直读它落盘的链接文件，不查挂载表。
 * 设备用 persistent_image_mounts=0，镜像只在游戏运行时才挂到 /mnt/shadowmnt，列表工具查挂载表必空——
 * 这正是前几版「全 PKG」的根因。真正持久、可信的判据是每个已注册游戏 /user/app/<TID>/ 下的两个文本文件：
 *   mount_img.lnk  镜像游戏独有，首行=源镜像真实路径（带 .ffpkg/.exfat/.ffpfs/.ffpfsc 扩展名）；
 *   mount.lnk      所有 ShadowMount 游戏都有，镜像游戏指向运行挂载点、裸目录游戏直接指向源目录；
 *   两者都没有     = 系统内置安装的 PKG。 */

/* 照 ShadowMountPlus detect_image_fs_type：源文件名扩展名 → 格式。先判长的 ffpfsc 再判 ffpfs，避免前缀误命中。 */
static const char *gm_img_fmt(const char *path) {
    const char *e = strrchr(path, '.');
    if (!e) return NULL;
    if (!strcasecmp(e, ".ffpfsc")) return "ffpfsc";
    if (!strcasecmp(e, ".ffpfs"))  return "ffpfs";
    if (!strcasecmp(e, ".ffpkg"))  return "ffpkg";
    if (!strcasecmp(e, ".exfat"))  return "exfat";
    return NULL;
}

static int gm_ext_is_image(const char *path) { return gm_img_fmt(path) != NULL; }

/* ── [CN-FORK] v0.5.2.127 来源类别：pkg / image / folder ────────────────────
 *
 * 判据必须**只看真实物理源**（那两个 lnk 文件），绝不能看 a.d_src。
 * a.d_src 记的是「这条是从 app.db 还是扫描路径**枚举**到的」，而 ShadowMount 的
 * 镜像/裸目录游戏**同样会注册进 app.db**，于是它们的 d_src 也是 "installed"。
 * 老实现拿 d_src 当来源类型，真机上 14 款 ShadowMount 游戏全被标成「已安装 PKG」，
 * 连带物理路径（错回 /user/app/<TID>）、can_manage_source（false → 删除按钮消失）、
 * 以及 /game_delete 的 403 EPERM 一起错。对齐 ShadowMountPlus 1.7 的
 * resolve_game_source()：mount_img.lnk → image，mount.lnk → folder，都没有 → pkg。
 *
 * 判据与 gm_fmt_size 完全同源（fmt 就是那两个 lnk 解析出来的），所以直接由 fmt 推 ——
 * 两处各写一遍一定会漂移。 */
static const char *gm_class_of_fmt(const char *fmt) {
    if (!strcmp(fmt, "pkg")) return "pkg";
    if (!strcmp(fmt, "folder") || !strcmp(fmt, "lz4")) return "folder";
    return "image";   /* ffpkg / exfat / ffpfs / ffpfsc */
}

/* 照 ShadowMountPlus 1.7 的 is_data_mounted()：/system_ex/app/<TID>/sce_sys/param.json
 * 存在 = 这个游戏此刻正被挂进系统（真的在跑）。
 * **不能**拿「runtime_path 存在」当判据：裸目录游戏的 mount.lnk 直接指向源目录，
 * 那目录**一直都在**，会让「已挂载」对所有裸目录游戏永远亮着（真机 PPSA01773 就是
 * 这样被误标成已挂载的，而 ShadowMount 自己回的是 mounted=false）。 */
static int gm_is_data_mounted(const char *tid) {
    char p[320];
    if (snprintf(p, sizeof(p), "%s/app/%s/sce_sys/param.json", GAME_MGR_SYSTEM_EX, tid) >= (int)sizeof(p)) return 0;
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* 读文本文件首行（去尾换行）到 out；空/读不到返回 0。 */
static int gm_read_lnk(const char *file, char *out, size_t osz) {
    FILE *f = fopen(file, "rb");
    if (!f) return 0;
    char b[512];
    size_t n = fread(b, 1, sizeof(b) - 1, f);
    fclose(f);
    if (n == 0) return 0;
    b[n] = 0;
    char *s = b;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t i = 0;
    for (; s[i] && s[i] != '\r' && s[i] != '\n' && i + 1 < osz; i++) out[i] = s[i];
    out[i] = 0;
    return i > 0;
}

/* 从 mount.lnk 解析裸目录游戏的实际根目录（镜像游戏 mount.lnk 指 /mnt/shadowmnt 运行时挂载点，
 * 未运行时不存在，此时返回 0，交由镜像逻辑处理）。 */
static int gm_lnk_root(const char *tid, char *root, size_t rsz) {
    char f[256], line[512];
    snprintf(f, sizeof(f), "%s/%s/mount.lnk", GAME_MGR_APP_BASE, tid);
    if (!gm_read_lnk(f, line, sizeof(line))) return 0;
    if (gm_ext_is_image(line)) return 0;
    snprintf(root, rsz, "%s", line);
    return 1;
}

/* 镜像游戏体积 = 源镜像文件 st_size（正是 ShadowMountPlus 挂载时读的那个 file_size，秒出精确）。 */
static unsigned long long gm_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) return (unsigned long long)st.st_size;
    return 0;
}

/* 裸目录游戏体积 = 有上限的递归 du（累加普通文件 st_size）。设备目录条目很少（几十到几百），成本极低；
 * 仍设硬上限防符号链接环/极端目录失控：限深度 + 限访问条目数，超限即停（返回已累加值）。 */
#define GM_DU_MAX_ENTRIES 200000u
#define GM_DU_MAX_DEPTH 16
static unsigned long long gm_du(const char *dir, int depth, unsigned *budget) {
    unsigned long long total = 0;
    if (depth > GM_DU_MAX_DEPTH || *budget == 0) return 0;
    DIR *dp = opendir(dir);
    if (!dp) return 0;
    struct dirent *ent;
    while ((ent = readdir(dp))) {
        if (*budget == 0) break;
        if (ent->d_name[0] == '.') continue;
        (*budget)--;
        char child[1024];
        int n = snprintf(child, sizeof(child), "%s/%s", dir, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(child)) continue;
        struct stat st;
        if (lstat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) total += gm_du(child, depth + 1, budget);
        else if (S_ISREG(st.st_mode)) total += (unsigned long long)st.st_size;
    }
    closedir(dp);
    return total;
}
static unsigned long long gm_dir_size(const char *root) {
    unsigned budget = GM_DU_MAX_ENTRIES;
    return gm_du(root, 0, &budget);
}

/* ── [CN-FORK] v0.5.2.118 递归 du 的进程内缓存（路径 → 体积 + 取值时刻）──
 *
 * 上面那句注释写的「设备目录条目很少（几十到几百），成本极低」**对列表这条路不成立**：
 * gm_fmt_size 与 game_enum_dump 是**每重建一次列表就给每款 folder/dump 游戏各 du 一遍**，
 * 一款完整解包的 PS5 游戏动辄上万文件，几十款就是几十万次 opendir/readdir/lstat。
 * 真机报的「游戏列表载入超级慢」主因是它 —— 快照缓存（见 g_game_cache）修好之后，
 * 这条仍然决定了**第一次**以及失效之后那一次要等多久。
 *
 * 游戏体积不是天天变的量，TTL 内直接复用；TTL 到点、或这个路径没登记过才真 du。
 * 与 g_game_cache 的 60s 快照**刻意不同 TTL**：快照要「刚改完名就看见名字」，
 * 体积要的是「别为了个十进制数重扫一遍盘」，两者诉求相反，不能共用一个开关。
 *
 * v0.5.2.136 起这张表**不只被列表/详情读**了 —— 后台预热线程（gm_warm_worker）
 * 也往里写，所以必须自己带锁 g_du_mtx。锁只护着这张表本身，**绝不护着扫盘**：
 * 否则后台线程一跑，列表与详情全堵在这把锁上。 */
#define GM_DU_TTL   86400    /* 24 小时：游戏体积不可能天天变。列表只 peek 缓存，
                                * 未命中交后台预热线程（gm_warm_*）去扫；详情照旧自己
                                * du 一次并写回。缓存 24 小时意味着一天里只有第一次
                                * （或过期后）真扫，之后都是毫秒级。 */
#define GM_DUCACHE  128
static struct {
    char root[512];
    unsigned long long size;
    time_t ts;
} g_du[GM_DUCACHE];
static pthread_mutex_t g_du_mtx = PTHREAD_MUTEX_INITIALIZER;

/* 只读缓存，**不扫盘**：命中返回体积，未命中/过期返回 0。
 * 列表模式用它 —— 拿不到就先留空，交给后台线程补（见 gm_warm_kick）。 */
static unsigned long long gm_dir_size_peek(const char *root) {
    if (!root || !root[0]) return 0;
    time_t now = time(NULL);
    unsigned long long v = 0;
    pthread_mutex_lock(&g_du_mtx);
    for (int i = 0; i < GM_DUCACHE; i++) {
        if (g_du[i].root[0] && strcmp(g_du[i].root, root) == 0) {
            if (now >= g_du[i].ts && now - g_du[i].ts < GM_DU_TTL) v = g_du[i].size;
            break;
        }
    }
    pthread_mutex_unlock(&g_du_mtx);
    return v;
}

/* 命中缓存直接返回；否则真 du 一次并写回。扫盘在**锁外**做（见上面那段注释）。 */
static unsigned long long gm_dir_size_cached(const char *root) {
    if (!root || !root[0]) return 0;
    unsigned long long v = gm_dir_size_peek(root);
    if (v) return v;

    v = gm_dir_size(root);
    /* 空目录 / 读不到：**不写缓存**。写 0 的话 peek 分不清「算出来就是 0」和
     * 「还没算过」，列表会把空目录也当成未命中反复预热。 */
    if (!v) return 0;

    time_t now = time(NULL);
    pthread_mutex_lock(&g_du_mtx);
    int slot = -1;
    for (int i = 0; i < GM_DUCACHE; i++)
        if (g_du[i].root[0] && strcmp(g_du[i].root, root) == 0) { slot = i; break; }
    if (slot < 0) {
        for (int i = 0; i < GM_DUCACHE; i++)
            if (!g_du[i].root[0]) { slot = i; break; }
        if (slot < 0) { /* 满了：轮转覆盖最老的那条 */
            slot = 0;
            for (int i = 1; i < GM_DUCACHE; i++)
                if (g_du[i].ts < g_du[slot].ts) slot = i;
        }
    }
    snprintf(g_du[slot].root, sizeof(g_du[slot].root), "%s", root);
    g_du[slot].size = v;
    g_du[slot].ts = now;
    pthread_mutex_unlock(&g_du_mtx);
    return v;
}

/* 游戏格式+体积：mount_img.lnk 命中 → 镜像（扩展名定档，体积=镜像文件 st_size）；
 * 仅 mount.lnk → 裸目录(folder，含 ampr_ 则 lz4，体积=有上限递归 du)；都无 → 内置 pkg（体积 0=未知）。
 * skip_du 非 0 时（列表模式）folder 游戏**只读体积缓存、不扫盘**：命中就给真实体积，
 * 未命中留 0，由 build_game_json 收集后交后台线程补（见 gm_warm_kick）—— 列表因此秒开，
 * 几秒后磁贴体积自己补齐。详情模式 skip_du=0 照旧现 du 并写回缓存。
 * 镜像游戏（mount_img.lnk）两条路都只 stat 一次镜像文件，不递归。 */
static const char *gm_fmt_size(const char *tid, const char *src, unsigned long long *size_out, int skip_du) {
    *size_out = 0;
    char f[256], line[512];
    snprintf(f, sizeof(f), "%s/%s/mount_img.lnk", GAME_MGR_APP_BASE, tid);
    if (gm_read_lnk(f, line, sizeof(line))) {
        const char *fmt = gm_img_fmt(line);
        if (fmt) { *size_out = gm_file_size(line); return fmt; }
    }
    char root[512];
    if (gm_lnk_root(tid, root, sizeof(root))) {
        *size_out = skip_du ? gm_dir_size_peek(root) : gm_dir_size_cached(root);
        DIR *dp = opendir(root);
        int lz4 = 0;
        if (dp) {
            struct dirent *ent;
            while ((ent = readdir(dp)))
                if (!strncmp(ent->d_name, "ampr_", 5)) { lz4 = 1; break; }
            closedir(dp);
        }
        return lz4 ? "lz4" : "folder";
    }
    /* 无 ShadowMount 链接文件：已注册(app.db)即内置安装 pkg；扫描到的裸目录即 folder。 */
    return strcmp(src, "installed") == 0 ? "pkg" : "folder";
}

/* 供 dump 扫描路径复用：裸目录里带 ampr_ 前缀条目即 ampr 实时解压(LZ4)变体。 */
static int gm_dir_is_lz4(const char *root) {
    DIR *dp = opendir(root);
    if (!dp) return 0;
    int hit = 0;
    struct dirent *ent;
    while ((ent = readdir(dp))) {
        if (!strncmp(ent->d_name, "ampr_", 5)) { hit = 1; break; }
    }
    closedir(dp);
    return hit;
}

static void game_enum_dump(enum_arg_t *a) {
    const unsigned n = sizeof(GM_SCAN_PATHS) / sizeof(GM_SCAN_PATHS[0]);
    for (unsigned s = 0; s < n && !a->overflow; s++) {
        DIR *dp = opendir(GM_SCAN_PATHS[s]);
        if (!dp) continue;
        struct dirent *ent;
        char sfo[768], root[768];
        while ((ent = readdir(dp))) {
            if (ent->d_name[0] == '.') continue;
            if (!gm_tid_is_game(ent->d_name)) continue;
            snprintf(root, sizeof(root), "%s/%s", GM_SCAN_PATHS[s], ent->d_name);
            snprintf(sfo, sizeof(sfo), "%s/sce_sys/param.sfo", root);
            if (access(sfo, F_OK) != 0) continue;
            char name[128];
            name[0] = 0;
            sfo_file_name(sfo, name, sizeof(name));
            if (!name[0]) snprintf(name, sizeof(name), "%s", ent->d_name);
            unsigned long long sz;
            const char *fmt = gm_fmt_size(ent->d_name, "dump", &sz, a->skip_du);
            if (!strcmp(fmt, "folder") && gm_dir_is_lz4(root)) fmt = "lz4";
            /* dump 游戏不在 /user/app，gm_fmt_size 里那条 lnk 路走不到，体积全靠这里：
             * 列表模式只 peek 缓存（不扫盘），未命中就记下来交后台补算。 */
            if (!sz) sz = a->skip_du ? gm_dir_size_peek(root) : gm_dir_size_cached(root);
            if (a->skip_du && !sz) gm_warm_add(a, root);
            gm_append_item(a, ent->d_name, name, "dump", fmt, sz, 0);   /* dump 游戏不在 app.db，appdb_size=0 */
        }
        closedir(dp);
    }
}

/* ══════════════ 图标文件解析（移植 stats.c pldmgr_get_game_icon_path 主体）══════════════ */
static int icon_file_ok(const char *p) {
    struct stat st;
    return p && p[0] && !strstr(p, "..") && stat(p, &st) == 0 && S_ISREG(st.st_mode)
        && st.st_size > 0 && st.st_size <= 4 * 1024 * 1024;
}

/* [CN-FORK] v0.5.2.116 找这个游戏**真正放磁贴图的目录**（已存在才返回，绝不新建）。
 *
 * 给 /game_tile_* 的「落到主机上」那一步用：app_installer.c 装自己的磁贴就是往
 * <dir>/icon0.png 与 <dir>/pic0.{png,dds} 写，这里要先找到那个 <dir>。
 * 候选顺序与 game_mgr_icon_path 同源，但**不看文件、只看目录**：
 *   ① /user/app/<TID>/sce_sys        —— 已安装（PKG）与已注册的 ShadowMount 游戏
 *   ② mount.lnk 解出的裸目录 sce_sys   —— 没注册、只有链接文件的 dump
 *   ③ scan paths 下的 <path>/<TID>/sce_sys —— 谁都没登记过的裸 dump
 * ② ③ 找不到就返回 0：**宁可不落盘（图仍在我们自己那份里，面板照常显示），
 * 也不去凭空 mkdir 一个 /user/app/<TID>** —— 那会往用户分区里塞一个系统认不出的目录。 */
int game_mgr_sce_sys_dir(const char *tid, char *out, size_t osz) {
    if (!tid || !out || osz < 64) return 0;
    if (!gm_tid_is_game(tid)) return 0;
    char cand[768];
    struct stat st;

    snprintf(cand, sizeof(cand), "%s/%s/sce_sys", GAME_MGR_APP_BASE, tid);
    if (stat(cand, &st) == 0 && S_ISDIR(st.st_mode)) { snprintf(out, osz, "%s", cand); return 1; }

    char root[512];
    if (gm_lnk_root(tid, root, sizeof(root))) {
        snprintf(cand, sizeof(cand), "%s/sce_sys", root);
        if (stat(cand, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(out, osz, "%s", cand); return 1;
        }
    }

    for (size_t i = 0; i < sizeof(GM_SCAN_PATHS) / sizeof(GM_SCAN_PATHS[0]); i++) {
        snprintf(cand, sizeof(cand), "%s/%s/sce_sys", GM_SCAN_PATHS[i], tid);
        if (stat(cand, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(out, osz, "%s", cand); return 1;
        }
    }
    return 0;
}

/* [CN-FORK] v0.5.2.116 读一个 param.sfo 里的字符串字段（路径 → 字段值）。
 * 存档槽的显示名就来自它：PS5 的存档目录里有一份 SFO 的 SAVEDATA_TITLE，
 * 没有 SFO（PS4 的 sdimg_<TID> 那种）时调用方自己退回用目录名。
 * 抽成公开函数是因为这已经是仓库里第三处要读 SFO 的地方了，再抄一遍没道理。 */
int game_mgr_sfo_text(const char *path, const char *key, char *out, size_t osz) {
    if (!path || !key || !out || !osz) return -1;
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char *d = (unsigned char *)malloc(65536);
    if (!d) { fclose(f); return -1; }
    size_t n = fread(d, 1, 65536, f);
    fclose(f);
    int rc = n ? sfo_str(d, n, key, out, osz) : -1;
    free(d);
    if (rc != 0) out[0] = 0;
    return rc;
}

int game_mgr_icon_path(const char *tid, char *out, size_t osz) {
    if (!tid || !out || osz < 256) return 0;
    if (!gm_tid_is_game(tid)) return 0; /* 9 位 alnum，防路径穿越 */
    char cand[768];

    snprintf(cand, sizeof(cand), "%s/%s/sce_sys/icon0.png", GAME_MGR_APP_BASE, tid);
    if (icon_file_ok(cand)) { snprintf(out, osz, "%s", cand); return 1; }

    /* 该 tid 的沙盒 app0：/mnt/sandbox/<TID>_xxx/app0/sce_sys/icon0.png */
    {
        struct statfs *mnts = NULL;
        int mn = getmntinfo(&mnts, MNT_NOWAIT);
        for (int i = 0; mn > 0 && i < mn; i++) {
            const char *to = mnts[i].f_mntonname;
            if (strncmp(to, "/mnt/sandbox/", 13) != 0) continue;
            if (strncmp(to + 13, tid, 9) != 0) continue;
            const char *sl = strchr(to + 13, '/');
            if (!sl || strncmp(sl + 1, "app0", 4) != 0 || (sl[5] && sl[5] != '/')) continue;
            snprintf(cand, sizeof(cand), "%s/sce_sys/icon0.png", to);
            if (icon_file_ok(cand)) { snprintf(out, osz, "%s", cand); return 1; }
        }
    }

    snprintf(cand, sizeof(cand), "/user/appmeta/%s/icon0.png", tid);
    if (icon_file_ok(cand)) { snprintf(out, osz, "%s", cand); return 1; }

    snprintf(cand, sizeof(cand), "/system_data/priv/appmeta/%s/icon0.png", tid);
    if (icon_file_ok(cand)) { snprintf(out, osz, "%s", cand); return 1; }

    return 0;
}

/* ══════════════ /game_detail：单个游戏的完整档案 ══════════════
 *
 * 与 /game_list 共用**同一套枚举**（enum_arg_t 的 want 模式），所以「列表说 folder、
 * 详情说 image」这种两个字段并排显示时才看得见的漂移，在结构上就不会发生。
 *
 * 除枚举结果外，还有两类数据只在这里取：
 *   ① app.db 的单行补充字段 —— contentId / size / installTime / lastAccessTime /
 *      uninstallable。列序号**不写死**，按建表 SQL 实时定位。
 *   ② 主机文件系统能给的 —— 源路径 / 运行时路径 / 存档与媒体条数 / 两层自定义图标志。
 *
 * **不做推断**：last_launch 读不到就给空串（前端显示「—」），不拿 mtime 冒充。
 * 假时间比空时间危害大得多 —— 用户会据此判断「我昨天没玩过这个游戏」。
 *
 * ── 为什么这里另写一个列序号定位，而不用旁边的 st_sql_col_index ────────────
 *   st_sql_col_index 从 `lp + 1` 起数括号，而 `lp` 是整句 "CREATE TABLE x (…)"，
 *   那个左括号先把 depth 顶成 1，往后直到列名都没有右括号把它降回 0 ——
 *   于是**它对任何列都返回 -1**（拿 _ftp_probe 的真 app.db 逐列核过）。
 *   它现在唯一的服务对象是 quicklinks 的 pprDeeplinkUri 同步：返回 -1 会让
 *   game_mgr_enum_deeplinks 走「库正常但无该列」那条分支，功能静默返回 0 条。
 *   那是**另一块已上线、且当前行为稳定**的功能，把它从「恒 0 条」改成「开始真的枚举」
 *   属于跨功能的未验证改动，不在这轮范围里 —— 所以这里用一个正确的本地实现，
 *   原函数一个字不动。两处的差异在上面这段注释里记死，免得以后有人「顺手统一」。 */

static int gm_sql_col_index(const char *lp, const char *col) {
    if (!lp || !col || !*col) return -1;
    const char *open = strchr(lp, '(');
    if (!open) return -1;

    const char *hit = open + 1;
    size_t clen = strlen(col);
    for (;;) {
        const char *f = strstr(hit, col);
        if (!f) return -1;
        /* 左边界：前一个字符不是标识符字符（防命中在更长的名字中间）；
         * 右边界：后一个字符也不是标识符字符 —— 这一条 st_sql_col_index 没有，
         * 对 "size" 靠「sizeOtherHdd 排在它后面」侥幸没出事，换个列名就会读错位。 */
        int left_ok = (f > lp) && !isalnum((unsigned char)f[-1]) && f[-1] != '_';
        const char *a = f + clen;
        int right_ok = !isalnum((unsigned char)*a) && *a != '_';
        if (left_ok && right_ok) { hit = f; break; }
        hit = f + 1;
    }

    /* 从左括号**之后**开始、以嵌套深度 0 为「顶层」数逗号 —— 这样外层那对括号
     * 不参与计数，列名里的 DECIMAL(10,2) 之类也不会把逗号算成列分隔符。
     * 老实现起点是 lp+1，外层左括号把 depth 顶成 1 且再也降不回来，故恒返回 -1。 */
    int depth = 0, idx = 0;
    for (const char *p = open + 1; p < hit; p++) {
        if (*p == '(') depth++;
        else if (*p == ')') { if (--depth < 0) return -1; }
        else if (*p == ',' && depth == 0) idx++;
    }
    return depth == 0 ? idx : -1;
}

#define GM_DB_NCOL 5
enum {
    GM_DB_CONTENTID = 0,
    GM_DB_ACCESS,     /* lastAccessTime → last_launch */
    GM_DB_UNINSTALL,  /* uninstallable   → can_uninstall */
    GM_DB_SIZE,       /* size            → appdb_size */
    GM_DB_INSTALL     /* installTime     → install_time */
};
static const char *const GM_DB_COLNAMES[GM_DB_NCOL] = {
    "contentId", "lastAccessTime", "uninstallable", "size", "installTime",
};

typedef struct { uint32_t root; int c[GM_DB_NCOL]; } gm_master_t;

static int st_cb_master_cols(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    gm_master_t *m = (gm_master_t *)arg;
    const uint8_t *tp;
    size_t tl;
    uint64_t num = 0;
    if (st_rec_col(d, sz, rec, 1, &tp, &tl, NULL) != 2 || tl != 15 ||
        memcmp(tp, "tbl_contentinfo", 15) != 0)
        return 0;
    if (st_rec_col(d, sz, rec, 3, NULL, NULL, &num) != 3 || !num) return 0;
    m->root = (uint32_t)num;
    if (st_rec_col(d, sz, rec, 4, &tp, &tl, NULL) == 2 && tl) {
        for (int i = 0; i < GM_DB_NCOL; i++)
            m->c[i] = gm_sql_col_index((const char *)tp, GM_DB_COLNAMES[i]);
    }
    return 1;
}

typedef struct {
    const char *tid;
    /* gm_sql_col_index 从建表 SQL 里解析出来的**真实列序**（GM_DB_* 下标 → 记录列号）。
     * st_cb_row 必须按它取列：固件间 tbl_contentinfo 列序会变，这正是 gm_db_row 要
     * 现查一遍列名的理由。原来这里直接拿 GM_DB_* **枚举序号**当列号用，等于把上面
     * 那套解析整个丢掉 —— content_id 读成第 0 列（titleId）、install_time 读到整型列、
     * appdb_size 读到文本列，详情页那 5 个字段在真机上全部错位。 */
    int col[GM_DB_NCOL];
    char content_id[80];
    char install[40];
    char last[40];
    long long db_size;
    int uninstallable;
    int found;
} gm_row_t;

static int st_cb_row(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    gm_row_t *r = (gm_row_t *)arg;
    const uint8_t *tp;
    size_t tl;
    /* titleId 恒在第 0 列，与 st_cb_enum 同一条规则：9 位才算一个游戏的 titleId。 */
    if (st_rec_col(d, sz, rec, 0, &tp, &tl, NULL) != 2 || tl != 9) return 0;
    char tid[12];
    memcpy(tid, tp, 9);
    tid[9] = 0;
    if (strcmp(tid, r->tid) != 0) return 0;
    r->found = 1;

    uint64_t num = 0;
    /* 一律按 r->col[GM_DB_*]（现查出来的列序）取，**不能**直接用 GM_DB_* 枚举值当列号。 */
    if (st_rec_col(d, sz, rec, r->col[GM_DB_CONTENTID], &tp, &tl, NULL) == 2 && tl && tl < sizeof(r->content_id)) {
        memcpy(r->content_id, tp, tl);
        r->content_id[tl] = 0;
    }
    if (st_rec_col(d, sz, rec, r->col[GM_DB_INSTALL], &tp, &tl, NULL) == 2 && tl && tl < sizeof(r->install)) {
        memcpy(r->install, tp, tl);
        r->install[tl] = 0;
    }
    if (st_rec_col(d, sz, rec, r->col[GM_DB_ACCESS], &tp, &tl, NULL) == 2 && tl && tl < sizeof(r->last)) {
        memcpy(r->last, tp, tl);
        r->last[tl] = 0;
    }
    if (st_rec_col(d, sz, rec, r->col[GM_DB_SIZE], NULL, NULL, &num) == 3) r->db_size = (long long)num;
    if (st_rec_col(d, sz, rec, r->col[GM_DB_UNINSTALL], NULL, NULL, &num) == 3) r->uninstallable = (int)num;
    return 1; /* 主键行，命中即停 */
}

/* app.db 里没有这个 tid 时全部给空值，由调用方自行回落 —— dump 游戏本来就不进库。 */
static void gm_db_row(const char *tid, gm_row_t *r) {
    memset(r, 0, sizeof(*r));
    r->tid = tid;
    r->uninstallable = -1;

    int fd = open(GAME_MGR_APPDB_PATH, O_RDONLY);
    if (fd < 0) return;
    off_t fsz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (fsz < 100 || fsz > (64 << 20)) { close(fd); return; }
    uint8_t *d = (uint8_t *)malloc((size_t)fsz);
    if (!d) { close(fd); return; }
    ssize_t rd = read(fd, d, (size_t)fsz);
    close(fd);
    if (rd < 200 || memcmp(d, "SQLite format 3", 16) != 0) { free(d); return; }
    uint32_t pgsz = st_be16(d + 16);
    if (pgsz == 1) pgsz = 65536;
    if (pgsz < 512 || pgsz > 65536) { free(d); return; }

    gm_master_t m;
    m.root = 0;
    for (int i = 0; i < GM_DB_NCOL; i++) m.c[i] = -1;
    st_bwalk(d, (size_t)rd, 1, pgsz, 0, st_cb_master_cols, &m);
    int all_ok = 1;
    for (int i = 0; i < GM_DB_NCOL; i++) if (m.c[i] < 0) all_ok = 0;
    if (m.root && all_ok) {
        for (int i = 0; i < GM_DB_NCOL; i++) r->col[i] = m.c[i];
        st_bwalk(d, (size_t)rd, m.root, pgsz, 0, st_cb_row, r);
    }
    free(d);
}

/* "1994-12-03 00:00:00.050" → "1994-12-03 00:00"。
 * 前端 formatStamp **原样透传**（见 components/game/format.js），所以这里只截到分钟，
 * 绝不做时区换算 —— 主机时区和浏览器时区经常不一样，来回转换一定把时间改错。 */
static void gm_trim_stamp(const char *s, char *out, size_t osz) {
    out[0] = 0;
    if (!s) return;
    size_t n = strlen(s);
    if (n >= 16 && s[4] == '-' && s[7] == '-' && s[10] == ' ' && s[13] == ':') {
        memcpy(out, s, 16);
        out[16] = 0;
        return;
    }
    snprintf(out, osz, "%s", s);
}

static void gm_stamp_from_mtime(const char *path, char *out, size_t osz) {
    out[0] = 0;
    struct stat st;
    if (stat(path, &st) != 0) return;
    struct tm tmv;
    if (!localtime_r(&st.st_ctime, &tmv)) return;
    strftime(out, osz, "%Y-%m-%d %H:%M", &tmv);
}

/* 源路径 + 运行时路径。两者含义不同（ShadowMount 语义）：
 *   path         物理源在哪 —— 镜像=那个 .ffpkg/.exfat/.ffpfs/.ffpfsc 文件，
 *                裸目录=mount.lnk 记的源目录，压根没注册的 dump=扫描根下的子目录，
 *                真·内置安装=app 目录；
 *   runtime_path ShadowMount 现在把游戏挂在哪 —— mount.lnk 的首行。
 *                没有 mount.lnk 就是空串：前端拿它显示「已挂载」徽标与按钮分工，
 *                凭空编一个 /mnt/shadowmnt/<TID> 会亮出一个点了没反应的按钮。
 *
 * cls 由 gm_class_of_fmt() 给（pkg / image / folder）。**不能**用 a.d_src 代替 ——
 * ShadowMount 的游戏也注册在 app.db 里（d_src 同样是 installed），真机取证：
 * PPSA17599 的物理源是 /mnt/ext1/PPSA17599三十三号.ffpfsc，不是 /user/app/PPSA17599。 */
static void gm_paths(const char *tid, const char *cls,
                     char *path, size_t psz, char *runtime, size_t rsz) {
    char f[320], line[512], appdir[300];
    path[0] = 0;
    runtime[0] = 0;
    snprintf(appdir, sizeof(appdir), "%s/%s", GAME_MGR_APP_BASE, tid);

    /* 物理源优先级：镜像文件 > mount.lnk 记的裸目录 > 扫描根下的 dump 目录 > app 目录。 */
    snprintf(f, sizeof(f), "%s/mount_img.lnk", appdir);
    if (gm_read_lnk(f, line, sizeof(line)) && gm_ext_is_image(line)) {
        snprintf(path, psz, "%s", line);
    } else if (gm_lnk_root(tid, line, sizeof(line))) {
        snprintf(path, psz, "%s", line);
    } else if (!strcmp(cls, "pkg")) {
        snprintf(path, psz, "%s", appdir);
    } else {
        for (size_t i = 0; i < sizeof(GM_SCAN_PATHS) / sizeof(GM_SCAN_PATHS[0]); i++) {
            snprintf(f, sizeof(f), "%s/%s", GM_SCAN_PATHS[i], tid);
            struct stat st;
            if (stat(f, &st) == 0 && S_ISDIR(st.st_mode)) { snprintf(path, psz, "%s", f); break; }
        }
        if (!path[0]) snprintf(path, psz, "%s", appdir);
    }

    snprintf(f, sizeof(f), "%s/mount.lnk", appdir);
    if (gm_read_lnk(f, line, sizeof(line))) snprintf(runtime, rsz, "%s", line);
}

/* [CN-FORK] v0.5.2.132 只读那两个 lnk 文件解析物理源 —— **不做** app.db 枚举。
 *
 * game_mgr_source() 要 gm_lookup（app.db 全库读 + 目录扫描，真机上几十到上百毫秒），
 * 那是给 /game_delete、/game_detail 这种「点一次查一次」的读入口用的。改名是**写路径**，
 * 写之前再枚举一遍纯属浪费，而且 app.db 打不开时它还会误判。
 *
 * 来源类别的判据本来就只有那两个 lnk 文件（见 gm_class_of_fmt / gm_fmt_size 的头注释），
 * 直接读它们：mount_img.lnk 带镜像扩展名 → image；mount.lnk 指非镜像路径 → folder；
 * 都没有 → pkg。分类口径与 gm_fmt_size **共用 gm_ext_is_image / gm_lnk_root**，
 * 没有第三份实现，不会漂移。
 *
 * kind 出 "pkg" | "image" | "folder"；path 出物理源（folder = 源目录，image = 镜像文件，
 * pkg = /user/app/<TID>）。返回 1 = 判定完成（pkg 也算），0 = tid 不合法。 */
int game_mgr_source_paths(const char *tid, char *kind, size_t ksz,
                          char *path, size_t psz) {
    if (kind && ksz) kind[0] = 0;
    if (path && psz) path[0] = 0;
    if (!tid || !gm_tid_is_game(tid)) return 0;

    char appdir[300], f[320], line[512];
    snprintf(appdir, sizeof(appdir), "%s/%s", GAME_MGR_APP_BASE, tid);

    snprintf(f, sizeof(f), "%s/mount_img.lnk", appdir);
    if (gm_read_lnk(f, line, sizeof(line)) && gm_ext_is_image(line)) {
        if (kind && ksz) snprintf(kind, ksz, "image");
        if (path && psz) snprintf(path, psz, "%s", line);
        return 1;
    }
    char root[512];
    if (gm_lnk_root(tid, root, sizeof(root))) {
        if (kind && ksz) snprintf(kind, ksz, "folder");
        if (path && psz) snprintf(path, psz, "%s", root);
        return 1;
    }
    if (kind && ksz) snprintf(kind, ksz, "pkg");
    if (path && psz) snprintf(path, psz, "%s", appdir);
    return 1;
}

/* [CN-FORK] v0.5.2.116 删除前的路径白名单用：path 是否落在某个 dump 扫描根**之下**。
 * 两个边界条件必须卡住：
 *   · `path[n] == '/'` —— 只允许根的**子项**，否则路径恰好等于 /mnt/ext0 时
 *     前缀匹配照样成立，一句 rm 就把整个盘删了；
 *   · 精确前缀比较 + 那个 '/'，所以 /mnt/ext0_backup 不会因为 /mnt/ext0 的前缀而中。 */
int game_mgr_is_scan_path(const char *path) {
    if (!path || path[0] != '/') return 0;
    for (size_t i = 0; i < sizeof(GM_SCAN_PATHS) / sizeof(GM_SCAN_PATHS[0]); i++) {
        size_t n = strlen(GM_SCAN_PATHS[i]);
        if (!strncmp(path, GM_SCAN_PATHS[i], n) && path[n] == '/') return 1;
    }
    return 0;
}

/* 只做「找这一个 tid」的枚举：app.db 优先（与列表同序），找不到再扫 dump。 */
static int gm_lookup(const char *tid, enum_arg_t *a) {
    memset(a, 0, sizeof(*a));
    a->want = tid;
    game_enum_appdb(a);
    if (!a->d_found) game_enum_dump(a);
    return a->d_found;
}

int game_mgr_detail_json(const char *tid, char *buf, size_t size) {
    if (!buf || size < 1024 || !gm_tid_is_game(tid)) return -1;

    enum_arg_t a;
    if (!gm_lookup(tid, &a)) return -1;  /* 不在任何一份枚举里 → 404，别回一份凭空拼的详情 */

    char name[512], esc_name[700];
    game_title_override_name(tid, a.d_name, name, sizeof(name));
    pldmgr_json_escape(name, esc_name, sizeof(esc_name));

    const char *platform = gm_platform(tid);
    /* 来源类别看**物理源**（见 gm_class_of_fmt 的注释），不看 a.d_src：
     * ShadowMount 的游戏也在 app.db 里，用 d_src 判会把它们全标成「已安装 PKG」。 */
    const char *source_type = gm_class_of_fmt(a.d_fmt);
    const char *image_type = !strcmp(source_type, "image") ? a.d_fmt : "";
    int installed_pkg = !strcmp(source_type, "pkg");

    char path[600], runtime[600], ep[800], er[800];
    gm_paths(tid, source_type, path, sizeof(path), runtime, sizeof(runtime));
    pldmgr_json_escape(path, ep, sizeof(ep));
    pldmgr_json_escape(runtime, er, sizeof(er));

    gm_row_t row;
    gm_db_row(tid, &row);

    char content_id[80], ecid[200], version[40], ev[100], install[40], last[40];
    /* version 只有读到 param.sfo 时才被写 —— 下面 sfo_path 为 NULL 那条路一个字节
     * 都不会碰它，不显式清空就直接把栈上垃圾 pldmgr_json_escape 进 JSON 了。 */
    version[0] = 0;
    snprintf(content_id, sizeof(content_id), "%s", row.content_id);

    /* 内容 ID 与版本号都不在 app.db 的这五列里，一律回 param.sfo 取：
     * 已安装走 /user/app/<TID>/sce_sys，dump 走它自己的 <path>/sce_sys。
     * 读不到就空串 —— 契约里那两格本来就是「可能读不到」，前端显示「—」。 */
    char sfo[768];
    const char *sfo_path = NULL;
    if (snprintf(sfo, sizeof(sfo), "%s/%s/sce_sys/param.sfo", GAME_MGR_APP_BASE, tid) < (int)sizeof(sfo) &&
        access(sfo, F_OK) == 0)
        sfo_path = sfo;
    else if (snprintf(sfo, sizeof(sfo), "%s/sce_sys/param.sfo", path) < (int)sizeof(sfo) &&
             access(sfo, F_OK) == 0)
        sfo_path = sfo;
    if (sfo_path) {
        if (!content_id[0]) game_mgr_sfo_text(sfo_path, "CONTENT_ID", content_id, sizeof(content_id));
        if (game_mgr_sfo_text(sfo_path, "APP_VER", version, sizeof(version)) != 0)
            game_mgr_sfo_text(sfo_path, "VERSION", version, sizeof(version));
    }
    pldmgr_json_escape(content_id, ecid, sizeof(ecid));
    pldmgr_json_escape(version, ev, sizeof(ev));

    gm_trim_stamp(row.install, install, sizeof(install));
    /* 兜底两跳：先看物理源（镜像文件 / 源目录）的 mtime，再退回 app 目录 ——
     * 只有 dump 游戏（不在 app.db、没有 app 目录）才靠第一跳，别把它的安装时间弄丢。 */
    if (!install[0]) gm_stamp_from_mtime(path, install, sizeof(install));
    if (!install[0]) {
        char appdir[320];
        if (snprintf(appdir, sizeof(appdir), "%s/%s", GAME_MGR_APP_BASE, tid) < (int)sizeof(appdir))
            gm_stamp_from_mtime(appdir, install, sizeof(install));
    }
    gm_trim_stamp(row.last, last, sizeof(last));

    /* 「已挂载」= 这个游戏此刻真的挂进系统了（照 ShadowMountPlus is_data_mounted）。
     * 内置 PKG 不参与（它没有 ShadowMount 运行时挂载这回事）。 */
    int mounted = !installed_pkg && gm_is_data_mounted(tid);
    int can_uninstall = (row.uninstallable < 0) ? 1 : (row.uninstallable != 0);
    int ic = 0, bc = 0;
    game_title_detail_flags(tid, &ic, &bc);
    int saves = game_saves_count(tid);
    int media = game_media_count(tid);

    int n = snprintf(buf, size,
        "{\"tid\":\"%s\",\"name\":\"%s\",\"src\":\"%s\",\"fmt\":\"%s\",\"size\":%llu,"
        "\"platform\":\"%s\",\"source_type\":\"%s\",\"image_type\":\"%s\","
        "\"content_id\":\"%s\",\"version\":\"%s\",\"path\":\"%s\",\"runtime_path\":\"%s\","
        "\"appdb_size\":%lld,\"install_time\":\"%s\",\"last_launch\":\"%s\","
        "\"mounted\":%s,\"installed_pkg\":%s,\"can_uninstall\":%s,"
        "\"can_manage_source\":%s,\"saves_count\":%d,\"media_count\":%d,"
        "\"title_icon_custom\":%s,\"title_bg_custom\":%s}",
        tid, esc_name, a.d_src, a.d_fmt, a.d_size,
        platform, source_type, image_type,
        ecid, ev, ep, er,
        row.db_size, install, last,
        mounted ? "true" : "false",
        installed_pkg ? "true" : "false",
        can_uninstall ? "true" : "false",
        installed_pkg ? "false" : "true",
        saves, media,
        ic ? "true" : "false",
        bc ? "true" : "false");
    if (n < 0 || (size_t)n >= size) return -1;
    return n;
}

/* ── 给 /game_delete / mount / uninstall 用的三元组：来源类别 + 源路径 + 运行时路径。
 * 路径算法与 /game_detail **共用 gm_paths**，所以「详情说可以删、真删时却找不到」
 * 这种两边算法各写一遍导致的错位不可能出现 —— 而删除是不可逆操作，值得较这个真。
 *
 * src 回的是**来源类别**（pkg / image / folder），不是枚举出处：
 * 调用方要的就是「这是不是一个真·内置安装的 PKG」（/game_delete 拿它挡 403 EPERM）。
 * v0.5.2.127 之前回的是 a.d_src，于是 ShadowMount 的镜像/裸目录游戏（也在 app.db 里）
 * 被当成 PKG，**删源被 403 挡死**，同时详情里的物理路径也错回 app 目录。 */
int game_mgr_source(const char *tid, char *src, size_t ssz,
                    char *path, size_t psz, char *runtime, size_t rsz) {
    if (src && ssz) src[0] = 0;
    if (path && psz) path[0] = 0;
    if (runtime && rsz) runtime[0] = 0;
    if (!tid || !gm_tid_is_game(tid)) return 0;
    enum_arg_t a;
    if (!gm_lookup(tid, &a)) return 0;
    const char *cls = gm_class_of_fmt(a.d_fmt);
    if (src && ssz) snprintf(src, ssz, "%s", cls);
    char tmp_p[600], tmp_r[600];
    gm_paths(tid, cls, tmp_p, sizeof(tmp_p), tmp_r, sizeof(tmp_r));
    if (path && psz) snprintf(path, psz, "%s", tmp_p);
    if (runtime && rsz) snprintf(runtime, rsz, "%s", tmp_r);
    return 1;
}

/* ══════════════ 顶层：mutex + 60s 快照缓存，一次性拉取不轮询 ══════════════ */
static pthread_mutex_t g_game_mtx = PTHREAD_MUTEX_INITIALIZER;

/* 快照缓冲必须与 http_server.c 的 RESPONSE_BUFFER_SIZE **同宽**（那边按 1MB 分响应缓冲，
 * /game_list 就按 1MB 现造 JSON）。
 *
 * 原来这里是 131072（128KB）。游戏一多 JSON 就超过 128KB —— 下面
 * `n < sizeof(g_game_cache)` 恒假，于是**每一份快照都存不进去**、g_game_cache_len
 * 恒 0，60s 缓存等于没有：每次 /game_list 都从头重建（app.db 全库读 + 每款一次
 * 递归 du），真机报的「游戏列表载入超级慢」另一半根因就在这。
 * 128KB 这个数在游戏少的时候看不出问题，装到 150+ 款才炸，属于「装得越多越慢」型。 */
#define GM_SNAPSHOT_MAX (1024 * 1024)
static char    g_game_cache[GM_SNAPSHOT_MAX];
static size_t  g_game_cache_len = 0;
static time_t  g_game_cache_ts  = 0;

/* ── [CN-FORK] v0.5.2.136 后台体积预热 ─────────────────────────────────────────
 *
 * 要解决的问题：folder/dump 游戏的真实体积只能靠递归 du（一款上万文件）。列表里
 * 十几款全扫一遍就是「游戏列表载入超级慢」的主因，所以 .134 让列表跳过 du —— 磁贴
 * 于是没有体积标签；.135 改用 app.db 的 size 列兜底，但**那不是游戏体积**（真机上
 * 132 GB 的博德之门 3 在这里只有 1.22 GB），等于磁贴在报一个偏小且语义错的数，
 * 比不显示更糟。
 *
 * 现在的做法：列表**只读体积缓存**（gm_dir_size_peek，不扫盘）—— 命中的直接显示
 * 真实体积；未命中的先留空，同时把根目录攒进 g_warm_roots。build_game_json 结束时
 * 起下面这个线程去把这些目录扫完（填进同一份缓存），扫完作废 60s 快照；前端凭响应
 * 里的 "warming":true 自动重拉一次 —— 几秒后磁贴体积自己补齐，且与详情页一致。
 *
 * 线程与锁：g_warm_running 与 g_warm_roots 都**只**在持有 g_game_mtx 时读写
 * （build_game_json 全程持锁；worker 只在收尾那一下取锁）。体积缓存另有 g_du_mtx。
 * 锁序恒为 g_game_mtx → g_du_mtx，worker 收尾只取 g_game_mtx、不套 g_du_mtx，无环。 */
static char g_warm_roots[GM_WARM_MAX][512];
static int  g_warm_running = 0;   /* 受 g_game_mtx 保护：同一时刻只允许一个预热线程 */

typedef struct { int n; char (*roots)[512]; } gm_warm_t;

static void *gm_warm_worker(void *arg) {
    gm_warm_t *w = (gm_warm_t *)arg;
    for (int i = 0; i < w->n; i++) gm_dir_size_cached(w->roots[i]);
    free(w->roots);
    free(w);
    /* 扫完作废快照：下一次 /game_list 会用刚填好的缓存重建，磁贴体积补齐。
     * 只碰那三个变量，锁是短持有 —— 不阻塞正在读列表的请求。 */
    pthread_mutex_lock(&g_game_mtx);
    g_game_cache_len = 0;
    g_game_cache_ts  = 0;
    g_warm_running   = 0;
    pthread_mutex_unlock(&g_game_mtx);
    return NULL;
}

/* 在 build_game_json 里调（此时**持有** g_game_mtx）：把这一轮收集到的未命中根目录
 * 拷给后台线程。已有线程在跑就跳过 —— 它跑完会作废快照，下一轮重建时再收集一轮。 */
static void gm_warm_kick(const enum_arg_t *a) {
    if (a->warm_n <= 0 || g_warm_running) return;
    gm_warm_t *w = (gm_warm_t *)malloc(sizeof(*w));
    if (!w) return;
    w->roots = (char (*)[512])malloc((size_t)a->warm_n * sizeof(*w->roots));
    if (!w->roots) { free(w); return; }
    w->n = a->warm_n;
    memcpy(w->roots, g_warm_roots, (size_t)w->n * sizeof(*w->roots));
    /* 先立标志再起线程：否则线程可能已经跑完并把 running 清 0，我们再置 1，
     * 那个 1 就永远挂着了（此后再也不预热）。持锁期间 worker 收尾那步进不来。 */
    g_warm_running = 1;
    pthread_t th;
    if (pthread_create(&th, NULL, gm_warm_worker, w) != 0) {
        g_warm_running = 0;
        free(w->roots);
        free(w);
        return;
    }
    pthread_detach(th);
}

static size_t build_game_json(char *buf, size_t size) {
    size_t pos = snprintf(buf, size, "{\"games\":[");
    if (pos >= size) return size - 1;
    enum_arg_t a;
    /* **整体清零，别逐字段挑** —— 这一行就是「游戏列表整个空掉」的根。
     *
     * v0.5.2.116 给这个结构体加了 want（详情模式：非 NULL 时只认这一个 tid、
     * 一个字节都不写 buf），而这里一直沿用逐字段赋值，漏了它 → want 成了栈上垃圾。
     * gm_append_item 第一行是 `if (a->want)`，垃圾非空时**每一个条目都早退**，
     * /game_list 于是回 {"games":[],"count":0}。
     *
     * 最迷惑的是它「先好后坏」：/game_detail 走的 gm_lookup 会 memset 再把 want
     * 指向 tid，同一 MHD 工作线程里跑过一次详情，这块栈就留下一个非空指针，
     * 此后列表一直空着 —— 不是每次都复现，是用着用着才空。
     *
     * 当时一条断言都没有：这个文件主机上要 getmntinfo()（BSD 专有）编不过，
     * 而报错又是「正常 200 + 空数组」不是崩，只看接口状态码根本看不出来。
     * 同一版补了 test/host_mnt_compat.h 顶掉那三个 BSD 符号 + 合成 app.db 夹具，
     * 现在 test/game_mgr_test.c 会**主动把这段栈播种成非空指针**再取列表 ——
     * 拿掉 memset 那行，它那条 count==2 立刻红。 */
    memset(&a, 0, sizeof(a));
    a.buf = buf; a.size = size; a.pos = pos;
    a.skip_du = 1;   /* 列表模式：folder 游戏只读体积缓存、不扫盘（见 enum_arg_t.skip_du 注释） */
    a.warm = g_warm_roots; a.warm_n = 0;   /* 收集缓存未命中的根目录，供后台预热 */
    game_enum_appdb(&a);   /* 已安装优先（格式经 gm_fmt 读 mount.lnk/mount_img.lnk 判定） */
    game_enum_dump(&a);    /* 再补扫描到的 dump，tid 去重 */
    gm_warm_kick(&a);      /* 有未命中的就起后台线程扫，扫完作废快照让前端重拉 */
    pos = a.pos;
    if (pos < size)
        pos += snprintf(buf + pos, size - pos, "],\"count\":%d%s%s}",
                        a.count, a.overflow ? ",\"truncated\":true" : "",
                        /* 前端据此在几秒后自动重拉一次，把预热好的体积显示出来。
                         * 没有预热在跑就不写这个字段（缺席 = false）。 */
                        g_warm_running ? ",\"warming\":true" : "");
    if (pos >= size) pos = size - 1;
    return pos;
}

int game_mgr_list_json(char *buf, size_t size, int force) {
    if (!buf || size < 64) return 0;
    pthread_mutex_lock(&g_game_mtx);
    time_t now = time(NULL);
    if (!force && g_game_cache_len && now >= g_game_cache_ts && (now - g_game_cache_ts) < 60) {
        size_t n = g_game_cache_len;
        if (n > size - 1) n = size - 1;
        memcpy(buf, g_game_cache, n);
        buf[n] = '\0';
        pthread_mutex_unlock(&g_game_mtx);
        return (int)n;
    }
    size_t n = build_game_json(buf, size);
    if (n > 0 && n < sizeof(g_game_cache) && n < size) {
        memcpy(g_game_cache, buf, n);
        g_game_cache[n] = '\0';
        g_game_cache_len = n;
        g_game_cache_ts  = now;
    } else {
        g_game_cache_len = 0;
    }
    pthread_mutex_unlock(&g_game_mtx);
    return (int)n;
}

/* 让 60s 的列表缓存立刻过期。删除/卸载之后**必须**调：否则「关掉弹窗、过一会儿
 * 再刷新列表」这条最真实的路径会读到一份还包含这个游戏的旧列表，用户据此判断
 * 「点了删除没反应」。mock 里靠 setTimeout 改内存状态达成的效果，真机靠这里。 */
void game_mgr_invalidate(void) {
    pthread_mutex_lock(&g_game_mtx);
    g_game_cache_len = 0;
    g_game_cache_ts = 0;
    pthread_mutex_unlock(&g_game_mtx);
}

time_t game_mgr_cache_ts(void) {
    pthread_mutex_lock(&g_game_mtx);
    time_t ts = g_game_cache_ts;
    pthread_mutex_unlock(&g_game_mtx);
    return ts;
}
