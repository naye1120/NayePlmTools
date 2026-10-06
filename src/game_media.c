/* [CN-FORK] v0.5.2.115 游戏管理 → 多媒体管理（截图 / 录屏）后端
 *
 * 契约、真机取证到的存储布局、两条铁律与 §7 待验证项，全写在 include/game_media.h
 * 头注释里 —— 那里是唯一事实来源，这个文件只写「实现上为什么这样写」。
 *
 * 四块，从上到下：
 *   ① 极简 SQLite 只读解析（与 game_mgr.c 的 st_* 同算法的第二份，刻意为之，理由见下）
 *   ② id 校验 / 路径解析 / 列表 / 定位
 *   ③ 缩略图：系统缩略图 → 按需缩放 → 落盘缓存
 *   ④ 删除 + 打包任务（单 worker + 自研 STORE zip，SDK 里没有 zlib）
 *
 * 内存约定：大文件一律把 fd 交给 MHD（它会替我们 close），绝不整段读进内存 ——
 * 录屏动辄几百 MB，读进内存就是把面板 OOM 掉。只有缩略图（几十 KB）走内存。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "game_media.h"
#include "game_title.h"   /* game_title_get_name（媒体项命名 / 压缩包命名） */
#include "zip_io.h"       /* v0.5.2.116 zip 编码器已搬出去（存档下载要共用） */
#include "game_job.h"     /* v0.5.2.116 任务槽已搬进 game_job.c（删除任务要共用） */
#include "pldmgr.h"       /* pldmgr_log / pldmgr_json_escape（经 utils.h） */
#include "json_helpers.h" /* ensure_dir_recursive */
#include "stb_image.h"
#include "stb_image_write.h"
#include "cJSON.h"

#define MD_MAX_ITEMS   8192          /* 一次扫描最多收这么多行（防坏库把内存吃穿） */
#define MD_SRC_MAX     (64u << 20)   /* 缩略图源图上限 64MB */
#define MD_THUMB_MAX   800           /* 缩略图缓存条数上限 */
#define MD_THUMB_KEEP  600           /* 超限后保留最新这么多 */
#define MD_THUMB_Q     82            /* JPEG 质量 */
#define MD_SQL_TMP     8192          /* 建表 SQL 暂存（tbl_PhotoVideo 约 1.7KB） */

/* ══════════════ ① 极简 SQLite 只读解析 ══════════════
 *
 * ⚠️ 这一段与 game_mgr.c 的 st_* 是**同一套算法的第二份**，不是笔误。
 * game_mgr.c 那份是 `static`，且**没有任何主机回归测试兜底**（test/run_host_tests.sh
 * 只覆盖 slot_resolve / offact_genid / safety_origin 三个），把它抽成公共文件就等于
 * 拿「游戏列表 + deeplink 同步」这两条已验证的路径去换 130 行不重复。
 * 仓库里同类取舍已有先例：gameApi.js 与 AccountToolsSection 各留一份 fetchWithTimeout，
 * 头注释写明「抽出去要动已验证过的代码，收益不值这个风险」。
 * 所以：**这里改了 bug，务必回去看 game_mgr.c 的对应位置**；反之亦然。 */

#define MD_GVS ((uint64_t)~0ull)

static uint64_t md_gvar(const uint8_t *d, size_t sz, size_t *o) {
    uint64_t v = 0;
    for (int i = 0; i < 9; i++) {
        if (*o >= sz) return MD_GVS;
        uint8_t b = d[(*o)++];
        if (i == 8) return (v << 8) | b;
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) return v;
    }
    return MD_GVS;
}

static uint16_t md_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t md_be32(const uint8_t *p) { return (uint32_t)(md_be16(p) << 16 | md_be16(p + 2)); }

/* 取记录第 idx 列：返回 2=字符串(写 tp/tl)，3=整数(写 num)，4=浮点(写 dv)，
 * 0=NULL，-1=错。
 *
 * 序列类型不能想当然写成 len=st —— 只有 1..4 的整数是「长度==类型号」，
 * 5 是 48 位（6 字节）、6 是 64 位（8 字节）、7 是 IEEE754 double（8 字节）。
 * 按 len=st 读会在 duration 是 REAL 列时直接错位，把后面所有列读成乱码。 */
static int md_rec_col(const uint8_t *d, size_t sz, size_t ro, int idx,
                      const uint8_t **tp, size_t *tl, uint64_t *num, double *dv) {
    size_t o = ro;
    uint64_t hsz = md_gvar(d, sz, &o);
    if (hsz == MD_GVS || hsz == 0 || ro + hsz > sz) return -1;
    size_t bo = ro + hsz;
    for (int i = 0; i <= idx; i++) {
        uint64_t st = md_gvar(d, sz, &o);
        if (st == MD_GVS) return -1;
        size_t len;
        int kind;
        uint64_t small = 0;
        int hassmall = 0;
        if (st == 0) { len = 0; kind = 0; }
        else if (st >= 1 && st <= 4) { len = st; kind = 3; }
        else if (st == 5) { len = 6; kind = 3; }
        else if (st == 6 || st == 7) { len = 8; kind = st == 7 ? 4 : 3; }
        else if (st == 8) { len = 0; kind = 3; small = 0; hassmall = 1; }
        else if (st == 9) { len = 0; kind = 3; small = 1; hassmall = 1; }
        /* ⚠️ 这行看着像写反了（`(st & 1)` 取到的是奇数却给「字符串」的 2），**没写反**：
         * SQLite 的 TEXT 是**奇数** serial type、BLOB 是**偶数**。
         * 实测（sqlite3 现造表后把记录字节拆开数）：TEXT 'hello' → 23、TEXT 'x'*100 → 213、
         * TEXT '' → 13；BLOB b'\x01\x02' → 16、BLOB b'' → 12。
         * 长度公式 (st-12)/2 对奇偶都成立（奇数走 (st-13)/2 结果相同）。
         * 别「顺手修正」它 —— game_mgr.c 读 app.db 也靠这一条，改了两个模块一起哑。 */
        else if (st >= 12) { len = (st - 12) / 2; kind = (st & 1) ? 2 : 1; }
        else { len = 0; kind = 0; } /* 10/11 保留位，当作 NULL 跳过，不能当成整数 */
        if (bo + len > sz) return -1;
        if (i == idx) {
            if (kind == 2) {
                if (tp) *tp = d + bo;
                if (tl) *tl = len;
                return 2;
            }
            if (kind == 3) {
                if (num) {
                    if (hassmall) *num = small;
                    else {
                        uint64_t v = 0;
                        for (size_t k = 0; k < len; k++) v = v << 8 | d[bo + k];
                        *num = v;
                    }
                }
                return 3;
            }
            if (kind == 4) {
                /* 库里是大端 IEEE754；按大端拼成 uint64 再写进 union，
                 * 在小端主机（PS5 / x86 都是）上正好得到正确的 double。 */
                if (dv) {
                    uint64_t bits = 0;
                    for (size_t k = 0; k < 8; k++) bits = bits << 8 | d[bo + k];
                    union { uint64_t u; double f; } cv;
                    cv.u = bits;
                    *dv = cv.f;
                }
                return 4;
            }
            return kind == 0 ? 0 : -1;
        }
        bo += len;
    }
    return -1;
}

/* 数值列的统一读法：整数与浮点都能取，取不到一律 0（宽度/时长读不出来只是不显示） */
static double md_num(const uint8_t *d, size_t sz, size_t rec, int idx) {
    uint64_t num = 0;
    double dv = 0.0;
    int k = md_rec_col(d, sz, rec, idx, NULL, NULL, &num, &dv);
    if (k == 3) return (double)num;
    if (k == 4) return dv;
    return 0.0;
}

typedef int (*md_rec_cb)(const uint8_t *, size_t, size_t, void *);

static int md_bwalk(const uint8_t *d, size_t sz, uint32_t pg, uint32_t pgsz, int depth,
                    md_rec_cb cb, void *arg) {
    uint64_t off64 = (uint64_t)(pg - 1) * pgsz;
    if (!pg || pg > 2000000 || off64 + 120 > sz) return 0;
    size_t off = (size_t)off64;
    size_t hdr = off + (pg == 1 ? 100 : 0);
    uint8_t flags = d[hdr];
    int ncell = md_be16(d + hdr + 3);
    if (ncell > 4096) return 0;
    if (flags != 13 && flags != 5) return 0;
    if (flags == 5 && depth >= 6) return 0;
    size_t cpt = hdr + (flags == 5 ? 12 : 8);
    for (int i = 0; i < ncell; i++) {
        size_t cp = cpt + (size_t)i * 2;
        if (cp + 2 > sz) return 0;
        size_t po = off + md_be16(d + cp);
        if (po + 2 > sz) return 0;
        if (flags == 13) {
            size_t ro = po;
            if (md_gvar(d, sz, &ro) == MD_GVS || md_gvar(d, sz, &ro) == MD_GVS) continue;
            if (cb(d, sz, ro, arg)) return 1;
        } else {
            if (md_bwalk(d, sz, md_be32(d + po), pgsz, depth + 1, cb, arg)) return 1;
        }
    }
    if (flags == 5) {
        uint32_t rm = md_be32(d + hdr + 8);
        if (md_bwalk(d, sz, rm, pgsz, depth + 1, cb, arg)) return 1;
    }
    return 0;
}

/* 从建表 SQL 里定位列序号：数第一个 '(' 到列名之间「顶层逗号」个数。
 * 列序在固件/版本间可能变，所以只认列名、不写死序号（同 game_mgr.c 的 st_sql_col_index）。
 * 前后都要挡：icon_width 会撞 width、appli_title_id 会撞 title。 */
static int md_sql_col_index(const char *sql, size_t sl, const char *col) {
    char tmp[MD_SQL_TMP];
    size_t n = sl < sizeof(tmp) - 1 ? sl : sizeof(tmp) - 1;
    memcpy(tmp, sql, n);
    tmp[n] = 0;
    const char *lp = strchr(tmp, '(');
    if (!lp) return -1;
    size_t cl = strlen(col);
    const char *cp = lp + 1;
    for (;;) {
        cp = strstr(cp, col);
        if (!cp) return -1;
        if (!isalnum((unsigned char)cp[-1]) && cp[-1] != '_' &&
            (cp[cl] == 0 || (!isalnum((unsigned char)cp[cl]) && cp[cl] != '_')))
            break;
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

typedef struct {
    const char *name;
    uint32_t root;
    const uint8_t *sql;
    size_t sql_len;
} md_master_t;

/* sqlite_master 行：col1=name, col3=rootpage, col4=sql */
static int md_cb_master(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    md_master_t *m = (md_master_t *)arg;
    const uint8_t *tp;
    size_t tl;
    if (md_rec_col(d, sz, rec, 1, &tp, &tl, NULL, NULL) != 2) return 0;
    size_t n = strlen(m->name);
    if (tl != n || memcmp(tp, m->name, n) != 0) return 0;
    uint64_t num = 0;
    if (md_rec_col(d, sz, rec, 3, NULL, NULL, &num, NULL) != 3 || !num) return 0;
    m->root = (uint32_t)num;
    if (md_rec_col(d, sz, rec, 4, &tp, &tl, NULL, NULL) == 2 && tl) {
        m->sql = tp;
        m->sql_len = tl;
    }
    return 1;
}

/* ── 库镜像缓存：整份 ~180KB 常驻，按 (size, mtime) 判失效 ──
 * mtime 秒级判据的已知盲区：同一秒内改了两次且总长没变 → 拿到旧镜像。
 * 媒体库只在截到新图/删图时才变，而新图落盘本身要花几百毫秒，撞不上；
 * 真撞上了也只是这一次列表少一条，下一次打开自愈 —— 比每次请求重读 180KB 便宜得多。 */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *g_img;
static size_t g_img_len;
static off_t g_img_size;
static time_t g_img_mtime;
static int g_img_ready;

/* 调用方必须持 g_mu */
static int md_img_locked(void) {
    struct stat st;
    if (stat(GAME_MEDIA_DB_PATH, &st) != 0) {
        free(g_img);
        g_img = NULL;
        g_img_len = 0;
        g_img_ready = 0;
        return -1;
    }
    if (g_img_ready && g_img_size == st.st_size && g_img_mtime == st.st_mtime) return 0;

    int fd = open(GAME_MEDIA_DB_PATH, O_RDONLY);
    if (fd < 0) return -1;
    if (st.st_size < 100 || st.st_size > (off_t)(64 << 20)) {
        close(fd);
        pldmgr_log("[PLDMGR] media: av_content.db bad size %lld\n", (long long)st.st_size);
        return -1;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)st.st_size);
    if (!buf) { close(fd); return -1; }
    size_t want = (size_t)st.st_size, got = 0;
    while (got < want) {
        ssize_t r = read(fd, buf + got, want - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    if (got < 200 || memcmp(buf, "SQLite format 3", 16) != 0) {
        free(buf);
        return -1;
    }
    free(g_img);
    g_img = buf;
    g_img_len = got;
    g_img_size = st.st_size;
    g_img_mtime = st.st_mtime;
    g_img_ready = 1;
    return 0;
}

typedef struct {
    int appli, path, icon, w, h, dur, durms;
} md_cols_t;

/* 调用方必须持 g_mu；成功返回 0。
 * ⚠️ 一次要带回**两个**东西：列序 + tbl_PhotoVideo 的 root page。
 * 拿到列序就直接 walk page 1 是错的 —— page 1 是 sqlite_master，里面是建表语句不是媒体行
 * （这一条当时漏了，表现是「库读到了、列也认得，可一条都扫不出来」）。 */
static int md_cols_locked(const uint8_t *d, size_t sz, uint32_t *pgsz_out,
                          uint32_t *root_out, md_cols_t *c) {
    if (sz < 200 || memcmp(d, "SQLite format 3", 16) != 0) return -1;
    uint32_t pgsz = md_be16(d + 16);
    if (pgsz == 1) pgsz = 65536;
    if (pgsz < 512 || pgsz > 65536) return -1;

    md_master_t m;
    memset(&m, 0, sizeof(m));
    m.name = "tbl_PhotoVideo";
    md_bwalk(d, sz, 1, pgsz, 0, md_cb_master, &m);
    if (!m.root || !m.sql) {
        pldmgr_log("[PLDMGR] media: tbl_PhotoVideo not found\n");
        return -1;
    }
    static const char *const want[7] = {
        "appli_title_id", "content_path", "icon_path",
        "width", "height", "duration", "duration_ms",
    };
    int *slots[7];
    slots[0] = &c->appli; slots[1] = &c->path; slots[2] = &c->icon;
    slots[3] = &c->w;     slots[4] = &c->h;    slots[5] = &c->dur;
    slots[6] = &c->durms;
    for (int i = 0; i < 7; i++) {
        *slots[i] = md_sql_col_index((const char *)m.sql, m.sql_len, want[i]);
        if (*slots[i] < 0) {
            pldmgr_log("[PLDMGR] media: column %s missing\n", want[i]);
            return -1;
        }
    }
    *root_out = m.root;
    *pgsz_out = pgsz;
    return 0;
}

/* ══════════════ ② id 校验 / 路径解析 / 列表 ══════════════ */

/* content_path 白名单。同一段代码同时用在**库读出来的路径**和**前端传来的 id** 上 ——
 * 前者防库被改坏，后者防路径穿越，只堵一头等于没堵。 */
static int md_path_ok(const char *p) {
    if (!p || p[0] != '/') return 0;
    size_t n = strlen(p);
    if (n < 16 || n >= 640) return 0;
    if (strncmp(p, "/av_contents/", 13) != 0) return 0;
    if (strstr(p, "..") || strstr(p, "//") || strchr(p, '\\')) return 0;
    int slashes = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c == 0x7f) return 0; /* 控制字符：会把 JSON 与日志打穿 */
        if (c == '/') slashes++;
    }
    /* 真实布局是 /av_contents/<photo|video>/<生成器>/<TID>/<hex>/<文件> = 6 段；
     * 放宽到 4..9 段，免得固件哪天多插一层就把功能整体打死。 */
    return slashes >= 4 && slashes <= 9;
}

/* rel → 绝对路径，并要求最终落在四个媒体根之下。 */
static int md_abs(const char *rel, char *out, size_t osz) {
    if (!md_path_ok(rel)) return -1;
    int n = snprintf(out, osz, GAME_MEDIA_USER_PREFIX "%s", rel);
    if (n < 0 || (size_t)n >= osz) return -1;
    static const char *const roots[] = {
        GAME_MEDIA_ROOT "/photo/",
        GAME_MEDIA_ROOT "/video/",
        GAME_MEDIA_ROOT "/thumbnails/photo/",
        GAME_MEDIA_ROOT "/thumbnails/video/",
    };
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        if (strncmp(out, roots[i], strlen(roots[i])) == 0) return 0;
    }
    return -1;
}

static void md_basename(const char *rel, char *out, size_t osz) {
    const char *sl = strrchr(rel, '/');
    snprintf(out, osz, "%s", sl ? sl + 1 : rel);
}

/* ── 命名（v0.5.2.122）──────────────────────────────────────────────────────
 * 用户口径：「截图下载和视频下载的命名方式是游戏名称-截图时间。压缩包也是
 * 游戏名称-打包时间戳」。三件事因此各有一份名字，别混：
 *
 *   it.name   「游戏名称-2026-10-06 12:59」  界面显示（人看的，可以有空格）
 *   it.dlname 「游戏名称-20261006_125900.png」 下载落盘（必须文件名安全）
 *   压缩包     「游戏名称-20261006_130512.zip」 打包时间戳
 *
 * 为什么下载名不用显示名那版：Windows 与 PS5 的 Shell 都不接受 ':'，
 * 而游戏名里带冒号是常态（「蜘蛛侠：迈尔斯」）；空格虽合法，但在下载目录里
 * 与命令行里都别扭。所以落盘一律用紧凑的 YYYYMMDD_HHMMSS。 */

/* 扩展名：小写、只收字母数字。取不到就给 "bin" —— 宁可后缀难看，
 * 也不能给空后缀（浏览器会自己猜，猜成 .zip 更尴尬）。 */
static void md_ext_of(const char *path, char *out, size_t osz) {
    out[0] = 0;
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1]) { snprintf(out, osz, "bin"); return; }
    size_t k = 0;
    for (const char *p = dot + 1; *p && k + 1 < osz; p++) {
        if (!isalnum((unsigned char)*p)) break;
        out[k++] = (char)tolower((unsigned char)*p);
    }
    out[k] = 0;
    if (!out[0]) snprintf(out, osz, "bin");
}

/* 把任意文本清成能当文件名的样子：空格 / 冒号 / 斜杠 / 引号 / 通配符 / 控制字符
 * 一律换 '-'。不清就是「下载失败」而不是「名字难看」。 */
static void md_sanitize_name(char *s) {
    for (int i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == ':' || c == '/' || c == '\\' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*' || c < 0x20 || c == 0x7f)
            s[i] = '-';
    }
}

/* [CN-FORK] v0.5.2.126 按 UTF-8 字符边界截断（不是按字节）。
 * 用途只有一个：给压缩包顶层文件夹名兜个长度上限。直接 s[max]=0 会把一个汉字
 * 劈成半个（解压出来是乱码），所以先退到不是 0b10xxxxxx 的那个字节上。 */
static void md_utf8_cap(char *s, size_t max) {
    size_t n = strlen(s);
    if (n <= max) return;
    n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
    s[n] = 0;
}

/* 时间戳：本地时区 YYYYMMDD_HHMMSS（与列表里显示的那个时区一致，
 * 玩家才对得上「我什么时候截的」）。 */
static void md_stamp_compact(time_t mt, char *out, size_t osz) {
    struct tm tmv;
    if (localtime_r(&mt, &tmv)) strftime(out, osz, "%Y%m%d_%H%M%S", &tmv);
    if (!out[0]) snprintf(out, osz, "unknown");
}

/* 「游戏名称-YYYYMMDD_HHMMSS.<ext>」；game 为空则退回原文件名（含扩展名）。 */
static void md_dlname(const char *game, const char *content, time_t mt,
                      char *out, size_t osz) {
    if (!game || !game[0]) {
        char base[256];
        md_basename(content, base, sizeof(base));
        snprintf(out, osz, "%s", base);
        return;
    }
    char ext[24];
    md_ext_of(content, ext, sizeof(ext));
    char g[256];
    snprintf(g, sizeof(g), "%s", game);
    md_sanitize_name(g);
    char ts[32] = "";
    md_stamp_compact(mt, ts, sizeof(ts));
    snprintf(out, osz, "%s-%s.%s", g, ts, ext);
}

/* tid 只用来做等值比较（不是 SQL，没有注入面），但仍要挡掉能把 JSON/日志打穿的字符 */
static int md_tid_ok(const char *tid) {
    if (!tid) return 0;
    size_t n = strlen(tid);
    if (n < 4 || n > 16) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)tid[i])) return 0;
    return 1;
}

typedef int (*md_item_cb)(const game_media_item_t *it, void *user);

typedef struct {
    const char *tid;
    const char *find; /* content_path 精确匹配，命中即停 */
    md_cols_t cols;
    md_item_cb cb;
    void *user;
    int n;
    char name[256]; /* 游戏名称，用于媒体项命名（游戏名称-截图时间） */
} md_scan_t;

static int md_cb_row(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    md_scan_t *s = (md_scan_t *)arg;
    const uint8_t *tp;
    size_t tl;

    /* 先做最便宜的那道筛：tid 不匹配就整行跳过，一个字节都不 stat */
    if (s->tid) {
        if (md_rec_col(d, sz, rec, s->cols.appli, &tp, &tl, NULL, NULL) != 2) return 0;
        size_t tn = strlen(s->tid);
        if (tl != tn || memcmp(tp, s->tid, tn) != 0) return 0;
    }

    if (md_rec_col(d, sz, rec, s->cols.path, &tp, &tl, NULL, NULL) != 2 || !tl || tl >= 640)
        return 0;
    game_media_item_t it;
    memset(&it, 0, sizeof(it));
    memcpy(it.content, tp, tl);
    it.content[tl] = 0;
    if (!md_path_ok(it.content)) return 0;
    if (s->find && strcmp(it.content, s->find) != 0) return 0;

    char abs[700];
    if (md_abs(it.content, abs, sizeof(abs)) != 0) return 0;
    struct stat sb;
    if (stat(abs, &sb) != 0 || !S_ISREG(sb.st_mode)) return 0; /* 库里有、盘上没了 → 不列表 */

    it.video = strncmp(it.content, "/av_contents/video/", 19) == 0;
    it.size = (long long)sb.st_size;
    /* ⚠️ 时间戳必须**先**算出来：下面那条命名要用它。
     * .121 初版把这段挪到了命名之后，于是 snprintf 拼的是 memset 过的空 stamp，
     * 每个名字都成了「游戏名称-」（尾巴空掉、只剩一个连字符）。 */
    {
        struct tm tmv;
        if (localtime_r(&sb.st_mtime, &tmv)) {
            strftime(it.stamp, sizeof(it.stamp), "%Y-%m-%d %H:%M", &tmv);
        }
    }
    /* [CN-FORK] v0.5.2.121 媒体项命名：游戏名称-截图时间（如「宇宙机器人-2026-10-06 12:59」）。
     * 取不到游戏名（没改过名 / name.txt 不在）就退回文件名 —— 至少能认出是哪一张。 */
    if (s->name[0]) {
        snprintf(it.name, sizeof(it.name), "%s-%s", s->name, it.stamp);
    } else {
        md_basename(it.content, it.name, sizeof(it.name));
    }
    /* 下载落盘用的名字（与显示名刻意不同，理由见 md_dlname 上面那段） */
    md_dlname(s->name[0] ? s->name : NULL, it.content, sb.st_mtime,
              it.dlname, sizeof(it.dlname));

    if (md_rec_col(d, sz, rec, s->cols.icon, &tp, &tl, NULL, NULL) == 2 &&
        tl && tl < sizeof(it.icon)) {
        memcpy(it.icon, tp, tl);
        it.icon[tl] = 0;
        /* 缩略图路径坏了只是没缩略图，不该拖垮整行 */
        if (!md_path_ok(it.icon)) it.icon[0] = 0;
    }

    double w = md_num(d, sz, rec, s->cols.w);
    double h = md_num(d, sz, rec, s->cols.h);
    double dur = md_num(d, sz, rec, s->cols.dur);
    double durms = md_num(d, sz, rec, s->cols.durms);
    it.w = (w > 0 && w < 40000) ? (int)w : 0;
    it.h = (h > 0 && h < 40000) ? (int)h : 0;
    /* 单位见 game_media.h §7b：duration_ms 非零就用它（毫秒），否则退回 duration（按秒）。
     * 实机当时只有截图、两列都是 0，视频这一支还没验过。 */
    it.duration = durms > 0 ? durms / 1000.0 : dur;

    s->n++;
    if (s->cb && s->cb(&it, s->user)) return 1; /* 回调要求停 */
    if (s->n >= MD_MAX_ITEMS) return 1;
    return 0;
}

/* 持锁扫描。返回 0 = 库读到了（哪怕一行没有），-1 = 库不可读。 */
static int md_scan_locked(const char *tid, const char *find, md_item_cb cb, void *user) {
    if (md_img_locked() != 0) return -1;
    uint32_t pgsz = 0, root = 0;
    md_cols_t cols;
    if (md_cols_locked(g_img, g_img_len, &pgsz, &root, &cols) != 0) return -1;
    md_scan_t s;
    memset(&s, 0, sizeof(s));
    s.tid = tid;
    s.find = find;
    s.cols = cols;
    s.cb = cb;
    s.user = user;
    /* 填充游戏名称，供 md_cb_row 命名媒体项（游戏名称-截图时间） */
    if (tid && md_tid_ok(tid)) {
        game_title_get_name(tid, s.name, sizeof(s.name));
    }
    /* 两次 walk，和 game_mgr.c 一模一样：page 1 只是 sqlite_master，媒体行在 root 页上 */
    md_bwalk(g_img, g_img_len, root, pgsz, 0, md_cb_row, &s);
    return 0;
}

/* ── 列表 → JSON ──
 * 计数要跟在 items 后面（扫完才知道条数），所以先写 head、回调往里追加、最后补尾巴。
 * 分隔符靠 first 标志 —— head 已经把 pos 顶成正数了，拿 pos>0 判首项会给第一项多打一个逗号。 */
typedef struct {
    char *buf;
    size_t cap;
    size_t pos;
    int first;
    int count;
    int overflow;
} md_json_t;

static int md_item_to_json(const game_media_item_t *it, void *user) {
    md_json_t *j = (md_json_t *)user;
    char epath[1400], ename[640], edl[700], item[3200];
    pldmgr_json_escape(it->content, epath, sizeof(epath));
    pldmgr_json_escape(it->name, ename, sizeof(ename));
    pldmgr_json_escape(it->dlname, edl, sizeof(edl));
    int n = snprintf(item, sizeof(item),
                     "%s{\"id\":\"%s\",\"kind\":\"%s\",\"name\":\"%s\",\"dlname\":\"%s\","
                     "\"mtime\":\"%s\",\"size\":%lld,\"w\":%d,\"h\":%d,\"duration\":%.1f}",
                     j->first ? "" : ",",
                     epath, it->video ? "video" : "screenshot", ename, edl, it->stamp,
                     it->size, it->w, it->h, it->duration);
    if (n < 0 || (size_t)n >= sizeof(item)) return 0;
    if (j->pos + (size_t)n >= j->cap) { j->overflow = 1; return 1; }
    memcpy(j->buf + j->pos, item, (size_t)n);
    j->pos += (size_t)n;
    j->first = 0;
    j->count++;
    return 0;
}

int game_media_list_json(const char *tid, char *buf, size_t size) {
    if (!buf || size < 64 || !md_tid_ok(tid)) return -1;

    md_json_t j;
    memset(&j, 0, sizeof(j));
    j.buf = buf;
    j.cap = size;
    j.first = 1;

    int hn = snprintf(buf, size, "{\"tid\":\"%s\",\"items\":[", tid);
    if (hn < 0 || (size_t)hn >= size) return -1;
    j.pos = (size_t)hn;

    pthread_mutex_lock(&g_mu);
    int rc = md_scan_locked(tid, NULL, md_item_to_json, &j);
    pthread_mutex_unlock(&g_mu);
    if (rc != 0) return -1;

    int tail = snprintf(buf + j.pos, size - j.pos, "],\"count\":%d%s}",
                        j.count, j.overflow ? ",\"truncated\":true" : "");
    if (tail < 0 || (size_t)tail >= size - j.pos) return -1;
    j.pos += (size_t)tail;
    return (int)j.pos;
}

/* ── 条数（/game_detail 的 media_count 用）────────────────────────────────
 * 走的是同一把锁、同一个 md_scan_locked，只是回调换成计数 —— 不复用
 * game_media_list_json 是因为那条路要写几百 KB 的 JSON 再解析回去，
 * 详情页每次打开都白烧一遍。计数本身仍要扫全表，这是拿准确值的代价，
 * 但比「拼 JSON + 找 count 字段」省掉一次大内存分配与一次 memcpy。
 *
 * [CN-FORK] v0.5.2.119 加缓存：列表渲染时每个游戏都会调一次 game_media_count，
 * 每次都扫全表会导致列表极慢。缓存 600 秒，媒体库变化时通过版本号失效。 */
typedef struct { int count; int cap; } md_count_t;
static int md_count_cb(const game_media_item_t *it, void *user) {
    md_count_t *c = (md_count_t *)user;
    (void)it;
    c->count++;
    return c->count >= c->cap ? 1 : 0; /* 到顶就停，别为一个「够大了」的数字扫到底 */
}

#define MD_COUNT_CACHE_MAX 128
#define MD_COUNT_CACHE_TTL 600

typedef struct {
    char tid[32];
    int count;
    time_t ts;
    unsigned long long ver;
} md_count_cache_t;

static md_count_cache_t g_count_cache[MD_COUNT_CACHE_MAX];
static int g_count_cache_n = 0;
static unsigned long long g_media_version = 0;

int game_media_count(const char *tid) {
    if (!md_tid_ok(tid)) return 0;
    
    /* 查缓存 */
    time_t now = time(NULL);
    for (int i = 0; i < g_count_cache_n; i++) {
        if (strcmp(g_count_cache[i].tid, tid) == 0 && 
            now - g_count_cache[i].ts < MD_COUNT_CACHE_TTL &&
            g_count_cache[i].ver == g_media_version) {
            return g_count_cache[i].count;
        }
    }
    
    /* 缓存未命中，扫描 */
    md_count_t c = {0, 100000};
    pthread_mutex_lock(&g_mu);
    md_scan_locked(tid, NULL, md_count_cb, &c);
    pthread_mutex_unlock(&g_mu);
    
    /* 更新缓存 */
    int idx = -1;
    for (int i = 0; i < g_count_cache_n; i++) {
        if (strcmp(g_count_cache[i].tid, tid) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        if (g_count_cache_n < MD_COUNT_CACHE_MAX) {
            idx = g_count_cache_n++;
        } else {
            /* 缓存满了，覆盖最旧的条目 */
            idx = 0;
            for (int i = 1; i < g_count_cache_n; i++) {
                if (g_count_cache[i].ts < g_count_cache[idx].ts) idx = i;
            }
        }
    }
    snprintf(g_count_cache[idx].tid, sizeof(g_count_cache[idx].tid), "%s", tid);
    g_count_cache[idx].count = c.count;
    g_count_cache[idx].ts = now;
    g_count_cache[idx].ver = g_media_version;
    
    return c.count;
}

/* 媒体库变化时调用，使计数缓存失效 */
static void md_count_cache_invalidate(void) {
    g_media_version++;
}

/* ── 定位单条 ── */
static int md_copy_cb(const game_media_item_t *it, void *user) {
    memcpy(user, it, sizeof(game_media_item_t));
    return 1; /* 命中即停 */
}

static int md_lookup_locked(const char *id, game_media_item_t *out) {
    memset(out, 0, sizeof(*out));
    if (md_scan_locked(NULL, id, md_copy_cb, out) != 0) return -1;
    return out->content[0] ? 0 : -1;
}

int game_media_lookup(const char *id, game_media_item_t *out) {
    if (!out || !md_path_ok(id)) return -1;
    pthread_mutex_lock(&g_mu);
    int rc = md_lookup_locked(id, out);
    pthread_mutex_unlock(&g_mu);
    return rc;
}

/* ── 原图 / 原视频：fd 直出 ── */
static const char *md_mime_of(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    dot++;
    if (!strcasecmp(dot, "png")) return "image/png";
    if (!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg")) return "image/jpeg";
    if (!strcasecmp(dot, "jxr") || !strcasecmp(dot, "wdp")) return "image/vnd.jxr";
    if (!strcasecmp(dot, "bmp")) return "image/bmp";
    if (!strcasecmp(dot, "gif")) return "image/gif";
    if (!strcasecmp(dot, "mp4") || !strcasecmp(dot, "m4v")) return "video/mp4";
    if (!strcasecmp(dot, "mov")) return "video/quicktime";
    if (!strcasecmp(dot, "webm")) return "video/webm";
    if (!strcasecmp(dot, "mkv")) return "video/x-matroska";
    return "application/octet-stream";
}

int game_media_open(const char *id, int *out_fd, off_t *out_size, const char **mime) {
    if (!out_fd || !out_size || !mime) return -1;
    char abs[700];
    if (md_abs(id, abs, sizeof(abs)) != 0) return -1;
    int fd = open(abs, O_RDONLY);
    if (fd < 0) return -2;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_size <= 0) {
        close(fd);
        return -2;
    }
    *out_fd = fd;
    *out_size = sb.st_size;
    *mime = md_mime_of(abs);
    return 0;
}

/* [CN-FORK] v0.5.2.122 下载文件名：与列表里那份 dlname 走**同一个 md_dlname**，
 * 免得「界面上显示的」与「下载下来的」两个名字各算各的、慢慢漂开。 */
int game_media_download_name(const char *tid, const char *id, char *out, size_t osz) {
    if (!out || !osz) return -1;
    out[0] = 0;
    if (!md_path_ok(id)) return -1;
    char abs[700];
    if (md_abs(id, abs, sizeof(abs)) != 0) return -1;
    struct stat sb;
    if (stat(abs, &sb) != 0 || !S_ISREG(sb.st_mode)) return -1;
    char game[256];
    game[0] = 0;
    if (tid && md_tid_ok(tid)) game_title_get_name(tid, game, sizeof(game));
    md_dlname(game, id, sb.st_mtime, out, osz);
    return out[0] ? 0 : -1;
}

/* ══════════════ ③ 缩略图：系统缩略图 → 按需缩放 → 落盘缓存 ══════════════ */

static uint64_t md_fnv64(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

static unsigned char *md_read_all(const char *path, size_t *out_len, size_t cap) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || sb.st_size <= 0 || (size_t)sb.st_size > cap) {
        close(fd);
        return NULL;
    }
    size_t sz = (size_t)sb.st_size, got = 0;
    unsigned char *b = (unsigned char *)malloc(sz);
    if (!b) { close(fd); return NULL; }
    while (got < sz) {
        ssize_t r = read(fd, b + got, sz - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    if (got != sz) { free(b); return NULL; }
    *out_len = sz;
    return b;
}

static int md_write_all(int fd, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, b + off, n - off);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

/* 盒式（面积）平均缩放。只做缩小（调用方保证 dw<=sw），大比例下比最近邻干净得多，
 * 实现十来行 —— 仓库里没有 stb_image_resize，为一张缩略图引一个新三方件不值。 */
static void md_resize_box(const unsigned char *src, int sw, int sh,
                          unsigned char *dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((long long)y * sh / dh);
        int y1 = (int)((long long)(y + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sh) y1 = sh;
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((long long)x * sw / dw);
            int x1 = (int)((long long)(x + 1) * sw / dw);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sw) x1 = sw;
            unsigned r = 0, g = 0, b = 0;
            int n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const unsigned char *row = src + (size_t)yy * (size_t)sw * 3;
                for (int xx = x0; xx < x1; xx++) {
                    const unsigned char *px = row + (size_t)xx * 3;
                    r += px[0]; g += px[1]; b += px[2];
                    n++;
                }
            }
            unsigned char *q = dst + ((size_t)y * (size_t)dw + (size_t)x) * 3;
            q[0] = (unsigned char)(r / (unsigned)n);
            q[1] = (unsigned char)(g / (unsigned)n);
            q[2] = (unsigned char)(b / (unsigned)n);
        }
    }
}

typedef struct { unsigned char *p; size_t len, cap; } md_mem_t;

/* stb 的 JPEG 只有 to_func 没有 to_mem，接一个可增长缓冲当 sink */
static void md_jpg_grow(void *ctx, void *data, int len) {
    md_mem_t *m = (md_mem_t *)ctx;
    if (len <= 0) return;
    if (m->len + (size_t)len > m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 65536;
        while (nc < m->len + (size_t)len) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(m->p, nc);
        if (!np) return; /* 分配失败就让 len 停住，最后按 len==0 判失败 */
        m->p = np;
        m->cap = nc;
    }
    memcpy(m->p + m->len, data, (size_t)len);
    m->len += (size_t)len;
}

typedef struct { char name[64]; time_t mt; } md_cent_t;

static int md_cent_cmp(const void *a, const void *b) {
    const md_cent_t *x = (const md_cent_t *)a;
    const md_cent_t *y = (const md_cent_t *)b;
    if (x->mt < y->mt) return -1;
    if (x->mt > y->mt) return 1;
    return strcmp(x->name, y->name);
}

/* 缓存超限：一趟 readdir 收 (名字, mtime)，按 mtime 排序删最旧的。
 * 每生成一张都数一遍目录太贵，所以只在第一次（g_thumb_primed=0）和之后每隔
 * MD_THUMB_MAX 张才走一次全量扫描。 */
static int g_thumb_count = -1;
static int g_thumb_since_scan;

static void md_thumb_prune(void) {
    DIR *dp = opendir(GAME_MEDIA_THUMB_DIR);
    if (!dp) { g_thumb_count = 0; return; }
    md_cent_t *v = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char p[700];
        snprintf(p, sizeof(p), "%s/%s", GAME_MEDIA_THUMB_DIR, de->d_name);
        struct stat sb;
        if (stat(p, &sb) != 0 || !S_ISREG(sb.st_mode)) continue;
        if (n == cap) {
            size_t nc = cap ? cap * 2 : 64;
            md_cent_t *nv = (md_cent_t *)realloc(v, nc * sizeof(md_cent_t));
            if (!nv) break;
            v = nv;
            cap = nc;
        }
        snprintf(v[n].name, sizeof(v[n].name), "%s", de->d_name);
        v[n].mt = sb.st_mtime;
        n++;
    }
    closedir(dp);

    int total = (int)n;
    if (total > MD_THUMB_MAX && v) {
        qsort(v, n, sizeof(md_cent_t), md_cent_cmp);
        size_t kill = n - MD_THUMB_KEEP;
        for (size_t i = 0; i < kill; i++) {
            char p[700];
            snprintf(p, sizeof(p), "%s/%s", GAME_MEDIA_THUMB_DIR, v[i].name);
            if (unlink(p) == 0) total--;
        }
    }
    free(v);
    g_thumb_count = total;
    g_thumb_since_scan = 0;
}

/* stbi 认得的四种位图。系统缩略图另有 .jxr 变体（HDR），那个没有解码器 ——
 * 只按「文件在不在」就把它当解码源，等于给 HDR 缩略图判了死刑（解出来是 NULL）。 */
static int md_is_stb_image(const char *m) {
    return strcmp(m, "image/png") == 0 || strcmp(m, "image/jpeg") == 0 ||
           strcmp(m, "image/bmp") == 0 || strcmp(m, "image/gif") == 0;
}

/* ── 视频封面帧：去缩略图目录里翻一份**真能解码**的（v0.5.2.123 / .124）─────
 * 真机的缩略图命名约定是 `<原名>.<原ext>.<jxr|jpeg>`（见 include/game_media.h 头注释），
 * 也就是同一个录屏旁边**可能同时躺着两份封面帧**：一份 HDR 的 .jxr、一份 .jpeg。
 * 库里 icon_path 只指其中一份，指到 .jxr 时 stb 解不动 —— 旧代码这时直接回占位图
 * （深底 + 播放三角），用户看到的就是「视频的预览图不对，应该是视频里面某一帧画面」。
 *
 * ⚠️ `.123` 第一版有两处错，真机上直接表现为「视频封面也没有」（格子全白）：
 *
 *   ① **目录不该由 id 推。** 第一版按 `GAME_MEDIA_ROOT/thumbnails/<id 去掉前缀>` 推
 *      同构目录 —— 可 `game_media.h` §7a 白纸黑字写着「录屏目录层级是否与 photo 同构」
 *      **尚未在真机验证过**（当年取证时机器上只有截图）。而 `icon_path` 是**媒体库自己
 *      写进库里的**，它指哪儿就是哪儿，是最权威的答案。现在先扫 `dirname(icon_path)`，
 *      推出来的同构目录只作兜底。
 *   ② **扩展名不等于能解码。** 第一版只按扩展名（`md_is_stb_image(md_mime_of(...))`）
 *      放行候选，于是可能挑中一个「名字像 JPEG、内容不是」的文件；它一路传到
 *      `stbi_load_from_memory` 失败 → 返回 -2 → HTTP 404 → **格子从「占位图」退化成
 *      「一块空白」**。现在按**文件头魔数**认（JPEG/PNG/BMP/GIF），扩展名只用来排除 .jxr。
 *
 * 找不到就回 -1，由调用方回占位图；`game_media_thumb` 另有一道「视频永不 404」的兜底。 */

/* 按文件头认位图，**不信任扩展名**（理由见上 ②）。只读 12 字节，代价可忽略。 */
static int md_magic_image(const char *path) {
    unsigned char h[12];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, h, sizeof(h));
    close(fd);
    if (n < 4) return 0;
    if (h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) return 1;              /* JPEG */
    if (h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') return 1; /* PNG  */
    if (h[0] == 'B' && h[1] == 'M') return 1;                                /* BMP  */
    if (h[0] == 'G' && h[1] == 'I' && h[2] == 'F' && h[3] == '8') return 1;  /* GIF  */
    return 0;
}

/* 在 dir 里找 `<base>.<后缀>` 且**真能解码**的那一份。
 * `blen` 处必须是 '.'：光有前缀不算，否则 `..._001.mp4` 会被 `..._001.mp4extra` 误命中。 */
static int md_scan_cover_dir(const char *dir, const char *base, size_t blen,
                             char *abs, size_t absz, struct stat *st) {
    DIR *dp = opendir(dir);
    if (!dp) return -1;
    int rc = -1;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strncmp(de->d_name, base, blen) != 0 || de->d_name[blen] != '.') continue;
        char cand[1400];
        int cn = snprintf(cand, sizeof(cand), "%s/%s", dir, de->d_name);
        if (cn < 0 || (size_t)cn >= sizeof(cand)) continue;
        if (!md_is_stb_image(md_mime_of(cand))) continue; /* .jxr 在这一步被排除 */
        if (!md_magic_image(cand)) continue;              /* 再按魔数确认一次 */
        struct stat sb;
        if (stat(cand, &sb) != 0 || !S_ISREG(sb.st_mode) ||
            sb.st_size <= 0 || (size_t)sb.st_size > MD_SRC_MAX) continue;
        snprintf(abs, absz, "%s", cand);
        *st = sb;
        rc = 0;
        break;
    }
    closedir(dp);
    return rc;
}

/* 一个都没找到时，把目录内容写进日志 —— 这是**唯一能一次问清真机布局**的办法。
 * `game_media.h` §7a 至今没验证过录屏的缩略图层级，之前两轮都是靠猜（猜错两轮）。
 *
 * ⚠️ 同一个 id 只报一次。媒体面板一打开就会给**每个**格子取缩略图，而「没有可解封面帧
 * 的视频」每次取都会走到这里 —— 不挡一下，三条诊断能把真正要紧的 `STALL` 行淹掉
 * （日志环形缓冲有限）。只留最近 8 个 id 的哈希，代价可忽略；重启 payload 后能再报一次，
 * 正合排查需要。 */
static void md_log_cover_dirs(const char *id, const char *d1, const char *d2) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static uint64_t seen[8];
    static int n;
    uint64_t h = md_fnv64(id);
    int dup = 0;
    pthread_mutex_lock(&mu);
    int cnt = n < 8 ? n : 8;
    for (int i = 0; i < cnt; i++) {
        if (seen[i] == h) { dup = 1; break; }
    }
    if (!dup) { seen[n % 8] = h; n++; }
    pthread_mutex_unlock(&mu);
    if (dup) return;

    const char *names[2];
    names[0] = d1;
    names[1] = d2;
    for (int i = 0; i < 2; i++) {
        if (!names[i] || !names[i][0]) continue;
        DIR *dp = opendir(names[i]);
        if (!dp) {
            pldmgr_log("[PLDMGR] media: cover dir MISSING %s\n", names[i]);
            continue;
        }
        char list[600];
        size_t off = 0;
        int shown = 0, total = 0;
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.') continue;
            total++;
            if (shown >= 6) continue;
            int w = snprintf(list + off, sizeof(list) - off, "%s%s",
                             shown ? ", " : "", de->d_name);
            if (w > 0 && (size_t)w < sizeof(list) - off) off += (size_t)w;
            shown++;
        }
        closedir(dp);
        list[sizeof(list) - 1] = 0;
        pldmgr_log("[PLDMGR] media: cover dir %s has %d entries [%s]\n",
                   names[i], total, list);
    }
    pldmgr_log("[PLDMGR] media: no decodable cover for %s\n", id);
}

static int md_find_cover(const char *id, const char *icon,
                         char *abs, size_t absz, struct stat *st) {
    const char *rest = (strncmp(id, "/av_contents/", 13) == 0) ? id + 13 : NULL;
    if (!rest || !rest[0]) return -1;
    const char *slash = strrchr(rest, '/');
    if (!slash || slash == rest) return -1;

    const char *base = slash + 1;
    size_t blen = strlen(base);
    if (blen == 0 || blen > 200) return -1; /* 超过 NAME_MAX 的前缀不可能命中 */

    /* ① 权威答案：icon_path 所在的那个目录（媒体库自己写进库的，不靠我们推）。 */
    char d1[760];
    d1[0] = 0;
    if (icon && icon[0]) {
        char ip[700];
        if (md_abs(icon, ip, sizeof(ip)) == 0) {
            char *sl = strrchr(ip, '/');
            if (sl && sl != ip) {
                *sl = 0;
                snprintf(d1, sizeof(d1), "%s", ip);
                if (md_scan_cover_dir(d1, base, blen, abs, absz, st) == 0) return 0;
            }
        }
    }

    /* ② 兜底：按 id 推同构目录（photo/video 两条链路）。§7a 未验证，所以只当兜底。 */
    char d2[760];
    d2[0] = 0;
    snprintf(d2, sizeof(d2), GAME_MEDIA_ROOT "/thumbnails/%.*s", (int)(slash - rest), rest);
    if (d2[0] && md_scan_cover_dir(d2, base, blen, abs, absz, st) == 0) return 0;

    md_log_cover_dirs(id, d1, d2);
    return -1;
}

/* 选解码源：优先库里那份系统缩略图；解不动（典型：录屏的封面帧是 .jxr）就去
 * 同目录翻一份能解的（见 md_find_cover）；再没有才退回原图本身，
 * 而原图只有位图才解得动。
 *
 * 返回值三种：
 *    0 = abs/st 是**可以直接交给 stbi 解码**的位图
 *   -1 = 没有可解的东西，回 404（HDR 截图走这条）
 *   -3 = 是**视频**且没有可用的封面帧 → 调用方生成占位图（深色底 + 播放三角）
 *
 * ⚠️ -3 的判据必须是「这条本身就是视频」，**不能是「有没有 icon_path」**：
 * 真机上录屏的 icon_path 会指向一张 .jxr，那文件存在、stat 也过，可 stb 解不动 ——
 * 只看存在性就会走进解码分支、失败、回 -2，占位图那条路一次都到不了。
 * （.120 初版正是这个洞：视频缩略图依旧空白，用户复现的第二轮才暴露出来。）
 * 另外 st 一律先清零：-3 分支拿 st_mtime 当缓存键，不清零就是拿栈上垃圾拼路径。 */
static int md_thumb_src(const char *id, char *abs, size_t absz, struct stat *st) {
    memset(st, 0, sizeof(*st));
    int is_video = strncmp(id, "/av_contents/video/", 19) == 0;
    /* it 必须先清零：查不到时它整块都是栈上垃圾，而下面要把 it.icon 带出去用。 */
    game_media_item_t it;
    memset(&it, 0, sizeof(it));
    const char *icon = NULL;
    if (game_media_lookup(id, &it) == 0) {
        if (it.video) is_video = 1;
        if (it.icon[0]) icon = it.icon;
        if (it.icon[0] && md_abs(it.icon, abs, absz) == 0 &&
            stat(abs, st) == 0 && st->st_size > 0 && (size_t)st->st_size <= MD_SRC_MAX &&
            md_is_stb_image(md_mime_of(abs)))
            return 0;
    }
    /* 排在「拿原图当缩略图」**之前**：原图是 4K 整图，解它又慢又占内存，
     * 而封面帧本来就是为缩略图准备的，先找它才是对的顺序。
     * 这里把 icon_path 一起递进去 —— 封面帧该去哪个目录翻，媒体库自己写进库的
     * icon_path 才是最权威的答案，比我们按 id 推同构目录可靠（.123 栽在这上面）。 */
    if (md_find_cover(id, icon, abs, absz, st) == 0) return 0;

    /* md_abs 失败也要看是不是视频：以前无条件回 -1，视频就跟着走了 404 那条路，
     * 界面从占位图退化成空白 —— 正是 .123 真机上的表现。 */
    if (md_abs(id, abs, absz) != 0) {
        memset(st, 0, sizeof(*st));
        return is_video ? -3 : -1;
    }
    if (stat(abs, st) != 0 || st->st_size <= 0 || (size_t)st->st_size > MD_SRC_MAX) {
        /* 盘上原文件不在了：库里既然列得出来，视频就给占位图，别的照旧 404 */
        memset(st, 0, sizeof(*st));
        return is_video ? -3 : -1;
    }
    if (md_is_stb_image(md_mime_of(abs))) return 0;
    /* 到这里是「原文件在、但不是能解的位图」—— 录屏就是这条（mp4 解不了）。
     * st 也要清零：占位图的缓存键必须与「真封面帧」的键分得开，否则两者 mtime
     * 撞上就互相串图（夹具里同一个脚本先后写的文件就在同一秒，当场复现过）。 */
    memset(st, 0, sizeof(*st));
    return is_video ? -3 : -1;
}

int game_media_thumb(const char *id, int want_w, unsigned char **out,
                     size_t *out_len, const char **mime) {
    if (!out || !out_len || !mime || !md_path_ok(id)) return -1;
    if (want_w <= 0 || want_w > 4096) want_w = 320;

    char src[700];
    struct stat st;
    int src_rc = md_thumb_src(id, src, sizeof(src), &st);
    if (src_rc == -1) return -2; /* 没有可解的东西（HDR 截图）→ HTTP 侧 404 */

    ensure_dir_recursive(GAME_MEDIA_THUMB_DIR);
    if (g_thumb_count < 0 || g_thumb_since_scan >= MD_THUMB_MAX) md_thumb_prune();

    char key[160];
    snprintf(key, sizeof(key), "%016llx_%d_%lld.jpg",
             (unsigned long long)md_fnv64(id), want_w, (long long)st.st_mtime);
    char cpath[700];
    snprintf(cpath, sizeof(cpath), "%s/%s", GAME_MEDIA_THUMB_DIR, key);

    /* 命中：整张读回来。文件只有几十 KB，比再造一个 fd 响应简单，
     * 也顺带把「缓存文件被并发删掉」的窗口收敛到一次 read。
     * 占位图走的是**同一条缓存**：同一张视频封面只画一次，之后直接读盘。 */
    size_t clen = 0;
    unsigned char *cached = md_read_all(cpath, &clen, 8u << 20);
    if (cached) {
        *out = cached;
        *out_len = clen;
        *mime = "image/jpeg";
        return 0;
    }

    md_mem_t m;
    memset(&m, 0, sizeof(m));

    if (src_rc == -3) {
        /* 视频没有可解封面帧 → 现画一张占位图（深色底 + 白色播放三角）。
         * 缩略图格子是 16:9，所以按 16:9 出图，铺进格子不会留黑边。 */
        int pw = want_w < 320 ? 320 : want_w;
        int ph = (int)((long long)pw * 9 / 16);
        if (ph < 1) ph = 1;
        unsigned char *buf = (unsigned char *)calloc((size_t)pw * (size_t)ph, 3);
        if (!buf) return -2;
        for (int i = 0; i < pw * ph; i++) {
            buf[i * 3 + 0] = 28;
            buf[i * 3 + 1] = 28;
            buf[i * 3 + 2] = 32;
        }
        /* 播放按钮三角形（指向右侧）：左顶点在 (cx,cy)，右边界在 x = ts - |y| */
        int cx = pw / 2, cy = ph / 2;
        int ts = (pw < ph ? pw : ph) / 4;
        for (int y = -ts; y <= ts; y++) {
            int ay = y < 0 ? -y : y;
            for (int x = 0; x <= ts; x++) {
                if (x <= ts - ay) {
                    int px = cx + x, py = cy + y;
                    if (px >= 0 && px < pw && py >= 0 && py < ph) {
                        buf[(py * pw + px) * 3 + 0] = 255;
                        buf[(py * pw + px) * 3 + 1] = 255;
                        buf[(py * pw + px) * 3 + 2] = 255;
                    }
                }
            }
        }
        stbi_write_jpg_to_func(md_jpg_grow, &m, pw, ph, 3, buf, MD_THUMB_Q);
        free(buf);
    } else {
        size_t slen = 0;
        unsigned char *sbuf = md_read_all(src, &slen, MD_SRC_MAX);
        if (!sbuf) return -2;
        int sw = 0, sh = 0, comp = 0;
        unsigned char *px = stbi_load_from_memory(sbuf, (int)slen, &sw, &sh, &comp, 3);
        free(sbuf);
        if (!px || sw <= 0 || sh <= 0) {
            stbi_image_free(px);
            return -2; /* 解不动（HDR .jxr）→ 404，浏览器同样显示不了 */
        }

        int dw = want_w < sw ? want_w : sw;
        if (dw < 64) dw = 64;
        if (dw > sw) dw = sw;
        int dh = (int)((long long)sh * dw / sw);
        if (dh < 1) dh = 1;

        unsigned char *rgb = px;
        if (dw != sw || dh != sh) {
            rgb = (unsigned char *)malloc((size_t)dw * (size_t)dh * 3);
            if (rgb) md_resize_box(px, sw, sh, rgb, dw, dh);
        }
        if (rgb) stbi_write_jpg_to_func(md_jpg_grow, &m, dw, dh, 3, rgb, MD_THUMB_Q);
        if (rgb != px) free(rgb);
        stbi_image_free(px);
    } /* 占位图 / 真缩略图：两条路都往 m 里出一张 JPEG */

    if (!m.p || m.len == 0) { free(m.p); return -2; }

    /* 落盘：先写 .tmp 再 rename —— 两个面板同时开也不会读到半张图 */
    char tpath[720];
    snprintf(tpath, sizeof(tpath), "%s.tmp", cpath);
    int fd = open(tpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (md_write_all(fd, m.p, m.len) != 0) unlink(tpath);
        else if (rename(tpath, cpath) != 0) unlink(tpath);
        close(fd);
    }
    g_thumb_since_scan++;

    *out = m.p;
    *out_len = m.len;
    *mime = "image/jpeg";
    return 0;
}

/* ══════════════ ④ 删除 ══════════════
 * 只删文件（原图 + 系统缩略图 + .meta/.dat + 本地缩略图缓存），**不碰 av_content.db**
 * —— 理由与代价见 include/game_media.h 铁律①。 */
static void md_drop_thumbs(const char *id) {
    DIR *dp = opendir(GAME_MEDIA_THUMB_DIR);
    if (!dp) return;
    char prefix[40];
    snprintf(prefix, sizeof(prefix), "%016llx_", (unsigned long long)md_fnv64(id));
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strncmp(de->d_name, prefix, strlen(prefix)) != 0) continue;
        char p[700];
        snprintf(p, sizeof(p), "%s/%s", GAME_MEDIA_THUMB_DIR, de->d_name);
        if (unlink(p) == 0 && g_thumb_count > 0) g_thumb_count--;
    }
    closedir(dp);
}

int game_media_delete(const char *tid, const char *id, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!md_tid_ok(tid) || !md_path_ok(id)) {
        if (msg) snprintf(msg, msgsz, "参数不合法");
        return -1;
    }
    /* 归属校验：tid 与 id 一起筛（md_cb_row 先比 tid 再比 path），两样都对得上才删。
     * 少这一步的话，前端传错 tid 就能把别的游戏的截图删掉。
     * it 必须先清零：md_scan_t 没命中回调时不会写 it，不清零就会拿栈上残留内容
     * 当「找到了」—— 实测正是这么误删的。 */
    game_media_item_t it;
    memset(&it, 0, sizeof(it));
    pthread_mutex_lock(&g_mu);
    int rc = md_scan_locked(tid, id, md_copy_cb, &it);
    pthread_mutex_unlock(&g_mu);
    if (rc != 0 || !it.content[0]) {
        if (msg) snprintf(msg, msgsz, "这个文件不在该游戏名下，或已经被删掉了");
        return -1;
    }

    char abs[700];
    if (md_abs(id, abs, sizeof(abs)) != 0) {
        if (msg) snprintf(msg, msgsz, "路径不合法");
        return -1;
    }
    if (unlink(abs) != 0) {
        if (msg) snprintf(msg, msgsz, "删除失败：%s", strerror(errno));
        return -1;
    }
    char side[740];
    snprintf(side, sizeof(side), "%s.meta", abs);
    unlink(side);
    snprintf(side, sizeof(side), "%s.dat", abs);
    unlink(side);

    if (it.icon[0] && md_abs(it.icon, side, sizeof(side)) == 0) unlink(side);
    md_drop_thumbs(id);
    md_count_cache_invalidate();  /* 删除媒体后使计数缓存失效 */

    pldmgr_log("[PLDMGR] media: deleted %s\n", abs);
    if (msg) snprintf(msg, msgsz, "已删除 %s", it.name);
    return 0;
}

/* zip 编码器已搬进 src/zip_io.c（zip_io.h 声明）。原位置是 zip_ent_t 加
 * md_p16/p32/p64 加 md_dos_time 加 md_pack_write，约 330 行。
 * 搬走的原因：存档上传/下载也要 zip（游戏管理的存档六条路由），一个仓库里
 * 放两份必须逐字节一致的 zip 编码器，改一处忘一处就是静默的数据损坏。
 * 条目结构现在是 zip_io.h 的 zip_ent_t（字段一字未改）。 */


/* ── v0.5.2.116：任务槽本体已搬进 game_job.c ──────────────────────────────
 * 原来这里的 g_job / g_job_mu / g_last_id 现在归 game_job.c 所有（打包与删除共用一个槽，
 * 见 include/game_job.h 的头注释）。下面三个 md_* 只是原名薄壳，免得动几百处调用点；
 * 进度/取消那三个也跟着 zip 编码器进了 zip_io.c，这里不再需要。 */

static void md_set_phase(const char *ph) { gj_set_phase(ph); }

static void md_pack_fail(const char *reason, const char *msg) { gj_fail(reason, msg); }

static void md_pack_ok_done(void) { gj_ok_done(); }

/* 打包进度与取消走任务槽（CRC 表也在 game_job.c）。
 * 这三个是 zip_hooks_t 的任务版实现；存档下载是同步 HTTP 请求、没有后台任务
 * 可挂，它那边直接传 NULL —— 把进度硬绑在任务槽上就得为一次同步下载伪造任务。 */
static void md_zb(void *u, long long n) { (void)u; gj_add_bytes(n); }
static void md_zf(void *u) { (void)u; gj_add_file(); }
static int md_zc(void *u) { (void)u; return gj_cancelled(); }
static const zip_hooks_t md_zip_hooks = { NULL, md_zc, md_zb, md_zf };


/* [CN-FORK] v0.5.2.126 摊平之后**必须去重**。
 *
 * 下载名只精确到秒（游戏名称-YYYYMMDD_HHMMSS.ext），原文件名里那截随机数被丢掉了
 * —— 同一秒里连拍两张就必然撞名，而 zip 里两条**同名**条目在 Windows 资源管理器里
 * 解压是互相覆盖：那是**静默丢文件**，比名字难看严重得多。
 * 所以撞了就补 -2 / -3 …；只有真的撞上才动手，常见情况仍是干净的名字。
 *
 * 只与**已经排在前面的**条目比（e[0..n-1]），所以同一批里第二次出现的那个改名，
 * 第一次出现的那个保持原样 —— 顺序变了名字会变，但**同一批永远是同一个结果**。 */
static void md_zname_unique(char *out, size_t osz, const char *folder,
                            const char *dl, const zip_ent_t *e, int n) {
    char stem[300], ext[40];
    ext[0] = 0;
    const char *dot = strrchr(dl, '.');
    if (dot && dot != dl) {
        snprintf(stem, sizeof(stem), "%.*s", (int)(dot - dl), dl);
        snprintf(ext, sizeof(ext), "%s", dot); /* 含那个 '.' */
    } else {
        snprintf(stem, sizeof(stem), "%s", dl);
    }

    snprintf(out, osz, "%s/%s%s", folder, stem, ext);
    for (int k = 2; k < 1000; k++) {
        int dup = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(e[i].zname, out) == 0) { dup = 1; break; }
        }
        if (!dup) return;
        snprintf(out, osz, "%s/%s-%d%s", folder, stem, k, ext);
    }
}

/* ── 收集待打包的路径 ── */
typedef struct { char **v; int n, cap; } md_paths_t;

static int md_collect_cb(const game_media_item_t *it, void *user) {
    md_paths_t *p = (md_paths_t *)user;
    if (p->n >= MD_MAX_ITEMS) return 1;
    if (p->n == p->cap) {
        int nc = p->cap ? p->cap * 2 : 32;
        char **nv = (char **)realloc(p->v, (size_t)nc * sizeof(char *));
        if (!nv) return 1;
        p->v = nv;
        p->cap = nc;
    }
    p->v[p->n] = strdup(it->content);
    if (!p->v[p->n]) return 1;
    p->n++;
    return 0;
}

static void md_paths_free(md_paths_t *p) {
    for (int i = 0; i < p->n; i++) free(p->v[i]);
    free(p->v);
    p->v = NULL;
    p->n = p->cap = 0;
}

/* 建条目表：解析路径 + stat。返回 0 成功；-1 = 一条可用的都没有。 */
static int md_build_entries(const char *tid, const char *const *ids, int nids,
                            zip_ent_t **out, int *out_n, long long *out_total,
                            int *skipped) {
    md_paths_t paths;
    memset(&paths, 0, sizeof(paths));
    *skipped = 0;

    if (ids && nids > 0) {
        for (int i = 0; i < nids; i++) {
            if (!md_path_ok(ids[i])) { (*skipped)++; continue; }
            if (paths.n == paths.cap) {
                int nc = paths.cap ? paths.cap * 2 : 32;
                char **nv = (char **)realloc(paths.v, (size_t)nc * sizeof(char *));
                if (!nv) break;
                paths.v = nv;
                paths.cap = nc;
            }
            paths.v[paths.n] = strdup(ids[i]);
            if (!paths.v[paths.n]) break;
            paths.n++;
        }
    } else {
        /* ⚠️ 必须持 g_mu —— 不能因为「g_img 已经加载过了」就跳过。
         * md_scan_locked 第一句就是 md_img_locked()，而那个函数**会在库的
         * (size, mtime) 变了时 free(g_img) 再整份重读**；一旦另一个线程
         * （game_media_list_json / game_media_count）正拿着 g_mu 走 g_img，
         * 这里就把它的缓冲区释放掉了 —— use-after-free。
         * .120 把「已加载就跳过锁」当成「打包卡住」的修法，方向是错的：
         * 它既没解掉那个症状（用户复现的第二轮仍停在 0%），又引入一条真崩溃路径。 */
        pthread_mutex_lock(&g_mu);
        int rc = md_scan_locked(tid, NULL, md_collect_cb, &paths);
        pthread_mutex_unlock(&g_mu);
        if (rc != 0) { md_paths_free(&paths); return -2; }
    }
    if (paths.n == 0) { md_paths_free(&paths); return -1; }

    zip_ent_t *e = (zip_ent_t *)calloc((size_t)paths.n, sizeof(zip_ent_t));
    if (!e) { md_paths_free(&paths); return -1; }

    /* ── 压缩包内的顶层文件夹（v0.5.2.126）───────────────────────────────────
     * 用户口径：「压缩包下级文件夹是 游戏编码-游戏名称，这个文件夹里面直接就是
     * 截图和视频」。也就是 zip 里**只有一层**目录，截图/录屏直接躺在里面 ——
     * 不再复刻 /av_contents/photo/<生成器>/<TID>/<hex>/ 那套磁盘层级
     * （那是我们的实现细节，对用户是噪音，还会把同一批图拆进不同目录）。
     *
     * 名字要当**文件夹名**用，所以按文件名的规则清一遍：游戏名里带冒号是常态
     * （「蜘蛛侠：迈尔斯」），不清就会解出一个奇怪的目录，甚至被当成路径分隔。
     * 再按 UTF-8 边界兜一个长度上限 —— 不能让 folder + "/" + dlname 撑爆
     * zip_ent_t.zname[512]，更不能把一个汉字劈成半个。 */
    char gname[256] = "";
    if (tid && md_tid_ok(tid)) game_title_get_name(tid, gname, sizeof(gname));
    char folder[192];
    {
        char gs[160];
        snprintf(gs, sizeof(gs), "%s", gname);
        md_sanitize_name(gs);
        md_utf8_cap(gs, 120);
        if (gs[0] && tid && md_tid_ok(tid))
            snprintf(folder, sizeof(folder), "%s-%s", tid, gs);
        else if (tid && md_tid_ok(tid))
            snprintf(folder, sizeof(folder), "%s", tid);
        else
            snprintf(folder, sizeof(folder), "media");
    }

    int n = 0;
    long long total = 0;
    for (int i = 0; i < paths.n; i++) {
        char abs[700];
        if (md_abs(paths.v[i], abs, sizeof(abs)) != 0) { (*skipped)++; continue; }
        struct stat sb;
        if (stat(abs, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_size <= 0) {
            (*skipped)++;
            continue;
        }
        snprintf(e[n].abs, sizeof(e[n].abs), "%s", abs);
        /* zip 内名字 = <游戏编码-游戏名称>/<游戏名称-时间戳>.<ext>。
         * 文件名那一段与「单独下载」那份 dlname **同源**（同一个 md_dlname）——
         * 压缩包里外两套名字只会让人对不上号。
         * 撞名（同一秒连拍两张）由 md_zname_unique 补 -2 / -3，别让两条同名条目
         * 在解压时互相覆盖。 */
        char dl[300];
        md_dlname(gname[0] ? gname : NULL, paths.v[i], sb.st_mtime, dl, sizeof(dl));
        md_zname_unique(e[n].zname, sizeof(e[n].zname), folder, dl, e, n);
        e[n].size = (long long)sb.st_size;
        total += e[n].size;
        n++;
    }
    md_paths_free(&paths);

    if (n == 0) { free(e); return -1; }
    *out = e;
    *out_n = n;
    *out_total = total;
    return 0;
}


typedef struct {
    char tid[32];
    char **ids;
    int nids;
} md_pack_req_t;

/* ── 看门狗：任务不动时把「卡在哪」写进日志（v0.5.2.123）────────────────────
 * 用户第二次报「一直是正在计算中」：面板停在「正在计算体积…」、体积 0 B / 58.7 MB、
 * 用时 0 秒，一个字节都不动。.122 已经修掉三处「任务自己卡死」的成因
 * （stdout 阻塞 / zip 写 0 字节自旋 / 轮询被回调异常带走），真机上仍然复现 ——
 * 说明还有第四处，而**从界面这一侧已经看不出任何信息**：阶段定住的时候，
 * 「正在啃一个 500 MB 的录屏」和「线程早没了」在界面上长得一模一样。
 *
 * 所以这一版不再靠猜。起一条极轻的守护线程：任务活着时每 5 秒看一眼
 * 「距上次有进展多少秒」，超过 15 秒就往日志里写一行**带全部计数**的 STALL。
 * 日志的两个去处（内存环形缓冲 → 界面「日志」页；以及 /data/nayeplmtools/pldmgr.log）
 * **都不经过前端轮询**，所以哪怕界面完全定住，这一行照样看得到。
 * 下次复现只要把那一行发过来：phase 是 preparing / measuring / packing
 * 就分别指向三个不同的下一步，不必再从「卡在正在计算中」这句话反推。
 *
 * 常驻（5 秒一轮，每轮只取一把锁读几个字段），代价可忽略；只在第一次真的
 * 起了打包任务时才创建 —— 只看看游戏列表的人不该被多起一条线程。 */
static int g_wd_started;
static pthread_mutex_t g_wd_mu = PTHREAD_MUTEX_INITIALIZER;

static void *md_watchdog(void *arg) {
    (void)arg;
    long long last_id = 0;
    long last_idle = 0;
    for (;;) {
        struct timespec nap;
        nap.tv_sec = 5;
        nap.tv_nsec = 0;
        nanosleep(&nap, NULL);

        long long id = 0;
        char ph[40] = "";
        long long bd = 0, bt = 0, fd = 0, ft = 0;
        long idle = 0;
        if (gj_snapshot(&id, ph, sizeof(ph), &bd, &bt, &fd, &ft, &idle) != 1) continue;
        if (idle < 15) continue;
        /* 同一个任务的同一段「无进展」别刷屏：首次报，之后每再涨 30 秒报一次。 */
        if (id == last_id && idle < last_idle + 30) continue;
        last_id = id;
        last_idle = idle;
        pldmgr_log("[PLDMGR] media: STALL job=%lld phase=%s idle=%lds "
                   "bytes=%lld/%lld files=%lld/%lld\n",
                   id, ph, idle, bd, bt, fd, ft);
    }
    return NULL;
}

static void md_watchdog_start(void) {
    pthread_mutex_lock(&g_wd_mu);
    if (!g_wd_started) {
        g_wd_started = 1;
        pthread_t th;
        if (pthread_create(&th, NULL, md_watchdog, NULL) == 0) {
            pthread_detach(th);
        } else {
            g_wd_started = 0; /* 起不来就下次再试：一条诊断线程不该挡住正事 */
        }
    }
    pthread_mutex_unlock(&g_wd_mu);
}

/* 单调毫秒：给各阶段打时间戳。CLOCK_MONOTONIC 不受系统改时间影响 ——
 * 日志里要的是「这一段花了多久」，不是墙上时间。 */
static long long md_ms_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000 + (long long)(ts.tv_nsec / 1000000);
}

static void md_pack_run(md_pack_req_t *req) {
    char zip[700];
    gj_lock();
    snprintf(zip, sizeof(zip), "%s", gj_state()->aux);
    gj_unlock();

    md_set_phase("preparing");
    /* 三段相变各留一条日志（v0.5.2.122），v0.5.2.123 起带上**单调毫秒**：
     * 没有 "measured" 就是卡在扫库，没有 "writing" 就是卡在清旧包，
     * 有 "writing" 而没 "done" 就是卡在 zip_write —— 一眼定位，不用再猜。
     * 带上毫秒还能顺带看出「慢」与「卡」：扫库 40 秒是慢，扫库 400 秒是卡。 */
    long long t0 = md_ms_now();
    pldmgr_log("[PLDMGR] media: pack start (tid=%s, ids=%d)\n", req->tid, req->nids);

    zip_ent_t *e = NULL;
    int n = 0, skipped = 0;
    long long total = 0;
    int rc = md_build_entries(req->tid,
                              (const char *const *)req->ids, req->nids,
                              &e, &n, &total, &skipped);
    if (rc == -2) { md_pack_fail("failed", "读取系统媒体库失败（av_content.db）"); return; }
    if (rc != 0) {
        md_pack_fail("failed", skipped ? "所选文件都不在了" : "这个游戏还没有可打包的媒体");
        return;
    }

    long long t1 = md_ms_now();
    md_set_phase("measuring");
    gj_set_totals(total, n);
    pldmgr_log("[PLDMGR] media: pack measured %lld bytes / %d files (skipped %d, +%lldms)\n",
               total, n, skipped, t1 - t0);

    /* 顺手清掉上一包：包名现在是「游戏名称-时间戳.zip」，不再是固定前缀，
     * 所以按 .zip 后缀认（这个目录里只有我们写的包）。不同文件名本来就不冲突；
     * 正在下旧包的连接拿着旧 inode，POSIX 下不受 unlink 影响。
     * `.zip.part` 也一起扫：那是上一次被打断的打包留下的临时名（见下面那段），
     * 不扫的话它会一直躺在盘上，还会让「按后缀认」的规则漏掉一个半截包。 */
    {
        DIR *dp = opendir(GAME_MEDIA_PACK_DIR);
        if (dp) {
            struct dirent *de;
            while ((de = readdir(dp)) != NULL) {
                size_t nl = strlen(de->d_name);
                int is_zip = (nl >= 5 && strcasecmp(de->d_name + nl - 4, ".zip") == 0);
                int is_part = (nl >= 5 && strcasecmp(de->d_name + nl - 5, ".part") == 0);
                if (!is_zip && !is_part) continue;
                char p[760];
                snprintf(p, sizeof(p), "%s/%s", GAME_MEDIA_PACK_DIR, de->d_name);
                if (strcmp(p, zip) != 0) unlink(p);
            }
            closedir(dp);
        }
    }

    long long t2 = md_ms_now();
    md_set_phase("packing");

    /* ⚠️ 先写临时名、写完再 rename（v0.5.2.124）。以前直接写成品名：
     * 写到一半时**成品路径上就是一个 size>0 的残包**，而 /game_media_pack_get
     * 只看「文件在不在、大小 > 0」—— 一次并发（或用户手快重按下载）就会把半截包
     * 当成品发出去，浏览器拿到的是坏 zip，表现就是「打包下载还不好用」。
     * rename 在同一文件系统内是原子的：成品路径上要么是完整的包，要么什么都没有。
     * 临时名固定是成品名 + `.part`（单槽只有一个 worker，不会撞；上一次的残留由上面
     * 那段清理扫描收掉）。 */
    char part[760];
    int pn = snprintf(part, sizeof(part), "%s.part", zip);
    if (pn < 0 || (size_t)pn >= sizeof(part)) {
        free(e);
        md_pack_fail("failed", "打包路径过长");
        return;
    }
    pldmgr_log("[PLDMGR] media: pack writing -> %s (%d entries, +%lldms)\n",
               part, n, t2 - t1);
    const char *err = "打包失败";
    int w = zip_write(part, e, n, &md_zip_hooks, &err);
    free(e);
    req->ids = NULL;
    req->nids = 0;

    if (w == 1) {
        unlink(part);
        md_pack_fail("cancelled", "任务已取消");
        return;
    }
    if (w != 0) { unlink(part); md_pack_fail("failed", err); return; }

    /* 发布。rename 失败（跨设备 / 目录没了）就得把临时包收掉，不能留着误导下一轮。 */
    if (rename(part, zip) != 0) {
        unlink(part);
        md_pack_fail("failed", "无法发布打包文件");
        return;
    }

    /* 先收尾、再补提示：md_pack_ok_done 会把 error 清空，反过来写等于白写一句
     * 「有 N 个文件不见了」（原版就是这个顺序，那句话一直没显示出来过）。 */
    md_pack_ok_done();
    if (skipped > 0) {
        char m[192];
        snprintf(m, sizeof(m), "完成，但有 %d 个文件在打包前不见了", skipped);
        gj_set_error(m);
    }
    pldmgr_log("[PLDMGR] media: pack job done -> %s (%lld bytes, %d files, +%lldms)\n",
               zip, total, n, md_ms_now() - t2);
}

static void *md_pack_thread(void *arg) {
    md_pack_req_t *req = (md_pack_req_t *)arg;
    md_pack_run(req);
    for (int i = 0; i < req->nids; i++) free(req->ids[i]);
    free(req->ids);
    free(req);
    return NULL;
}

static int md_pack_start(const char *tid, const char *const *ids, int nids,
                         long long *job_id, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!md_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "参数不合法");
        return -2;
    }
    if (nids < 0 || nids > MD_MAX_ITEMS) {
        if (msg) snprintf(msg, msgsz, "参数不合法");
        return -2;
    }
    if (ids && nids > 0) {
        for (int i = 0; i < nids; i++) {
            if (!md_path_ok(ids[i])) {
                if (msg) snprintf(msg, msgsz, "参数不合法");
                return -2;
            }
        }
    }

    md_pack_req_t *req = (md_pack_req_t *)calloc(1, sizeof(md_pack_req_t));
    if (!req) {
        if (msg) snprintf(msg, msgsz, "内存不足");
        return -1;
    }
    snprintf(req->tid, sizeof(req->tid), "%s", tid);
    if (ids && nids > 0) {
        req->ids = (char **)calloc((size_t)nids, sizeof(char *));
        if (!req->ids) { free(req); if (msg) snprintf(msg, msgsz, "内存不足"); return -1; }
        for (int i = 0; i < nids; i++) {
            req->ids[i] = strdup(ids[i]);
            if (!req->ids[i]) {
                req->nids = i;
                for (int k = 0; k < i; k++) free(req->ids[k]);
                free(req->ids);
                free(req);
                if (msg) snprintf(msg, msgsz, "内存不足");
                return -1;
            }
            req->nids = i + 1;
        }
    }

    gj_crc_init();
    md_watchdog_start(); /* v0.5.2.123 卡死取证：见 md_watchdog 的注释 */
    if (ensure_dir_recursive(GAME_MEDIA_PACK_DIR) != 0) {
        for (int i = 0; i < req->nids; i++) free(req->ids[i]);
        free(req->ids);
        free(req);
        if (msg) snprintf(msg, msgsz, "无法创建打包目录");
        return -1;
    }

    /* [CN-FORK] v0.5.2.121 压缩包命名：游戏名称-打包时间戳.zip（如「宇宙机器人-20261006_123456.zip」）。
     * 在 gj_begin **之前**算好：gj_begin 返回时仍持任务锁，而取名要读磁盘上的
     * name.txt（game_title_get_name）—— 把一次文件 I/O 放进任务锁里，等于让
     * /game_job_status 陪着一起等盘。 */
    char zipname[300];
    {
        char name[256];
        name[0] = 0;
        game_title_get_name(tid, name, sizeof(name));
        md_sanitize_name(name);
        if (!name[0]) snprintf(name, sizeof(name), "%s", tid);
        char ts[32] = "";
        md_stamp_compact(time(NULL), ts, sizeof(ts));
        snprintf(zipname, sizeof(zipname), "%s-%s.zip", name, ts);
    }

    long long id = 0;
    if (gj_begin(GJ_KIND_PACK, &id, msg, msgsz) != 0) {
        /* 已有任务在跑：槽没领到，这里必须把整份请求还回去 */
        for (int i = 0; i < req->nids; i++) free(req->ids[i]);
        free(req->ids);
        free(req);
        return -1;
    }
    snprintf(gj_state()->phase, sizeof(gj_state()->phase), "preparing");
    snprintf(gj_state()->aux, sizeof(gj_state()->aux), "%s/%s", GAME_MEDIA_PACK_DIR, zipname);
    gj_unlock();

    pthread_t th;
    if (pthread_create(&th, NULL, md_pack_thread, req) != 0) {
        md_pack_fail("failed", "无法启动后台任务线程");
        if (msg) snprintf(msg, msgsz, "无法启动后台任务线程");
        return -1;
    }
    pthread_detach(th);

    if (job_id) *job_id = id;
    pldmgr_log("[PLDMGR] media: pack job %lld started (tid=%s, ids=%d)\n", id, tid, nids);
    return 0;
}

/* POST /game_media_pack 的请求体入口：{tid, ids:[…]|null} */
int game_media_pack_start_json(const char *body, size_t len,
                               long long *job_id, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!body || len == 0 || len > (512u << 10)) {
        if (msg) snprintf(msg, msgsz, "请求体为空或过大");
        return -2;
    }
    char *raw = (char *)malloc(len + 1);
    if (!raw) { if (msg) snprintf(msg, msgsz, "内存不足"); return -1; }
    memcpy(raw, body, len);
    raw[len] = 0;

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) {
        if (msg) snprintf(msg, msgsz, "请求体不是合法 JSON");
        return -2;
    }
    cJSON *jt = cJSON_GetObjectItemCaseSensitive(root, "tid");
    if (!cJSON_IsString(jt) || !jt->valuestring) {
        cJSON_Delete(root);
        if (msg) snprintf(msg, msgsz, "缺少 tid");
        return -2;
    }

    /* ids 缺省 / null / 空数组 = 全量。数组里的元素必须**拷出来**再 cJSON_Delete ——
     * valuestring 挂在 JSON 树上，树一删就是悬垂指针。实测就是这里漏拷，让
     * md_path_ok 读到已释放内存，于是「按 id 打包」莫名其妙回 -2。 */
    cJSON *ji = cJSON_GetObjectItemCaseSensitive(root, "ids");
    int nids = 0;
    char **ids = NULL;
    if (cJSON_IsArray(ji)) {
        int raw_n = cJSON_GetArraySize(ji);
        if (raw_n > MD_MAX_ITEMS) raw_n = MD_MAX_ITEMS;
        if (raw_n > 0) {
            ids = (char **)calloc((size_t)raw_n, sizeof(char *));
            if (!ids) {
                cJSON_Delete(root);
                if (msg) snprintf(msg, msgsz, "内存不足");
                return -1;
            }
            for (int i = 0; i < raw_n; i++) {
                cJSON *el = cJSON_GetArrayItem(ji, i);
                if (!cJSON_IsString(el) || !el->valuestring) {
                    /* 非字符串元素一律拒绝，不「跳过」—— 跳过会让 [1,2] 变成全量打包 */
                    for (int k = 0; k < nids; k++) free(ids[k]);
                    free(ids);
                    cJSON_Delete(root);
                    if (msg) snprintf(msg, msgsz, "ids 必须是字符串数组");
                    return -2;
                }
                ids[nids] = strdup(el->valuestring);
                if (!ids[nids]) {
                    for (int k = 0; k < nids; k++) free(ids[k]);
                    free(ids);
                    cJSON_Delete(root);
                    if (msg) snprintf(msg, msgsz, "内存不足");
                    return -1;
                }
                nids++;
            }
        }
    }

    /* tid 同样要拷出来再释放树 */
    char tid[32];
    snprintf(tid, sizeof(tid), "%s", jt->valuestring);
    cJSON_Delete(root);

    int rc = md_pack_start(tid, (const char *const *)ids, nids, job_id, msg, msgsz);
    for (int i = 0; i < nids; i++) free(ids[i]);
    free(ids);
    return rc;
}

/* ── 三个对外入口（v0.5.2.116 起都是薄壳，实体在 game_job.c）───────────────
 *
 * /game_job_status 与 /game_job_cancel 这两条路由对**两种任务一视同仁**（打包、删除都走
 * 它们），所以真正的实现是 gj_status_json / gj_cancel；这里保留原函数名只是为了不惊动
 * http_server.c 与 test/media_scan_test.c 里已经跑通的调用点。
 *
 * game_media_pack_open 则**必须**按 kind 过滤：删除任务的 job id 同样能被查到，
 * 但它的 aux 装的是待删源路径而不是 zip —— 不挡这道，用一个删除任务的 id 就能把
 * aux 当成文件路径去 open。 */
int game_media_pack_status(long long job_id, char *buf, size_t size) {
    /* [CN-FORK] v0.5.2.123 轮询心跳。
     * /game_job_status 在 is_noisy_route() 里是降噪的（1s 一次，不能每发都记），
     * 于是真机上「界面定住」时日志里**看不出前端到底还在不在轮询** ——
     * 而这一条恰恰是最关键的分岔：日志里还在跳 → 后端没事，是渲染/前端那一侧；
     * 日志里 #1 之后再没有 → 连接或后端被堵住了。所以按「首个 + 每 20 个」记一条，
     * 约 20 秒一行，既补上这个盲区又不淹日志。 */
    static pthread_mutex_t ph_mu = PTHREAD_MUTEX_INITIALIZER;
    static long long ph_job = 0;
    static long ph_n = 0;
    long shown = 0;
    int logit = 0;
    pthread_mutex_lock(&ph_mu);
    if (job_id != ph_job) { ph_job = job_id; ph_n = 0; }
    ph_n++;
    shown = ph_n;
    logit = (ph_n == 1 || ph_n % 20 == 0);
    pthread_mutex_unlock(&ph_mu);
    if (logit) pldmgr_log("[PLDMGR] media: status poll #%ld (job=%lld)\n", shown, job_id);

    return gj_status_json(job_id, buf, size);
}

int game_media_pack_cancel(long long job_id) {
    return gj_cancel(job_id, NULL, 0);
}

/* 返回值三态（v0.5.2.124 起细分，HTTP 侧按这个映射）：
 *   -1 = 任务不存在 / kind 不符        → 404「任务不存在或已过期」
 *   -2 = 任务在、但**还没成功收尾**     → 409「还没打包完」（不是 404！）
 *   -3 = 收尾了、成品却不在 / 是 0 字节 → 404「产出文件已被清理」
 *
 * ⚠️ 为什么必须问 gj_ready：光看「文件在不在、大小 > 0」挡不住**残包**。
 * 用户报的「打包下载还不好用」有一半就出在这儿 —— 打包进行到一半时成品路径上
 * 已经有个 size>0 的半截包，一次并发下载就把它当成品发走了。 */
int game_media_pack_open(long long job_id, int *out_fd, off_t *out_size) {
    if (!out_fd || !out_size) return -1;
    char zip[700];
    if (gj_aux(job_id, GJ_KIND_PACK, zip, sizeof(zip)) != 0) return -1;

    char st[40];
    if (!gj_ready(job_id, GJ_KIND_PACK, st, sizeof(st))) {
        pldmgr_log("[PLDMGR] media: pack_get job=%lld not ready (state=%s)\n", job_id, st);
        return -2;
    }

    int fd = open(zip, O_RDONLY);
    if (fd < 0) {
        pldmgr_log("[PLDMGR] media: pack_get job=%lld open failed: %s\n", job_id, zip);
        return -3;
    }
    struct stat sb;
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_size <= 0) {
        close(fd);
        pldmgr_log("[PLDMGR] media: pack_get job=%lld bad output (not a file or 0 bytes)\n",
                   job_id);
        return -3;
    }
    *out_fd = fd;
    *out_size = sb.st_size;
    pldmgr_log("[PLDMGR] media: pack_get job=%lld serving %lld bytes (%s)\n",
               job_id, (long long)sb.st_size, zip);
    return 0;
}

/* 成品 zip 的文件名。名字的唯一来源是 aux（md_pack_start 写进去的那份），
 * 这里只做 basename —— 让路由去拼一次必然与 aux 漂开。 */
int game_media_pack_name(long long job_id, char *out, size_t osz) {
    if (!out || !osz) return -1;
    out[0] = 0;
    char zip[700];
    if (gj_aux(job_id, GJ_KIND_PACK, zip, sizeof(zip)) != 0) return -1;
    const char *sl = strrchr(zip, '/');
    snprintf(out, osz, "%s", sl ? sl + 1 : zip);
    return out[0] ? 0 : -1;
}