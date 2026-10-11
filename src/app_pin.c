/* [CN-FORK] v0.5.2.164 应用置顶：把某个主机内置软件钉在主屏固定位置。
 *
 * 实现要点（与 app_vis 的对照）：
 *   · `app_vis_*` 操作的是 app.db 的 tbl_iconinfo.visible，那是系统对**显隐**的记录；
 *     本模块操作的是「本面板的置顶收藏」，与「系统可见性」是两件事 —— 写库走
 *     /data/nayeplmtools/app_pins.json，不动 app.db。
 *   · 写一次读一行：进程内缓存 map（只缓存 on/off 即可），不做 stale-check，
 *     因为列表接口（/system_apps）会触发 re-read。
 *
 * 接口契约（与 game_prefs_* 同档）：
 *   app_pin_list_json(char *out, size_t size) → int
 *     写出 { "pins": { "<tid>": true|false, ... } }，空文件 → {"pins":{}}。
 *   app_pin_set(const char *tid, int pinned, char *msg, size_t msgsz) → int
 *     0=成功，-1=失败（msg 写人话）。
 *
 * 名单限制：只接受 NPXS 前缀的内置软件 tid（与 app_vis 同口径，
 * 写错前缀 / 写非内置 = 拒绝，避免把任意 tid 写进 JSON 里）。 */

#include "app_visibility.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "pldmgr.h"
#include "utils.h"

#ifndef APP_PIN_PATH
#define APP_PIN_PATH "/data/nayeplmtools/app_pins.json"
#endif

#define APP_PIN_PREFIX "NPXS"

static int av_pin_tid_ok(const char *t) {
    if (!t) return 0;
    size_t n = strlen(t);
    if (n != 9) return 0;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)t[i])) return 0;
    return strncmp(t, APP_PIN_PREFIX, 4) == 0;
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

int app_pin_set(const char *tid, int pinned, char *msg, size_t msgsz) {
    if (msg && msgsz) msg[0] = 0;
    if (!av_pin_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "只能置顶主机内置软件");
        return -1;
    }
    cJSON *root = av_pin_load();
    if (!root) {
        if (msg) snprintf(msg, msgsz, "读取置顶列表失败");
        return -1;
    }
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
    if (rc != 0) {
        if (msg) snprintf(msg, msgsz, "写入置顶列表失败");
        return -1;
    }
    return 0;
}

/* 把 {pins:{...}} 序列化成字符串（前端 /system_apps 也用这份），空文件 → {"pins":{}}。 */
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

/* 取某 tid 的置顶状态（0/1）。未登记返回 0。给 app_vis_list_json 在拼每条 app 时调用。 */
int app_pin_get(const char *tid) {
    if (!av_pin_tid_ok(tid)) return 0;
    cJSON *root = av_pin_load();
    if (!root) return 0;
    cJSON *pins = cJSON_GetObjectItem(root, "pins");
    cJSON *v = pins ? cJSON_GetObjectItem(pins, tid) : NULL;
    int pinned = (v && cJSON_IsBool(v) && cJSON_IsTrue(v)) ? 1 : 0;
    cJSON_Delete(root);
    return pinned;
}