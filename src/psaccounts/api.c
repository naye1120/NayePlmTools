/* [CN-FORK] v0.5.2.99 PS5 Account Tools 接口层实现，语义见 api.h。
 * 各 endpoint 函数体保持与源工程 src/websrv.c 逐行一致，只在最外层加了认领判断与串行锁。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <microhttpd.h>

#include "api.h"
#include "websrv.h"
#include "accounts.h"
#include "avatar.h"

/* 连接状态：magic 让 request-completed 回调认出这是本模块的状态，不误放别人的。 */
#define ACCT_CONN_MAGIC 0x4143544Fu /* "ACTO" */

typedef struct {
  uint32_t magic;
  void *avatar_upload_state;
} acct_conn_t;

/* 串行锁，原因见 api.h 第 2 条。 */
static pthread_mutex_t acct_api_mu = PTHREAD_MUTEX_INITIALIZER;

/* 原工程 websrv.c 的同名函数，逐行照搬：所有响应统一带 CORS（它原来就发 *）。
 * avatar.c 也直接调它，所以保留原名、不声明 static。见 websrv.h。 */
enum MHD_Result
websrv_queue_response(struct MHD_Connection *conn, unsigned int status,
                      struct MHD_Response *resp) {
  MHD_add_response_header(resp, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN,
                          "*");
  return MHD_queue_response(conn, status, resp);
}

/* Serve a malloc'd (freed by MHD) or persistent JSON body. */
static enum MHD_Result
serve_json_text(struct MHD_Connection *conn, unsigned int status,
                const char *body, size_t len, int must_free) {
  enum MHD_ResponseMemoryMode mode = must_free ? MHD_RESPMEM_MUST_FREE
                                               : MHD_RESPMEM_PERSISTENT;
  struct MHD_Response *resp = MHD_create_response_from_buffer(len,
                                (void*)body, mode);
  if(!resp) { if(must_free) free((void*)body); return MHD_NO; }
  MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE,
                          "application/json");
  enum MHD_Result ret = websrv_queue_response(conn, status, resp);
  MHD_destroy_response(resp);
  return ret;
}

static enum MHD_Result
serve_error(struct MHD_Connection *conn, unsigned int status, const char *msg) {
  char body[512];
  int n = snprintf(body, sizeof(body), "{\"ok\":false,\"message\":\"%s\"}", msg);
  if(n < 0) n = 0;
  return serve_json_text(conn, status, body, (size_t)n, 0);
}


/* ------------------------------------------------------------------ *
 * Accounts endpoints.                                                  *
 * ------------------------------------------------------------------ */

static enum MHD_Result
accounts_endpoint(struct MHD_Connection *conn, const char *url) {
  if(!strcmp(url, "/api/accounts")) {
    char *body = accounts_list_json();
    if(!body) return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
    return serve_json_text(conn, MHD_HTTP_OK, body, strlen(body), 1);
  }

  /* /api/accounts/<action>?user=<id>[&accountId=<hex>][&to=<name>][&confirm=online] */
  if(!strncmp(url, "/api/accounts/", 14)) {
    const char *action = url + 14;
    const char *user = MHD_lookup_connection_value(conn,
                          MHD_GET_ARGUMENT_KIND, "user");
    if(!user || !*user)
      return serve_error(conn, MHD_HTTP_BAD_REQUEST, "missing user");
    uint32_t uid = (uint32_t)strtoul(user, NULL, 0);

    /* 只读预览：把 signin 真会写的键清单算出来给界面看，一个字节都不写。 */
    if(!strcmp(action, "preview")) {
      char *body = accounts_preview_json(uid);
      if(!body) return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
      return serve_json_text(conn, MHD_HTTP_OK, body, strlen(body), 1);
    }

    /* Sign-in is the one action that hands the forged identity to NP
       (auth.dat + config.dat + the sign-in flags). Require an explicit
       handshake parameter so no click — or scripted call — can do it by
       accident; the UI asks for a typed confirmation before adding it. */
    if(!strcmp(action, "signin")) {
      const char *cf = MHD_lookup_connection_value(conn,
                             MHD_GET_ARGUMENT_KIND, "confirm");
      if(!cf || strcmp(cf, "online")) {
        static const char n[] =
          "{\"ok\":false,\"needConfirm\":\"online\","
          "\"message\":\"signin requires confirm=online: it writes auth.dat "
          "and config.dat, which the shell presents as a signed-in NP "
          "account. Restore first if you are not sure.\"}";
        return serve_json_text(conn, MHD_HTTP_CONFLICT, n, sizeof(n)-1, 0);
      }
    }

    const char *aid = MHD_lookup_connection_value(conn,
                          MHD_GET_ARGUMENT_KIND, "accountId");
    const char *to = MHD_lookup_connection_value(conn,
                          MHD_GET_ARGUMENT_KIND, "to");
    uint64_t custom_id = 0;
    if(aid && *aid)
      custom_id = (uint64_t)strtoull(aid, NULL, 0);
    char *body = accounts_action(action, uid, custom_id, to);
    if(!body) return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
    return serve_json_text(conn, MHD_HTTP_OK, body, strlen(body), 1);
  }

  return serve_error(conn, MHD_HTTP_NOT_FOUND, "no such endpoint");
}


/* ------------------------------------------------------------------ *
 * Safety endpoints: backup inventory + one-click restore.              *
 * ------------------------------------------------------------------ */

static enum MHD_Result
safety_endpoint(struct MHD_Connection *conn, const char *url) {
  const char *mode;
  char *body;

  if(!strcmp(url, "/api/safety"))                mode = "status";
  else if(!strcmp(url, "/api/safety/restore"))   mode = "restore";
  else return serve_error(conn, MHD_HTTP_NOT_FOUND, "no such endpoint");

  body = accounts_safety_json(mode);
  if(!body) return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
  return serve_json_text(conn, MHD_HTTP_OK, body, strlen(body), 1);
}


/* ------------------------------------------------------------------ *
 * 远程游玩配对 PIN（只读：调系统库现生成，不写注册表、不碰文件）        *
 * ------------------------------------------------------------------ */

static enum MHD_Result
pin_endpoint(struct MHD_Connection *conn) {
  char *body = accounts_pin_json();
  if(!body) return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
  return serve_json_text(conn, MHD_HTTP_OK, body, strlen(body), 1);
}

/* 出完码之后界面每秒问一次：配到哪一步了、还剩多少秒。 */
static enum MHD_Result
pin_status_endpoint(struct MHD_Connection *conn) {
  char *body = accounts_pin_status_json();
  if(!body) return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
  return serve_json_text(conn, MHD_HTTP_OK, body, strlen(body), 1);
}


/* ------------------------------------------------------------------ *
 * Dispatch.                                                            *
 * ------------------------------------------------------------------ */

int psaccounts_wants(const char *url) {
  return url && strncmp(url, "/api/", 5) == 0;
}

enum MHD_Result
psaccounts_handle(struct MHD_Connection *conn, const char *url,
                  const char *method, const char *upload_data,
                  size_t *upload_data_size, void **con_cls) {
  acct_conn_t *st = *con_cls;
  enum MHD_Result ret;

  if(strcmp(method, MHD_HTTP_METHOD_GET) &&
     strcmp(method, MHD_HTTP_METHOD_POST) &&
     strcmp(method, MHD_HTTP_METHOD_HEAD))
    return serve_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "method not allowed");

  pthread_mutex_lock(&acct_api_mu);

  if(!st) {
    st = *con_cls = calloc(1, sizeof(acct_conn_t));
    if(!st) {
      ret = serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
      goto done;
    }
    st->magic = ACCT_CONN_MAGIC;
    ret = MHD_YES; /* 首帧只是建状态，不回应（与 MHD 访问回调约定一致） */
    goto done;
  }

  /* 非本模块分配的连接状态（理论上不会走到这里）：不接管，免得把别人的内存搞坏 */
  if(st->magic != ACCT_CONN_MAGIC) {
    ret = serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "bad connection state");
    goto done;
  }

  /* Avatar upload is a streaming raw-body POST. */
  if(!strcmp(method, MHD_HTTP_METHOD_POST)) {
    if(!strcmp("/api/avatar/upload", url)) {
      ret = avatar_upload_request(conn, upload_data, upload_data_size,
                                  &st->avatar_upload_state);
      /* [CN-FORK] v0.5.2.109 判据必须是「avatar_upload_request 把 *state 置回了 NULL」，
       * 不能再用 `*upload_data_size == 0` —— 这是实机「点头像 → 一直显示上传中、
       * 不报错也不结束」的真根因。
       *
       * MHD 的调用序列是：① size=0 建状态 ② 后续若干帧带 body ③ 最后一帧 size=0 收尾。
       * 而 avatar_upload_request 只在**第 ③ 帧**（或中途出错答完）才把 *state 置 NULL；
       * 第 ① 帧虽然 size 也是 0，但它只是把文件打开、*state 仍指向新建的流式状态。
       * 旧代码只看 size==0 就 free 掉流式状态 + free(st) + *con_cls=NULL —— 于是第 ① 帧
       * 就把连接状态扔了，MHD 后续重新调用时又拿到 *con_cls==NULL、重新分配、
       * 又进这个分支、又不消费 body……请求永远走不到收尾那帧，浏览器就一直等。
       * 源工程（D:\offact-new/accounts/src/websrv.c）根本没有这个 free 块，
       * 流式状态一律交给 request-completed 回调回收 —— 现在与它对齐。 */
      if(st->avatar_upload_state == NULL) {
        free(st);
        *con_cls = NULL;
      }
      goto done;
    }
    if(*upload_data_size) { *upload_data_size = 0; ret = MHD_YES; goto done; }
  }

  /* API. 前缀判断与源工程 websrv.c 保持一致 */
  if(!strncmp("/api/accounts", url, 13))        ret = accounts_endpoint(conn, url);
  else if(!strncmp("/api/safety", url, 11))     ret = safety_endpoint(conn, url);
  else if(!strncmp("/api/avatar", url, 11))     ret = avatar_request(conn, url);
  else if(!strcmp(url, "/api/remoteplay/pin"))  ret = pin_endpoint(conn);
  else if(!strcmp(url, "/api/remoteplay/pin/status")) ret = pin_status_endpoint(conn);
  else ret = serve_error(conn, MHD_HTTP_NOT_FOUND, "not found");

  /* 非流式请求到这里就已经答完了；状态留在原地，由完成回调统一回收 */

done:
  pthread_mutex_unlock(&acct_api_mu);
  return ret;
}

/* [CN-FORK] 浏览器中途断开时（头像上传传一半关页面）回收流式缓冲。
 * 只认 magic 匹配的自家状态；本仓库原有的 UploadStatus/PostStatus 一律不动。 */
void psaccounts_conn_completed(void *con_cls) {
  acct_conn_t *st = con_cls;
  if(!st || st->magic != ACCT_CONN_MAGIC) return;
  pthread_mutex_lock(&acct_api_mu);
  if(st->avatar_upload_state) avatar_upload_free(st->avatar_upload_state);
  pthread_mutex_unlock(&acct_api_mu);
  free(st);
}
