#ifndef MEDIA_REMOTE_H
#define MEDIA_REMOTE_H

#include <stddef.h>

#include "save_remote.h" /* sr_cfg_t：媒体复用存档那一套远程配置结构 */

/* [CN-FORK] v0.6.0.177 多媒体的远程备份（设置页「多媒体存档配置」）。
 *
 * 与存档的远程同步（save_remote.c 的 sr_push / sr_pull）是**两条独立的线**：
 *   · 配置独立 —— MEDIA_ARCHIVE_PATH 这份 JSON 与 ARCHIVE_GLOBAL_PATH 那份互不相干，
 *     用户可以往不同的共享 / 目录推（用户口径：多媒体有自己的 SMB/FTP 位置）。
 *   · 传输复用 —— 底层那套 curl / libsmb2 选项、.tmp 改名的原子写、错误话术全在
 *     save_remote.c，这里只负责「挑哪些文件、记哪些传过、进度与取消」。
 *
 * 远端布局：<base>/<TID>/media/<rel>，rel = content_path 去掉 /av_contents/，
 * 即 photo/<生成器>/<TID>/<hex>/<文件> —— 见 save_remote.c 的 sr_push_media。
 *
 * 两条入口：
 *   ① 手动  —— POST /game_media_sync（多媒体管理页的「远程同步」按钮），
 *               走全局单槽任务（GJ_KIND_SYNC），进度 / 取消与打包同一套端点。
 *   ② 自动  —— mr_auto_kick：/game_media 列表拉完后若开了自动备份，
 *               后台把「没传过 / 变了」的文件增量传上去。**不占任务槽**
 *               （用户可能正在打包，两个都要能跑），自己拿一把进程内互斥。
 *
 * 增量的判据写在 MEDIA_BACKUP_STATE_DIR/<tid>.json：{rel → size}。
 * 媒体文件是**只增不改**的（截图 / 录屏落盘后不会再被写），所以
 * 「账本里有这条且大小一致」就等于「已经传过，不用再传」。 */

/* 读配置。0 = 读到且协议受支持；-1 = 没配置 / 读不了 / 协议不支持（err 给人话）。
 * *auto_on 回自动备份开关（配置里没有这个字段按 0 处理）。 */
int mr_load_cfg(sr_cfg_t *cfg, int *auto_on, char *err, size_t errsz);

/* POST /game_media_sync 的请求体入口：{tid, ids:[content_path,…]|null}。
 * ids 缺省 / null / 空数组 = 全量（增量：已传过的跳过）；
 * 指定了 ids = 只推这些，**并且一律强推**（用户点名的东西不看账本）。
 * 返回 0 = 已开工并写出 job_id；-1 = 已有任务在跑（msg 给人话，HTTP 侧回 409）；
 * -2 = 请求体不合法 / 远程没配（msg 给人话，HTTP 侧回 400）。 */
int mr_sync_start(const char *body, size_t len, long long *job_id,
                  char *msg, size_t msgsz);

/* /game_media 列表返回之后调用：开着自动备份就在后台做一次增量同步。
 * **非阻塞**（HTTP 线程不能站着等网络）；没配 / 没开 / 已经有同步在跑 → 直接返回。 */
void mr_auto_kick(const char *tid);

#endif
