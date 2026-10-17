/* [CN-FORK] v0.6.0.179 内置软件换图标 / 换背景 —— **写主机 app.db 两列**那一段
 * （src/app_tile_write.c）的宿主测试。
 *
 * 为什么必须有一条：这一轮改的是「主机的应用数据库」，而真机上它不可达
 * （要插上电视点一次才知道主屏认不认）。旧的那条路（写游戏目录）在真机上
 * 就是「面板说成功、主屏一动不动」—— 与 `.164` 置顶是同一个病。所以判据要在这里钉死：
 *   ① 写完之后 app.db 那两列**指到我们自己的文件**（形状 `<路径>?ts=<mtime>`）；
 *   ② 底片留的是**动手之前**那一串，且「恢复默认」按原样写回；
 *   ③ `pic0Info` 出厂是 SQL NULL：还原必须还原成 NULL，**不是空串**
 *      （写空串主屏会照着一张不存在的图去画）；
 *   ④ 主机那一行读不到时**不写主机**，但面板这一份照存 —— 并且 `host_ok` 必须回 0、
 *      message 里说明白。这就是「不假成功」的落点：不许再出现「界面说好了、电视上没变」。
 *
 * 夹具（app.db）用 sqlite3 现造，形状照真机原件裁剪：tbl_contentinfo(titleId,
 * icon0Info, pic0Info)。图片用仓库自己的 stb 编码器现编一张真 PNG —— 手搓字节
 * 会让「这不是一张能读的 PNG」那条分支永远测不出来。
 *
 * game_title.c 在这里**按普通 TU 链接**（它的两份规格函数是本测试的被测对象之一），
 * 所以要把它引用的 game_mgr_* 打桩（与 test/param_rename_test.c 同一手法）。 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_tile.h"
#include "sqlite3.h"
#include "stb_image_write.h"
#include "tile_db.h"

/* log_server.c 不进来：日志直接打 stderr（失败时看得见原因，与别的宿主测试同手法）。 */
void pldmgr_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* game_title.c 引用的三个符号（本测试不测改名那条链，全部空实现）。 */
void game_mgr_invalidate(void) {}
int game_mgr_sce_sys_dir(const char *tid, char *out, size_t osz) {
    (void)tid; if (osz && out) out[0] = 0; return 0;
}
int game_mgr_source_paths(const char *tid, char *kind, size_t ksz,
                          char *out, size_t osz) {
    (void)tid; if (ksz && kind) kind[0] = 0; if (osz && out) out[0] = 0; return -1;
}

static int fails = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("  FAIL ");                                             \
            printf(__VA_ARGS__);                                           \
            printf("   [%s:%d]\n", __FILE__, __LINE__);                    \
            fails++;                                                       \
        } else {                                                           \
            printf("  ok   ");                                             \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

#define ROOT   "/tmp/pldmgr_apptilew_test"
#define DB     ROOT "/app.db"
#define TID    "NPXS40047"
/* 真机取证里内置软件的 icon0Info 长这样（只读系统区 + ?ts= 后缀）。 */
#define ORIG_ICON "/system_ex/rnps/apps/NPXS40047/appdb/default/icon0.png?ts=111"

/* ── 现编一张真 PNG ─────────────────────────────────────────────────────── */

typedef struct { unsigned char *p; size_t len, cap; } wr_t;

static void wr_cb(void *ctx, void *data, int size) {
    wr_t *m = (wr_t *)ctx;
    if (size <= 0) return;
    if (m->len + (size_t)size > m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 4096;
        while (nc < m->len + (size_t)size) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(m->p, nc);
        if (!np) return;
        m->p = np;
        m->cap = nc;
    }
    memcpy(m->p + m->len, data, (size_t)size);
    m->len += (size_t)size;
}

static unsigned char *make_png(size_t *out_len, int w, int h, int seed) {
    static unsigned char px[64 * 64 * 3];
    for (int i = 0; i < w * h * 3; i++) px[i] = (unsigned char)((i * 7 + seed) & 0xFF);
    wr_t m;
    memset(&m, 0, sizeof(m));
    if (!stbi_write_png_to_func(wr_cb, &m, w, h, 3, px, w * 3) || !m.p) {
        free(m.p);
        return NULL;
    }
    *out_len = m.len;
    return m.p;
}

/* ── 夹具 ───────────────────────────────────────────────────────────────── */

/* 夹具库这一侧必须带 CREATE：tile_db.c 用的是纯 READWRITE（真机上那两个库一定已经
 * 存在），照抄它就会得到一个「文件不存在 → 打开失败 → 静默按没有这一行处理」的假夹具 ——
 * 那样这条测试会一路绿，却什么都没测到。 */
static sqlite3 *open_db(int create) {
    sqlite3 *db = NULL;
    int flags = create ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
                       : SQLITE_OPEN_READONLY;
    if (sqlite3_open_v2(DB, &db, flags, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return NULL;
    }
    return db;
}

static void make_db(void) {
    unlink(DB);
    sqlite3 *db = open_db(1);
    if (!db) { printf("  (夹具 app.db 建不出来)\n"); return; }
    char *err = NULL;
    sqlite3_exec(db,
                 "CREATE TABLE tbl_contentinfo (titleId TEXT PRIMARY KEY,"
                 " localConceptId TEXT, icon0Info TEXT, pic0Info TEXT);",
                 NULL, NULL, &err);
    if (err) { printf("  (夹具建表失败: %s)\n", err); sqlite3_free(err); }
    /* 只有这一行：icon0Info 指系统只读区，pic0Info 出厂就是 **NULL**。 */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO tbl_contentinfo VALUES ('%s','cid:scp:1','%s',NULL);",
             TID, ORIG_ICON);
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
}

/* 读主机那一列（0 = 有这一行；*is_null 告诉是不是 NULL）。 */
static int db_get(const char *column, char *out, size_t osz, int *is_null) {
    return tile_db_get_image(TID, column, out, osz, is_null);
}

static const char *leaf_of(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

int main(void) {
    system("rm -rf " ROOT);
    mkdir(ROOT, 0777);
    make_db();

    size_t ilen = 0, blen = 0;
    unsigned char *ipng = make_png(&ilen, 16, 16, 3);
    unsigned char *bpng = make_png(&blen, 32, 18, 9);
    if (!ipng || !bpng) {
        printf("  现编 PNG 失败（stb）—— 本测试的前提\n");
        return 1;
    }

    printf("=== 换图标：写面板 + 把 icon0Info 指过去 ===\n");
    {
        long long bytes = 0;
        int host_ok = -1;
        char msg[192];
        int rc = app_tile_set_icon(TID, ipng, ilen, &bytes, &host_ok, msg, sizeof(msg));
        CHECK(rc == 0, "上传应成功（rc=%d msg=%s）", rc, msg);
        CHECK(bytes == (long long)ilen, "回给前端的字节数应是原 PNG 大小");
        CHECK(host_ok == 1, "主机那一列写成了 → host_ok 必须为 1");
        char f[900];
        CHECK(app_tile_icon_file(TID, f, sizeof(f)) == 0, "面板这一份应落盘");
        char v[1024];
        int is_null = -1;
        CHECK(db_get(APP_TILE_ICON_COLUMN, v, sizeof(v), &is_null) == 0 && !is_null,
              "主机 icon0Info 应已写上（%s）", v);
        CHECK(strstr(v, "apptiles/") != NULL && strstr(v, "icon0.png?ts=") != NULL,
              "那一列的形状必须是 <我们这份文件>?ts=<秒>（%s）", v);
        /* 底片留的是**动手之前**那一串，不是我们刚写进去的。 */
        char o[1024];
        int onull = -1;
        CHECK(app_tile_orig_read(TID, 0, o, sizeof(o), &onull) == 0 && onull == 0 &&
              strcmp(o, ORIG_ICON) == 0, "底片必须是最初那一串（%s）", o);
    }

    printf("=== 换背景：两份文件（dds 给主机、png 给面板预览）+ NULL 底片 ===\n");
    {
        long long bytes = 0, dds = 0;
        int host_ok = -1;
        char msg[192];
        int rc = app_tile_set_bg(TID, bpng, blen, &bytes, &dds, &host_ok, msg, sizeof(msg));
        CHECK(rc == 0, "上传背景应成功（rc=%d msg=%s）", rc, msg);
        CHECK(dds > 0, "主机那份 DDS 的字节数应带出来（%lld）", dds);
        CHECK(host_ok == 1, "背景也写进主机了 → host_ok 必须为 1");
        char f[900];
        CHECK(app_tile_bg_dds_file(TID, f, sizeof(f)) == 0 &&
              strcmp(leaf_of(f), "pic0.dds") == 0,
              "落点文件名必须是 pic0.dds（.118 消融的结论）");
        CHECK(app_tile_bg_png_file(TID, f, sizeof(f)) == 0,
              "面板预览那份 bg.png 也要在");
        char v[1024];
        int is_null = -1;
        CHECK(db_get(TILE_DB_BG_COLUMN, v, sizeof(v), &is_null) == 0 && !is_null &&
              strstr(v, "pic0.dds?ts=") != NULL,
              "主机 pic0Info 应指着我们的 pic0.dds（%s）", v);
        int onull = -1;
        char o[8];
        CHECK(app_tile_orig_read(TID, 1, o, sizeof(o), &onull) == 0 && onull == 1,
              "pic0Info 出厂是 NULL：底片必须记成 NULL");
    }

    printf("=== 恢复默认：按原样写回（NULL 要回 NULL，不是空串）===\n");
    {
        char msg[192];
        CHECK(app_tile_reset_bg(TID, msg, sizeof(msg)) == 0, "恢复背景应成功（%s）", msg);
        char v[1024];
        int is_null = -1;
        CHECK(db_get(TILE_DB_BG_COLUMN, v, sizeof(v), &is_null) == 0 && is_null == 1,
              "还原后 pic0Info 必须是 **SQL NULL**，写空串主屏会照着一张不存在的图去画");
        char f[900];
        CHECK(app_tile_bg_dds_file(TID, f, sizeof(f)) != 0 &&
              app_tile_bg_png_file(TID, f, sizeof(f)) != 0,
              "面板这两份都应删掉（不然 /app_tile_bg 还会回旧图）");

        CHECK(app_tile_reset_icon(TID, msg, sizeof(msg)) == 0, "恢复图标应成功（%s）", msg);
        CHECK(db_get(APP_TILE_ICON_COLUMN, v, sizeof(v), &is_null) == 0 && is_null == 0 &&
              strcmp(v, ORIG_ICON) == 0,
              "还原后 icon0Info 必须**逐字**回到最初那一串（%s）", v);
        CHECK(app_tile_icon_file(TID, f, sizeof(f)) != 0, "面板这份图标也应删掉");
        /* /app_icon 的兜底：还原之后主机那一列指着只读区，取出来应是它（去掉 ?ts=）。 */
        char hp[1024];
        CHECK(app_tile_host_icon_path(TID, hp, sizeof(hp)) == 0 &&
              strcmp(hp, "/system_ex/rnps/apps/NPXS40047/appdb/default/icon0.png") == 0,
              "主机自带图标路径应能从那一列取出来（%s）", hp);
    }

    printf("=== 主机没有这一行：面板照存，但 host_ok=0 且说明白 ===\n");
    {
        /* 这是「不假成功」那条规矩的落点：写不进主机**不许**回一个干净的成功。 */
        long long bytes = 0;
        int host_ok = -1;
        char msg[192];
        int rc = app_tile_set_icon("NPXS40099", ipng, ilen, &bytes, &host_ok, msg, sizeof(msg));
        CHECK(rc == 0, "面板这一份是真的 → 接口层仍是成功（rc=%d）", rc);
        CHECK(host_ok == 0, "主机那一列没写成 → host_ok 必须为 0");
        CHECK(strstr(msg, "没敢写主机") != NULL || strstr(msg, "没写进去") != NULL,
              "message 里必须说明白，不能让人以为主屏会变（%s）", msg);
        char f[900];
        CHECK(app_tile_icon_file("NPXS40099", f, sizeof(f)) == 0,
              "面板这一份照样存好了（列表里的图标读它）");
        /* 没留成底片时「恢复默认」只删面板那一份，主机一个字节都不碰。 */
        CHECK(app_tile_reset_icon("NPXS40099", msg, sizeof(msg)) == 0,
              "没写过主机的条目，恢复默认应成功（%s）", msg);
    }

    printf("=== 拒收：非法编号 / 坏图 ===\n");
    {
        char msg[192];
        long long bytes = 0;
        int host_ok = 0;
        CHECK(app_tile_set_icon("PPSA00001", ipng, ilen, &bytes, &host_ok, msg, sizeof(msg)) == -1,
              "游戏编号必须被拒（面板目录名由 tid 拼出来，放进来就是路径穿越）");
        CHECK(app_tile_set_icon(TID, (const unsigned char *)"not-a-png", 9, &bytes, &host_ok,
                                msg, sizeof(msg)) == -1,
              "不是 PNG 必须被拒（%s）", msg);
        CHECK(app_tile_set_bg(TID, (const unsigned char *)"not-a-png", 9, &bytes, &bytes,
                              &host_ok, msg, sizeof(msg)) == -1,
              "背景同样只收能解出来的 PNG");
    }

    free(ipng);
    free(bpng);
    printf("\n%s（%d 项失败）\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
