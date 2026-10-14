#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <microhttpd.h>

/* Bound accumulated JSON and streamed payloads independently. */
#define HTTP_POST_BODY_LIMIT ((size_t)1024 * 1024)
#define HTTP_UPLOAD_BODY_LIMIT ((size_t)512 * 1024 * 1024)

/* MHD request handler callback — dispatches all routes. */
enum MHD_Result http_on_request(void *cls, struct MHD_Connection *conn,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls);

/* Registered with MHD_OPTION_NOTIFY_COMPLETED for every daemon instance.
 * [CN-FORK] v0.6.0.176 本回调由上游 v0.6.0 的 http_request_completed 与 fork
 * v0.5.2.99 的 http_on_request_completed 合并而来：先按上游清理 RequestContext
 * （上传临时文件 / POST 缓冲），再转发给账号板块回收流式头像上传状态。 */
void http_request_completed(void *cls, struct MHD_Connection *conn,
                            void **con_cls, enum MHD_RequestTerminationCode reason);

#endif
