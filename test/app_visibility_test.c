/* [CN-FORK] v0.5.2.158 「个性化软件」的宿主测试：app_visibility.c 读/写
 * app.db 里每个账号的 tbl_iconinfo_<uid>.visible。
 *
 * 为什么这条**必须**有：真机上「主屏到底画不画这张磁贴」只有插上电视才看得见，
 * 而它写的是**主机的应用数据库**。仓库里对 tile_db 那两条写链就没有宿主测试
 * （它们靠真机消融验证过），但这一条是新猜的判据（visible 那一列），
 * 所以先在合成的 app.db 上把「读得到 / 写对行 / 不越界 / 挡得住注入」钉死。
 *
 * 夹具用 sqlite3 现造，不用 python —— 少一个「缺 python3 就 SKIP」的口子，
 * 数据形状照 _ftp_probe/app.db 那份真机原件的列裁剪。
 *
 * `.160` 在这一关里补的两条钉子（都是真机上看不见、只能在这里钉死的）：
 * ① 面板改不动的固定条目要在列表里带 `controllable:false` + 非空 reason，
 *    且写入一律回 EFIXED、一行都不许动 —— 免得再出现假的「已隐藏」；
 * ② 在册查询失败要说「读不了/正忙」，不能说「主机上没有这个内置软件」。
 * `.161` 又按用户真机口径把名单收窄了一条钉子：**所有应用程序 / 欢迎 是可控的**，
 * 夹具里写它们必须真的落库；名单里只剩两个内容保存库 + 编辑 + 光碟占位。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "app_visibility.h"
#include "app_tile.h"
#include "sqlite3.h"

/* pldmgr_log 的桩：与 test/game_mgr_test.c 同一手法（log_server.c 不进来）。 */
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

#define ROOT "/tmp/pldmgr_appvis_test"
#define DB ROOT "/app.db"

static void xexec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        printf("  夹具 SQL 失败: %s\n    %s\n", sql, err ? err : "");
        if (err) sqlite3_free(err);
        exit(1);
    }
}

static void xinsert(sqlite3 *db, const char *sql, const char *tid, int visible) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        printf("  夹具 prepare 失败: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    sqlite3_bind_text(st, 1, tid, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, visible);
    if (sqlite3_step(st) != SQLITE_DONE) {
        printf("  夹具 step 失败: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    sqlite3_finalize(st);
}

/* 造一份两账号的 app.db。列只留本模块用到的那几列。 */
static void make_db(void) {
    unlink(DB);
    sqlite3 *db = NULL;
    if (sqlite3_open(DB, &db) != SQLITE_OK) { printf("  夹具打不开库\n"); exit(1); }

    xexec(db, "CREATE TABLE tbl_contentinfo (titleId TEXT PRIMARY KEY, titleName TEXT, conceptId INT)");
    xexec(db, "INSERT INTO tbl_contentinfo VALUES ('NPXS40037','PlayStation Plus',0)");
    xexec(db, "INSERT INTO tbl_contentinfo VALUES ('NPXS40047','PlayStation Store',0)");
    xexec(db, "INSERT INTO tbl_contentinfo VALUES ('NPXS40075','媒体相册',0)");
    xexec(db, "INSERT INTO tbl_contentinfo VALUES ('NPXS40074','Remote Play',0)");
    xexec(db, "INSERT INTO tbl_contentinfo VALUES ('NPXS40056','所有应用程序',0)");
    xexec(db, "INSERT INTO tbl_contentinfo VALUES ('NPXS40063','欢迎',0)");
    /* 固定条目（主屏不认 visible 那几个之一）：列表里要带 controllable:false，
     * 写入要在打开库之前就拒掉。名字用真机那一份的「游戏内容保存库」。 */
    xexec(db, "INSERT INTO tbl_contentinfo VALUES ('NPXS40071','游戏内容保存库',0)");
    xexec(db, "INSERT INTO tbl_contentinfo VALUES ('PPSA00001','测试游戏',0)");
    /* NPXS9 开头但**没注册**在 contentinfo 里的行：只有 iconinfo 有它。
     * 写它必须被拒（列表也读不到它）—— 这是「静默 no-op」那条判据。 */

    const char *icon_cols = "(titleId TEXT PRIMARY KEY, titleName TEXT, visible INT, pinnedStatus INT)";
    char sql[512];
    snprintf(sql, sizeof(sql), "CREATE TABLE tbl_iconinfo_111 %s", icon_cols);
    xexec(db, sql);
    snprintf(sql, sizeof(sql), "CREATE TABLE tbl_iconinfo_222 %s", icon_cols);
    xexec(db, sql);

    const char *u1 = "INSERT INTO tbl_iconinfo_111 (titleId, visible) VALUES (?, ?)";
    const char *u2 = "INSERT INTO tbl_iconinfo_222 (titleId, visible) VALUES (?, ?)";
    /* 账号 111：Plus 藏、Store 显、媒体相册藏、RemotePlay 藏、游戏显 */
    xinsert(db, u1, "NPXS40037", 0);
    xinsert(db, u1, "NPXS40047", 1);
    xinsert(db, u1, "NPXS40056", 1);
    xinsert(db, u1, "NPXS40063", 0);
    xinsert(db, u1, "NPXS40075", 0);
    xinsert(db, u1, "NPXS40074", 0);
    xinsert(db, u1, "NPXS40071", 1);
    xinsert(db, u1, "PPSA00001", 1);
    /* 账号 222：只有媒体相册与 111 不一致（那边藏这边显）→ 累积判据走「与」 */
    xinsert(db, u2, "NPXS40037", 0);
    xinsert(db, u2, "NPXS40047", 1);
    xinsert(db, u2, "NPXS40056", 1);
    xinsert(db, u2, "NPXS40063", 0);
    xinsert(db, u2, "NPXS40075", 1);
    xinsert(db, u2, "NPXS40074", 0);
    xinsert(db, u2, "NPXS40071", 1);
    xinsert(db, u2, "PPSA00001", 1);

    /* 两张**不该被碰**的表：
     * ① tbl_concepticoninfo_111 —— 名字里也含 "iconinfo"，但 LIKE 'tbl_iconinfo%' 不匹配它；
     * ② tbl_group_111 —— 同样带 visible 列，但表名不对。
     * 两行都故意填 7，事后核对它还是 7。
     * 再一张「名字对但没有 visible 列」的：靠列探测排除。 */
    xexec(db, "CREATE TABLE tbl_concepticoninfo_111 (localConceptId TEXT PRIMARY KEY, visible INT)");
    xexec(db, "INSERT INTO tbl_concepticoninfo_111 VALUES ('NPXS40047',7)");
    xexec(db, "CREATE TABLE tbl_group_111 (itemId TEXT PRIMARY KEY, visible INT)");
    xexec(db, "INSERT INTO tbl_group_111 VALUES ('NPXS40047',7)");
    xexec(db, "CREATE TABLE tbl_iconinfo_333 (titleId TEXT PRIMARY KEY, titleName TEXT)");

    /* [CN-FORK] v0.6.0.179 主机的主屏固定列表（tbl_group_<uid> 的 MyHome 组）。
     * 真机取证里内置软件**本来就在这个组里**（NPXS40037 / NPXS40047 / NPXS40071），
     * 这一轮「置顶」就是往这里写（`.164` 只写我们自己的 JSON，主机压根不知道，
     * 用户看到的就是「置顶按了没反应、重启就被重置」）。
     * 两张表的条目集合故意不一致：222 只有两条、333 有三条 —— 用来钉「跨账号取并集」，
     * 写侧本来就是一次写所有账号表。上面那张 tbl_group_111 是**形状不对**的诱饵
     * （只有 itemId/visible，没有 groupName/idx）：它必须被 tg_is_group_table 排除，
     * 否则置顶会写进一张缺列的表、SQL 报错、置顶又变成静默失败。 */
    const char *gcols = "(itemId TEXT, groupName TEXT, type TEXT, idx INT, lastModifiedDate TEXT)";
    char gsql[256];
    snprintf(gsql, sizeof(gsql), "CREATE TABLE tbl_group_222 %s", gcols);
    xexec(db, gsql);
    snprintf(gsql, sizeof(gsql), "CREATE TABLE tbl_group_333 %s", gcols);
    xexec(db, gsql);
    xexec(db, "INSERT INTO tbl_group_222 VALUES ('NPXS40047','MyHome','title',1,NULL)");
    xexec(db, "INSERT INTO tbl_group_222 VALUES ('PPSA32786','MyHome','title',2,NULL)");
    xexec(db, "INSERT INTO tbl_group_333 VALUES ('NPXS40071','MyHome','title',1,NULL)");
    xexec(db, "INSERT INTO tbl_group_333 VALUES ('PPSA32786','MyHome','title',2,NULL)");
    xexec(db, "INSERT INTO tbl_group_333 VALUES ('NPXS40037','MyHome','title',3,NULL)");
    /* OtherGroup 里的条目不算「主屏固定」：列表只读 MyHome。 */
    xexec(db, "INSERT INTO tbl_group_333 VALUES ('NPXS40056','OtherGroup','title',1,NULL)");

    sqlite3_close(db);
}

/* 主机固定列表里有没有这条 MyHome 条目（1 = 有）。 */
static int read_group_item(const char *table, const char *tid) {
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(DB, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -999;
    }
    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT count(*) FROM %s WHERE itemId='%s' AND groupName='MyHome'", table, tid);
    sqlite3_stmt *st = NULL;
    int n = -999;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int(st, 0);
    if (st) sqlite3_finalize(st);
    sqlite3_close(db);
    return n > 0 ? 1 : 0;
}

static int read_visible(const char *table, const char *tid) {
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(DB, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -999;
    }
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT visible FROM %s WHERE titleId='%s'", table, tid);
    sqlite3_stmt *st = NULL;
    int v = -999;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
        else v = -1;   /* 没有这一行 */
    }
    if (st) sqlite3_finalize(st);
    sqlite3_close(db);
    return v;
}

static char *list_json(void) {
    char *buf = malloc(65536);
    buf[0] = 0;
    int n = app_vis_list_json(buf, 65536);
    if (n < 0) { printf("  app_vis_list_json 返回 %d\n", n); buf[0] = 0; }
    return buf;
}

int main(void) {
    mkdir(ROOT, 0777);
    make_db();
    printf("=== 列表：只回内置软件，且按「所有账号都隐藏」判定 ===\n");
    char *j = list_json();
    printf("  %s\n", j);
    CHECK(strstr(j, "\"NPXS40037\"") != NULL, "列表里应有 NPXS40037");
    CHECK(strstr(j, "\"PPSA00001\"") == NULL, "列表里不该有游戏 PPSA00001");
    CHECK(strstr(j, "\"accounts\":2") != NULL, "应认出 2 张账号表");
    CHECK(strstr(j, "\"count\":7") != NULL, "应回 7 个内置软件");
    CHECK(strstr(j, "\"error\":\"\"") != NULL, "正常路径 error 应为空串");
    /* 40037 两账号都 0 -> hidden true；40075 一个 0 一个 1 -> hidden false */
    const char *p37 = strstr(j, "\"NPXS40037\"");
    const char *p75 = strstr(j, "\"NPXS40075\"");
    CHECK(p37 && strstr(p37, "\"hidden\":true") != NULL, "两账号都隐藏 -> hidden:true");
    CHECK(p75 && p75 < j + strlen(j) && strstr(p75, "\"hidden\":false") != NULL,
          "只要一个账号还显示 -> hidden:false");
    CHECK(strstr(j, "媒体相册") != NULL, "中文标题应原样出来（UTF-8 不该被转义坏）");

    /* `.160` 的核心判据：面板改不动的那几条要**在列表里就说清楚**，
     * 否则前端只能靠一份硬编码名单猜，猜漏了就又是假的「已隐藏」。 */
    const char *p71 = strstr(j, "\"NPXS40071\"");
    CHECK(p71 != NULL, "固定条目 NPXS40071 也要出现在列表里（说明用，不是开关用）");
    CHECK(p71 && strstr(p71, "\"controllable\":false") != NULL, "NPXS40071 应标 controllable:false");
    CHECK(p71 && strstr(p71, "\"reason\":\"") != NULL &&
          strstr(p71, "\"reason\":\"\"") == NULL, "固定条目必须带一句非空 reason");
    CHECK(p75 && strstr(p75, "\"controllable\":true") != NULL, "普通内置软件应标 controllable:true");

    /* `.161`：用户真机口径「所有应用程序、欢迎 是可以编辑显示和隐藏的」。
     * `.160` 把这两条错关进固定名单 —— 这里钉死它们**必须是可控的**，
     * 免得下次又有人拿出厂值反推规则。欢迎出厂 visible=0，所以它是 hidden:true 但照样可控。
     * ⚠ v0.5.2.164 起每条 app 多一个 `"pinned"` 字段；
     * ⚠ v0.6.0.179 起再多 `"custom_icon"` / `"custom_bg"` 两个（面板里设过自定义图没有）。
     * 位置固定在 hidden 与 controllable 之间 —— 这里的锚点串跟着改，
     * 别把新字段当成「格式坏了」而把它们从响应里删掉。 */
    CHECK(strstr(j, "\"NPXS40056\",\"name\":\"所有应用程序\",\"hidden\":false,\"pinned\":false,\"custom_icon\":0,\"custom_bg\":0,\"controllable\":true") != NULL,
          "所有应用程序应回 controllable:true");
    CHECK(strstr(j, "\"NPXS40063\",\"name\":\"欢迎\",\"hidden\":true,\"pinned\":false,\"custom_icon\":0,\"custom_bg\":0,\"controllable\":true") != NULL,
          "欢迎出厂就是 0：hidden:true 但**不是**不可控");

    /* [CN-FORK] v0.6.0.179 pinned 改成**读主机的固定列表**（tbl_group_<uid> 的 MyHome），
     * **跨账号取并集**：任一账号的固定列表里有它就算固定。取「与」会在真机上造出
     * 「一个账号固定了、另一个没有 → 界面说没固定」这种没法自洽的状态。
     * 上游 `.164` 那份读的是我们自己的 app_pins.json —— 主机压根不认，
     * 所以这里每一句都在钉「读的是主机的库」。 */
    const char *q47p = strstr(j, "\"NPXS40047\"");
    CHECK(q47p && strstr(q47p, "\"pinned\":true") != NULL, "222 的 MyHome 里有它 → pinned:true");
    CHECK(app_pin_get("NPXS40037") == 1, "333 的 MyHome 里有它 → app_pin_get 也是 1");
    CHECK(app_pin_get("NPXS40075") == 0, "两份 MyHome 里都没有它 → 0");
    CHECK(read_group_item("tbl_group_222", "NPXS40056") == 0,
          "OtherGroup 里的条目不算主屏固定，读侧也不该顺手写它");

    /* [CN-FORK] v0.5.2.164 / v0.6.0.179 置顶（app_pin_*）。**状态在主机**：
     * app_pin_set 写 app.db 的 tbl_group_<uid>（groupName='MyHome'），app_pin_get 也读主机；
     * 我们自己那份 app_pins.json 只是意图记录（开机对账用，见下面那一节）。
     * 这里钉四件事：
     *   ① 写进去之后**同一个列表接口**能把它读回来（前端只认这一份，读不回来就等于没存）；
     *   ② 置顶**不许碰 app.db 的 visible**（写错了会变成「点置顶顺手把软件藏了」）；
     *   ③ 主机里那一行真的被插进/删掉了（不是只改了 JSON）；
     *   ④ 取消置顶后读回状态也跟着回 false。 */
    {
        char pmsg[192];
        int pacc = 0, pidx = 0;
        CHECK(app_pin_set("NPXS40075", 1, pmsg, sizeof(pmsg), &pacc, &pidx) == 0,
              "置顶 NPXS40075 应成功（%s）", pmsg);
        CHECK(pacc == 2, "两张账号表都要写上（accounts=%d）", pacc);
        CHECK(pidx == 1, "置顶后位置应是第 1 位（idx=%d）", pidx);
        CHECK(app_pin_get("NPXS40075") == 1, "置顶后读回状态应为已固定");
        CHECK(read_group_item("tbl_group_222", "NPXS40075") == 1,
              "主机 tbl_group_222 里应多出这一行");
        /* 幂等：已经在最前时再点一次，**一次写都不发**（开机对账每次都要跑这条）。 */
        CHECK(app_pin_set("NPXS40075", 1, pmsg, sizeof(pmsg), &pacc, &pidx) == 0 && pacc == 0,
              "已经是第 1 位时再置顶应零写入（accounts=%d）", pacc);
        char *j2 = malloc(65536);
        CHECK(j2 && app_vis_list_json(j2, 65536) > 0, "置顶后重新拉列表应成功");
        const char *q75 = j2 ? strstr(j2, "\"NPXS40075\"") : NULL;
        CHECK(q75 && strstr(q75, "\"pinned\":true") != NULL, "置顶后该条应回 pinned:true");
        CHECK(read_visible("tbl_iconinfo_111", "NPXS40075") == 0 &&
              read_visible("tbl_iconinfo_222", "NPXS40075") == 1,
              "置顶**不动** app.db 的 visible（40075 出厂 111=0 / 222=1）");
        free(j2);

        CHECK(app_pin_set("NPXS40075", 0, pmsg, sizeof(pmsg), NULL, NULL) == 0,
              "取消置顶应成功（%s）", pmsg);
        CHECK(app_pin_get("NPXS40075") == 0, "取消后读回应为未固定");
        CHECK(read_group_item("tbl_group_222", "NPXS40075") == 0,
              "取消置顶后主机里那一行要真的删掉");
        /* 前置白名单：不是内置软件的一条都不许进主屏固定列表。 */
        CHECK(app_pin_set("PPSA00001", 1, pmsg, sizeof(pmsg), NULL, NULL) != 0,
              "游戏编号不许置顶");
        CHECK(app_pin_set("NAYE00001", 1, pmsg, sizeof(pmsg), NULL, NULL) != 0,
              "本面板自己的磁贴不许置顶（恢复入口就在那块磁贴上）");

        /* 开机对账：主机重扫 / 换账号表把固定列表丢了 → 按意图补回来
         * （「重启后被重置」这类问题的统一修法：意图留在盘上，开机重放）。 */
        CHECK(app_pin_set("NPXS40075", 1, pmsg, sizeof(pmsg), NULL, NULL) == 0,
              "再置顶一次（这一回意图文件里记的是 true）");
        {
            sqlite3 *gdb = NULL;
            if (sqlite3_open_v2(DB, &gdb, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK) {
                xexec(gdb, "DELETE FROM tbl_group_222 WHERE itemId='NPXS40075'");
                xexec(gdb, "DELETE FROM tbl_group_333 WHERE itemId='NPXS40075'");
                sqlite3_close(gdb);
            } else {
                CHECK(0, "夹具打不开库做 DELETE");
            }
        }
        CHECK(app_pin_get("NPXS40075") == 0, "模拟主机把固定列表丢了 → 读回应为未固定");
        int pc = 0, pa = 0, pf = 0;
        CHECK(app_pin_sync_all(pmsg, sizeof(pmsg), &pc, &pa, &pf) == 0,
              "置顶对账应成功（%s）", pmsg);
        CHECK(pa >= 1, "置顶对账应把它补回主机（applied=%d 失败=%d）", pa, pf);
        CHECK(pf == 0, "置顶对账不该有失败（failed=%d）", pf);
        CHECK(app_pin_get("NPXS40075") == 1, "补写之后读回应为已固定");
        int pc2 = 0, pa2 = 0, pf2 = 0;
        CHECK(app_pin_sync_all(pmsg, sizeof(pmsg), &pc2, &pa2, &pf2) == 0 && pa2 == 0,
              "一致时置顶对账应零写入（applied=%d）", pa2);
        CHECK(app_pin_set("NPXS40075", 0, pmsg, sizeof(pmsg), NULL, NULL) == 0,
              "收尾：取消置顶（意图也一起回 false）");
    }

    printf("=== 自定义图标 / 背景：列表如实报（面板这一份） ===\n");
    {
        CHECK(app_tile_store_icon("NPXS40047", (const unsigned char *)"\x89PNG\r\n\x1a\nX", 9) == 0,
              "落一份自定义图标");
        CHECK(app_tile_store_bg("NPXS40047", (const unsigned char *)"\0\0\0\0DDS", 7,
                                (const unsigned char *)"\x89PNG\r\n\x1a\nX", 9) == 0,
              "落一份自定义背景");
        char *j3 = list_json();
        const char *q47c = j3 ? strstr(j3, "\"NPXS40047\"") : NULL;
        CHECK(q47c && strstr(q47c, "\"custom_icon\":1") != NULL, "有自定义图标时应回 1");
        CHECK(q47c && strstr(q47c, "\"custom_bg\":1") != NULL, "有自定义背景时应回 1");
        const char *q63c = j3 ? strstr(j3, "\"NPXS40063\"") : NULL;
        CHECK(q63c && strstr(q63c, "\"custom_icon\":0") != NULL, "没设过的应回 0");
        free(j3);
        CHECK(app_tile_drop_icon("NPXS40047") == 0 && app_tile_drop_bg("NPXS40047") == 0,
              "清理（后面还要拉列表，别让自定义状态串场）");
    }
    free(j);

    printf("=== 写入：一次把所有账号表写平 ===\n");
    char msg[192];
    int rc = app_vis_set("NPXS40047", 1, msg, sizeof(msg));
    CHECK(rc == 0, "隐藏 NPXS40047 应成功（rc=%d msg=%s）", rc, msg);
    CHECK(read_visible("tbl_iconinfo_111", "NPXS40047") == 0, "账号 111 应写成 visible=0");
    CHECK(read_visible("tbl_iconinfo_222", "NPXS40047") == 0, "账号 222 应写成 visible=0");

    rc = app_vis_set("NPXS40075", 0, msg, sizeof(msg));
    CHECK(rc == 0, "显示 NPXS40075 应成功（rc=%d msg=%s）", rc, msg);
    CHECK(read_visible("tbl_iconinfo_111", "NPXS40075") == 1, "账号 111 应写成 visible=1");
    CHECK(read_visible("tbl_iconinfo_222", "NPXS40075") == 1, "账号 222 应写成 visible=1");

    /* `.161` 撤出固定名单的那两条必须真的写得动 —— 否则又是面板在替用户决定改不了。 */
    rc = app_vis_set("NPXS40056", 1, msg, sizeof(msg));
    CHECK(rc == 0, "隐藏「所有应用程序」应成功（rc=%d msg=%s）", rc, msg);
    CHECK(read_visible("tbl_iconinfo_111", "NPXS40056") == 0, "NPXS40056 账号 111 应写成 visible=0");
    CHECK(read_visible("tbl_iconinfo_222", "NPXS40056") == 0, "NPXS40056 账号 222 应写成 visible=0");
    rc = app_vis_set("NPXS40063", 0, msg, sizeof(msg));
    CHECK(rc == 0, "显示「欢迎」应成功（rc=%d msg=%s）", rc, msg);
    CHECK(read_visible("tbl_iconinfo_111", "NPXS40063") == 1, "NPXS40063 账号 111 应写成 visible=1");
    CHECK(read_visible("tbl_iconinfo_222", "NPXS40063") == 1, "NPXS40063 账号 222 应写成 visible=1");

    j = list_json();
    CHECK(strstr(j, "\"NPXS40047\"") != NULL && strstr(strstr(j, "\"NPXS40047\""), "\"hidden\":true") != NULL,
          "改完再读一次列表应反映新状态");
    free(j);

    printf("=== 开机对账：主机把行改回来后，重放能对齐（「关机开机后被重置」的修法）===\n");
    {
        /* 现状：NPXS40056 刚被我们隐藏（两账号都 0）。模拟主机重扫 / 新账号表
         * 把它翻回 visible=1 —— 真机上用户看到的就是「藏好的，重启又回来了」。 */
        sqlite3 *db = NULL;
        if (sqlite3_open_v2(DB, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
            CHECK(0, "夹具打不开库做 UPDATE");
        } else {
            xexec(db, "UPDATE tbl_iconinfo_111 SET visible=1 WHERE titleId='NPXS40056'");
            xexec(db, "UPDATE tbl_iconinfo_222 SET visible=1 WHERE titleId='NPXS40056'");
            sqlite3_close(db);
        }
        char *jb = list_json();
        const char *q56 = strstr(jb, "\"NPXS40056\"");
        CHECK(q56 && strstr(q56, "\"hidden\":false") != NULL,
              "被主机改回来之后列表应显示 hidden:false（这就是用户报的现象）");
        free(jb);

        char smsg[192];
        int sc = 0, sa = 0, sf = 0;
        CHECK(app_vis_sync_all(smsg, sizeof(smsg), &sc, &sa, &sf) == 0, "对账应成功（%s）", smsg);
        CHECK(sc >= 1, "至少检查了一条意图（checked=%d）", sc);
        CHECK(sa >= 1, "至少补写一条（applied=%d）", sa);
        CHECK(sf == 0, "不该有失败（failed=%d）", sf);
        CHECK(read_visible("tbl_iconinfo_111", "NPXS40056") == 0 &&
              read_visible("tbl_iconinfo_222", "NPXS40056") == 0,
              "对账应把用户当初的「隐藏」写回去（两个账号都要）");

        /* 一致时**一次写都不发**：这条链每次开机都跑，幂等是它成立的前提。 */
        int sc2 = 0, sa2 = 0, sf2 = 0;
        CHECK(app_vis_sync_all(smsg, sizeof(smsg), &sc2, &sa2, &sf2) == 0 && sa2 == 0,
              "第二次对账应零写入（applied=%d）", sa2);
        CHECK(read_visible("tbl_iconinfo_111", "NPXS40071") == 1,
              "固定条目从不记意图，对账不该碰它");
    }

    printf("=== 越界：不该动的表一行都不动 ===\n");
    {
        sqlite3 *db = NULL;
        sqlite3_open_v2(DB, &db, SQLITE_OPEN_READONLY, NULL);
        sqlite3_stmt *st = NULL;
        int concept7 = 0, group7 = 0, n333 = 0;
        sqlite3_prepare_v2(db, "SELECT visible FROM tbl_concepticoninfo_111 WHERE localConceptId='NPXS40047'", -1, &st, NULL);
        if (sqlite3_step(st) == SQLITE_ROW) concept7 = sqlite3_column_int(st, 0);
        sqlite3_finalize(st); st = NULL;
        sqlite3_prepare_v2(db, "SELECT visible FROM tbl_group_111 WHERE itemId='NPXS40047'", -1, &st, NULL);
        if (sqlite3_step(st) == SQLITE_ROW) group7 = sqlite3_column_int(st, 0);
        sqlite3_finalize(st); st = NULL;
        sqlite3_prepare_v2(db, "SELECT count(*) FROM tbl_iconinfo_333", -1, &st, NULL);
        if (sqlite3_step(st) == SQLITE_ROW) n333 = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        sqlite3_close(db);
        CHECK(concept7 == 7, "tbl_concepticoninfo_111 不该被写（还是 7？实际 %d）", concept7);
        CHECK(group7 == 7, "tbl_group_111 不该被写（还是 7？实际 %d）", group7);
        CHECK(n333 == 0, "没有 visible 列的 tbl_iconinfo_333 不该被建行（行数=%d）", n333);
    }

    printf("=== 拒绝：非内置软件 / 没注册的编号 / 注入 ===\n");
    rc = app_vis_set("PPSA00001", 1, msg, sizeof(msg));
    CHECK(rc == -1, "游戏一律拒写（rc=%d）", rc);
    CHECK(read_visible("tbl_iconinfo_111", "PPSA00001") == 1, "拒写后游戏那行不该被动过");
    rc = app_vis_set("NAYE00001", 1, msg, sizeof(msg));
    CHECK(rc == -1, "自制软件（本面板自己的磁贴）一律拒写（rc=%d）", rc);
    rc = app_vis_set("NPXS99999", 1, msg, sizeof(msg));
    CHECK(rc == -1, "没注册在 contentinfo 里的编号应拒写而不是静默成功（rc=%d msg=%s）", rc, msg);
    CHECK(strstr(msg, "没有") != NULL, "应给一句人话：%s", msg);
    rc = app_vis_set("NPXS40047'; DROP TABLE tbl_contentinfo; --", 1, msg, sizeof(msg));
    CHECK(rc == -1, "带引号分号的 tid 应被字符白名单挡掉（rc=%d）", rc);
    {
        sqlite3 *db = NULL;
        sqlite3_open_v2(DB, &db, SQLITE_OPEN_READONLY, NULL);
        sqlite3_stmt *st = NULL;
        int alive = 0;
        sqlite3_prepare_v2(db, "SELECT count(*) FROM tbl_contentinfo", -1, &st, NULL);
        if (sqlite3_step(st) == SQLITE_ROW) alive = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        sqlite3_close(db);
        CHECK(alive == 8, "注入之后 tbl_contentinfo 还在（%d 行）", alive);
    }

    printf("=== 固定条目：列表里说明得清楚，写入一律拒且一行不写 ===\n");
    rc = app_vis_set("NPXS40071", 1, msg, sizeof(msg));
    CHECK(rc == APP_VIS_EFIXED, "固定条目应回 EFIXED（-3）而不是成功（rc=%d msg=%s）", rc, msg);
    CHECK(msg[0] != 0, "拒写要带一句原因：%s", msg);
    CHECK(read_visible("tbl_iconinfo_111", "NPXS40071") == 1, "账号 111 的固定条目不该被写");
    CHECK(read_visible("tbl_iconinfo_222", "NPXS40071") == 1, "账号 222 的固定条目不该被写");

    printf("=== 分清「读不了」与「没装」：在册查询失败要说忙，不能说没装 ===\n");
    {
        sqlite3 *db = NULL;
        if (sqlite3_open_v2(DB, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
            CHECK(0, "夹具打不开库做 DROP");
        } else {
            xexec(db, "DROP TABLE tbl_contentinfo");
            sqlite3_close(db);
        }
        rc = app_vis_set("NPXS40047", 1, msg, sizeof(msg));
        CHECK(rc == -2, "contentinfo 读不了应回忙/占用（-2），不是「没装」（-1）（rc=%d msg=%s）", rc, msg);
        CHECK(strstr(msg, "读不了") != NULL || strstr(msg, "正忙") != NULL,
              "要说什么读不了/正忙，别让人去查为什么没装：%s", msg);
        CHECK(strstr(msg, "没有这个内置软件") == NULL, "同样的失败不该报成「主机上没有这个内置软件」：%s", msg);
        j = list_json();
        CHECK(strstr(j, "\"apps\":[]") != NULL, "清单读不动时列表应是空数组");
        CHECK(strstr(j, "\"error\":\"\"") == NULL, "清单读不动时 error 必须非空，别冒充「主机上没有可管理的软件」：%s", j);
        free(j);
    }

    printf("=== 读不到库：error 要说话，不能拿空数组冒充成功 ===\n");
    unlink(DB);
    j = list_json();
    CHECK(strstr(j, "\"apps\":[]") != NULL, "库没了应回空数组");
    CHECK(strstr(j, "\"error\":\"\"") == NULL, "库没了 error 必须非空：%s", j);
    rc = app_vis_set("NPXS40047", 1, msg, sizeof(msg));
    CHECK(rc == -1, "库打不开时写入应失败（rc=%d）", rc);

    printf("\n%s  (%d 项失败)\n", fails ? "!!! 有失败项" : "全部通过", fails);
    return fails ? 1 : 0;
}
