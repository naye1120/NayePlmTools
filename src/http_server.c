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
#include "game_runtime.h" /* [CN-FORK] v0.5.2.116 删源 / 挂载 / 卸载挂载 / 卸载安装 */
#include "game_saves.h"   /* [CN-FORK] v0.5.2.116 存档六条 */
#include "game_auto.h"    /* [CN-FORK] v0.5.2.142 自动存档手动推 / 拉 */
#include "game_title.h"   /* [CN-FORK] v0.5.2.116 改名 + 磁贴两层图六条 */
#include "fan_control.h" /* [CN-FORK] 风扇温控（模型 A）/fan_control */
#include "app_visibility.h" /* [CN-FORK] v0.5.2.158 「个性化软件」/system_apps* */
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
    if (strcmp(url, ROUTE_GAME_DELETE) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_AUTO) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_TEST) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_SNAP) == 0 ||
        strcmp(url, ROUTE_GAME_SAVE_SYNC) == 0 ||
        strcmp(url, ROUTE_GAME_RENAME) == 0 ||
        strcmp(url, ROUTE_GAME_SYNC) == 0 ||
        strcmp(url, ROUTE_GAME_MOUNT) == 0 ||
        strcmp(url, ROUTE_GAME_UNMOUNT) == 0 ||
        strcmp(url, ROUTE_GAME_UNINSTALL) == 0)
        return (size_t)1 << 20;
    /* [CN-FORK] v0.5.2.158 「个性化软件」的开关：body 只有 tid + hidden 两个字段。
     * ⚠ 新加 POST 路由必须在这里登记，否则请求体压根不收（见上面的文件头注释）。 */
    if (strcmp(url, ROUTE_SYSTEM_APP_VISIBLE) == 0)
        return (size_t)1 << 20;
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
        int persisted = 0;
        msg[0] = 0;
        if (!gr_json_str(body, len, "tid", btid, sizeof(btid)) ||
            !gr_json_str(body, len, "name", bname, sizeof(bname)))
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL", "缺少 tid 或 name");
        if (game_title_set_name(btid, bname, msg, sizeof(msg),
                                &persisted, pnote, sizeof(pnote)) != 0)
            return gr_json_err(out, size, code, MHD_HTTP_BAD_REQUEST, "EINVAL",
                               msg[0] ? msg : "名称不合法");
        /* 回**存下来的**名字，不是原样回传：set_name 会去控制字符、去首尾空白、
         * 按 UTF-8 字符数截到 128，回一个没截过的会让前端存下一份对不上的值。 */
        game_title_get_name(btid, shown, sizeof(shown));
        pldmgr_json_escape(shown, esc, sizeof(esc));
        pldmgr_json_escape(pnote, pesc, sizeof(pesc));
        *code = MHD_HTTP_OK;
        /* [CN-FORK] v0.5.2.132 persisted = 名字源（游戏包里的 sce_sys/param.json）
         * 也改成功了，SMP 重扫不会回退。false 时 persist_note 说清为什么
         * （镜像游戏 / 系统内置 / 源写失败）。前端据此决定要不要提示 ——
         * 别把 false 当错误：面板层与主机索引层已经生效，改名本身是成功的。 */
        int n = snprintf(out, size,
                         "{\"ok\":true,\"name\":\"%s\","
                         "\"persisted\":%s,\"persist_note\":\"%s\"}",
                         esc, persisted ? "true" : "false", pesc);
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

    return gr_json_err(out, size, code, MHD_HTTP_NOT_FOUND, "ENOENT", "没有这条路由");
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
    /* [CN-FORK] v0.5.2.116 磁贴三张图都是 <img src> 拉的，一次打开要打三发 */
    if (strcmp(url, ROUTE_GAME_TILE_ICON) == 0) return 1;
    if (strcmp(url, ROUTE_GAME_TILE_BG) == 0) return 1;
    if (strcmp(url, ROUTE_GAME_TILE_BG_DDS) == 0) return 1;
    if (strcmp(url, ROUTE_FAN_CONTROL) == 0) return 1; /* [CN-FORK] 风扇温控页加载/应用，降噪 */
    if (strcmp(url, ROUTE_SYSTEM_APPS) == 0) return 1; /* [CN-FORK] v0.5.2.158 个性化软件弹窗，降噪 */
    if (strcmp(url, ROUTE_SYSTEM_APP_VISIBLE) == 0) return 1; /* 同上，每次拨开关一次 */
    if (strcmp(url, ROUTE_PROCESSES_LIST) == 0) return 1;
    if (strcmp(url, ROUTE_HISTORY_LIST) == 0) return 1;
    /* [CN-FORK] v0.5.1.64 删 /system_stats 轮询（该端点已随硬件信息功能一起下线） */
    if (strcmp(url, ROUTE_PKG_STATUS) == 0) return 1; /* [CN-FORK] PKG安装页轮询，降噪 */
    if (strcmp(url, ROUTE_SMP_STATUS) == 0) return 1; /* [CN-FORK] v0.5.2.131 SMP 探活，拉起期间轮询，降噪 */
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

        /* [CN-FORK] v0.5.2.116 游戏管理的 POST 请求体：八条 JSON + 两条裸字节
         * （磁贴 PNG、存档包）共用同一个 PostStatus 累加器。三条 JSON 的语义
         * 在**收完之后**才分岔（见下方 gr_dispatch_post），所以这里只认路由不认内容。 */
        if (strcmp(method, "POST") == 0 && gr_post_limit(url) != 0) {
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

    /* ── [CN-FORK] v0.5.2.116 Handle POST data for /game_* ──
     * 九条端点（删源 / 存档自动与连通性 / 存档上传 / 改名 / 磁贴两层图 / 挂载 /
     * 卸载挂载 / 卸载安装）共用同一个累加器，**收完才分发**。
     * 与 /game_media_pack 那条刻意保持同一套结构：MHD 是把请求体切成若干块
     * 送进来的，只有 upload_data_size==0 那一发才代表「体收完了」，
     * 在那之前动手会拿到半截 JSON 或半张 PNG。 */
    {
        size_t grcap = gr_post_limit(url);
        if (grcap != 0 && strcmp(method, "POST") == 0) {
            struct PostStatus *status = (struct PostStatus *)*con_cls;
            if (*upload_data_size != 0) {
                if (status->size + *upload_data_size > grcap) {
                    status->error = 1; /* 超限后只排水不收数，仍要一路 drain 到收完 */
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
            }

            char out[4096];
            int code = MHD_HTTP_INTERNAL_SERVER_ERROR;
            int on;
            out[0] = '\0';
            if (status->error) {
                on = snprintf(out, sizeof(out),
                              "{\"error\":\"EBAD\",\"message\":\"请求体过大或内存不足\"}");
                code = MHD_HTTP_CONTENT_TOO_LARGE;
            } else {
                on = gr_dispatch_post(conn, url, status->data, status->size,
                                      out, sizeof(out), &code);
            }
            if (status->data) free(status->data);
            free(status);
            *con_cls = NULL;
            if (on < 0 || (size_t)on >= sizeof(out)) {
                /* 走到这里说明上面某个 snprintf 被截断了 —— 半截 JSON 比一条
                 * 通用错误严重得多：前端 .json() 直接抛，用户看到的是白屏式失败。 */
                on = snprintf(out, sizeof(out),
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
            return gr;
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
        /* [CN-FORK] 游戏列表：app.db 已安装 + 扫描 dump，mutex+60s 缓存，进页面一次拉取不轮询。
         * v0.5.2.133 加 force=1：用户手点「刷新」时跳过 60s 快照直接重建。
         * 前端两处调用区分开 —— 进页面那次不带参数（吃缓存，快），刷新按钮带 force=1。
         * 不加这条的话，外部改了 app.db（FTP、SMP 扫进新游戏）用户连点十次刷新都看不到。 */
        const char *gforce = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "force");
        int gl_force = (gforce && gforce[0] == '1') ? 1 : 0;
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
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
        struct MHD_Response *xoom = alloc_response_buffer(&xb);
        if (xoom)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, xoom);
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
        struct MHD_Response *soom = alloc_response_buffer(&sb);
        if (soom)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, soom);
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
        struct MHD_Response *room = alloc_response_buffer(&rb);
        if (room)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, room);
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

        /* [CN-FORK] v0.5.2.145 全局存档配置原文（设置页「存档配置」）。
         * 单游戏弹窗要求它来判断「远程位置来自全局还是本游戏」，所以必须从 /get_config
         * 发出去 —— 不发的话前端每次刷新就丢，用户填的全局配置看起来像没保存。
         * 没配过时是空串（前端 loadGlobalCfg 会拿它跟 EMPTY_CFG 合并，不会崩）。
         * 两个缓冲都刻意压小：这条请求跑在 64KB 栈的连接线程上（见 check_stack_frames.sh）。 */
        char ag_raw[2048];
        if (config_read_archive_global(ag_raw, sizeof(ag_raw)) < 0)
            ag_raw[0] = 0;
        char ag_escaped[2816];
        pldmgr_json_escape(ag_raw, ag_escaped, sizeof(ag_escaped));

        char *resp_buf;
        resp = alloc_response_buffer(&resp_buf);
        if (!resp) {
            snprintf(resp_buf, RESPONSE_BUFFER_SIZE,
                     "{\"AUTOLOAD_ENABLED\":%s,\"AUTOLOAD_LIST\":\"%s\",\"LAST_"
                     "REPOSITORY_UPDATE\":%ld,"
                     "\"AUTO_BROWSER_OPEN\":%s,\"AUTOLOAD_DELAY\":%d,"
                     "\"KILL_DISC_PLAYER_ON_STARTUP\":%s,\"SCAN_USB_PAYLOADS\":%s,"
                     "\"AUTO_INSTALL_APP\":%s,\"MULTI_SOURCES_ENABLED\":%s,"
                     "\"DEFAULT_REPO_ENABLED\":%s,\"ARCHIVE_GLOBAL_CONFIG\":\"%s\"}",
                     cfg.autoload_enabled ? "true" : "false", list_escaped, cfg.last_repository_update,
                     cfg.auto_browser_open ? "true" : "false", cfg.autoload_delay,
                     cfg.kill_disc_player ? "true" : "false", cfg.scan_usb_payloads ? "true" : "false",
                     cfg.auto_install_app ? "true" : "false",
                     cfg.multi_sources_enabled ? "true" : "false",
                     cfg.default_repo_enabled ? "true" : "false",
                     ag_escaped);
            resp = MHD_create_response_from_buffer(strlen(resp_buf),
                                                   (void *)resp_buf,
                                                   MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(resp, "Content-Type", "application/json");
        }
    } else if (strcmp(url, ROUTE_SYSTEM_APPS) == 0) {
        /* [CN-FORK] v0.5.2.158 「个性化软件」：主机内置软件 + 每个账号累积出来的显隐状态。
         * 一次弹窗拉一遍，不轮询；库打不开时 error 非空、apps 为空数组，
         * 前端据此把「没有软件」和「读不到」分开显示。 */
        char *resp_buf;
        struct MHD_Response *oom_resp = alloc_response_buffer(&resp_buf);
        if (oom_resp)
            return MHD_queue_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, oom_resp);
        int alen = app_vis_list_json(resp_buf, RESPONSE_BUFFER_SIZE);
        if (alen < 0) alen = 0;
        resp = MHD_create_response_from_buffer((size_t)alen, (void *)resp_buf, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(resp, "Content-Type", "application/json");
        MHD_add_response_header(resp, "Cache-Control", "no-store");
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
