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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_tile.h"     /* app_tile_is_builtin / app_tile_icon_ver / app_tile_bg_ver */
#include "cJSON.h"        /* 隐藏意图文件（app_vis_wanted.json） */
#include "json_helpers.h" /* JsonListBuilder / json_append */
#include "pldmgr.h"       /* pldmgr_log */
#include "sqlite3.h"
#include "tile_db.h"      /* tile_db_backup_dbs */
#include "tile_group.h"   /* tile_group_pin_tids（列表里的 pinned 以主机为准） */
#include "utils.h"        /* pldmgr_json_escape */

/* 内置软件不多（真机原件 19 个 NPXS 行），上限给足余量即可，不做动态扩容。 */
#define AV_MAX_APPS 64
#define AV_NAME_MAX 192

/* 隐藏意图文件（见 include/app_visibility.h 的 app_vis_sync_all 注释）。
 * 单独一个文件而不是塞进 nayeplmtools_config.txt：那份配置是「面板设置」的
 * 键值对，这里是「每一条内置软件的选择」，形状与生命周期都不一样。 */
#ifndef APP_VIS_WANTED_PATH
#define APP_VIS_WANTED_PATH "/data/nayeplmtools/app_vis_wanted.json"
#endif

/* ── 基础防护 ─────────────────────────────────────────────────────────────── */

/* 与 tile_db.c 的 td_safe_ident 同规格：表名是从 sqlite_master 读回来的，
 * 这一层是纵深防御，不是在防外部输入。 */
static int av_safe_ident(const char *s) {
    return s && *s && !strchr(s, '\'') && !strchr(s, '"') && !strchr(s, ';');
}

/* 6..16 位 A-Z0-9，再加一条 NPXS 前缀白名单。
 * 前缀这条**不是**为了好看：它是「别把自己藏掉」的硬闸门 ——
 * NayePlmTools 自己的磁贴是 NAYE00001，恢复入口就在那块磁贴上。
 * v0.6.0.179 起这条规则只有一份实现（app_tile.c 的 app_tile_is_builtin）：
 * 它同时是路径穿越防护、写主机的闸门、以及换图标/背景那条链的准入条件，
 * 抄成两份的那天，某一侧就会放进来一个本来该拒的 tid。 */
static int av_tid_ok(const char *tid) {
    return app_tile_is_builtin(tid);
}

/* ── 面板改不动的那几条（v0.5.2.160 建名单，.161 按用户真机反馈收窄）─────────
 * 判据只有一条：**在真机上把 visible 写成 0，磁贴还在不在**。
 * 用户实测口径（2026-10-10）：「所有应用程序、欢迎 是可以编辑显示和隐藏的」——
 * `.160` 把这两条也关进名单是**错的**：`NPXS40056` 在快照里本来就是 `visible=1`
 * （主屏看得到它，正是因为没被隐藏，不是因为这列不管它）；`NPXS40063 欢迎` 出厂就是 0，
 * 我拿「出厂 0 却还在画」当证据，可那颗磁贴到底画不画根本没人核对过。
 * ⚠ 教训：**别拿出厂值反推显隐规则**，只认「写了 0、再看主屏」这一种实验。
 * 仍留在名单里的只有：两个内容保存库（写 0 之后主屏照旧画，用户连着两轮报的都是它），
 * 以及下面那两组本身就不是「可隐藏的软件磁贴」的系统条目。 */
typedef struct {
    const char *tid;
    const char *reason;
} AvFixed;

static const AvFixed AV_FIXED[] = {
    { "NPXS40071", "主屏固定入口：实测把它写成隐藏，主屏照旧画出来" },
    { "NPXS40139", "主屏固定入口：实测把它写成隐藏，主屏照旧画出来" },
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

/* 这条 tid 在不在「主机固定列表」里。ids 是一次读回来的并集（见 tile_group_pin_tids），
 * 不在循环里逐条开库：19 条各开一次既慢，那 19 次之间主机还可能变。 */
static int av_in_ids(const char (*ids)[TILE_GROUP_ID_MAX], int n, const char *tid) {
    for (int i = 0; i < n; i++) {
        if (strcmp(ids[i], tid) == 0) return 1;
    }
    return 0;
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
    size_t cap = 4096 + (size_t)count * (AV_NAME_MAX * 2 + 416);
    if (cap > size) cap = size;
    buf = malloc(cap);
    if (!buf) { if (apps) free(apps); return -1; }

    /* 主机固定列表读一次（跨账号并集）。分配失败时按「一条都没固定」处理：
     * 这个字段只决定一颗开关的初始位置，不该为它把整个弹窗打断 ——
     * 真正的「读不了库」由 error 字段报出来，一个布尔字段不承担两个含义。 */
    char (*pinned_ids)[TILE_GROUP_ID_MAX] = NULL;
    int npinned = 0;
    if (count > 0) {
        pinned_ids = malloc(sizeof(char) * TILE_GROUP_ID_MAX * AV_MAX_APPS);
        if (pinned_ids) npinned = tile_group_pin_tids(pinned_ids, AV_MAX_APPS);
        else pldmgr_log("[PLDMGR] app_vis: pinned 集合分配失败，按全未固定处理\n");
    }

    JsonListBuilder jb = { buf, cap, 0, 1 };
    json_append(&jb, "{\"apps\":[");
    for (int i = 0; i < count; i++) {
        char name_e[AV_NAME_MAX * 4 + 8];
        pldmgr_json_escape(apps[i].name, name_e, sizeof(name_e));
        int hidden = apps[i].hidden == 1;
        int pinned = av_in_ids(pinned_ids, npinned, apps[i].tid);
        /* 「面板里设过自定义图没有」。0 也要发：前端据此决定「恢复默认」能不能点，
         * 以及不去取一张不存在的图（省掉每行两次白跑的 404）。 */
        int cicon = app_tile_icon_ver(apps[i].tid) > 0 ? 1 : 0;
        int cbg = app_tile_bg_ver(apps[i].tid) > 0 ? 1 : 0;
        const char *why = NULL;
        if (app_vis_fixed_reason(apps[i].tid, &why)) {
            char why_e[512];
            pldmgr_json_escape(why, why_e, sizeof(why_e));
            if (json_append(&jb,
                            "%s{\"tid\":\"%s\",\"name\":\"%s\",\"hidden\":%s,"
                            "\"pinned\":%s,\"custom_icon\":%d,\"custom_bg\":%d,"
                            "\"controllable\":false,\"reason\":\"%s\"}",
                            i ? "," : "", apps[i].tid, name_e,
                            hidden ? "true" : "false",
                            pinned ? "true" : "false",
                            cicon, cbg, why_e) != 0)
                break;
            continue;
        }
        if (json_append(&jb,
                        "%s{\"tid\":\"%s\",\"name\":\"%s\",\"hidden\":%s,"
                        "\"pinned\":%s,\"custom_icon\":%d,\"custom_bg\":%d,"
                        "\"controllable\":true}",
                        i ? "," : "", apps[i].tid, name_e,
                        hidden ? "true" : "false",
                        pinned ? "true" : "false",
                        cicon, cbg) != 0)
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
    free(pinned_ids);
    if (apps) free(apps);
    return ok ? (int)n : -1;
}

/* ── 隐藏意图（开机对账）─────────────────────────────────────────────────────
 *
 * 存 `{"hidden":{"<tid>":true|false,...}}`。每一次写成功的 app_vis_set 都会记一笔；
 * 开机时 app_vis_sync_all 按这份记录把主机重新对齐（契约见 include/app_visibility.h）。
 *
 * 为什么需要它：主机自己会重写 tbl_iconinfo_<uid>（真机三次快照里账号表集合
 * 1 → 4 → 2 张，新出现的行默认 visible=1），而列表用跨账号「与」语义 ——
 * 「藏好的」于是自己翻回来（用户口径：「有些按钮关机开机后会被重置」）。
 * 用户要的是设置活下来，不是每次开机被告知它没了。
 *
 * 为什么不用「读到什么就显示什么」：那样面板显示的会长期与用户的选择不一致，
 * 而且失一次效就要重设一次 —— 那是把问题推给用户，不是修它。 */

static cJSON *av_wanted_load(void) {
    FILE *f = fopen(APP_VIS_WANTED_PATH, "rb");
    if (!f) return cJSON_CreateObject();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024) { fclose(f); return cJSON_CreateObject(); }
    char *b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return NULL; }
    size_t rd = fread(b, 1, (size_t)sz, f);
    fclose(f);
    b[rd] = 0;
    cJSON *root = cJSON_Parse(b);
    free(b);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return cJSON_CreateObject();
    }
    return root;
}

/* 原子写：先 .tmp 再 rename（与 app_pin.c 那份同一个手法）。 */
static int av_wanted_save(cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    if (!s) return -1;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", APP_VIS_WANTED_PATH);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(s); return -1; }
    size_t n = strlen(s);
    int ok = (fwrite(s, 1, n, f) == n) && (fclose(f) == 0);
    free(s);
    if (!ok) { unlink(tmp); return -1; }
    if (rename(tmp, APP_VIS_WANTED_PATH) != 0) { unlink(tmp); return -1; }
    return 0;
}

static int av_wanted_set(const char *tid, int hidden) {
    cJSON *root = av_wanted_load();
    if (!root) return -1;
    cJSON *h = cJSON_GetObjectItem(root, "hidden");
    if (!h || !cJSON_IsObject(h)) {
        h = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "hidden", h);
    }
    /* 删了再加：cJSON 的 Object 允许多个同名键并存，不删就会出现两个同 tid 的项。 */
    cJSON_DeleteItemFromObject(h, tid);
    cJSON_AddBoolToObject(h, tid, hidden ? 1 : 0);
    int rc = av_wanted_save(root);
    cJSON_Delete(root);
    return rc;
}

/* 主机现在这一条算藏还是算显：跨账号取「与」，与 app_vis_list_json 同一条口径
 * （只要还有一个账号看得见，就不算藏）。
 * 0/1 = 状态；-1 = 读不到（表/行都没有，或 prepare 失败）——
 * 读不到时调用方照旧写一遍：写不进去会如实回失败，**不能**因为「读不了」就跳过。 */
static int av_current_hidden(sqlite3 *db, char (*tables)[AV_IDENT_MAX], int ntables,
                             const char *tid) {
    int any = 0, all_hidden = 1;
    for (int i = 0; i < ntables; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "SELECT visible FROM %s WHERE titleId=?", tables[i]);
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) continue;
        sqlite3_bind_text(st, 1, tid, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            any = 1;
            if (sqlite3_column_int(st, 0) != 0) all_hidden = 0;
        }
        sqlite3_finalize(st);
    }
    if (!any) return -1;
    return all_hidden ? 1 : 0;
}

int app_vis_sync_all(char *msg, size_t msgsz, int *checked, int *applied, int *failed) {
    if (msg && msgsz) msg[0] = 0;
    if (checked) *checked = 0;
    if (applied) *applied = 0;
    if (failed) *failed = 0;

    cJSON *root = av_wanted_load();
    if (!root) {
        if (msg) snprintf(msg, msgsz, "读不了隐藏记录");
        return -1;
    }
    cJSON *h = cJSON_GetObjectItem(root, "hidden");
    if (!h || !cJSON_IsObject(h)) {
        cJSON_Delete(root);
        if (msg) snprintf(msg, msgsz, "还没有隐藏记录");
        return 0;
    }

    sqlite3 *db = av_open();
    char tables[AV_TABLE_MAX][AV_IDENT_MAX];
    int ntables = db ? av_iconinfo_tables(db, tables, AV_TABLE_MAX) : 0;

    int n_ck = 0, n_ap = 0, n_fa = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, h) {
        if (!it->string || !av_tid_ok(it->string)) continue;
        if (!cJSON_IsBool(it)) continue;
        if (app_vis_fixed_reason(it->string, NULL)) continue; /* 名单里的写不动，也从不记意图 */
        n_ck++;
        int want = cJSON_IsTrue(it) ? 1 : 0;
        int cur = db ? av_current_hidden(db, tables, ntables, it->string) : -1;
        if (cur == want) continue; /* 已经一致：一次写都不发（这条链每次开机都跑） */
        char m[192];
        int rc = app_vis_set(it->string, want, m, sizeof(m));
        if (rc == 0) {
            n_ap++;
        } else {
            n_fa++;
            pldmgr_log("[PLDMGR] app_vis: 对账 %s -> hidden=%d 失败（rc=%d）：%s\n",
                       it->string, want, rc, m[0] ? m : "见上一条");
        }
    }
    if (db) sqlite3_close(db);
    cJSON_Delete(root);

    if (checked) *checked = n_ck;
    if (applied) *applied = n_ap;
    if (failed) *failed = n_fa;
    if (msg) snprintf(msg, msgsz, "对账 %d 条：补写 %d、失败 %d", n_ck, n_ap, n_fa);
    return 0;
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
    /* 记下用户的意图（开机对账靠它）。写失败只记日志：主机已经生效了，
     * 这条记录是给「下次开机能自己补回来」用的，不该反过来把它当失败报给用户。 */
    if (av_wanted_set(tid, hidden ? 1 : 0) != 0)
        pldmgr_log("[PLDMGR] app_vis: %s 已写主机，但意图文件没写成"
                   "（下次开机会漏掉这条对账）\n", tid);
    av_err(msg, msgsz, "");
    return 0;
}
