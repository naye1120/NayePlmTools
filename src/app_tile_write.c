/* [CN-FORK] v0.6.0.179 内置软件的磁贴图 —— **写主机那一层**。
 *
 * 面板这一份的路径 / 版本号 / 底片在 app_tile.c；这里只做三件事：
 *   ① 把入参 PNG 变成「确定能装」的那两份（图标 = PNG；背景 = PNG + BC1 DDS），
 *      规格全部向 game_title.c 那套已真机验证过的口径借（不另抄一份）；
 *   ② 落盘到我们自己的目录（app_tile_store_*）；
 *   ③ 把 app.db 里对应的那一列指过去（tile_db_set_image）。
 *
 * 为什么写的是**列指针**而不是像游戏那样写进 sce_sys：见 include/app_tile.h
 * 的头注释（真机取证里内置软件的 icon0Info 就指着 /system_ex 只读区里的文件，
 * 而 game_mgr_sce_sys_dir 对 NPXS 一律返回 0）。一句话：写不进去的地方别硬写。
 *
 * 三个「不许假成功」的点，都在这里落实：
 *   · 主机那一列没写进去 → message 里明说（调用方照旧回 200，因为面板这一份是真的）；
 *   · 底片一定在**第一次动手之前**留（写完之后再读回来就是我们改过的值了）；
 *   · 读不到主机那一行时**不写主机**（没有底片 = 将来还原不了），也明说。 */

#include "app_tile.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_title.h"
#include "pldmgr.h"
#include "tile_db.h"

/* 拿主机那一列的原始值留底。返回 1 = 拿到并留好了底片，0 = 读不到（那一行还没有）。
 * 底片已经存在时 app_tile_orig_save_once 自己会跳过，所以这里不用先问。 */
static int at_save_orig(const char *tid, int is_bg, const char *column) {
    char cur[1024];
    int is_null = 0;
    if (tile_db_get_image(tid, column, cur, sizeof(cur), &is_null) != 0) {
        pldmgr_log("[PLDMGR] app_tile: %s 读不到 %s 的原始值（主机上还没有这一行？）\n",
                   tid, column);
        return 0;
    }
    return app_tile_orig_save_once(tid, is_bg, is_null ? NULL : cur) == 0 ? 1 : 0;
}

/* 把一列指到我们自己的文件上。0 = 主机已登记；非 0 = 没写成（hmsg 给人话）。 */
static int at_register(const char *tid, const char *column, const char *path,
                       char *hmsg, size_t hmsgsz) {
    char m[192];
    int rc = tile_db_set_image(tid, column, path, m, sizeof(m));
    if (rc != 0) {
        pldmgr_log("[PLDMGR] app_tile: %s 的 %s 没登记进 app.db：%s\n", tid, column,
                   m[0] ? m : "见上一条日志");
        if (hmsg && hmsgsz) snprintf(hmsg, hmsgsz, "%s", m[0] ? m : "主机数据库写不进去");
        return rc;
    }
    return 0;
}

int app_tile_set_icon(const char *tid, const unsigned char *png, size_t len,
                      long long *out_bytes, int *out_host_ok,
                      char *msg, size_t msgsz) {
    if (out_bytes) *out_bytes = 0;
    if (out_host_ok) *out_host_ok = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!app_tile_is_builtin(tid)) {
        if (msg) snprintf(msg, msgsz, "只能给主机内置软件换图标");
        return -1;
    }
    if (!png || len == 0) {
        if (msg) snprintf(msg, msgsz, "图片为空");
        return -1;
    }
    if (len > APP_TILE_MAX_PNG) {
        if (msg) snprintf(msg, msgsz, "图片过大（上限 64MB）");
        return -1;
    }

    /* ① 先留底片。顺序不能反 —— 写完再读回来就是我们自己那一串了。 */
    int have_orig = at_save_orig(tid, 0, APP_TILE_ICON_COLUMN);

    /* ② 规范化成 PNG 并落盘（面板这一份是接口的事实源）。 */
    unsigned char *store = NULL;
    size_t store_len = 0;
    if (game_title_img_normalize_png(png, len, &store, &store_len, msg, msgsz) != 0)
        return -1;
    if (app_tile_store_icon(tid, store, store_len) != 0) {
        free(store);
        if (msg) snprintf(msg, msgsz, "写入面板目录失败（%s）", strerror(errno));
        return -2;
    }
    free(store);

    /* ③ 登记进主机的 icon0Info。 */
    char p[900];
    if (app_tile_icon_file(tid, p, sizeof(p)) != 0) {
        if (msg) snprintf(msg, msgsz, "图标没落盘，主机那一列不动");
        return -2;
    }
    if (!have_orig) {
        /* 没有底片就不写主机：将来「恢复默认」还原不回去，主屏会指着一个不存在的文件。 */
        if (msg) snprintf(msg, msgsz, "已存到面板；主机数据库里读不到这条的图标记录，没敢写主机");
        return 0;
    }
    char hmsg[192];
    if (at_register(tid, APP_TILE_ICON_COLUMN, p, hmsg, sizeof(hmsg)) != 0) {
        if (msg) snprintf(msg, msgsz, "已存到面板，但主机没写进去：%s", hmsg);
        return 0;
    }
    if (out_host_ok) *out_host_ok = 1;
    if (out_bytes) *out_bytes = (long long)len;
    if (msg) snprintf(msg, msgsz, "图标已写入主机（重启或待机唤醒后主屏生效）");
    pldmgr_log("[PLDMGR] app_tile: %s icon set (%zu bytes)\n", tid, len);
    return 0;
}

int app_tile_set_bg(const char *tid, const unsigned char *png, size_t len,
                    long long *out_bytes, long long *out_dds, int *out_host_ok,
                    char *msg, size_t msgsz) {
    if (out_bytes) *out_bytes = 0;
    if (out_dds) *out_dds = 0;
    if (out_host_ok) *out_host_ok = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!app_tile_is_builtin(tid)) {
        if (msg) snprintf(msg, msgsz, "只能给主机内置软件换背景");
        return -1;
    }
    if (!png || len == 0) {
        if (msg) snprintf(msg, msgsz, "图片为空");
        return -1;
    }
    if (len > APP_TILE_MAX_PNG) {
        if (msg) snprintf(msg, msgsz, "图片过大（上限 64MB）");
        return -1;
    }

    int have_orig = at_save_orig(tid, 1, TILE_DB_BG_COLUMN);

    /* 1920×1080 的 PNG（面板预览）+ BC1 DDS（主机那一列指的）。 */
    unsigned char *dds = NULL, *pic0 = NULL;
    size_t dds_len = 0, pic0_len = 0;
    if (game_title_img_encode_bg(png, len, &dds, &dds_len, &pic0, &pic0_len,
                                 msg, msgsz) != 0)
        return -1;
    if (app_tile_store_bg(tid, dds, dds_len, pic0, pic0_len) != 0) {
        free(dds);
        free(pic0);
        if (msg) snprintf(msg, msgsz, "写入面板目录失败（%s）", strerror(errno));
        return -2;
    }
    free(pic0);

    char p[900];
    if (app_tile_bg_dds_file(tid, p, sizeof(p)) != 0) {
        free(dds);
        if (msg) snprintf(msg, msgsz, "背景没落盘，主机那一列不动");
        return -2;
    }
    if (!have_orig) {
        free(dds);
        if (msg) snprintf(msg, msgsz, "已存到面板；主机数据库里读不到这条的背景记录，没敢写主机");
        return 0;
    }
    char hmsg[192];
    int rc = at_register(tid, TILE_DB_BG_COLUMN, p, hmsg, sizeof(hmsg));
    long long dl = (long long)dds_len;
    free(dds);
    if (rc != 0) {
        if (msg) snprintf(msg, msgsz, "已存到面板，但主机没写进去：%s", hmsg);
        return 0;
    }
    if (out_host_ok) *out_host_ok = 1;
    if (out_bytes) *out_bytes = (long long)len;
    if (out_dds) *out_dds = dl;
    if (msg) snprintf(msg, msgsz, "背景已写入主机（主屏实时重绘；没变的话待机唤醒一次）");
    pldmgr_log("[PLDMGR] app_tile: %s bg set (%zu bytes -> dds %lld)\n", tid, len, dl);
    return 0;
}

/* ── 还原 ────────────────────────────────────────────────────────────────── */

/* 还原一条。每一条都走同一个形状：先读底片 → 写回主机那一列 → 删掉面板这一份。
 *
 * 底片**不删**：它才是「主机的原值」唯一一份记录，删了就再也拿不回来
 * （下一次上传时 save_once 也不会覆盖它，所以反复「换图 → 恢复」都是回到出厂那条）。
 * 没留过底片 = 我们从没写过主机那一列 → 只删面板这一份，主机一个字节都不动。 */
static int at_reset(const char *tid, int is_bg, const char *column,
                    char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!app_tile_is_builtin(tid)) {
        if (msg) snprintf(msg, msgsz, "只能操作主机内置软件");
        return -1;
    }

    char v[1024];
    int is_null = 0;
    int had = (app_tile_orig_read(tid, is_bg, v, sizeof(v), &is_null) == 0);

    if (is_bg) app_tile_drop_bg(tid);
    else app_tile_drop_icon(tid);

    if (!had) {
        if (msg) snprintf(msg, msgsz, "已恢复默认");
        return 0;
    }
    char hmsg[192];
    int rc = tile_db_set_image_raw(tid, column, is_null ? NULL : v, hmsg, sizeof(hmsg));
    if (rc != 0) {
        if (msg) snprintf(msg, msgsz, "面板这一份已删，但主机那一列没还原成：%s", hmsg);
        return -2;
    }
    if (msg) snprintf(msg, msgsz, "已恢复主机原来的那一份");
    pldmgr_log("[PLDMGR] app_tile: %s %s reset -> %s\n", tid, column,
               is_null ? "(NULL)" : v);
    return 0;
}

int app_tile_reset_icon(const char *tid, char *msg, size_t msgsz) {
    return at_reset(tid, 0, APP_TILE_ICON_COLUMN, msg, msgsz);
}

int app_tile_reset_bg(const char *tid, char *msg, size_t msgsz) {
    return at_reset(tid, 1, TILE_DB_BG_COLUMN, msg, msgsz);
}

/* ── 主机自带图标的路径（/app_icon 的兜底）────────────────────────────────── */

int app_tile_host_icon_path(const char *tid, char *out, size_t osz) {
    if (out && osz) out[0] = 0;
    if (!out || osz < 8 || !app_tile_is_builtin(tid)) return -1;
    char v[1024];
    int is_null = 0;
    if (tile_db_get_image(tid, APP_TILE_ICON_COLUMN, v, sizeof(v), &is_null) != 0)
        return -1;
    if (is_null || !v[0]) return -1;
    /* 存进去的形状是 `<路径>?ts=<秒>`（写侧就是那么拼的），取问号前那一段。 */
    char *q = strchr(v, '?');
    if (q) *q = 0;
    if (!v[0] || v[0] != '/') return -1; /* 不是绝对路径：宁可不给，也别让前端去猜 */
    snprintf(out, osz, "%s", v);
    return 0;
}

/* ── 开机对账 ────────────────────────────────────────────────────────────── */

/* 把一条重新登记一遍；一致就一次写都不发。返回 1 = 写了，0 = 已一致/没我们这份，
 * -1 = 写了但失败。 */
static int at_reapply(const char *tid, int is_bg, const char *column) {
    char p[900];
    int have = is_bg ? app_tile_bg_dds_file(tid, p, sizeof(p))
                     : app_tile_icon_file(tid, p, sizeof(p));
    if (have != 0) return 0;

    /* 已经指着我们这份（路径相同、只是 ?ts= 可能旧了）就跳过 —— 对账每次开机都跑，
     * 一致时不开事务是这个仓库的老规矩（见 game_title_sync_all 的注释）。 */
    char cur[1024];
    int is_null = 0;
    if (tile_db_get_image(tid, column, cur, sizeof(cur), &is_null) == 0 && !is_null) {
        char *q = strchr(cur, '?');
        if (q) *q = 0;
        if (strcmp(cur, p) == 0) return 0;
    }
    char hmsg[192];
    if (at_register(tid, column, p, hmsg, sizeof(hmsg)) != 0) return -1;
    return 1;
}

int app_tile_sync_all(char *msg, size_t msgsz, int *checked, int *applied, int *failed) {
    if (msg && msgsz) msg[0] = 0;
    if (checked) *checked = 0;
    if (applied) *applied = 0;
    if (failed) *failed = 0;

    DIR *dp = opendir(APP_TILE_DIR);
    if (!dp) {
        /* 目录还没建 = 用户一张图都没设过。这不是错误，静默返回。 */
        if (msg) snprintf(msg, msgsz, "还没有自定义的图标/背景");
        return 0;
    }

    int n_ck = 0, n_ap = 0, n_fa = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (!app_tile_is_builtin(de->d_name)) continue;
        n_ck++;
        int r1 = at_reapply(de->d_name, 0, APP_TILE_ICON_COLUMN);
        int r2 = at_reapply(de->d_name, 1, TILE_DB_BG_COLUMN);
        if (r1 > 0) n_ap++;
        if (r2 > 0) n_ap++;
        if (r1 < 0 || r2 < 0) n_fa++;
    }
    closedir(dp);

    if (checked) *checked = n_ck;
    if (applied) *applied = n_ap;
    if (failed) *failed = n_fa;
    if (msg) snprintf(msg, msgsz, "对账 %d 条：重写 %d、失败 %d", n_ck, n_ap, n_fa);
    return 0;
}
