/* [CN-FORK] v0.5.2.170 tile_db 改名写链的宿主测试。
 *
 * 为什么补这一条：仓库此前对 tile_db 两条写链**一个宿主测试都没有**
 * （见 test/app_visibility_test.c 开头那句），全靠真机消融。于是 `.170` 这个
 * 真 bug 才活到今天 ——
 *
 *   tbl_appinfo / tbl_conceptinfo 的名字 key 是**带语言后缀**的一整套：
 *       TITLE, TITLE_00, TITLE_01, TITLE_09, TITLE_10, TITLE_11, TITLE_27 ...
 *       concept_name, concept_name_00 ... concept_name_30
 *   早年只写了 TITLE / TITLE_01，**简中系统读的是 TITLE_11** —— 主屏/SMP 照旧
 *   显示旧名。用户原话：「P端改完名称，PC端变了，但 smb 没变」。
 *
 * 夹具用 sqlite3 现造（与 app_visibility_test.c 同一手法，不用 python），
 * 形状照真机 192.168.1.214 的 appinfo.db 列裁剪：tbl_appinfo(titleId,metaDataId,key,val)
 * + tbl_conceptinfo(local_concept_id,metadata_id,key,val)，另加一个 TITLE_ID 作负向对照。
 *
 * 钉死四件事：
 *  ① 所有 TITLE_NN + concept_name_NN 都被写成新名（简中槽 TITLE_11 是重点）；
 *  ② **TITLE_ID 一个字节都不许动**（它是标题号，写坏了比不改名严重）；
 *  ③ 幂等：第二次调用「本来就一致」不报错、返回 0；
 *  ④ 别人的行 / 别的 key（AGE_LEVEL 等）一个字不许改。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "tile_db.h"
#include "sqlite3.h"

/* pldmgr_log 的桩（log_server.c 不进来），与 game_mgr_test.c 同一手法。 */
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

#define ROOT "/tmp/pldmgr_tiledb_test"

static void xexec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        printf("  夹具 SQL 失败: %s\n    %s\n", sql, err ? err : "");
        if (err) sqlite3_free(err);
        exit(1);
    }
}

/* 造一份最小但形状正确的 app.db + appinfo.db。 */
static void build_fixtures(void) {
    /* ── app.db：名字列那张表（改名链也会写它）+ 一个概念表 ── */
    {
        char p[256];
        snprintf(p, sizeof(p), "%s/app.db", ROOT);
        sqlite3 *db = NULL;
        if (sqlite3_open(p, &db) != SQLITE_OK) { printf("开 app.db 失败\n"); exit(1); }
        xexec(db, "DROP TABLE IF EXISTS tbl_contentinfo");
        xexec(db, "CREATE TABLE tbl_contentinfo(titleId TEXT, titleName TEXT, "
                  "localConceptId TEXT, icon0Info TEXT)");
        xexec(db, "INSERT INTO tbl_contentinfo VALUES"
                  "('PPSA21567','宇宙机器人 88','cid:scp:000000000098a0fc','x')");
        xexec(db, "INSERT INTO tbl_contentinfo VALUES"
                  "('PPSA99999','别人的游戏','cid:scp:other','y')");
        xexec(db, "DROP TABLE IF EXISTS tbl_conceptmetadata");
        xexec(db, "CREATE TABLE tbl_conceptmetadata(localConceptId TEXT, conceptName TEXT)");
        xexec(db, "INSERT INTO tbl_conceptmetadata VALUES"
                  "('cid:scp:000000000098a0fc','宇宙机器人 88')");
        sqlite3_close(db);
    }
    /* ── appinfo.db：key/value 两层 + TITLE_ID 负向对照 ── */
    {
        char p[256];
        snprintf(p, sizeof(p), "%s/appinfo.db", ROOT);
        sqlite3 *db = NULL;
        if (sqlite3_open(p, &db) != SQLITE_OK) { printf("开 appinfo.db 失败\n"); exit(1); }
        xexec(db, "DROP TABLE IF EXISTS tbl_appinfo");
        xexec(db, "CREATE TABLE tbl_appinfo(titleId TEXT, metaDataId TEXT, "
                  "key TEXT, val TEXT)");
        /* 真机形状：TITLE + 00/01/09/10/11/27 语言槽（值都是英文默认名） */
        const char *tid = "PPSA21567";
        const char *keys[] = {"TITLE","TITLE_00","TITLE_01","TITLE_09",
                              "TITLE_10","TITLE_11","TITLE_27"};
        char sql[512];
        for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
            snprintf(sql, sizeof(sql),
                     "INSERT INTO tbl_appinfo VALUES('%s','m','%s','宇宙机器人 88')",
                     tid, keys[i]);
            xexec(db, sql);
        }
        /* ★ 负向对照：TITLE_ID 是标题号，绝不能被改名链碰到 */
        xexec(db, "INSERT INTO tbl_appinfo VALUES"
                  "('PPSA21567','m','TITLE_ID','PPSA21567')");
        /* 无关 key 也不能被碰 */
        xexec(db, "INSERT INTO tbl_appinfo VALUES"
                  "('PPSA21567','m','AGE_LEVEL','{\"CN\":0,\"default\":10}')");
        /* 别人的行 */
        xexec(db, "INSERT INTO tbl_appinfo VALUES"
                  "('PPSA99999','m','TITLE','别人的游戏')");
        xexec(db, "INSERT INTO tbl_appinfo VALUES"
                  "('PPSA99999','m','TITLE_11','别人的游戏简中')");

        xexec(db, "DROP TABLE IF EXISTS tbl_conceptinfo");
        xexec(db, "CREATE TABLE tbl_conceptinfo(local_concept_id TEXT, "
                  "metadata_id TEXT, key TEXT, val TEXT)");
        const char *ckey0[] = {"concept_name","concept_name_00","concept_name_01",
                               "concept_name_09","concept_name_10",
                               "concept_name_11","concept_name_27"};
        for (size_t i = 0; i < sizeof(ckey0)/sizeof(ckey0[0]); i++) {
            snprintf(sql, sizeof(sql),
                     "INSERT INTO tbl_conceptinfo VALUES"
                     "('cid:scp:000000000098a0fc','m','%s','宇宙机器人 88')", ckey0[i]);
            xexec(db, sql);
        }
        xexec(db, "INSERT INTO tbl_conceptinfo VALUES"
                  "('cid:scp:000000000098a0fc','m','concept_id','10002684')");
        sqlite3_close(db);
    }
}

/* 读一个 key 的值（找不到返回 NULL 走调用方判）。 */
static int q1(sqlite3 *db, const char *sql, char *out, size_t osz) {
    sqlite3_stmt *st = NULL;
    out[0] = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    int rc = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *v = (const char *)sqlite3_column_text(st, 0);
        if (v) { snprintf(out, osz, "%s", v); rc = 1; }
    }
    sqlite3_finalize(st);
    return rc;
}

int main(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", ROOT, ROOT);
    if (system(cmd) != 0) { printf("建临时目录失败\n"); return 1; }

    build_fixtures();

    char p[256];
    snprintf(p, sizeof(p), "%s/appinfo.db", ROOT);
    sqlite3 *db = NULL;
    if (sqlite3_open(p, &db) != SQLITE_OK) { printf("开 appinfo.db 失败\n"); return 1; }

    const char *NEW = "宇宙机器人1";

    /* ── ① 写：所有语言槽都该变 ── */
    printf("=== ① tile_db_set_name 写 appinfo.db ===\n");
    {
        char msg[256] = {0};
        int rc = tile_db_set_name("PPSA21567", NEW, msg, sizeof(msg));
        CHECK(rc == 0, "tile_db_set_name 返回 0（实得 %d）msg=%s", rc, msg);
    }
    const char *langs[] = {"TITLE","TITLE_00","TITLE_01","TITLE_09",
                           "TITLE_10","TITLE_11","TITLE_27"};
    char sql[512], got[256];
    for (size_t i = 0; i < sizeof(langs)/sizeof(langs[0]); i++) {
        snprintf(sql, sizeof(sql),
                 "SELECT val FROM tbl_appinfo WHERE titleId='PPSA21567' AND key='%s'",
                 langs[i]);
        q1(db, sql, got, sizeof(got));
        CHECK(strcmp(got, NEW) == 0, "tbl_appinfo.%s = %s（期望 %s）", langs[i], got, NEW);
    }
    /* 简中槽单独再点名一次 —— 用户现场就是撞的这个 */
    q1(db, "SELECT val FROM tbl_appinfo WHERE titleId='PPSA21567' AND key='TITLE_11'",
       got, sizeof(got));
    CHECK(strcmp(got, NEW) == 0, "★ 简中槽 TITLE_11 被改到（%s）—— 这是本 bug 的核心", got);

    /* ── ② 负向：TITLE_ID 绝不能动 ── */
    printf("=== ② TITLE_ID / 无关 key / 别人的行 一个字都不许动 ===\n");
    q1(db, "SELECT val FROM tbl_appinfo WHERE titleId='PPSA21567' AND key='TITLE_ID'",
       got, sizeof(got));
    CHECK(strcmp(got, "PPSA21567") == 0, "★ TITLE_ID 未被误伤（= %s，期望 PPSA21567）", got);
    q1(db, "SELECT val FROM tbl_appinfo WHERE titleId='PPSA21567' AND key='AGE_LEVEL'",
       got, sizeof(got));
    CHECK(strncmp(got, "{\"CN\":0", 7) == 0, "无关 key AGE_LEVEL 未被改（= %.20s）", got);
    q1(db, "SELECT val FROM tbl_appinfo WHERE titleId='PPSA99999' AND key='TITLE'",
       got, sizeof(got));
    CHECK(strcmp(got, "别人的游戏") == 0, "别人的行未被改（= %s）", got);
    q1(db, "SELECT val FROM tbl_appinfo WHERE titleId='PPSA99999' AND key='TITLE_11'",
       got, sizeof(got));
    CHECK(strcmp(got, "别人的游戏简中") == 0, "别人行的语言槽未被改（= %s）", got);

    /* ── concept 侧 ── */
    printf("=== ③ tbl_conceptinfo 的语言槽 ===\n");
    const char *cks[] = {"concept_name","concept_name_00","concept_name_01",
                         "concept_name_09","concept_name_10",
                         "concept_name_11","concept_name_27"};
    for (size_t i = 0; i < sizeof(cks)/sizeof(cks[0]); i++) {
        snprintf(sql, sizeof(sql),
                 "SELECT val FROM tbl_conceptinfo WHERE local_concept_id="
                 "'cid:scp:000000000098a0fc' AND key='%s'", cks[i]);
        q1(db, sql, got, sizeof(got));
        CHECK(strcmp(got, NEW) == 0, "tbl_conceptinfo.%s = %s", cks[i], got);
    }
    q1(db, "SELECT val FROM tbl_conceptinfo WHERE local_concept_id="
           "'cid:scp:000000000098a0fc' AND key='concept_id'", got, sizeof(got));
    CHECK(strcmp(got, "10002684") == 0, "concept_id 未被改（= %s）", got);

    /* ── ④ 幂等 ── */
    printf("=== ④ 幂等：再调一次不报错、不再写 ===\n");
    {
        char msg[256] = {0};
        int rc = tile_db_set_name("PPSA21567", NEW, msg, sizeof(msg));
        CHECK(rc == 0, "第二次 tile_db_set_name 仍返回 0（实得 %d）", rc);
    }

    sqlite3_close(db);

    /* ── ⑤ app.db 侧：titleName / conceptName 也要写 ── */
    printf("=== ⑤ app.db 的名字列 ===\n");
    snprintf(p, sizeof(p), "%s/app.db", ROOT);
    if (sqlite3_open(p, &db) != SQLITE_OK) { printf("开 app.db 失败\n"); return 1; }
    q1(db, "SELECT titleName FROM tbl_contentinfo WHERE titleId='PPSA21567'", got, sizeof(got));
    CHECK(strcmp(got, NEW) == 0, "app.db tbl_contentinfo.titleName = %s", got);
    q1(db, "SELECT titleName FROM tbl_contentinfo WHERE titleId='PPSA99999'", got, sizeof(got));
    CHECK(strcmp(got, "别人的游戏") == 0, "app.db 别人的行未被改（= %s）", got);
    q1(db, "SELECT conceptName FROM tbl_conceptmetadata WHERE localConceptId="
           "'cid:scp:000000000098a0fc'", got, sizeof(got));
    CHECK(strcmp(got, NEW) == 0, "app.db tbl_conceptmetadata.conceptName = %s", got);
    sqlite3_close(db);

    /* ── ⑥ 未注册的游戏：一行都没匹配 → -1 ── */
    printf("=== ⑥ 未注册的游戏返回 -1 ===\n");
    {
        char msg[256] = {0};
        int rc = tile_db_set_name("PPSA00000", "随便", msg, sizeof(msg));
        CHECK(rc == -1, "未注册 tid 返回 -1（实得 %d）msg=%s", rc, msg);
    }

    snprintf(cmd, sizeof(cmd), "rm -rf %s", ROOT);
    if (system(cmd) != 0) { /* 清理失败不致命 */ }

    printf("\n%s (%d failures)\n", fails ? "RESULT: FAIL" : "RESULT: PASS", fails);
    return fails ? 1 : 0;
}
