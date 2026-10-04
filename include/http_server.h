#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <microhttpd.h>

/* MHD request handler callback — dispatches all routes. */
enum MHD_Result http_on_request(void *cls, struct MHD_Connection *conn,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls);

/* [CN-FORK] v0.5.2.99 MHD_OPTION_NOTIFY_COMPLETED 回调，main.c 的 4 个
 * MHD_start_daemon 站点全部经 http_daemon_start() 挂上它。
 * 本工程的 UploadStatus/PostStatus 在路由内部就 free 并把 *con_cls 置 NULL，
 * 所以这里只服务账号板块的流式头像上传：浏览器中途关页面时回收其状态。
 * 幂等 —— 见 src/psaccounts/api.c 的 magic 标记。 */
void http_on_request_completed(void *cls, struct MHD_Connection *conn,
                               void **con_cls, enum MHD_RequestTerminationCode toe);

#endif
