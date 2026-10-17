/* [CN-FORK] v0.6.0.179 应用置顶：改成写**主机**的固定列表。
 *
 * ── 为什么改（`.164` 的坑）────────────────────────────────────────────────
 * `.164` 起这一栏写的是我们自己的 /data/nayeplmtools/app_pins.json，主机压根
 * 不知道有这回事 —— 用户看到的就是「置顶按了没反应」，重启之后更明显
 * （主屏顺序完全由主机决定）。`.166` 给游戏磁贴已经改成写 app.db 的
 * `tbl_group_<uid>`（groupName='MyHome'，`idx` 就是主屏位置），这一轮把内置软件
 * 也搬到同一条链上：`tile_group_set_pin()` 本来就只按 tid 写，内置软件与游戏的
 * 差别只有前缀，而真机取证里 MyHome 组内**本来就有** NPXS40037 / NPXS40047 /
 * NPXS40071 这些系统条目 —— 它们和用户自己固定的条目在同一张表里。
 *
 * ── app_pins.json 不删，但语义变了 ────────────────────────────────────────
 * 它退化成**面板这一侧的意图记录**：
 *   · 「现在固定没固定」以主机为准（app_pin_get → tile_group_has）——
 *     读主机才可能看见「用户在主机自己的界面里改过」；
 *   · 开机对账（app_pin_sync_all）靠它知道用户想固定哪几条：主机重扫、
 *     新账号表出现、用户在主屏上取消固定之后再重启……都能自己补回来。
 * 这是「关机开机后被重置」那一类问题的统一修法：**意图留在盘上，开机重放**。
 * 与 game_title.c 的 game_title_sync_all（改名重启后被 SMP 冲回原名）同一个套路。
 *
 * 名单限制：只接受 NPXS 前缀的内置软件 tid（与 app_vis 同口径），
 * 拒绝把任意 tid 写进主机的主屏固定列表。 */

#include "app_visibility.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_tile.h"
#include "cJSON.h"
#include "pldmgr.h"
#include "tile_group.h"
#include "utils.h"

#ifndef APP_PIN_PATH
#define APP_PIN_PATH "/data/nayeplmtools/app_pins.json"
#endif

/* 与 app_tile.c 的 app_tile_is_builtin 同一条规则（NPXS + A-Z0-9）。
 * 不另抄一份：这条规则同时是**路径穿越防护**与「能不能写主机」的闸门，
 * 两处漂开的那天，某一侧就会放进来一个本来该拒的 tid。 */
static int av_pin_tid_ok(const char *t) {
    if (!t) return 0;
    size_t n = strlen(t);
    if (n != 9) return 0;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)t[i])) return 0;
    return app_tile_is_builtin(t);
}

/* 读整份 JSON。若文件不存在/为空，回一份 {"pins":{}}。 */
static cJSON *av_pin_load(void) {
    FILE *f = fopen(APP_PIN_PATH, "rb");
    if (!f) return cJSON_CreateObject();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024) { fclose(f); return cJSON_CreateObject(); }
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

/* 把 cJSON 树原子写到文件：先写 .tmp，再 rename。写一半崩了不至于覆盖半截。 */
static int av_pin_save(cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    if (!s) return -1;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", APP_PIN_PATH);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(s); return -1; }
    size_t n = strlen(s);
    int ok = (fwrite(s, 1, n, f) == n) && (fclose(f) == 0);
    free(s);
    if (!ok) { unlink(tmp); return -1; }
    if (rename(tmp, APP_PIN_PATH) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* 意图文件里记一笔（`.164` 那个实现原样留着，只是不再当作状态源）。 */
static int av_pin_intent_set(const char *tid, int pinned) {
    cJSON *root = av_pin_load();
    if (!root) return -1;
    cJSON *pins = cJSON_GetObjectItem(root, "pins");
    if (!pins || !cJSON_IsObject(pins)) {
        pins = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "pins", pins);
    }
    /* 删除旧值再加新的（避免 Object 里同名键并存）。 */
    cJSON_DeleteItemFromObject(pins, tid);
    cJSON_AddBoolToObject(pins, tid, pinned ? 1 : 0);
    int rc = av_pin_save(root);
    cJSON_Delete(root);
    return rc;
}

int app_pin_set(const char *tid, int pinned, char *msg, size_t msgsz,
                int *out_accounts, int *out_idx) {
    if (out_accounts) *out_accounts = 0;
    if (out_idx) *out_idx = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!av_pin_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "只能置顶主机内置软件");
        return -1;
    }

    /* 先写主机。主机没写成**就不记意图** —— 意图是给「开机对账」用的，
     * 记了会让下一次开机偷偷把它补上，而用户当下看到的是失败，两边对不上账。 */
    int accounts = 0, idx = 0;
    int rc = tile_group_set_pin(tid, pinned, msg, msgsz, &accounts, &idx);
    if (rc != 0) {
        pldmgr_log("[PLDMGR] app_pin: %s 主机固定列表没写进去（rc=%d）：%s\n", tid, rc,
                   (msg && msg[0]) ? msg : "见上一条");
        return rc;
    }

    if (av_pin_intent_set(tid, pinned ? 1 : 0) != 0)
        pldmgr_log("[PLDMGR] app_pin: %s 主机已生效，但意图文件没写成"
                   "（下次开机会漏掉这条对账）\n", tid);
    /* 原样把主机算出来那两个数带出去：前端要凭它们说清「写进了几张账号表、
     * 现在排在第几位」。只回一句「成功」的话，用户在主屏上看不出变化也没法问为什么。 */
    if (out_accounts) *out_accounts = accounts;
    if (out_idx) *out_idx = idx;
    pldmgr_log("[PLDMGR] app_pin: %s pinned=%d accounts=%d idx=%d\n", tid,
               pinned ? 1 : 0, accounts, idx);
    return 0;
}

/* 取某 tid 的置顶状态（0/1）。**以主机为准**：面板自己那份文件只记意图，
 * 拿它当状态会出现「用户在主机上取消了固定，面板还显示已固定」。
 * 读不了主机（库被占 / 没有 tbl_group）一律回 0，那与「没固定」在界面上同形，
 * 而库读不了这件事由同一次请求里的别的字段负责报出来。 */
int app_pin_get(const char *tid) {
    if (!av_pin_tid_ok(tid)) return 0;
    return tile_group_has(tid);
}

/* 写下 {"pins":{"<tid>":true|false, ...}} —— 现在是**意图记录**的原文，
 * 不是「当前状态」。空文件 → {"pins":{}}。 */
int app_pin_list_json(char *out, size_t size) {
    if (!out || size < 16) return -1;
    cJSON *root = av_pin_load();
    if (!root) { snprintf(out, size, "{\"pins\":{}}"); return (int)strlen(out); }
    cJSON *pins = cJSON_GetObjectItem(root, "pins");
    if (!pins || !cJSON_IsObject(pins)) {
        cJSON_AddObjectToObject(root, "pins");
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) { snprintf(out, size, "{\"pins\":{}}"); return (int)strlen(out); }
    size_t n = strlen(s);
    int ok = n < size;
    if (ok) memcpy(out, s, n + 1);
    free(s);
    if (!ok) { snprintf(out, size, "{\"pins\":{}}"); return (int)strlen(out); }
    return (int)(ok ? n : strlen(out));
}

/* ── 开机对账 ────────────────────────────────────────────────────────────── */

/* 只重放 `true`（用户要固定）的那些：
 *   · 主机固定列表里已经有它 → 一次写都不发（这是每次开机都跑的链，幂等是硬要求）；
 *   · 没有 → 重新置顶。
 * `false` 的**一概不动**：主机的默认状态就是「不在列表里」，而「它现在在列表里」
 * 只可能是用户自己在主屏上固定的 —— 那种情况下把人家固定的东西挪走才是错的
 * （面板不该拿一份旧账去覆盖用户在主机上的新选择）。 */
int app_pin_sync_all(char *msg, size_t msgsz, int *checked, int *applied, int *failed) {
    if (msg && msgsz) msg[0] = 0;
    if (checked) *checked = 0;
    if (applied) *applied = 0;
    if (failed) *failed = 0;

    cJSON *root = av_pin_load();
    if (!root) {
        if (msg) snprintf(msg, msgsz, "读不了置顶记录");
        return -1;
    }
    cJSON *pins = cJSON_GetObjectItem(root, "pins");
    if (!pins || !cJSON_IsObject(pins)) {
        cJSON_Delete(root);
        if (msg) snprintf(msg, msgsz, "还没有置顶记录");
        return 0;
    }

    int n_ck = 0, n_ap = 0, n_fa = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, pins) {
        if (!it->string || !av_pin_tid_ok(it->string)) continue;
        if (!cJSON_IsTrue(it)) continue;
        n_ck++;
        if (tile_group_has(it->string)) continue; /* 主机上已经在固定列表里 */
        char m[192];
        int accounts = 0, idx = 0;
        if (tile_group_set_pin(it->string, 1, m, sizeof(m), &accounts, &idx) == 0) {
            n_ap++;
        } else {
            n_fa++;
            pldmgr_log("[PLDMGR] app_pin: 对账重新置顶 %s 失败：%s\n", it->string,
                       m[0] ? m : "见上一条");
        }
    }
    cJSON_Delete(root);

    if (checked) *checked = n_ck;
    if (applied) *applied = n_ap;
    if (failed) *failed = n_fa;
    if (msg) snprintf(msg, msgsz, "对账 %d 条：补写 %d、失败 %d", n_ck, n_ap, n_fa);
    return 0;
}
