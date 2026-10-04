/* [CN-FORK] v0.5.2.99 PS5 Account Tools 源码并入：websrv.h 的最小替身。
 *
 * 原工程的 src/websrv.h 声明三样东西：监听循环 websrv_listen()、退出接口用的
 * pause/kill 机制、以及队列响应的 websrv_queue_response()。并入主 ELF 后前两者
 * 都不存在了（监听与生命周期由本服务的 http_server.c / main.c 负责），只有
 * avatar.c 仍在调 websrv_queue_response()，所以这里只留这一条声明。
 * 实现见 src/psaccounts/api.c —— 它就是原 websrv.c 里的那个同名函数，作用是把
 * 共享的 CORS 头（Access-Control-Allow-Origin: *）统一加到每个响应上。
 */

#pragma once

#include <microhttpd.h>

/* Queue a response with the shared CORS header applied. */
enum MHD_Result websrv_queue_response(struct MHD_Connection *conn,
                                      unsigned int status,
                                      struct MHD_Response *resp);
