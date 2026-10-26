#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include "quicklinks.h"
#include "game_mgr.h"
#include "json_helpers.h"
#include "pkgmanager.h" /* [CN-FORK] PKGMGR_UI_PORT：本机程序类入口识别 */
#include "pldmgr.h"
#include "utils.h" /* [CN-FORK] pldmgr_get_local_ip：磁贴链接本机回环地址 → LAN IP 改写 */

/* ── Entry type & file I/O（照 sources.c 的骨架，无默认条目、按下标管理） ──
 * src: "manual"=用户手添 / "auto"=app.db 磁贴链接自动收录；
 * hidden[]: 用户删过的 URL 抑制名单（同存 quicklinks.json），同步时跳过。 */

typedef struct {
    char name[128];
    char url[512];
    char src[8];
} QuickLink;

#define MAX_QUICKLINKS_HIDDEN 64

typedef struct {
    QuickLink entries[MAX_QUICKLINKS];
    int count;
    char hidden[MAX_QUICKLINKS_HIDDEN][512];
    int hidden_count;
} QuicklinksStore;

static int ql_hidden_add(QuicklinksStore *s, const char *url) {
    for (int i = 0; i < s->hidden_count; i++)
        if (strcmp(s->hidden[i], url) == 0) return 0;
    if (s->hidden_count >= MAX_QUICKLINKS_HIDDEN) return -1;
    strncpy(s->hidden[s->hidden_count], url, sizeof(s->hidden[0]) - 1);
    s->hidden_count++;
    return 0;
}

static void ql_hidden_del(QuicklinksStore *s, const char *url) {
    for (int i = 0; i < s->hidden_count; i++) {
        if (strcmp(s->hidden[i], url) == 0) {
            for (int j = i; j < s->hidden_count - 1; j++)
                memcpy(s->hidden[j], s->hidden[j + 1], sizeof(s->hidden[0]));
            s->hidden_count--;
            return;
        }
    }
}

static void load_quicklinks(QuicklinksStore *s) {
    char *json = NULL;
    size_t size = 0;

    memset(s, 0, sizeof(*s));

    if (read_file_text(QUICKLINKS_CONFIG_PATH, &json, &size) != 0 || !json || size == 0) {
        if (json) free(json);
        return; /* 没有文件 = 空清单，不是错误 */
    }

    const char *arr_start = strstr(json, "\"entries\"");
    if (arr_start) arr_start = strchr(arr_start, '[');
    if (arr_start) {
        const char *p = arr_start + 1;
        const char *limit = json + size;
        while (s->count < MAX_QUICKLINKS && (p = strchr(p, '{')) != NULL) {
            /* [CN-FORK] v0.6.3.195 修「点一次刷新，快捷入口全部消失」：
             * json_extract_string 的 end 是**开区间**（内部要求 r.p == end，即指向 '}'
             * 之后才算解析完整），这里原先直接把 strchr 得到的 '}' 当 end 传进去，于是
             * 每个条目都判成 JSON_STRING_INVALID、name/url 皆空被跳过 —— 整份清单读回来
             * 恒 0 条。病灶只在「读」这一侧，文件一直是好的：
             *   ① payload 重启后第一次 GET /quicklinks，load 得 0 条，随后的 app.db 同步
             *      现场枚举出全部条目、标 dirty 落盘，于是这次能看见；
             *   ② 再点刷新（或任何第二次请求），load 依旧 0 条，而同步的 mtime/size/ip
             *      闸门已命中直接跳过 → 返回空清单。
             * 即「第一次进页面正常，点一下刷新就全没了」。改用 json_find_object_end 正确定界，
             * 顺带不怕 name/url 里出现 '}'。 */
            const char *end = NULL;
            if (json_find_object_end(p, limit, &end) != 0) break;

            char name[128] = "", url[512] = "", src[16] = "";
            json_extract_string(p, end, "name", name, sizeof(name));
            json_extract_string(p, end, "url",  url,  sizeof(url));
            json_extract_string(p, end, "src",  src,  sizeof(src));
            if (name[0] && url[0]) {
                strncpy(s->entries[s->count].name, name, sizeof(s->entries[0].name) - 1);
                strncpy(s->entries[s->count].url,  url,  sizeof(s->entries[0].url) - 1);
                snprintf(s->entries[s->count].src, sizeof(s->entries[0].src), "%s",
                         strcmp(src, "auto") == 0 ? "auto" : "manual");
                s->count++;
            }
            p = end;   /* end 已是 '}' 之后的位置，不能再 +1 跳过下一个条目 */
        }
    }

    /* hidden 抑制名单（URL 数组，均为转义后的普通字符串） */
    const char *hp = strstr(json, "\"hidden\"");
    if (hp) hp = strchr(hp, '[');
    const char *he = hp ? strchr(hp, ']') : NULL;
    if (hp && he) {
        const char *p = hp + 1;
        while (s->hidden_count < MAX_QUICKLINKS_HIDDEN && (p = strchr(p, '"')) != NULL && p < he) {
            const char *q = strchr(p + 1, '"');
            if (!q || q > he) break;
            size_t l = (size_t)(q - p - 1);
            if (l > 0 && l < sizeof(s->hidden[0])) {
                memcpy(s->hidden[s->hidden_count], p + 1, l);
                s->hidden[s->hidden_count][l] = 0;
                s->hidden_count++;
            }
            p = q + 1;
        }
    }

    free(json);
}

static int save_quicklinks(const QuicklinksStore *s) {
    /* 条目名/URL + 抑制名单全量序列化，最坏几 KB×几十条；堆上给足余量（原先 16KB 栈缓冲偏小） */
    size_t cap = 131072;
    char *buf = malloc(cap);
    if (!buf) return -1;
    JsonListBuilder jb = { buf, cap, 0, 1 };

    ensure_dir_recursive(BASE_DATA_DIR);

    json_append(&jb, "{\"entries\":[\n");
    for (int i = 0; i < s->count; i++) {
        char name_e[512], url_e[1024];
        pldmgr_json_escape(s->entries[i].name, name_e, sizeof(name_e));
        pldmgr_json_escape(s->entries[i].url,  url_e,  sizeof(url_e));
        if (json_append(&jb, "  {\"name\":\"%s\",\"url\":\"%s\",\"src\":\"%s\"}%s\n",
            name_e, url_e, s->entries[i].src, (i < s->count - 1) ? "," : "") != 0) {
            break;
        }
    }
    json_append(&jb, "]");
    if (s->hidden_count) {
        json_append(&jb, ",\n\"hidden\":[");
        for (int i = 0; i < s->hidden_count; i++) {
            char url_e[1024];
            pldmgr_json_escape(s->hidden[i], url_e, sizeof(url_e));
            if (json_append(&jb, "%s\"%s\"", i ? "," : "", url_e) != 0) break;
        }
        json_append(&jb, "]");
    }
    json_append(&jb, "}\n");

    int rc = write_file_text(QUICKLINKS_CONFIG_PATH, buf, jb.pos);
    free(buf);
    return rc;
}

/* ── [CN-FORK] app.db 磁贴链接自动收录（按需取 + mtime 闸门）──────────────
 * 语义：打开「常用入口」页触发一次同步；app.db 的 mtime+size 与本机 IP 与上次
 * 完全一致才跳过。用「不等」而不是「更新」判断——PS5 时钟回退时 mtime 可能变小，
 * 变化就必须触发；IP 变化同理（换网后旧 IP 的收录条目要重写）。 */

#define MAX_SYNC_DEEPLINKS 64

typedef struct {
    char uris[MAX_SYNC_DEEPLINKS][512];
    char names[MAX_SYNC_DEEPLINKS][128];
    int count;
} deeplink_hits_t;

static int ql_collect_cb(const char *tid, const char *name, const char *uri, void *user) {
    (void)tid;
    deeplink_hits_t *h = (deeplink_hits_t *)user;
    for (int i = 0; i < h->count; i++)
        if (strcmp(h->uris[i], uri) == 0) return 0;  /* 多个 titleId 共用同一 URL 时只收一条 */
    if (h->count >= MAX_SYNC_DEEPLINKS) return 1;
    strncpy(h->uris[h->count], uri, sizeof(h->uris[0]) - 1);
    strncpy(h->names[h->count], name, sizeof(h->names[0]) - 1);
    h->count++;
    return 0;
}

static time_t g_appdb_mtime = 0;
static off_t  g_appdb_size  = -1;
static int    g_appdb_seen  = 0;
static char   g_appdb_ip[64] = ""; /* [CN-FORK] 上次收录时的本机 IP，变化要强制重同步 */

/* 合并语义：① 磁贴已消失（卸载/删除）→ 移除对应 auto 条目；② 新磁贴 → 追加为 auto；
 * ③ 已存在 URL 与上次删除的抑制名单都跳过；④ 满 MAX_QUICKLINKS 截断。*dirty=1 表示需落盘。 */
static void quicklinks_sync_appdb(QuicklinksStore *s, int *dirty) {
    struct stat st;
    if (stat(GAME_MGR_APPDB_PATH, &st) != 0) return;      /* 没有 app.db：静默跳过 */

    /* [CN-FORK] 本机 IP 参与闸门：换网/IP 变化时 auto 条目要跟着重写，
     * 否则陈旧 IP 一直留着。取不到 IP 时按原样跳过即可（没有可改写的东西）。 */
    char self_ip[64];
    int have_ip = (pldmgr_get_local_ip(self_ip, sizeof(self_ip)) == 0 && self_ip[0] != '\0');

    if (g_appdb_seen && st.st_mtime == g_appdb_mtime && st.st_size == g_appdb_size &&
        (!have_ip || strcmp(self_ip, g_appdb_ip) == 0)) return;

    deeplink_hits_t *h = calloc(1, sizeof(deeplink_hits_t));
    if (!h) return;
    int hits = game_mgr_enum_deeplinks(ql_collect_cb, h);
    if (hits < 0) { free(h); return; }                    /* 解析失败：不设闸门，下次请求重试 */
    g_appdb_mtime = st.st_mtime;
    g_appdb_size  = st.st_size;
    g_appdb_seen  = 1;
    if (have_ip) snprintf(g_appdb_ip, sizeof(g_appdb_ip), "%s", self_ip);

    /* [CN-FORK] 磁贴 deeplink 多为 http://127.0.0.1:8911/（本机回环），收录条目是给
     * PC/手机用的，必须换成本机当前 LAN IP；前缀整体匹配，防止误伤 127.0.0.10 之类。 */
    if (have_ip) {
        for (int j = 0; j < h->count; j++) {
            if (strncmp(h->uris[j], "http://127.0.0.1", 16) != 0) continue;
            char c = h->uris[j][16];
            if (c != ':' && c != '/' && c != '\0') continue;
            char rewritten[512];
            snprintf(rewritten, sizeof(rewritten), "http://%s%s", self_ip, h->uris[j] + 16);
            strncpy(h->uris[j], rewritten, sizeof(h->uris[j]) - 1);
            h->uris[j][sizeof(h->uris[j]) - 1] = '\0';
        }
    }

    int w = 0;
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->entries[i].src, "auto") != 0) { s->entries[w++] = s->entries[i]; continue; }
        int found = 0;
        for (int j = 0; j < h->count; j++)
            if (strcmp(h->uris[j], s->entries[i].url) == 0) { found = 1; break; }
        if (found) s->entries[w++] = s->entries[i];
        else *dirty = 1;
    }
    s->count = w;

    for (int j = 0; j < h->count; j++) {
        int exists = 0;
        for (int i = 0; i < s->count; i++)
            if (strcmp(s->entries[i].url, h->uris[j]) == 0) { exists = 1; break; }
        if (exists) continue;
        int suppressed = 0;
        for (int k = 0; k < s->hidden_count; k++)
            if (strcmp(s->hidden[k], h->uris[j]) == 0) { suppressed = 1; break; }
        if (suppressed) continue;
        if (s->count >= MAX_QUICKLINKS) break;
        strncpy(s->entries[s->count].name, h->names[j], sizeof(s->entries[0].name) - 1);
        strncpy(s->entries[s->count].url,  h->uris[j],  sizeof(s->entries[0].url) - 1);
        snprintf(s->entries[s->count].src, sizeof(s->entries[0].src), "auto");
        s->count++;
        *dirty = 1;
    }
    free(h);
}

/* ── TCP 探活：解析 URL 的 host:port，非阻塞 connect + select 限时 ────────── */

static pthread_mutex_t g_ql_mtx = PTHREAD_MUTEX_INITIALIZER;

/* 从 http(s)://host[:port]/... 提取 host 与 port；失败返回 -1。
 * host 支持 IP 字面量与域名（getaddrinfo）。 */
static int parse_url_hostport(const char *url, char *host, size_t hsz, int *port) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        *port = 80;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
    } else {
        return -1;
    }

    size_t n = 0;
    while (*p && *p != ':' && *p != '/' && *p != '?' && n + 1 < hsz)
        host[n++] = *p++;
    host[n] = '\0';
    if (n == 0) return -1;

    if (*p == ':') {
        p++;
        int pv = 0, digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 6) {
            pv = pv * 10 + (*p - '0');
            p++;
            digits++;
        }
        if (digits == 0 || pv <= 0 || pv > 65535) return -1;
        *port = pv;
    }
    return 0;
}

/* timeout_ms 内能完成 TCP 握手返回 1，否则 0。死主机不会拖过超时：
 * 阻塞 connect 在 BSD 上不受 SO_*TIMEO 约束，必须非阻塞 + select。 */
static int tcp_probe(const char *host, int port, int timeout_ms) {
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return 0;

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return 0;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    int alive = 0;
    if (connect(sock, res->ai_addr, res->ai_addrlen) == 0) {
        alive = 1;
    } else if (errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        if (select(sock + 1, NULL, &wfds, NULL, &tv) > 0) {
            int err = 0;
            socklen_t elen = sizeof(err);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &elen);
            alive = (err == 0);
        }
    }
    close(sock);
    freeaddrinfo(res);
    return alive;
}

static int probe_url(const char *url) {
    char host[256];
    int port = 0;
    if (parse_url_hostport(url, host, sizeof(host), &port) != 0)
        return 0;
    return tcp_probe(host, port, 400);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int quicklinks_list_json(char *buf, size_t size) {
    QuicklinksStore *s = calloc(1, sizeof(QuicklinksStore));
    if (!s) {
        snprintf(buf, size, "{\"entries\":[]}");
        return -1;
    }

    /* 锁内：加载 + app.db 同步（含落盘）；锁外再做 TCP 探活，别让慢探活占着写锁 */
    pthread_mutex_lock(&g_ql_mtx);
    load_quicklinks(s);
    int dirty = 0;
    quicklinks_sync_appdb(s, &dirty);
    if (dirty) {
        if (save_quicklinks(s) == 0)
            pldmgr_log("[PLDMGR] quicklinks: synced app.db deeplinks, %d entries\n", s->count);
        else
            pldmgr_log("[PLDMGR] quicklinks: sync save failed\n");
    }
    pthread_mutex_unlock(&g_ql_mtx);

    /* [CN-FORK] 本机程序识别：host 为本机（127.0.0.1 / localhost / 本机 LAN IP）且端口为
     * PKG Manager UI 端口（8844）的条目输出 program=pkgmanager；前端点击这类卡片时未运行
     * 就自动拉起（elfldr）、已在运行则直接打开。self_ip 只取一次，逐条仅做字符串比较。 */
    char self_ip[64] = "";
    pldmgr_get_local_ip(self_ip, sizeof(self_ip));

    JsonListBuilder jb = { buf, size, 0, 1 };
    buf[0] = '\0';

    json_append(&jb, "{\"entries\":[\n");
    for (int i = 0; i < s->count; i++) {
        char name_e[512], url_e[1024];
        pldmgr_json_escape(s->entries[i].name, name_e, sizeof(name_e));
        pldmgr_json_escape(s->entries[i].url,  url_e,  sizeof(url_e));

        const char *program = "";
        char host[256];
        int port = 0;
        if (parse_url_hostport(s->entries[i].url, host, sizeof(host), &port) == 0 &&
            port == PKGMGR_UI_PORT &&
            (strcmp(host, "127.0.0.1") == 0 || strcmp(host, "localhost") == 0 ||
             (self_ip[0] && strcmp(host, self_ip) == 0))) {
            program = "pkgmanager";
        }

        if (json_append(&jb, "  {\"name\":\"%s\",\"url\":\"%s\",\"src\":\"%s\",\"alive\":%s,\"program\":\"%s\"}%s\n",
            name_e, url_e, s->entries[i].src,
            probe_url(s->entries[i].url) ? "true" : "false", program,
            (i < s->count - 1) ? "," : "") != 0) {
            break;
        }
    }
    json_append(&jb, "]}\n");
    free(s);
    return 0;
}

int quicklinks_add(const char *name, const char *url, char *msg, size_t msz) {
    if (!name || !name[0]) {
        snprintf(msg, msz, "名称不能为空");
        return -1;
    }
    if (strlen(name) > 127) {
        snprintf(msg, msz, "名称过长（最多 127 字符）");
        return -1;
    }
    if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) {
        snprintf(msg, msz, "URL 必须以 http:// 或 https:// 开头");
        return -1;
    }
    if (strlen(url) > 511) {
        snprintf(msg, msz, "URL 过长（最多 511 字符）");
        return -1;
    }

    QuicklinksStore *s = calloc(1, sizeof(QuicklinksStore));
    if (!s) {
        snprintf(msg, msz, "内存分配失败");
        return -1;
    }

    pthread_mutex_lock(&g_ql_mtx);
    load_quicklinks(s);

    if (s->count >= MAX_QUICKLINKS) {
        pthread_mutex_unlock(&g_ql_mtx);
        free(s);
        snprintf(msg, msz, "条目已达上限 %d", MAX_QUICKLINKS);
        return -1;
    }
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->entries[i].url, url) == 0) {
            pthread_mutex_unlock(&g_ql_mtx);
            free(s);
            snprintf(msg, msz, "该 URL 已存在");
            return -1;
        }
    }

    strncpy(s->entries[s->count].name, name, sizeof(s->entries[0].name) - 1);
    strncpy(s->entries[s->count].url,  url,  sizeof(s->entries[0].url) - 1);
    snprintf(s->entries[s->count].src, sizeof(s->entries[0].src), "manual");
    s->count++;
    ql_hidden_del(s, url); /* 手动重新添加 = 撤销此前的删除抑制 */

    int rc = save_quicklinks(s);
    pthread_mutex_unlock(&g_ql_mtx);
    free(s);
    if (rc != 0) {
        snprintf(msg, msz, "保存失败");
        return -1;
    }
    pldmgr_log("[PLDMGR] quicklink added: %s -> %s\n", name, url);
    snprintf(msg, msz, "OK");
    return 0;
}

int quicklinks_remove(int index, char *msg, size_t msz) {
    QuicklinksStore *s = calloc(1, sizeof(QuicklinksStore));
    if (!s) {
        snprintf(msg, msz, "内存分配失败");
        return -1;
    }

    pthread_mutex_lock(&g_ql_mtx);
    load_quicklinks(s);

    if (index < 0 || index >= s->count) {
        pthread_mutex_unlock(&g_ql_mtx);
        free(s);
        snprintf(msg, msz, "无效的条目序号");
        return -1;
    }

    /* 删除即抑制：不论 auto/manual，URL 记入 hidden，避免下一次同步又把 auto 条目加回来 */
    if (ql_hidden_add(s, s->entries[index].url) != 0)
        pldmgr_log("[PLDMGR] quicklinks: hidden list full, %s may reappear\n", s->entries[index].url);

    for (int i = index; i < s->count - 1; i++)
        s->entries[i] = s->entries[i + 1];
    s->count--;

    int rc = save_quicklinks(s);
    pthread_mutex_unlock(&g_ql_mtx);
    free(s);
    if (rc != 0) {
        snprintf(msg, msz, "保存失败");
        return -1;
    }
    snprintf(msg, msz, "OK");
    return 0;
}
