/* [CN-FORK] v0.5.2.166 主屏固定列表（app.db 的 tbl_group_<uid>）读写。
 * 契约与真机取证见 include/tile_group.h 的头注释 —— 这里只写实现要点。
 *
 * 三个容易踩的点：
 *
 * ① **表名不能硬编码**。账号 ID 是 10 位数字（真机上是 0438122023 / 25 / 26 / 27），
 *    换个机器就变。与 tile_db.c / app_visibility.c 同口径：扫 sqlite_master 拿前缀，
 *    再按列名确认它真的是我们要的那张表。
 *
 * ② **idx 是主屏位置**，不是「固定了几次」。所以置顶不是「把 idx 改成 0 或 -1」，
 *    而是「把这一条移到最前、其余依次后移、整体重编号 1..N」—— 只改一个值会让
 *    两条撞在同一个 idx 上，Shell 的排序就变成未定义行为。
 *
 * ③ **只动 MyHome 组**。tbl_group 里可能还有别的 groupName（真机这次只有 MyHome，
 *    但没有任何东西保证以后不会有），所有 SQL 都带 `AND groupName='MyHome'`。
 *
 * 栈预算：所有缓冲都 malloc（TG_MAX_ITEMS 条 × 28 字节 ≈ 14KB，栈上放不下）。
 * 单条 SQL 文本走 512 字节的栈缓冲，那是这个仓库里反复验证过的尺寸。 */

#include "tile_group.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "pldmgr.h"
#include "sqlite3.h"
#include "tile_db.h"

/* 一个账号的 MyHome 组最多记这么多条。真机上是 3~5 条；给 512 是留足余量，
 * 又能在有人把整库塞进来时挡住内存膨胀（超出部分截断并记日志）。 */
#define TG_MAX_ITEMS 512
/* itemId 实际 9 位，留到 24 免得将来有更长的形式。值公开在 tile_group.h
 * （调用方要按它开数组），这里只做个别名，免得两个数字各写一份、哪天改一个。 */
#define TG_ID_MAX TILE_GROUP_ID_MAX

typedef struct {
    char id[TG_ID_MAX];
    int idx;
} tg_item;

typedef struct {
    tg_item *it;
    int n;
} tg_list;

/* ── 基础防护（与 tile_db.c 的 td_tid_ok / td_safe_ident 同规格）────────────── */

static int tg_tid_ok(const char *t) {
    if (!t) return 0;
    size_t n = strlen(t);
    if (n < 6 || n > 16) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = t[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

static int tg_safe_ident(const char *s) {
    return s && *s && !strchr(s, '\'') && !strchr(s, '"') && !strchr(s, ';');
}

static int tg_has_column(sqlite3 *db, const char *table, const char *col) {
    if (!tg_safe_ident(table) || !tg_safe_ident(col)) return 0;
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

/* 这张表是不是「我们要的那张 tbl_group」：四条列齐了才算。 */
static int tg_is_group_table(sqlite3 *db, const char *table) {
    return tg_has_column(db, table, "itemId") &&
           tg_has_column(db, table, "groupName") &&
           tg_has_column(db, table, "idx") &&
           tg_has_column(db, table, "type");
}

static int tg_exec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err) sqlite3_free(err);
    return rc;
}

static sqlite3 *tg_open(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(TILE_DB_APPDB_PATH, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        pldmgr_log("[PLDMGR] tile_group: 打不开 %s（%s）\n", TILE_DB_APPDB_PATH,
                   db ? sqlite3_errmsg(db) : "no handle");
        if (db) sqlite3_close(db);
        return NULL;
    }
    /* Shell 自己也常开着这个库 —— 与 tile_db.c 同口径给 5 秒。 */
    sqlite3_busy_timeout(db, 5000);
    return db;
}

/* ── 单张表上的原子操作 ──────────────────────────────────────────────────── */

static void tg_list_free(tg_list *l) {
    if (l && l->it) { free(l->it); l->it = NULL; l->n = 0; }
}

static int tg_list_alloc(tg_list *l) {
    l->it = (tg_item *)malloc(sizeof(tg_item) * TG_MAX_ITEMS);
    l->n = 0;
    return l->it ? 0 : -1;
}

/* 读 MyHome 组，按 idx 升序（idx 为 NULL 的排在最后，用 itemId 兜底保证顺序稳定）。 */
static int tg_load(sqlite3 *db, const char *table, tg_list *l) {
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT itemId, idx FROM %s WHERE groupName='%s' "
             "ORDER BY (idx IS NULL), idx, itemId",
             table, TILE_GROUP_NAME);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pldmgr_log("[PLDMGR] tile_group: 读 %s 失败：%s\n", table, sqlite3_errmsg(db));
        if (st) sqlite3_finalize(st);
        return -1;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (l->n >= TG_MAX_ITEMS) {
            pldmgr_log("[PLDMGR] tile_group: %s 超过 %d 条，多余部分忽略\n",
                       table, TG_MAX_ITEMS);
            break;
        }
        const char *id = (const char *)sqlite3_column_text(st, 0);
        if (!id) continue;
        snprintf(l->it[l->n].id, TG_ID_MAX, "%s", id);
        l->it[l->n].idx = sqlite3_column_int(st, 1);
        l->n++;
    }
    sqlite3_finalize(st);
    return 0;
}

/* 改一行已有的 idx。返回 1 = 改到了，0 = 这一行不在组里。 */
static int tg_set_idx(sqlite3 *db, const char *table, const char *id, int idx) {
    char sql[512];
    snprintf(sql, sizeof(sql),
             "UPDATE %s SET idx=? WHERE itemId=? AND groupName='%s'",
             table, TILE_GROUP_NAME);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, idx);
    sqlite3_bind_text(st, 2, id, -1, SQLITE_TRANSIENT);
    int rc = (sqlite3_step(st) == SQLITE_DONE) ? sqlite3_changes(db) : -1;
    sqlite3_finalize(st);
    return rc;
}

/* 往组里插一条。type 固定 'title'（真机上所有条目都是它），lastModifiedDate 留 NULL ——
 * 真机上**每一行都是 NULL**，说明系统自己也不填这一列，那我们也不填，少一处偏离。 */
static int tg_insert(sqlite3 *db, const char *table, const char *id, int idx) {
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO %s (itemId, groupName, type, idx, lastModifiedDate) "
             "VALUES (?, '%s', 'title', ?, NULL)",
             table, TILE_GROUP_NAME);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, idx);
    int rc = (sqlite3_step(st) == SQLITE_DONE) ? sqlite3_changes(db) : -1;
    sqlite3_finalize(st);
    return rc;
}

/* 把一条移出组。返回 1 = 删掉了，0 = 本来就不在。 */
static int tg_delete(sqlite3 *db, const char *table, const char *id) {
    char sql[512];
    snprintf(sql, sizeof(sql),
             "DELETE FROM %s WHERE itemId=? AND groupName='%s'", table, TILE_GROUP_NAME);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    int rc = (sqlite3_step(st) == SQLITE_DONE) ? sqlite3_changes(db) : -1;
    sqlite3_finalize(st);
    return rc;
}

/* 把整组重新编号成 1..n（保持传入数组的先后）。返回写了几行。 */
static int tg_renumber(sqlite3 *db, const char *table, tg_list *l) {
    int written = 0;
    for (int i = 0; i < l->n; i++) {
        int want = i + 1;
        if (l->it[i].idx == want) continue; /* 已经对了：一次写都不发 */
        int r = tg_set_idx(db, table, l->it[i].id, want);
        if (r < 0) return -1;
        written += r;
        l->it[i].idx = want;
    }
    return written;
}

/* ── 表遍历 ──────────────────────────────────────────────────────────────── */

/* 对每张 tbl_group_% 表调一次 fn(db, table, ctx)；每张表各自一个事务。
 * fn 返回「写了几行」：>0 才 COMMIT，否则 ROLLBACK 立刻把锁放掉（与 tile_db.c 同策略）。
 * 返回处理的表数；*failed 置位表示至少一张表出错（照旧继续处理其余表）。 */
typedef int (*tg_table_fn)(sqlite3 *db, const char *table, void *ctx);

static int tg_foreach(int *out_tables, int *out_failed, tg_table_fn fn, void *ctx) {
    if (out_tables) *out_tables = 0;
    if (out_failed) *out_failed = 0;
    sqlite3 *db = tg_open();
    if (!db) {
        if (out_failed) *out_failed = 1;
        return 0;
    }
    /* 第一次动手前留一份原始底片（已有就不覆盖）—— 这个库改坏了游戏可能起不来。 */
    tile_db_backup_dbs();

    sqlite3_stmt *st = NULL;
    int tables = 0, failed = 0;
    if (sqlite3_prepare_v2(db,
                           "SELECT name FROM sqlite_master WHERE type='table' "
                           "AND name LIKE 'tbl_group%' ORDER BY name",
                           -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *table = (const char *)sqlite3_column_text(st, 0);
            if (!table || !tg_safe_ident(table) || !tg_is_group_table(db, table)) continue;
            tables++;
            if (tg_exec(db, "BEGIN IMMEDIATE") != SQLITE_OK) {
                pldmgr_log("[PLDMGR] tile_group: %s BEGIN 失败：%s\n", table,
                           sqlite3_errmsg(db));
                failed = 1;
                continue;
            }
            int written = fn(db, table, ctx);
            if (written < 0) {
                failed = 1;
                tg_exec(db, "ROLLBACK");
                pldmgr_log("[PLDMGR] tile_group: %s 写失败：%s\n", table,
                           sqlite3_errmsg(db));
            } else if (written > 0) {
                if (tg_exec(db, "COMMIT") != SQLITE_OK) {
                    failed = 1;
                    tg_exec(db, "ROLLBACK");
                    pldmgr_log("[PLDMGR] tile_group: %s COMMIT 失败：%s\n", table,
                               sqlite3_errmsg(db));
                }
            } else {
                tg_exec(db, "ROLLBACK");
            }
        }
    } else {
        failed = 1;
        pldmgr_log("[PLDMGR] tile_group: sqlite_master 扫描失败：%s\n", sqlite3_errmsg(db));
    }
    if (st) sqlite3_finalize(st);
    sqlite3_close(db);
    if (out_tables) *out_tables = tables;
    if (out_failed) *out_failed = failed;
    return tables;
}

/* ── ① 读列表 ────────────────────────────────────────────────────────────── */

/* 跨账号合成一份列表。
 *
 * 取最小 idx 是**错的**：账号 …023 只有 3 条（NPXS40071 排在 3），而 …025 有 5 条
 * （NPXS40071 排在 5），取最小会让 NPXS40071 与 PPSA32786 并列在 3 —— 顺序失去意义。
 * 改成**以条目最多的那个账号为基准**：它的顺序就是主屏的真实顺序，其余账号里
 * 多出来的条目按「基准之后依次追加」补上。真机上 4 个账号里 …025/026/027 都是 5 条，
 * 基准就是它们，结果与主屏一致。
 *
 * 两趟扫描：第一趟挑基准（需要看完全部表才知道谁最大），第二趟补其余账号的独有条目。 */
typedef struct {
    tg_item *base;
    int n;
    int best;
    int accounts;
    int phase;   /* 0 = 挑基准，1 = 合并其余 */
    int next_idx;
} tg_merge_ctx;

static int tg_merge_find(const tg_item *base, int n, const char *id) {
    for (int i = 0; i < n; i++) {
        if (strcmp(base[i].id, id) == 0) return i;
    }
    return -1;
}

static int tg_merge_cb(sqlite3 *db, const char *table, void *ctx) {
    tg_merge_ctx *m = (tg_merge_ctx *)ctx;
    tg_list l;
    if (tg_list_alloc(&l) != 0) return 0;
    if (tg_load(db, table, &l) != 0) { tg_list_free(&l); return -1; }
    if (m->phase == 0) m->accounts++; /* 两趟扫描，只在第一趟数账号 */

    if (m->phase == 0) {
        if (l.n > m->best) {
            for (int i = 0; i < l.n; i++) m->base[i] = l.it[i];
            m->n = l.n;
            m->best = l.n;
        }
    } else {
        for (int i = 0; i < l.n; i++) {
            if (tg_merge_find(m->base, m->n, l.it[i].id) >= 0) continue;
            if (m->n >= TG_MAX_ITEMS) break;
            m->base[m->n] = l.it[i];
            /* 基准之外的条目排在基准之后，编号依次往后接。 */
            m->base[m->n].idx = m->next_idx++;
            m->n++;
        }
    }
    tg_list_free(&l);
    return 0; /* 只读：0 → 上面会 ROLLBACK，立刻放锁 */
}

static int tg_item_cmp(const void *a, const void *b) {
    const tg_item *x = (const tg_item *)a;
    const tg_item *y = (const tg_item *)b;
    if (x->idx != y->idx) return x->idx - y->idx;
    return strcmp(x->id, y->id);
}

int tile_group_list_json(char *out, size_t size) {
    if (!out || size < 64) return -1;
    tg_merge_ctx m;
    memset(&m, 0, sizeof(m));
    m.base = (tg_item *)malloc(sizeof(tg_item) * TG_MAX_ITEMS);
    if (!m.base) { snprintf(out, size, "{\"ok\":false,\"note\":\"内存不足\"}"); return (int)strlen(out); }

    int tables = 0, failed = 0, t2 = 0, f2 = 0;
    tg_foreach(&tables, &failed, tg_merge_cb, &m);           /* 第一趟：挑基准 */
    m.phase = 1;
    m.next_idx = 1;
    for (int i = 0; i < m.n; i++) {
        if (m.base[i].idx >= m.next_idx) m.next_idx = m.base[i].idx + 1;
    }
    tg_foreach(&t2, &f2, tg_merge_cb, &m);                   /* 第二趟：补独有条目 */
    if (f2) failed = 1;

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(m.base);
        snprintf(out, size, "{\"ok\":false,\"note\":\"内存不足\"}");
        return (int)strlen(out);
    }
    cJSON_AddBoolToObject(root, "ok", (tables > 0 && !failed) ? 1 : 0);
    cJSON_AddStringToObject(root, "group", TILE_GROUP_NAME);
    cJSON_AddNumberToObject(root, "accounts", m.accounts);
    if (tables == 0)
        cJSON_AddStringToObject(root, "note", "主机 app.db 里没有 tbl_group 表");
    else if (failed)
        cJSON_AddStringToObject(root, "note", "部分账号的固定列表读失败，见主机日志");

    qsort(m.base, (size_t)m.n, sizeof(tg_item), tg_item_cmp);
    cJSON *arr = cJSON_AddArrayToObject(root, "items");
    for (int i = 0; arr && i < m.n; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddStringToObject(o, "itemId", m.base[i].id);
        cJSON_AddNumberToObject(o, "idx", m.base[i].idx);
        cJSON_AddItemToArray(arr, o);
    }
    free(m.base);

    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) { snprintf(out, size, "{\"ok\":false,\"note\":\"序列化失败\"}"); return (int)strlen(out); }
    size_t n = strlen(s);
    int ok = n < size;
    if (ok) memcpy(out, s, n + 1);
    free(s);
    if (!ok) { out[0] = 0; return -1; }
    return (int)n;
}

/* ── ② 置顶 / 取消置顶 ───────────────────────────────────────────────────── */

typedef struct {
    const char *tid;
    int pinned;
    int wrote;   /* 写了几个账号的库 */
    int top_idx; /* 置顶后的位置（1 起） */
    int changed; /* 至少有一个账号真的动过 */
} tg_pin_ctx;

static int tg_pin_cb(sqlite3 *db, const char *table, void *ctx) {
    tg_pin_ctx *p = (tg_pin_ctx *)ctx;
    tg_list l;
    if (tg_list_alloc(&l) != 0) return -1;
    if (tg_load(db, table, &l) != 0) { tg_list_free(&l); return -1; }

    /* 先找出 tid 现在在不在组里、在第几位（0 = 不在）。 */
    int at = -1;
    for (int i = 0; i < l.n; i++) {
        if (strcmp(l.it[i].id, p->tid) == 0) { at = i; break; }
    }

    int written = 0;
    if (!p->pinned) {
        if (at < 0) { tg_list_free(&l); return 0; } /* 本来就没固定，什么都不用做 */
        int r = tg_delete(db, table, p->tid);
        if (r < 0) { tg_list_free(&l); return -1; }
        written += r;
        /* 把它从数组里摘掉，剩下的重新编号（编号连续，Shell 的排序才确定）。 */
        for (int i = at; i + 1 < l.n; i++) l.it[i] = l.it[i + 1];
        l.n--;
        int rr = tg_renumber(db, table, &l);
        if (rr < 0) { tg_list_free(&l); return -1; }
        written += rr;
        p->changed = 1;
    } else {
        if (at == 0) { tg_list_free(&l); return 0; } /* 已经在最前，不用动 */
        /* 新顺序 = [tid] + 原来的（去掉 tid），再整体重编号 1..N。 */
        tg_item head;
        snprintf(head.id, TG_ID_MAX, "%s", p->tid);
        head.idx = 1;
        int w = 0;
        for (int i = 0; i < l.n; i++) {
            if (i == at) continue;
            l.it[w++] = l.it[i];
        }
        l.n = w;
        /* 从后往前挪，给头部腾出位置。满员时丢掉最后一条 —— 越界写数组
         * 是这仓库最贵的一类 bug（栈上 64KB 数组那次直接把整机干掉了）。 */
        if (l.n >= TG_MAX_ITEMS) l.n = TG_MAX_ITEMS - 1;
        for (int i = l.n; i > 0; i--) l.it[i] = l.it[i - 1];
        l.it[0] = head;
        l.n++;
        if (at < 0) {
            /* 不在组里 → 得先插一行（UPDATE 改不到不存在的行）。 */
            int r = tg_insert(db, table, p->tid, 1);
            if (r < 0) { tg_list_free(&l); return -1; }
            written += r;
        }
        int rr = tg_renumber(db, table, &l);
        if (rr < 0) { tg_list_free(&l); return -1; }
        written += rr;
        p->top_idx = 1;
        p->changed = 1;
    }
    tg_list_free(&l);
    if (written > 0) p->wrote++;
    return written;
}

int tile_group_set_pin(const char *tid, int pinned, char *msg, size_t msgsz,
                       int *out_accounts, int *out_idx) {
    if (out_accounts) *out_accounts = 0;
    if (out_idx) *out_idx = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!tg_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "游戏编号不合法");
        return -1;
    }

    tg_pin_ctx p;
    memset(&p, 0, sizeof(p));
    p.tid = tid;
    p.pinned = pinned ? 1 : 0;

    int tables = 0, failed = 0;
    tg_foreach(&tables, &failed, tg_pin_cb, &p);

    if (tables == 0) {
        if (msg) snprintf(msg, msgsz, "主机 app.db 里没有 tbl_group 表，固定列表写不进去");
        return -2;
    }
    if (failed && !p.changed) {
        if (msg) snprintf(msg, msgsz, "主机固定列表写不进去（库被占用或只读）");
        return -2;
    }
    if (out_accounts) *out_accounts = p.wrote;
    if (out_idx) *out_idx = p.top_idx;
    pldmgr_log("[PLDMGR] tile_group: %s pin=%d accounts=%d changed=%d\n",
               tid, p.pinned, p.wrote, p.changed);
    return 0;
}

/* ── ③ 按给定顺序重排已固定项 ────────────────────────────────────────────── */

/* 重排时的一条：`rank` = 在调用方给的顺序里的名次，`orig` = 它**现在**占的 idx。 */
typedef struct {
    char id[TG_ID_MAX];
    int rank;
    int orig;
} tg_sel;

typedef struct {
    const char *const *tids;
    int n;
    int moved;  /* 真的换了位置的条目数（跨账号取最大值，避免重复计数） */
} tg_order_ctx;

static int tg_order_cb(sqlite3 *db, const char *table, void *ctx) {
    tg_order_ctx *o = (tg_order_ctx *)ctx;
    tg_list l;
    if (tg_list_alloc(&l) != 0) return -1;
    if (tg_load(db, table, &l) != 0) { tg_list_free(&l); return -1; }

    /* ① 找出「在组里 **且** 出现在 tids 里」的条目：记下它的新名次与**原来的 idx**。
     *    l 是按 idx 升序读出来的，所以 sel[] 里 orig 天然是升序 —— 它就是「槽位序列」。 */
    tg_sel *sel = (tg_sel *)malloc(sizeof(tg_sel) * TG_MAX_ITEMS);
    if (!sel) { tg_list_free(&l); return -1; }
    int ns = 0;
    for (int i = 0; i < l.n; i++) {
        int rank = -1;
        for (int j = 0; j < o->n; j++) {
            if (strcmp(l.it[i].id, o->tids[j]) == 0) { rank = j; break; }
        }
        if (rank < 0) continue;
        snprintf(sel[ns].id, TG_ID_MAX, "%s", l.it[i].id);
        sel[ns].rank = rank;
        sel[ns].orig = l.it[i].idx;
        ns++;
    }
    if (ns < 2) { /* 少于两条没有「换位」可言 */
        free(sel); tg_list_free(&l);
        return 0;
    }

    /* ② 先把「槽位序列」抄下来（此时仍是升序），再按新名次把 sel[] 排好。
     *    排完第 i 名就拿第 i 个槽位 —— 槽位集合一个不增一个不减，
     *    所以没被提到的条目（系统条目、别的游戏）一个字节都不动。 */
    int *slot = (int *)malloc(sizeof(int) * (size_t)ns);
    if (!slot) { free(sel); tg_list_free(&l); return -1; }
    for (int i = 0; i < ns; i++) slot[i] = sel[i].orig;

    for (int i = 1; i < ns; i++) { /* 插入排序：ns 很小（真机个位数） */
        tg_sel k = sel[i];
        int j = i - 1;
        while (j >= 0 && sel[j].rank > k.rank) { sel[j + 1] = sel[j]; j--; }
        sel[j + 1] = k;
    }

    int written = 0, moved = 0;
    for (int i = 0; i < ns; i++) {
        /* 「原来占的槽位」与「排完后该占的槽位」一样 → 这一条名次没变，一次写都不发。 */
        if (sel[i].orig == slot[i]) continue;
        int r = tg_set_idx(db, table, sel[i].id, slot[i]);
        if (r < 0) { free(slot); free(sel); tg_list_free(&l); return -1; }
        written += r;
        moved++;
    }
    free(slot);
    free(sel);
    tg_list_free(&l);
    if (moved > o->moved) o->moved = moved;
    return written;
}

int tile_group_reorder(const char *const *tids, int n, char *msg, size_t msgsz,
                       int *out_moved) {
    if (out_moved) *out_moved = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!tids || n <= 0) {
        if (msg) snprintf(msg, msgsz, "顺序列表是空的");
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (!tg_tid_ok(tids[i])) {
            if (msg) snprintf(msg, msgsz, "顺序列表里有不合法的游戏编号");
            return -1;
        }
    }

    tg_order_ctx o;
    memset(&o, 0, sizeof(o));
    o.tids = tids;
    o.n = n;

    int tables = 0, failed = 0;
    tg_foreach(&tables, &failed, tg_order_cb, &o);

    if (tables == 0) {
        if (msg) snprintf(msg, msgsz, "主机 app.db 里没有 tbl_group 表，顺序写不进去");
        return -2;
    }
    if (failed) {
        if (msg) snprintf(msg, msgsz, "主机固定列表写不进去（库被占用或只读）");
        return -2;
    }
    if (out_moved) *out_moved = o.moved;
    pldmgr_log("[PLDMGR] tile_group: reorder n=%d moved=%d\n", n, o.moved);
    return 0;
}

/* ── ④ [CN-FORK] v0.6.0.179 只读查询（个性化软件的 pinned 字段）──────────── */

/* 不回写，所以每张表都不开事务（tg_foreach 里 fn 返回 0 → 立刻 ROLLBACK 放锁）。
 * 但 tg_foreach 仍会走一次 tile_db_backup_dbs —— 那是幂等的「首次动手前留底片」，
 * 读路径撞上它只是建一回文件，不会改 app.db 一个字节。 */

typedef struct {
    char (*ids)[TG_ID_MAX];
    int max;
    int n;
} tg_ids_ctx;

static int tg_ids_cb(sqlite3 *db, const char *table, void *ctx) {
    tg_ids_ctx *p = (tg_ids_ctx *)ctx;
    tg_list l;
    if (tg_list_alloc(&l) != 0) return 0;
    if (tg_load(db, table, &l) != 0) { tg_list_free(&l); return 0; }
    for (int i = 0; i < l.n && p->n < p->max; i++) {
        int dup = 0;
        for (int k = 0; k < p->n; k++) {
            if (strcmp(p->ids[k], l.it[i].id) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        snprintf(p->ids[p->n], TG_ID_MAX, "%s", l.it[i].id);
        p->n++;
    }
    tg_list_free(&l);
    return 0;
}

int tile_group_pin_tids(char (*ids)[TG_ID_MAX], int max) {
    if (!ids || max <= 0) return 0;
    tg_ids_ctx p;
    memset(&p, 0, sizeof(p));
    p.ids = ids;
    p.max = max;
    tg_foreach(NULL, NULL, tg_ids_cb, &p);
    return p.n;
}

typedef struct {
    const char *tid;
    int found;
} tg_has_ctx;

static int tg_has_cb(sqlite3 *db, const char *table, void *ctx) {
    tg_has_ctx *p = (tg_has_ctx *)ctx;
    tg_list l;
    if (tg_list_alloc(&l) != 0) return 0;
    if (tg_load(db, table, &l) != 0) { tg_list_free(&l); return 0; }
    for (int i = 0; i < l.n; i++) {
        if (strcmp(l.it[i].id, p->tid) == 0) { p->found = 1; break; }
    }
    tg_list_free(&l);
    return 0;
}

int tile_group_has(const char *tid) {
    if (!tg_tid_ok(tid)) return 0;
    tg_has_ctx p;
    memset(&p, 0, sizeof(p));
    p.tid = tid;
    tg_foreach(NULL, NULL, tg_has_cb, &p);
    return p.found;
}
