#ifndef GAME_RUNTIME_H
#define GAME_RUNTIME_H

/* [CN-FORK] v0.5.2.116 游戏管理 → 运行时层（删除源 / 挂载 / 卸载 / 卸载安装）
 *
 * 契约定死在 frontend/src/utils/gameApi.js 文件头：
 *
 *   POST /game_delete   {tid, confirm:true}
 *        202 {job_id, scan_queued}              异步任务，进度走 /game_job_status
 *        200 {source_missing:true, scan_queued} 源已经不在了（**不算失败**，前端据此关窗）
 *        409 {error:"EBUSY", reason, message}   游戏正在运行 / 已有别的任务在跑
 *        403 {error:"EPERM", message}           已安装 PKG 没有可删的物理源
 *        404 {error:"ENOENT", message}          主机上找不到这个游戏
 *        400 {error:"EINVAL", message}          没带 confirm
 *   POST /game_mount    {tid, mode:"ro"|"rw"}  → {ok, mode}
 *   POST /game_unmount  {tid}                  → {ok}
 *   POST /game_uninstall {tid}                 → {ok, scan_queued}
 *
 * 三个函数都**自己拼完整响应体**，并通过 *status 回一个纯 HTTP 状态码
 * （200/202/400/403/404/409/500/503）。用纯数字而不是 MHD_HTTP_* 是刻意的：
 * 本模块不该知道 HTTP 库的存在，host 测试也才能在没有 microhttpd 的机器上跑。
 * 路由那边数字与 MHD_HTTP_* 是同一套取值，直接传即可。
 *
 * ── 挂载与卸载是**代理**，不是自己实现 ────────────────────────────────────
 * 挂载/卸载是 ShadowMountPlus 的活（它掌握镜像、PFS、/mnt/shadowmnt 那一套）。
 * 面板只把请求转给 127.0.0.1:10101，不碰任何挂载逻辑 —— 两套实现去挂同一个镜像，
 * 结果一定是互相踩。它没起来时回 EUNAVAILABLE 并告诉用户去哪儿把它拉起来，
 * 而不是回一个含糊的 500。
 *
 * ── 删除删的是「物理源」，不是安装 ────────────────────────────────────────
 * 已安装的 PKG 没有可管的物理源 → 403（要删用「卸载」）。
 * 删除前有一道**路径白名单**：镜像必须是注册过的 .ffpkg/.exfat/.ffpfs/.ffpfsc，
 * 目录必须带 sce_sys/param.sfo 且落在扫描根下或目录名就是 titleId。
 * 宁可 403 也不 rm -rf 一条拼错的路径 —— 面板拿不到 undo。
 */

#include <stddef.h>

/* ShadowMount+ 的 HTTP 根。端口取自上游 docs/api.md v1.7。 */
#ifndef GR_SMP_BASE
#define GR_SMP_BASE "http://127.0.0.1:10101"
#endif

/* body 是 POST 的原始 JSON。写响应体到 buf（NUL 结尾），*status 回 HTTP 状态码。
 * 返回 0 = 已经把响应写好了；-1 = buf 太小（调用方回 500）。 */
int game_runtime_delete(const char *body, size_t len, char *buf, size_t size, int *status);
int game_runtime_mount(const char *body, size_t len, char *buf, size_t size, int *status);
int game_runtime_unmount(const char *body, size_t len, char *buf, size_t size, int *status);
int game_runtime_uninstall(const char *body, size_t len, char *buf, size_t size, int *status);

#endif
