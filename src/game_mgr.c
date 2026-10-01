/* [CN-FORK] 游戏管理模块：枚举「已安装(app.db) + 已存在(扫描 dump)」游戏。
 * SQLite 只读解析器与 param.sfo 解析逐字移植自 v0.5.1.63 src/stats.c（实机验证过的实现），
 * 把原来「按单个 titleId 查一行」改造成「遍历 tbl_contentinfo 收集所有游戏行」。
 * 一次性拉取 + pthread_mutex 串行 + 60s 缓存，绝不轮询（app.db 全库读 + 目录扫描有 IO 成本，
 * 参考 /system_stats 死机教训，频率必须压住）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define APPDB_PATH  "/system_data/priv/mms/app.db"
#define MAX_GAMES   512

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

/* 在 sqlite_master 里定位 tbl_contentinfo 的根页 */
typedef struct { uint32_t root; } st_master_arg;
static int st_cb_master(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    st_master_arg *m = (st_master_arg *)arg;
    const uint8_t *tp;
    size_t tl;
    uint64_t num = 0;
    if (st_rec_col(d, sz, rec, 1, &tp, &tl, NULL) != 2 || tl != 15 || memcmp(tp, "tbl_contentinfo", 15) != 0) return 0;
    if (st_rec_col(d, sz, rec, 3, NULL, NULL, &num) != 3 || !num) return 0;
    m->root = (uint32_t)num;
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
} enum_arg_t;

static void gm_append_item(enum_arg_t *a, const char *tid, const char *name, const char *src, const char *fmt) {
    if (a->count >= MAX_GAMES) { a->overflow = 1; return; }
    for (int i = 0; i < a->count; i++)
        if (!strcmp(a->tids[i], tid)) return;   /* 去重：已安装优先（先入者胜） */
    if (a->pos + 640 >= a->size) { a->overflow = 1; return; }
    char esc[512];
    pldmgr_json_escape(name, esc, sizeof(esc));
    a->pos += snprintf(a->buf + a->pos, a->size - a->pos,
                       "%s{\"tid\":\"%s\",\"name\":\"%s\",\"src\":\"%s\",\"fmt\":\"%s\"}",
                       a->count ? "," : "", tid, esc, src, fmt);
    snprintf(a->tids[a->count], sizeof(a->tids[0]), "%s", tid);
    a->count++;
}

/* [CN-FORK] 前向声明：游戏格式解析（定义见下方 ShadowMountPlus 挂载表块），供 app.db 枚举回调使用。 */
static const char *gm_fmt(const char *tid, const char *src);

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
    gm_append_item(a, tid, name, "installed", gm_fmt(tid, "installed"));
    return 0;
}

static void game_enum_appdb(enum_arg_t *a) {
    int fd = open(APPDB_PATH, O_RDONLY);
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
    st_master_arg m = { 0 };
    st_bwalk(d, (size_t)rd, 1, pgsz, 0, st_cb_master, &m);
    if (m.root) st_bwalk(d, (size_t)rd, m.root, pgsz, 0, st_cb_enum, a);
    free(d);
}

/* ── 扫描 ShadowMount 式 scan paths，发现「已存在但未安装」的 dump 目录 ──
 * 一级子目录名即 TITLE_ID，判 <path>/<TID>/sce_sys/param.sfo 存在才算一个游戏。 */
static const char *const GM_SCAN_PATHS[] = {
    "/data/homebrew", "/data/etaHEN/games",
    "/mnt/ext0", "/mnt/ext1", "/mnt/shadowmnt", "/mnt/shadowmnt/pfsc",
    "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3",
    "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
};

/* ── [CN-FORK] ShadowMountPlus 格式探测：照它的真实格式集显示，不自己发明。
 * 镜像挂载点命名约定 /mnt/shadowmnt/<镜像名>_<hash>（PFSC 容器在 /mnt/shadowmnt/pfsc/...），
 * 镜像名通常以 9 位 TITLE_ID 开头、以格式扩展名结尾：.ffpkg(UFS,推荐) / .exfat / .ffpfs / .ffpfsc。
 * 扫挂载表解析成 <TID> -> 格式；认不出镜像的：app.db 内置安装记为 pkg、扫描到的裸目录记为 folder。
 * getmntinfo 仅在 60s 快照重建时调一次，不轮询。 */
#define GM_MAX_MNT 160
static char g_mnt_tid[GM_MAX_MNT][12];
static char g_mnt_fmt[GM_MAX_MNT][8];
static int  g_mnt_cnt = 0;

/* 从 ShadowMount 挂载的文件系统类型判格式（照 ShadowMountPlus 的 image_fs_type 语义）：
 * .ffpkg 镜像挂成 ufs、.exfat 挂成 exfat、.ffpfs 挂成 pfs/devpfs；PFSC 容器挂在 /mnt/shadowmnt/pfsc 下。
 * 挂载点名字里扩展名被 strip 掉了、来源又是 /dev/lvdN 块设备，所以只能靠 f_fstypename 区分。认不出返回 NULL。 */
static const char *gm_fmt_from_fstype(const char *to, const char *fstype) {
    if (strstr(to, "/mnt/shadowmnt/pfsc/")) return "ffpfsc";
    if (!strcmp(fstype, "ufs"))   return "ffpkg";
    if (!strcmp(fstype, "exfat") || !strcmp(fstype, "msdos") || !strcmp(fstype, "exfatfs")) return "exfat";
    if (strstr(fstype, "pfs"))    return "ffpfs";   /* pfs / devpfs / transaction_pfs / ppr_pfs */
    return NULL;
}

/* 从 <dir>/sce_sys/param.json 抠 titleId（照 ShadowMountPlus get_game_info 的做法，挂载点根即游戏根）。 */
static int gm_param_json_tid(const char *dir, char out[12]) {
    char p[768];
    snprintf(p, sizeof(p), "%s/sce_sys/param.json", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return 0;
    buf[n] = 0;
    const char *key = strstr(buf, "\"titleId\"");
    if (!key) key = strstr(buf, "\"title_id\"");
    if (!key) return 0;
    const char *c = strchr(key, ':');
    if (!c) return 0;
    do { if (!*++c) return 0; } while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r');
    if (*c != '"') return 0;
    c++;
    int i = 0;
    while (i < 11 && c[i] && c[i] != '"') { out[i] = c[i]; i++; }
    out[i] = 0;
    return i >= 9 ? 1 : 0;
}

/* 从路径里任意位置抠出 9 位 TITLE_ID（镜像文件名/挂载点常以 TITLE_ID 开头，作 param.json 读不到时的回退）。 */
static const char *const GM_TID_PREFIX[] = { "PPSA", "CUSA", "PPJB", "EMUA", "PAUS", "PPHD", NULL };
static int gm_extract_tid(const char *s, char out[10]) {
    for (const char *p = s; *p; p++) {
        for (int i = 0; GM_TID_PREFIX[i]; i++) {
            size_t pl = strlen(GM_TID_PREFIX[i]);
            if (strncmp(p, GM_TID_PREFIX[i], pl) != 0) continue;
            int k = 0;
            while (k < 9 && p[k] && isalnum((unsigned char)p[k])) k++;
            if (k >= 9) { memcpy(out, p, 9); out[9] = 0; return 1; }
        }
    }
    return 0;
}

static void gm_build_mount_map(void) {
    static int dumped = 0;   /* ShadowMount 挂载明细只在首次快照 dump 一次，供实机校准 fstype */
    g_mnt_cnt = 0;
    struct statfs *mnts = NULL;
    int mn = getmntinfo(&mnts, MNT_NOWAIT);
    for (int i = 0; mn > 0 && i < mn; i++) {
        const char *to = mnts[i].f_mntonname;
        if (strncmp(to, "/mnt/shadowmnt/", 15) != 0) continue;   /* 只看 ShadowMount 镜像挂载 */
        const char *fstype = mnts[i].f_fstypename;
        const char *fmt = gm_fmt_from_fstype(to, fstype);
        if (!dumped) pldmgr_log("[PLDMGR] shadowmnt to=%s fstype=%s fmt=%s\n", to, fstype, fmt ? fmt : "?");
        if (!fmt) continue;
        char tid[12];
        if (!gm_param_json_tid(to, tid) && !gm_extract_tid(to, tid)) continue;  /* 先读 param.json，回退挂载点前缀 */
        if (g_mnt_cnt >= GM_MAX_MNT) break;
        snprintf(g_mnt_tid[g_mnt_cnt], sizeof(g_mnt_tid[0]), "%s", tid);
        snprintf(g_mnt_fmt[g_mnt_cnt], sizeof(g_mnt_fmt[0]), "%s", fmt);
        g_mnt_cnt++;
    }
    dumped = 1;
}

/* 游戏格式：优先 ShadowMount 镜像格式（ffpkg/exfat/ffpfs/ffpfsc）；否则内置安装=pkg、裸目录=folder。 */
static const char *gm_fmt(const char *tid, const char *src) {
    for (int i = 0; i < g_mnt_cnt; i++)
        if (!strncmp(g_mnt_tid[i], tid, 9)) return g_mnt_fmt[i];
    return strcmp(src, "installed") == 0 ? "pkg" : "folder";
}

/* [CN-FORK] LZ4 变体（ampr 实时解压）：裸目录里带 ampr_*.index / ampr_*.pak 与 eboot.bin.orig，
 * 与普通「文件夹」转储不同，单列一档。扫目录里是否有 ampr_ 前缀条目即可判定。 */
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
            const char *fmt = gm_fmt(ent->d_name, "dump");
            if (!strcmp(fmt, "folder") && gm_dir_is_lz4(root)) fmt = "lz4";
            gm_append_item(a, ent->d_name, name, "dump", fmt);
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

int game_mgr_icon_path(const char *tid, char *out, size_t osz) {
    if (!tid || !out || osz < 256) return 0;
    if (!gm_tid_is_game(tid)) return 0; /* 9 位 alnum，防路径穿越 */
    char cand[768];

    snprintf(cand, sizeof(cand), "/user/app/%s/sce_sys/icon0.png", tid);
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

/* ══════════════ 顶层：mutex + 60s 快照缓存，一次性拉取不轮询 ══════════════ */
static pthread_mutex_t g_game_mtx = PTHREAD_MUTEX_INITIALIZER;
static char    g_game_cache[131072];
static size_t  g_game_cache_len = 0;
static time_t  g_game_cache_ts  = 0;

static size_t build_game_json(char *buf, size_t size) {
    size_t pos = snprintf(buf, size, "{\"games\":[");
    if (pos >= size) return size - 1;
    enum_arg_t a;
    a.buf = buf; a.size = size; a.pos = pos; a.count = 0; a.overflow = 0;
    gm_build_mount_map();  /* [CN-FORK] 刷新 ShadowMount 挂载表，供 dump 判格式 */
    game_enum_appdb(&a);   /* 已安装优先 */
    game_enum_dump(&a);    /* 再补扫描到的 dump，tid 去重 */
    pos = a.pos;
    if (pos < size)
        pos += snprintf(buf + pos, size - pos, "],\"count\":%d%s}",
                        a.count, a.overflow ? ",\"truncated\":true" : "");
    if (pos >= size) pos = size - 1;
    return pos;
}

int game_mgr_list_json(char *buf, size_t size) {
    if (!buf || size < 64) return 0;
    pthread_mutex_lock(&g_game_mtx);
    time_t now = time(NULL);
    if (g_game_cache_len && now >= g_game_cache_ts && (now - g_game_cache_ts) < 60) {
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
