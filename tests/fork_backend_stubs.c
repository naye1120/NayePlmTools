/* [CN-FORK] v0.6.0.177 本 fork 自有路由的宿主测试桩。
 *
 * 为什么会有这个文件：tests/http_backend_stubs.c 只把**上游**那几条路由接得住，
 * fork 后来加的游戏 / 多媒体 / PKG / 账号后端一个都没补 —— 于是
 * tests/run_http_tests.py 在 link 阶段就报几十个 undefined reference，
 * 「跑不了的测试」和「跑绿的测试」在报告里长得一样，405 那批 bug 因此一直没人发现。
 * （缺桩是这套测试长期红着的**唯一**原因，路由表、闸门本身都没问题。）
 *
 * 三条规矩，改这个文件之前先读：
 *   ① 桩**只做两件事**：写出一个合法 JSON / 返回一个能让 HTTP 层落到
 *      200 或 400 的返回码。绝不碰文件系统、绝不起线程 —— 测试是顺序跑的，
 *      桩里起了线程，跑失败时栈里就有别人的现场了。
 *   ② 「起任务」类的桩（打包 / 同步 / 删除游戏）一律**返回失败**，
 *      因为它们在真机上是起线程 + 占全局任务槽的；测试要验的是「方法闸门放行了
 *      POST、不是 405」，任务真跑起来反而把 run_request 的生命周期断言搞乱。
 *   ③ 原型一律照 include/ 里那张声明写，别凭印象改参数顺序 —— 编译期会报
 *      conflicting types，但那份报错不告诉你原声明长什么样。
 *
 * http_responses_test.c 的请求表里新增的 fork 路由（POST 有、GET 405）就是这
 * 份文件存在的目的：谁哪天又把某条 fork 路由从 is_post_route 里漏掉，那张表会立刻红。
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <microhttpd.h>

#include "app_visibility.h"
#include "config.h"
#include "fan_control.h"
#include "game_auto.h"
#include "game_media.h"
#include "game_mgr.h"
#include "game_music.h"
#include "game_prefs.h"
#include "game_runtime.h"
#include "game_saves.h"
#include "game_title.h"
#include "media_remote.h"
#include "pkgmanager.h"
#include "pldmgr.h"
#include "quicklinks.h"
#include "thermal.h"
#include "tile_group.h"
#include "psaccounts/api.h"

extern int test_json_failure; /* tests/http_backend_stubs.c */

static size_t j(char *out, size_t size, const char *value) {
    if (test_json_failure) { if (size) out[0] = 0; return 0; }
    int n = snprintf(out, size, "%s", value);
    return n < 0 ? 0 : (size_t)n;
}

/* ── 配置 ───────────────────────────────────────────────────────────────────
 * 两份远程位置配置都回一份**合法 JSON**：/get_config 是把这段原文嵌进响应里的，
 * 桩回空串会让整个响应变成非法 JSON，而那张表里 /get_config 断言的是 200。
 * --config-only 模式下 src/config.c 是真跑的（PLDMGR_TEST_REAL_CONFIG），
 * 这两个符号由它提供 —— 桩再给一份就是重复定义，所以按同一把条件编译闸住。 */
#ifndef PLDMGR_TEST_REAL_CONFIG
int config_read_archive_global(char *out, size_t osz) { return (int)j(out, osz, "{\"mode\":\"smb\",\"enabled\":false}"); }
int config_read_media_archive(char *out, size_t osz)  { return (int)j(out, osz, "{\"mode\":\"smb\",\"auto\":false}"); }
#endif

/* ── 温控 / 风扇 ───────────────────────────────────────────────────────── */
int thermal_stats_json(char *buf, size_t size) { return (int)j(buf, size, "{\"thermal\":{}}"); }
int fan_control_status_json(char *buf, size_t size) { return (int)j(buf, size, "{\"fan\":{\"enabled\":false}}"); }
int fan_control_apply_json(char *buf, size_t size, int target_c) {
    (void)target_c; return (int)j(buf, size, "{\"ok\":true}");
}
int fan_control_set_enabled_json(char *buf, size_t size, int enabled) {
    (void)enabled; return (int)j(buf, size, "{\"ok\":true}");
}

/* ── 个性化软件：显示 / 隐藏、置顶、组内排序 ─────────────────────────── */
int app_vis_list_json(char *out, size_t size) { return (int)j(out, size, "{\"apps\":[]}"); }
int app_vis_set(const char *tid, int hidden, char *msg, size_t msgsz) {
    (void)tid; (void)hidden; snprintf(msg, msgsz, "test stub"); return 0;
}
int app_pin_set(const char *tid, int pinned, char *msg, size_t msgsz) {
    (void)tid; (void)pinned; snprintf(msg, msgsz, "test stub"); return 0;
}
int tile_group_list_json(char *out, size_t size) { return (int)j(out, size, "{\"group\":[]}"); }
int tile_group_set_pin(const char *tid, int pinned, char *msg, size_t msgsz,
                       int *out_accounts, int *out_idx) {
    (void)tid; (void)pinned;
    snprintf(msg, msgsz, "test stub");
    if (out_accounts) *out_accounts = 1;
    if (out_idx) *out_idx = pinned ? 1 : 0;
    return 0;
}
int tile_group_reorder(const char *const *tids, int n, char *msg, size_t msgsz, int *out_moved) {
    (void)tids; (void)n; snprintf(msg, msgsz, "test stub");
    if (out_moved) *out_moved = 0;
    return 0;
}

/* ── 游戏列表 / 详情 / 磁贴 ────────────────────────────────────────────── */
int game_mgr_list_json(char *buf, size_t size, int force) {
    (void)force; return (int)j(buf, size, "{\"games\":[]}");
}
int game_mgr_detail_json(const char *tid, char *buf, size_t size) {
    (void)tid; return (int)j(buf, size, "{\"tid\":\"\"}");
}
/* 找不到路径 = -1 → 那几条 <img> / 下载路由回 404，与真机上「没设过」一致。 */
int game_mgr_icon_path(const char *tid, char *out, size_t osz) {
    (void)tid; if (osz) out[0] = 0; return -1;
}
int game_title_get_name(const char *tid, char *out, size_t osz) {
    (void)tid; return (int)j(out, osz, "\"\"");
}
int game_title_set_name(const char *tid, const char *name, char *msg, size_t msgsz,
                        int *persisted, char *pnote, size_t pnotesz,
                        int *host_ok, char *hnote, size_t hnotesz) {
    (void)tid; (void)name;
    snprintf(msg, msgsz, "test stub");
    if (persisted) *persisted = 0;
    if (pnote) snprintf(pnote, pnotesz, "test stub");
    if (host_ok) *host_ok = 0;
    if (hnote) snprintf(hnote, hnotesz, "test stub");
    return 0;
}
int game_title_sync_all(char *msg, size_t msgsz, int *scanned, int *applied,
                        int *persisted, int *failed) {
    snprintf(msg, msgsz, "test stub");
    if (scanned) *scanned = 0;
    if (applied) *applied = 0;
    if (persisted) *persisted = 0;
    if (failed) *failed = 0;
    return 0;
}
int game_title_icon_path(const char *tid, char *out, size_t osz) {
    (void)tid; if (osz) out[0] = 0; return -1;
}
int game_title_bg_png_path(const char *tid, char *out, size_t osz) {
    (void)tid; if (osz) out[0] = 0; return -1;
}
int game_title_bg_dds_path(const char *tid, char *out, size_t osz) {
    (void)tid; if (osz) out[0] = 0; return -1;
}
int game_title_set_icon(const char *tid, const unsigned char *png, size_t len,
                        long long *out_bytes, char *msg, size_t msgsz) {
    (void)tid; (void)png; (void)len;
    if (out_bytes) *out_bytes = 0;
    snprintf(msg, msgsz, "test stub");
    return 0;
}
int game_title_set_bg(const char *tid, const unsigned char *png, size_t len,
                      long long *out_bytes, long long *out_dds, char *msg, size_t msgsz) {
    (void)tid; (void)png; (void)len;
    if (out_bytes) *out_bytes = 0;
    if (out_dds) *out_dds = 0;
    snprintf(msg, msgsz, "test stub");
    return 0;
}
int game_title_reset_icon(const char *tid) { (void)tid; return 0; }
int game_title_reset_bg(const char *tid) { (void)tid; return 0; }

int game_prefs_list_json(char *out, size_t size) { return (int)j(out, size, "{\"games\":[]}"); }
int game_prefs_set(const char *tid, int fav, int pin, int *out_fav, int *out_pin,
                   char *msg, size_t msgsz) {
    (void)tid;
    if (out_fav) *out_fav = fav;
    if (out_pin) *out_pin = pin;
    snprintf(msg, msgsz, "test stub");
    return 0;
}
int game_prefs_set_order(const char *const *tids, int n) { (void)tids; (void)n; return 0; }

/* ── 运行时：删除 / 挂载 / 卸载 ───────────────────────────────────────────
 * 这四个在真机上都是**起任务**的，照文件头规矩②回失败（不占任务槽、不起线程）。
 * *status 是 HTTP 状态：回 400，与「请求体不合法」同一档。
 * ⚠ 返回值的含义按 game_runtime.h 的原话是「**0 = 响应已经写好**；
 *   -1 = buf 太小（调用方回 500）」—— 不是「成 / 败」。头一版把它当成了
 *   成败返回值回 -1，于是 curl 打过去是 500 + EIO「响应过长」，
 *   平白多一条查不出原因的报错。状态码归 *status，返回值只归 buf。 */
static int rt_reject(char *buf, size_t size, int *status) {
    (void)j(buf, size, "{\"error\":\"EINVAL\",\"message\":\"test stub\"}");
    if (status) *status = 400;
    return 0;
}
int game_runtime_delete(const char *body, size_t len, char *buf, size_t size, int *status) {
    (void)body; (void)len; return rt_reject(buf, size, status);
}
int game_runtime_mount(const char *body, size_t len, char *buf, size_t size, int *status) {
    (void)body; (void)len; return rt_reject(buf, size, status);
}
int game_runtime_unmount(const char *body, size_t len, char *buf, size_t size, int *status) {
    (void)body; (void)len; return rt_reject(buf, size, status);
}
int game_runtime_uninstall(const char *body, size_t len, char *buf, size_t size, int *status) {
    (void)body; (void)len; return rt_reject(buf, size, status);
}

/* ── 存档（列表读得、改写一律拒） ─────────────────────────────────────── */
int game_saves_list_json(const char *tid, char *buf, size_t size) {
    (void)tid; return (int)j(buf, size, "{\"saves\":[]}");
}
int game_saves_snap_ev_json(const char *tid, int max, char *buf, size_t size) {
    (void)tid; (void)max; return (int)j(buf, size, "{\"events\":[]}");
}
int game_saves_auto_set(const char *tid, const char *body, size_t len, char *msg, size_t msgsz) {
    (void)tid; (void)body; (void)len; snprintf(msg, msgsz, "test stub"); return -1;
}
int game_saves_test(const char *body, size_t len, char *buf, size_t size) {
    (void)body; (void)len; return (int)j(buf, size, "{\"ok\":true,\"latency_ms\":1}");
}
int game_saves_pack(const char *tid, const char *save, const char *out_path,
                    long long *out_files, char *msg, size_t msgsz) {
    (void)tid; (void)save; (void)out_path;
    if (out_files) *out_files = 0;
    snprintf(msg, msgsz, "test stub");
    return -1;
}
int game_saves_snap_pack(const char *tid, const char *uid, const char *ts, const char *out_path,
                         long long *out_files, char *msg, size_t msgsz) {
    (void)tid; (void)uid; (void)ts; (void)out_path;
    if (out_files) *out_files = 0;
    snprintf(msg, msgsz, "test stub");
    return -1;
}
int game_saves_remove(const char *tid, const char *save, char *msg, size_t msgsz) {
    (void)tid; (void)save; snprintf(msg, msgsz, "test stub"); return -1;
}
int game_saves_remote_info(const char *tid, const char *save, char *buf, size_t size) {
    (void)tid; (void)save; return (int)j(buf, size, "{\"remote\":null}");
}
int game_saves_remote_download(const char *tid, const char *save, const char *out_path,
                               char *msg, size_t msgsz) {
    (void)tid; (void)save; (void)out_path; snprintf(msg, msgsz, "test stub"); return -1;
}
int game_saves_remote_delete(const char *tid, const char *save, char *msg, size_t msgsz) {
    (void)tid; (void)save; snprintf(msg, msgsz, "test stub"); return -1;
}
int game_saves_upload(const char *tid, const char *save, const unsigned char *body, size_t len,
                      long long *out_files, long long *out_size, char *msg, size_t msgsz) {
    (void)tid; (void)save; (void)body; (void)len;
    if (out_files) *out_files = 0;
    if (out_size) *out_size = 0;
    snprintf(msg, msgsz, "test stub");
    return -1;
}
int game_saves_import_garlic(const char *tid, const char *save, const char *name,
                             const unsigned char *body, size_t len, long long *out_files,
                             char *msg, size_t msgsz) {
    (void)tid; (void)save; (void)name; (void)body; (void)len;
    if (out_files) *out_files = 0;
    snprintf(msg, msgsz, "test stub");
    return -1;
}
int game_saves_snap_action(const char *body, size_t len, char *buf, size_t size,
                           char *msg, size_t msgsz) {
    (void)body; (void)len;
    (void)j(buf, size, "{\"error\":\"EINVAL\",\"message\":\"test stub\"}");
    snprintf(msg, msgsz, "test stub");
    return -1;
}
int game_auto_sync_action(const char *body, size_t len, char *buf, size_t size,
                          char *msg, size_t msgsz) {
    (void)body; (void)len;
    (void)j(buf, size, "{\"ok\":true}");
    snprintf(msg, msgsz, "test stub");
    return 0;
}

/* ── 多媒体：列表读得，文件 / 打包 / 删除一律拒（见文件头规矩②） ─────── */
int game_media_list_json(const char *tid, char *buf, size_t size) {
    (void)tid; return (int)j(buf, size, "{\"items\":[],\"total_bytes\":0}");
}
int game_media_delete(const char *tid, const char *id, char *msg, size_t msgsz) {
    (void)tid; (void)id; snprintf(msg, msgsz, "test stub"); return -1;
}
int game_media_open(const char *id, int *out_fd, off_t *out_size, const char **mime) {
    (void)id; if (out_fd) *out_fd = -1; if (out_size) *out_size = 0;
    if (mime) *mime = NULL;
    return -1;
}
int game_media_thumb(const char *id, int want_w, unsigned char **out, size_t *out_len,
                     const char **mime) {
    (void)id; (void)want_w;
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (mime) *mime = NULL;
    return -1;
}
int game_media_download_name(const char *tid, const char *id, char *out, size_t osz) {
    (void)tid; (void)id; if (osz) out[0] = 0; return -1;
}
int game_media_pack_start_json(const char *body, size_t len, long long *job_id,
                               char *msg, size_t msgsz) {
    (void)body; (void)len;
    if (job_id) *job_id = 0;
    snprintf(msg, msgsz, "test stub");
    return -1;
}
int game_media_pack_status(long long job_id, char *buf, size_t size) {
    (void)job_id; return (int)j(buf, size, "{\"done\":1,\"ok\":0,\"active\":false}");
}
int game_media_pack_cancel(long long job_id) { (void)job_id; return -1; }
int game_media_pack_name(long long job_id, char *out, size_t osz) {
    (void)job_id; if (osz) out[0] = 0; return -1;
}
int game_media_pack_open(long long job_id, int *out_fd, off_t *out_size) {
    (void)job_id; if (out_fd) *out_fd = -1; if (out_size) *out_size = 0;
    return -1;
}

/* ── 远程备份（本 fork 新增） ───────────────────────────────────────────
 * mr_sync_start 回 -2 = 「请求体不合法」→ HTTP 侧 400；**不起线程**。
 * mr_auto_kick 空转：/game_media 成功返回后它会被调到，测试里没有远端可连，
 * 而且它是异步的（铁律②），空转正好让跑出来的结果可重复。 */
int mr_sync_start(const char *body, size_t len, long long *job_id, char *msg, size_t msgsz) {
    (void)body; (void)len;
    if (job_id) *job_id = 0;
    snprintf(msg, msgsz, "test stub");
    return -2;
}
void mr_auto_kick(const char *tid) { (void)tid; }

/* ── 磁贴 / 音乐 ──────────────────────────────────────────────────────── */
int game_music_path(const char *tid, char *tp, size_t tpsz, char *mime, size_t mimesz) {
    (void)tid; if (tpsz) tp[0] = 0; if (mimesz) mime[0] = 0; return -1;
}
void game_music_reset(const char *tid) { (void)tid; }
void game_music_reset_host(const char *tid) { (void)tid; }
int game_music_set(const char *tid, const char *name, const unsigned char *data, size_t len,
                   long long *bytes_out, char *msg, size_t msgsz) {
    (void)tid; (void)name; (void)data; (void)len;
    if (bytes_out) *bytes_out = 0;
    snprintf(msg, msgsz, "test stub");
    return 0;
}
int game_music_set_host(const char *tid, const unsigned char *at9, size_t len, const char *lufs,
                        long long *bytes_out, char *msg, size_t msgsz) {
    (void)tid; (void)at9; (void)len; (void)lufs;
    if (bytes_out) *bytes_out = 0;
    snprintf(msg, msgsz, "test stub");
    return 0;
}

/* ── PKG / 快捷方式 ───────────────────────────────────────────────────── */
void pkgmanager_status_json(char *out, size_t osz) { (void)j(out, osz, "{\"running\":false}"); }
int pkgmanager_launch(char *msg, size_t msz) { snprintf(msg, msz, "test stub"); return 0; }
int pkgmanager_stop(char *msg, size_t msz) { snprintf(msg, msz, "test stub"); return 0; }
int pkgmanager_commit_upload(const char *tmp_path, const char *filename_hint,
                             char *msg, size_t msz) {
    (void)tmp_path; (void)filename_hint; snprintf(msg, msz, "test stub"); return -1;
}
int quicklinks_list_json(char *buf, size_t size) { return (int)j(buf, size, "{\"links\":[]}"); }
int quicklinks_add(const char *name, const char *url, char *msg, size_t msz) {
    (void)name; (void)url; snprintf(msg, msz, "test stub"); return 0;
}
int quicklinks_remove(int index, char *msg, size_t msz) {
    (void)index; snprintf(msg, msz, "test stub"); return 0;
}

/* ── PS5 Account Tools：/api/ 那一组 ────────────────────────────────────────
 * psaccounts_wants 回 0 = 这一轮里没有一条 /api/ 路由属于它，http_server.c 会按
 * 普通路由处理（404）—— 这与**改闸门之前**的行为一致：那之前这仨符号根本没被链进来。
 * 下面两个只是让链接过，正常路径不会被调到。 */
int psaccounts_wants(const char *url) { (void)url; return 0; }
enum MHD_Result psaccounts_handle(struct MHD_Connection *conn, const char *url, const char *method,
                                  const char *upload_data, size_t *upload_data_size, void **con_cls) {
    (void)conn; (void)url; (void)method; (void)upload_data;
    (void)upload_data_size; (void)con_cls;
    return MHD_NO;
}
void psaccounts_conn_completed(void *con_cls) { (void)con_cls; }
