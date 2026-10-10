/* [CN-FORK] v0.5.2.158 「个性化软件」的实现：读写 app.db 里每个账号的
 * tbl_iconinfo_<uid>.visible。语义、真机取证与范围限制见 include/app_visibility.h。
 *
 * 为什么可以直接跑 SQL：本文件与 tile_db.c 一样静态链进合并版 sqlite3
 * （固件不带 libSceSqlite.sprx，动态链接不了 —— Makefile 里 SQLITE_OBJ 那段）。
 * game_mgr.c 那份手写只读解析器是给「每条连接 64KB 栈 + 高频轮询」用的，
 * 这里一次弹窗拉一遍、低频，用不着那套。
 *
 * 三条从 tile_db.c 抄下来的规矩，一条都不省：
 *   ① 表名要插进 SQL 文本（bind 不了标识符），所以先过一遍引号/分号过滤；
 *   ② busy_timeout 给 5 秒 —— Shell 自己常开着 app.db 的读句柄；
 *   ③ 第一次动手前留一份不可覆盖的原始底片（tile_db_backup_dbs）。 */

#include "app_visibility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_helpers.h" /* JsonListBuilder / json_append */
#include "pldmgr.h"       /* pldmgr_log */
#include "sqlite3.h"
#include "tile_db.h"      /* tile_db_backup_dbs */
#include "utils.h"        /* pldmgr_json_escape */

/* 内置软件不多（真机原件 19 个 NPXS 行），上限给足余量即可，不做动态扩容。 */
#define AV_MAX_APPS 64
#define AV_NAME_MAX 192

/* ── 基础防护 ─────────────────────────────────────────────────────────────── */

/* 与 tile_db.c 的 td_safe_ident 同规格：表名是从 sqlite_master 读回来的，
 * 这一层是纵深防御，不是在防外部输入。 */
static int av_safe_ident(const char *s) {
    return s && *s && !strchr(s, '\'') && !strchr(s, '"') && !strchr(s, ';');
}

/* 6..16 位 A-Z0-9（与 td_tid_ok 同规格），再加一条 NPXS 前缀白名单。
 * 前缀这条**不是**为了好看：它是「别把自己藏掉」的硬闸门 ——
 * NayePlmTools 自己的磁贴是 NAYE00001，恢复入口就在那块磁贴上。 */
static int av_tid_ok(const char *tid) {
    if (!tid) return 0;
    size_t n = strlen(tid);
    if (n < 6 || n > 16) return 0;
    if (strncmp(tid, APP_VIS_PREFIX, strlen(APP_VIS_PREFIX)) != 0) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = tid[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

/* ── 面板改不动的那几条（v0.5.2.160）────────────────────────────────────────
 * 依据是真机快照，不是猜的（取证脚本 _ftp_probe/probe_vis159*.py，2026-10-10）：
 * 同一次拉回来的 app.db 里，PlayStation Store / 媒体相册 的 visible=0 确实让磁贴
 * 消失了，而下面前四条同样是 0 却照样画 —— 所以它们不听这一列。
 * 后一组（编辑 / 光碟占位）**没有**「写 0 仍显示」的实测证据（它们出厂就是 0，
 * 用户也没报它们在主屏上），归进来的理由是「主屏什么时候画它们由系统自己定」，
 * 面板给它们一个开关只会再造出一条对不上账的「已隐藏」。
 * ⚠ 这两类的措辞刻意不同，别把它们混成同一句话。 */
typedef struct {
    const char *tid;
    const char *reason;
} AvFixed;

static const AvFixed AV_FIXED[] = {
    { "NPXS40056", "主屏固定入口：实测把它写成隐藏，主屏照旧画出来" },
    { "NPXS40071", "主屏固定入口：实测把它写成隐藏，主屏照旧画出来" },
    { "NPXS40139", "主屏固定入口：实测把它写成隐藏，主屏照旧画出来" },
    { "NPXS40063", "主屏固定入口：它出厂就是隐藏值，主屏却一直在画" },
    { "NPXS40145", "主屏自己的编辑入口，不是一张可隐藏的软件磁贴" },
    { "NPXS40205", "主屏自己的编辑入口，不是一张可隐藏的软件磁贴" },
    { "NPXS40140", "光碟占位条目：插不插碟由系统决定，面板改不动" },
    { "NPXS40144", "光碟占位条目：插不插碟由系统决定，面板改不动" },
    { "NPXS40148", "光碟占位条目：插不插碟由系统决定，面板改不动" },
    { "NPXS40149", "光碟占位条目：插不插碟由系统决定，面板改不动" },
    { "NPXS40150", "光碟占位条目：插不插碟由系统决定，面板改不动" },
};

int app_vis_fixed_reason(const char *tid, const char **why) {
    if (!tid) return 0;
    for (size_t i = 0; i < sizeof(AV_FIXED) / sizeof(AV_FIXED[0]); i++) {
        if (strcmp(AV_FIXED[i].tid, tid) != 0) continue;
        if (why) *why = AV_FIXED[i].reason;
        return 1;
    }
    return 0;
}

static sqlite3 *av_open(void) {
    sqlite3 *db = NULL;
    /* READWRITE 而不是 READONLY：只读打开碰到 WAL 库会因拿不到 -shm 直接失败
     * （10-10 这台实测是 journal 模式，但没有 -wal 不等于别的主机也没有）。
     * 读路径上我们一条 UPDATE 都不发，多要这个权限没有副作用。 */
    if (sqlite3_open_v2(APP_VIS_APPDB_PATH, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        pldmgr_log("[PLDMGR] app_vis: 打不开 %s（%s）\n", APP_VIS_APPDB_PATH,
                   db ? sqlite3_errmsg(db) : "no handle");
        if (db) sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 5000);
    return db;
}

/* sqlite_master 里所有带 visible + titleId 两列的 tbl_iconinfo_<uid> 表。
 * 表名写进 out 数组（各 AV_IDENT_MAX 字节），返回找到的张数。
 * 用 LIKE 'tbl_iconinfo%' 而不是硬编码那串数字后缀：后缀是账号 uid，每台主机不同，
 * 而且一台机器可以有多个用户。tbl_concepticoninfo_<uid> 天然不匹配（前缀不同），
 * 它也没有 visible 列，双重保险。 */
#define AV_TABLE_MAX 8
#define AV_IDENT_MAX 64
static int av_iconinfo_tables(sqlite3 *db, char (*tables)[AV_IDENT_MAX], int max) {
    int n = 0;
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT name FROM sqlite_master WHERE type='table' "
                      "AND name LIKE 'tbl_iconinfo%' ORDER BY name";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        if (!name || !av_safe_ident(name)) continue;
        char probe[AV_IDENT_MAX + 128];
        snprintf(probe, sizeof(probe),
                 "SELECT count(*) FROM pragma_table_info('%s') "
                 "WHERE name='visible' OR name='titleId'", name);
        sqlite3_stmt *ck = NULL;
        int has = 0;
        if (sqlite3_prepare_v2(db, probe, -1, &ck, NULL) == SQLITE_OK) {
            if (sqlite3_step(ck) == SQLITE_ROW) has = sqlite3_column_int(ck, 0) == 2;
        }
        if (ck) sqlite3_finalize(ck);
        if (!has) continue;
        strncpy(tables[n], name, AV_IDENT_MAX - 1);
        tables[n][AV_IDENT_MAX - 1] = 0;
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

/* ── 列表 ─────────────────────────────────────────────────────────────────── */

typedef struct {
    char tid[16];
    char name[AV_NAME_MAX];
    int hidden;   /* 三态累积：-1 未定，1 所有账号都隐藏，0 至少一个账号还显示 */
} AvApp;

/* 把一条 (tid, name, visible) 并进累积表：已有同 tid 就按「与」收敛显隐状态。 */
static void av_merge(AvApp *apps, int *count, const char *tid, const char *name,
                     int visible) {
    int now_hidden = visible ? 0 : 1;
    for (int i = 0; i < *count; i++) {
        if (strcmp(apps[i].tid, tid) != 0) continue;
        /* 多账号取「与」：只要还有一个用户看得见，就不算被藏起来。
         * 写侧本来就是一次把所有账号表都写平，所以真机上这个与几乎恒等于单值。 */
        if (apps[i].hidden == -1) apps[i].hidden = now_hidden;
        else if (apps[i].hidden != now_hidden) apps[i].hidden = 0;
        if (!apps[i].name[0] && name && name[0])
            strncpy(apps[i].name, name, AV_NAME_MAX - 1);
        return;
    }
    if (*count >= AV_MAX_APPS) return;
    AvApp *a = &apps[*count];
    strncpy(a->tid, tid, sizeof(a->tid) - 1);
    a->tid[sizeof(a->tid) - 1] = 0;
    a->name[0] = 0;
    if (name && name[0]) strncpy(a->name, name, AV_NAME_MAX - 1);
    a->hidden = now_hidden;
    (*count)++;
}

/* 一个比较器给 qsort 用：按 titleId 升序，界面顺序就稳定了。
 * 不按名字排：中文标题按 UTF-8 字节排出来的顺序对中文用户是乱的。 */
static int av_cmp(const void *x, const void *y) {
    return strcmp(((const AvApp *)x)->tid, ((const AvApp *)y)->tid);
}

/* 出错时把原因写进 out_err（可为 NULL）。 */
static void av_err(char *out_err, size_t esz, const char *msg) {
    if (out_err && esz) {
        strncpy(out_err, msg, esz - 1);
        out_err[esz - 1] = 0;
    }
}

int app_vis_list_json(char *out, size_t size) {
    char tables[AV_TABLE_MAX][AV_IDENT_MAX];
    char *buf = NULL;
    AvApp *apps = NULL;
    int count = 0, ntables = 0;
    char err[128];
    err[0] = 0;

    if (!out || size < 64) return -1;
    out[0] = 0;

    sqlite3 *db = av_open();
    if (!db) {
        av_err(err, sizeof(err), "打不开主机的应用数据库 app.db");
        goto emit;
    }
    ntables = av_iconinfo_tables(db, tables, AV_TABLE_MAX);
    if (ntables <= 0) {
        av_err(err, sizeof(err), "应用数据库里没有可见性表（tbl_iconinfo_*）");
        goto emit;
    }

    apps = calloc(AV_MAX_APPS, sizeof(AvApp));
    if (!apps) {
        av_err(err, sizeof(err), "内存不足");
        goto emit;
    }

    /* 每个账号一条 JOIN：名字以 tbl_contentinfo 为准（改名链写的是它），
     * 显隐以该账号自己的 iconinfo 行为准。 */
    int q_failed = 0;
    for (int i = 0; i < ntables; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql),
                 "SELECT c.titleId, c.titleName, t.visible "
                 "FROM tbl_contentinfo c JOIN %s t ON t.titleId = c.titleId "
                 "WHERE c.titleId LIKE 'NPXS%%' ORDER BY c.titleId",
                 tables[i]);
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
            q_failed = 1;
            pldmgr_log("[PLDMGR] app_vis: 查 %s 失败（%s）\n", tables[i],
                       sqlite3_errmsg(db));
            continue;
        }
        int sr = SQLITE_OK;
        while ((sr = sqlite3_step(st)) == SQLITE_ROW) {
            const char *tid = (const char *)sqlite3_column_text(st, 0);
            const char *nm = (const char *)sqlite3_column_text(st, 1);
            int vis = sqlite3_column_int(st, 2);
            if (!tid || !*tid) continue;
            if (strncmp(tid, APP_VIS_PREFIX, strlen(APP_VIS_PREFIX)) != 0) continue;
            if (!nm || !*nm) nm = tid;
            av_merge(apps, &count, tid, nm, vis);
        }
        /* 读到一半出错（库被 Shell 占着 / 表被换掉）和「一条内置软件都没有」是两回事：
         * 前者要是悄悄吞掉，界面就回一张空列表冒充「主机上没有可管理的软件」——
         * 与 `.160` 修掉的「忙说成没装」同一类谎。步数返回码只能在循环里接住，
         * sqlite3_reset 已经拿不到它了。 */
        if (sr != SQLITE_DONE) {
            q_failed = 1;
            snprintf(err, sizeof(err), "读主机内置软件清单失败（%s）", sqlite3_errmsg(db));
        }
        sqlite3_finalize(st);
    }
    if (count) qsort(apps, (size_t)count, sizeof(AvApp), av_cmp);
    if (!count && q_failed && !err[0]) {
        snprintf(err, sizeof(err), "读主机内置软件清单失败，请稍后再试一次");
    }

emit:
    if (db) sqlite3_close(db);

    /* 拼 JSON 走堆：64 条 × 平均一百多字节，栈上放不下（连接线程只有 64KB，
     * 见 tools/check_stack_frames.sh）。`"controllable":false` 那几条还多带一句
     * reason，所以每条的余量按名字 + 理由一起算。 */
    size_t cap = 4096 + (size_t)count * (AV_NAME_MAX * 2 + 352);
    if (cap > size) cap = size;
    buf = malloc(cap);
    if (!buf) { if (apps) free(apps); return -1; }

    JsonListBuilder jb = { buf, cap, 0, 1 };
    json_append(&jb, "{\"apps\":[");
    for (int i = 0; i < count; i++) {
        char name_e[AV_NAME_MAX * 4 + 8];
        pldmgr_json_escape(apps[i].name, name_e, sizeof(name_e));
        int hidden = apps[i].hidden == 1;
        const char *why = NULL;
        if (app_vis_fixed_reason(apps[i].tid, &why)) {
            char why_e[512];
            pldmgr_json_escape(why, why_e, sizeof(why_e));
            if (json_append(&jb,
                            "%s{\"tid\":\"%s\",\"name\":\"%s\",\"hidden\":%s,"
                            "\"controllable\":false,\"reason\":\"%s\"}",
                            i ? "," : "", apps[i].tid, name_e,
                            hidden ? "true" : "false", why_e) != 0)
                break;
            continue;
        }
        if (json_append(&jb,
                        "%s{\"tid\":\"%s\",\"name\":\"%s\",\"hidden\":%s,"
                        "\"controllable\":true}",
                        i ? "," : "", apps[i].tid, name_e,
                        hidden ? "true" : "false") != 0)
            break;
    }
    char err_e[256];
    pldmgr_json_escape(err, err_e, sizeof(err_e));
    json_append(&jb, "],\"count\":%d,\"accounts\":%d,\"error\":\"%s\"}",
                count, ntables, err_e);

    size_t n = jb.pos;
    int ok = n < size;
    if (ok) {
        memcpy(out, buf, n + 1);
    } else {
        out[0] = 0;
        n = 0;
    }
    free(buf);
    if (apps) free(apps);
    return ok ? (int)n : -1;
}

/* ── 写入 ─────────────────────────────────────────────────────────────────── */

int app_vis_set(const char *tid, int hidden, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!av_tid_ok(tid)) {
        av_err(msg, msgsz, "软件编号不合法");
        return -1;
    }
    if (strncmp(tid, APP_VIS_PREFIX, strlen(APP_VIS_PREFIX)) != 0) {
        /* 走到这里只可能是调用方绕过了 av_tid_ok，留一条响一点的日志。 */
        av_err(msg, msgsz, "只能显示或隐藏主机内置软件");
        pldmgr_log("[PLDMGR] app_vis: 拒绝写非内置软件 %s\n", tid);
        return -1;
    }
    /* 固定条目**在这一步就挡掉**：库不打开、备份不留、一行不写。
     * `.158` 的毛病正是「照写不误 + 回一句成功」，用户看到「已隐藏」而主屏照旧，
     * 之后每一次排查都要从「到底写没写进去」重新开始。 */
    {
        const char *why = NULL;
        if (app_vis_fixed_reason(tid, &why)) {
            av_err(msg, msgsz, why);
            pldmgr_log("[PLDMGR] app_vis: 拒绝写固定条目 %s（面板改不动它）\n", tid);
            return APP_VIS_EFIXED;
        }
    }

    sqlite3 *db = av_open();
    if (!db) {
        av_err(msg, msgsz, "打不开主机的应用数据库 app.db");
        return -1;
    }

    char tables[AV_TABLE_MAX][AV_IDENT_MAX];
    int ntables = av_iconinfo_tables(db, tables, AV_TABLE_MAX);
    if (ntables <= 0) {
        sqlite3_close(db);
        av_err(msg, msgsz, "应用数据库里没有可见性表（tbl_iconinfo_*）");
        return -1;
    }

    /* 先确认这个 tid 真的注册在主机上：写一个不存在的行是静默 no-op，
     * 用户点了开关、界面回一句成功、磁贴一个字没动 —— 那是最难查的一类问题。
     * ⚠ `.160` 起把「读的时候出错」和「真的没有这一行」分开：Shell 常开着 app.db 的
     * 句柄，prepare/step 回 BUSY 时以前一律报「主机上没有这个内置软件（可能已被卸载）」，
     * 真机日志里 NPXS40099 在 20:45:45 被这样拒掉、20:47:37 同样的写又通过 ——
     * 明明只是**那两分钟正忙**。忙要说忙，别让人去查「为什么没装」。 */
    int exists = 0, step_rc = SQLITE_DONE;
    {
        sqlite3_stmt *st = NULL;
        step_rc = sqlite3_prepare_v2(db, "SELECT 1 FROM tbl_contentinfo WHERE titleId=?",
                                     -1, &st, NULL);
        if (step_rc == SQLITE_OK) {
            sqlite3_bind_text(st, 1, tid, -1, SQLITE_STATIC);
            step_rc = sqlite3_step(st);
            exists = (step_rc == SQLITE_ROW);
        }
        if (st) sqlite3_finalize(st);
    }
    if (!exists && step_rc != SQLITE_DONE) {
        char em[192];
        snprintf(em, sizeof(em), "主机正忙或应用数据库读不了（%s），稍后再试一次",
                 sqlite3_errmsg(db));
        /* 日志要在 close 之前落：sqlite3_errmsg(db) 句柄关掉之后就是野指针。 */
        pldmgr_log("[PLDMGR] app_vis: 查 %s 是否在册失败（%s）\n", tid, sqlite3_errmsg(db));
        sqlite3_close(db);
        av_err(msg, msgsz, em);
        return -2;
    }
    if (!exists) {
        sqlite3_close(db);
        av_err(msg, msgsz, "主机上没有这个内置软件（可能已被卸载）");
        pldmgr_log("[PLDMGR] app_vis: %s 不在 tbl_contentinfo 里\n", tid);
        return -1;
    }

    tile_db_backup_dbs();

    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        av_err(msg, msgsz, "主机正忙，应用数据库开不了写事务");
        return -2;
    }

    int written = 0, failed = 0;
    for (int i = 0; i < ntables; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "UPDATE %s SET visible=? WHERE titleId=?", tables[i]);
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
            failed++;
            pldmgr_log("[PLDMGR] app_vis: 准备 %s 失败（%s）\n", tables[i],
                       sqlite3_errmsg(db));
            continue;
        }
        sqlite3_bind_int(st, 1, hidden ? 0 : 1);
        sqlite3_bind_text(st, 2, tid, -1, SQLITE_STATIC);
        if (sqlite3_step(st) != SQLITE_DONE) {
            failed++;
            pldmgr_log("[PLDMGR] app_vis: 写 %s 失败（%s）\n", tables[i],
                       sqlite3_errmsg(db));
        } else {
            written += sqlite3_changes(db);
        }
        sqlite3_finalize(st);
    }

    if (written == 0 || failed > 0) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db);
        av_err(msg, msgsz, failed ? "写入失败，已回滚" : "主机上没有任何一行匹配这个软件");
        return failed ? -2 : -1;
    }
    if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db);
        av_err(msg, msgsz, "提交失败，已回滚");
        return -2;
    }
    sqlite3_close(db);

    pldmgr_log("[PLDMGR] app_vis: %s -> visible=%d（%d 张账号表）\n", tid,
               hidden ? 0 : 1, written);
    av_err(msg, msgsz, "");
    return 0;
}
