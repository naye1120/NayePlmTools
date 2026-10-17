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
#include <ctype.h>   /* [UPSTREAM] v0.6.0 isxdigit/tolower（url_decode_in_place） */
#include <errno.h>   /* [CN-FORK] v0.5.2.116 mkdir 的 EEXIST（存档下载临时目录） */
#include <time.h>    /* [CN-FORK] v0.5.2.116 存档下载临时包名里的毫秒级唯一因子 */
#include <fcntl.h>  /* [CN-FORK] open() for /game_icon */
#include <unistd.h> /* [CN-FORK] close() for /game_icon */
#include <stdint.h> /* [CN-FORK] gr_file_read 的 uint64_t pos（MHD 内容回调签名） */
#include <sys/stat.h>
#include <sys/socket.h>   /* [CN-FORK] v0.5.2.131 /smp_status 的 loopback TCP 探活 */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <microhttpd.h>

#include "http_server.h"
#include "pldmgr.h"
#include "log_server.h"
#include "config.h"
#include "cJSON.h" /* [CN-FORK] v0.5.2.116 POST 分发里从 body 取 tid/name */
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
#include "media_remote.h" /* [CN-FORK] v0.6.0.177 多媒体远程同步 /game_media_sync + 自动备份 kick */
#include "game_runtime.h" /* [CN-FORK] v0.5.2.116 删源 / 挂载 / 卸载挂载 / 卸载安装 */
#include "game_saves.h"   /* [CN-FORK] v0.5.2.116 存档六条 */
#include "save_remote.h"  /* [CN-FORK] v0.5.2.172 SR_ENOTFOUND（远程存档下载的「远端没有」）*/
#include "game_auto.h"    /* [CN-FORK] v0.5.2.142 自动存档手动推 / 拉 */
#include "game_title.h"   /* [CN-FORK] v0.5.2.116 改名 + 磁贴两层图六条 */
#include "fan_control.h" /* [CN-FORK] 风扇温控（模型 A）/fan_control */
#include "app_visibility.h" /* [CN-FORK] v0.5.2.158 「个性化软件」/system_apps* */
#include "app_tile.h"       /* [CN-FORK] v0.6.0.179 内置软件换图标/背景 /app_icon 等 */
#include "game_prefs.h"     /* [CN-FORK] v0.5.2.164 游戏磁贴收藏 / 置顶 /game_prefs* */
#include "game_music.h"     /* [CN-FORK] v0.5.2.163 背景音乐 /game_music[_reset] */
#include "tile_group.h"     /* [CN-FORK] v0.5.2.166 主屏固定列表 /tile_group* */
/* [CN-FORK] v0.5.2.99 PS5 Account Tools 源码并入主 ELF：账号/头像/安全还原/远程游玩
 * 配对接口认领 /api/ 前缀，其余路由仍全部归本文件 */
#include "psaccounts/api.h"
#include "slot.h"        /* [UPSTREAM] v0.6.0 槽位化存储 */
#include "json_helpers.h" /* [UPSTREAM] v0.6.0 JSON 构建器（json_append / JsonListBuilder） */

#include "assets_index_html.h"
#include "assets_cache_appcache.h"
#include "assets_favicon_svg.h"
#include "assets_icon_png.h"

#define RESPONSE_BUFFER_SIZE 4194304
#define SMALL_RESPONSE_BUFFER_SIZE 65536
#define SOURCES_RESPONSE_BUFFER_SIZE 524288
#define LOG_RESPONSE_BUFFER_SIZE (MAX_LOG_LINES * (6 * MAX_LOG_LINE_LEN + 4) + 32)

/* Shared flag — set from main() shutdown route, read by main loop */
volatile int http_keep_running = 1;

/* [CN-FORK] 面板自带前端由主机同源提供，fork 自有路由沿用宽松 CORS 值 */
#define CORS_ORIGIN "*"

/* ── Helpers ───────────────────────────────────────────────── */

/* A failed response creation leaves MUST_FREE buffers owned by us. */
static struct MHD_Response *create_buffer_response(size_t size, void *buffer,
                                                  enum MHD_ResponseMemoryMode mode) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(size, buffer, mode);
    if (!resp && mode == MHD_RESPMEM_MUST_FREE)
        free(buffer);
    return resp;
}

/* [CN-FORK] v0.5.2.115 小 JSON 直接按栈上内容回（MUST_COPY 会先拷再交给 MHD，
 * 所以传局部数组是安全的）。统一加 Content-Type + CORS，免得每条路由各写一遍漏掉一个。
 *
 * ⚠ v0.6.0 合并说明：上游改成了「CORS 只有一个所有者」的模型 —— 由 send_response()
 * 统一发 `Vary: Origin` 并**只**在同源时回显 Origin。fork 侧自有的 ~40 条路由仍走
 * 这套 add_cors_headers（`Access-Control-Allow-Origin: *`），两套机制作用在互不相交
 * 的路由集合上，不会重复下发头部。上游路由一律走 send_response()。 */
static void add_cors_headers(struct MHD_Response *resp) {
    if (resp) MHD_add_response_header(resp, "Access-Control-Allow-Origin", CORS_ORIGIN);
}

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

/* ── [UPSTREAM] v0.6.0 CSRF / 同源安全层 ─────────────────────
 * 这是 v0.6.0 的**核心安全改动**：所有「写」路由（POST/GET 变更类）在分配请求状态、
 * 打开上传、触发浏览器/自动加载、消费请求体之前，先校验 Origin/Referer/Sec-Fetch-Site
 * 与 X-PLDMGR-Request 标记 —— 挡住用户浏览恶意网页时被跨站发起的变更请求。 */
static enum MHD_Result add_response_header(struct MHD_Response *resp,
                                           const char *name, const char *value) {
    return resp ? MHD_add_response_header(resp, name, value) : MHD_NO;
}

/* Explicit actions use POST. Automatic cache refresh on a read remains a read;
 * repository_refresh is the explicit forced-refresh action.
 *
 * [CN-FORK] v0.6.0.177 这张表**当初只登记了上游自己的 14 条**，本 fork 加的
 * 全部 POST 路由一条都没进来 —— 于是下面那道方法闸门（`mutation ? POST : GET`）
 * 对它们一律按「不是 mutation」处理，**强制 GET**，而前端发的是 POST，
 * 结果就是用户看到的「好几个功能无缘无故 405」（不是 404、不是 400，是 405）。
 *
 * 漏表是双输：漏掉一条 POST = 用户看到 405；漏掉一条 GET 变更 = 完全绕过 CSRF。
 *
 * ⚠ 新增 POST 路由的三处登记缺一不可：这里（is_post_route）、
 * gr_post_limit（收不收请求体）、gr_dispatch_post（收到后怎么处理）。
 * 后两处由 tools/check_post_routes.sh 静态守，这一处也已被它一并覆盖。 */
/* 表 A：**必须是 POST** 的路由（方法闸门 + CSRF 两处都用）。 */
static int is_post_route(const char *url) {
    const char *routes[] = { ROUTE_UPLOAD, ROUTE_DELETE, ROUTE_SHUTDOWN,
        ROUTE_SET_CONFIG, ROUTE_ABORT, ROUTE_AUTOLOAD_CLEAR, ROUTE_REPO_REFRESH,
        ROUTE_REPO_INSTALL, ROUTE_SOURCES_SET, ROUTE_SOURCES_ADD,
        ROUTE_SOURCES_REMOVE, ROUTE_REPO_CACHE_CLEAR, ROUTE_USB_MOVE_PERFORM, ROUTE_PROCESS_KILL,
        /* ── [CN-FORK] 本 fork 的 POST 路由（25 条：23 条在 gr_dispatch_post 里分发，
         *   /game_media_pack 与 /pkgmanager_upload 各走自己的专用分支，另加新端点
         *   /game_media_sync）—— 上游那张表只登了自己的 14 条，这 25 条当初
         *   一条都没进来，于是它们全被方法闸门当成 GET 而前端发的是 POST，
         *   结果就是用户报的「好几个功能无缘无故 405」。 ── */
        ROUTE_GAME_SAVE_UPLOAD, ROUTE_GAME_SAVE_GARLIC, ROUTE_GAME_TILE_ICON,
        ROUTE_GAME_TILE_BACKGROUND, ROUTE_GAME_MUSIC, ROUTE_GAME_MUSIC_AT9,
        ROUTE_GAME_PREFS_SET, ROUTE_GAME_DELETE, ROUTE_GAME_SAVE_AUTO,
        ROUTE_GAME_SAVE_TEST, ROUTE_GAME_SAVE_SNAP, ROUTE_GAME_SAVE_SYNC,
        ROUTE_GAME_RENAME, ROUTE_GAME_SYNC, ROUTE_GAME_MOUNT, ROUTE_GAME_UNMOUNT,
        ROUTE_GAME_UNINSTALL, ROUTE_GAME_SAVE_REMOTE_DELETE, ROUTE_SYSTEM_APP_VISIBLE,
        ROUTE_SYSTEM_APP_PIN, ROUTE_TILE_GROUP_PIN, ROUTE_TILE_GROUP_ORDER,
        ROUTE_APP_TILE_ICON, ROUTE_APP_TILE_BACKGROUND,
        ROUTE_GAME_MEDIA_PACK, ROUTE_GAME_MEDIA_SYNC, ROUTE_PKG_UPLOAD };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); i++)
        if (!strcmp(url, routes[i])) return 1;
    return !strncmp(url, ROUTE_LOAD_PAYLOAD, strlen(ROUTE_LOAD_PAYLOAD));
}

/* 表 B：**变更型 GET** —— 方法仍然是 GET（前端就是这么调的，改成 POST 要连
 * 前端一起动，两头不同步会把老功能从 405 换成 403），但它们确实改状态，
 * 所以必须过下面的 CSRF 校验 —— v0.6.0 上游把它们整个漏在表外，
 * 于是这几条**任意跨站页面都能触发**（用户点开一个网页就能删他的存档/截图）。
 * 只进 CSRF，不进方法闸门，两张表由此分开。 */
static int is_get_mutation(const char *url) {
    const char *routes[] = {
        ROUTE_QUICKLINKS_ADD, ROUTE_QUICKLINKS_REMOVE, ROUTE_GAME_SAVE_DELETE,
        ROUTE_GAME_MEDIA_DELETE, ROUTE_GAME_TILE_ICON_RESET,
        ROUTE_GAME_TILE_BACKGROUND_RESET, ROUTE_GAME_MUSIC_RESET,
        /* [CN-FORK] v0.6.0.179 内置软件的「恢复默认」：会改主机 app.db 的两列，
         * 必须过 CSRF（与游戏那两条 *_reset 同档）。 */
        ROUTE_APP_TILE_ICON_RESET, ROUTE_APP_TILE_BACKGROUND_RESET,
        ROUTE_GAME_JOB_CANCEL, ROUTE_PKG_LAUNCH, ROUTE_PKG_STOP, ROUTE_FAN_CONTROL };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); i++)
        if (!strcmp(url, routes[i])) return 1;
    return 0;
}

/* 「这条请求是写吗」= 方法闸门（表 A）∪ CSRF（表 A ∪ 表 B）。
 * 拆成两个谓词是为了让两件事各归各：表 B 的路由进了这张总表之后
 * 仍然能被表 A 的方法闸门放行成 GET。 */
static int is_mutation_route(const char *url) {
    return is_post_route(url) || is_get_mutation(url);
}
/* HTTP daemon only. Compare the complete authority, including the port; never
 * accept suffix/prefix matches, null/file origins, userinfo, or multiple origins.
 * Host matching is CSRF protection, not authentication or DNS-rebinding defense. */
static int same_origin(const char *value, const char *host, int referer) {
    if (!value || !host || !host[0] || strncmp(value, "http://", 7)) return 0;
    for (const unsigned char *p = (const unsigned char *)host; *p; p++)
        if (!isalnum(*p) && *p != '.' && *p != '-' && *p != ':' && *p != '[' && *p != ']') return 0;
    const char *authority = value + 7;
    size_t n = strcspn(authority, "/?#");
    if (strlen(host) != n || strncasecmp(authority, host, n)) return 0;
    return referer ? (!authority[n] || authority[n] == '/') : authority[n] == 0;
}
/* Allow 头里的方法表可能是一个列表（psaccounts 那一组允许 GET、POST 两种），
 * 这里按逗号分段比对，而不是 strcmp —— 直接 strcmp 会让 psaccounts 那几条
 * GET 请求变成 405（它明明能处理 GET）。 */
static int in_method_list(const char *list, const char *m) {
    if (!list || !m) return 0;
    size_t n = strlen(m);
    const char *p = list;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        while (len && *p == ' ') { p++; len--; }
        while (len && p[len - 1] == ' ') len--;
        if (len == n && strncmp(p, m, n) == 0) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static int mutation_authorized(struct MHD_Connection *conn) {
    const char *host = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Host");
    const char *origin = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Origin");
    const char *referer = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Referer");
    const char *site = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Sec-Fetch-Site");
    if (site && !strcmp(site, "cross-site")) return 0;
    if (origin) return same_origin(origin, host, 0);
    if (referer) return same_origin(referer, host, 1);
    const char *marker = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "X-PLDMGR-Request");
    return marker && !strcmp(marker, "1");
}
static int preflight_headers_valid(const char *headers) {
    if (!headers || !headers[0]) return 1;
    const char *p = headers;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        const char *start = p;
        while (*p && *p != ',') p++;
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        size_t n = (size_t)(end-start);
        if (!((n == 12 && !strncasecmp(start, "Content-Type", n)) ||
              (n == 16 && !strncasecmp(start, "X-PLDMGR-Request", n)))) return 0;
        if (*p && !*++p) return 0;
    }
    return 1;
}

/* MHD retains its own reference on a successful queue. Always release ours,
 * including when queueing fails. CORS headers have exactly one owner. */
static enum MHD_Result send_response(struct MHD_Connection *conn,
                                     unsigned int status, struct MHD_Response *resp) {
    if (!resp)
        return MHD_NO;
    enum MHD_Result ret = MHD_NO;
    const char *origin = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Origin");
    const char *host = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Host");
    int headers_ok = add_response_header(resp, "Vary", "Origin") == MHD_YES;
    if (origin && same_origin(origin, host, 0))
        headers_ok = headers_ok && add_response_header(resp, "Access-Control-Allow-Origin", origin) == MHD_YES;
    if (headers_ok) ret = MHD_queue_response(conn, status, resp);
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

/* [CN-FORK] v0.5.2.131 ShadowMount+ 探活：只探 loopback。
 *
 * 为什么必须在本机探、不能交给前端 fetch：SMP 的 Web UI（10101）在很多机器上
 * 只监听 127.0.0.1，电脑用浏览器直连不一定通；而「面板能不能拉起它」这件事跟
 * 浏览器在哪台机器上无关 —— 只看主机自己能不能连上。探测逻辑留在主机侧，
 * 前端只读一个布尔值。
 *
 * 实现照 pkgmanager_is_running()（pkgmanager.c）：非阻塞 connect 的 select 版
 * 留给 quicklinks 那套跨机探测用，本机 loopback 建连极快，阻塞 connect 加
 * 400ms 收发超时兜底就够，少引一套非阻塞状态机。 */
#define SMP_UI_PORT 10101

static int smp_is_running(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;
    struct timeval tv = {0, 400000}; /* loopback 建连很快，400ms 兜底 */
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SMP_UI_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int running = (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    close(sock);
    return running;
}

/* 只读状态：面板不拉起、不关闭 SMP —— 拉起要走 /loadpayload:（上游 elfldr），
 * 关闭 SMP 属于它的生命周期管理，交给 SMP 自己或用户手动做。 */
static void smp_status_json(char *out, size_t osz) {
    snprintf(out, osz, "{\"running\":%s,\"port\":%d}",
             smp_is_running() ? "true" : "false", SMP_UI_PORT);
}

/* [CN-FORK] v0.5.2.116 **文件直出一律走自写回调，不再用 MHD_create_response_from_fd64。**
 *
 * 症状（用户实测，多媒体管理两条）：原图/录屏预览只画出图片**最顶上的一条**，
 * 打包 zip 下载**直接失败**。同一进程里其余响应全好 —— 缩略图、所有 JSON 走
 * MHD_create_response_from_buffer，/events 的 SSE 流走 MHD_create_response_from_callback。
 * 也就是说本工程里**唯一**出问题的就是 fd 直出这一条分支。
 *
 * 排查到的差别：链接进来的 libmicrohttpd.a 里导出了 MHD_send_sendfile_，说明
 * fd 直出会落到内核 sendfile() 那条路上；而 MHD 的文档自己写着，构建没开
 * MHD_FEATURE_RESPONSES_SHARED_FD 时「同一条响应在多个连接上并发可能把错误数据
 * 发给客户端」。Content-Length 是照 fstat 报出去的，字节却少一截 —— 图片解码到断点
 * 就停（只剩顶部一条），zip 少尾（下载判失败）。两条症状同一个根。
 *
 * 改法：自己按 pos lseek + read，整条 sendfile 路径绕开。内存仍是每块 32KB，
 * 几百 MB 的录屏照样不进堆 —— 换掉的只是「谁来搬字节」，不是「搬多少」。
 * /events 那条流真机上一直是好的，_from_callback 这条路是通的，两处走同一条。
 *
 * fd 所有权仍按 MHD 约定由 free 回调关；任何创建失败的路径都必须自己收尸，
 * 否则每次「没设过背景」都会漏一个 fd。 */
typedef struct { int fd; } gr_file_cls_t;

static ssize_t gr_file_read(void *cls, uint64_t pos, char *buf, size_t max) {
    gr_file_cls_t *f = (gr_file_cls_t *)cls;
    if (!f || f->fd < 0) return MHD_CONTENT_READER_END_WITH_ERROR;
    if (max == 0) return 0;
    if (lseek(f->fd, (off_t)pos, SEEK_SET) < 0)
        return MHD_CONTENT_READER_END_WITH_ERROR;
    ssize_t n = read(f->fd, buf, max);
    if (n < 0) {
        if (errno == EINTR) return 0; /* 被信号打断：让 MHD 再问一次，别判死 */
        return MHD_CONTENT_READER_END_WITH_ERROR;
    }
    /* 0 = 文件比声明的 size 短（理论上到不了）。回 END_OF_STREAM 会被 MHD 当成
     * error 收连接 —— 与其悄悄发半张图，不如让浏览器明确知道这次没拿到完整的。 */
    if (n == 0) return MHD_CONTENT_READER_END_OF_STREAM;
    return n;
}

static void gr_file_free(void *cls) {
    gr_file_cls_t *f = (gr_file_cls_t *)cls;
    if (!f) return;
    if (f->fd >= 0) close(f->fd);
    free(f);
}

static struct MHD_Response *gr_file_response(off_t size, int fd) {
    gr_file_cls_t *f = (gr_file_cls_t *)calloc(1, sizeof(*f));
    if (!f) { close(fd); return NULL; }
    f->fd = fd;
    struct MHD_Response *r = MHD_create_response_from_callback(
        (uint64_t)size, 32u * 1024u, &gr_file_read, f, &gr_file_free);
    if (!r) gr_file_free(f); /* 创建失败 MHD 不会替我们调 free 回调 */
    return r;
}

/* [CN-FORK] v0.5.2.116 把磁贴图 / 存档包按路径直出。
 * 不读进内存：用户上传的背景能到 64MB，存档包能到 512MB，进内存就是拿面板的堆去赌一次
 * OOM；字节怎么搬交给上面的 gr_file_response（见那段注释为什么不用 fd 直出）。
 * remove_after 在**response 创建成功之后**再 unlink：POSIX 下已打开的 fd 不受
 * unlink 影响，MHD 会照常把它流完，这样下载不必等连接结束才能清理临时包。 */
static enum MHD_Result reply_file(struct MHD_Connection *conn, const char *path,
                                  const char *mime, const char *disposition,
                                  int remove_after) {
    int fd = path ? open(path, O_RDONLY) : -1;
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
            /* 从这一行起 fd 的所有权交出去了 —— gr_file_response 无论成败都自己收尸
             *（calloc 失败 close(fd)，create 失败走 gr_file_free），所以这里必须把
             * fd 置 -1：再 close 一次就是关掉一个已经归它管的描述符，关到别的
             * 文件头上就是数据串台。 */
            struct MHD_Response *r = gr_file_response(st.st_size, fd);
            fd = -1;
            if (r) {
                MHD_add_response_header(r, "Content-Type", mime);
                if (disposition) MHD_add_response_header(r, "Content-Disposition", disposition);
                MHD_add_response_header(r, "Cache-Control", "no-store");
                add_cors_headers(r);
                if (remove_after) unlink(path);
                enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
                MHD_destroy_response(r);
                return ret;
            }
        }
        if (fd >= 0) close(fd); /* 只关还没交出去的那几条（fstat 失败 / 非普通文件 / 0 字节） */
    }
    /* 失败也一并清掉临时包（成功那条分支已经 unlinks 过，再 unlink 一次只是
     * 白得一个 ENOENT，但换来「任何退出路径都不留垃圾」这条不会忘的性质）。 */
    if (remove_after && path) unlink(path);
    return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "文件不存在");
}

/* [CN-FORK] v0.5.2.122 下载文件名下发（用户口径：「截图下载和视频下载的命名方式是
 * 游戏名称-截图时间。压缩包也是游戏名称-打包时间戳」）。
 *
 * 中文名必须走 RFC 5987 的 `filename*`，但 PS5 的 WebKit 很老，只认
 * `filename="…"` —— 所以两个都给：前者是 UTF-8 百分号编码（现代浏览器优先用它），
 * 后者把非 ASCII 压成 '_'（兜底，保证头本身合法）。
 *
 * 引号 / 反斜杠 / 控制字符一律转义：名字来自游戏名，游戏名是用户输入的，
 * 一个 '"' 就能把 Content-Disposition 拆坏（那是响应头注入）。 */
static void add_disposition(struct MHD_Response *resp, const char *name) {
    if (!resp || !name || !name[0]) return;
    static const char *hex = "0123456789ABCDEF";

    char ascii[300];
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p && k + 1 < sizeof(ascii); p++) {
        unsigned char c = *p;
        ascii[k++] = (c < 0x20 || c >= 0x7f) ? '_' : (char)c;
    }
    ascii[k] = 0;

    char enc[1200];
    size_t e = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p && e + 4 < sizeof(enc); p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\' || c < 0x20 || c >= 0x7f) {
            enc[e++] = '%';
            enc[e++] = hex[c >> 4];
            enc[e++] = hex[c & 0x0f];
        } else {
            enc[e++] = (char)c;
        }
    }
    enc[e] = 0;

    char hdr[1600];
    int n = snprintf(hdr, sizeof(hdr),
                     "attachment; filename=\"%s\"; filename*=UTF-8''%s", ascii, enc);
    if (n > 0 && (size_t)n < sizeof(hdr))
        MHD_add_response_header(resp, "Content-Disposition", hdr);
}

/* [CN-FORK] v0.5.2.116 这条 POST 要收多少字节的请求体；0 = 不归我们管。
 * 上限不是随手定的，逐条对着 mock-server.js 的 express.raw({limit:…})：
 *   存档包 512mb、磁贴 PNG 64mb、其余 JSON 1MB。
 * 前端能过、后端拒的后果是用户只看到一句「请求失败」，连为什么都拿不到。
 *
 * ⚠ 加了新 POST 路由**必须同时**在这里登记（`.137` 的教训）：这个返回值就是
 * 收请求体的闸门 —— 返回 0 时上面的分发根本不建 PostStatus，`gr_dispatch_post`
 * 永不触发，客户端看到的是**连接被直接关掉**（RemoteDisconnected），
 * 而不是 404 / 500，在真机上极难从现象反推回这里。 */
static size_t gr_post_limit(const char *url) {
    if (strcmp(url, ROUTE_GAME_SAVE_UPLOAD) == 0) return (size_t)512 << 20;
    /* [CN-FORK] v0.5.2.154 garlic 导入收的是**加密镜像**（一个存档目录里的单个
     * sdimg_ 文件，真机实测几 MB 到二十几 MB），与上传同一档上限。
     * ⚠ 新加 POST 路由**必须**在这儿登记 —— 只加分发不登记的话，客户端会看到
     * 「请求被直接关掉」（没有 Content-Length 上限 → 走到默认 0 → 不读 body）。
     * 守卫 tools/check_post_routes.sh 会抓这个。 */
    if (strcmp(url, ROUTE_GAME_SAVE_GARLIC) == 0) return (size_t)512 << 20;
    if (strcmp(url, ROUTE_GAME_TILE_ICON) == 0 ||
        strcmp(url, ROUTE_GAME_TILE_BACKGROUND) == 0)
        return (size_t)64 << 20;
    /* [CN-FORK] v0.6.0.179 内置软件的图标 / 背景：与游戏那两条同形（裸 PNG），
     * 同一个 64MB 上限（图标前端先缩到 512²、背景 1920×1080，正常几百 KB）。
     * ⚠ 不登记这里的后果不是 404，而是**连接被直接关掉**（见文件头那段注释）。 */
    if (strcmp(url, ROUTE_APP_TILE_ICON) == 0 ||
        strcmp(url, ROUTE_APP_TILE_BACKGROUND) == 0)
        return (size_t)64 << 20;
    /* [CN-FORK] v0.5.2.163 背景音乐：MP3/OGG/WAV，体积上限 30MB（前端的软限制）。
     * 给到 64MB 是因为前端的 30MB 是软提示（防误传），这里真做了硬限。 */
    if (strcmp(url, ROUTE_GAME_MUSIC) == 0) return (size_t)64 << 20;
    /* [CN-FORK] v0.5.2.166 已编码好的 .at9：PS5 的硬上限是 2MB，给 8MB 留足余量
     * （前端会自己卡在 2MB，这里只是防呆）。 */
    if (strcmp(url, ROUTE_GAME_MUSIC_AT9) == 0) return (size_t)8 << 20;
    /* [CN-FORK] v0.5.2.164 游戏收藏 / 置顶：body 只有 tid + fav/pin 两个布尔。 */
    if (strcmp(url, ROUTE_GAME_PREFS_SET) == 0) return (size_t)1 << 20;
    if (strcmp(url, ROUTE_GAME_DELETE) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_AUTO) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_TEST) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_SNAP) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_SYNC) == 0 ||
        strcmp(url, ROUTE_GAME_RENAME) == 0 ||
        strcmp(url, ROUTE_GAME_SYNC) == 0 ||
        strcmp(url, ROUTE_GAME_MOUNT) == 0 ||
        strcmp(url, ROUTE_GAME_UNMOUNT) == 0 ||
        strcmp(url, ROUTE_GAME_UNINSTALL) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_REMOTE_DELETE) == 0) /* [CN-FORK] v0.5.2.159 body 只有 tid + save */
        return (size_t)1 << 20;
    /* [CN-FORK] v0.5.2.158 「个性化软件」的开关：body 只有 tid + hidden 两个字段。
     * ⚠ 新加 POST 路由必须在这里登记，否则请求体压根不收（见上面的文件头注释）。
     * [CN-FORK] v0.5.2.164 置顶开关同形（tid + pinned）。 */
    if (strcmp(url, ROUTE_SYSTEM_APP_VISIBLE) == 0 ||
        strcmp(url, ROUTE_SYSTEM_APP_PIN) == 0)
        return (size_t)1 << 20;
    /* [CN-FORK] v0.5.2.166 主屏固定列表。置顶的 body 只有 tid + pinned 两个字段；
     * 重排的 body 是一个 order 数组（几十个 9 位 tid ≈ 1KB 以内），给 64KB 是留足余量。 */
    if (strcmp(url, ROUTE_TILE_GROUP_PIN) == 0) return (size_t)1 << 20;
    if (strcmp(url, ROUTE_TILE_GROUP_ORDER) == 0) return (size_t)64 << 10;
    /* [CN-FORK] v0.6.0.177 多媒体远程同步：body 与 /game_media_pack 同形
     * {tid, ids:[…]|null}，全是 content_path 字符串。给 1MB（pack 那条是 512KB），
     * 是因为同步允许**全选**：全选时 ids 会比打包那次长一截，撞上限的表现是
     * 「点同步没反应」（status->error → 413），比 405 还难查。 */
    if (strcmp(url, ROUTE_GAME_MEDIA_SYNC) == 0) return (size_t)1 << 20;
    return 0;
}

/* 统一的 {error, message} 回包，返回写入长度。与 queue_json_error 的区别：
 * 这一个把 *code 也一起设了 —— POST 分发有 7 条分支、6 种状态码，
 * 让每条分支自己 snprintf 再各自记得改 code，漏一个就是「参数错了却回 200」，
 * 前端只会显示一句干巴巴的成功提示。 */
static int gr_json_err(char *out, size_t size, int *code, int http,
                       const char *ecode, const char *msg) {
    char esc[480];
    pldmgr_json_escape(msg ? msg : "", esc, sizeof(esc));
    int n = snprintf(out, size, "{\"error\":\"%s\",\"message\":\"%s\"}", ecode, esc);
    if (n < 0 || (size_t)n >= size) return -1;
    *code = http;
    return n;
}

/* 从 JSON body 里取一个字符串字段。取不到 / 类型不对 / 超长一律 0。 */
static int gr_json_str(const char *body, size_t len, const char *key,
                       char *out, size_t osz) {
    cJSON *root;
    out[0] = '\0';
    if (!body || !len) return 0;
    root = cJSON_ParseWithLength(body, len);
    if (!root) return 0;
    cJSON *f = cJSON_GetObjectItem(root, key);
    int ok = 0;
    if (cJSON_IsString(f) && f->valuestring) {
        /* 超长就整个拒绝，不截断 —— 截断后的名字会被存成另一回事，
         * 用户看到的和他输的对不上，还以为是编码问题。 */
        if (strlen(f->valuestring) < osz) {
            snprintf(out, osz, "%s", f->valuestring);
            ok = 1;
        }
    }
    cJSON_Delete(root);
    return ok;
}

/* [CN-FORK] v0.5.2.158 从 JSON body 里取一个布尔字段。取不到 / 类型不对回 -1，
 * 成功时 *out = 1/0。和 gr_json_str 一样「拿不到就说拿不到」，不替调用方猜默认值 ——
 * 隐藏开关猜错方向的后果是把用户想留的磁贴藏了。 */
static int gr_json_bool(const char *body, size_t len, const char *key, int *out) {
    cJSON *root;
    if (out) *out = -1;
    if (!body || !len || !out) return -1;
    root = cJSON_ParseWithLength(body, len);
    if (!root) return -1;
    cJSON *f = cJSON_GetObjectItem(root, key);
    int ok = -1;
    if (cJSON_IsBool(f)) {
        *out = cJSON_IsTrue(f) ? 1 : 0;
        ok = 0;
    }
    cJSON_Delete(root);
    return ok;
}

/* [CN-FORK] v0.5.2.116 POST 请求体收完之后的分发。返回响应体长度，*code 回状态码。
 * 删源/挂载/卸载四条**自己**拼完整响应体（它们的状态码种类最多，见 game_runtime.h）；
 * 其余五条在这里翻译。 */
static int gr_dispatch_post(struct MHD_Connection *conn, const char *url,
                            const char *body, size_t len,
                            char *out, size_t size, int *code) {
    const char *tid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
    const char *save = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "save");

    if (strcmp(url, ROUTE_GAME_DELETE) == 0)
        return game_runtime_delete(body, len, out, size, code);
    if (strcmp(url, ROUTE_GAME_MOUNT) == 0)
        return game_runtime_mount(body, len, out, size, code);
    if (strcmp(url, ROUTE_GAME_UNMOUNT) == 0)
        return game_runtime_unmount(body, len, out, size, code);
    if (strcmp(url, ROUTE_GAME_UNINSTALL) == 0)
        return game_runtime_uninstall(body, len, out, size, code);

    if (strcmp(url, ROUTE_GAME_SAVE_TEST) == 0) {
        if (game_saves_test(body, len, out, size) == 0) {
            *code = MHD_HTTP_OK;
            return (int)strlen(out);
        }
        return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST,
                           "EHOSTUNREACH", "连不上这个地址，请检查主机与端口");
    }

    if (strcmp(url, ROUTE_GAME_SAVE_AUTO) == 0) {
        char msg[192], btid[40];
        msg[0] = 0;
        /* tid 走的是 **body** —— gameApi 的 setSaveAuto 是
         * `body: { tid, ...cfg }`，mock 也从 `req.body.tid` 取。这条不能像
         * /game_saves 那些 GET 一样去 query 拿：query 里根本没有，取到必然是
         * NULL，结果就是用户每次点「保存配置」都撞 400「缺少 tid」，而配置
         * 一个字都没写进去 —— 属于「接口说失败、用户不知道为什么」那种。 */
        if (!gr_json_str(body, len, "tid", btid, sizeof(btid)) || !btid[0]) {
            if (tid && *tid) snprintf(btid, sizeof(btid), "%s", tid); /* 兜底：query 也认 */
        }
        if (!btid[0])
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (game_saves_auto_set(btid, body, len, msg, sizeof(msg)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               msg[0] ? msg : "参数不合法");
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size, "{\"ok\":true}");
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.140 本地快照：立即备份 / 还原 / 删除。三条都自己拼响应体
     * （成功形状不同：run 带 made 计数），失败一律翻成人话回 400。 */
    if (strcmp(url, ROUTE_GAME_SAVE_SNAP) == 0) {
        char msg[200];
        msg[0] = 0;
        if (game_saves_snap_action(body, len, out, size, msg, sizeof(msg)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "快照操作失败");
        *code = MHD_HTTP_OK;
        return (int)strlen(out);
    }

    /* [CN-FORK] v0.5.2.142 自动存档的手动推 / 拉。**同步**执行（与 /game_save_test、
     * /game_save_upload 同一取舍）：用户手点就是要一个明确结果，挂成后台任务反而
     * 要再配一套轮询。超时在传输层里封着（180s + 低速熔断），不会无限挂。 */
    if (strcmp(url, ROUTE_GAME_SAVE_SYNC) == 0) {
        char msg[240];
        msg[0] = 0;
        if (game_auto_sync_action(body, len, out, size, msg, sizeof(msg)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "ESYNC",
                               msg[0] ? msg : "同步失败");
        *code = MHD_HTTP_OK;
        return (int)strlen(out);
    }

    /* [CN-FORK] v0.5.2.159 删掉远端那一份存档。tid / save 都从 **body** 取
     * （不像 upload 那样摊在 query 上）：这一条没有文件体，两个参数就是一整个请求，
     * 拼成一个 JSON 最省事，也不会出现「query 有 tid、body 里又写一个」的两种真相。
     * ⚠ 同步执行 —— 与 sync / upload 同一取舍，用户手点就是要一个明确结果。
     * 失败一律 400 + 人话：这条路上「删不掉」的原因（连不上 / 没权限 / 协议不支持）
     * 全是用户能自己修的配置问题，回 500 只会让人以为面板坏了。 */
    if (strcmp(url, ROUTE_GAME_SAVE_REMOTE_DELETE) == 0) {
        char rtid[64], rsave[64], msg[240];
        msg[0] = 0;
        /* gr_json_str 是「1 = 取到了」，与其它 helper 的 0=成功 相反，别看错。 */
        if (gr_json_str(body, len, "tid", rtid, sizeof(rtid)) != 1 ||
            gr_json_str(body, len, "save", rsave, sizeof(rsave)) != 1)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               "缺少 tid 或 save");
        if (game_saves_remote_delete(rtid, rsave, msg, sizeof(msg)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EREMOTE",
                               msg[0] ? msg : "删除远程存档失败");
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size, "{\"ok\":true}");
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    if (strcmp(url, ROUTE_GAME_SAVE_UPLOAD) == 0) {
        char msg[192];
        long long files = 0, bytes = 0;
        msg[0] = 0;
        if (!tid || !save)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid 或 save");
        if (!body || !len)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "上传的文件是空的");
        if (game_saves_upload(tid, save, (const unsigned char *)body, len,
                              &files, &bytes, msg, sizeof(msg)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "上传失败");
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size, "{\"ok\":true,\"files\":%lld,\"size\":%lld}", files, bytes);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.154 garlic 兼容导入。与上传走同一个「收完 body 再分发」的
     * 通路（不是 repo_install 那种流式上传），因为镜像只有几 MB，内存里放得下，
     * 而 game_saves_import_garlic 要能一次看全 body 才判得出「是 zip 还是裸镜像」。
     * save 允许缺省（= 第一个用户），name 是**原始文件名** —— 裸镜像时全靠它，
     * 主机认的就是那个名字（`sdimg_<名>`），我们不去猜。 */
    if (strcmp(url, ROUTE_GAME_SAVE_GARLIC) == 0) {
        const char *name = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "name");
        char msg[192];
        long long files = 0;
        msg[0] = 0;
        if (!tid)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (!body || !len)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "导入的文件是空的");
        if (game_saves_import_garlic(tid, save, name, (const unsigned char *)body, len,
                                     &files, msg, sizeof(msg)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "导入失败");
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size, "{\"ok\":true,\"files\":%lld}", files);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    if (strcmp(url, ROUTE_GAME_RENAME) == 0) {
        char btid[32], bname[512], msg[192], shown[GAME_TITLE_MAX_NAME * 4 + 8];
        char esc[GAME_TITLE_MAX_NAME * 6 + 16], pnote[512], pesc[700];
        char hnote[256], hesc[700];
        int persisted = 0, host_ok = 0;
        msg[0] = 0;
        if (!gr_json_str(body, len, "tid", btid, sizeof(btid)) ||
            !gr_json_str(body, len, "name", bname, sizeof(bname)))
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid 或 name");
        if (game_title_set_name(btid, bname, msg, sizeof(msg),
                                &persisted, pnote, sizeof(pnote),
                                &host_ok, hnote, sizeof(hnote)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               msg[0] ? msg : "名称不合法");
        /* 回**存下来的**名字，不是原样回传：set_name 会去控制字符、去首尾空白、
         * 按 UTF-8 字符数截到 128，回一个没截过的会让前端存下一份对不上的值。 */
        game_title_get_name(btid, shown, sizeof(shown));
        pldmgr_json_escape(shown, esc, sizeof(esc));
        pldmgr_json_escape(pnote, pesc, sizeof(pesc));
        pldmgr_json_escape(hnote, hesc, sizeof(hesc));
        *code = MHD_HTTP_OK;
        /* [CN-FORK] v0.5.2.132 persisted = 名字源（游戏包里的 sce_sys/param.json）
         * 也改成功了，SMP 重扫不会回退。false 时 persist_note 说清为什么
         * （镜像游戏 / 系统内置 / 源写失败）。前端据此决定要不要提示 ——
         * 别把 false 当错误：面板层与主机索引层已经生效，改名本身是成功的。
         *
         * [CN-FORK] v0.5.2.165 host_ok / host_note = **主机磁贴数据库**（app.db 平铺列 +
         * appinfo.db）那一步的成败。以前只进日志，前端一律报「已改名」，于是
         * 「库里没写进去」与「写进去了但 Shell 还没重绘」长得一样 —— 用户报的
         * 「改完名 PS5 上没生效」就是被这句假成功盖住的。host_ok=false 时
         * host_note 说原因；host_ok=true 时前端要补一句「主屏要重启 / 待机唤醒才重绘」。 */
        int n = snprintf(out, size,
                         "{\"ok\":true,\"name\":\"%s\","
                         "\"persisted\":%s,\"persist_note\":\"%s\","
                         "\"host_ok\":%s,\"host_note\":\"%s\"}",
                         esc, persisted ? "true" : "false", pesc,
                         host_ok ? "true" : "false", hesc);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.137 名字回填：把面板里所有 name.txt 重放到主机各层。
     * 为什么需要一条批量入口而不是让用户逐款重点一次改名 —— 见 game_title.h 的
     * game_title_sync_all 注释（真机撞到的「改名发生在修复之前，主机侧永远追不上」）。
     * 无请求体、无参数：它读的是我们自己的 name.txt，不接受外部指定要改谁。
     * 返回 ok:true 恒成立（best-effort），成败都在计数与 note 里。 */
    if (strcmp(url, ROUTE_GAME_SYNC) == 0) {
        char smsg[256], sesc[700];
        int scanned = 0, applied = 0, persisted = 0, failed = 0;
        game_title_sync_all(smsg, sizeof(smsg), &scanned, &applied, &persisted, &failed);
        pldmgr_json_escape(smsg, sesc, sizeof(sesc));
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size,
                         "{\"ok\":true,\"scanned\":%d,\"applied\":%d,"
                         "\"persisted\":%d,\"failed\":%d,\"note\":\"%s\"}",
                         scanned, applied, persisted, failed, sesc);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* 磁贴两层图：tid 在 query（契约定的），body 是 PNG 原始字节。 */
    if (strcmp(url, ROUTE_GAME_TILE_ICON) == 0) {
        char msg[192];
        long long bytes = 0;
        msg[0] = 0;
        if (!tid || !*tid)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (!body || !len)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "图片是空的");
        int rc = game_title_set_icon(tid, (const unsigned char *)body, len,
                                     &bytes, msg, sizeof(msg));
        if (rc == -1)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "图片不合法");
        if (rc != 0)
            return gr_json_err(out, size, code, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                               msg[0] ? msg : "写入失败");
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size, "{\"ok\":true,\"bytes\":%lld}", bytes);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    if (strcmp(url, ROUTE_GAME_TILE_BACKGROUND) == 0) {
        char msg[192];
        long long bytes = 0, dds = 0;
        msg[0] = 0;
        if (!tid || !*tid)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (!body || !len)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "图片是空的");
        int rc = game_title_set_bg(tid, (const unsigned char *)body, len,
                                   &bytes, &dds, msg, sizeof(msg));
        if (rc == -1)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "图片不合法");
        if (rc != 0)
            return gr_json_err(out, size, code, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                               msg[0] ? msg : "写入失败");
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size, "{\"ok\":true,\"bytes\":%lld,\"dds_bytes\":%lld}", bytes, dds);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.6.0.179 内置软件换图标 / 换背景：与上面游戏那两条同形
     * （tid 在 query，body 是裸 PNG 字节），错误码分档照同一套：
     *   -1 = 参数/图片不合法（400）、-2 = 连面板这一份都没写下去（500）。
     * **「主机那一列有没有写进去」不在这里分档**，而是由 message 如实带出去：
     * 面板这一份是接口的事实源（列表里的图标与 /app_icon 都读它），主机那层是
     * best-effort —— 但绝不能因此变成一次假成功（`.164` 置顶那个病的教训）。 */
    if (strcmp(url, ROUTE_APP_TILE_ICON) == 0) {
        char msg[192], esc[480];
        long long bytes = 0;
        int host_ok = 0;
        msg[0] = 0;
        if (!tid || !*tid)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (!body || !len)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "图片是空的");
        int rc = app_tile_set_icon(tid, (const unsigned char *)body, len, &bytes, &host_ok,
                                   msg, sizeof(msg));
        if (rc == -1)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "图片不合法");
        if (rc != 0)
            return gr_json_err(out, size, code, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                               msg[0] ? msg : "写入失败");
        pldmgr_json_escape(msg, esc, sizeof(esc));
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size,
                         "{\"ok\":true,\"bytes\":%lld,\"host_ok\":%s,\"message\":\"%s\"}",
                         bytes, host_ok ? "true" : "false", esc);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    if (strcmp(url, ROUTE_APP_TILE_BACKGROUND) == 0) {
        char msg[192], esc[480];
        long long bytes = 0, dds = 0;
        int host_ok = 0;
        msg[0] = 0;
        if (!tid || !*tid)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (!body || !len)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "图片是空的");
        int rc = app_tile_set_bg(tid, (const unsigned char *)body, len, &bytes, &dds, &host_ok,
                                 msg, sizeof(msg));
        if (rc == -1)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "图片不合法");
        if (rc != 0)
            return gr_json_err(out, size, code, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                               msg[0] ? msg : "写入失败");
        pldmgr_json_escape(msg, esc, sizeof(esc));
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size,
                         "{\"ok\":true,\"bytes\":%lld,\"dds_bytes\":%lld,\"host_ok\":%s,"
                         "\"message\":\"%s\"}",
                         bytes, dds, host_ok ? "true" : "false", esc);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.163 背景音乐：POST body 是音频字节，name 在 query 里（前端用 file.name 原样带上）。
     实现落在 src/game_music.c（这一轮新增），接口与 game_title_set_icon 同一档：返回 -1 = 校验失败 / 1 = IO 失败 / 0 = 成功 + bytes。 */
    if (strcmp(url, ROUTE_GAME_MUSIC) == 0) {
        const char *name = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "name");
        char msg[192];
        long long bytes = 0;
        msg[0] = 0;
        if (!tid || !*tid)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (!body || !len)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "音频文件是空的");
        int rc = game_music_set(tid, name, (const unsigned char *)body, len, &bytes, msg, sizeof(msg));
        if (rc == -1)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "音频不合法（仅支持 MP3 / OGG / WAV，体积上限 30MB）");
        if (rc != 0)
            return gr_json_err(out, size, code, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                               msg[0] ? msg : "写入失败");
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size, "{\"ok\":true,\"bytes\":%lld,\"name\":\"%s\"}",
                         bytes, name ? name : "");
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.166 把**已编码好的 ATRAC9** 写进主机的 <sce_sys>/snd0.at9。
     *
     * 为什么单开一条路由而不是并进上面那条：两者收的东西根本不是一回事 ——
     * /game_music 收的是用户原始音频（mp3/ogg/wav，只落我们自己的目录给面板试听），
     * 这一条收的是浏览器里刚编出来的 .at9（只写主机）。分开之后，
     * 「面板试听成功但主机没写进去」与「主机写了但面板没有试听文件」两种半成品
     * 在日志与回包里都能分得清，不会再合成一句「背景音乐已应用」。
     *
     * 主机端写不进去（只读镜像 / 没装进 /user/app）时**回 400**，不回 200 ——
     * 用户这一轮的抱怨就是「界面说成功了、PS5 上没生效」，不能再造一个假成功。 */
    if (strcmp(url, ROUTE_GAME_MUSIC_AT9) == 0) {
        const char *lufs = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "lufs");
        char msg[192], esc[480];
        long long bytes = 0;
        msg[0] = 0;
        if (!tid || !*tid)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (!body || !len)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "音频是空的");
        int rc = game_music_set_host(tid, (const unsigned char *)body, len, lufs,
                                     &bytes, msg, sizeof(msg));
        if (rc != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST,
                               rc == -1 ? "EBAD" : "EHOST",
                               msg[0] ? msg : "主机端没写上");
        pldmgr_json_escape(msg, esc, sizeof(esc));
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size,
                         "{\"ok\":true,\"bytes\":%lld,\"host\":true,\"message\":\"%s\"}",
                         bytes, esc);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.158 「个性化软件」：把某个主机内置软件在主屏上隐藏 / 显示。
     * tid 与 hidden 都在 body 里（与 /game_save_auto 同一口径：query 里根本没有，
     * 去 query 取必然拿到 NULL —— `.116` 在改名那条上踩过）。
     * 状态只落在 app.db 那一列，所以这里不回「配置保存成功」而回**这一行的新状态**。 */
    if (strcmp(url, ROUTE_SYSTEM_APP_VISIBLE) == 0) {
        char btid[32], msg[192], esc[480];
        int hidden = -1;
        msg[0] = 0;
        if (!gr_json_str(body, len, "tid", btid, sizeof(btid)) || !btid[0])
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (gr_json_bool(body, len, "hidden", &hidden) != 0 || hidden < 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               "缺少 hidden（true=隐藏 / false=显示）");
        int rc = app_vis_set(btid, hidden, msg, sizeof(msg));
        /* [CN-FORK] v0.5.2.160 固定条目（所有应用程序 / 两个内容保存库 / 欢迎 / 编辑 /
         * 光碟占位）单独一个码：库里那一列对它们不起作用，写成功也不会有任何变化，
         * 所以既不能回 200（那是谎报），也不能混进 EIO 让人以为主机出故障了。 */
        if (rc == APP_VIS_EFIXED)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EFIXED",
                               msg[0] ? msg : "这条是主屏固定条目，面板改不动它");
        if (rc != 0)
            return gr_json_err(out, size, code,
                               rc == -2 ? MHD_HTTP_INTERNAL_SERVER_ERROR : MHD_HTTP_BAD_REQUEST,
                               rc == -2 ? "EIO" : "EINVAL",
                               msg[0] ? msg : "写入失败");
        pldmgr_json_escape(msg, esc, sizeof(esc));
        *code = MHD_HTTP_OK;
        /* btid 能走到这一行说明 app_vis_set 已经按 A-Z0-9 校验过它，原样回显是安全的。 */
        int n = snprintf(out, size,
                         "{\"ok\":true,\"tid\":\"%s\",\"hidden\":%s,\"message\":\"%s\"}",
                         btid, hidden ? "true" : "false", esc);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.164 「个性化软件」的第二个开关：置顶。
     * v0.6.0.179 起**写主机**（app.db 的 MyHome 固定列表），与 /tile_group_pin
     * 同一个载体；回包里带上 accounts / idx，前端才能说清「写进了几张账号表、
     * 现在排在第几位」—— 以前只写我们自己那份 JSON，界面上看不出任何差别。 */
    if (strcmp(url, ROUTE_SYSTEM_APP_PIN) == 0) {
        char btid[32], msg[192], esc[480];
        int pinned = -1, accounts = 0, gidx = 0;
        msg[0] = 0;
        if (!gr_json_str(body, len, "tid", btid, sizeof(btid)) || !btid[0])
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (gr_json_bool(body, len, "pinned", &pinned) != 0 || pinned < 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               "缺少 pinned（true=置顶 / false=取消置顶）");
        int rc = app_pin_set(btid, pinned, msg, sizeof(msg), &accounts, &gidx);
        if (rc != 0)
            return gr_json_err(out, size, code,
                               rc == -2 ? MHD_HTTP_INTERNAL_SERVER_ERROR : MHD_HTTP_BAD_REQUEST,
                               rc == -2 ? "EHOST" : "EBAD",
                               msg[0] ? msg : "写入失败");
        pldmgr_json_escape(msg, esc, sizeof(esc));
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size,
                         "{\"ok\":true,\"tid\":\"%s\",\"pinned\":%s,\"accounts\":%d,"
                         "\"idx\":%d,\"message\":\"%s\"}",
                         btid, pinned ? "true" : "false", accounts, gidx, esc);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.164 游戏磁贴的收藏 / 置顶。fav / pin 都是可选的（三态）：
     * 前端只拨一颗按钮，没带的那一项必须原样保留 —— 两态入参会把另一项抹掉。 */
    if (strcmp(url, ROUTE_GAME_PREFS_SET) == 0) {
        char btid[32], msg[192];
        int fav = -1, pin = -1, efav = 0, epin = 0;
        msg[0] = 0;
        if (!gr_json_str(body, len, "tid", btid, sizeof(btid)) || !btid[0])
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        gr_json_bool(body, len, "fav", &fav);
        gr_json_bool(body, len, "pin", &pin);
        if (fav < 0 && pin < 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               "fav / pin 至少给一个");
        if (game_prefs_set(btid, fav, pin, &efav, &epin, msg, sizeof(msg)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EBAD",
                               msg[0] ? msg : "写入失败");
        *code = MHD_HTTP_OK;
        /* 回**改完之后**的状态（不是入参）：只拨 fav 时 pin 那一项是「不动」，
         * 把入参原样回出去会把 -1 说成 false，前端下一次渲染就跟着错。 */
        int n = snprintf(out, size, "{\"ok\":true,\"tid\":\"%s\",\"fav\":%s,\"pin\":%s}",
                         btid, efav ? "true" : "false", epin ? "true" : "false");
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.166 置顶 / 取消置顶 —— **写主机的** app.db（tbl_group_<uid>）。
     * 与上面那条 /game_prefs_set 的 pin 字段不是一回事：那个只改我们自己的 JSON，
     * 主机压根不知道，所以用户在 PS5 上看不到任何变化（这正是本轮要修的 bug）。
     * 这里回 accounts（写了几个账号的库）与 idx（置顶后的位置），前端据此说清楚。 */
    if (strcmp(url, ROUTE_TILE_GROUP_PIN) == 0) {
        char btid[32], msg[192], esc[480];
        int pinned = -1, accounts = 0, gidx = 0;
        msg[0] = 0;
        if (!gr_json_str(body, len, "tid", btid, sizeof(btid)) || !btid[0])
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
        if (gr_json_bool(body, len, "pinned", &pinned) != 0 || pinned < 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               "缺少 pinned（true=置顶 / false=取消置顶）");
        if (tile_group_set_pin(btid, pinned, msg, sizeof(msg), &accounts, &gidx) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EHOST",
                               msg[0] ? msg : "主机固定列表写不进去");
        pldmgr_json_escape(msg, esc, sizeof(esc));
        *code = MHD_HTTP_OK;
        int n = snprintf(out, size,
                         "{\"ok\":true,\"tid\":\"%s\",\"pinned\":%s,\"accounts\":%d,"
                         "\"idx\":%d,\"message\":\"%s\"}",
                         btid, pinned ? "true" : "false", accounts, gidx, esc);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    /* [CN-FORK] v0.5.2.166 拖拽排序落地。body = {"order":["TID1","TID2",...]}。
     * 一次请求落两处，因为「拖完松手」在用户眼里就是一个动作：
     *   ① 面板侧顺序（game_prefs.json 的 order 数组）—— 刷新页面后列表还是用户排的样子；
     *   ② 主机侧顺序（app.db 的 MyHome 组 idx）—— 只重排**已经在组里**的条目，
     *      槽位集合不变，没被提到的条目一个字节都不动（理由见 include/tile_group.h）。
     * 两者成败分开报：面板那一半几乎不会失败，主机那一半可能写不进去（只读库），
     * 而「写不进去」必须说出来 —— 用户这一轮的抱怨就是界面报成功、PS5 没变化。 */
    if (strcmp(url, ROUTE_TILE_GROUP_ORDER) == 0) {
        char msg[192], esc2[480];
        const char *tids[64];
        int n = 0, moved = 0;
        msg[0] = 0;
        cJSON *root = cJSON_ParseWithLength(body, len);
        if (!root)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               "请求体不是合法 JSON");
        cJSON *arr = cJSON_GetObjectItem(root, "order");
        if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
            cJSON_Delete(root);
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               "缺少 order 数组");
        }
        /* tids[] 借的是 root 里的字符串指针，所以 root 必须活到两次调用之后才删。 */
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, arr) {
            if (n >= (int)(sizeof(tids) / sizeof(tids[0]))) break;
            if (cJSON_IsString(it) && it->valuestring) tids[n++] = it->valuestring;
        }
        int rc = (n > 0) ? tile_group_reorder(tids, n, msg, sizeof(msg), &moved) : -1;
        int host_ok = (rc == 0);
        char host_note[192];
        snprintf(host_note, sizeof(host_note), "%s", msg);
        /* 主机那一半失败也要把面板顺序存下来（面板顺序本身没有副作用）。 */
        int panel_ok = (n > 0) ? (game_prefs_set_order(tids, n) == 0) : 0;
        cJSON_Delete(root);
        if (!panel_ok)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               "顺序列表不合法或写不进面板");
        pldmgr_json_escape(host_ok ? "主机固定列表已按新顺序重排" : host_note,
                           esc2, sizeof(esc2));
        *code = MHD_HTTP_OK;
        int rn = snprintf(out, size,
                          "{\"ok\":true,\"moved\":%d,\"host_ok\":%s,\"message\":\"%s\"}",
                          moved, host_ok ? "true" : "false", esc2);
        return (rn < 0 || (size_t)rn >= size) ? -1 : rn;
    }

    /* [CN-FORK] v0.6.0.177 多媒体「远程同步」（多媒体管理页的手动推送按钮）。
     * 返回形状与 /game_media_pack 逐字同形（job_id / error / message，200/409/400）：
     * 前端 watchJob 那一套轮询对两条任务是同一份代码，形状不一样就得写两个分支，
     * 而两个分支迟早会走偏（`.137` 的教训就是「分开写的两张表」）。
     * 任务在 mr_sync_start 里就起线程了，HTTP 这边立刻回 job_id —— 传几个 GB
     * 的录屏绝不能让连接站着等（前端 post 超时 20s，连接必断）。 */
    if (strcmp(url, ROUTE_GAME_MEDIA_SYNC) == 0) {
        long long job_id = 0;
        char msg[240], emsg[700];
        msg[0] = 0;
        int rc = mr_sync_start(body, len, &job_id, msg, sizeof(msg));
        pldmgr_json_escape(msg, emsg, sizeof(emsg));
        *code = (rc == 0) ? MHD_HTTP_OK
                          : (rc == -1 ? MHD_HTTP_CONFLICT : MHD_HTTP_BAD_REQUEST);
        int n = snprintf(out, size, "{\"job_id\":%lld,\"error\":\"%s\",\"message\":\"%s\"}",
                         job_id, rc == 0 ? "" : (rc == -1 ? "EBUSY" : "EBAD"), emsg);
        return (n < 0 || (size_t)n >= size) ? -1 : n;
    }

    return gr_json_err(out, size, code, MHD_HTTP_NOT_FOUND, "ENOENT", "没有这条路由");
}

/* ── [UPSTREAM] v0.6.0 统一的响应 / 错误回包助手 ───────────── */
static enum MHD_Result send_out_of_memory(struct MHD_Connection *conn) {
    static const char oom[] = "{\"error\":\"Out of memory\"}";
    struct MHD_Response *resp = create_buffer_response(
        sizeof(oom) - 1, (void *)oom, MHD_RESPMEM_PERSISTENT);
    add_response_header(resp, "Content-Type", "application/json");
    return send_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
}

/* Messages passed here are fixed application strings, not user input. */
static enum MHD_Result send_request_error(struct MHD_Connection *conn,
                                          unsigned int status, const char *message) {
    char json[256];
    snprintf(json, sizeof(json), "{\"error\":\"%s\"}", message);
    struct MHD_Response *resp = create_buffer_response(
        strlen(json), json, MHD_RESPMEM_MUST_COPY);
    add_response_header(resp, "Content-Type", "application/json");
    return send_response(conn, status, resp);
}

static enum MHD_Result send_serialization_error(struct MHD_Connection *conn, char *buffer) {
    free(buffer);
    return send_request_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                              "JSON serialization failed or exceeds the size limit");
}

/* Success is separate from response creation: a failed OOM response must not
 * be mistaken for a successful buffer allocation. */
static int alloc_response_buffer_sized(char **out, size_t size) {
    *out = malloc(size);
    if (*out && size) (*out)[0] = '\0';
    return *out != NULL;
}

static int alloc_response_buffer(char **out) {
    return alloc_response_buffer_sized(out, RESPONSE_BUFFER_SIZE);
}

static int alloc_response_buffer_small(char **out) {
    return alloc_response_buffer_sized(out, SMALL_RESPONSE_BUFFER_SIZE);
}

static int is_safe_filename(const char *name) {
    if (!name || name[0] == '\0') return 0;
    if (strstr(name, "/") || strstr(name, "..")) return 0;
    return 1;
}

static void url_decode_in_place(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2] && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            int h1 = tolower((unsigned char)src[1]);
            int h2 = tolower((unsigned char)src[2]);
            int v1 = isdigit(h1) ? (h1 - '0') : (h1 - 'a' + 10);
            int v2 = isdigit(h2) ? (h2 - '0') : (h2 - 'a' + 10);
            *dst++ = (char)((v1 << 4) | v2);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ── POST request state ────────────────────────────────────── */

struct PostStatus {
    char *data;
    size_t size;
    size_t capacity;
    unsigned int error;
};

struct UploadStatus {
    FILE *fp;
    size_t total_size;
    unsigned int error;
    char filename[256];
    char temp_path[512];
    char mode[32];
};

/* [CN-FORK] v0.6.0 合并：在上游 4 种 kind 基础上补两种 fork 需要的行为 ——
 *   REQUEST_BODY       —— 收完请求体后**继续往下走**路由链（fork 的游戏管理系列
 *                         POST 端点自己解析 body，语义在收完之后才分岔）；
 *   REQUEST_PKG_UPLOAD —— 流式写 PKGMGR_TMP_PATH 后交给 pkgmanager_commit_upload，
 *                         与 /manage:upload（mkstemp → payload_mgr_import_to_storage_mode）
 *                         落点不同，不能共用 REQUEST_UPLOAD。 */
enum RequestKind { REQUEST_CONFIG, REQUEST_SOURCES, REQUEST_UPLOAD, REQUEST_ACTION,
                   REQUEST_BODY, REQUEST_PKG_UPLOAD };

/* [CN-FORK] 本结构自己的 magic：连接结束时回调要同时接待三种 *con_cls ——
 * 本结构、psaccounts 的 acct_conn_t（magic "ACTO"）、以及 REQUEST_MARKER 占位。
 * 先比 magic 再决定谁来回收，避免把对方的结构当自己人 free。 */
#define REQUEST_CONTEXT_MAGIC 0x52455143u /* "REQC" */

struct RequestContext {
    uint32_t magic;
    enum RequestKind kind;
    int finished;
    /* [CN-FORK] REQUEST_BODY 的路由各自有体量上限（存档上传 512MB、JSON 1MB…），
     * 由 gr_post_limit() 在注册时定下，收体时按它设闸。 */
    size_t body_limit;
    union {
        struct PostStatus post;
        struct UploadStatus upload;
    } state;
};

/* Read-only requests need no allocation. This marker is never dereferenced. */
#define REQUEST_MARKER ((void *)1)

void http_request_completed(void *cls, struct MHD_Connection *conn,
                            void **con_cls, enum MHD_RequestTerminationCode reason) {
    (void)cls;
    (void)conn;
    (void)reason;
    if (!con_cls || !*con_cls)
        return;
    void *context = *con_cls;
    *con_cls = NULL;
    if (context == REQUEST_MARKER)
        return;

    /* [CN-FORK] 不是本结构的就是 psaccounts 的流式状态（它自己再认一遍 magic）。 */
    if (*(const uint32_t *)context != REQUEST_CONTEXT_MAGIC) {
        psaccounts_conn_completed(context);
        return;
    }

    struct RequestContext *request = context;
    if (request->kind == REQUEST_UPLOAD || request->kind == REQUEST_PKG_UPLOAD) {
        struct UploadStatus *upload = &request->state.upload;
        if (upload->fp && fclose(upload->fp) != 0)
            pldmgr_log("[PLDMGR] Warning: Failed to close upload during cleanup\n");
        /* Each request owns a unique path. A committed file was renamed out
         * of this path; remove any uncommitted remainder on every exit. */
        if (upload->temp_path[0] && remove(upload->temp_path) != 0 && errno != ENOENT)
            pldmgr_log("[PLDMGR] Warning: Failed to remove upload temp file: %s\n",
                       upload->temp_path);
    } else {
        free(request->state.post.data);
    }
    free(request);
}

static unsigned int check_body_length(struct MHD_Connection *conn, size_t limit) {
    const char *value = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Content-Length");
    if (!value)
        return 0; /* Chunked/unknown length is checked while receiving data. */
    if (!isdigit((unsigned char)*value))
        return MHD_HTTP_BAD_REQUEST;
    errno = 0;
    char *end;
    unsigned long long length = strtoull(value, &end, 10);
    if (errno == ERANGE || *end)
        return MHD_HTTP_BAD_REQUEST;
    return length > limit ? MHD_HTTP_CONTENT_TOO_LARGE : 0;
}

/* [CN-FORK] 带显式上限的版本：fork 的 REQUEST_BODY 路由体量上限各不相同
 * （见 gr_post_limit），不能再写死 HTTP_POST_BODY_LIMIT。 */
static void append_post_data_limit(struct PostStatus *post, const char *data,
                                   size_t size, size_t limit) {
    if (post->error)
        return;
    if (size > limit - post->size) {
        post->error = MHD_HTTP_CONTENT_TOO_LARGE;
        return;
    }
    size_t needed = post->size + size + 1;
    if (needed > post->capacity) {
        size_t capacity = post->capacity ? post->capacity : 4096;
        while (capacity < needed) {
            capacity = capacity > (limit + 1) / 2 ? limit + 1 : capacity * 2;
        }
        char *new_data = realloc(post->data, capacity);
        if (!new_data) {
            post->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }
        post->data = new_data;
        post->capacity = capacity;
    }
    memcpy(post->data + post->size, data, size);
    post->size += size;
    post->data[post->size] = '\0';
}

static void append_post_data(struct PostStatus *post, const char *data, size_t size) {
    append_post_data_limit(post, data, size, HTTP_POST_BODY_LIMIT);
}

static void prepare_upload(struct MHD_Connection *conn, struct UploadStatus *upload) {
    const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
    const char *mode = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "mode");
    if (!mode)
        mode = "new";
    if (!is_safe_filename(filename) || strlen(filename) >= sizeof(upload->filename) ||
        strlen(mode) >= sizeof(upload->mode)) {
        upload->error = MHD_HTTP_BAD_REQUEST;
        return;
    }
    strcpy(upload->filename, filename);
    strcpy(upload->mode, mode);
    upload->error = check_body_length(conn, HTTP_UPLOAD_BODY_LIMIT);
    if (upload->error)
        return;

    if ((mkdir(BASE_DATA_DIR, 0777) != 0 && errno != EEXIST) ||
        (mkdir(PAYLOADS_STORAGE_DIR, 0777) != 0 && errno != EEXIST)) {
        upload->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
        return;
    }
    int n = snprintf(upload->temp_path, sizeof(upload->temp_path),
                     "%s/.upload-XXXXXX", PAYLOADS_STORAGE_DIR);
    if (n < 0 || (size_t)n >= sizeof(upload->temp_path)) {
        upload->temp_path[0] = '\0';
        upload->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
        return;
    }
    int fd = mkstemp(upload->temp_path);
    if (fd < 0) {
        upload->temp_path[0] = '\0'; /* No file was acquired to clean up. */
        upload->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
        return;
    }
    upload->fp = fdopen(fd, "wb");
    if (!upload->fp) {
        close(fd);
        upload->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
        return;
    }
    pldmgr_log("[PLDMGR] Starting upload: %s (mode: %s)\n", filename, mode);
}

/* [CN-FORK] /pkgmanager_upload：手动上传新版 PKG Manager ELF，裸 body 流式写
 * PKGMGR_TMP_PATH（固定落点，pkgmanager_commit_upload 按这个路径接手），
 * filename 只用于猜版本号，取 basename、不参与拼路径。 */
static unsigned int prepare_pkg_upload(struct MHD_Connection *conn, struct UploadStatus *upload) {
    const char *pkg_fname = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
    if (pkg_fname) {
        const char *base = strrchr(pkg_fname, '/');
        base = base ? base + 1 : pkg_fname;
        if (strlen(base) >= sizeof(upload->filename)) return MHD_HTTP_BAD_REQUEST;
        strncpy(upload->filename, base, sizeof(upload->filename) - 1);
        upload->filename[sizeof(upload->filename) - 1] = '\0';
    } else {
        upload->filename[0] = '\0';
    }
    strncpy(upload->temp_path, PKGMGR_TMP_PATH, sizeof(upload->temp_path) - 1);
    upload->temp_path[sizeof(upload->temp_path) - 1] = '\0';
    unsigned int error = check_body_length(conn, HTTP_UPLOAD_BODY_LIMIT);
    if (error) return error;
    if ((mkdir(BASE_DATA_DIR, 0777) != 0 && errno != EEXIST) ||
        (mkdir(PKGMGR_DIR, 0777) != 0 && errno != EEXIST))
        return MHD_HTTP_INTERNAL_SERVER_ERROR;
    upload->fp = fopen(PKGMGR_TMP_PATH, "wb");
    if (!upload->fp) {
        pldmgr_log("[PLDMGR] !!! PKG Manager upload: cannot open %s\n", PKGMGR_TMP_PATH);
        return MHD_HTTP_INTERNAL_SERVER_ERROR;
    }
    pldmgr_log("[PLDMGR] Starting PKG Manager upload to: %s\n", PKGMGR_TMP_PATH);
    return 0;
}

static enum MHD_Result finish_request_error(struct MHD_Connection *conn,
                                           struct RequestContext *request,
                                           unsigned int status) {
    request->finished = 1;
    if (status == MHD_HTTP_CONTENT_TOO_LARGE)
        return send_request_error(conn, status, "Request body exceeds the size limit");
    if (status == MHD_HTTP_BAD_REQUEST)
        return send_request_error(conn, status, "Invalid or empty request");
    return send_request_error(conn, status, "Failed to receive or save request");
}

/* Build complete CSV entries within the output limit, including lines that
 * span read chunks. Excess input is reported rather than silently truncated. */
static int read_autoload_list(char *out, size_t size, int include_delays) {
    if (!size)
        return -1;
    out[0] = '\0';
    FILE *file = fopen(AUTOLOAD_CONFIG_PATH, "r");
    if (!file)
        return errno == ENOENT ? 0 : -1;
    size_t pos = 0;
    int line_start = 1, has_data = 0, skip = 0, result = 0;
    int c;
    while ((c = fgetc(file)) != EOF) {
        if (c == '\0') {
            result = -1;
            break;
        }
        if (c == '\n') {
            line_start = 1; has_data = 0; skip = 0;
            continue;
        }
        if (line_start) {
            skip = !include_delays && c == '!';
            line_start = 0;
        }
        if (c == '\r')
            skip = 1;
        if (skip)
            continue;
        size_t needed = (!has_data && pos ? 1 : 0) + 1;
        if (needed >= size - pos) {
            result = -1;
            break;
        }
        if (!has_data && pos)
            out[pos++] = ',';
        out[pos++] = (char)c;
        has_data = 1;
    }
    out[pos] = '\0';
    if (ferror(file))
        result = -1;
    fclose(file);
    return result;
}

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
    /* [CN-FORK] v0.5.2.116 磁贴三张图都是 <img src> 拉的，一次打开要打三发 */
    if (strcmp(url, ROUTE_GAME_TILE_ICON) == 0) return 1;
    if (strcmp(url, ROUTE_GAME_TILE_BG) == 0) return 1;
    if (strcmp(url, ROUTE_GAME_TILE_BG_DDS) == 0) return 1;
    /* [CN-FORK] v0.6.0.179 内置软件的图标同样是每行一张 <img src>：
     * 弹窗一开就是十几发，降噪（背景预览与两条 reset 一次一发，照旧记日志）。 */
    if (strcmp(url, ROUTE_APP_ICON) == 0) return 1;
    if (strcmp(url, ROUTE_APP_TILE_BG) == 0) return 1;
    /* [CN-FORK] v0.5.2.163 背景音乐读取（GET）。同一个 URL 一路径两方法：
     * POST 走 gr_dispatch_post（已登记），GET 这里返回 1 让它继续走到主处理函数。 */
    if (strcmp(url, ROUTE_GAME_MUSIC) == 0) return 1;
    if (strcmp(url, ROUTE_GAME_MUSIC_RESET) == 0) return 1;
    if (strcmp(url, ROUTE_GAME_MUSIC_AT9) == 0) return 1; /* [CN-FORK] v0.5.2.166 写主机 snd0.at9 */
    if (strcmp(url, ROUTE_FAN_CONTROL) == 0) return 1; /* [CN-FORK] 风扇温控页加载/应用，降噪 */
    if (strcmp(url, ROUTE_SYSTEM_APPS) == 0) return 1; /* [CN-FORK] v0.5.2.158 个性化软件弹窗，降噪 */
    if (strcmp(url, ROUTE_SYSTEM_APP_VISIBLE) == 0) return 1; /* 同上，每次拨开关一次 */
    if (strcmp(url, ROUTE_SYSTEM_APP_PIN) == 0) return 1; /* [CN-FORK] v0.5.2.164 置顶开关，同上 */
    if (strcmp(url, ROUTE_GAME_PREFS) == 0) return 1; /* [CN-FORK] v0.5.2.164 收藏/置顶列表，进页一次 */
    if (strcmp(url, ROUTE_GAME_PREFS_SET) == 0) return 1; /* 同上，每次拨按钮一次 */
    /* [CN-FORK] v0.5.2.166 主屏固定列表：进游戏页拉一次 /tile_group，拨置顶或拖拽排序各一次。 */
    if (strcmp(url, ROUTE_TILE_GROUP) == 0) return 1;
    if (strcmp(url, ROUTE_TILE_GROUP_PIN) == 0) return 1;
    if (strcmp(url, ROUTE_TILE_GROUP_ORDER) == 0) return 1;
    if (strcmp(url, ROUTE_PROCESSES_LIST) == 0) return 1;
    if (strcmp(url, ROUTE_HISTORY_LIST) == 0) return 1;
    /* [CN-FORK] v0.5.1.64 删 /system_stats 轮询（该端点已随硬件信息功能一起下线） */
    if (strcmp(url, ROUTE_PKG_STATUS) == 0) return 1; /* [CN-FORK] PKG安装页轮询，降噪 */
    /* [CN-FORK] v0.5.2.174 「快照活动」—— 存档弹窗开着的时候每 5 秒问一次，
     * 就是为了让「排队重试 → 重试成功」这条链能被看着发生。降噪是必须的。 */
    if (strcmp(url, ROUTE_GAME_SAVE_SNAP_LOG) == 0) return 1;
    if (strcmp(url, ROUTE_SMP_STATUS) == 0) return 1; /* [CN-FORK] v0.5.2.131 SMP 探活，拉起期间轮询，降噪 */
    if (strcmp(url, ROUTE_UPDATE_CHECK) == 0) return 1; /* [UPSTREAM] v0.6.0 更新检查轮询 */
    if (strcmp(url, "/events") == 0) return 1;
    /* [CN-FORK] v0.5.2.99 远程游玩配对的进度查询，界面每 1 秒问一次，降噪；
     * 出码/配对成功/失败/超时那几条仍会记日志（是别的 URL）。 */
    if (strcmp(url, "/api/remoteplay/pin/status") == 0) return 1;
    return 0;
}

/* ── Main request handler ──────────────────────────────────── */

/* [CN-FORK] v0.6.0.179 内置软件的图标 / 背景（GET 侧）。
 *
 * 为什么都是独立函数、不就地写在下面那条 if/else 链里：clang 会把**所有分支**的
 * 局部数组算进同一个栈帧（-fstack-usage 实测那条链已被推到 35KB，而
 * tools/check_stack_frames.sh 的闸门是 32KB/函数，见 /get_config 那段的说明）。
 * 这里几个 KB 级的路径缓冲留在链里就是往闸门上撞，所以一律挪出来。 */

/* GET /app_icon?tid= —— 「每个标题前那个图标」的来源。
 * 有自定义就回我们那一份（<APP_TILE_DIR>/<TID>/icon0.png）；
 * 没有就回**主机自带那张**（tbl_contentinfo.icon0Info 指的文件，内置软件的资产
 * 在只读的 /system_ex 里，只读不写）。
 * 两样都没有（比如光碟占位那几条本来就没有图标）→ 404，前端显示占位方块。 */
static enum MHD_Result app_tile_reply_icon(struct MHD_Connection *conn) {
    const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
    if (!xt || !*xt)
        return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
    char p[1024];
    p[0] = 0;
    if (app_tile_icon_file(xt, p, sizeof(p)) == 0)
        return reply_file(conn, p, "image/png", NULL, 0);
    if (app_tile_host_icon_path(xt, p, sizeof(p)) == 0)
        return reply_file(conn, p, "image/png", NULL, 0);
    return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "拿不到这张图标");
}

/* GET /app_tile_bg?tid= —— 我们存的那份**PNG 预览**（浏览器的 <img> 解不了 DDS）。 */
static enum MHD_Result app_tile_reply_bg(struct MHD_Connection *conn) {
    const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
    if (!xt || !*xt)
        return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
    char p[1024];
    p[0] = 0;
    if (app_tile_bg_png_file(xt, p, sizeof(p)) != 0)
        return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "没有设过自定义背景");
    return reply_file(conn, p, "image/png", NULL, 0);
}

/* GET /app_tile_icon_reset | /app_tile_background_reset?tid= —— 恢复主机原来那一份。
 * **message 一定带回前端**：主机那一列没还原成的时候，界面绝不能只显示一句
 * 「已恢复默认」—— 那时主屏上指着一个不存在的文件，用户必须知道。
 * 返回值分档与 game_*_reset 那两条不同（它们没有失败路径）：
 *   -1 参数不合法 → 400；-2 主机没还原成 → **500 + ok:false**；0 → 200。 */
static enum MHD_Result app_tile_reply_reset(struct MHD_Connection *conn, int is_bg) {
    const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
    if (!xt || !*xt)
        return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid");
    char msg[192], esc[480], body[768];
    msg[0] = 0;
    int rc = is_bg ? app_tile_reset_bg(xt, msg, sizeof(msg))
                   : app_tile_reset_icon(xt, msg, sizeof(msg));
    if (rc == -1)
        return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD",
                                msg[0] ? msg : "这个编号改不了");
    pldmgr_json_escape(msg, esc, sizeof(esc));
    int n = snprintf(body, sizeof(body), "{\"ok\":%s,\"message\":\"%s\"}",
                     rc == 0 ? "true" : "false", esc);
    if (n < 0 || (size_t)n >= sizeof(body)) n = 0;
    return queue_json(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_INTERNAL_SERVER_ERROR,
                      body, (size_t)n);
}

enum MHD_Result http_on_request(void *cls, struct MHD_Connection *conn,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {
    (void)cls;
    (void)version;

    /* [CN-FORK] v0.6.0.177 方法闸门看**表 A**（必须 POST），CSRF 看**总表**
     * （表 A ∪ 变更型 GET）。两者合一的老写法在这里会互相打架：变更型 GET 一旦
     * 进了总表就被强制成 POST，而前端是按 GET 调的，修一个 405 会造出另一个。
     * psaccounts 的 /api/ 一组是**两种方法都收**（api.c 自己认 GET/POST/HEAD），
     * 所以它只在「要不要允许多一个 POST」这点上搭一下闸门，**不**并进本文件的
     * CSRF 层 —— 那一层要求 Origin/Referer/标记，会挡住它自己那套 cookie 会话
     * 的合法调用方（比如从收藏夹 / 书签直接 GET 的场景）。 */
    int want_post = is_post_route(url);
    int psapi = psaccounts_wants(url);
    const char *allow = psapi ? "GET, POST" : (want_post ? "POST" : "GET");
    int mutation = is_mutation_route(url);
    if (!strcmp(method, "OPTIONS")) {
        const char *origin = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Origin");
        const char *host = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Host");
        const char *requested_method = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Access-Control-Request-Method");
        const char *headers = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Access-Control-Request-Headers");
        if (!same_origin(origin, host, 0) || !preflight_headers_valid(headers) ||
            (requested_method && !in_method_list(allow, requested_method)))
            return send_request_error(conn, MHD_HTTP_FORBIDDEN, "Preflight rejected");
        struct MHD_Response *resp = create_buffer_response(0, NULL, MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Access-Control-Allow-Methods", allow);
        add_response_header(resp, "Access-Control-Allow-Headers", "Content-Type, X-PLDMGR-Request");
        return send_response(conn, MHD_HTTP_OK, resp);
    }
    if (!in_method_list(allow, method)) {
        struct MHD_Response *resp = create_buffer_response(0, NULL, MHD_RESPMEM_PERSISTENT);
        /* Allow 头照 RFC 要连 OPTIONS 一起列；而 Access-Control-Allow-Methods
         * 里**不能**带 OPTIONS —— run_http_tests.py 钉死了那条要等于 "POST"。 */
        char allow_hdr[32];
        snprintf(allow_hdr, sizeof(allow_hdr), "%s, OPTIONS", allow);
        add_response_header(resp, "Allow", allow_hdr);
        return send_response(conn, MHD_HTTP_METHOD_NOT_ALLOWED, resp);
    }
    /* Reject before allocating request state, opening an upload, activating
     * browser/autoload state, consuming a body, or calling any backend action. */
    if (mutation && !mutation_authorized(conn))
        return send_request_error(conn, MHD_HTTP_FORBIDDEN, "Mutation origin or request marker rejected");

    /* Set flag that we received a request (meaning browser is open) */
    if (mutation_authorized(conn)) log_server_set_active();

    /* ── [CN-FORK] v0.5.2.99 账号板块接口：整段认领给 psaccounts，路径 /api/… ──
     * 必须放在下面的 *con_cls == NULL 注册块之前：那个块末尾会给所有其它请求
     * 打 *con_cls = REQUEST_MARKER 的占位标记，而 psaccounts 把 *con_cls 当作自己的
     * 状态结构解引用 —— 一旦被打了标记，这里读 magic 就是访问地址 1，直接崩。
     * 认领之后本请求不再走下面任何链，*con_cls 由 psaccounts 自己分配/回收。 */
    if (psaccounts_wants(url)) {
        if (!is_noisy_route(url)) {
            pldmgr_log("[PLDMGR] Request: %s %s\n", method, url);
        }
        return psaccounts_handle(conn, url, method, upload_data,
                                 upload_data_size, con_cls);
    }

    /* The completion callback owns request state on success, error, disconnect,
     * and daemon stop. Keep it alive until MHD finishes the request.
     * ── Initial call for a new request ────────────────────── */
    if (*con_cls == NULL) {
        int upload = strcmp(url, ROUTE_UPLOAD) == 0;
        int config = strcmp(url, ROUTE_SET_CONFIG) == 0 && strcmp(method, "POST") == 0;
        int sources = strcmp(url, ROUTE_SOURCES_SET) == 0 && strcmp(method, "POST") == 0;
        /* [CN-FORK] /pkgmanager_upload：裸 body 流式写 PKGMGR_TMP_PATH */
        int pkg_upload = strcmp(method, "POST") == 0 &&
                         strncmp(url, ROUTE_PKG_UPLOAD, strlen(ROUTE_PKG_UPLOAD)) == 0;
        /* [CN-FORK] 游戏管理系列 POST 端点：各自体量上限见 gr_post_limit()；
         * /game_media_pack 是 512KB 的 JSON。这类请求体**收完后**才分岔，见下方
         * REQUEST_BODY 的处置。 */
        size_t fork_body_limit = 0;
        if (strcmp(method, "POST") == 0) {
            fork_body_limit = gr_post_limit(url);
            if (!fork_body_limit && strcmp(url, ROUTE_GAME_MEDIA_PACK) == 0)
                fork_body_limit = (size_t)512 << 10;
        }
        int body = fork_body_limit != 0;
        if (upload || config || sources || pkg_upload || mutation || body) {
            struct RequestContext *request = malloc(sizeof(*request));
            if (!request) {
                *con_cls = REQUEST_MARKER;
                return send_out_of_memory(conn);
            }
            memset(request, 0, sizeof(*request));
            request->magic = REQUEST_CONTEXT_MAGIC;
            request->kind = upload ? REQUEST_UPLOAD
                         : config ? REQUEST_CONFIG
                         : sources ? REQUEST_SOURCES
                         : pkg_upload ? REQUEST_PKG_UPLOAD
                         : body ? REQUEST_BODY
                         : REQUEST_ACTION;
            request->body_limit = body ? fork_body_limit : HTTP_POST_BODY_LIMIT;
            *con_cls = request;
            unsigned int error;
            if (upload) {
                prepare_upload(conn, &request->state.upload);
                error = request->state.upload.error;
            } else if (pkg_upload) {
                error = prepare_pkg_upload(conn, &request->state.upload);
            } else {
                error = check_body_length(conn, request->body_limit);
                request->state.post.error = error;
            }
            if (error)
                return finish_request_error(conn, request, error);
        } else {
            *con_cls = REQUEST_MARKER;
        }
        return MHD_YES;
    }

    if (*con_cls != REQUEST_MARKER) {
        struct RequestContext *request = *con_cls;
        if (request->finished) {
            *upload_data_size = 0;
            return MHD_YES;
        }
        if (request->kind == REQUEST_ACTION) {
            struct PostStatus *post = &request->state.post;
            if (*upload_data_size) {
                if (*upload_data_size > HTTP_POST_BODY_LIMIT - post->size) {
                    *upload_data_size = 0;
                    return finish_request_error(conn, request, MHD_HTTP_CONTENT_TOO_LARGE);
                }
                post->size += *upload_data_size;
                *upload_data_size = 0;
                return MHD_YES;
            }
            request->finished = 1;
        } else if (request->kind == REQUEST_BODY) {
            /* [CN-FORK] 收完体后**不**在这里分岔：置 finished 后继续往下走，
             * 由路由链里的游戏管理 POST 处理器读 request->state.post 自己分发。 */
            struct PostStatus *post = &request->state.post;
            if (*upload_data_size) {
                append_post_data_limit(post, upload_data, *upload_data_size,
                                       request->body_limit);
                *upload_data_size = 0;
                if (post->error)
                    return finish_request_error(conn, request, post->error);
                return MHD_YES;
            }
            request->finished = 1;
        } else if (request->kind != REQUEST_UPLOAD && request->kind != REQUEST_PKG_UPLOAD) {
            struct PostStatus *post = &request->state.post;
            if (*upload_data_size) {
                append_post_data(post, upload_data, *upload_data_size);
                *upload_data_size = 0;
                if (post->error)
                    return finish_request_error(conn, request, post->error);
                return MHD_YES;
            }
            if (!post->size)
                return finish_request_error(conn, request, MHD_HTTP_BAD_REQUEST);
            int rc = request->kind == REQUEST_CONFIG
                ? config_handle_set_json(post->data) : sources_save(post->data, post->size);
            if (rc != 0)
                return finish_request_error(conn, request, rc == CONFIG_INVALID ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_INTERNAL_SERVER_ERROR);
            request->finished = 1;
            struct MHD_Response *resp = create_buffer_response(
                strlen(MSG_OK), (void *)MSG_OK, MHD_RESPMEM_PERSISTENT);
            add_response_header(resp, "Content-Type", "text/plain");
            return send_response(conn, MHD_HTTP_OK, resp);
        }

        if (request->kind == REQUEST_UPLOAD) {
            struct UploadStatus *status = &request->state.upload;
            if (*upload_data_size) {
                if (*upload_data_size > HTTP_UPLOAD_BODY_LIMIT - status->total_size) {
                    status->error = MHD_HTTP_CONTENT_TOO_LARGE;
                } else {
                    size_t written = fwrite(upload_data, 1, *upload_data_size, status->fp);
                    status->total_size += written;
                    if (written != *upload_data_size)
                        status->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
                }
                *upload_data_size = 0;
                if (status->error)
                    return finish_request_error(conn, request, status->error);
                return MHD_YES;
            }

            /* Both operations must run: a failed flush must not skip fclose. */
            if (fflush(status->fp) != 0)
                status->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
            if (fclose(status->fp) != 0)
                status->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
            status->fp = NULL;
            if (!status->total_size && !status->error)
                status->error = MHD_HTTP_BAD_REQUEST;
            if (!status->error) {
                char msg[256] = "";
                char out_slot[128];
                if (payload_mgr_import_to_storage_mode(status->filename, status->temp_path,
                                                      status->mode, "web_upload", "",
                                                      msg, sizeof(msg), out_slot, sizeof(out_slot)) != 0) {
                    pldmgr_log("[PLDMGR] !!! Failed to commit upload: %s\n", msg);
                    status->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
                } else {
                    /* Import consumed the temp file. Relinquish its pathname so
                     * completion cannot unlink a later owner of the same name. */
                    status->temp_path[0] = '\0';
                }
            }
            pldmgr_log("[PLDMGR] Upload finished. Total bytes: %zu, Error: %u\n",
                       status->total_size, status->error);
            if (status->error)
                return finish_request_error(conn, request, status->error);
            request->finished = 1;
            struct MHD_Response *resp = create_buffer_response(
                strlen(MSG_OK), (void *)MSG_OK, MHD_RESPMEM_PERSISTENT);
            add_response_header(resp, "Content-Type", "text/plain");
            return send_response(conn, MHD_HTTP_OK, resp);
        }

        if (request->kind == REQUEST_PKG_UPLOAD) {
            /* [CN-FORK] 流式写 PKGMGR_TMP_PATH → pkgmanager_commit_upload */
            struct UploadStatus *status = &request->state.upload;
            if (*upload_data_size) {
                if (*upload_data_size > HTTP_UPLOAD_BODY_LIMIT - status->total_size) {
                    status->error = MHD_HTTP_CONTENT_TOO_LARGE;
                } else {
                    size_t written = fwrite(upload_data, 1, *upload_data_size, status->fp);
                    status->total_size += written;
                    if (written != *upload_data_size)
                        status->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
                }
                *upload_data_size = 0;
                if (status->error)
                    return finish_request_error(conn, request, status->error);
                return MHD_YES;
            }
            if (status->fp) {
                if (fflush(status->fp) != 0) status->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
                if (fclose(status->fp) != 0) status->error = MHD_HTTP_INTERNAL_SERVER_ERROR;
                status->fp = NULL;
            }
            int err = status->error;
            /* [CN-FORK] 栈闸门（32KB/函数）：本分支三块缓冲改堆分配，
             * 否则会被 clang 累加进 http_on_request 的巨型栈帧。 */
            char *buf = malloc(256 + 640 + 512);
            if (!buf) return send_out_of_memory(conn);
            char *result = buf;       /* 256 */
            char *json = buf + 256;   /* 640 */
            char *esc = buf + 896;    /* 512 */
            result[0] = '\0';
            if (!err) {
                if (pkgmanager_commit_upload(status->temp_path, status->filename,
                                             result, 256) != 0)
                    err = MHD_HTTP_BAD_REQUEST;
                else
                    status->temp_path[0] = '\0'; /* commit 已接手，别再删 */
            } else {
                remove(status->temp_path);
                status->temp_path[0] = '\0';
                snprintf(result, 256, "write error during upload");
            }
            pldmgr_log("[PLDMGR] PKG Manager upload finished: %zu bytes, err=%d\n",
                       status->total_size, err);
            request->finished = 1;
            pldmgr_json_escape(result, esc, 512);
            if (err)
                snprintf(json, 640, "{\"ok\":false,\"message\":\"%s\"}", esc);
            else
                snprintf(json, 640, "{\"ok\":true,\"version\":\"%s\"}", esc);
            struct MHD_Response *resp = create_buffer_response(
                strlen(json), (void *)json, MHD_RESPMEM_MUST_COPY);
            free(buf);
            add_response_header(resp, "Content-Type", "application/json");
            return send_response(conn, err ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_OK, resp);
        }
    }

    /* ── [CN-FORK] 游戏管理系列 POST 端点：请求体已由 REQUEST_BODY 收好 ──
     * 九条端点（删源 / 存档自动与连通性 / 存档上传 / 改名 / 磁贴两层图 / 挂载 /
     * 卸载挂载 / 卸载安装）共用同一个累加器，**收完才分发**。与 /game_media_pack
     * 那条刻意保持同一套结构：MHD 是把请求体切成若干块送进来的，只有
     * upload_data_size==0 那一发才代表「体收完了」。 */
    if (strcmp(method, "POST") == 0 && *con_cls != NULL && *con_cls != REQUEST_MARKER) {
        struct RequestContext *request = *con_cls;
        if (request->kind == REQUEST_BODY) {
            struct PostStatus *status = &request->state.post;

            if (strcmp(url, ROUTE_GAME_MEDIA_PACK) == 0) {
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
                request->finished = 1;
                char emsg[400];
                pldmgr_json_escape(msg, emsg, sizeof(emsg));
                char out[600];
                int on = snprintf(out, sizeof(out), "{\"job_id\":%lld,\"error\":\"%s\","
                                   "\"message\":\"%s\"}",
                                   job_id, rc == 0 ? "" : (rc == -1 ? "EBUSY" : "EBAD"), emsg);
                if (on < 0 || (size_t)on >= sizeof(out)) on = 0;
                return queue_json(conn,
                                  rc == 0 ? MHD_HTTP_OK
                                          : (rc == -1 ? MHD_HTTP_CONFLICT : MHD_HTTP_BAD_REQUEST),
                                  out, (size_t)on);
            }

            /* [CN-FORK] 4KB 响应缓冲走堆：本函数是巨型 if/else 链，clang 会把所有
             * 分支的局部数组算进同一个栈帧（见 /get_config 处的说明）。 */
            char *out = malloc(4096);
            if (!out) {
                request->finished = 1;
                return send_out_of_memory(conn);
            }
            int code = MHD_HTTP_INTERNAL_SERVER_ERROR;
            int on;
            out[0] = '\0';
            if (status->error) {
                on = snprintf(out, 4096,
                              "{\"error\":\"EBAD\",\"message\":\"请求体过大或内存不足\"}");
                code = MHD_HTTP_CONTENT_TOO_LARGE;
            } else {
                on = gr_dispatch_post(conn, url, status->data, status->size,
                                      out, 4096, &code);
            }
            request->finished = 1;
            if (on < 0 || (size_t)on >= 4096) {
                /* 走到这里说明上面某个 snprintf 被截断了 —— 半截 JSON 比一条
                 * 通用错误严重得多：前端 .json() 直接抛，用户看到的是白屏式失败。 */
                on = snprintf(out, 4096,
                              "{\"error\":\"EIO\",\"message\":\"响应过长\"}");
                code = MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
            struct MHD_Response *r = MHD_create_response_from_buffer(
                (size_t)on, (void *)out, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(r, "Content-Type", "application/json");
            MHD_add_response_header(r, "Cache-Control", "no-store");
            add_cors_headers(r);
            enum MHD_Result gr = MHD_queue_response(conn, code, r);
            MHD_destroy_response(r);
            free(out);
            return gr;
        }
    }

    /* ── Log significant requests ──────────────────────────── */
    if (!is_noisy_route(url)) {
        pldmgr_log("[PLDMGR] Request: %s %s\n", method, url);
    }

    struct MHD_Response *resp = NULL;
    int http_status = MHD_HTTP_OK;

    /* ── USB routes ────────────────────────────────────────── */
    if (strcmp(url, ROUTE_USB_MOVE_CHECK) == 0) {
        const char *path = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "path");
        if (path) {
            char *resp_buf;
            if (!alloc_response_buffer_small(&resp_buf))
                return send_out_of_memory(conn);
            int rc = payload_mgr_usb_check(path, resp_buf, SMALL_RESPONSE_BUFFER_SIZE);
            if (!resp_buf[0]) return send_serialization_error(conn, resp_buf);
            resp = create_buffer_response(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
            add_response_header(resp, "Content-Type", "application/json");

            return send_response(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
        } else {
            const char *err = "{\"error\":\"Missing path\"}";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            add_response_header(resp, "Content-Type", "application/json");
        }

        return send_response(conn, MHD_HTTP_BAD_REQUEST, resp);
    }

    if (strcmp(url, ROUTE_USB_MOVE_PERFORM) == 0) {
        const char *path = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "path");
        const char *mode = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "mode");
        const char *overwrite_str = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "overwrite");
        if (!mode) {
            int overwrite = (overwrite_str && strcmp(overwrite_str, "true") == 0) ? 1 : 0;
            mode = overwrite ? "replace" : "new";
        }
        const char *keep_original_str = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "keep_original");
        int keep_original = (keep_original_str && strcmp(keep_original_str, "true") == 0) ? 1 : 0;
        if (path) {
            char *resp_buf;
            if (!alloc_response_buffer_small(&resp_buf))
                return send_out_of_memory(conn);
            int rc = payload_mgr_usb_move(path, mode, keep_original, resp_buf, SMALL_RESPONSE_BUFFER_SIZE);
            if (!resp_buf[0]) return send_serialization_error(conn, resp_buf);
            resp = create_buffer_response(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
            add_response_header(resp, "Content-Type", "application/json");

            return send_response(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
        } else {
            const char *err = "{\"error\":\"Missing path\"}";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            add_response_header(resp, "Content-Type", "application/json");
        }

        return send_response(conn, MHD_HTTP_BAD_REQUEST, resp);
    }

    /* ── Static assets ─────────────────────────────────────── */
    if (strcmp(url, ROUTE_INDEX) == 0 || strcmp(url, ROUTE_INDEX_HTML) == 0) {
        resp = create_buffer_response(assets_index_html_len,
                                               (void *)assets_index_html,
                                               MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Content-Type", "text/html");
        add_response_header(resp, "Cache-Control", "no-cache, must-revalidate");
    } else if (strcmp(url, ROUTE_CACHE_MANIFEST) == 0) {
        resp = create_buffer_response(assets_cache_appcache_len,
                                               (void *)assets_cache_appcache,
                                               MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Content-Type", "text/cache-manifest");
        add_response_header(resp, "Cache-Control", "no-cache, must-revalidate");
    } else if (strcmp(url, "/favicon.svg") == 0) {
        resp = create_buffer_response(assets_favicon_svg_len,
                                               (void *)assets_favicon_svg,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "image/svg+xml");
        MHD_add_response_header(resp, "Cache-Control", "no-cache, must-revalidate"); /* [CN-FORK] 图标随版本即时刷新，不吃 7 天缓存 */
    } else if (strcmp(url, "/icon.png") == 0) {
        resp = create_buffer_response(assets_icon_png_len,
                                               (void *)assets_icon_png,
                                               MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "image/png");
        MHD_add_response_header(resp, "Cache-Control", "no-cache, must-revalidate"); /* [CN-FORK] 图标随版本即时刷新，不吃 7 天缓存 */

    /* ── API routes ────────────────────────────────────────── */
    } else if (strcmp(url, ROUTE_UPDATE_CHECK) == 0) {
        const char *resp_buf = "{\"status\":\"none\"}";
        resp = create_buffer_response(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Content-Type", "application/json");

        return send_response(conn, MHD_HTTP_OK, resp);
    } else if (strcmp(url, ROUTE_CHECK) == 0) {
        const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
        if (!filename) {
            const char *err = "{\"error\":\"Missing filename\"}";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            add_response_header(resp, "Content-Type", "application/json");

            return send_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        if (!is_safe_filename(filename)) {
            const char *err = "{\"error\":\"Invalid filename\"}";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            add_response_header(resp, "Content-Type", "application/json");

            return send_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        char *resp_buf;
        if (!alloc_response_buffer_small(&resp_buf))
            return send_out_of_memory(conn);
        payload_mgr_check_existing(filename, resp_buf, SMALL_RESPONSE_BUFFER_SIZE);
        if (!resp_buf[0]) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");

        return send_response(conn, MHD_HTTP_OK, resp);
    } else if (strcmp(url, ROUTE_LIST_PAYLOADS) == 0) {
        char *resp_buf;
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
        size_t len = payload_mgr_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        if (!len || len >= RESPONSE_BUFFER_SIZE) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(len, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_PROCESSES_LIST) == 0) {
        char *resp_buf;
        if (!alloc_response_buffer_small(&resp_buf))
            return send_out_of_memory(conn);
        size_t len = process_list_json(resp_buf, SMALL_RESPONSE_BUFFER_SIZE);
        if (!len || len >= SMALL_RESPONSE_BUFFER_SIZE) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(len, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_HISTORY_LIST) == 0) {
        char *json = malloc(SMALL_RESPONSE_BUFFER_SIZE);
        if (json) {
            size_t len = history_mgr_to_json(json, SMALL_RESPONSE_BUFFER_SIZE);
            if (!len) return send_serialization_error(conn, json);
            resp = create_buffer_response(len, (void *)json, MHD_RESPMEM_MUST_FREE);
            add_response_header(resp, "Content-Type", "application/json");

            return send_response(conn, MHD_HTTP_OK, resp);
        } else {
            return send_out_of_memory(conn);
        }
    } else if (strcmp(url, ROUTE_PROCESS_KILL) == 0) {
        const char *pid_str = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "pid");
        if (!pid_str) {
            const char *err = "{\"ok\":false,\"message\":\"Missing pid\"}";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            add_response_header(resp, "Content-Type", "application/json");

            return send_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        int pid = atoi(pid_str);
        int rc = process_kill(pid);

        pldmgr_log("[PLDMGR] Process kill requested for PID %d: %s\n", pid, rc == 0 ? "SUCCESS" : "FAILED");

        char json_resp[256];
        snprintf(json_resp, sizeof(json_resp), "{\"ok\":%s,\"message\":\"%s\"}",
                 rc == 0 ? "true" : "false", rc == 0 ? "Killed" : "Failed to kill");
        resp = create_buffer_response(strlen(json_resp), (void *)json_resp, MHD_RESPMEM_MUST_COPY);
        add_response_header(resp, "Content-Type", "application/json");

        return send_response(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    } else if (strcmp(url, ROUTE_REPO_CACHE_CLEAR) == 0) {
        int result = sources_clear_cache();
        const char *body = result ? "{\"ok\":false}" : "{\"ok\":true}";
        resp = create_buffer_response(strlen(body), (void *)body, MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Content-Type", "application/json");
        return send_response(conn, result ? MHD_HTTP_INTERNAL_SERVER_ERROR : MHD_HTTP_OK, resp);
    } else if (strcmp(url, ROUTE_REPO_ICONS) == 0) {
        const char *source = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "source");
        char *buffer;
        if (!alloc_response_buffer(&buffer)) return send_out_of_memory(conn);
        size_t length = sources_icons_json(source, buffer, RESPONSE_BUFFER_SIZE);
        if (!length) {
            free(buffer);
            resp = create_buffer_response(strlen("{\"icons\":{}}"), (void *)"{\"icons\":{}}", MHD_RESPMEM_PERSISTENT);
            add_response_header(resp, "Content-Type", "application/json");
            return send_response(conn, MHD_HTTP_NOT_FOUND, resp);
        }
        resp = create_buffer_response(length, buffer, MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
        add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_REPO_LIST) == 0) {
        char *resp_buf;
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
        PldmgrConfig cfg;
        config_read(&cfg);
        size_t len;
        if (cfg.multi_sources_enabled)
            len = sources_multi_repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 0);
        else
            len = repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 0);
        if (!len || len >= RESPONSE_BUFFER_SIZE) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(len, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_REPO_REFRESH) == 0) {
        char *resp_buf;
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
        PldmgrConfig cfg;
        config_read(&cfg);
        size_t len2;
        if (cfg.multi_sources_enabled)
            len2 = sources_multi_repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 1);
        else
            len2 = repository_list_json(resp_buf, RESPONSE_BUFFER_SIZE, 1);
        if (!len2 || len2 >= RESPONSE_BUFFER_SIZE) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(len2, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_REPO_INSTALL) == 0) {
        const char *id = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "id");
        const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
        const char *source_url = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "source_url");
        if (!source_url) {
            source_url = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "repo_url");
        }
        const char *mode = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "mode");
        const char *replace_slot = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "replace_slot");

        char derived_id[128] = "";
        if (!id || id[0] == '\0') {
            if (filename && filename[0]) {
                slot_local_key(filename, derived_id, sizeof(derived_id));
                id = derived_id;
            } else {
                const char *err = "{\"ok\":false,\"message\":\"Missing id parameter\"}";
                resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
                add_response_header(resp, "Content-Type", "application/json");

                return send_response(conn, MHD_HTTP_BAD_REQUEST, resp);
            }
        }

        char *resp_buf;
        if (!alloc_response_buffer_small(&resp_buf))
            return send_out_of_memory(conn);

        int rc = sources_repo_install(source_url, id, mode, replace_slot, resp_buf, SMALL_RESPONSE_BUFFER_SIZE);
        if (!resp_buf[0]) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");

        return send_response(conn, rc == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
    } else if (strcmp(url, ROUTE_SOURCES_LIST) == 0) {
        char *resp_buf;
        if (!alloc_response_buffer_sized(&resp_buf, SOURCES_RESPONSE_BUFFER_SIZE))
            return send_out_of_memory(conn);
        if (sources_list_json(resp_buf, SOURCES_RESPONSE_BUFFER_SIZE) != 0) resp_buf[0] = '\0';
        if (!resp_buf[0]) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_SOURCES_ADD) == 0) {
        const char *src_url = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "url");
        if (!src_url || !src_url[0]) {
            const char *err = "{\"ok\":false,\"message\":\"Missing url parameter\"}";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            add_response_header(resp, "Content-Type", "application/json");

            return send_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        char msg_buf_src[512] = "";
        int rc_src = sources_add(src_url, msg_buf_src, sizeof(msg_buf_src));
        char json_resp_src[4096];
        if (rc_src == 0) {
            char name_escaped[6 * 512];
            pldmgr_json_escape(msg_buf_src, name_escaped, sizeof(name_escaped));
            json_format(json_resp_src, sizeof(json_resp_src),
                     "{\"ok\":true,\"name\":\"%s\"}", name_escaped);
        } else {
            char msg_escaped[6 * 512];
            pldmgr_json_escape(msg_buf_src, msg_escaped, sizeof(msg_escaped));
            json_format(json_resp_src, sizeof(json_resp_src),
                     "{\"ok\":false,\"message\":\"%s\"}", msg_escaped);
        }
        if (!json_resp_src[0]) return send_request_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "JSON serialization failed or exceeds the size limit");
        resp = create_buffer_response(strlen(json_resp_src), (void *)json_resp_src, MHD_RESPMEM_MUST_COPY);
        add_response_header(resp, "Content-Type", "application/json");

        return send_response(conn, rc_src == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
    } else if (strcmp(url, ROUTE_SOURCES_REMOVE) == 0) {
        const char *idx_str = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "index");
        if (!idx_str) {
            const char *err = "{\"ok\":false,\"message\":\"Missing index parameter\"}";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_MUST_COPY);
            add_response_header(resp, "Content-Type", "application/json");

            return send_response(conn, MHD_HTTP_BAD_REQUEST, resp);
        }
        int idx = atoi(idx_str);
        char msg_rm[256] = "";
        int rc_rm = sources_remove(idx, msg_rm, sizeof(msg_rm));
        char json_rm[512];
        char msg_rm_e[6 * 512];
        pldmgr_json_escape(msg_rm, msg_rm_e, sizeof(msg_rm_e));
        json_format(json_rm, sizeof(json_rm), "{\"ok\":%s,\"message\":\"%s\"}",
                 rc_rm == 0 ? "true" : "false", msg_rm_e);
        if (!json_rm[0]) return send_request_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "JSON serialization failed or exceeds the size limit");
        resp = create_buffer_response(strlen(json_rm), (void *)json_rm, MHD_RESPMEM_MUST_COPY);
        add_response_header(resp, "Content-Type", "application/json");

        return send_response(conn, rc_rm == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, resp);
    } else if (strncmp(url, ROUTE_LOAD_PAYLOAD, strlen(ROUTE_LOAD_PAYLOAD)) == 0) {
        const char *arg = url + strlen(ROUTE_LOAD_PAYLOAD);
        char clean_arg[512];
        strncpy(clean_arg, arg, sizeof(clean_arg) - 1);
        clean_arg[sizeof(clean_arg) - 1] = '\0';
        url_decode_in_place(clean_arg);

        char resolved_path[512];
        if (strlen(arg) >= sizeof(clean_arg) || strstr(arg, "%00")) {
            const char *err = "Invalid payload path\n";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
            http_status = MHD_HTTP_BAD_REQUEST;
        } else if (is_pldmgr_name(clean_arg)) {
            pldmgr_log("[PLDMGR] Refusing to launch payload manager itself: %s\n", clean_arg);
            const char *err = "Payload Manager cannot be launched as a payload\n";
            resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
            http_status = MHD_HTTP_BAD_REQUEST;
        } else if (slot_resolve(clean_arg, resolved_path, sizeof(resolved_path)) == 0) {
            if (is_pldmgr_name(resolved_path)) {
                pldmgr_log("[PLDMGR] Refusing to launch payload manager itself: %s\n", resolved_path);
                const char *err = "Payload Manager cannot be launched as a payload\n";
                resp = create_buffer_response(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
                http_status = MHD_HTTP_BAD_REQUEST;
            } else if (ps5_launch_elf(resolved_path) == 0) {
                resp = create_buffer_response(strlen(MSG_OK), (void *)MSG_OK,
                                                       MHD_RESPMEM_PERSISTENT);
            } else {
                const char *err = "Failed to launch payload\n";
                resp = create_buffer_response(strlen(err), (void *)err,
                                                       MHD_RESPMEM_PERSISTENT);
                http_status = MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
        } else {
            pldmgr_log("[PLDMGR] !!! Payload slot/path rejected or not found: %s (raw: %s)\n", clean_arg, arg);
            const char *err = "Invalid payload name\n";
            resp = create_buffer_response(strlen(err), (void *)err,
                                                   MHD_RESPMEM_PERSISTENT);
            http_status = MHD_HTTP_BAD_REQUEST;
        }
        add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_DELETE) == 0) {
        const char *slot =
            MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "slot");
        const char *filename =
            MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
        const char *target = slot ? slot : filename;
        if (target) {
            if (strstr(target, "..")) {
                const char *err = "Invalid payload identifier\n";
                resp = create_buffer_response(strlen(err), (void *)err,
                                                       MHD_RESPMEM_PERSISTENT);
                http_status = MHD_HTTP_BAD_REQUEST;
            } else {
                if (payload_mgr_delete_payload(target) == 0) {
                    pldmgr_log("[PLDMGR] Deleted payload: %s\n", target);
                    resp = create_buffer_response(strlen(MSG_OK), (void *)MSG_OK,
                                                           MHD_RESPMEM_PERSISTENT);
                } else {
                    const char *err = "Failed to delete payload\n";
                    resp = create_buffer_response(strlen(err), (void *)err,
                                                           MHD_RESPMEM_PERSISTENT);
                    http_status = MHD_HTTP_INTERNAL_SERVER_ERROR;
                }
            }
        } else {
            const char *err = "Missing slot parameter\n";
            resp = create_buffer_response(strlen(err), (void *)err,
                                                   MHD_RESPMEM_PERSISTENT);
            http_status = MHD_HTTP_BAD_REQUEST;
        }
        add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_SHUTDOWN) == 0) {
        const char *msg = "NayePlmTools Core shutting down...\n"; /* [CN-FORK] 对外名称 */
        pldmgr_log("[PLDMGR] %s", msg);
        resp = create_buffer_response(strlen(msg), (void *)msg,
                                               MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Content-Type", "text/plain");
        http_keep_running = 0;
    } else if (strcmp(url, ROUTE_LOG) == 0) {
        char *resp_buf;
        if (!alloc_response_buffer_sized(&resp_buf, LOG_RESPONSE_BUFFER_SIZE))
            return send_out_of_memory(conn);
        size_t pos = log_build_json(resp_buf, LOG_RESPONSE_BUFFER_SIZE);
        if (!pos || pos >= LOG_RESPONSE_BUFFER_SIZE) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(pos, (void *)resp_buf,
                                               MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_VERSION) == 0) {
        resp = create_buffer_response(
            strlen(MENU_VERSION), (void *)MENU_VERSION, MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_GETIP) == 0) {
        char ip[64];
        if (pldmgr_get_local_ip(ip, sizeof(ip)) != 0) {
            strcpy(ip, "0.0.0.0");
        }
        resp = create_buffer_response(strlen(ip), (void *)ip,
                                               MHD_RESPMEM_MUST_COPY);
        add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_AUTOLOAD_STATUS) == 0) {
        if (mutation_authorized(conn)) pldmgr_autoload_frontend_ready();
        AutoloadSnapshot snapshot;
        pldmgr_autoload_get_snapshot(&snapshot);
        /* [CN-FORK] 栈闸门（32KB/函数）：这两块转义缓冲（6*128 + 6*4096 = 25344 B）
         * 与上面的 AutoloadSnapshot（8320 B）落在同一分支里，会把 http_on_request
         * 的栈帧顶到 33.8KB —— 越过 tools/check_stack_frames.sh 的闸门（真机每条
         * 连接线程只有 64KB 栈）。改走堆，一次分配、指针切段。 */
        char *esc = malloc(6 * 128 + 6 * 4096);
        if (!esc) return send_out_of_memory(conn);
        char *current_escaped = esc;            /* 6 * 128  */
        char *list_escaped    = esc + 6 * 128;  /* 6 * 4096 */
        if (pldmgr_json_escape(snapshot.current, current_escaped, 6 * 128) ||
            pldmgr_json_escape(snapshot.list, list_escaped, 6 * 4096)) {
            free(esc);
            return send_request_error(conn,MHD_HTTP_INTERNAL_SERVER_ERROR,"Invalid autoload snapshot");
        }

        char *resp_buf;
        if (!alloc_response_buffer_small(&resp_buf)) {
            free(esc);
            return send_out_of_memory(conn);
        }
        json_format(resp_buf, SMALL_RESPONSE_BUFFER_SIZE,
                 "{\"remaining\":%d,\"remaining_ms\":%lld,\"total\":%d,\"done\":%d,\"current\":\"%s\","
                 "\"list\":\"%s\",\"delay\":%d,\"SCAN_USB_PAYLOADS\":%s,"
                 "\"state\":\"%s\",\"successful\":%d,\"failed\":%d,\"skipped\":%d,\"outcomes\":\"%s\",\"entry_index\":%d,\"entry_duration_ms\":%d,\"entry_remaining_ms\":%lld}",
                 snapshot.remaining, snapshot.remaining_ms, snapshot.total, snapshot.done, current_escaped,
                 list_escaped, snapshot.delay, snapshot.scan_usb_payloads ? "true" : "false",
                 pldmgr_autoload_state_name(snapshot.state),snapshot.successful,snapshot.failed,snapshot.skipped,snapshot.outcomes,
                 snapshot.entry_index,snapshot.entry_duration_ms,snapshot.entry_remaining_ms);
        free(esc);
        if (!resp_buf[0]) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(strlen(resp_buf),
                                               (void *)resp_buf,
                                               MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_AUTOLOAD_CLEAR) == 0) {
        pldmgr_autoload_reset();
        const char *msg = "Autoload status cleared.\n";
        resp = create_buffer_response(strlen(msg), (void *)msg,
                                               MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_ABORT) == 0) {
        pldmgr_autoload_abort();
        const char *msg = "Autoload sequence aborted.\n";
        resp = create_buffer_response(strlen(msg), (void *)msg,
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
    } else if (strcmp(url, ROUTE_SMP_STATUS) == 0) {
        /* [CN-FORK] v0.5.2.131 ShadowMount+ 探活（本机 loopback 10101）。
         * 供游戏详情「去 ShadowMount 管理」先问一句：没起就拉，起了直连。 */
        char smp_json[128];
        smp_status_json(smp_json, sizeof(smp_json));
        resp = MHD_create_response_from_buffer(strlen(smp_json), (void *)smp_json,
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
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
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
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
        thermal_stats_json(resp_buf, RESPONSE_BUFFER_SIZE);
        resp = MHD_create_response_from_buffer(strlen(resp_buf), (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_FAN_CONTROL) == 0) {
        /* [CN-FORK] 风扇温控（模型 A + 守护）：?apply=1&target=NN=应用生效（写硬件）；
         * ?set_enabled=0/1=只切开关（不写硬件）；无参=读状态（只读）。safe_mode/设备缺失时 supported:false。 */
        char *resp_buf;
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
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
        /* [CN-FORK] 游戏列表：app.db 已安装 + 扫描 dump，mutex+60s 缓存，进页面一次拉取不轮询。
         * v0.5.2.133 加 force=1：用户手点「刷新」时跳过 60s 快照直接重建。
         * 前端两处调用区分开 —— 进页面那次不带参数（吃缓存，快），刷新按钮带 force=1。
         * 不加这条的话，外部改了 app.db（FTP、SMP 扫进新游戏）用户连点十次刷新都看不到。 */
        const char *gforce = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "force");
        int gl_force = (gforce && gforce[0] == '1') ? 1 : 0;
        char *resp_buf;
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
        int glen = game_mgr_list_json(resp_buf, RESPONSE_BUFFER_SIZE, gl_force);
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
        if (!alloc_response_buffer(&mresp_buf))
            return send_out_of_memory(conn);
        int mlen = game_media_list_json(mtid, mresp_buf, RESPONSE_BUFFER_SIZE);
        if (mlen < 0) {
            free(mresp_buf);
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                                    "读取系统媒体库失败（av_content.db）");
        }
        /* [CN-FORK] v0.6.0.177 自动备份的**唯一触发点**：列表返回时顺手踢一下。
         * 为什么不单开一条端点 / 为什么不是「新文件落地时立刻传」——
         * 面板侧拿不到系统「新截图落盘」的事件（那是主机的事，铁律② 也不许轮询），
         * 而打开多媒体管理页本身就是「这里可能有新东西」的信号；kick 是非阻塞的
         * （读一遍配置 + 起线程），HTTP 这边一毫秒都不多站。 */
        mr_auto_kick(mtid);
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
        /* [CN-FORK] 原图 / 原视频：录屏动辄几百 MB，读进内存就是把面板 OOM 掉，
         * 所以按路径直出。字节由 gr_file_response 自己 lseek+read 搬（为什么不用
         * MHD_create_response_from_fd64，见那段注释 —— 真机上正是那条路只画出图片
         * 顶部一条、打包 zip 下载判失败）。fd 的所有权在 free 回调里关，
         * 创建失败 gr_file_response 已经替我们收了尸，这里不能再 close。 */
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
        resp = gr_file_response(fsz, ffd);
        if (!resp)
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO", "无法读取文件");
        MHD_add_response_header(resp, "Content-Type", fmime);
        /* dl=1 才下发文件名：同一个 URL 还要给 <img>/<video> 当预览源，
         * 对预览也挂 attachment 会让某些浏览器直接不渲染。 */
        {
            const char *fdl = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "dl");
            if (fdl && fdl[0] == '1') {
                const char *ftid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
                char fname[300];
                if (game_media_download_name(ftid, fid, fname, sizeof(fname)) == 0)
                    add_disposition(resp, fname);
            }
        }
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
        /* 4096 不是拍脑袋：phase + error + reason + name（v0.5.2.122 新增的成品
         * zip 名，游戏名最长 255 字节、JSON 转义后翻倍）四段都做了转义，最坏总长
         * 已经贴近 1700 —— 用 1024 一旦截断，game_media_pack_status 会返回 -1，
         * 路由把它当「任务不存在」回 404，前端看到的是一个凭空消失的任务。 */
        char jbuf[4096];
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
        /* [CN-FORK] 取打包成品：包可以几百 MB，按路径直出；字节由
         * gr_file_response 自己 lseek+read 搬（不用 MHD_create_response_from_fd64
         * 的理由见那段注释 —— 真机上正是那条路把 zip 发短了，下载直接判失败）。 */
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
        /* [CN-FORK] v0.5.2.124 「还没好」与「文件没了」要分开：
         * 前者回 409（前端据此继续等，或提示「还没打包完」），后者才是 404。
         * 以前两者都回 404 —— 于是「打包进行中点下载」看起来像「任务凭空消失」，
         * 排查时也分不清是状态问题还是文件问题。 */
        if (prc == -2)
            return queue_json_error(conn, MHD_HTTP_CONFLICT, "EBUSY", "还没打包完，请稍候");
        if (prc != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT",
                                    "打包产出文件已被清理");
        resp = gr_file_response(psz, pfd); /* 失败时 fd 已被 gr_file_response 收掉 */
        if (!resp)
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO", "无法读取打包文件");
        MHD_add_response_header(resp, "Content-Type", "application/zip");
        /* 包名在 md_pack_start 里就定死了（游戏名称-打包时间戳.zip），这里只取 basename。
         * 以前硬编码 "media_pack.zip" —— 用户要的「压缩包也是游戏名称-打包时间戳」
         * 因此从来没到过浏览器（落盘的包名是对的，下载下来的却永远是 media_pack.zip）。 */
        {
            char pname[300];
            if (game_media_pack_name(pid, pname, sizeof(pname)) == 0)
                add_disposition(resp, pname);
            else
                MHD_add_response_header(resp, "Content-Disposition",
                                        "attachment; filename=\"media_pack.zip\"");
        }
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_DETAIL) == 0) {
        /* [CN-FORK] v0.5.2.116 单游戏详情：打开弹窗拉一次，不轮询。
         * 刻意**不**套用 /game_list 那份 60s 快照 —— 详情里有存档数、媒体数、
         * 两层自定义图标志这些「刚改完就要看见」的活值，进了缓存就等于把
         * 用户刚设的图标吞掉一分钟。 */
        const char *xd = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        if (!xd)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid");
        char *xb;
        if (!alloc_response_buffer(&xb))
            return send_out_of_memory(conn);
        int xn = game_mgr_detail_json(xd, xb, RESPONSE_BUFFER_SIZE);
        if (xn < 0) {
            free(xb);
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "主机上找不到这个游戏");
        }
        resp = MHD_create_response_from_buffer((size_t)xn, (void *)xb, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_SAVES) == 0) {
        /* [CN-FORK] v0.5.2.116 存档列表。项数由槽决定（一个存档目录一个槽），
         * 但保险起见仍走 1MB 的大缓冲，不挤 jbuf[2048]。 */
        const char *xs = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        if (!xs)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid");
        char *sb;
        if (!alloc_response_buffer(&sb))
            return send_out_of_memory(conn);
        int sn = game_saves_list_json(xs, sb, RESPONSE_BUFFER_SIZE);
        if (sn < 0) {
            free(sb);
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "参数不合法");
        }
        resp = MHD_create_response_from_buffer((size_t)sn, (void *)sb, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_SAVE_REMOTE_INFO) == 0) {
        /* [CN-FORK] v0.5.2.145 远程存档只读元数据：前端「远程游戏」标签展示用。
         * tid / save 都走 query，与 /game_saves 一致；save 缺省回 400。 */
        const char *xr = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        const char *xrs = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "save");
        if (!xr || !xrs)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid 或 save");
        char *rb;
        if (!alloc_response_buffer(&rb))
            return send_out_of_memory(conn);
        int rn = game_saves_remote_info(xr, xrs, rb, RESPONSE_BUFFER_SIZE);
        if (rn < 0) {
            free(rb);
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "参数不合法");
        }
        resp = MHD_create_response_from_buffer((size_t)rn, (void *)rb, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_SAVE_DOWNLOAD) == 0) {
        /* [CN-FORK] v0.5.2.116 打包即下：没有任务槽可复用（契约里这条路不产生 job_id），
         * 所以同步打完、fd 直出、流完自动清理。文件名只用 tid（alnum），
         * 不掺 save —— save 是用户起的槽名，能带引号，会把 Content-Disposition 打断。 */
        const char *xd = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        const char *xs2 = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "save");
        if (!xd || !xs2)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid 或 save");
        if (mkdir(GAME_SAVES_PACK_DIR, 0777) != 0 && errno != EEXIST)
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                                    "无法创建下载临时目录");
        char zpath[900], zmsg[192], disp[160];
        zmsg[0] = 0;
        snprintf(zpath, sizeof(zpath), "%s/%s_%lld_%d.zip", GAME_SAVES_PACK_DIR, xd,
                 (long long)time(NULL), (int)getpid());
        if (game_saves_pack(xd, xs2, zpath, NULL, zmsg, sizeof(zmsg)) != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT",
                                    zmsg[0] ? zmsg : "找不到这个存档");
        snprintf(disp, sizeof(disp), "attachment; filename=\"%s_saves.zip\"", xd);
        return reply_file(conn, zpath, "application/zip", disp, 1);
    } else if (strcmp(url, ROUTE_GAME_SAVE_SNAP_DOWNLOAD) == 0) {
        /* [CN-FORK] v0.5.2.172 下载**一份本地快照**（打包即下，与 /game_save_download
         * 同一套：同步打完、fd 直出、流完自动清理）。
         * 文件名只用 tid / uid / ts —— 三个都是受控字符集（tid 是 alnum、
         * uid 是十六进制、ts 是 YYYYmmdd-HHMMSS），拼进 Content-Disposition 安全。 */
        const char *ntid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        const char *nuid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "uid");
        const char *nts = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "ts");
        if (!ntid || !nuid || !nts)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid / uid / ts");
        if (mkdir(GAME_SAVES_PACK_DIR, 0777) != 0 && errno != EEXIST)
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                                    "无法创建下载临时目录");
        char npath[900], nmsg[192], ndisp[220];
        nmsg[0] = 0;
        snprintf(npath, sizeof(npath), "%s/%s_%s_%lld_%d.zip", GAME_SAVES_PACK_DIR,
                 ntid, nuid, (long long)time(NULL), (int)getpid());
        if (game_saves_snap_pack(ntid, nuid, nts, npath, NULL, nmsg, sizeof(nmsg)) != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT",
                                    nmsg[0] ? nmsg : "找不到这份快照");
        snprintf(ndisp, sizeof(ndisp), "attachment; filename=\"%s_%s_%s.zip\"",
                 ntid, nuid, nts);
        return reply_file(conn, npath, "application/zip", ndisp, 1);
    } else if (strcmp(url, ROUTE_GAME_SAVE_REMOTE_DOWNLOAD) == 0) {
        /* [CN-FORK] v0.5.2.172 下载**远端那一份**存档。同步取回临时文件再直出 ——
         * 与「立即拉取」的区别是它一个字都不碰主机存档（见 game_saves.h）。
         * 「远端没有这份」回 404（前端当「没得下」），连不上 / 没配回 502 +
         * 人话 —— 这两类原因用户能自己修，回 500 只会让人以为面板坏了。 */
        const char *qtid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        const char *qsave = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "save");
        if (!qtid || !qsave)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid 或 save");
        if (mkdir(GAME_SAVES_PACK_DIR, 0777) != 0 && errno != EEXIST)
            return queue_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "EIO",
                                    "无法创建下载临时目录");
        char qpath[900], qmsg[192], qdisp[200];
        qmsg[0] = 0;
        snprintf(qpath, sizeof(qpath), "%s/remote_%s_%lld_%d.zip", GAME_SAVES_PACK_DIR,
                 qtid, (long long)time(NULL), (int)getpid());
        int qrc = game_saves_remote_download(qtid, qsave, qpath, qmsg, sizeof(qmsg));
        if (qrc != 0)
            return queue_json_error(conn,
                                    qrc == SR_ENOTFOUND ? MHD_HTTP_NOT_FOUND
                                                        : MHD_HTTP_BAD_GATEWAY,
                                    qrc == SR_ENOTFOUND ? "ENOENT" : "EREMOTE",
                                    qmsg[0] ? qmsg : "下载远程存档失败");
        snprintf(qdisp, sizeof(qdisp), "attachment; filename=\"%s_%s_remote.zip\"",
                 qtid, qsave);
        return reply_file(conn, qpath, "application/zip", qdisp, 1);
    } else if (strcmp(url, ROUTE_GAME_SAVE_SNAP_LOG) == 0) {
        /* [CN-FORK] v0.5.2.174 「快照活动」只读接口（见 pldmgr.h 里那段）。
         * 用户口径：「发生记录也没有写到屏幕上」。
         *
         * tid 可空（空 = 全部游戏）；不认识的 tid 回空列表而不是 400 —— 这条只是
         * 展示用，前端换游戏时会有一瞬间 tid 还没跟上，报错只会闪一条红字。
         * `n` 默认 16、封顶 64（与事件环同尺寸）。
         *
         * 缓冲走**栈**（6KB）：响应撑死 24 条 × 160 字节。用 alloc_response_buffer
         * 那条路要 malloc 1MB（RESPONSE_BUFFER_SIZE），给一条每 5 秒轮询一次的
         * 只读接口不值得。`queue_json` 是 MUST_COPY，传栈上数组是安全的。
         * ⚠ 拼装函数保证「放不下就停手再收尾」，所以截断时也是**合法 JSON**。 */
        const char *ltid = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        const char *ln = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "n");
        int lnmax = ln ? atoi(ln) : 0;
        if (lnmax <= 0) lnmax = 16;
        if (lnmax > 64) lnmax = 64;
        char ljson[6144];
        int lpos = game_saves_snap_ev_json(ltid, lnmax, ljson, sizeof(ljson));
        if (lpos <= 0)
            return queue_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "{\"ok\":false}", 13);
        return queue_json(conn, MHD_HTTP_OK, ljson, (size_t)lpos);
    } else if (strcmp(url, ROUTE_GAME_SAVE_DELETE) == 0) {
        const char *xd = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        const char *xs2 = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "save");
        if (!xd || !xs2)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid 或 save");
        char dmsg[192];
        dmsg[0] = 0;
        if (game_saves_remove(xd, xs2, dmsg, sizeof(dmsg)) != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT",
                                    dmsg[0] ? dmsg : "找不到这个存档");
        return queue_json(conn, MHD_HTTP_OK, "{\"ok\":true}", 11);
    } else if (strcmp(url, ROUTE_GAME_TILE_ICON) == 0) {
        const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        char tp[768];
        tp[0] = 0;
        if (!xt || game_title_icon_path(xt, tp, sizeof(tp)) != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "没有设过自定义图标");
        return reply_file(conn, tp, "image/png", NULL, 0);
    } else if (strcmp(url, ROUTE_GAME_TILE_BG) == 0) {
        /* 回的是 **PNG 预览**：浏览器的 <img> 解不了 DDS，前端也只消费这个 URL。 */
        const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        char tp[768];
        tp[0] = 0;
        if (!xt || game_title_bg_png_path(xt, tp, sizeof(tp)) != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "没有设过自定义背景");
        return reply_file(conn, tp, "image/png", NULL, 0);
    } else if (strcmp(url, ROUTE_GAME_TILE_BG_DDS) == 0) {
        const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        char tp[768];
        tp[0] = 0;
        if (!xt || game_title_bg_dds_path(xt, tp, sizeof(tp)) != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "没有设过自定义背景");
        return reply_file(conn, tp, "application/octet-stream", NULL, 0);
    } else if (strcmp(url, ROUTE_GAME_TILE_ICON_RESET) == 0) {
        const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        if (!xt)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid");
        game_title_reset_icon(xt);
        return queue_json(conn, MHD_HTTP_OK, "{\"ok\":true}", 11);
    } else if (strcmp(url, ROUTE_GAME_TILE_BACKGROUND_RESET) == 0) {
        const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        if (!xt)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid");
        game_title_reset_bg(xt);
        return queue_json(conn, MHD_HTTP_OK, "{\"ok\":true}", 11);
    /* [CN-FORK] v0.6.0.179 内置软件的图标 / 背景预览 / 恢复默认。
     * 实现在上面的 app_tile_reply_* 里，这里只转发 —— 那几个 KB 级的路径缓冲
     * 留在本条链上就会把栈帧顶过闸门（见那里的注释）。 */
    } else if (strcmp(url, ROUTE_APP_ICON) == 0) {
        return app_tile_reply_icon(conn);
    } else if (strcmp(url, ROUTE_APP_TILE_BG) == 0) {
        return app_tile_reply_bg(conn);
    } else if (strcmp(url, ROUTE_APP_TILE_ICON_RESET) == 0) {
        return app_tile_reply_reset(conn, 0);
    } else if (strcmp(url, ROUTE_APP_TILE_BACKGROUND_RESET) == 0) {
        return app_tile_reply_reset(conn, 1);
    /* [CN-FORK] v0.5.2.163 背景音乐读取 + 恢复默认（GET 分支）。
     * 与 game_title_*_path 同一档：tid 在 query，没设过就 404 + ENOENT。
     * mime 走 game_music_path 报上来的（按落盘扩展名给 audio/mpeg 等）。 */
    } else if (strcmp(url, ROUTE_GAME_MUSIC) == 0) {
        const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        char tp[768], mime[64];
        tp[0] = 0; mime[0] = 0;
        if (!xt || game_music_path(xt, tp, sizeof(tp), mime, sizeof(mime)) != 0)
            return queue_json_error(conn, MHD_HTTP_NOT_FOUND, "ENOENT", "没有设过自定义音乐");
        return reply_file(conn, tp, mime, NULL, 0);
    } else if (strcmp(url, ROUTE_GAME_MUSIC_RESET) == 0) {
        const char *xt = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tid");
        if (!xt)
            return queue_json_error(conn, MHD_HTTP_BAD_REQUEST, "EBAD", "缺少 tid");
        game_music_reset(xt);
        /* [CN-FORK] v0.5.2.166 「恢复默认」要连**主机上那份**一起还原 ——
         * 只删我们自己的预览副本，PS5 主屏上还在放用户设过的音乐。 */
        game_music_reset_host(xt);
        return queue_json(conn, MHD_HTTP_OK, "{\"ok\":true}", 11);
    } else if (strcmp(url, ROUTE_GET_CONFIG) == 0) {
        PldmgrConfig cfg;
        config_read(&cfg);

        /* [CN-FORK] v0.5.2.145 全局存档配置原文（设置页「存档配置」）+ v0.6.0 收藏列表。
         * 全局存档配置：单游戏弹窗要求它来判断「远程位置来自全局还是本游戏」，所以
         * 必须从 /get_config 发出去 —— 不发的话前端每次刷新就丢，用户填的全局配置
         * 看起来像没保存。没配过时是空串（前端 loadGlobalCfg 会拿它跟 EMPTY_CFG 合并）。
         *
         * ⚠ 三份原始缓冲 + 三份 JSON 转义缓冲**一律走堆**（两块连续分配，指针切段）。
         * 理由：http_on_request 是一条巨型 if/else 链，clang 会把**所有分支**的局部
         * 数组都算进同一个栈帧（-fstack-usage 实测把这条链推到 35KB），而
         * tools/check_stack_frames.sh 的闸门是 32KB/函数。 */
        size_t raw_cap = 4096 + 2048 + 4096 + 2048;   /* list / archive-global / favorites / media-archive */
        char *raw = malloc(raw_cap);
        char *esc = malloc(6 * raw_cap);
        if (!raw || !esc) {
            free(raw); free(esc);
            return send_out_of_memory(conn);
        }
        char *list_buf     = raw;                        /* 4096 */
        char *ag_raw       = raw + 4096;                 /* 2048 */
        char *fav_buf      = raw + 6144;                 /* 4096 */
        char *ma_raw       = raw + 10240;                /* 2048 */
        char *list_escaped = esc;                        /* 6*4096  */
        char *ag_escaped   = esc + 6 * 4096;             /* 6*2048  */
        char *fav_escaped  = esc + 6 * 4096 + 6 * 2048;  /* 6*4096  */
        char *ma_escaped   = esc + 6 * 4096 + 6 * 2048 + 6 * 4096; /* 6*2048 */
        char *resp_buf = NULL;

        if (read_autoload_list(list_buf, 4096, 1) != 0) {
            free(raw); free(esc);
            return send_request_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                      "Autoload list is unreadable or exceeds the size limit");
        }
        ag_raw[0] = 0;
        if (config_read_archive_global(ag_raw, 2048) < 0)
            ag_raw[0] = 0;
        /* [CN-FORK] v0.6.0.177 多媒体存档配置（设置页那个新弹窗）。
         * 与 ARCHIVE_GLOBAL_CONFIG 平级的**另一份**原文，同样必须从 /get_config
         * 发出去 —— 不发的话弹窗一关再开就是空的，用户会以为没保存。 */
        ma_raw[0] = 0;
        if (config_read_media_archive(ma_raw, 2048) < 0)
            ma_raw[0] = 0;
        fav_buf[0] = 0;
        int fav_exists = 0;
        if (config_get_favorites_csv(fav_buf, 4096, &fav_exists) != 0) {
            free(raw); free(esc);
            return send_request_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                      "Favorites list is unreadable or exceeds the size limit");
        }
        pldmgr_json_escape(list_buf, list_escaped, 6 * 4096);
        pldmgr_json_escape(ag_raw, ag_escaped, 6 * 2048);
        pldmgr_json_escape(fav_buf, fav_escaped, 6 * 4096);
        pldmgr_json_escape(ma_raw, ma_escaped, 6 * 2048);

        if (!alloc_response_buffer(&resp_buf)) {
            free(raw); free(esc);
            return send_out_of_memory(conn);
        }
        json_format(resp_buf, RESPONSE_BUFFER_SIZE,
                    "{\"AUTOLOAD_ENABLED\":%s,\"AUTOLOAD_LIST\":\"%s\",\"LAST_"
                    "REPOSITORY_UPDATE\":%ld,"
                    "\"AUTO_BROWSER_OPEN\":%s,\"AUTOLOAD_DELAY\":%d,"
                    "\"KILL_DISC_PLAYER_ON_STARTUP\":%s,\"SCAN_USB_PAYLOADS\":%s,"
                    "\"AUTO_INSTALL_APP\":%s,\"MULTI_SOURCES_ENABLED\":%s,"
                    "\"DEFAULT_REPO_ENABLED\":%s,\"ARCHIVE_GLOBAL_CONFIG\":\"%s\","
                    "\"MEDIA_ARCHIVE_CONFIG\":\"%s\","
                    "\"FAVORITES\":\"%s\",\"FAVORITES_INITIALIZED\":%s}",
                    cfg.autoload_enabled ? "true" : "false", list_escaped, cfg.last_repository_update,
                    cfg.auto_browser_open ? "true" : "false", cfg.autoload_delay,
                    cfg.kill_disc_player ? "true" : "false", cfg.scan_usb_payloads ? "true" : "false",
                    cfg.auto_install_app ? "true" : "false",
                    cfg.multi_sources_enabled ? "true" : "false",
                    cfg.default_repo_enabled ? "true" : "false",
                    ag_escaped, ma_escaped, fav_escaped, fav_exists ? "true" : "false");
        free(raw); free(esc);
        if (!resp_buf[0]) return send_serialization_error(conn, resp_buf);
        resp = create_buffer_response(strlen(resp_buf),
                                               (void *)resp_buf,
                                               MHD_RESPMEM_MUST_FREE);
        add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_SYSTEM_APPS) == 0) {
        /* [CN-FORK] v0.5.2.158 「个性化软件」：主机内置软件 + 每个账号累积出来的显隐状态。
         * 一次弹窗拉一遍，不轮询；库打不开时 error 非空、apps 为空数组，
         * 前端据此把「没有软件」和「读不到」分开显示。 */
        char *resp_buf;
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
        int alen = app_vis_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        if (alen < 0) alen = 0;
        resp = MHD_create_response_from_buffer((size_t)alen, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_GAME_PREFS) == 0) {
        /* [CN-FORK] v0.5.2.164 游戏磁贴的收藏 / 置顶（面板侧列表排序）。
         * 前端进游戏页时拉一次，与 /game_list 合并后排序；no-store 是因为它会被
         * /game_prefs_set 立刻改掉，缓存一份只会让「点了收藏但列表没动」变成幽灵问题。 */
        char *resp_buf;
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
        int plen = game_prefs_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        if (plen < 0) plen = 0;
        resp = MHD_create_response_from_buffer((size_t)plen, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, ROUTE_TILE_GROUP) == 0) {
        /* [CN-FORK] v0.5.2.166 主屏固定列表（主机 app.db 的 tbl_group_<uid>）。
         * 前端进游戏页时与 /game_prefs 一起拉：prefs 给「收藏」与面板顺序，
         * 这一份给「置顶」的**真身**（以前那份 pin 只写在我们的 JSON 里，主机不知道）。
         * no-store 同上：拨一次置顶它就变了。 */
        char *resp_buf;
        if (!alloc_response_buffer(&resp_buf))
            return send_out_of_memory(conn);
        int tlen = tile_group_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        if (tlen < 0) tlen = 0;
        resp = MHD_create_response_from_buffer((size_t)tlen, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
    } else if (strcmp(url, "/events") == 0) {
        struct LogSSEConnection *sse = malloc(sizeof(struct LogSSEConnection));
        if (!sse)
            return MHD_NO;
        log_stream_init(sse, MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Last-Event-ID"));

        resp = MHD_create_response_from_callback(
            MHD_SIZE_UNKNOWN, 1024, &log_stream_callback, sse, &log_stream_cleanup);
        if (!resp) {
            log_stream_cleanup(sse);
            return MHD_NO;
        }
        add_response_header(resp, "Content-Type", "text/event-stream");
        add_response_header(resp, "Cache-Control", "no-cache");
        add_response_header(resp, "Connection", "keep-alive");
    } else {
        /* Default: 404 */
        const char *not_found = "404 Not Found\n";
        resp = create_buffer_response(strlen(not_found), (void *)not_found,
                                               MHD_RESPMEM_PERSISTENT);
        add_response_header(resp, "Content-Type", "text/plain");
    }

    if (!resp)
        return MHD_NO;

    return send_response(conn, http_status, resp);
}
