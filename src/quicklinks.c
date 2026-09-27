#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "quicklinks.h"
#include "json_helpers.h"
#include "pldmgr.h"

/* ── Entry type & file I/O（照 sources.c 的骨架，无默认条目、按下标管理） ── */

typedef struct {
    char name[128];
    char url[512];
} QuickLink;

static int load_quicklinks(QuickLink *out, int *out_count) {
    char *json = NULL;
    size_t size = 0;
    int count = 0;

    memset(out, 0, sizeof(QuickLink) * MAX_QUICKLINKS);
    *out_count = 0;

    if (read_file_text(QUICKLINKS_CONFIG_PATH, &json, &size) != 0 || !json || size == 0) {
        if (json) free(json);
        return 0; /* 没有文件 = 空清单，不是错误 */
    }

    const char *arr_start = strstr(json, "\"entries\"");
    if (!arr_start) { free(json); return 0; }
    arr_start = strchr(arr_start, '[');
    if (!arr_start) { free(json); return 0; }

    const char *p = arr_start + 1;
    while (count < MAX_QUICKLINKS && (p = strchr(p, '{')) != NULL) {
        const char *end = strchr(p, '}');
        if (!end) break;

        char name[128] = "", url[512] = "";
        json_extract_string(p, end, "name", name, sizeof(name));
        json_extract_string(p, end, "url",  url,  sizeof(url));
        if (name[0] && url[0]) {
            strncpy(out[count].name, name, sizeof(out[count].name) - 1);
            strncpy(out[count].url,  url,  sizeof(out[count].url) - 1);
            count++;
        }
        p = end + 1;
    }

    free(json);
    *out_count = count;
    return 0;
}

static int save_quicklinks(QuickLink *links, int count) {
    char buf[16384];
    JsonListBuilder jb = { buf, sizeof(buf), 0, 1 };

    ensure_dir_recursive(BASE_DATA_DIR);

    json_append(&jb, "{\"entries\":[\n");
    for (int i = 0; i < count; i++) {
        char name_e[256], url_e[1024];
        pldmgr_json_escape(links[i].name, name_e, sizeof(name_e));
        pldmgr_json_escape(links[i].url,  url_e,  sizeof(url_e));
        if (json_append(&jb, "  {\"name\":\"%s\",\"url\":\"%s\"}%s\n",
            name_e, url_e, (i < count - 1) ? "," : "") != 0) {
            break;
        }
    }
    json_append(&jb, "]}\n");

    return write_file_text(QUICKLINKS_CONFIG_PATH, buf, jb.pos);
}

/* ── TCP 探活：解析 URL 的 host:port，非阻塞 connect + select 限时 ────────── */

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
    QuickLink *links = calloc(MAX_QUICKLINKS, sizeof(QuickLink));
    if (!links) {
        snprintf(buf, size, "{\"entries\":[]}");
        return -1;
    }
    int count = 0;
    load_quicklinks(links, &count);

    JsonListBuilder jb = { buf, size, 0, 1 };
    buf[0] = '\0';

    json_append(&jb, "{\"entries\":[\n");
    for (int i = 0; i < count; i++) {
        char name_e[256], url_e[1024];
        pldmgr_json_escape(links[i].name, name_e, sizeof(name_e));
        pldmgr_json_escape(links[i].url,  url_e,  sizeof(url_e));
        if (json_append(&jb, "  {\"name\":\"%s\",\"url\":\"%s\",\"alive\":%s}%s\n",
            name_e, url_e, probe_url(links[i].url) ? "true" : "false",
            (i < count - 1) ? "," : "") != 0) {
            break;
        }
    }
    json_append(&jb, "]}\n");
    free(links);
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

    QuickLink *links = calloc(MAX_QUICKLINKS, sizeof(QuickLink));
    if (!links) {
        snprintf(msg, msz, "内存分配失败");
        return -1;
    }
    int count = 0;
    load_quicklinks(links, &count);

    if (count >= MAX_QUICKLINKS) {
        free(links);
        snprintf(msg, msz, "条目已达上限 %d", MAX_QUICKLINKS);
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(links[i].url, url) == 0) {
            free(links);
            snprintf(msg, msz, "该 URL 已存在");
            return -1;
        }
    }

    strncpy(links[count].name, name, sizeof(links[count].name) - 1);
    strncpy(links[count].url,  url,  sizeof(links[count].url) - 1);
    count++;

    if (save_quicklinks(links, count) != 0) {
        free(links);
        snprintf(msg, msz, "保存失败");
        return -1;
    }
    free(links);
    pldmgr_log("[PLDMGR] quicklink added: %s -> %s\n", name, url);
    snprintf(msg, msz, "OK");
    return 0;
}

int quicklinks_remove(int index, char *msg, size_t msz) {
    QuickLink *links = calloc(MAX_QUICKLINKS, sizeof(QuickLink));
    if (!links) {
        snprintf(msg, msz, "内存分配失败");
        return -1;
    }
    int count = 0;
    load_quicklinks(links, &count);

    if (index < 0 || index >= count) {
        free(links);
        snprintf(msg, msz, "无效的条目序号");
        return -1;
    }

    for (int i = index; i < count - 1; i++)
        links[i] = links[i + 1];
    count--;

    if (save_quicklinks(links, count) != 0) {
        free(links);
        snprintf(msg, msz, "保存失败");
        return -1;
    }
    free(links);
    snprintf(msg, msz, "OK");
    return 0;
}
