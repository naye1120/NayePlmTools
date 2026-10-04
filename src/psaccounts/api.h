/* [CN-FORK] v0.5.2.99 PS5 Account Tools 接口层（源码并入主 ELF 版）
 *
 * 这个板块不再是一个独立进程：账号功能的全部 C 逻辑已经搬进本 payload，
 * 接口也就跟着挂在本服务自己的端口（MENU_PORT 8911）上，路径保持它原来的
 * /api/… 形状不变，前端按原契约调用即可。
 *
 * 与它原生的 src/websrv.c 相比只有三处并入必需的差异：
 *   1. 不带监听循环、不带内嵌首页、不带 /api/exit（本进程的整体退出仍走 /shutdown）；
 *   2. 加了了一把串行锁：原工程跑在 MHD_USE_INTERNAL_POLLING_THREAD（单线程串行处理），
 *      而本服务是 MHD_USE_THREAD_PER_CONNECTION；safety.c 的备份账本与 npsignin.c 的
 *      预览计划都是无锁全局状态，远程游玩会话更是只允许一条，必须串行；
 *   3. 连接状态带 magic 头，供 MHD 的 request-completed 回调认出「是它的」再释放。
 */

#pragma once

#include <microhttpd.h>

/* 本接口层认领 /api/ 前缀（本仓库自身的路由全是扁平名，不占用该前缀）。 */
int psaccounts_wants(const char *url);

/* 处理一个已认领的请求：一定会 queue 响应并返回 MHD_YES（内部错误也一样回 JSON）。
 * 语义与 MHD 访问回调一致，*con_cls 由本模块自己分配/释放。 */
enum MHD_Result psaccounts_handle(struct MHD_Connection *conn, const char *url,
                                  const char *method, const char *upload_data,
                                  size_t *upload_data_size, void **con_cls);

/* 请求结束回调里调用：只释放带本模块 magic 的连接状态，其它连接状态一律不动。 */
void psaccounts_conn_completed(void *con_cls);
