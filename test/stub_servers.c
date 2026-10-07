/* [CN-FORK] v0.5.2.142 宿主测试用的极小 HTTP / FTP 桩。见 stub_servers.h。 */

#define _GNU_SOURCE
#include "stub_servers.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ── 公共小工具 ─────────────────────────────────────────────────────────── */
/* 协议交互出问题时靠它看真相（export STUB_VERBOSE=1）。默认静音。 */
static int stub_dbg(void) {
    static int d = -1;
    if (d < 0) d = getenv("STUB_VERBOSE") ? 1 : 0;
    return d;
}
#define STUB_DBG(...)                                     \
    do {                                                  \
        if (stub_dbg()) fprintf(stderr, "[stub] " __VA_ARGS__); \
    } while (0)

static int stub_listen(int *out_port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 只在本机，绝不对局域网暴露 */
    a.sin_port = 0;                             /* 随机端口，避免与真机/别的测试撞 */
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }
    if (listen(fd, 8) != 0) { close(fd); return -1; }
    socklen_t al = sizeof(a);
    if (getsockname(fd, (struct sockaddr *)&a, &al) != 0) { close(fd); return -1; }
    *out_port = ntohs(a.sin_port);
    return fd;
}

static int mkdirs(const char *path) {
    char p[1400];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        mkdir(p, 0755);
        *s = '/';
    }
    return mkdir(p, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

/* 带缓冲的行读取。两个桩都是行式协议，逐字节 recv 太慢也不好写。 */
typedef struct {
    int fd;
    char buf[8192];
    int n, pos;
} br_t;

static int br_getc(br_t *b) {
    if (b->pos >= b->n) {
        b->n = (int)recv(b->fd, b->buf, sizeof(b->buf), 0);
        b->pos = 0;
        if (b->n <= 0) return -1;
    }
    return (unsigned char)b->buf[b->pos++];
}

/* 读一行（到 '\n'，剥掉 \r\n）。返回长度，-1 = 对端关了。 */
static int br_line(br_t *b, char *out, int cap) {
    int n = 0;
    for (;;) {
        int c = br_getc(b);
        if (c < 0) return -1;
        if (c == '\n') break;
        if (c == '\r') continue;
        if (n < cap - 1) out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}

static int br_read(br_t *b, char *out, int want) {
    int got = 0;
    while (got < want) {
        if (b->pos >= b->n) {
            b->n = (int)recv(b->fd, b->buf, sizeof(b->buf), 0);
            b->pos = 0;
            if (b->n <= 0) break;
        }
        int take = b->n - b->pos;
        if (take > want - got) take = want - got;
        memcpy(out + got, b->buf + b->pos, (size_t)take);
        b->pos += take;
        got += take;
    }
    return got;
}

static int send_all(int fd, const void *p, size_t n) {
    const char *s = (const char *)p;
    size_t off = 0;
    while (off < n) {
        ssize_t k = send(fd, s + off, n - off, 0);
        if (k <= 0) return -1;
        off += (size_t)k;
    }
    return 0;
}

static long file_size(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static int read_file(const char *p, char **out, size_t *outn) {
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    char *b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return -1; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    *out = b;
    *outn = got;
    return 0;
}

static int write_file(const char *p, const char *data, size_t n) {
    char parent[1400];
    snprintf(parent, sizeof(parent), "%s", p);
    char *sl = strrchr(parent, '/');
    if (sl) { *sl = 0; mkdirs(parent); }
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    size_t w = n ? fwrite(data, 1, n, f) : 0;
    fclose(f);
    return (n == 0 || w == n) ? 0 : -1;
}

/* ══ HTTP 桩 ═══════════════════════════════════════════════════════════════ */
static int g_http_lfd = -1, g_http_port = 0;
static pthread_t g_http_th;
static volatile int g_http_run = 0;
static char g_http_root[600];

static void http_reply(int c, int code, const char *reason, const char *body, size_t bn) {
    char h[256];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.0 %d %s\r\nContent-Length: %zu\r\n"
                     "Content-Type: application/octet-stream\r\nConnection: close\r\n\r\n",
                     code, reason, bn);
    send_all(c, h, (size_t)n);
    if (bn) send_all(c, body, bn);
}

static void http_handle(int c) {
    br_t b;
    memset(&b, 0, sizeof(b));
    b.fd = c;

    char line[2048];
    if (br_line(&b, line, sizeof(line)) < 0) return;
    char method[16], path[1024], ver[32];
    if (sscanf(line, "%15s %1023s %31s", method, path, ver) != 3) return;
    STUB_DBG("HTTP %s %s\n", method, path);

    long clen = -1;
    for (;;) {
        if (br_line(&b, line, sizeof(line)) < 0) return;
        if (!line[0]) break;
        char key[128], val[512];
        if (sscanf(line, "%127[^:]: %511[^\r\n]", key, val) == 2) {
            for (char *p = key; *p; p++)
                if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
            if (!strcmp(key, "content-length")) clen = atol(val);
        }
    }

    /* URL 路径原样映射到 root 下（<TID>/<uid>.zip 那两层就是这么落盘的）。 */
    char full[1400];
    snprintf(full, sizeof(full), "%s%s", g_http_root, path);

    if (!strcmp(method, "PUT") || !strcmp(method, "POST")) {
        if (clen < 0) { http_reply(c, 411, "Length Required", NULL, 0); return; }
        char *body = (char *)malloc((size_t)clen + 1);
        if (!body) { http_reply(c, 500, "OOM", NULL, 0); return; }
        int got = br_read(&b, body, (int)clen);
        int rc = (got == clen) ? write_file(full, body, (size_t)clen) : -1;
        free(body);
        if (rc != 0) { http_reply(c, 500, "Write Failed", NULL, 0); return; }
        http_reply(c, 201, "Created", NULL, 0);
        return;
    }
    if (!strcmp(method, "GET")) {
        char *body = NULL;
        size_t bn = 0;
        if (read_file(full, &body, &bn) != 0) {
            http_reply(c, 404, "Not Found", NULL, 0);
            return;
        }
        http_reply(c, 200, "OK", body, bn);
        free(body);
        return;
    }
    http_reply(c, 405, "Method Not Allowed", NULL, 0);
}

static void *http_loop(void *arg) {
    (void)arg;
    while (g_http_run) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int c = accept(g_http_lfd, (struct sockaddr *)&ca, &cl);
        if (c < 0) {
            if (!g_http_run) break;
            usleep(1000);
            continue;
        }
        http_handle(c);
        close(c);
    }
    return NULL;
}

int stub_http_start(const char *root) {
    if (g_http_lfd >= 0) return 0;
    snprintf(g_http_root, sizeof(g_http_root), "%s", root);
    mkdirs(g_http_root);
    g_http_lfd = stub_listen(&g_http_port);
    if (g_http_lfd < 0) return -1;
    g_http_run = 1;
    if (pthread_create(&g_http_th, NULL, http_loop, NULL) != 0) {
        close(g_http_lfd);
        g_http_lfd = -1;
        g_http_run = 0;
        return -1;
    }
    return 0;
}

void stub_http_stop(void) {
    if (g_http_lfd < 0) return;
    g_http_run = 0;
    shutdown(g_http_lfd, SHUT_RDWR);
    close(g_http_lfd);
    g_http_lfd = -1;
    pthread_join(g_http_th, NULL);
}

int stub_http_port(void) { return g_http_port; }

/* ══ FTP 桩 ═══════════════════════════════════════════════════════════════ */
static int g_ftp_lfd = -1, g_ftp_port = 0;
static pthread_t g_ftp_th;
static volatile int g_ftp_run = 0;
static char g_ftp_root[600];

/* 虚拟路径归一化：处理开头的 '/'、"." 与 ".."，拼到 root 下。
 * **必须做**：curl 会 CWD 到 URL 里的目录，而我们要验证的正是
 * 「远端目录还不存在时能不能自己建出来」。 */
static int ftp_real(const char *cwd, const char *arg, char *out, size_t osz) {
    char v[900];
    if (arg && arg[0] == '/')
        snprintf(v, sizeof(v), "%s", arg);
    else if (cwd && cwd[0])
        snprintf(v, sizeof(v), "%s/%s", cwd, arg ? arg : "");
    else
        snprintf(v, sizeof(v), "/%s", arg ? arg : "");

    char norm[900];
    size_t np = 0;
    norm[np++] = '/';
    const char *s = v;
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;
        const char *e = s;
        while (*e && *e != '/') e++;
        size_t len = (size_t)(e - s);
        if (len == 1 && s[0] == '.') {
            /* 当前目录：什么都不做 */
        } else if (len == 2 && s[0] == '.' && s[1] == '.') {
            while (np > 1 && norm[np - 1] != '/') np--;
            if (np > 1) np--;
        } else {
            if (np > 1 && norm[np - 1] != '/') norm[np++] = '/';
            if (np + len + 1 >= sizeof(norm)) return -1;
            memcpy(norm + np, s, len);
            np += len;
        }
        s = e;
    }
    norm[np] = 0;
    int n = snprintf(out, osz, "%s%s", g_ftp_root, norm);
    return (n > 0 && (size_t)n < osz) ? 0 : -1;
}

static void ftp_line(int c, const char *fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b) - 3, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    b[n] = '\r';
    b[n + 1] = '\n';
    b[n + 2] = 0;
    send_all(c, b, (size_t)(n + 2));
}

/* PASV / EPSV 各建一个临时监听口，返回端口。 */
static int ftp_pasv_open(int *out_port) {
    int fd = stub_listen(out_port);
    return fd;
}

static int ftp_data_accept(int lfd) {
    struct sockaddr_in ca;
    socklen_t cl = sizeof(ca);
    return accept(lfd, (struct sockaddr *)&ca, &cl);
}

static void ftp_handle(int c) {
    char cwd[900] = "/";
    int pasv = -1;
    ftp_line(c, "220 NayePlmTools stub FTP");

    br_t b;
    memset(&b, 0, sizeof(b));
    b.fd = c;

    char line[1024];
    for (;;) {
        if (br_line(&b, line, sizeof(line)) < 0) break;
        if (!line[0]) continue;
        char cmd[32], arg[900];
        arg[0] = 0;
        int n = sscanf(line, "%31s %899[^\r\n]", cmd, arg);
        if (n < 1) continue;
        for (char *p = cmd; *p; p++)
            if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
        STUB_DBG("FTP %s %s (cwd=%s)\n", cmd, arg, cwd);

        if (!strcmp(cmd, "USER")) { ftp_line(c, "331 need password"); continue; }
        if (!strcmp(cmd, "PASS")) { ftp_line(c, "230 logged in"); continue; }
        if (!strcmp(cmd, "SYST")) { ftp_line(c, "215 UNIX Type: L8"); continue; }
        /* FEAT 只回一行结束标记、不列任何特性：curl 就不会去试 MLSD/MDTM，
         * 那些我们没实现，试了会变成一串没意义的 502。 */
        if (!strcmp(cmd, "FEAT")) { ftp_line(c, "211 End"); continue; }
        if (!strcmp(cmd, "OPTS")) { ftp_line(c, "200 ok"); continue; }
        if (!strcmp(cmd, "TYPE")) { ftp_line(c, "200 type set"); continue; }
        if (!strcmp(cmd, "PWD") || !strcmp(cmd, "XPWD")) {
            ftp_line(c, "257 \"%s\"", cwd);
            continue;
        }
        if (!strcmp(cmd, "CWD")) {
            char real[1400];
            struct stat st;
            if (ftp_real(cwd, arg, real, sizeof(real)) == 0 && stat(real, &st) == 0 &&
                S_ISDIR(st.st_mode)) {
                if (arg[0] == '/') snprintf(cwd, sizeof(cwd), "%s", arg);
                else if (!strcmp(arg, "..")) {
                    char *sl = strrchr(cwd, '/');
                    if (sl && sl != cwd) *sl = 0;
                    else snprintf(cwd, sizeof(cwd), "/");
                } else if (strcmp(arg, ".") != 0) {
                    if (strcmp(cwd, "/") != 0)
                        snprintf(cwd + strlen(cwd), sizeof(cwd) - strlen(cwd), "/%s", arg);
                    else
                        snprintf(cwd, sizeof(cwd), "/%s", arg);
                }
                ftp_line(c, "250 ok");
            } else {
                ftp_line(c, "550 no such directory");
            }
            continue;
        }
        /* MKD 要能一次建出多层（curl 的 create-missing-dirs 会把整条目录路径
         * 丢过来），所以是 mkdir -p 的语义。 */
        if (!strcmp(cmd, "MKD") || !strcmp(cmd, "XMKD")) {
            char real[1400];
            if (ftp_real(cwd, arg, real, sizeof(real)) != 0) {
                ftp_line(c, "550 bad path");
                continue;
            }
            if (mkdirs(real) == 0) ftp_line(c, "257 \"%s\" created", arg);
            else ftp_line(c, "550 mkdir failed");
            continue;
        }
        if (!strcmp(cmd, "PASV")) {
            if (pasv >= 0) close(pasv);
            int p = 0;
            pasv = ftp_pasv_open(&p);
            if (pasv < 0) { ftp_line(c, "425 cannot open data port"); continue; }
            ftp_line(c, "227 Entering Passive Mode (127,0,0,1,%d,%d)", p >> 8, p & 0xff);
            continue;
        }
        if (!strcmp(cmd, "EPSV")) {
            if (pasv >= 0) close(pasv);
            int p = 0;
            pasv = ftp_pasv_open(&p);
            if (pasv < 0) { ftp_line(c, "425 cannot open data port"); continue; }
            ftp_line(c, "229 Entering Extended Passive Mode (|||%d|)", p);
            continue;
        }
        if (!strcmp(cmd, "SIZE")) {
            char real[1400];
            long sz = (ftp_real(cwd, arg, real, sizeof(real)) == 0) ? file_size(real) : -1;
            if (sz < 0) ftp_line(c, "550 not found");
            else ftp_line(c, "213 %ld", sz);
            continue;
        }
        if (!strcmp(cmd, "STOR") || !strcmp(cmd, "RETR")) {
            char real[1400];
            if (pasv < 0 || ftp_real(cwd, arg, real, sizeof(real)) != 0) {
                ftp_line(c, "550 bad path");
                continue;
            }
            int d = ftp_data_accept(pasv);
            close(pasv);
            pasv = -1;
            if (d < 0) { ftp_line(c, "425 no data connection"); continue; }
            ftp_line(c, "150 opening data connection");
            if (!strcmp(cmd, "STOR")) {
                char *body = (char *)malloc(1 << 20);
                size_t cap = 1 << 20, n2 = 0;
                if (!body) { close(d); ftp_line(c, "451 oom"); continue; }
                for (;;) {
                    if (n2 == cap) {
                        cap *= 2;
                        char *nb = (char *)realloc(body, cap);
                        if (!nb) break;
                        body = nb;
                    }
                    ssize_t k = recv(d, body + n2, cap - n2, 0);
                    if (k <= 0) break;
                    n2 += (size_t)k;
                }
                close(d);
                int rc = write_file(real, body, n2);
                free(body);
                ftp_line(c, rc == 0 ? "226 transfer complete" : "451 write failed");
            } else {
                char *body = NULL;
                size_t bn = 0;
                if (read_file(real, &body, &bn) != 0) {
                    close(d);
                    ftp_line(c, "550 not found");
                    continue;
                }
                send_all(d, body, bn);
                free(body);
                close(d);
                ftp_line(c, "226 transfer complete");
            }
            continue;
        }
        if (!strcmp(cmd, "QUIT")) { ftp_line(c, "221 bye"); break; }
        if (!strcmp(cmd, "NOOP")) { ftp_line(c, "200 ok"); continue; }
        ftp_line(c, "502 not implemented");
    }
    if (pasv >= 0) close(pasv);
}

static void *ftp_loop(void *arg) {
    (void)arg;
    while (g_ftp_run) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int c = accept(g_ftp_lfd, (struct sockaddr *)&ca, &cl);
        if (c < 0) {
            if (!g_ftp_run) break;
            usleep(1000);
            continue;
        }
        ftp_handle(c);
        close(c);
    }
    return NULL;
}

int stub_ftp_start(const char *root) {
    if (g_ftp_lfd >= 0) return 0;
    snprintf(g_ftp_root, sizeof(g_ftp_root), "%s", root);
    mkdirs(g_ftp_root);
    g_ftp_lfd = stub_listen(&g_ftp_port);
    if (g_ftp_lfd < 0) return -1;
    g_ftp_run = 1;
    if (pthread_create(&g_ftp_th, NULL, ftp_loop, NULL) != 0) {
        close(g_ftp_lfd);
        g_ftp_lfd = -1;
        g_ftp_run = 0;
        return -1;
    }
    return 0;
}

void stub_ftp_stop(void) {
    if (g_ftp_lfd < 0) return;
    g_ftp_run = 0;
    shutdown(g_ftp_lfd, SHUT_RDWR);
    close(g_ftp_lfd);
    g_ftp_lfd = -1;
    pthread_join(g_ftp_th, NULL);
}

int stub_ftp_port(void) { return g_ftp_port; }
