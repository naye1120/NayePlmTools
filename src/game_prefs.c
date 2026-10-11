/* [CN-FORK] v0.5.2.164 游戏磁贴的收藏 / 置顶实现。契约见 include/game_prefs.h。 */

#include "game_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "pldmgr.h" /* pldmgr_log */

/* 文件上限：条目本身极小（一条 ~40 字节），64KB 够存一千多款游戏。 */
#define GP_FILE_MAX (64 * 1024)

/* 与 game_saves.c 的 gs_tid_ok / tile_db.c 的 td_tid_ok 同规格：6..16 位 A-Z0-9。
 * 这里**不**加前缀白名单（不像 app_vis 只认 NPXS）—— 游戏是用户内容，
 * 前缀有 PPSA / CUSA / EMUA / PAUS / PPJB / 自制 NAYE… 好几类，列白名单只会漏。 */
static int gp_tid_ok(const char *t) {
    if (!t) return 0;
    size_t n = strlen(t);
    if (n < 6 || n > 16) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = t[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

/* 读整份 JSON。文件不存在 / 空 / 坏掉一律回一个空对象（"没有偏好"是正常状态，
 * 不是错误 —— 报错会让第一次点收藏的用户看到一个吓人的提示）。 */
static cJSON *gp_load(void) {
    FILE *f = fopen(GAME_PREFS_PATH, "rb");
    if (!f) return cJSON_CreateObject();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > GP_FILE_MAX) { fclose(f); return cJSON_CreateObject(); }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return cJSON_CreateObject();
    }
    return root;
}

/* 原子写：先写 .tmp 再 rename，写一半崩了不至于留半截 JSON。 */
static int gp_save(cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    if (!s) return -1;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", GAME_PREFS_PATH);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(s); return -1; }
    size_t n = strlen(s);
    int ok = (fwrite(s, 1, n, f) == n) && (fclose(f) == 0);
    free(s);
    if (!ok) { unlink(tmp); return -1; }
    if (rename(tmp, GAME_PREFS_PATH) != 0) { unlink(tmp); return -1; }
    return 0;
}

int game_prefs_list_json(char *out, size_t size) {
    if (!out || size < 32) return -1;
    cJSON *root = gp_load();
    if (!root) { snprintf(out, size, "{\"prefs\":{}}"); return (int)strlen(out); }
    cJSON *wrap = cJSON_CreateObject();
    if (!wrap) { cJSON_Delete(root); return -1; }
    /* AddItemToObject 接管所有权：wrap 删掉时 root 一并释放。 */
    cJSON_AddItemToObject(wrap, "prefs", root);
    char *s = cJSON_PrintUnformatted(wrap);
    cJSON_Delete(wrap);
    if (!s) { snprintf(out, size, "{\"prefs\":{}}"); return (int)strlen(out); }
    size_t n = strlen(s);
    int ok = n < size;
    if (ok) memcpy(out, s, n + 1);
    free(s);
    if (!ok) { out[0] = 0; return -1; }
    return (int)n;
}

int game_prefs_set(const char *tid, int fav, int pin,
                   int *out_fav, int *out_pin, char *msg, size_t msgsz) {
    if (out_fav) *out_fav = 0;
    if (out_pin) *out_pin = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!gp_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "游戏编号不合法");
        return -1;
    }
    if (fav < 0 && pin < 0) {
        if (msg) snprintf(msg, msgsz, "没有要改的项（fav / pin 至少给一个）");
        return -1;
    }
    cJSON *root = gp_load();
    if (!root) {
        if (msg) snprintf(msg, msgsz, "读取游戏偏好失败");
        return -1;
    }

    cJSON *cur = cJSON_GetObjectItem(root, tid);
    /* 旧值：前端只拨一颗按钮时，另一项必须原样保留 —— 这正是三态入参存在的理由。 */
    int nf = (cur && cJSON_IsObject(cur) && cJSON_IsTrue(cJSON_GetObjectItem(cur, "fav"))) ? 1 : 0;
    int np = (cur && cJSON_IsObject(cur) && cJSON_IsTrue(cJSON_GetObjectItem(cur, "pin"))) ? 1 : 0;
    if (fav >= 0) nf = fav ? 1 : 0;
    if (pin >= 0) np = pin ? 1 : 0;

    cJSON_DeleteItemFromObject(root, tid);
    if (nf || np) {
        cJSON *o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(root); if (msg) snprintf(msg, msgsz, "内存不足"); return -1; }
        cJSON_AddBoolToObject(o, "fav", nf);
        cJSON_AddBoolToObject(o, "pin", np);
        cJSON_AddItemToObject(root, tid, o);
    }
    /* nf / np 都是 0 → 条目直接删掉（上面已经 DeleteItem），文件不会越攒越长。 */

    int rc = gp_save(root);
    cJSON_Delete(root);
    if (rc != 0) {
        if (msg) snprintf(msg, msgsz, "写入游戏偏好失败");
        return -1;
    }
    pldmgr_log("[PLDMGR] game_prefs: %s fav=%d pin=%d\n", tid, nf, np);
    if (out_fav) *out_fav = nf;
    if (out_pin) *out_pin = np;
    return 0;
}
