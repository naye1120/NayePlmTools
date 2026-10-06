/*
 * HTTP Server — Route dispatch for all API and asset endpoints.
 *
 * Contains the MHD on_request callback and all route-handling logic
 * previously embedded in main.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>  /* [CN-FORK] open() for /game_icon */
#include <unistd.h> /* [CN-FORK] close() for /game_icon */
#include <stdint.h> /* [CN-FORK] v0.5.2.115 MHD_create_response_from_fd64(uint64_t) */
#include <sys/stat.h>
#include <microhttpd.h>

#include "http_server.h"
#include "pldmgr.h"
#include "log_server.h"
#include "config.h"
#include "payload_mgr.h"
#include "repository.h"
#include "sha256.h"
#include "sources.h"
#include "history_mgr.h"
#include "process_mgr.h"
#include "ps5_launcher.h"
/* [CN-FORK] v0.5.1.64 主页硬件信息功能整体删除：stats.c / stats.h 已从构建中移除，
 * 温度/风扇/内存/显存/存储/FPS/当前游戏 这些内核探测不再存在（整机死机的首选收敛） */
#include "pkgmanager.h" /* [CN-FORK] PKG Manager 内置集成 */
#include "quicklinks.h" /* [CN-FORK] 「常用入口」自定义快捷访问 */
#include "thermal.h"    /* [CN-FORK] 温控只读端点 /thermal_stats */
#include "game_mgr.h"   /* [CN-FORK] 游戏列表 /game_list + 图标 /game_icon */
#include "game_media.h" /* [CN-FORK] v0.5.2.115 多媒体管理 /game_media*（六条 + 任务两条） */
#include "fan_control.h" /* [CN-FORK] 风扇温控（模型 A）/fan_control */
/* [CN-FORK] v0.5.2.99 PS5 Account Tools 源码并入主 ELF：账号/头像/安全还原/远程游玩
 * 配对接口认领 /api/ 前缀，其余路由仍全部归本文件 */
#include "psaccounts/api.h"

#include "assets_index_html.h"
#include "assets_cache_appcache.h"
#include "assets_favicon_svg.h"
#include "assets_icon_png.h"

#define RESPONSE_BUFFER_SIZE 1048576
#define CORS_ORIGIN "*"

/* Shared flag — set from main() shutdown route, read by main loop */
volatile int http_keep_running = 1;

/* ── Helpers ───────────────────────────────────────────────── */

static void add_cors_headers(struct MHD_Response *resp) {
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", CORS_ORIGIN);
}

static struct MHD_Response *alloc_response_buffer(char **out) {
    *out = malloc(RESPONSE_BUFFER_SIZE);
    if (*out)
        return NULL;
    static const char oom[] = "{\"error\":\"Out of memory\"}";
    struct MHD_Response *resp = MHD_create_response_from_buffer(
        sizeof(oom) - 1, (void *)oom, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    add_cors_headers(resp);
    return resp;
}

/* [CN-FORK] v0.5.2.115 小 JSON 直接按栈上内容回（MUST_COPY 会先拷再交给 MHD，
 * 所以传局部数组是安全的）。统一加 Content-Type + CORS，免得每条路由各写一遍漏掉一个。 */
static enum MHD_Result queue_json(struct MHD_Connection *conn, int status,
                                  const char *json, size_t len) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(
        len, (void *)json, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    add_cors_headers(resp);
    enum MHD_Result ret = MHD_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

/* 统一的 JSON 错误回包：{error, message}。
 * message 走 pldmgr_json_escape —— 出错文案里带 strerror 时会有引号和反斜杠，
 * 直接 snprintf 进 JSON 会造出一个前端 .json() 解析不了的响应体。 */
static enum MHD_Result queue_json_error(struct MHD_Connection *conn, int status,
                                        const char *code, const char *msg) {
    char esc[480];
    char buf[560];
    pldmgr_json_escape(msg ? msg : "", esc, sizeof(esc));
    int n = snprintf(buf, sizeof(buf), "{\"error\":\"%s\",\"message\":\"%s\"}", code, esc);
    if (n < 0 || (size_t)n >= sizeof(buf)) n = 0;
    return queue_json(conn, status, buf, (size_t)n);
}

static int has_supported_extension(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;
    return strcasecmp(ext, ".elf") == 0 || strcasecmp(ext, ".bin") == 0;
}

static int is_safe_filename(const char *name) {
    if (!name || name[0] == '\0') return 0;
    if (strstr(name, "/") || strstr(name, "..")) return 0;
    return 1;
}

static int is_allowed_payload_path(const char *path) {
    if (!path || path[0] == '\0') return 0;
    if (strstr(path, "..")) return 0;
    if (!has_supported_extension(path)) return 0;

    for (int i = 0; i < SCAN_DIRS_COUNT; i++) {
        size_t len = strlen(SCAN_DIRS[i]);
        if (strncmp(path, SCAN_DIRS[i], len) == 0 &&
            (path[len] == '/' || path[len] == '\0')) {
            return 1;
        }
    }

    /* Allow USB root payloads if SCAN_USB_PAYLOADS is enabled in config */
    if (strncmp(path, "/mnt/usb", 8) == 0 &&
        path[8] >= '0' && path[8] <= '7' &&
        path[9] == '/') {
        if (config_read_bool("SCAN_USB_PAYLOADS", 0)) {
            return 1;
        }
    }

    return 0;
}

/* ── POST request state ────────────────────────────────────── */

struct PostStatus {
    char *data;
    size_t size;
    int error;
};

struct UploadStatus {
    FILE *fp;
    size_t total_size;
    int error;
    char filename[256];
    char temp_path[512];
    char repo_url[512];
};

/* ── Helper: Noisy routes ──────────────────────────────────── */

/* Filter out endpoints that the frontend polls automatically to reduce log spam */
static int is_noisy_route(const char *url) {
    if (strcmp(url, ROUTE_LOG) == 0) return 1;
    if (strcmp(url, ROUTE_INDEX) == 0) return 1;
    if (strcmp(url, ROUTE_INDEX_HTML) == 0) return 1;
    if (strcmp(url, "/favicon.svg") == 0) return 1;
    if (strcmp(url, "/icon.png") == 0) return 1;
    if (strcmp(url, ROUTE_CACHE_MANIFEST) == 0) return 1;
    if (strcmp(url, ROUTE_GETIP) == 0) return 1;
    if (strcmp(url, ROUTE_AUTOLOAD_STATUS) == 0) return 1;
    if (strcmp(url, ROUTE_VERSION) == 0) return 1;
    if (strcmp(url, ROUTE_LIST_PAYLOADS) == 0) return 1;
    if (strcmp(url, ROUTE_GET_CONFIG) == 0) return 1;
    if (strcmp(url, ROUTE_REPO_LIST) == 0) return 1;
    if (strcmp(url, ROUTE_THERMAL) == 0) return 1;
    if (strcmp(url, ROUTE_GAME_LIST) == 0) return 1; /* [CN-FORK] 游戏板块拉取，降噪 */
    if (strcmp(url, ROUTE_GAME_ICON) == 0) return 1;
    /* [CN-FORK] v0.5.2.115 缩略图一次开面板要打几十张、任务进度 1s 一次，这两条降噪；
     * /game_media（列表）与 /game_media_pack_get（下载）一次一发，留着记日志。 */
    if (strcmp(url, ROUTE_GAME_MEDIA_THUMB) == 0) return 1;
    if (strcmp(url, ROUTE_GAME_JOB_STATUS) == 0) return 1;
    if (strcmp(url, ROUTE_FAN_CONTROL) == 0) return 1; /* [CN-FORK] 风扇温控页加载/应用，降噪 */
    if (strcmp(url, ROUTE_PROCESSES_LIST) == 0) return 1;
    if (strcmp(url, ROUTE_HISTORY_LIST) == 0) return 1;
    /* [CN-FORK] v0.5.1.64 删 /system_stats 轮询（该端点已随硬件信息功能一起下线） */
    if (strcmp(url, ROUTE_PKG_STATUS) == 0) return 1; /* [CN-FORK] PKG安装页轮询，降噪 */
    if (strcmp(url, "/events") == 0) return 1;
    /* [CN-FORK] v0.5.2.99 远程游玩配对的进度查询，界面每 1 秒问一次，降噪；
     * 出码/配对成功/失败/超时那几条仍会记日志（是别的 URL）。 */
    if (strcmp(url, "/api/remoteplay/pin/status") == 0) return 1;
    return 0;
}

/* ── Main request handler ──────────────────────────────────── */

enum MHD_Result http_on_request(void *cls, struct MHD_Connection *conn,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {

    /* Handle CORS Preflight (OPTIONS) */
    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *resp =
            MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        add_cors_headers(resp);
        MHD_add_response_header(resp, "Access-Control-Allow-Methods",
                                "GET, POST, OPTIONS");
        MHD_add_response_header(resp, "Access-Control-Allow-Headers",
                                "Content-Type");
        enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    /* Set flag that we received a request (meaning browser is open) */
    log_server_set_active();

    /* ── [CN-FORK] v0.5.2.99 账号板块接口：整段认领给 psaccounts，路径 /api/… ──
     * 必须放在下面的 *con_cls == NULL 注册块之前：那个块末尾会给所有其它请求
     * 打 *con_cls = (void *)1 的占位标记，而 psaccounts 把 *con_cls 当作自己的
     * 状态结构解引用 —— 一旦被打了标记，这里读 magic 就是访问地址 1，直接崩。
     * 认领之后本请求不再走下面任何链，*con_cls 由 psaccounts 自己分配/回收。 */
    if (psaccounts_wants(url)) {
        if (!is_noisy_route(url)) {
            pldmgr_log("[PLDMGR] Request: %s %s\n", method, url);
        }
        return psaccounts_handle(conn, url, method, upload_data,
                                 upload_data_size, con_cls);
    }

    /* ── Initial call for a new request ────────────────────── */
    if (*con_cls == NULL) {
        if (strncmp(url, ROUTE_UPLOAD, strlen(ROUTE_UPLOAD)) == 0) {
            struct UploadStatus *status = malloc(sizeof(struct UploadStatus));
            status->fp = NULL;
            status->total_size = 0;
            status->error = 0;

            const char *filename =
                MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
            if (filename && !strstr(filename, "/") && !strstr(filename, "..")) {
                char path[512];
                mkdir(BASE_DATA_DIR, 0777);
                mkdir(PAYLOADS_STORAGE_DIR, 0777);
                snprintf(path, sizeof(path), "%s/%s.tmp", PAYLOADS_STORAGE_DIR, filename);

                strncpy(status->filename, filename, sizeof(status->filename) - 1);
                status->filename[sizeof(status->filename) - 1] = '\0';
                strncpy(status->temp_path, path, sizeof(status->temp_path) - 1);
                status->temp_path[sizeof(status->temp_path) - 1] = '\0';

                status->fp = fopen(path, "wb");
                if (!status->fp) {
                    pldmgr_log("[PLDMGR] !!! FAILED to open temp file: %s\n", path);
                    status->error = 1;
                } else {
                    pldmgr_log("[PLDMGR] Starting upload to temp: %s\n", path);
                }
            } else {
                pldmgr_log("[PLDMGR] !!! Upload failed: Missing filename parameter\n");
                status->error = 1;
            }
            *con_cls = status;
            return MHD_YES;
        }

        /* [CN-FORK] /pkgmanager_upload：手动上传新版 PKG Manager ELF，裸 body 流式写 .tmp */
        if (strncmp(url, ROUTE_PKG_UPLOAD, strlen(ROUTE_PKG_UPLOAD)) == 0 &&
            strcmp(method, "POST") == 0) {
            struct UploadStatus *status = malloc(sizeof(struct UploadStatus));
            status->fp = NULL;
            status->total_size = 0;
            status->error = 0;
            status->repo_url[0] = '\0';
            /* filename 仅用于猜版本号（上传新版时二进制里未必有签名串），取 basename、不参与拼路径 */
            const char *pkg_fname =
                MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
            if (pkg_fname) {
                const char *base = strrchr(pkg_fname, '/');
                base = base ? base + 1 : pkg_fname;
                strncpy(status->filename, base, sizeof(status->filename) - 1);
                status->filename[sizeof(status->filename) - 1] = '\0';
            } else {
                status->filename[0] = '\0';
            }
            strncpy(status->temp_path, PKGMGR_TMP_PATH, sizeof(status->temp_path) - 1);
            status->temp_path[sizeof(status->temp_path) - 1] = '\0';
            mkdir(BASE_DATA_DIR, 0777);
            mkdir(PKGMGR_DIR, 0777);
            status->fp = fopen(PKGMGR_TMP_PATH, "wb");
            if (!status->fp) {
                pldmgr_log("[PLDMGR] !!! PKG Manager upload: cannot open %s\n", PKGMGR_TMP_PATH);
                status->error = 1;
            } else {
                pldmgr_log("[PLDMGR] Starting PKG Manager upload to: %s\n", PKGMGR_TMP_PATH);
            }
            *con_cls = status;
            return MHD_YES;
        }

        if (strcmp(url, ROUTE_SET_CONFIG) == 0 && strcmp(method, "POST") == 0) {
            struct PostStatus *status = malloc(sizeof(struct PostStatus));
            status->data = NULL;
            status->size = 0;
            status->error = 0;
            *con_cls = status;
            return MHD_YES;
        }

        if (strcmp(url, ROUTE_REPO_PUSH) == 0 && strcmp(method, "POST") == 0) {
            struct PostStatus *status = malloc(sizeof(struct PostStatus));
            status->data = NULL;
            status->size = 0;
            status->error = 0;
            *con_cls = status;
            return MHD_YES;
        }

        if (strcmp(url, ROUTE_SOURCES_SET) == 0 && strcmp(method, "POST") == 0) {
            struct PostStatus *status = malloc(sizeof(struct PostStatus));
            status->data = NULL;
            status->size = 0;
            status->error = 0;
            *con_cls = status;
            return MHD_YES;
        }

        /* [CN-FORK] v0.5.2.115 打包任务：{tid, ids:[…]|null} 的 JSON 体 */
        if (strcmp(url, ROUTE_GAME_MEDIA_PACK) == 0 && strcmp(method, "POST") == 0) {
            struct PostStatus *status = malloc(sizeof(struct PostStatus));
            if (!status) return MHD_NO;
            status->data = NULL;
            status->size = 0;
            status->error = 0;
            *con_cls = status;
            return MHD_YES;
        }

        if (strncmp(url, ROUTE_REPO_INSTALL_PUSH,
                    strlen(ROUTE_REPO_INSTALL_PUSH)) == 0 &&
            strcmp(method, "POST") == 0) {
            struct UploadStatus *status = malloc(sizeof(struct UploadStatus));
            status->fp = NULL;
            status->total_size = 0;
            status->error = 0;
            const char *filename =
                MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
            if (filename && !strstr(filename, "/") && !strstr(filename, "..")) {
                char path[512];
                mkdir(BASE_DATA_DIR, 0777);
                mkdir(PAYLOADS_STORAGE_DIR, 0777);
                snprintf(path, sizeof(path), "%s/%s.part", PAYLOADS_STORAGE_DIR, filename);
                strncpy(status->filename, filename, sizeof(status->filename) - 1);
                status->filename[sizeof(status->filename) - 1] = '\0';
                strncpy(status->temp_path, path, sizeof(status->temp_path) - 1);
                status->temp_path[sizeof(status->temp_path) - 1] = '\0';

                const char *repo_url = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "repo_url");
                if (repo_url) {
                    strncpy(status->repo_url, repo_url, sizeof(status->repo_url) - 1);
                    status->repo_url[sizeof(status->repo_url) - 1] = '\0';
                } else {
                    strncpy(status->repo_url, REPOSITORY_SOURCE_URL, sizeof(status->repo_url) - 1);
                    status->repo_url[sizeof(status->repo_url) - 1] = '\0';
                }

                status->fp = fopen(path, "wb");
                if (!status->fp) {
                    pldmgr_log("[PLDMGR] !!! Failed to open for install: %s\n", path);
                    status->error = 1;
                } else {
                    pldmgr_log("[PLDMGR] Installing payload: %s\n", path);
                }
            } else {
                pldmgr_log("[PLDMGR] !!! install_push: invalid filename\n");
                status->error = 1;
            }
            *con_cls = status;
            return MHD_YES;
        }

        *con_cls = (void *)1;
        return MHD_YES;
    }

    /* ── Handle POST data for /set_config ──────────────────── */
    if (strcmp(url, ROUTE_SET_CONFIG) == 0 && strcmp(method, "POST") == 0) {
        struct PostStatus *status = (struct PostStatus *)*con_cls;
        if (*upload_data_size != 0) {
            char *new_data = realloc(status->data, status->size + *upload_data_size + 1);
            if (!new_data) {
                status->error = 1;
            } else {
                status->data = new_data;
                memcpy(status->data + status->size, upload_data, *upload_data_size);
                status->size += *upload_data_size;
                status->data[status->size] = '\0';
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            /* Finished receiving JSON */
            pldmgr_log("[PLDMGR] Received config update: %s\n",
                       status->data ? status->data : "(null)");

            if (status->data && !status->error) {
                config_handle_set_json(status->data);
            }

            if (status->data)
                free(status->data);
            free(status);
            *con_cls = NULL;

            struct MHD_Response *resp = MHD_create_response_from_buffer(
                strlen(MSG_OK), (void *)MSG_OK, MHD_RESPMEM_MUST_COPY);
            add_cors_headers(resp);
            enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
            MHD_destroy_response(resp);
            return ret;
        }
    }

    /* ── Handle POST data for /repository_push ─────────────── */
    if (strcmp(url, ROUTE_REPO_PUSH) == 0 && strcmp(method, "POST") == 0) {
        struct PostStatus *status = (struct PostStatus *)*con_cls;
        if (*upload_data_size != 0) {
            char *nd = realloc(status->data, status->size + *upload_data_size + 1);
            if (!nd) {
                status->error = 1;
            } else {
                status->data = nd;
                memcpy(status->data + status->size, upload_data, *upload_data_size);
                status->size += *upload_data_size;
                status->data[status->size] = '\0';
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            int ok = -1;
            if (status->data && !status->error)
                ok = repository_push_json(status->data, status->size);
            if (status->data)
                free(status->data);
            free(status);
            *con_cls = NULL;

            /* Return the current list JSON so the frontend refreshes in one trip */
            char *resp_buf;
            struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
            if (oom_resp) {
                enum MHD_Result ret2 = MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
                MHD_destroy_response(oom_resp);
                return ret2;
            }

            size_t len = repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 0);
            struct MHD_Response *resp = MHD_create_response_from_buffer(
                len, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            enum MHD_Result ret2 = MHD_queue_response(
                conn, ok == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
            MHD_destroy_response(resp);
            return ret2;
        }
    }

    /* ── Handle POST data for /sources_set ─────────────────── */
    if (strcmp(url, ROUTE_SOURCES_SET) == 0 && strcmp(method, "POST") == 0) {
        struct PostStatus *status = (struct PostStatus *)*con_cls;
        if (*upload_data_size != 0) {
            char *nd = realloc(status->data, status->size + *upload_data_size + 1);
            if (!nd) {
                status->error = 1;
            } else {
                status->data = nd;
                memcpy(status->data + status->size, upload_data, *upload_data_size);
                status->size += *upload_data_size;
                status->data[status->size] = '\0';
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            int ok = -1;
            if (status->data && !status->error)
                ok = sources_save(status->data, status->size);
            if (status->data)
                free(status->data);
            free(status);
            *con_cls = NULL;

            const char *msg = ok == 0 ? MSG_OK : "Failed to save sources";
            struct MHD_Response *resp_ss = MHD_create_response_from_buffer(
                strlen(msg), (void *)msg, MHD_RESPMEM_MUST_COPY);
            add_cors_headers(resp_ss);
            enum MHD_Result ret_ss = MHD_queue_response(
                conn, ok == 0 ? MHD_HTTP_OK : MHD_HTTP_INTERNAL_SERVER_ERROR, resp_ss);
            MHD_destroy_response(resp_ss);
            return ret_ss;
        }
    }

    /* ── [CN-FORK] v0.5.2.115 Handle POST data for /game_media_pack ──
     * 请求体 {tid, ids:[…]|null} → 202 语义上的 {job_id}。前端随后 1s 一次打
     * /game_job_status?job= 拿进度，跑完再 GET /game_media_pack_get?job= 取成品。 */
    if (strcmp(url, ROUTE_GAME_MEDIA_PACK) == 0 && strcmp(method, "POST") == 0) {
        struct PostStatus *status = (struct PostStatus *)*con_cls;
        if (*upload_data_size != 0) {
            /* 请求体最多 512KB：每个 id 约 60 字节，够写 8000 个了 */
            if (status->size + *upload_data_size > (512u << 10)) {
                status->error = 1;
            } else {
                char *nd = realloc(status->data, status->size + *upload_data_size + 1);
                if (!nd) {
                    status->error = 1;
                } else {
                    status->data = nd;
                    memcpy(status->data + status->size, upload_data, *upload_data_size);
                    status->size += *upload_data_size;
                    status->data[status->size] = '\0';
                }
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            long long job_id = 0;
            char msg[192];
            int rc = -2;
            msg[0] = 0;
            if (status->error) {
                snprintf(msg, sizeof(msg), "请求体过大或内存不足");
            } else if (status->data) {
                rc = game_media_pack_start_json(status->data, status->size, &job_id,
                                                msg, sizeof(msg));
            } else {
                snprintf(msg, sizeof(msg), "请求体为空");
            }
            if (status->data) free(status->data);
            free(status);
            *con_cls = NULL;

            char emsg[400];
            pldmgr_json_escape(msg, emsg, sizeof(emsg));
            char out[600];
            int on = snprintf(out, sizeof(out), "{\"job_id\":%lld,\"error\":\"%s\","
                               "\"message\":\"%s\"}",
                               job_id, rc == 0 ? "" : (rc == -1 ? "EBUSY" : "EBAD"), emsg);
            if (on < 0 || (size_t)on >= sizeof(out)) on = 0;

            struct MHD_Response *resp = MHD_create_response_from_buffer(
                (size_t)on, (void *)out, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            enum MHD_Result ret = MHD_queue_response(
                conn,
                rc == 0 ? MHD_HTTP_OK : (rc == -1 ? MHD_HTTP_CONFLICT : MHD_HTTP_BAD_REQUEST),
                resp);
            MHD_destroy_response(resp);
            return ret;
        }
    }

    /* ── Chunked upload for /manage:upload ─────────────────── */
    if (strncmp(url, ROUTE_UPLOAD, strlen(ROUTE_UPLOAD)) == 0) {
        struct UploadStatus *status = (struct UploadStatus *)*con_cls;
        if (*upload_data_size != 0) {
            if (status->fp && !status->error) {
                size_t written = fwrite(upload_data, 1, *upload_data_size, status->fp);
                if (written != *upload_data_size) {
                    pldmgr_log("[PLDMGR] !!! Write error: expected %zu, got %zu\n",
                               *upload_data_size, written);
                    status->error = 1;
                }
                status->total_size += written;
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            /* Upload finished */
            if (status->fp) {
                fflush(status->fp);
                fclose(status->fp);
                status->fp = NULL;

                if (!status->error) {
                    char msg[256];
                    if (payload_mgr_import_to_storage(status->filename, status->temp_path, "web_upload", "", msg, sizeof(msg)) != 0) {
                        pldmgr_log("[PLDMGR] !!! Failed to commit upload: %s\n", msg);
                        status->error = 1;
                    }
                }
            }
            pldmgr_log("[PLDMGR] Upload finished. Total bytes: %zu, Error: %d\n",
                       status->total_size, status->error);

            int err = status->error;
            free(status);
            *con_cls = NULL;

            const char *msg = err ? "Error during upload\n" : MSG_OK;
            struct MHD_Response *resp = MHD_create_response_from_buffer(
                strlen(msg), (void *)msg, MHD_RESPMEM_MUST_COPY);
            add_cors_headers(resp);
            enum MHD_Result ret = MHD_queue_response(
                conn, err ? MHD_HTTP_INTERNAL_SERVER_ERROR : MHD_HTTP_OK, resp);
            MHD_destroy_response(resp);
            return ret;
        }
    }

    /* ── [CN-FORK] Chunked upload for /pkgmanager_upload ─────── */
    if (strncmp(url, ROUTE_PKG_UPLOAD, strlen(ROUTE_PKG_UPLOAD)) == 0 && *con_cls != NULL) {
        struct UploadStatus *status = (struct UploadStatus *)*con_cls;
        if (*upload_data_size != 0) {
            if (status->fp && !status->error) {
                size_t written = fwrite(upload_data, 1, *upload_data_size, status->fp);
                if (written != *upload_data_size)
                    status->error = 1;
                status->total_size += written;
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            if (status->fp) {
                fflush(status->fp);
                fclose(status->fp);
                status->fp = NULL;
            }
            int err = status->error;
            char result[256] = "";
            if (!err) {
                if (pkgmanager_commit_upload(status->temp_path, status->filename, result, sizeof(result)) != 0)
                    err = 1;
            } else {
                remove(status->temp_path);
                snprintf(result, sizeof(result), "write error during upload");
            }
            pldmgr_log("[PLDMGR] PKG Manager upload finished: %zu bytes, err=%d\n",
                       status->total_size, err);
            free(status);
            *con_cls = NULL;

            char json[640];
            char esc[512];
            pldmgr_json_escape(result, esc, sizeof(esc));
            if (err)
                snprintf(json, sizeof(json), "{\"ok\":false,\"message\":\"%s\"}", esc);
            else
                snprintf(json, sizeof(json), "{\"ok\":true,\"version\":\"%s\"}", esc);
            struct MHD_Response *resp_up = MHD_create_response_from_buffer(
                strlen(json), (void *)json, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp_up, "Content-Type", "application/json");
            add_cors_headers(resp_up);
            enum MHD_Result ret_up = MHD_queue_response(
                conn, err ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_OK, resp_up);
            MHD_destroy_response(resp_up);
            return ret_up;
        }
    }

    /* ── Chunked upload for /repository_install_push ────────── */
    if (strncmp(url, ROUTE_REPO_INSTALL_PUSH, strlen(ROUTE_REPO_INSTALL_PUSH)) == 0) {
        struct UploadStatus *status = (struct UploadStatus *)*con_cls;
        if (*upload_data_size != 0) {
            if (status->fp && !status->error) {
                size_t written = fwrite(upload_data, 1, *upload_data_size, status->fp);
                if (written != *upload_data_size) {
                    status->error = 1;
                }
                status->total_size += written;
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            if (status->fp) {
                fflush(status->fp);
                fclose(status->fp);
            }
            int err = status->error;

            char msg_buf[1024] = "";
            if (!err) {
                if (repository_install_commit(
                        status->filename, status->temp_path, "repository", status->repo_url, msg_buf,
                        sizeof(msg_buf)) != 0) {
                    err = 1;
                }
            }

            pldmgr_log("[PLDMGR] Payload install %s (%zu bytes)\n",
                       err ? "FAILED" : "complete", status->total_size);
            free(status);
            *con_cls = NULL;
            if (err && msg_buf[0] == '\0') {
                snprintf(msg_buf, sizeof(msg_buf), "Write error");
            }
            char json_resp[1024];
            snprintf(json_resp, sizeof(json_resp), "{\"ok\":%s,\"message\":\"%s\"}",
                     err ? "false" : "true", err ? msg_buf : "Installed");
            struct MHD_Response *resp2 = MHD_create_response_from_buffer(
                strlen(json_resp), (void *)json_resp, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp2, "Content-Type", "application/json");
            add_cors_headers(resp2);
            enum MHD_Result ret2 = MHD_queue_response(
                conn, err ? MHD_HTTP_INTERNAL_SERVER_ERROR : MHD_HTTP_OK, resp2);
            MHD_destroy_response(resp2);
            return ret2;
        }
    }

    /* ── Log significant requests ──────────────────────────── */
    if (!is_noisy_route(url)) {
        pldmgr_log("[PLDMGR] Request: %s %s\n", method, url);
    }

    struct MHD_Response *resp = NULL;
    enum MHD_Result ret;
    int http_status = MHD_HTTP_OK;

    /* ── USB routes ────────────────────────────────────────── */
    if (strcmp(url, ROUTE_USB_MOVE_CHECK) == 0) {
        const char *path = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "path");
        if (path) {
            char *resp_buf;
            struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
            if (oom_resp) {
                ret = MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
                MHD_destroy_response(oom_resp);
                return ret;
            }
            int rc = payload_mgr_usb_check(path, resp_buf, RESPONSE_BUFFER_SIZE);
            resp = MHD_create_response_from_buffer(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            ret = MHD_queue_response(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
            MHD_destroy_response(resp);
            return ret;
        } else {
            const char *err = "{\"error\":\"Missing path\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
        }
        add_cors_headers(resp);
        ret = MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    if (strcmp(url, ROUTE_USB_MOVE_PERFORM) == 0) {
        const char *path = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "path");
        const char *overwrite_str = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "overwrite");
        int overwrite = (overwrite_str && strcmp(overwrite_str, "true") == 0) ? 1 : 0;
        const char *keep_original_str = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "keep_original");
        int keep_original = (keep_original_str && strcmp(keep_original_str, "true") == 0) ? 1 : 0;
        if (path) {
            char *resp_buf;
            struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
            if (oom_resp) {
                ret = MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
                MHD_destroy_response(oom_resp);
                return ret;
            }
            int rc = payload_mgr_usb_move(path, overwrite, keep_original, resp_buf, RESPONSE_BUFFER_SIZE);
            resp = MHD_create_response_from_buffer(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            ret = MHD_queue_response(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
            MHD_destroy_response(resp);
            return ret;
        } else {
            const char *err = "{\"error\":\"Missing path\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
        }
        add_cors_headers(resp);
        ret = MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    /* ── Static assets ─────────────────────────────────────── */
    if (strcmp(url, ROUTE_INDEX) == 0 || strcmp(url, ROUTE_INDEX_HTML) == 0) {
        resp = MHD_create_response_from_buffer(assets_index_html_len,
                                               (void *)assets_index_html,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/html");
        MHD_add_response_header(resp, "Cache-Control", "no-cache, must-revalidate");
    } else if (strcmp(url, ROUTE_CACHE_MANIFEST) == 0) {
        resp = MHD_create_response_from_buffer(assets_cache_appcache_len,
                                               (void *)assets_cache_appcache,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/cache-manifest");
        MHD_add_response_header(resp, "Cache-Control", "no-cache, must-revalidate");
    } else if (strcmp(url, "/favicon.svg") == 0) {
        resp = MHD_create_response_from_buffer(assets_favicon_svg_len,
                                               (void *)assets_favicon_svg,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "image/svg+xml");
        MHD_add_response_header(resp, "Cache-Control", "no-cache, must-revalidate"); /* [CN-FORK] 图标随版本即时刷新，不吃 7 天缓存 */
    } else if (strcmp(url, "/icon.png") == 0) {
        resp = MHD_create_response_from_buffer(assets_icon_png_len,
                                               (void *)assets_icon_png,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "image/png");
        MHD_add_response_header(resp, "Cache-Control", "no-cache, must-revalidate"); /* [CN-FORK] 图标随版本即时刷新，不吃 7 天缓存 */

    /* ── API routes ────────────────────────────────────────── */
    } else if (strcmp(url, ROUTE_CHECK) == 0) {
        const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
        if (!filename) {
            const char *err = "{\"error\":\"Missing filename\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        if (!is_safe_filename(filename)) {
            const char *err = "{\"error\":\"Invalid filename\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        payload_mgr_check_existing(filename, resp_buf, RESPONSE_BUFFER_SIZE);
        resp = MHD_create_response_from_buffer(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        add_cors_headers(resp);
        return MHD_queue_response(conn, MHD_HTTP_OK, resp);
    } else if (strcmp(url, ROUTE_LIST_PAYLOADS) == 0) {
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        size_t len = payload_mgr_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        resp = MHD_create_response_from_buffer(len, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_PROCESSES_LIST) == 0) {
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        size_t len = process_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        resp = MHD_create_response_from_buffer(len, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_HISTORY_LIST) == 0) {
        char *json = malloc(8192);
        if (json) {
            size_t len = history_mgr_to_json(json, 8192);
            resp = MHD_create_response_from_buffer(len, (void *)json, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_OK, resp);
        } else {
            const char *err = "{\"error\":\"Memory allocation failed\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
        }
    } else if (strcmp(url, ROUTE_PROCESS_KILL) == 0) {
        const char *pid_str = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "pid");
        if (!pid_str) {
            const char *err = "{\"ok\":false,\"message\":\"Missing pid\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        int pid = atoi(pid_str);
        int rc = process_kill(pid);
        
        pldmgr_log("[PLDMGR] Process kill requested for PID %d: %s\n", pid, rc == 0 ? "SUCCESS" : "FAILED");
        
        char json_resp[256];
        snprintf(json_resp, sizeof(json_resp), "{\"ok\":%s,\"message\":\"%s\"}",
                 rc == 0 ? "true" : "false", rc == 0 ? "Killed" : "Failed to kill");
        resp = MHD_create_response_from_buffer(strlen(json_resp), (void *)json_resp, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        add_cors_headers(resp);
        return MHD_queue_response(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    } else if (strcmp(url, ROUTE_REPO_LIST) == 0) {
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        PldmgrConfig cfg;
        config_read(&cfg);
        size_t len;
        if (cfg.multi_sources_enabled)
            len = sources_multi_repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 0);
        else
            len = repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 0);
        resp = MHD_create_response_from_buffer(len, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_REPO_REFRESH) == 0) {
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        PldmgrConfig cfg;
        config_read(&cfg);
        size_t len2;
        if (cfg.multi_sources_enabled)
            len2 = sources_multi_repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 1);
        else
            len2 = repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 1);
        resp = MHD_create_response_from_buffer(len2, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_REPO_INSTALL) == 0) {
        const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
        const char *repo_url = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "repo_url");
        const char *source_id = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "source_id");
        if (!filename) {
            const char *err = "{\"ok\":false,\"message\":\"Missing filename\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        if (!is_safe_filename(filename)) {
            const char *err = "{\"ok\":false,\"message\":\"Invalid filename\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }

        char msg_buf[1024] = "";
        int rc;
        if (source_id && source_id[0]) {
            rc = sources_multi_repository_install(filename, source_id,
                                                  repo_url ? repo_url : "",
                                                  msg_buf, sizeof(msg_buf));
        } else {
            const char *detail = (repo_url && repo_url[0]) ? repo_url : REPOSITORY_SOURCE_URL;
            rc = repository_install_download(filename, detail, msg_buf, sizeof(msg_buf));
        }
        if (msg_buf[0] == '\0') {
            snprintf(msg_buf, sizeof(msg_buf), rc == 0 ? "Installed" : "Install failed");
        }

        char json_resp[1024];
        snprintf(json_resp, sizeof(json_resp), "{\"ok\":%s,\"message\":\"%s\"}",
                 rc == 0 ? "true" : "false", msg_buf);
        resp = MHD_create_response_from_buffer(strlen(json_resp), (void *)json_resp, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        add_cors_headers(resp);
        return MHD_queue_response(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    } else if (strcmp(url, ROUTE_SOURCES_LIST) == 0) {
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        sources_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        resp = MHD_create_response_from_buffer(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_SOURCES_ADD) == 0) {
        const char *src_url = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "url");
        if (!src_url || !src_url[0]) {
            const char *err = "{\"ok\":false,\"message\":\"Missing url parameter\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        char msg_buf_src[512] = "";
        int rc_src = sources_add(src_url, msg_buf_src, sizeof(msg_buf_src));
        char json_resp_src[1024];
        if (rc_src == 0) {
            char name_escaped[512];
            pldmgr_json_escape(msg_buf_src, name_escaped, sizeof(name_escaped));
            snprintf(json_resp_src, sizeof(json_resp_src),
                     "{\"ok\":true,\"name\":\"%s\"}", name_escaped);
        } else {
            char msg_escaped[512];
            pldmgr_json_escape(msg_buf_src, msg_escaped, sizeof(msg_escaped));
            snprintf(json_resp_src, sizeof(json_resp_src),
                     "{\"ok\":false,\"message\":\"%s\"}", msg_escaped);
        }
        resp = MHD_create_response_from_buffer(strlen(json_resp_src), (void *)json_resp_src, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        add_cors_headers(resp);
        return MHD_queue_response(conn, rc_src == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
    } else if (strcmp(url, ROUTE_SOURCES_REMOVE) == 0) {
        const char *idx_str = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "index");
        if (!idx_str) {
            const char *err = "{\"ok\":false,\"message\":\"Missing index parameter\"}";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Content-Type", "application/json");
            add_cors_headers(resp);
            return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        int idx = atoi(idx_str);
        char msg_rm[256] = "";
        int rc_rm = sources_remove(idx, msg_rm, sizeof(msg_rm));
        char json_rm[512];
        char msg_rm_e[256];
        pldmgr_json_escape(msg_rm, msg_rm_e, sizeof(msg_rm_e));
        snprintf(json_rm, sizeof(json_rm), "{\"ok\":%s,\"message\":\"%s\"}",
                 rc_rm == 0 ? "true" : "false", msg_rm_e);
        resp = MHD_create_response_from_buffer(strlen(json_rm), (void *)json_rm, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        add_cors_headers(resp);
        return MHD_queue_response(conn, rc_rm == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
    } else if (strncmp(url, ROUTE_LOAD_PAYLOAD, strlen(ROUTE_LOAD_PAYLOAD)) == 0) {
        const char *path = url + strlen(ROUTE_LOAD_PAYLOAD);
        const char *final_path = NULL;
        char resolved_path[512];
        if (is_allowed_payload_path(path)) {
            final_path = path;
        } else {
            const char *filename = strrchr(path, '/');
            filename = filename ? filename + 1 : path;
            if (is_safe_filename(filename) &&
                payload_mgr_resolve_path(filename, resolved_path,
                                         sizeof(resolved_path)) == 0) {
                final_path = resolved_path;
            }
        }

        if (!final_path) {
            pldmgr_log("[PLDMGR] !!! Payload path rejected: %s\n", path);
            const char *err = "Invalid payload name\n";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err,
                                                   MHD_RESPMEM_PERSISTENT);
            http_status = MHD_HTTP_BAD_REQUEST;
        } else if (ps5_launch_elf(final_path) == 0) {
            resp = MHD_create_response_from_buffer(strlen(MSG_OK), (void *)MSG_OK,
                                                   MHD_RESPMEM_PERSISTENT);
        } else {
            const char *err = "Failed to launch payload\n";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err,
                                                   MHD_RESPMEM_PERSISTENT);
            http_status = MHD_HTTP_INTERNAL_SERVER_ERROR;
        }
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strncmp(url, ROUTE_DELETE, strlen(ROUTE_DELETE)) == 0) {
        const char *filename =
            MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
        if (filename) {
            if (strstr(filename, "/") || strstr(filename, "..")) {
                const char *err = "Invalid filename\n";
                resp = MHD_create_response_from_buffer(strlen(err), (void *)err,
                                                       MHD_RESPMEM_PERSISTENT);
            } else {
                if (payload_mgr_delete_payload_file(filename) == 0) {
                    pldmgr_log("[PLDMGR] Deleted payload: %s\n", filename);
                    resp = MHD_create_response_from_buffer(strlen(MSG_OK), (void *)MSG_OK,
                                                           MHD_RESPMEM_PERSISTENT);
                } else {
                    const char *err = "Failed to delete file\n";
                    resp = MHD_create_response_from_buffer(strlen(err), (void *)err,
                                                           MHD_RESPMEM_PERSISTENT);
                }
            }
        } else {
            const char *err = "Missing filename\n";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err,
                                                   MHD_RESPMEM_PERSISTENT);
        }
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_SHUTDOWN) == 0) {
        const char *msg = "NayePlmTools Core shutting down...\n"; /* [CN-FORK] 对外名称 */
        pldmgr_log("[PLDMGR] %s", msg);
        resp = MHD_create_response_from_buffer(strlen(msg), (void *)msg,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
        http_keep_running = 0;
    } else if (strcmp(url, ROUTE_LOG) == 0) {
        char *resp_buf;
        resp = alloc_response_buffer(&resp_buf);
        if (!resp) {
            size_t pos = log_build_json(resp_buf, RESPONSE_BUFFER_SIZE);
            resp = MHD_create_response_from_buffer(pos, (void *)resp_buf,
                                                   MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
        }
    } else if (strcmp(url, ROUTE_VERSION) == 0) {
        resp = MHD_create_response_from_buffer(
            strlen(MENU_VERSION), (void *)MENU_VERSION, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_GETIP) == 0) {
        char ip[64];
        if (pldmgr_get_local_ip(ip, sizeof(ip)) != 0) {
            strcpy(ip, "0.0.0.0");
        }
        resp = MHD_create_response_from_buffer(strlen(ip), (void *)ip,
                                               MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_AUTOLOAD_STATUS) == 0) {
        int total, done;
        char current[128];
        pldmgr_autoload_get_status(&total, &done, current);
        int remaining = pldmgr_autoload_get_remaining_seconds();
        long long remaining_ms = pldmgr_autoload_get_remaining_ms();

        char list_buf[4096] = "";
        FILE *f = fopen(AUTOLOAD_CONFIG_PATH, "r");
        if (f) {
            char line[256];
            int first = 1;
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                if (strlen(line) == 0 || line[0] == '!')
                    continue;
                if (!first) strcat(list_buf, ",");
                strcat(list_buf, line);
                first = 0;
            }
            fclose(f);
        }

        PldmgrConfig cfg;
        config_read(&cfg);

        char current_escaped[256];
        char list_escaped[8192];
        pldmgr_json_escape(current, current_escaped, sizeof(current_escaped));
        pldmgr_json_escape(list_buf, list_escaped, sizeof(list_escaped));

        char *resp_buf;
        resp = alloc_response_buffer(&resp_buf);
        if (!resp) {
            snprintf(resp_buf, RESPONSE_BUFFER_SIZE,
                     "{\"remaining\":%d,\"remaining_ms\":%lld,\"total\":%d,\"done\":%d,\"current\":\"%s\","
                     "\"list\":\"%s\",\"delay\":%d,\"KILL_DISC_PLAYER_ON_STARTUP\":%s,\"SCAN_USB_PAYLOADS\":%s}",
                     remaining, remaining_ms, total, done, current_escaped, list_escaped, cfg.autoload_delay,
                     cfg.kill_disc_player ? "true" : "false", cfg.scan_usb_payloads ? "true" : "false");
            resp = MHD_create_response_from_buffer(strlen(resp_buf),
                                                   (void *)resp_buf,
                                                   MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
        }
    } else if (strcmp(url, ROUTE_AUTOLOAD_CLEAR) == 0) {
        pldmgr_autoload_reset();
        const char *msg = "Autoload status cleared.\n";
        resp = MHD_create_response_from_buffer(strlen(msg), (void *)msg,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_ABORT) == 0) {
        pldmgr_autoload_abort();
        const char *msg = "Autoload sequence aborted.\n";
        resp = MHD_create_response_from_buffer(strlen(msg), (void *)msg,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, "/autoload_status") == 0) {
        char resp_buf[64];
        snprintf(resp_buf, sizeof(resp_buf), "{\"remaining\":%d}",
                 pldmgr_autoload_get_remaining_seconds());
        resp = MHD_create_response_from_buffer(strlen(resp_buf),
                                               (void *)resp_buf,
                                               MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_PKG_STATUS) == 0) {
        /* [CN-FORK] PKG Manager 状态：探活 8844 + 内置/当前版本 */
        char pkg_json[512];
        pkgmanager_status_json(pkg_json, sizeof(pkg_json));
        resp = MHD_create_response_from_buffer(strlen(pkg_json), (void *)pkg_json,
                                               MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_PKG_LAUNCH) == 0) {
        /* [CN-FORK] 播种（如需要）并拉起 PKG Manager */
        char lm[256] = "";
        int lrc = pkgmanager_launch(lm, sizeof(lm));
        char lesc[512], ljson[640];
        pldmgr_json_escape(lm, lesc, sizeof(lesc));
        snprintf(ljson, sizeof(ljson), "{\"ok\":%s,\"message\":\"%s\"}",
                 lrc == 0 ? "true" : "false", lesc);
        resp = MHD_create_response_from_buffer(strlen(ljson), (void *)ljson,
                                               MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
        http_status = lrc == 0 ? MHD_HTTP_OK : MHD_HTTP_INTERNAL_SERVER_ERROR;
    } else if (strcmp(url, ROUTE_PKG_STOP) == 0) {
        /* [CN-FORK] 关闭 PKG Manager（按进程名 SIGKILL，不影响其它 payload）*/
        char sm[256] = "";
        int src = pkgmanager_stop(sm, sizeof(sm));
        char sesc[512], sjson[640];
        pldmgr_json_escape(sm, sesc, sizeof(sesc));
        snprintf(sjson, sizeof(sjson), "{\"ok\":%s,\"message\":\"%s\"}",
                 src == 0 ? "true" : "false", sesc);
        resp = MHD_create_response_from_buffer(strlen(sjson), (void *)sjson,
                                               MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
        http_status = src == 0 ? MHD_HTTP_OK : MHD_HTTP_INTERNAL_SERVER_ERROR;
    } else if (strcmp(url, ROUTE_QUICKLINKS) == 0) {
        /* [CN-FORK] 常用入口：清单 + 逐条 TCP 探活（同步串行，进页面拉一次不轮询） */
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        quicklinks_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        resp = MHD_create_response_from_buffer(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_QUICKLINKS_ADD) == 0) {
        /* [CN-FORK] 常用入口：新增（GET 参数由 MHD 自动 URL 解码） */
        const char *ql_name = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "name");
        const char *ql_url = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "url");
        char qmsg[256] = "";
        int qrc = quicklinks_add(ql_name, ql_url, qmsg, sizeof(qmsg));
        char qesc[512], qjson[640];
        pldmgr_json_escape(qmsg, qesc, sizeof(qesc));
        snprintf(qjson, sizeof(qjson), "{\"ok\":%s,\"message\":\"%s\"}",
                 qrc == 0 ? "true" : "false", qesc);
        resp = MHD_create_response_from_buffer(strlen(qjson), (void *)qjson, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
        if (qrc != 0) http_status = MHD_HTTP_BAD_REQUEST;
    } else if (strcmp(url, ROUTE_QUICKLINKS_REMOVE) == 0) {
        /* [CN-FORK] 常用入口：按下标删除 */
        const char *ql_idx = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "index");
        char qmsg[256] = "";
        int qrc = ql_idx ? quicklinks_remove(atoi(ql_idx), qmsg, sizeof(qmsg))
                         : (snprintf(qmsg, sizeof(qmsg), "缺少 index 参数"), -1);
        char qesc[512], qjson[640];
        pldmgr_json_escape(qmsg, qesc, sizeof(qesc));
        snprintf(qjson, sizeof(qjson), "{\"ok\":%s,\"message\":\"%s\"}",
                 qrc == 0 ? "true" : "false", qesc);
        resp = MHD_create_response_from_buffer(strlen(qjson), (void *)qjson, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
        if (qrc != 0) http_status = MHD_HTTP_BAD_REQUEST;
    } else if (strcmp(url, ROUTE_THERMAL) == 0) {
        /* [CN-FORK] 温控只读：CPU/GPU/SSD 温度 + 风扇占空比（safe_mode 短路 + 5s 快照，避免轮询打死内核） */
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        thermal_stats_json(resp_buf, RESPONSE_BUFFER_SIZE);
        resp = MHD_create_response_from_buffer(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_FAN_CONTROL) == 0) {
        /* [CN-FORK] 风扇温控（模型 A + 守护）：?apply=1&target=NN=应用生效（写硬件）；
         * ?set_enabled=0/1=只切开关（不写硬件）；无参=读状态（只读）。safe_mode/设备缺失时 supported:false。 */
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        const char *fapply = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "apply");
        const char *fset   = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "set_enabled");
        const char *ft     = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "target");
        int flen;
        if (fapply) {
            int ftgt = ft ? atoi(ft) : 0;
            flen = fan_control_apply_json(resp_buf, RESPONSE_BUFFER_SIZE, ftgt);
        } else if (fset) {
            flen = fan_control_set_enabled_json(resp_buf, RESPONSE_BUFFER_SIZE, atoi(fset));
        } else {
            flen = fan_control_status_json(resp_buf, RESPONSE_BUFFER_SIZE);
        }
        if (flen < 0) flen = 0;
        resp = MHD_create_response_from_buffer((size_t)flen, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_LIST) == 0) {
        /* [CN-FORK] 游戏列表：app.db 已安装 + 扫描 dump，mutex+60s 缓存，进页面一次拉取不轮询 */
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        int glen = game_mgr_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        if (glen < 0) glen = 0;
        resp = MHD_create_response_from_buffer((size_t)glen, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_ICON) == 0) {
        /* [CN-FORK] 游戏图标：按 tid 读 icon0.png 二进制；找不到回 404。前端 <img src="/game_icon?tid=..."> */
        const char *gtid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        char gpath[768];
        gpath[0] = 0;
        unsigned char *gdata = NULL;
        size_t gsz = 0;
        if (gtid && game_mgr_icon_path(gtid, gpath, sizeof(gpath))) {
            int gfd = open(gpath, O_RDONLY);
            if (gfd >= 0) {
                struct stat gst;
                if (fstat(gfd, &gst) == 0 && S_ISREG(gst.st_mode) && gst.st_size > 0 && gst.st_size <= 4 * 1024 * 1024) {
                    gsz = (size_t)gst.st_size;
                    gdata = (unsigned char *)malloc(gsz);
                    if (gdata && read(gfd, gdata, gsz) != (ssize_t)gsz) { free(gdata); gdata = NULL; }
                }
                close(gfd);
            }
        }
        if (gdata) {
            resp = MHD_create_response_from_buffer(gsz, (void *)gdata, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "image/png");
            MHD_add_response_header(resp, "Cache-Control", "public, max-age=3600");
        } else {
            const char *g404 = "404 Not Found\n";
            resp = MHD_create_response_from_buffer(strlen(g404), (void *)g404, MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(resp, "Content-Type", "text/plain");
            http_status = MHD_HTTP_NOT_FOUND;
        }
    } else if (strcmp(url, ROUTE_GAME_MEDIA) == 0) {
        /* [CN-FORK] v0.5.2.115 多媒体列表：打开弹窗拉一次 + 手动刷新，绝不轮询。
         * 项数由扫库决定，写在 items 后面（md_item_to_json 那套 first 分隔符逻辑）。 */
        const char *mtid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        if (!mtid)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid");
        char *mresp_buf;
        struct MHD_Response *moom = alloc_response_buffer(&mresp_buf);
        if (moom)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, moom);
        int mlen = game_media_list_json(mtid, mresp_buf, RESPONSE_BUFFER_SIZE);
        if (mlen < 0) {
            free(mresp_buf);
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                                    "读取系统媒体库失败（av_content.db）");
        }
        resp = MHD_create_response_from_buffer((size_t)mlen, (void *)mresp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_MEDIA_THUMB) == 0) {
        /* [CN-FORK] 缩略图二进制：系统缩略图 → 盒式平均缩放 → JPEG 落盘缓存。
         * 前端网格按 ~240px 取 w=320 —— 4K 原图绝不进网格。 */
        const char *thid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "id");
        const char *thw = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "w");
        int thw_i = thw ? atoi(thw) : 320;
        unsigned char *td = NULL;
        size_t tlen = 0;
        const char *tmime = "image/jpeg";
        int trc = thid ? game_media_thumb(thid, thw_i, &td, &tlen, &tmime) : -1;
        if (trc == 0 && td) {
            resp = MHD_create_response_from_buffer(tlen, (void *)td, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", tmime);
            MHD_add_response_header(resp, "Cache-Control", "public, max-age=86400");
        } else if (trc == -1) {
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "参数不合法");
        } else {
            /* -2 = 源图不在，或格式没有解码器（HDR .jxr / mp4 视频帧）。
             * 前端缩略图是 <img onError> 兜的，回 404 就够。 */
            const char *t404 = "404 Not Found\n";
            resp = MHD_create_response_from_buffer(strlen(t404), (void *)t404, MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(resp, "Content-Type", "text/plain");
            http_status = MHD_HTTP_NOT_FOUND;
        }
    } else if (strcmp(url, ROUTE_GAME_MEDIA_FILE) == 0) {
        /* [CN-FORK] 原图 / 原视频 fd 直出：录屏动辄几百 MB，读进内存就是把面板 OOM 掉。
         * response 成功创建后 fd 的所有权交给 MHD（销毁时它替我们 close）；
         * 创建失败必须自己 close，否则这里会漏一个 fd。 */
        const char *fid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "id");
        if (!fid)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 id");
        int ffd = -1;
        off_t fsz = 0;
        const char *fmime = "application/octet-stream";
        int frc = game_media_open(fid, &ffd, &fsz, &fmime);
        if (frc == -1)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "参数不合法");
        if (frc != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "文件不存在");
        resp = MHD_create_response_from_fd64((uint64_t)fsz, ffd);
        if (!resp) {
            close(ffd);
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO", "无法读取文件");
        }
        MHD_add_response_header(resp, "Content-Type", fmime);
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_MEDIA_DELETE) == 0) {
        /* [CN-FORK] 删除：只删文件，不写 av_content.db —— 见 include/game_media.h 铁律①。
         * 逻辑失败回 404 带 message，前端 request() 会把 message 抛成 toast 文案。 */
        const char *dtid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        const char *did = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "id");
        if (!dtid || !did)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid 或 id");
        char dmsg[192];
        int drc = game_media_delete(dtid, did, dmsg, sizeof(dmsg));
        if (drc == 0) {
            char desc[400];
            char dout[480];
            pldmgr_json_escape(dmsg, desc, sizeof(desc));
            int dn = snprintf(dout, sizeof(dout), "{\"ok\":true,\"message\":\"%s\"}", desc);
            if (dn < 0 || (size_t)dn >= sizeof(dout)) dn = 0;
            return queue_json(conn, MHD_HTTP_OK, dout, (size_t)dn);
        }
        return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", dmsg[0] ? dmsg : "删除失败");
    } else if (strcmp(url, ROUTE_GAME_JOB_STATUS) == 0) {
        /* [CN-FORK] 任务进度：前端 1s 一次轮询（在 is_noisy_route 里已降噪）。 */
        const char *jqs = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "job");
        char *jend = NULL;
        long long jid = jqs ? strtoll(jqs, &jend, 10) : 0;
        if (!jqs || !jend || jend == jqs || jid <= 0)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少或非法的 job");
        /* 2048 不是拍脑袋：phase + error + reason 三段都做了 JSON 转义，最坏总长
         * 已经贴近 1000 —— 用 1024 一旦截断，game_media_pack_status 会返回 -1，
         * 路由把它当「任务不存在」回 404，前端看到的是一个凭空消失的任务。 */
        char jbuf[2048];
        int jn = game_media_pack_status(jid, jbuf, sizeof(jbuf));
        if (jn < 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "任务不存在或已过期");
        return queue_json(conn, MHD_HTTP_OK, jbuf, (size_t)jn);
    } else if (strcmp(url, ROUTE_GAME_JOB_CANCEL) == 0) {
        const char *cqs = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "job");
        char *cend = NULL;
        long long cid = cqs ? strtoll(cqs, &cend, 10) : 0;
        if (!cqs || !cend || cend == cqs || cid <= 0)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少或非法的 job");
        if (game_media_pack_cancel(cid) != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "任务不存在或已结束");
        return queue_json(conn, MHD_HTTP_OK,
                          "{\"ok\":true,\"reason\":\"cancelled\"}", 33);
    } else if (strcmp(url, ROUTE_GAME_MEDIA_PACK_GET) == 0) {
        /* [CN-FORK] 取打包成品：同样 fd 直出，包可以几百 MB。 */
        const char *pqs = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "job");
        char *pend = NULL;
        long long pid = pqs ? strtoll(pqs, &pend, 10) : 0;
        if (!pqs || !pend || pend == pqs || pid <= 0)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少或非法的 job");
        int pfd = -1;
        off_t psz = 0;
        int prc = game_media_pack_open(pid, &pfd, &psz);
        if (prc == -1)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "任务不存在或已过期");
        if (prc != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT",
                                    "打包还没完成，或产出文件已被清理");
        resp = MHD_create_response_from_fd64((uint64_t)psz, pfd);
        if (!resp) {
            close(pfd);
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO", "无法读取打包文件");
        }
        MHD_add_response_header(resp, "Content-Type", "application/zip");
        MHD_add_response_header(resp, "Content-Disposition",
                                "attachment; filename=\"media_pack.zip\"");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GET_CONFIG) == 0) {
        PldmgrConfig cfg;
        config_read(&cfg);

        /* Get list */
        char list_buf[4096] = {0};
        FILE *f = fopen(AUTOLOAD_CONFIG_PATH, "r");
        if (f) {
            char line[256];
            int first = 1;
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                if (strlen(line) == 0)
                    continue;
                if (!first)
                    strncat(list_buf, ",", sizeof(list_buf) - strlen(list_buf) - 1);
                strncat(list_buf, line, sizeof(list_buf) - strlen(list_buf) - 1);
                first = 0;
            }
            fclose(f);
        }

        char list_escaped[8192];
        pldmgr_json_escape(list_buf, list_escaped, sizeof(list_escaped));

        char *resp_buf;
        resp = alloc_response_buffer(&resp_buf);
        if (!resp) {
            snprintf(resp_buf, RESPONSE_BUFFER_SIZE,
                     "{\"AUTOLOAD_ENABLED\":%s,\"AUTOLOAD_LIST\":\"%s\",\"LAST_"
                     "REPOSITORY_UPDATE\":%ld,"
                     "\"AUTO_BROWSER_OPEN\":%s,\"AUTOLOAD_DELAY\":%d,"
                     "\"KILL_DISC_PLAYER_ON_STARTUP\":%s,\"SCAN_USB_PAYLOADS\":%s,"
                     "\"AUTO_INSTALL_APP\":%s,\"MULTI_SOURCES_ENABLED\":%s,"
                     "\"DEFAULT_REPO_ENABLED\":%s}",
                     cfg.autoload_enabled ? "true" : "false", list_escaped, cfg.last_repository_update,
                     cfg.auto_browser_open ? "true" : "false", cfg.autoload_delay,
                     cfg.kill_disc_player ? "true" : "false", cfg.scan_usb_payloads ? "true" : "false",
                     cfg.auto_install_app ? "true" : "false",
                     cfg.multi_sources_enabled ? "true" : "false",
                     cfg.default_repo_enabled ? "true" : "false");
            resp = MHD_create_response_from_buffer(strlen(resp_buf),
                                                   (void *)resp_buf,
                                                   MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
        }
    } else if (strcmp(url, "/events") == 0) {
        struct LogSSEConnection *sse = malloc(sizeof(struct LogSSEConnection));
        sse->last_log_version = 0;
        sse->sent_initial = 0;

        resp = MHD_create_response_from_callback(
            MHD_SIZE_UNKNOWN, 1024, &log_stream_callback, sse, &log_stream_cleanup);
        MHD_add_response_header(resp, "Content-Type", "text/event-stream");
        MHD_add_response_header(resp, "Cache-Control", "no-cache");
        MHD_add_response_header(resp, "Connection", "keep-alive");
    } else {
        /* Default: 404 */
        const char *not_found = "404 Not Found\n";
        resp = MHD_create_response_from_buffer(strlen(not_found), (void *)not_found,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    }

    if (!resp)
        return MHD_NO;

    add_cors_headers(resp);
    ret = MHD_queue_response(conn, http_status, resp);
    MHD_destroy_response(resp);

    return ret;
}

/* [CN-FORK] v0.5.2.99 账号板块的流式头像上传需要在请求结束时回收状态（浏览器
 * 传到一半关页面/断网）。本工程原有的 UploadStatus/PostStatus 都在各链末尾
 * 自行 free 并把 *con_cls 置 NULL，所以这里只需要转发给 psaccounts；它自己再用
 * magic 头认一遍，不是它的状态一律不动。
 * (void *)1 是上面注册块给普通请求打的占位标记，不是指针，绝不能解引用。 */
void http_on_request_completed(void *cls, struct MHD_Connection *conn,
                               void **con_cls,
                               enum MHD_RequestTerminationCode toe) {
    (void)cls;
    (void)conn;
    (void)toe;

    if (con_cls == NULL || *con_cls == NULL || *con_cls == (void *)1) return;
    psaccounts_conn_completed(*con_cls);
}
