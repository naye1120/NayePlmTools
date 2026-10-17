/* [CN-FORK] v0.6.0.179 内置软件的磁贴图（图标 / 背景）—— **面板这一份**。
 *
 * 这一层只碰文件：路径、版本号、落盘、原始列值的底片。真正把它登记进主机
 * app.db 的两列（icon0Info / pic0Info）在 app_tile_write.c。
 * 分开的原因写在 include/app_tile.h 末尾：app_visibility.c 只需要
 * 「有没有设过图」这一个布尔，不该为它把 stb 与 sqlite 拖进每个宿主套件。
 *
 * 目录布局（<APP_TILE_DIR>/<TID>/）：
 *   icon0.png            我们自己那份图标（= /app_icon 的响应体）
 *   pic0.dds             我们自己那份背景（登记进主机 pic0Info 的就是它）
 *   bg.png               背景的 PNG 预览（= /app_tile_bg 的响应体）
 *   icon.ver / bg.ver    版本号（时间戳式的严格递增整数，见 at_ver_bump）
 *   orig_icon0info.txt   第一次动手前 icon0Info 的原始值（空文件 = 原本是 SQL NULL）
 *   orig_pic0info.txt    同上，pic0Info
 *
 * 版本号为什么不是 mtime：同一秒内连传两次（「我再挑一张」很常见）会得到同一个
 * 版本号，前端拿它当 ?v= 就不刷新了，用户看到「换图没生效」—— 与 game_title.c
 * 的 tt_ver_bump 同一个坑，那里已经踩过一次。 */

#include "app_tile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "json_helpers.h"
#include "pldmgr.h"

int app_tile_is_builtin(const char *tid) {
    if (!tid) return 0;
    size_t n = strlen(tid);
    if (n < 6 || n > 16) return 0;
    if (strncmp(tid, "NPXS", 4) != 0) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = tid[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

static void at_path(char *out, size_t osz, const char *dir, const char *leaf) {
    snprintf(out, osz, "%s/%s", dir, leaf);
}

/* 「盘上有这个文件」。注意与 game_title.c 的 tt_exists 不同：**允许 0 字节**
 * —— 底片文件用 0 字节表示「原本是 SQL NULL」，不能拿 size>0 当存在判据。 */
static int at_present(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

int app_tile_dir(const char *tid, char *out, size_t osz) {
    if (!out || osz < 32) return -1;
    out[0] = 0;
    if (!app_tile_is_builtin(tid)) return -1;
    int n = snprintf(out, osz, APP_TILE_DIR "/%s", tid);
    if (n < 0 || (size_t)n >= osz) { out[0] = 0; return -1; }
    return 0;
}

static int at_file(const char *tid, const char *leaf, char *out, size_t osz) {
    if (!out || osz < 32) return -1;
    out[0] = 0;
    char d[768];
    if (app_tile_dir(tid, d, sizeof(d)) != 0) return -1;
    at_path(out, osz, d, leaf);
    return at_present(out) ? 0 : -1;
}

int app_tile_icon_file(const char *tid, char *out, size_t osz) {
    return at_file(tid, APP_TILE_ICON_LEAF, out, osz);
}
int app_tile_bg_dds_file(const char *tid, char *out, size_t osz) {
    return at_file(tid, APP_TILE_BG_DDS_LEAF, out, osz);
}
int app_tile_bg_png_file(const char *tid, char *out, size_t osz) {
    return at_file(tid, APP_TILE_BG_PNG_LEAF, out, osz);
}

/* ── 版本号 ──────────────────────────────────────────────────────────────── */

static long long at_ver_read(const char *dir, const char *leaf) {
    char p[900];
    at_path(p, sizeof(p), dir, leaf);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    char b[32];
    size_t n = fread(b, 1, sizeof(b) - 1, f);
    fclose(f);
    b[n] = 0;
    return (long long)strtoll(b, NULL, 10);
}

/* 严格递增：取 max(now, prev+1)。返回新的版本号，0 = 写失败（调用方照旧当没设过，
 * 但文件已经落盘了 —— 下一次上传会重新 bump，不影响正确性，只记一条日志）。 */
static long long at_ver_bump(const char *dir, const char *leaf) {
    long long prev = at_ver_read(dir, leaf);
    long long v = (long long)time(NULL);
    if (v <= prev) v = prev + 1;
    char p[900], s[32];
    at_path(p, sizeof(p), dir, leaf);
    snprintf(s, sizeof(s), "%lld\n", v);
    if (write_file_text(p, s, strlen(s)) != 0) {
        pldmgr_log("[PLDMGR] app_tile: 写 %s 失败：%s\n", p, strerror(errno));
        return 0;
    }
    return v;
}

static long long at_ver(const char *tid, const char *leaf) {
    char d[768];
    if (app_tile_dir(tid, d, sizeof(d)) != 0) return 0;
    return at_ver_read(d, leaf);
}

long long app_tile_icon_ver(const char *tid) { return at_ver(tid, "icon.ver"); }
long long app_tile_bg_ver(const char *tid) { return at_ver(tid, "bg.ver"); }

/* ── 落盘 / 删除 ─────────────────────────────────────────────────────────── */

/* 抽出「建目录 → 写文件」这一段：图标与背景各两个文件，抄四遍没道理。 */
static int at_store(const char *dir, const char *leaf,
                    const unsigned char *data, size_t len) {
    if (!data || !len) return -1;
    if (ensure_dir_recursive(dir) != 0) {
        pldmgr_log("[PLDMGR] app_tile: 建目录 %s 失败：%s\n", dir, strerror(errno));
        return -1;
    }
    char p[900];
    at_path(p, sizeof(p), dir, leaf);
    if (write_file_text(p, (const char *)data, len) != 0) {
        pldmgr_log("[PLDMGR] app_tile: 写 %s 失败：%s\n", p, strerror(errno));
        return -1;
    }
    return 0;
}

int app_tile_store_icon(const char *tid, const unsigned char *png, size_t len) {
    char d[768];
    if (app_tile_dir(tid, d, sizeof(d)) != 0) return -1;
    if (at_store(d, APP_TILE_ICON_LEAF, png, len) != 0) return -1;
    at_ver_bump(d, "icon.ver");
    return 0;
}

int app_tile_store_bg(const char *tid, const unsigned char *dds, size_t dds_len,
                      const unsigned char *png, size_t png_len) {
    char d[768];
    if (app_tile_dir(tid, d, sizeof(d)) != 0) return -1;
    /* 先 dds 再 png：登记进主机的是 dds，它写不下去就整个失败 ——
     * 只留一份 bg.png 会让「面板里看得到、主机上没有」悄悄成立。 */
    if (at_store(d, APP_TILE_BG_DDS_LEAF, dds, dds_len) != 0) return -1;
    if (png && png_len && at_store(d, APP_TILE_BG_PNG_LEAF, png, png_len) != 0) return -1;
    at_ver_bump(d, "bg.ver");
    return 0;
}

static int at_drop(const char *tid, const char *leaf, const char *ver_leaf) {
    char d[768], p[900];
    if (app_tile_dir(tid, d, sizeof(d)) != 0) return -1;
    at_path(p, sizeof(p), d, leaf);
    remove(p); /* 不存在也算删掉了：调用方要的语义是「盘上不该再有它」 */
    if (ver_leaf) at_ver_bump(d, ver_leaf);
    return 0;
}

int app_tile_drop_icon(const char *tid) {
    return at_drop(tid, APP_TILE_ICON_LEAF, "icon.ver");
}

int app_tile_drop_bg(const char *tid) {
    /* 背景有两份文件（dds 给主机、png 给面板预览），一起删干净。 */
    char d[768], p[900];
    if (app_tile_dir(tid, d, sizeof(d)) != 0) return -1;
    at_path(p, sizeof(p), d, APP_TILE_BG_DDS_LEAF);
    remove(p);
    at_path(p, sizeof(p), d, APP_TILE_BG_PNG_LEAF);
    remove(p);
    at_ver_bump(d, "bg.ver");
    return 0;
}

/* ── 原始列值的底片 ─────────────────────────────────────────────────────── */

static void at_orig_path(const char *dir, int is_bg, char *out, size_t osz) {
    at_path(out, osz, dir, is_bg ? "orig_pic0info.txt" : "orig_icon0info.txt");
}

int app_tile_orig_save_once(const char *tid, int is_bg, const char *value) {
    char d[768], p[900];
    if (app_tile_dir(tid, d, sizeof(d)) != 0) return -1;
    if (ensure_dir_recursive(d) != 0) return -1;
    at_orig_path(d, is_bg, p, sizeof(p));
    if (at_present(p)) return 0; /* 已经有底片：绝不用现在的值覆盖它 */
    const char *v = value ? value : "";
    if (write_file_text(p, v, strlen(v)) != 0) {
        pldmgr_log("[PLDMGR] app_tile: 写底片 %s 失败：%s\n", p, strerror(errno));
        return -1;
    }
    pldmgr_log("[PLDMGR] app_tile: %s 的%s原始值已留底（%s）\n", tid,
               is_bg ? "背景" : "图标", v[0] ? v : "(NULL)");
    return 0;
}

int app_tile_orig_read(const char *tid, int is_bg, char *out, size_t osz, int *is_null) {
    if (out && osz) out[0] = 0;
    if (is_null) *is_null = 0;
    if (!out || osz < 2) return -1;
    char d[768], p[900];
    if (app_tile_dir(tid, d, sizeof(d)) != 0) return -1;
    at_orig_path(d, is_bg, p, sizeof(p));
    if (!at_present(p)) return -1;
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, osz - 1, f);
    fclose(f);
    out[n] = 0;
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    if (is_null) *is_null = (n == 0);
    return 0;
}
