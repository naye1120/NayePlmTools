/* [CN-FORK] v0.5.2.166 主屏固定列表（src/tile_group.c）的宿主测试。
 *
 * 为什么这条必须有：`.164` 的置顶写的是**我们自己的 JSON**，主机压根不知道，
 * 用户看到的就是「置顶按钮在 PS5 里没有生效」。这一轮把它改成写主机 app.db 的
 * `tbl_group_<uid>`（groupName='MyHome'，idx = 主屏位置）—— 那是**主机的**库，
 * 写错一行轻则顺序乱、重则主屏列表错位，而真机现在不可达（8911 / 10101 都拒连），
 * 所以必须在一份合成的 app.db 上把判据钉死：
 *
 *   ① 读得到：跨账号合成、按 idx 升序、去重取最小 idx；
 *   ② 置顶：插到最前 + **其余条目依次后移重新编号**（只改一个 idx 会让两条撞车）；
 *   ③ 幂等：已经排在第一的再点一次置顶，一行都不许动；
 *   ④ 取消置顶：删掉 + 剩下的重新编号；
 *   ⑤ 重排：只动「在组里且被提到」的条目，且**槽位集合不变**（没被提到的条目
 *      一个字节都不动 —— 系统条目就在里面）；
 *   ⑥ 越界：tid 带引号 / 分号一律拒；非 MyHome 的组名一律不碰；
 *      名字带前缀但列不齐的表（tbl_group_extra）必须跳过。
 *
 * 夹具用 sqlite3 现造，不依赖 python3（少一个「缺 python3 就 SKIP」的口子），
 * 数据形状照 `_ftp_probe/evidence/recon3/app.db` 那份真机原件的 4 个账号裁剪：
 * 账号 …023 只有 3 条系统条目，…025/026/027 多出用户自己固定的
 * `PPSA32786`（仁王 3）与 `WKAL00001`（WebKit Autoloader）。
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sqlite3.h"
#include "tile_group.h"

/* pldmgr_log 的桩：与 test/app_visibility_test.c 同一手法（log_server.c 不进来）。 */
void pldmgr_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
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

#define ROOT "/tmp/pldmgr_tilegrp_test"
#define DB ROOT "/app.db"

static void xexec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        printf("  夹具 SQL 失败: %s\n    %s\n", sql, err ? err : "");
        if (err) sqlite3_free(err);
        exit(1);
    }
}

static void xrow(sqlite3 *db, const char *table, const char *id,
                 const char *group, int idx) {
    char sql[512];
    sqlite3_stmt *st = NULL;
    snprintf(sql, sizeof(sql),
             "INSERT INTO %s (itemId, groupName, type, idx, lastModifiedDate) "
             "VALUES (?, ?, 'title', ?, NULL)", table);
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        printf("  夹具 prepare 失败: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    sqlite3_bind_text(st, 1, id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, group, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, idx);
    if (sqlite3_step(st) != SQLITE_DONE) {
        printf("  夹具 insert 失败: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    sqlite3_finalize(st);
}

static void make_fixture(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open(DB, &db) != SQLITE_OK) { printf("夹具库建不起来\n"); exit(1); }

    const char *accs[] = {"0438122023", "0438122025", "0438122026", "0438122027"};
    for (int i = 0; i < 4; i++) {
        char full[512];
        snprintf(full, sizeof(full),
                 "CREATE TABLE tbl_group_%s (itemId TEXT NOT NULL,groupName TEXT NOT NULL,"
                 "type TEXT NOT NULL,idx INT,lastModifiedDate TEXT,"
                 "PRIMARY KEY(itemId,groupName))", accs[i]);
        xexec(db, full);
    }

    /* …023：只有三条系统条目（真机原样）。 */
    xrow(db, "tbl_group_0438122023", "NPXS40037", "MyHome", 1);
    xrow(db, "tbl_group_0438122023", "NPXS40047", "MyHome", 2);
    xrow(db, "tbl_group_0438122023", "NPXS40071", "MyHome", 3);

    /* …025/026/027：多出用户自己固定的两个（仁王 3 与 WebKit Autoloader）。 */
    const char *acc5[] = {"0438122025", "0438122026", "0438122027"};
    for (int i = 0; i < 3; i++) {
        char t[128];
        snprintf(t, sizeof(t), "tbl_group_%s", acc5[i]);
        xrow(db, t, "NPXS40037", "MyHome", 1);
        xrow(db, t, "NPXS40047", "MyHome", 2);
        xrow(db, t, "PPSA32786", "MyHome", 3);
        xrow(db, t, "WKAL00001", "MyHome", 4);
        xrow(db, t, "NPXS40071", "MyHome", 5);
        /* 另一组：证明「只动 MyHome」不是嘴上说说。 */
        xrow(db, t, "NPXS99999", "OtherGroup", 7);
    }

    /* 名字带前缀、但列不齐 → 必须被跳过（否则 SQL 直接报错）。 */
    xexec(db, "CREATE TABLE tbl_group_extra (itemId TEXT, whatever INT)");
    /* 名字不带前缀、列齐全 → 也必须被跳过。 */
    xexec(db, "CREATE TABLE tbl_iconinfo_0438122025 (titleId TEXT, visible INT)");

    sqlite3_close(db);
}

/* 把一张表的 MyHome 组读成 "ID:idx ID:idx ..."（按 idx 升序）。 */
static void dump_group(const char *table, char *out, size_t osz) {
    sqlite3 *db = NULL;
    out[0] = 0;
    if (sqlite3_open(DB, &db) != SQLITE_OK) return;
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT itemId, idx FROM %s WHERE groupName='MyHome' "
             "ORDER BY (idx IS NULL), idx, itemId", table);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        size_t used = 0;
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *id = (const char *)sqlite3_column_text(st, 0);
            int idx = sqlite3_column_int(st, 1);
            int n = snprintf(out + used, osz - used, "%s%s:%d",
                             used ? " " : "", id ? id : "?", idx);
            if (n < 0 || (size_t)n >= osz - used) break;
            used += (size_t)n;
        }
    }
    if (st) sqlite3_finalize(st);
    sqlite3_close(db);
}

/* 查一个组里某条目还在不在、idx 是多少。找不到回 -1。 */
static int group_idx(const char *table, const char *id, const char *group) {
    sqlite3 *db = NULL;
    int rc = -1;
    if (sqlite3_open(DB, &db) != SQLITE_OK) return -1;
    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT idx FROM %s WHERE itemId=? AND groupName=?",
             table);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, id, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, group, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) rc = sqlite3_column_int(st, 0);
    }
    if (st) sqlite3_finalize(st);
    sqlite3_close(db);
    return rc;
}

/* JSON 里有没有某个子串（这份列表很小，子串足够，且不引入 JSON 解析依赖）。 */
static int has(const char *hay, const char *needle) {
    return hay && strstr(hay, needle) != NULL;
}

int main(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", ROOT);
    if (system(cmd) != 0) { /* 目录本来不存在也算正常 */ }
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", ROOT);
    if (system(cmd) != 0) { printf("建不出夹具目录\n"); return 1; }
    make_fixture();

    static char json[8192];
    char buf[1024];
    char msg[256];
    int accounts = 0, idx = 0, moved = 0;

    printf("=== ① 读列表：跨账号合成 + 去重取最小 idx ===\n");
    {
        int n = tile_group_list_json(json, sizeof(json));
        CHECK(n > 0, "list_json 返回正长度（%d）", n);
        CHECK(has(json, "\"ok\":true"), "ok=true");
        CHECK(has(json, "\"accounts\":4"), "accounts=4（4 个账号的 tbl_group 都读到）");
        CHECK(has(json, "\"itemId\":\"NPXS40037\",\"idx\":1"), "NPXS40037 在 idx 1");
        CHECK(has(json, "\"itemId\":\"PPSA32786\",\"idx\":3"), "PPSA32786（仁王 3）在 idx 3");
        CHECK(has(json, "\"itemId\":\"WKAL00001\",\"idx\":4"), "WKAL00001 在 idx 4");
        CHECK(has(json, "\"itemId\":\"NPXS40071\",\"idx\":5"), "NPXS40071 在 idx 5");
        CHECK(!has(json, "NPXS99999"), "OtherGroup 里的条目没被算进来");
        CHECK(!has(json, "NPXS99999\""), "（同上，双重确认）");
    }

    printf("=== ② 置顶：插到最前，其余依次后移重新编号 ===\n");
    {
        int r = tile_group_set_pin("CUSA00001", 1, msg, sizeof(msg), &accounts, &idx);
        CHECK(r == 0, "置顶成功（r=%d msg=%s）", r, msg);
        CHECK(accounts == 4, "4 个账号都写了（写了 %d 个）", accounts);
        CHECK(idx == 1, "回的位置是 1（回了 %d）", idx);

        dump_group("tbl_group_0438122025", buf, sizeof(buf));
        CHECK(strcmp(buf, "CUSA00001:1 NPXS40037:2 NPXS40047:3 PPSA32786:4 WKAL00001:5 NPXS40071:6") == 0,
              "…025 顺序正确（实际 %s）", buf);

        dump_group("tbl_group_0438122023", buf, sizeof(buf));
        CHECK(strcmp(buf, "CUSA00001:1 NPXS40037:2 NPXS40047:3 NPXS40071:4") == 0,
              "…023 原来没有这条，插进去后其余也重编号（实际 %s）", buf);

        CHECK(group_idx("tbl_group_0438122025", "NPXS99999", "OtherGroup") == 7,
              "OtherGroup 那一行一个字节没动");
    }

    printf("=== ③ 幂等：已经排第一的再点一次，一行都不许动 ===\n");
    {
        int r = tile_group_set_pin("CUSA00001", 1, msg, sizeof(msg), &accounts, &idx);
        CHECK(r == 0, "再点一次仍返回 0（r=%d）", r);
        CHECK(accounts == 0, "没写任何账号（accounts=%d）", accounts);
        dump_group("tbl_group_0438122025", buf, sizeof(buf));
        CHECK(strncmp(buf, "CUSA00001:1 ", 12) == 0, "顺序没变（实际 %s）", buf);
    }

    printf("=== ④ 重排：只动被提到的条目，槽位集合不变 ===\n");
    {
        /* 组里现在是 CUSA00001(1) NPXS40037(2) NPXS40047(3) PPSA32786(4) WKAL00001(5) NPXS40071(6)。
         * 把 WKAL00001 与 PPSA32786 换位 → 它们占的槽位 4/5 互换，其余不动。 */
        const char *order[] = {"WKAL00001", "PPSA32786"};
        int r = tile_group_reorder(order, 2, msg, sizeof(msg), &moved);
        CHECK(r == 0, "重排成功（r=%d msg=%s）", r, msg);
        CHECK(moved == 2, "两条真的换了位（moved=%d）", moved);
        dump_group("tbl_group_0438122025", buf, sizeof(buf));
        CHECK(strcmp(buf, "CUSA00001:1 NPXS40037:2 NPXS40047:3 WKAL00001:4 PPSA32786:5 NPXS40071:6") == 0,
              "槽位 4/5 互换、系统条目与首位都没动（实际 %s）", buf);
        CHECK(group_idx("tbl_group_0438122025", "NPXS99999", "OtherGroup") == 7,
              "OtherGroup 依旧没被动");

        /* 顺序没变的重排：一条都不该写。 */
        int r2 = tile_group_reorder(order, 2, msg, sizeof(msg), &moved);
        CHECK(r2 == 0, "同样的顺序再排一次仍返回 0");
        CHECK(moved == 0, "没换位时 moved=0（moved=%d）", moved);
    }

    printf("=== ⑤ 重排只认「已经在组里」的条目 ===\n");
    {
        /* CUSA00099 不在任何组里 → 整个顺序里只有一条在组里 → 少于两条，什么都不做。 */
        const char *order[] = {"CUSA00099", "WKAL00001"};
        int r = tile_group_reorder(order, 2, msg, sizeof(msg), &moved);
        CHECK(r == 0, "不在组里的编号被安静忽略（r=%d）", r);
        CHECK(moved == 0, "没有任何条目动（moved=%d）", moved);
        dump_group("tbl_group_0438122025", buf, sizeof(buf));
        CHECK(strcmp(buf, "CUSA00001:1 NPXS40037:2 NPXS40047:3 WKAL00001:4 PPSA32786:5 NPXS40071:6") == 0,
              "顺序原样（实际 %s）", buf);
    }

    printf("=== ⑥ 取消置顶：删掉 + 剩下的重新编号 ===\n");
    {
        int r = tile_group_set_pin("CUSA00001", 0, msg, sizeof(msg), &accounts, &idx);
        CHECK(r == 0, "取消置顶成功（r=%d msg=%s）", r, msg);
        CHECK(accounts == 4, "4 个账号都删了（写了 %d 个）", accounts);
        CHECK(idx == 0, "取消置顶回 idx=0（回了 %d）", idx);
        dump_group("tbl_group_0438122025", buf, sizeof(buf));
        CHECK(strcmp(buf, "NPXS40037:1 NPXS40047:2 WKAL00001:3 PPSA32786:4 NPXS40071:5") == 0,
              "删掉后编号重新连成 1..5（实际 %s）", buf);
        CHECK(group_idx("tbl_group_0438122025", "CUSA00001", "MyHome") == -1,
              "那一条真的不在组里了");

        /* 再取消一次：本来就没有，什么都不该做。 */
        int r2 = tile_group_set_pin("CUSA00001", 0, msg, sizeof(msg), &accounts, &idx);
        CHECK(r2 == 0 && accounts == 0, "重复取消是空操作（r=%d accounts=%d）", r2, accounts);
    }

    printf("=== ⑦ 越界与注入 ===\n");
    {
        CHECK(tile_group_set_pin("PPSA'1;--", 1, msg, sizeof(msg), NULL, NULL) == -1,
              "tid 带引号/分号被拒");
        CHECK(tile_group_set_pin("ppsa32786", 1, msg, sizeof(msg), NULL, NULL) == -1,
              "小写 tid 被拒");
        CHECK(tile_group_set_pin("", 1, msg, sizeof(msg), NULL, NULL) == -1, "空 tid 被拒");
        const char *bad[] = {"WKAL00001", "PPSA'1;--"};
        CHECK(tile_group_reorder(bad, 2, msg, sizeof(msg), NULL) == -1,
              "重排顺序里有非法编号 → 整条拒绝");
        CHECK(tile_group_reorder(NULL, 0, msg, sizeof(msg), NULL) == -1, "空顺序被拒");
    }

    printf("=== ⑧ 没有 tbl_group 表时：不是崩溃，是「说清楚」 ===\n");
    {
        snprintf(cmd, sizeof(cmd), "rm -f %s", DB);
        if (system(cmd) != 0) { /* 文件没了就行 */ }
        int n = tile_group_list_json(json, sizeof(json));
        CHECK(n > 0 && has(json, "\"ok\":false"), "list_json 仍回结构体、ok=false");
        CHECK(has(json, "\"accounts\":0"), "accounts=0");
        int r = tile_group_set_pin("PPSA32786", 1, msg, sizeof(msg), NULL, NULL);
        CHECK(r == -2, "置顶回 -2（不是崩溃）");
        CHECK(msg[0] != 0, "msg 里有人话（%s）", msg);
    }

    printf("\n%s（%d 处失败）\n", fails ? "TILE_GROUP: FAIL" : "TILE_GROUP: PASS", fails);
    snprintf(cmd, sizeof(cmd), "rm -rf %s", ROOT);
    if (system(cmd) != 0) { /* 收尾失败不影响判定 */ }
    return fails ? 1 : 0;
}
