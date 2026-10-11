/* [CN-FORK] v0.5.2.118 磁贴写回主机 —— 改名与换背景的两条 SQLite UPDATE 链。
 *
 * 读侧（game_mgr.c 那份只读 SQLite 解析器）一个字节都没动；**写侧必须真跑 SQL**：
 * 页内改一个 TEXT 的长度要搬记录、超页还要分裂，手写等于重实现一遍 SQLite，
 * 所以这里静态链接合并版（固件不带 libSceSqlite.sprx、SDK 也没有 stub，
 * sqlite3_* 动态链接不了；见 Makefile 里 SQLITE_CFLAGS 那段）。
 *
 * 两段逻辑逐条照抄 D:\y2jb-new\{rename-payload,tilebg-payload} —— 两个都在真机上验证过，
 * 这里保留它们的原注释，因为每一条都是「试错过一次才写下来」的：
 *
 * ① 磁贴**名字不是文件**。它在两个库里：
 *      /system_data/priv/mms/app.db
 *        每张带 titleName / conceptName / primaryTitleName 列的表
 *        SET 该列 = <新名>  WHERE titleId = <tid>
 *                          或 localConceptId = <cid>（概念表；<cid> 是从
 *        tbl_contentinfo 读回来的 "cid:scp:..."，推不出来，只能读）
 *      /system_data/priv/mms/appinfo.db
 *        tbl_appinfo     SET val=<新名> WHERE titleId=<tid>
 *                                        AND (key='TITLE' OR key GLOB 'TITLE_[0-9]*')
 *        tbl_conceptinfo SET val=<新名> WHERE local_concept_id=<cid>
 *                                        AND (key='concept_name' OR
 *                                             key GLOB 'concept_name_[0-9]*')
 *    ⚠ [CN-FORK] v0.5.2.170 早年只写 TITLE/TITLE_01（照抄参考实现），**漏掉了
 *    TITLE_00..30 这一整套语言槽** —— 简中系统读 TITLE_11，于是「改完名主屏还是
 *    旧名」。TITLE_NN / concept_name_NN 是**语言维度**，不是可选别名，必须全写。
 *    匹配式刻意不用 `LIKE 'TITLE_%'`：tbl_appinfo 里还有个 **TITLE_ID** key
 *    （值是标题号），`TITLE_%` 会把它改成游戏名 —— 用 GLOB '[0-9]' 排掉它。
 *    **真机消融只写这 9 个平铺格子，AppInfoJson / ConceptInfoJson 镜像一字节不碰**，
 *    磁贴照样改了 —— 反过来原地改 JSON 会写坏：ConceptInfoJson 里带按字段的 byte
 *    "size"，字符串长度一变就与实际不符。per-account 的 tbl_iconinfo_<uid> /
 *    tbl_concepticoninfo_<uid> 靠**列**发现，绝不硬编码表名。
 *    写完 Shell 要等**重启 / 待机唤醒**才重绘（与自定义图标一致）。
 *
 * ② 磁贴**背景只看 app.db 一个列**：
 *      每张带 pic0Info 列的表
 *        SET pic0Info = "<pic0.dds 的实际路径>?ts=<mtime>" WHERE titleId=<tid>
 *    三个各自坑过一次的点：文件必须叫 **pic0.dds**（pic0.png / background.png 根本不被读，
 *    ".dds" 是烤进 Shell 查找路径里的）；ts 是**秒**不是毫秒；手工装的 app 光写文件、
 *    甚至重启都不会自己补上这一列，必须自己写。写完 Shell **实时**重绘，不重启。
 *
 * 两条链都 best-effort：库打不开、一行没匹配、写失败 —— 一律记日志、返回负数，
 * 绝不把整个请求打死。面板那一层（name.txt、我们自己那份 icon.png / bg.png）已经生效，
 * 主机端没生效只该变成一句可查的日志，不该变成一次 500。 */

#include "tile_db.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "json_helpers.h"  /* ensure_dir_recursive */
#include "pldmgr.h"        /* pldmgr_log */
#include "sqlite3.h"

/* ── 首次写之前的原始备份 ──────────────────────────────────────────────────
 * 这两个库是**主机的**，改坏了游戏可能起不来。事务（BEGIN IMMEDIATE/COMMIT/ROLLBACK）
 * 保证的是「一次改要么全成要么全不成」，保证不了「我这版逻辑本身没写错」——
 * 所以第一次动手前先留一份干净底片。
 *
 * 只备**一次**，且已有就绝不覆盖：覆盖掉的会变成「上一次被我改过的版本」，
 * 真出事时反而没有可回滚的原件。备份失败只记日志，照常往下走（best-effort）。 */
#ifndef TILE_DB_BAK_DIR
#define TILE_DB_BAK_DIR "/data/nayeplmtools/dbbak"
#endif

static int td_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    unsigned char buf[8192]; /* 小栈块：上一轮刚把这个仓库里 64KB 栈数组当成过死机嫌疑 */
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    if (rc != 0) remove(dst); /* 半截备份比没有备份更糟 */
    return rc;
}

static void td_backup_once(const char *src, const char *leaf) {
    char bak[768];
    struct stat sb;
    if (stat(src, &sb) != 0) return; /* 库都还没有，没什么可备的 */
    snprintf(bak, sizeof(bak), "%s/%s.bak", TILE_DB_BAK_DIR, leaf);
    if (stat(bak, &sb) == 0) return; /* 已经留过原始底片了 */
    if (ensure_dir_recursive(TILE_DB_BAK_DIR) != 0) {
        pldmgr_log("[PLDMGR] tile_db: 备份目录建不起来（%s），跳过 %s 备份\n",
                   strerror(errno), leaf);
        return;
    }
    if (td_copy_file(src, bak) != 0) {
        pldmgr_log("[PLDMGR] tile_db: 备份 %s 失败，继续（%s）\n", leaf,
                   strerror(errno));
        return;
    }
    pldmgr_log("[PLDMGR] tile_db: 已留存原始备份 %s\n", bak);
}

void tile_db_backup_dbs(void) {
    td_backup_once(TILE_DB_APPDB_PATH, "app.db");
    td_backup_once(TILE_DB_APPINFO_PATH, "appinfo.db");
}


/* ── 基础防护 ─────────────────────────────────────────────────────────────── */

/* tid 与 game_title.c 的 tt_tid_ok 同规格（6..16 位 A-Z0-9）。拿不到 '/' 也拿不到 '.',
 * 路径穿越与 SQL 注入都在这里被挡死 —— 下面所有拼字符串的 SQL 都只吃 tid 与
 * sqlite_master 读出来的表名。 */
static int td_tid_ok(const char *tid) {
    if (!tid) return 0;
    size_t n = strlen(tid);
    if (n < 6 || n > 16) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = tid[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

/* 表名要插进 SQL 文本（没法 bind），先挡掉引号/分号。名字是从我们自己机器上的
 * sqlite_master 读回来的，这一层是纵深防御，不是在防外部输入。 */
static int td_safe_ident(const char *s) {
    return s && *s && !strchr(s, '\'') && !strchr(s, '"') && !strchr(s, ';');
}

static int td_has_column(sqlite3 *db, const char *table, const char *col) {
    if (!td_safe_ident(table) || !td_safe_ident(col)) return 0;
    char q[512];
    snprintf(q, sizeof(q),
             "SELECT count(*) FROM pragma_table_info('%s') WHERE name='%s'",
             table, col);
    sqlite3_stmt *st = NULL;
    int n = 0;
    if (sqlite3_prepare_v2(db, q, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0) > 0;
    }
    if (st) sqlite3_finalize(st);
    return n;
}

/* 只跑 BEGIN / COMMIT / ROLLBACK 这种没有结果行的语句。 */
static int td_exec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err) sqlite3_free(err);
    return rc;
}

static sqlite3 *td_open(const char *path) {
    sqlite3 *db = NULL;
    /* READWRITE：必须能改。只读挂载会在这里失败，记一条日志返回，绝不崩。 */
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        pldmgr_log("[PLDMGR] tile_db: 打不开 %s（%s）\n", path,
                   db ? sqlite3_errmsg(db) : "no handle");
        if (db) sqlite3_close(db);
        return NULL;
    }
    /* Shell 自己也常开着这两个库的读句柄 —— 给个超时比第一次撞锁就失败强。 */
    sqlite3_busy_timeout(db, 5000);
    return db;
}

/* 把 "cid:scp:..." 从 app.db 读回来（推不出来，只能读）。失败时 out 为空串。 */
static void td_read_local_cid(sqlite3 *db, const char *tid, char *out, size_t osz) {
    out[0] = 0;
    if (!td_has_column(db, "tbl_contentinfo", "localConceptId")) return;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT localConceptId FROM tbl_contentinfo "
                           "WHERE titleId = ?",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, tid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *v = (const char *)sqlite3_column_text(st, 0);
            if (v) snprintf(out, osz, "%s", v);
        }
    }
    if (st) sqlite3_finalize(st);
}

/* ── 改名：读出来比一比，已经对了就一次写都不发 ───────────────────────────── */

/* count_differ：这一句选中几行、其中几行还不等于 new_name（"IS NOT ?" 让 NULL 也算不同）。
 * 用来把「已经是对的」与「这一行压根不属于这个游戏」分开 —— 前者不需要开写事务，
 * 也就不用跟 Shell 抢锁。 */
static int td_count_differ(sqlite3 *db, const char *sql, const char *new_name,
                           const char *key_value, int *matched_out) {
    sqlite3_stmt *st = NULL;
    int matched = 0, differ = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, new_name, -1, SQLITE_TRANSIENT);
        if (key_value) sqlite3_bind_text(st, 2, key_value, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            matched = sqlite3_column_int(st, 0);
            differ = sqlite3_column_int(st, 1); /* SUM 全 NULL 时读回 0 */
        }
    } else {
        pldmgr_log("[PLDMGR] tile_db: 计数失败（%s）：%s\n", sql,
                   sqlite3_errmsg(db));
    }
    if (st) sqlite3_finalize(st);
    if (matched_out) *matched_out = matched;
    return differ;
}

/* 一张表的一列。优先按 titleId 定位，概念表退回 localConceptId；
 * 两样都没有就**什么都不写** —— 表名换了宁可不更新，也不能改到别人那行。 */
static int td_write_name_cell(sqlite3 *db, const char *table, const char *col,
                              const char *tid, const char *local_cid,
                              const char *new_name, int *matched_out) {
    const char *key = NULL;
    const char *bind = NULL;
    if (td_has_column(db, table, "titleId")) {
        key = "titleId";
        bind = tid;
    } else if (local_cid && local_cid[0] && td_has_column(db, table, "localConceptId")) {
        key = "localConceptId";
        bind = local_cid;
    }
    if (!key) {
        pldmgr_log("[PLDMGR] tile_db: %s 有 %s 列，却没有任何键能定位到 %s\n",
                   table, col, tid);
        if (matched_out) *matched_out = 0;
        return 0;
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT COUNT(*), SUM(CASE WHEN \"%s\" IS NOT ? THEN 1 ELSE 0 END)"
             " FROM \"%s\" WHERE \"%s\" = ?",
             col, table, key);
    int matched = 0;
    int differ = td_count_differ(db, sql, new_name, bind, &matched);
    if (matched_out) *matched_out = matched;
    if (matched == 0) return 0;
    if (differ == 0) return 0; /* 已经是这个名，跳过写事务 */

    snprintf(sql, sizeof(sql), "UPDATE \"%s\" SET \"%s\" = ? WHERE \"%s\" = ?",
             table, col, key);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pldmgr_log("[PLDMGR] tile_db: %s.%s 预编译失败：%s\n", table, col,
                   sqlite3_errmsg(db));
        if (st) sqlite3_finalize(st);
        return -1;
    }
    sqlite3_bind_text(st, 1, new_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, bind, -1, SQLITE_TRANSIENT);

    int written = 0;
    if (sqlite3_step(st) == SQLITE_DONE)
        written = sqlite3_changes(db);
    else
        pldmgr_log("[PLDMGR] tile_db: %s.%s 写失败：%s\n", table, col,
                   sqlite3_errmsg(db));
    sqlite3_finalize(st);
    if (written)
        pldmgr_log("[PLDMGR] tile_db: %s.%s 已更新 %d 行（键 %s）\n", table, col,
                   written, key);
    return written;
}

static const char *const TD_NAME_COLS[] = {"titleName", "conceptName",
                                           "primaryTitleName"};
#define TD_NAME_COLS_N ((int)(sizeof(TD_NAME_COLS) / sizeof(TD_NAME_COLS[0])))

/* app.db：扫 sqlite_master，每张带名字列的表各写一格，整趟一个事务。
 * 返回写入行数（>=0），或 -1 = 尝试写了但失败。*matched_out = 属于这个游戏的名字格数。 */
static int td_update_appdb(const char *tid, const char *new_name, int *matched_out) {
    sqlite3 *db = td_open(TILE_DB_APPDB_PATH);
    if (!db) return -1;

    if (td_exec(db, "BEGIN IMMEDIATE") != SQLITE_OK) {
        pldmgr_log("[PLDMGR] tile_db: app.db BEGIN 失败：%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    char local_cid[160];
    td_read_local_cid(db, tid, local_cid, sizeof(local_cid));

    int written = 0, failed = 0, matched = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table'",
                           -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *table = (const char *)sqlite3_column_text(st, 0);
            if (!table || !td_safe_ident(table)) continue;
            for (int c = 0; c < TD_NAME_COLS_N; c++) {
                if (!td_has_column(db, table, TD_NAME_COLS[c])) continue;
                int m = 0;
                int r = td_write_name_cell(db, table, TD_NAME_COLS[c], tid,
                                           local_cid, new_name, &m);
                matched += m;
                if (r < 0) failed = 1;
                else written += r;
            }
        }
    } else {
        pldmgr_log("[PLDMGR] tile_db: app.db 表结构扫描失败：%s\n",
                   sqlite3_errmsg(db));
        failed = 1;
    }
    if (st) sqlite3_finalize(st);

    if (matched_out) *matched_out = matched;

    if (written > 0 && !failed) {
        if (td_exec(db, "COMMIT") != SQLITE_OK) {
            pldmgr_log("[PLDMGR] tile_db: app.db COMMIT 失败：%s\n",
                       sqlite3_errmsg(db));
            td_exec(db, "ROLLBACK");
            sqlite3_close(db);
            return -1;
        }
    } else {
        /* 没东西要落：要么本来就对，要么这游戏还没注册。回滚立刻把锁放掉。 */
        td_exec(db, "ROLLBACK");
        if (failed) written = -1;
    }
    sqlite3_close(db);
    return written;
}

/* appinfo.db 的一条带键 UPDATE。先问一句，已经对了就一次写都不发。 */
static int td_update_appinfo_kv(sqlite3 *db, const char *sql_str,
                                const char *count_sql, const char *new_name,
                                const char *key_value, int *matched_out) {
    int matched = 0;
    int differ = td_count_differ(db, count_sql, new_name, key_value, &matched);
    if (matched_out) *matched_out = matched;
    if (matched == 0) return 0;
    if (differ == 0) return 0;

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql_str, -1, &st, NULL) != SQLITE_OK) {
        pldmgr_log("[PLDMGR] tile_db: appinfo 预编译失败（%s）：%s\n", sql_str,
                   sqlite3_errmsg(db));
        if (st) sqlite3_finalize(st);
        return -1;
    }
    sqlite3_bind_text(st, 1, new_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, key_value, -1, SQLITE_TRANSIENT);
    int written = 0;
    if (sqlite3_step(st) == SQLITE_DONE)
        written = sqlite3_changes(db);
    else
        pldmgr_log("[PLDMGR] tile_db: appinfo 写失败：%s\n", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return written;
}

/* [CN-FORK] v0.5.2.170 名字 key 的语言维度 —— 只改 TITLE/TITLE_01 是个真 bug。
 *
 * 真机取证（192.168.1.214，PPSA21567「宇宙机器人」）：tbl_appinfo 里同一个
 * titleId 下**并存**下面这一整套 key（值全是英文默认名，改名前）：
 *     TITLE, TITLE_00, TITLE_01, TITLE_02 ... TITLE_30, (另有一个 TITLE_ID)
 * 我们以前只写 TITLE + TITLE_01 —— 于是**简中系统读 TITLE_11、繁中读 TITLE_10、
 * 韩文读 TITLE_09、泰文读 TITLE_27**，这些槽位一个都没动，主屏/SMP 照旧显示旧名。
 * 用户看到的现象就是「改名只有一个位置变了 / SMP 里还是旧名」。
 * tbl_conceptinfo 的 concept_name / concept_name_00..30 是同一个病，一并修。
 *
 * 匹配式用两个条件而不是 `LIKE 'TITLE_%'`：
 *   · `key = 'TITLE'`            —— 不带数字的默认槽，必须留；
 *   · `key GLOB 'TITLE_[0-9]*'`  —— 只要**数字后缀**的那些。
 * ⚠ 绝不能用 `LIKE 'TITLE_%'`：tbl_appinfo 里还有一个 **TITLE_ID** key（值是
 *   "PPSA21567" 这种标题号），`TITLE_%` 会把它一起改成游戏名 —— 那是把身份
 *   字段写坏，比不改名严重得多。GLOB 的 `[0-9]` 正好把 TITLE_ID 排除在外
 *   （它的第五个字符是 'I' 不是数字）。
 * concept 那边同形（concept_id 是纯数字槽值，但 key 名没有 concept_id 这种兄弟，
 * 仍统一用同一套匹配式，少一个特例少一个坑）。 */
#define TD_TITLE_KEY_MATCH    "key = 'TITLE' OR key GLOB 'TITLE_[0-9]*'"
#define TD_CONCEPT_KEY_MATCH  "key = 'concept_name' OR key GLOB 'concept_name_[0-9]*'"

/* appinfo.db：TITLE / TITLE_00..30（按 titleId）+ concept_name(_00..30)（按 cid）。 */
static int td_update_appinfo(const char *tid, const char *local_cid,
                             const char *new_name, int *matched_out) {
    sqlite3 *db = td_open(TILE_DB_APPINFO_PATH);
    if (!db) return -1;

    if (td_exec(db, "BEGIN IMMEDIATE") != SQLITE_OK) {
        pldmgr_log("[PLDMGR] tile_db: appinfo.db BEGIN 失败：%s\n",
                   sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    int written = 0, failed = 0, matched = 0, m = 0;
    written += td_update_appinfo_kv(
        db,
        "UPDATE tbl_appinfo SET val = ? WHERE titleId = ? "
        "AND (" TD_TITLE_KEY_MATCH ")",
        "SELECT COUNT(*), SUM(CASE WHEN val IS NOT ? THEN 1 ELSE 0 END) "
        "FROM tbl_appinfo WHERE titleId = ? AND (" TD_TITLE_KEY_MATCH ")",
        new_name, tid, &m);
    matched += m;
    if (local_cid && local_cid[0]) {
        m = 0;
        written += td_update_appinfo_kv(
            db,
            "UPDATE tbl_conceptinfo SET val = ? WHERE local_concept_id = ? "
            "AND (" TD_CONCEPT_KEY_MATCH ")",
            "SELECT COUNT(*), SUM(CASE WHEN val IS NOT ? THEN 1 ELSE 0 END) "
            "FROM tbl_conceptinfo WHERE local_concept_id = ? "
            "AND (" TD_CONCEPT_KEY_MATCH ")",
            new_name, local_cid, &m);
        matched += m;
    }
    if (written < 0) failed = 1;

    if (matched_out) *matched_out = matched;

    if (written > 0 && !failed) {
        if (td_exec(db, "COMMIT") != SQLITE_OK) {
            pldmgr_log("[PLDMGR] tile_db: appinfo.db COMMIT 失败：%s\n",
                       sqlite3_errmsg(db));
            td_exec(db, "ROLLBACK");
            sqlite3_close(db);
            return -1;
        }
    } else {
        td_exec(db, "ROLLBACK");
        if (failed) written = -1;
    }
    sqlite3_close(db);
    return written;
}

/* ── 背景：一个列，每张带它的表各写一行 ──────────────────────────────────── */

static int td_write_bg_cell(sqlite3 *db, const char *table, const char *tid,
                            const char *local_cid, const char *value) {
    const char *key = NULL;
    const char *bind = NULL;
    if (td_has_column(db, table, "titleId")) {
        key = "titleId";
        bind = tid;
    } else if (local_cid && local_cid[0] && td_has_column(db, table, "localConceptId")) {
        key = "localConceptId";
        bind = local_cid;
    }
    if (!key) return 0; /* 没有键就一条都不写：宁可不更新也不能改到别人的行 */

    char sql[512];
    snprintf(sql, sizeof(sql), "UPDATE \"%s\" SET \"%s\" = ? WHERE \"%s\" = ?",
             table, TILE_DB_BG_COLUMN, key);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pldmgr_log("[PLDMGR] tile_db: %s 预编译失败：%s\n", table,
                   sqlite3_errmsg(db));
        if (st) sqlite3_finalize(st);
        return 0;
    }
    sqlite3_bind_text(st, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, bind, -1, SQLITE_TRANSIENT);

    int written = 0;
    if (sqlite3_step(st) == SQLITE_DONE)
        written = sqlite3_changes(db);
    else
        pldmgr_log("[PLDMGR] tile_db: %s 写失败：%s\n", table, sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return written;
}

/* ── 对外两个入口 ────────────────────────────────────────────────────────── */

int tile_db_set_name(const char *tid, const char *new_name,
                     char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!td_tid_ok(tid) || !new_name || !new_name[0]) {
        if (msg && msgsz) snprintf(msg, msgsz, "参数不合法");
        return -1;
    }
    tile_db_backup_dbs();

    /* cid 只能读不能推，先单独开一次库读出来，两个库共用。 */
    char local_cid[160];
    local_cid[0] = 0;
    {
        sqlite3 *adb = td_open(TILE_DB_APPDB_PATH);
        if (adb) {
            td_read_local_cid(adb, tid, local_cid, sizeof(local_cid));
            sqlite3_close(adb);
        }
    }

    int ma = 0, mi = 0;
    int n = td_update_appdb(tid, new_name, &ma);
    int m = td_update_appinfo(tid, local_cid, new_name, &mi);

    if (n < 0 || m < 0) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "写主机数据库失败（app.db %d / appinfo.db %d）", n, m);
        pldmgr_log("[PLDMGR] tile_db: %s 改名未落库 -> \"%s\"（app.db %d / appinfo %d）\n",
                   tid, new_name, n, m);
        return -2;
    }
    if (n + m == 0 && ma + mi == 0) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "这个游戏还没注册进主机数据库");
        pldmgr_log("[PLDMGR] tile_db: app.db / appinfo.db 里没有 %s 的行（还没注册？）\n",
                   tid);
        return -1;
    }
    pldmgr_log("[PLDMGR] tile_db: %s -> \"%s\" %s（app.db 写 %d 行、appinfo 写 %d 行，"
               "匹配 %d + %d）\n",
               tid, new_name, (n + m > 0) ? "已更新" : "本来就一致", n, m, ma, mi);
    return 0;
}

int tile_db_set_bg(const char *tid, const char *dds_path,
                   char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!td_tid_ok(tid) || !dds_path || !dds_path[0]) {
        if (msg && msgsz) snprintf(msg, msgsz, "参数不合法");
        return -1;
    }
    tile_db_backup_dbs();

    /* ts 必须是**这个文件**的 mtime：先写文件再注册，两边才对得上。 */
    struct stat sb;
    if (stat(dds_path, &sb) != 0) {
        if (msg && msgsz) snprintf(msg, msgsz, "主机上没有这个背景文件");
        return -1;
    }
    char value[512];
    int vn = snprintf(value, sizeof(value), "%s?ts=%lld", dds_path,
                      (long long)sb.st_mtime);
    if (vn < 0 || (size_t)vn >= sizeof(value)) {
        if (msg && msgsz) snprintf(msg, msgsz, "路径太长");
        return -1;
    }

    sqlite3 *db = td_open(TILE_DB_APPDB_PATH);
    if (!db) {
        if (msg && msgsz) snprintf(msg, msgsz, "打不开主机数据库");
        return -1;
    }
    if (td_exec(db, "BEGIN IMMEDIATE") != SQLITE_OK) {
        if (msg && msgsz) snprintf(msg, msgsz, "开事务失败（%s）", sqlite3_errmsg(db));
        pldmgr_log("[PLDMGR] tile_db: bg BEGIN 失败：%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    /* 概念表按 localConceptId 定位。真标题用 "cid:scp:..." 只能读回来；
     * 读不到就退回参考工程那条 "cid:local:<tid>" —— 绑错最多 0 行，不会伤到别人。 */
    char local_cid[160];
    local_cid[0] = 0;
    td_read_local_cid(db, tid, local_cid, sizeof(local_cid));
    if (!local_cid[0]) snprintf(local_cid, sizeof(local_cid), "cid:local:%s", tid);

    int written = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table'",
                           -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *table = (const char *)sqlite3_column_text(st, 0);
            if (table && td_safe_ident(table) &&
                td_has_column(db, table, TILE_DB_BG_COLUMN))
                written += td_write_bg_cell(db, table, tid, local_cid, value);
        }
    } else {
        pldmgr_log("[PLDMGR] tile_db: bg 表结构扫描失败：%s\n", sqlite3_errmsg(db));
    }
    if (st) sqlite3_finalize(st);

    if (written <= 0) {
        td_exec(db, "ROLLBACK");
        sqlite3_close(db);
        if (msg && msgsz)
            snprintf(msg, msgsz, "主机数据库里还没有这个标题的背景行");
        pldmgr_log("[PLDMGR] tile_db: app.db 里没有 %s 的 %s 行（还没注册？）\n",
                   tid, TILE_DB_BG_COLUMN);
        return -1;
    }
    if (td_exec(db, "COMMIT") != SQLITE_OK) {
        if (msg && msgsz) snprintf(msg, msgsz, "提交失败（%s）", sqlite3_errmsg(db));
        pldmgr_log("[PLDMGR] tile_db: bg COMMIT 失败：%s\n", sqlite3_errmsg(db));
        td_exec(db, "ROLLBACK");
        sqlite3_close(db);
        return -2;
    }
    sqlite3_close(db);

    pldmgr_log("[PLDMGR] tile_db: %s 背景已登记 -> %s（%d 行，主屏实时重绘）\n",
               tid, value, written);
    return 0;
}
