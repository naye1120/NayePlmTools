/* [CN-FORK] v0.5.2.143 存档的远程传输层（FTP / HTTP 走 libcurl，SMB 走 libsmb2）。
 * 设计（三条协议线的来历、远端布局、为什么文件名不带时间戳、libsmb2 的两条坑）
 * 见 include/save_remote.h 的文件头。 */

#include "save_remote.h"

#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* [CN-FORK] v0.5.2.172 sr_fetch 落盘后 stat 取字节数 */
#include <unistd.h>

/* libsmb2 的头文件**不自包含**，顺序也不能换：
 *   <stdint.h>  → 否则报 unknown type name 'uint8_t' / 'uint32_t'（smb2.h 内部用定宽类型却不带它）
 *   <time.h>    → 否则报 unknown type name 'time_t'
 *   <smb2/smb2.h> → 否则报 unknown type name 'SMB2_GUID_SIZE' / 'smb2_lease_key'
 * 这三条是 6.2 的实际坑，别「顺手整理」掉。 */
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

#include "game_saves.h"
#include "cJSON.h"        /* [CN-FORK] v0.5.2.145 sr_stat 解析 <uid>.meta.json 的 files */
#include "json_helpers.h" /* ensure_dir_recursive */
#include "pldmgr.h"       /* pldmgr_log */
#include "utils.h"        /* pldmgr_curl_init */

/* 传输超时。真机实测的存档是 4~20 MB，家用网络 + 对面是台 NAS 的话
 * 几十秒很正常 —— 给 180 秒，别让一次正常的推送被判成失败。
 * 但**必须**配 LOW_SPEED：对面掉线时 TCP 会一直挂着不报错，
 * 没有这条就会把连接线程/守护线程永远占住。 */
#define SR_CONNECT_TIMEOUT 6L
#define SR_IO_TIMEOUT 180L
#define SR_LOW_SPEED_BYTES 1L
#define SR_LOW_SPEED_SEC 60L

#define SR_URL_MAX 1200
#define SR_ERR_MAX 256

/* SMB 侧的额外常量。libsmb2 的超时是**每个 PDU** 的，不是整次传输 ——
 * 一包最多 1 MB，60 秒还写不完那就是对面挂了，别等到 180 秒。 */
#define SR_SMB_TIMEOUT 60
#define SR_SMB_PATH_MAX 700
#define SR_SMB_CHUNK_MAX (1024 * 1024)
#define SR_SMB_CHUNK_MIN (64 * 1024)

/* ── 协议支持 ───────────────────────────────────────────────────────────── */
int sr_mode_supported(const char *mode) {
    return (mode && (!strcmp(mode, "ftp") || !strcmp(mode, "http") ||
                     !strcmp(mode, "smb"))) ? 1 : 0;
}

/* 路径段闸门：tid / uid 会被拼进远端路径，**这一层就是远程侧的路径穿越防线**
 * （`..` 与分隔符一律拿不到）。与 game_saves.c 的 gs_tid_ok / gs_uid_ok 是两套
 * 独立的校验，刻意不互相依赖 —— 那条链上有任何一环被放宽，这里还挡得住。 */
static int sr_seg_ok(const char *s) {
    if (!s || !*s || strlen(s) > 128) return 0;
    if (strstr(s, "..")) return 0;
    for (const char *p = s; *p; p++)
        if (*p == '/' || *p == '\\' || (unsigned char)*p < 0x20) return 0;
    return 1;
}

static void sr_trim_slash(char *s) {
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == '/') s[--n] = 0;
}

/* SMB 的共享名与子目录：用户可能敲 `/share/` 也可能敲 `share\`，
 * 两种分隔符都认，统一去掉首尾 —— 不归一的话推上去和拉下来的路径就不是同一个，
 * 而且两边都不报错。顺手把内部分隔符统一成反斜杠，见 smb_join。 */
static void smb_norm(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '/' || s[n - 1] == '\\')) s[--n] = 0;
    const char *p = s;
    while (*p == '/' || *p == '\\') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    for (char *q = s; *q; q++)
        if (*q == '/') *q = '\\';
}

/* 共享内相对路径的拼接。**分隔符必须是反斜杠**：libsmb2 只有 smb2_rename 会
 * 把 '/' 转成 '\'，open / stat / mkdir / unlink 都是原样把名字发出去，
 * 用 '/' 在不少服务器上会被当成文件名的一部分。 */
static void smb_join(char *out, size_t osz, const char *a, const char *b) {
    if (!out || !osz) return;
    if (!a || !*a) snprintf(out, osz, "%s", b ? b : "");
    else if (!b || !*b) snprintf(out, osz, "%s", a);
    else snprintf(out, osz, "%s\\%s", a, b);
}

/* 展示用（smb:// URL）归一：去掉首尾分隔符，内部统一成**正斜杠**。
 * 与 smb_norm 刻意分开——smb_norm 内部转反斜杠（那是给 open/stat 当线路径的，
 * 不少服务器只认反斜杠），而 smb:// URL 按惯例用正斜杠、给用户看也更自然。
 * 两处各用各的，别因为「长得像」就并成一个。 */
static void smb_url_norm(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '/' || s[n - 1] == '\\')) s[--n] = 0;
    const char *p = s;
    while (*p == '/' || *p == '\\') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    for (char *q = s; *q; q++)
        if (*q == '\\') *q = '/';
}

int sr_build_url(const sr_cfg_t *c, const char *tid, const char *uid,
                 char *out, size_t osz, char *err, size_t errsz) {
    if (err && errsz) err[0] = 0;
    if (!out || osz < 64) return -1;
    out[0] = 0;
    if (!c) {
        if (err) snprintf(err, errsz, "没有配置远程位置");
        return -1;
    }
    if (!sr_seg_ok(tid) || !sr_seg_ok(uid)) {
        if (err) snprintf(err, errsz, "标题 ID 或用户标识不合法");
        return -1;
    }

    if (!strcmp(c->mode, "ftp")) {
        if (!c->host[0]) {
            if (err) snprintf(err, errsz, "没有填 FTP 主机地址");
            return -1;
        }
        int port = (c->port > 0 && c->port < 65536) ? c->port : 21;
        /* 远端目录：前后斜杠都归一成一个，空配置就是根目录。
         * 不归一的话「/ps5save/」与「/ps5save」会拼出两个不同的路径 ——
         * 推上去的文件和拉下来的位置就不是同一个，而且两边都不报错。 */
        char dir[700];
        dir[0] = 0;
        if (c->path[0]) {
            const char *p = c->path;
            while (*p == '/') p++;
            snprintf(dir, sizeof(dir), "/%s", p);
            sr_trim_slash(dir);
        }
        int n = snprintf(out, osz, "ftp://%s:%d%s/%s/%s.zip", c->host, port, dir, tid, uid);
        if (n <= 0 || (size_t)n >= osz) {
            if (err) snprintf(err, errsz, "地址过长");
            return -1;
        }
        return 0;
    }

    if (!strcmp(c->mode, "http")) {
        if (!c->url[0]) {
            if (err) snprintf(err, errsz, "没有填 HTTP 地址");
            return -1;
        }
        if (strncmp(c->url, "http://", 7) != 0 && strncmp(c->url, "https://", 8) != 0) {
            if (err) snprintf(err, errsz, "HTTP 地址要以 http:// 或 https:// 开头");
            return -1;
        }
        char base[700];
        snprintf(base, sizeof(base), "%s", c->url);
        sr_trim_slash(base);
        int n = snprintf(out, osz, "%s/%s/%s.zip", base, tid, uid);
        if (n <= 0 || (size_t)n >= osz) {
            if (err) snprintf(err, errsz, "地址过长");
            return -1;
        }
        return 0;
    }

    if (!strcmp(c->mode, "smb")) {
        if (!c->host[0]) {
            if (err) snprintf(err, errsz, "没有填 SMB 主机地址");
            return -1;
        }
        if (!c->share[0]) {
            if (err) snprintf(err, errsz, "没有填 SMB 共享名");
            return -1;
        }
        int port = (c->port > 0 && c->port < 65536) ? c->port : 445;
        char share[300], sub[600];
        snprintf(share, sizeof(share), "%s", c->share);
        smb_url_norm(share);
        sub[0] = 0;
        if (c->sub[0]) {
            snprintf(sub, sizeof(sub), "%s", c->sub);
            smb_url_norm(sub);
        }
        /* 与 ftp 完全同构：<base>/<TID>/<uid>.zip，sub 就是 ftp 的 path。
         * libsmb2 的 server 串支持 `<host>[:<port>]`，端口写在这里而不是另设。 */
        int n = snprintf(out, osz, "smb://%s:%d/%s%s%s/%s/%s.zip",
                         c->host, port, share, sub[0] ? "/" : "", sub, tid, uid);
        if (n <= 0 || (size_t)n >= osz) {
            if (err) snprintf(err, errsz, "地址过长");
            return -1;
        }
        return 0;
    }

    if (err) snprintf(err, errsz, "不支持的传输方式（%s）", c->mode);
    return -1;
}

/* ── curl 小工具 ────────────────────────────────────────────────────────── */
/* 把 curl 的英文错误码翻成人话。前端的 toast 直接显示这句 ——
 * 给用户看 "Couldn't connect to server" 等于什么都没说。 */
static const char *sr_curl_msg(CURLcode rc) {
    switch (rc) {
        case CURLE_COULDNT_RESOLVE_HOST:   return "主机名解析不了（检查地址拼写）";
        case CURLE_COULDNT_CONNECT:        return "连不上这个地址（检查主机与端口）";
        case CURLE_OPERATION_TIMEDOUT:     return "连接或传输超时";
        case CURLE_LOGIN_DENIED:           return "用户名或密码不对";
        case CURLE_REMOTE_FILE_NOT_FOUND:  return "远程没有这个文件";
        case CURLE_UPLOAD_FAILED:          return "远程拒绝了这次上传（可能没有写权限）";
        case CURLE_REMOTE_ACCESS_DENIED:   return "远程拒绝访问（权限不足）";
        case CURLE_SSL_CONNECT_ERROR:      return "TLS 握手失败";
        case CURLE_PARTIAL_FILE:           return "传输中断，收到的字节不完整";
        case CURLE_WRITE_ERROR:            return "写本地文件失败（磁盘满？）";
        case CURLE_ABORTED_BY_CALLBACK:    return "传输被中止";
        default:                           return curl_easy_strerror(rc);
    }
}

/* 两种传输共用的选项：超时、NOSIGNAL、低速熔断。
 * **NOSIGNAL 必须开** —— 我们在多线程里用 curl，默认的 alarm 超时会把进程打死。 */
static void sr_common_opts(CURL *cu, const char *url) {
    curl_easy_setopt(cu, CURLOPT_URL, url);
    curl_easy_setopt(cu, CURLOPT_CONNECTTIMEOUT, SR_CONNECT_TIMEOUT);
    curl_easy_setopt(cu, CURLOPT_TIMEOUT, SR_IO_TIMEOUT);
    curl_easy_setopt(cu, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(cu, CURLOPT_LOW_SPEED_LIMIT, SR_LOW_SPEED_BYTES);
    curl_easy_setopt(cu, CURLOPT_LOW_SPEED_TIME, SR_LOW_SPEED_SEC);
}

static size_t sr_write_file(void *ptr, size_t sz, size_t nm, void *ud) {
    FILE *f = (FILE *)ud;
    if (!f) return 0;
    return fwrite(ptr, 1, sz * nm, f);
}

static size_t sr_read_file(char *ptr, size_t sz, size_t nm, void *ud) {
    FILE *f = (FILE *)ud;
    if (!f) return 0;
    return fread(ptr, 1, sz * nm, f);
}

/* 从内存块读（meta.json 这类小内容直接 PUT，不必落临时文件）。 */
struct sr_mem_src { const char *p; size_t len; size_t off; };
static size_t sr_read_mem(char *ptr, size_t sz, size_t nm, void *ud) {
    struct sr_mem_src *m = (struct sr_mem_src *)ud;
    if (!m || m->off >= m->len) return 0;
    size_t want = sz * nm;
    if (want > m->len - m->off) want = m->len - m->off;
    memcpy(ptr, m->p + m->off, want);
    m->off += want;
    return want;
}

/* [CN-FORK] v0.5.2.145 把一小段字节（目前只给 <uid>.meta.json 用）上传到 url。
 * 协议选项与 sr_push 的 FTP/HTTP 分支保持一致。失败只回 -1、不向上抛 ——
 * meta 只是「展示用」辅助信息，写失败不能让一次成功的存档推送变成失败。 */
static int sr_curl_put_bytes(const sr_cfg_t *c, const char *url,
                             const char *body, size_t len) {
    pldmgr_curl_init();
    CURL *cu = curl_easy_init();
    if (!cu) return -1;
    struct curl_slist *hdrs = NULL;
    sr_common_opts(cu, url);
    curl_easy_setopt(cu, CURLOPT_UPLOAD, 1L);
    struct sr_mem_src mb = { body, len, 0 };
    curl_easy_setopt(cu, CURLOPT_READFUNCTION, sr_read_mem);
    curl_easy_setopt(cu, CURLOPT_READDATA, &mb);
    curl_easy_setopt(cu, CURLOPT_INFILESIZE_LARGE, (curl_off_t)len);
    if (!strcmp(c->mode, "ftp")) {
        if (c->user[0]) {
            curl_easy_setopt(cu, CURLOPT_USERNAME, c->user);
            curl_easy_setopt(cu, CURLOPT_PASSWORD, c->password);
        }
        curl_easy_setopt(cu, CURLOPT_FTP_CREATE_MISSING_DIRS, (long)CURLFTP_CREATE_DIR_RETRY);
        curl_easy_setopt(cu, CURLOPT_FTP_USE_EPSV, 1L);
    } else {
        curl_easy_setopt(cu, CURLOPT_CUSTOMREQUEST, "PUT");
        hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
        hdrs = curl_slist_append(hdrs, "Expect:");
        if (c->token[0]) {
            char auth[400];
            snprintf(auth, sizeof(auth), "Authorization: Bearer %s", c->token);
            hdrs = curl_slist_append(hdrs, auth);
        }
        curl_easy_setopt(cu, CURLOPT_HTTPHEADER, hdrs);
    }
    CURLcode rc = curl_easy_perform(cu);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(cu);
    return (rc == CURLE_OK) ? 0 : -1;
}

/* [CN-FORK] v0.5.2.154 把一个小文件（目前只给 <uid>.meta.json 用）读进内存。
 * 与 sr_curl_put_bytes 对称：FTP 走 RETR、HTTP 走 GET，选项与 sr_pull 的
 * FTP/HTTP 分支保持一致（少一处不一致就多一种「推送能上、读取读不到」）。
 * 0 成功（*out_len 是读到的字节数，已补 '\0' 结尾）；
 * SR_ENOTFOUND = 远端明确没有这个文件；-1 = 说不清（连接 / 权限 / 其它）。
 *
 * 写回调刻意**不因超出容量而失败**：meta.json 只有一百来字节，真超了说明对面
 * 回的不是我们的文件，多出来的部分丢掉、照样回成功长度即可 —— 让 curl 中途
 * 报错反而会把「读到了一个怪文件」说成「网络断了」。 */
struct sr_mem_dst { char *p; size_t cap; size_t len; };
static size_t sr_write_mem(void *ptr, size_t sz, size_t nm, void *ud) {
    struct sr_mem_dst *m = (struct sr_mem_dst *)ud;
    if (!m) return 0;
    size_t n = sz * nm;
    if (m->len + n > m->cap) n = m->cap - m->len;
    if (n) {
        memcpy(m->p + m->len, ptr, n);
        m->len += n;
    }
    return sz * nm; /* 必须回「原样收下」，否则 curl 判写失败 */
}

static int sr_curl_get_mem(const sr_cfg_t *c, const char *url,
                           char *out, size_t cap, size_t *out_len,
                           char *err, size_t errsz) {
    if (out_len) *out_len = 0;
    if (!out || cap < 2) return -1;
    out[0] = 0;
    pldmgr_curl_init();
    CURL *cu = curl_easy_init();
    if (!cu) {
        if (err) snprintf(err, errsz, "初始化传输失败");
        return -1;
    }
    struct curl_slist *hdrs = NULL;
    struct sr_mem_dst md = { out, cap - 1, 0 };
    sr_common_opts(cu, url);
    curl_easy_setopt(cu, CURLOPT_WRITEFUNCTION, sr_write_mem);
    curl_easy_setopt(cu, CURLOPT_WRITEDATA, &md);
    curl_easy_setopt(cu, CURLOPT_FOLLOWLOCATION, 1L);
    if (!strcmp(c->mode, "ftp")) {
        if (c->user[0]) {
            curl_easy_setopt(cu, CURLOPT_USERNAME, c->user);
            curl_easy_setopt(cu, CURLOPT_PASSWORD, c->password);
        }
        curl_easy_setopt(cu, CURLOPT_FTP_USE_EPSV, 1L);
    } else if (c->token[0]) {
        char auth[400];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", c->token);
        hdrs = curl_slist_append(hdrs, auth);
        curl_easy_setopt(cu, CURLOPT_HTTPHEADER, hdrs);
    }
    CURLcode rc = curl_easy_perform(cu);
    long http = 0;
    curl_easy_getinfo(cu, CURLINFO_RESPONSE_CODE, &http);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(cu);
    if (rc != CURLE_OK) {
        /* 与 sr_pull 同一套口径：FTP 上「文件不存在」和「CWD 到不存在的目录」
         * 都是 550，libcurl 分别报 FILE_NOT_FOUND / ACCESS_DENIED，都算「没有」。 */
        if (rc == CURLE_REMOTE_FILE_NOT_FOUND || rc == CURLE_REMOTE_ACCESS_DENIED) {
            if (err) snprintf(err, errsz, "远程没有这个文件");
            return SR_ENOTFOUND;
        }
        if (err) snprintf(err, errsz, "%s", sr_curl_msg(rc));
        return -1;
    }
    if (http == 404) {
        if (err) snprintf(err, errsz, "远程没有这个文件");
        return SR_ENOTFOUND;
    }
    if (http >= 400) {
        if (err) snprintf(err, errsz, "远程拒绝了这次读取（HTTP %ld）", http);
        return -1;
    }
    out[md.len] = 0;
    if (out_len) *out_len = md.len;
    return 0;
}

/* 临时归档的落点。用 GAME_SAVES_PACK_DIR（与「下载存档」共用那个目录）——
 * 它本来就不是存档目录，专门用来放这种一次性的包，见 game_saves.h。 */
static int sr_tmp_path(const char *what, const char *tid, const char *uid,
                       char *out, size_t osz, char *err, size_t errsz) {
    if (ensure_dir_recursive(GAME_SAVES_PACK_DIR) != 0) {
        if (err) snprintf(err, errsz, "无法创建临时目录（%s）", strerror(errno));
        return -1;
    }
    int n = snprintf(out, osz, "%s/%s_%s_%s_%u.zip",
                     GAME_SAVES_PACK_DIR, what, tid, uid, (unsigned)getpid());
    if (n <= 0 || (size_t)n >= osz) {
        if (err) snprintf(err, errsz, "临时路径过长");
        return -1;
    }
    return 0;
}

/* ── SMB（libsmb2）──────────────────────────────────────────────────────── */
/* 一次推或拉对应一个 context：建连接 → 干完 → 断 → 销毁。
 * 刻意不做长连接池 —— 自动存档是几分钟一次的事，为了省一次协商去维护
 * 一批可能已经半死的 socket，不值当。 */

typedef struct {
    struct smb2_context *s;
    char dir[SR_SMB_PATH_MAX]; /* 共享内的目录：<sub>\<tid>，或 <tid> */
} smb_sess_t;

/* 把一次失败翻译成 errno。libsmb2 的同步接口返回 -errno，但只有 open / connect
 * 会把 NT 状态码留在 context 里，而 smb2_open 失败是**返回 NULL**（拿不到码）——
 * 那种情况调用方传 rc = 0，这里就改去读 context 里的 NT 状态码。
 * 别传 -1：errno 1 在 FreeBSD 上是 EPERM，会被误翻成「密码不对」。 */
static int smb_errno_of(struct smb2_context *s, int rc) {
    if (rc < 0) return -rc;
    int nt = s ? smb2_get_nterror(s) : 0;
    if (nt) {
        int e = nterror_to_errno((uint32_t)nt);
        if (e > 0) return e;
    }
    return 0;
}

/* 给用户的那句话。libsmb2 自己的错误串是英文 NT 状态（"Open failed with
 * (0xc0000022) STATUS_ACCESS_DENIED."），直接塞进 toast 等于没说。 */
static const char *smb_human(struct smb2_context *s, int rc) {
    switch (smb_errno_of(s, rc)) {
        case EACCES: case EPERM:    return "用户名或密码不对，或没有访问权限";
        case ENOENT:                return "远程没有这个文件或目录";
        case EEXIST:                return "远程已经有同名的东西";
        case ENOTDIR:               return "远程路径上有个同名文件挡着（不是目录）";
        case ENOSPC:                return "远程空间不足";
        case ECONNREFUSED:          return "对方拒绝连接（检查端口）";
        case EHOSTUNREACH: case ENETUNREACH: case ENETDOWN:
                                    return "连不上这个地址（检查主机与端口）";
        case ETIMEDOUT:             return "连接或传输超时";
        case EINVAL:                return "参数不对（检查共享名与路径）";
        case EIO:                   return "读写出错";
        default: break;
    }
    const char *e = s ? smb2_get_error(s) : NULL;
    return (e && *e) ? e : "SMB 操作失败";
}

static void smb_sess_close(smb_sess_t *ss) {
    if (!ss || !ss->s) return;
    smb2_disconnect_share(ss->s);
    smb2_destroy_context(ss->s);
    ss->s = NULL;
}

static int smb_sess_open(const sr_cfg_t *c, const char *tid, smb_sess_t *ss,
                         char *err, size_t errsz) {
    memset(ss, 0, sizeof(*ss));
    if (!c->host[0]) {
        if (err) snprintf(err, errsz, "没有填 SMB 主机地址");
        return -1;
    }
    char share[300], sub[600];
    snprintf(share, sizeof(share), "%s", c->share);
    smb_norm(share);
    if (!share[0]) {
        if (err) snprintf(err, errsz, "没有填 SMB 共享名");
        return -1;
    }
    sub[0] = 0;
    if (c->sub[0]) {
        snprintf(sub, sizeof(sub), "%s", c->sub);
        smb_norm(sub);
    }
    smb_join(ss->dir, sizeof(ss->dir), sub, tid);

    int port = (c->port > 0 && c->port < 65536) ? c->port : 445;
    char server[400];
    snprintf(server, sizeof(server), "%s:%d", c->host, port);

    struct smb2_context *s = smb2_init_context();
    if (!s) {
        if (err) snprintf(err, errsz, "初始化 SMB 失败");
        return -1;
    }
    ss->s = s;
    smb2_set_timeout(s, SR_SMB_TIMEOUT);
    /* 空用户名 + 空密码 = 访客共享。别因为「没填用户名」就不调 set_user，
     * 那样 libsmb2 会去取当前用户，PS5 上取出来的名字没有意义。 */
    smb2_set_user(s, c->user);
    smb2_set_password(s, c->password);

    int rc = smb2_connect_share(s, server, share, c->user);
    if (rc != 0) {
        if (err) snprintf(err, errsz, "连不上 SMB 共享：%s", smb_human(s, rc));
        smb_sess_close(ss);
        return -1;
    }
    return 0;
}

/* 逐级建目录，已存在就跳过（EEXIST 不算错）。
 * 不自己建的话，第一次推送时 <TID>/ 必然不存在，SMB 只会回一句
 * STATUS_OBJECT_PATH_NOT_FOUND，用户看不出是「没建目录」还是「配置错了」。 */
static int smb_mkdirs(struct smb2_context *s, const char *dir,
                      char *err, size_t errsz) {
    char cur[SR_SMB_PATH_MAX];
    const char *p = dir;
    cur[0] = 0;
    while (*p) {
        const char *q = strchr(p, '\\');
        size_t n = q ? (size_t)(q - p) : strlen(p);
        if (n > 0) {
            size_t used = strlen(cur);
            if (used + n + 2 > sizeof(cur)) {
                if (err) snprintf(err, errsz, "远程路径过长");
                return -1;
            }
            if (used) cur[used++] = '\\';
            memcpy(cur + used, p, n);
            cur[used + n] = 0;
            int rc = smb2_mkdir(s, cur);
            if (rc != 0 && rc != -EEXIST) {
                if (err) snprintf(err, errsz, "建不了远程目录（%s）：%s",
                                  cur, smb_human(s, rc));
                return -1;
            }
        }
        if (!q) break;
        p = q + 1;
    }
    return 0;
}

static int sr_smb_push(const sr_cfg_t *c, const char *tid, const char *uid,
                       const char *local, long long *out_bytes, long long pack_files,
                       long long src_mtime,
                       char *err, size_t errsz) {
    smb_sess_t ss;
    struct smb2fh *fh = NULL;
    uint8_t *buf = NULL;
    FILE *f = NULL;
    char finalp[SR_SMB_PATH_MAX], tmpp[SR_SMB_PATH_MAX];
    char nm[200], tnm[220], mnm[240], metap[SR_SMB_PATH_MAX];
    long long total = 0;
    int rc = -1;

    if (smb_sess_open(c, tid, &ss, err, errsz) != 0) return -1;

    snprintf(nm, sizeof(nm), "%s.zip", uid);
    snprintf(tnm, sizeof(tnm), "%s.zip.tmp", uid);
    smb_join(finalp, sizeof(finalp), ss.dir, nm);
    smb_join(tmpp, sizeof(tmpp), ss.dir, tnm);

    if (smb_mkdirs(ss.s, ss.dir, err, errsz) != 0) goto out;

    /* 先写 .tmp 再改名：libsmb2 没有 O_TRUNC（写了也不会截断），而且
     * 直接写正式名的话，传到一半断线就会在远端留一个半截的 <uid>.zip，
     * 下次拉回来的是一个打不开的包。改名是原子操作，远端那份要么完整
     * 要么还是旧的。上一轮崩溃留下的 .tmp 先清掉，失败无所谓。 */
    smb2_unlink(ss.s, tmpp);
    fh = smb2_open(ss.s, tmpp, O_CREAT | O_WRONLY);
    if (!fh) {
        if (err) snprintf(err, errsz, "在远程建不了临时文件：%s", smb_human(ss.s, 0));
        goto out;
    }

    uint32_t chunk = smb2_get_max_write_size(ss.s);
    if (chunk < SR_SMB_CHUNK_MIN) chunk = SR_SMB_CHUNK_MIN;
    if (chunk > SR_SMB_CHUNK_MAX) chunk = SR_SMB_CHUNK_MAX;
    /* 堆上分配：真机 MHD 连接线程只有 64 KB 栈，1 MB 的栈缓冲是自杀。 */
    buf = (uint8_t *)malloc(chunk);
    if (!buf) {
        if (err) snprintf(err, errsz, "内存不足");
        goto out;
    }

    f = fopen(local, "rb");
    if (!f) {
        if (err) snprintf(err, errsz, "打不开刚打好的包（%s）", strerror(errno));
        goto out;
    }
    for (;;) {
        size_t got = fread(buf, 1, chunk, f);
        if (got == 0) break;
        int w = smb2_write(ss.s, fh, buf, (uint32_t)got);
        if (w < 0) {
            if (err) snprintf(err, errsz, "写远程失败：%s", smb_human(ss.s, w));
            goto out;
        }
        if ((size_t)w != got) {
            if (err) snprintf(err, errsz, "写远程的字节数对不上（要 %u，实际 %d）",
                              (unsigned)got, w);
            goto out;
        }
        total += w;
    }
    fclose(f);
    f = NULL;
    smb2_fsync(ss.s, fh); /* 刷不刷得动都无所谓，改名的原子性才是关键 */
    if (smb2_close(ss.s, fh) != 0) {
        fh = NULL;
        if (err) snprintf(err, errsz, "关闭远程文件失败：%s", smb_human(ss.s, 0));
        goto out;
    }
    fh = NULL;

    /* libsmb2 的 rename 是 replace_if_exist=0，**目标已存在会直接失败**。
     * 所以先试着直接改名（正常第一次推就是这条路），撞上 EEXIST 才删旧的重来
     * —— 这样绝大多数情况下远端那份旧的全程都在，只在覆盖那一瞬短暂缺失。 */
    int r = smb2_rename(ss.s, tmpp, finalp);
    if (r == -EEXIST) {
        smb2_unlink(ss.s, finalp);
        r = smb2_rename(ss.s, tmpp, finalp);
    }
    if (r != 0) {
        smb2_unlink(ss.s, tmpp);
        if (err) snprintf(err, errsz, "替换远程存档失败：%s", smb_human(ss.s, r));
        goto out;
    }
    /* [CN-FORK] v0.5.2.145 推送成功后**顺便**写一份 <uid>.meta.json，记录这个包
     * 的真实文件数 / 字节数 / 打包时间（time_t）。前端「远程游戏」标签要展示和本地
     * 存档列表同款的信息，文件数没法从裸 <uid>.zip 直接拿到，只能我们写一份。
     *
     * [CN-FORK] v0.5.2.162 再多一个 `src_mtime`：这份包是**从主机上哪个时刻的存档**
     * 打出来的。为什么不能拿 `mtime` 当它 —— `mtime` 是这个文件在对面落盘的时刻
     * （SMB 取的是 zip 的文件 mtime，FTP/HTTP 直接写 time(NULL)），永远比存档本身晚。
     * 于是「刚推上去的这份」在拉取判定里永远算「远程更新」，自动拉取会把它原样
     * 拉回来，用户看到的就是「推完又拉、拉完又推」。src_mtime 才是可比的量：
     * 它和本地那次 game_saves_local_mtime() 取的是同一类东西（存档目录的最大 mtime），
     * 而且拉回来的文件落盘时包里的时间会被 utimes 还原，所以拉完之后再比仍然相等。
     *
     * meta 与 zip 同目录、同名换后缀；libsmb2 没有 O_TRUNC，先 unlink 旧的再写新的。
     * 写 meta 失败**不**算推送失败（zip 本体已经到位），只打日志，前端降级显示。 */
    snprintf(mnm, sizeof(mnm), "%s.meta.json", uid);
    smb_join(metap, sizeof(metap), ss.dir, mnm);
    char metab[256];
    int mlen = snprintf(metab, sizeof(metab),
                        "{\"files\":%lld,\"size\":%lld,\"mtime\":%lld,\"src_mtime\":%lld}",
                        pack_files, total, (long long)time(NULL), src_mtime);
    smb2_unlink(ss.s, metap);
    struct smb2fh *mfh = smb2_open(ss.s, metap, O_CREAT | O_WRONLY);
    if (mfh) {
        int w = smb2_write(ss.s, mfh, (uint8_t *)metab, (uint32_t)mlen);
        if (w != mlen)
            pldmgr_log("[PLDMGR] saves: meta write short (%d/%d) for %s/%s\n",
                       w, mlen, tid, uid);
        smb2_close(ss.s, mfh);
    } else {
        pldmgr_log("[PLDMGR] saves: cannot open remote meta %s (%s)\n",
                   metap, smb_human(ss.s, 0));
    }
    rc = 0;

out:
    if (f) fclose(f);
    if (fh) smb2_close(ss.s, fh);
    if (buf) free(buf);
    smb_sess_close(&ss);
    if (rc == 0 && out_bytes) *out_bytes = total;
    return rc;
}

static int sr_smb_pull(const sr_cfg_t *c, const char *tid, const char *uid,
                       const char *local, char *err, size_t errsz) {
    smb_sess_t ss;
    struct smb2fh *fh = NULL;
    uint8_t *buf = NULL;
    FILE *f = NULL;
    char rp[SR_SMB_PATH_MAX], nm[200];
    int rc = -1;

    if (smb_sess_open(c, tid, &ss, err, errsz) != 0) return -1;

    snprintf(nm, sizeof(nm), "%s.zip", uid);
    smb_join(rp, sizeof(rp), ss.dir, nm);

    /* 「远端还没有」必须单独认出来：第一次用自动存档时 <TID>/ 目录都还不存在，
     * stat 会回 OBJECT_PATH_NOT_FOUND —— libsmb2 把它翻成 ENOENT。
     * 不认出来的话，用户第一次开游戏就会看到一条红色的报错。 */
    struct smb2_stat_64 st;
    memset(&st, 0, sizeof(st));
    int r = smb2_stat(ss.s, rp, &st);
    if (r < 0) {
        if (r == -ENOENT) {
            if (err) snprintf(err, errsz, "远程还没有这个游戏的存档");
            rc = SR_ENOTFOUND;
        } else {
            if (err) snprintf(err, errsz, "查不到远程存档：%s", smb_human(ss.s, r));
        }
        goto out;
    }

    fh = smb2_open(ss.s, rp, O_RDONLY);
    if (!fh) {
        if (smb_errno_of(ss.s, 0) == ENOENT) {
            if (err) snprintf(err, errsz, "远程还没有这个游戏的存档");
            rc = SR_ENOTFOUND;
        } else {
            if (err) snprintf(err, errsz, "打不开远程存档：%s", smb_human(ss.s, 0));
        }
        goto out;
    }

    uint32_t chunk = smb2_get_max_read_size(ss.s);
    if (chunk < SR_SMB_CHUNK_MIN) chunk = SR_SMB_CHUNK_MIN;
    if (chunk > SR_SMB_CHUNK_MAX) chunk = SR_SMB_CHUNK_MAX;
    buf = (uint8_t *)malloc(chunk);
    if (!buf) {
        if (err) snprintf(err, errsz, "内存不足");
        goto out;
    }
    f = fopen(local, "wb");
    if (!f) {
        if (err) snprintf(err, errsz, "建不了临时文件（%s）", strerror(errno));
        goto out;
    }
    for (;;) {
        int got = smb2_read(ss.s, fh, buf, chunk);
        if (got < 0) {
            if (err) snprintf(err, errsz, "读远程失败：%s", smb_human(ss.s, got));
            goto out;
        }
        if (got == 0) break;
        if (fwrite(buf, 1, (size_t)got, f) != (size_t)got) {
            if (err) snprintf(err, errsz, "写本地文件失败（磁盘满？）");
            goto out;
        }
    }
    fclose(f);
    f = NULL;
    /* 读方向的 close 失败不算失败：字节都拿全了，关不掉只是远端少一次 tidy。 */
    smb2_close(ss.s, fh);
    fh = NULL;
    rc = 0;

out:
    if (f) fclose(f);
    if (fh) smb2_close(ss.s, fh);
    if (buf) free(buf);
    smb_sess_close(&ss);
    return rc;
}

/* ── 推送 ───────────────────────────────────────────────────────────────── */
int sr_push(const char *tid, const char *uid, const sr_cfg_t *c,
            long long *out_bytes, char *err, size_t errsz) {
    if (out_bytes) *out_bytes = 0;
    if (err && errsz) err[0] = 0;

    char url[SR_URL_MAX];
    if (sr_build_url(c, tid, uid, url, sizeof(url), err, errsz) != 0) return -1;

    char tmp[SR_URL_MAX];
    if (sr_tmp_path("push", tid, uid, tmp, sizeof(tmp), err, errsz) != 0) return -1;

    char pmsg[SR_ERR_MAX];
    pmsg[0] = 0;
    long long pack_files = 0;
    /* [CN-FORK] v0.5.2.162 先记下「这份存档此刻的最晚写入时刻」，再打包。
     * 顺序是有意的：打包之后本地可能又被游戏写了一笔，那会儿的 mtime 已经不在
     * 这个包里 —— 把它写成 src_mtime 会让远端看起来比实际新，下次拉取就用一个
     * **更旧**的包盖掉了本地更新的一份。宁可少报（顶多多拉一次），不可多报。 */
    long long src_mtime = 0;
    if (game_saves_local_mtime(tid, uid, &src_mtime) != 1) src_mtime = 0;
    if (game_saves_pack(tid, uid, tmp, &pack_files, pmsg, sizeof(pmsg)) != 0) {
        if (err) snprintf(err, errsz, "%s", pmsg[0] ? pmsg : "打包失败");
        unlink(tmp);
        return -1;
    }

    /* SMB 不走 curl —— SDK 那份 libcurl 没编 smb，这条线是 libsmb2。
     * 打包这一步是共用的，分岔只发生在「怎么把包送出去」。 */
    if (!strcmp(c->mode, "smb")) {
        long long b = 0;
        int rc = sr_smb_push(c, tid, uid, tmp, &b, pack_files, src_mtime, err, errsz);
        unlink(tmp);
        if (rc == 0 && out_bytes) *out_bytes = b;
        return rc;
    }

    FILE *f = fopen(tmp, "rb");
    if (!f) {
        if (err) snprintf(err, errsz, "打不开刚打好的包（%s）", strerror(errno));
        unlink(tmp);
        return -1;
    }
    long long sz = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        long t = ftell(f);
        sz = t > 0 ? (long long)t : 0;
        fseek(f, 0, SEEK_SET);
    }

    pldmgr_curl_init();
    CURL *cu = curl_easy_init();
    if (!cu) {
        fclose(f);
        unlink(tmp);
        if (err) snprintf(err, errsz, "初始化传输失败");
        return -1;
    }

    struct curl_slist *hdrs = NULL;
    sr_common_opts(cu, url);
    curl_easy_setopt(cu, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(cu, CURLOPT_READFUNCTION, sr_read_file);
    curl_easy_setopt(cu, CURLOPT_READDATA, f);
    curl_easy_setopt(cu, CURLOPT_INFILESIZE_LARGE, (curl_off_t)sz);

    if (!strcmp(c->mode, "ftp")) {
        if (c->user[0]) {
            curl_easy_setopt(cu, CURLOPT_USERNAME, c->user);
            curl_easy_setopt(cu, CURLOPT_PASSWORD, c->password);
        }
        /* 远端是「一个 tid 一个目录」，第一次推送时那个目录必然还不存在 ——
         * 不自己建就永远推不上去，而且 FTP 报的还是 550 这种含糊的错误。 */
        curl_easy_setopt(cu, CURLOPT_FTP_CREATE_MISSING_DIRS, (long)CURLFTP_CREATE_DIR_RETRY);
        curl_easy_setopt(cu, CURLOPT_FTP_USE_EPSV, 1L);
    } else {
        curl_easy_setopt(cu, CURLOPT_CUSTOMREQUEST, "PUT");
        hdrs = curl_slist_append(hdrs, "Content-Type: application/zip");
        /* 关掉 100-continue：自建的小 HTTP 服务常常处理不了它，会白等一秒。 */
        hdrs = curl_slist_append(hdrs, "Expect:");
        if (c->token[0]) {
            char auth[400];
            snprintf(auth, sizeof(auth), "Authorization: Bearer %s", c->token);
            hdrs = curl_slist_append(hdrs, auth);
        }
        curl_easy_setopt(cu, CURLOPT_HTTPHEADER, hdrs);
    }

    CURLcode rc = curl_easy_perform(cu);
    long http = 0;
    curl_easy_getinfo(cu, CURLINFO_RESPONSE_CODE, &http);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(cu);
    fclose(f);
    unlink(tmp);

    if (rc != CURLE_OK) {
        if (err) snprintf(err, errsz, "%s", sr_curl_msg(rc));
        return -1;
    }
    if (http >= 400) {
        if (err) snprintf(err, errsz, "远程拒绝了这次上传（HTTP %ld）", http);
        return -1;
    }
    /* [CN-FORK] v0.5.2.145 推送成功后写一份 .meta.json（与 SMB 分支同款）。
     * meta 与 zip 同目录、同名换后缀；失败不影响推送结果，前端降级显示。
     * [CN-FORK] v0.5.2.162 src_mtime 的理由见 SMB 分支那段（FTP/HTTP 这条路上
     * 它是唯一的时间戳来源 —— 这两个协议的 sr_stat 本来就只读 meta.json）。 */
    char metab[256];
    int mlen = snprintf(metab, sizeof(metab),
                        "{\"files\":%lld,\"size\":%lld,\"mtime\":%lld,\"src_mtime\":%lld}",
                        pack_files, sz, (long long)time(NULL), src_mtime);
    char meta_url[SR_URL_MAX];
    size_t ul = strlen(url);
    if (ul > 4 && strcmp(url + ul - 4, ".zip") == 0)
        snprintf(meta_url, sizeof(meta_url), "%.*s.meta.json", (int)(ul - 4), url);
    else
        snprintf(meta_url, sizeof(meta_url), "%s.meta.json", url);
    if (sr_curl_put_bytes(c, meta_url, metab, (size_t)mlen) != 0)
        pldmgr_log("[PLDMGR] saves: meta put failed for %s\n", meta_url);
    if (out_bytes) *out_bytes = sz;
    return 0;
}

/* 三种协议下载完成后的**同一条**落盘路径：落到主机走的是与「上传存档」
 * 完全一样的实现（整份替换、旧目录先留一手、失败能退回来）。传输层不自己写
 * 一套落盘逻辑 —— 那必然两边慢慢走偏。tmp 由调用方保证存在，这里负责删。 */
static int sr_finish_pull(const char *tid, const char *uid, const char *tmp,
                          long long *out_files, long long *out_bytes,
                          char *err, size_t errsz) {
    char imsg[SR_ERR_MAX];
    imsg[0] = 0;
    long long files = 0, bytes = 0;
    if (game_saves_import_file(tid, uid, tmp, &files, &bytes, imsg, sizeof(imsg)) != 0) {
        unlink(tmp);
        if (err) snprintf(err, errsz, "%s", imsg[0] ? imsg : "导入失败");
        return -1;
    }
    unlink(tmp);
    if (out_files) *out_files = files;
    if (out_bytes) *out_bytes = bytes;
    return 0;
}

/* ── 下载：把远端那份 zip 取到本地一个文件 ───────────────────────────────
 * [CN-FORK] v0.5.2.172 从 sr_pull 里抽出这一段，「拉取」与「下载远程存档」共用。
 * 0 成功（path 已就位）；SR_ENOTFOUND = 远端没有这份；-1 = 失败（err 人话）。
 *
 * 为什么必须抽出来、而不是让下载直接复用 sr_pull：sr_pull 的落点是 sr_finish_pull，
 * 它会把 zip **解包并整份替换主机存档**（游戏启动时那条路的语义）。拿它做下载
 * 等于「点一下下载，本地存档反被云上那份覆盖了」—— 这是这一轮最不能出的错。 */
static int sr_download_to(const sr_cfg_t *c, const char *tid, const char *uid,
                          const char *path, char *err, size_t errsz) {
    char url[SR_URL_MAX];
    if (sr_build_url(c, tid, uid, url, sizeof(url), err, errsz) != 0) return -1;

    /* SMB 不走 curl —— SDK 那份 libcurl 没编 smb，这条线是 libsmb2。 */
    if (!strcmp(c->mode, "smb")) return sr_smb_pull(c, tid, uid, path, err, errsz);

    FILE *f = fopen(path, "wb");
    if (!f) {
        if (err) snprintf(err, errsz, "建不了临时文件（%s）", strerror(errno));
        return -1;
    }

    pldmgr_curl_init();
    CURL *cu = curl_easy_init();
    if (!cu) {
        fclose(f);
        unlink(path);
        if (err) snprintf(err, errsz, "初始化传输失败");
        return -1;
    }

    struct curl_slist *hdrs = NULL;
    sr_common_opts(cu, url);
    curl_easy_setopt(cu, CURLOPT_WRITEFUNCTION, sr_write_file);
    curl_easy_setopt(cu, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(cu, CURLOPT_FOLLOWLOCATION, 1L);
    if (!strcmp(c->mode, "ftp")) {
        if (c->user[0]) {
            curl_easy_setopt(cu, CURLOPT_USERNAME, c->user);
            curl_easy_setopt(cu, CURLOPT_PASSWORD, c->password);
        }
        curl_easy_setopt(cu, CURLOPT_FTP_USE_EPSV, 1L);
    } else if (c->token[0]) {
        char auth[400];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", c->token);
        hdrs = curl_slist_append(hdrs, auth);
        curl_easy_setopt(cu, CURLOPT_HTTPHEADER, hdrs);
    }

    CURLcode rc = curl_easy_perform(cu);
    long http = 0;
    curl_easy_getinfo(cu, CURLINFO_RESPONSE_CODE, &http);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(cu);
    fclose(f);

    if (rc != CURLE_OK) {
        unlink(path);
        /* FTP 这边「远端还没有这个文件」有两种表现，必须都算「没得拉」：
         *   - RETR 一个不存在的文件 → CURLE_REMOTE_FILE_NOT_FOUND
         *   - **CWD 到一个还不存在的目录 → 550 → CURLE_REMOTE_ACCESS_DENIED**
         *     （libcurl 对 CWD 失败就是报这个码）
         * 第二条正是「第一次用自动存档」的必然形态：远端 <TID>/ 目录要等第一次
         * 推送才存在。不把它归进来的话，用户第一次开游戏就会看到一条红色的
         * 「权限不足」，而实际上什么都没配错。
         * 代价是「FTP 上真的没有权限」会被说成「还没有存档」—— 但这两件事在
         * 原始 FTP 回复里都是 550，服务器自己也不区分，措辞里如实带上即可。 */
        if (rc == CURLE_REMOTE_FILE_NOT_FOUND) {
            if (err) snprintf(err, errsz, "远程还没有这个游戏的存档");
            return SR_ENOTFOUND;
        }
        if (rc == CURLE_REMOTE_ACCESS_DENIED) {
            if (err) snprintf(err, errsz, "远程还没有这个游戏的存档（或没有访问权限）");
            return SR_ENOTFOUND;
        }
        if (err) snprintf(err, errsz, "%s", sr_curl_msg(rc));
        return -1;
    }
    if (http == 404) {
        unlink(path);
        if (err) snprintf(err, errsz, "远程还没有这个游戏的存档");
        return SR_ENOTFOUND;
    }
    if (http >= 400) {
        unlink(path);
        if (err) snprintf(err, errsz, "远程拒绝了这次下载（HTTP %ld）", http);
        return -1;
    }

    return 0;
}

/* ── 拉取（把远端那份**整份替换**进主机存档）────────────────────────────── */
int sr_pull(const char *tid, const char *uid, const sr_cfg_t *c,
            long long *out_files, long long *out_bytes, char *err, size_t errsz) {
    if (out_files) *out_files = 0;
    if (out_bytes) *out_bytes = 0;
    if (err && errsz) err[0] = 0;

    char tmp[SR_URL_MAX];
    if (sr_tmp_path("pull", tid, uid, tmp, sizeof(tmp), err, errsz) != 0) return -1;

    int rc = sr_download_to(c, tid, uid, tmp, err, errsz);
    if (rc != 0) {
        unlink(tmp);
        return rc; /* 可能是 SR_ENOTFOUND，原样往上抛 */
    }
    return sr_finish_pull(tid, uid, tmp, out_files, out_bytes, err, errsz);
}

/* ── 下载远程存档（原样取回，一个字节都不碰主机存档）──────────────────────
 * [CN-FORK] v0.5.2.172 前端「远程存档」那一行的下载按钮用。
 * 0 成功（out_path 已就位）；SR_ENOTFOUND = 远端没有这份；-1 = 失败（err 人话）。
 * *out_bytes 回实际落盘的字节数（可为 NULL）。 */
int sr_fetch(const char *tid, const char *uid, const sr_cfg_t *c,
             const char *out_path, long long *out_bytes, char *err, size_t errsz) {
    if (out_bytes) *out_bytes = 0;
    if (err && errsz) err[0] = 0;
    if (!out_path || !out_path[0]) {
        if (err) snprintf(err, errsz, "缺少输出路径");
        return -1;
    }
    int rc = sr_download_to(c, tid, uid, out_path, err, errsz);
    if (rc == 0 && out_bytes) {
        struct stat sb;
        if (stat(out_path, &sb) == 0) *out_bytes = (long long)sb.st_size;
    }
    return rc;
}

/* ── 只读元数据（方案 A：前端「远程游戏」标签展示用）────────────────────── */
/* [CN-FORK] v0.5.2.145 只读取一份远程存档的真实元数据，**不上传也不下载**存档本身。
 * 供前端「远程游戏」标签展示与本地存档列表同款的信息（大小 / 修改时间 / 文件数）。
 *   out_size  ← <uid>.zip 的字节数（smb2_stat 直接拿，真实）
 *   out_mtime ← <uid>.zip 的 mtime（真实打包时间）
 *   out_files ← 推送时一并写的 <uid>.meta.json 里的 files；老存档没有 meta 时回 -1
 * [CN-FORK] v0.5.2.162 out_src_mtime ← meta.json 里的 src_mtime（这份包是从主机上
 *   哪个时刻的存档打出来的）。**没有 meta 或没有这个字段一律回 0**，调用方据此
 *   退回「说不清 → 照旧拉」，绝不因为缺字段把拉取关掉。理由见 sr_stat 的声明。
 * 0 成功；SR_ENOTFOUND = 远端还没有这份（语义同 sr_pull，前端当「没得显示」）；
 * -1 = 配置 / 连接错误（err 人话）。
 * [CN-FORK] v0.5.2.154 起 FTP / HTTP 也实现了（读 <uid>.meta.json，见下方分支）。
 * ⚠ 只有 SMB 会回 SR_ENOTFOUND；FTP / HTTP **一律回 -1**，理由见那条分支里
 *   的长注释（「meta.json 不在」分不清是「没存档」还是「老存档没写 meta」）。 */
int sr_stat(const sr_cfg_t *c, const char *tid, const char *uid,
            long long *out_size, time_t *out_mtime, long long *out_files,
            long long *out_src_mtime, char *err, size_t errsz) {
    if (out_size) *out_size = 0;
    if (out_mtime) *out_mtime = 0;
    if (out_files) *out_files = -1;
    if (out_src_mtime) *out_src_mtime = 0;
    if (err && errsz) err[0] = 0;
    if (!c) {
        if (err) snprintf(err, errsz, "没有配置远程位置");
        return -1;
    }
    if (!sr_seg_ok(tid) || !sr_seg_ok(uid)) {
        if (err) snprintf(err, errsz, "标题 ID 或用户标识不合法");
        return -1;
    }

    if (!strcmp(c->mode, "smb")) {
        smb_sess_t ss;
        if (smb_sess_open(c, tid, &ss, err, errsz) != 0) return -1;
        char nm[200], rp[SR_SMB_PATH_MAX], mnm[240], metap[SR_SMB_PATH_MAX];
        snprintf(nm, sizeof(nm), "%s.zip", uid);
        smb_join(rp, sizeof(rp), ss.dir, nm);
        struct smb2_stat_64 st;
        memset(&st, 0, sizeof(st));
        int r = smb2_stat(ss.s, rp, &st);
        if (r < 0) {
            if (r == -ENOENT) {
                if (err) snprintf(err, errsz, "远程还没有这个游戏的存档");
                smb_sess_close(&ss);
                return SR_ENOTFOUND;
            }
            if (err) snprintf(err, errsz, "查不到远程存档：%s", smb_human(ss.s, r));
            smb_sess_close(&ss);
            return -1;
        }
        if (out_size) *out_size = (long long)st.smb2_size;
        if (out_mtime) *out_mtime = (time_t)st.smb2_mtime;
        /* 读 <uid>.meta.json 拿 files 与 src_mtime；没有 meta（老存档）时保持
         * files=-1 / src_mtime=0，前端降级显示、拉取判定退回 `.154` 口径。
         * 256 字节足够装下我们写的那条 {"files":N,"size":N,"mtime":N,"src_mtime":N}。 */
        snprintf(mnm, sizeof(mnm), "%s.meta.json", uid);
        smb_join(metap, sizeof(metap), ss.dir, mnm);
        struct smb2fh *mfh = smb2_open(ss.s, metap, O_RDONLY);
        if (mfh) {
            uint8_t *mbuf = (uint8_t *)malloc(256);
            if (mbuf) {
                int got = smb2_read(ss.s, mfh, mbuf, 255);
                if (got > 0) {
                    mbuf[got] = 0;
                    cJSON *root = cJSON_Parse((const char *)mbuf);
                    if (root) {
                        cJSON *fj = cJSON_GetObjectItem(root, "files");
                        if (cJSON_IsNumber(fj) && out_files)
                            *out_files = (long long)fj->valuedouble;
                        /* [CN-FORK] v0.5.2.162 `.162` 之前推上去的包没有这个字段，
                         * 读不到就留在 0 —— 判定侧把 0 当「说不清」，不是当「一样」。 */
                        cJSON *xj = cJSON_GetObjectItem(root, "src_mtime");
                        if (cJSON_IsNumber(xj) && out_src_mtime)
                            *out_src_mtime = (long long)xj->valuedouble;
                        cJSON_Delete(root);
                    }
                }
                free(mbuf);
            }
            smb2_close(ss.s, mfh);
        }
        smb_sess_close(&ss);
        return 0;
    }

    /* FTP / HTTP：[CN-FORK] v0.5.2.154 用推送时一并写下的 <uid>.meta.json 拿时间戳。
     *
     * 为什么不实现 FTP `MDTM` / HTTP `HEAD`：那两条各协议语义不一 —— 有的 FTP
     * 服务器不认 MDTM，自建的小 HTTP 服务未必实现 HEAD，真机上要一个个踩才知道
     * 哪条能用；而 meta.json 是**我们自己写的**文件，三个协议上都是同一次普通
     * 读取（RETR / GET），复用已经跑熟的那条路。
     *
     * ⚠ **一律「说不清就回 -1」，绝不在这里回 SR_ENOTFOUND。**
     * 调用方是「拉取前的新旧判定」，它把 -1 当「说不清 → 照旧拉」、把
     * SR_ENOTFOUND 当「远端还没有存档 → 跳过拉取」。而 meta.json 只有 .145 起
     * 推送才会写，老存档必然没有 —— 若在这里回 SR_ENOTFOUND，那些用户的自动
     * 拉取会**整个失效**（明明有存档却再也不拉）。宁可少判一次，不能把功能关掉。
     *
     * 读到了 meta 却解不开、或里面没有 mtime → 同样回 -1（说不清）。 */
    {
        char zurl[SR_URL_MAX], murl[SR_URL_MAX];
        if (sr_build_url(c, tid, uid, zurl, sizeof(zurl), err, errsz) != 0) return -1;
        size_t ul = strlen(zurl);
        if (ul > 4 && strcmp(zurl + ul - 4, ".zip") == 0)
            snprintf(murl, sizeof(murl), "%.*s.meta.json", (int)(ul - 4), zurl);
        else
            snprintf(murl, sizeof(murl), "%s.meta.json", zurl);

        char body[512];
        size_t blen = 0;
        if (sr_curl_get_mem(c, murl, body, sizeof(body), &blen, err, errsz) != 0) return -1;
        if (blen == 0) return -1;
        cJSON *root = cJSON_Parse(body);
        if (!root) return -1;
        cJSON *mj = cJSON_GetObjectItem(root, "mtime");
        if (!cJSON_IsNumber(mj)) {
            cJSON_Delete(root);
            return -1;
        }
        if (out_mtime) *out_mtime = (time_t)mj->valuedouble;
        cJSON *sj = cJSON_GetObjectItem(root, "size");
        if (cJSON_IsNumber(sj) && out_size) *out_size = (long long)sj->valuedouble;
        cJSON *fj = cJSON_GetObjectItem(root, "files");
        if (cJSON_IsNumber(fj) && out_files) *out_files = (long long)fj->valuedouble;
        /* [CN-FORK] v0.5.2.162 拉取判定真正用的那一个。缺字段（`.162` 之前推的包）
         * 就留在 0：调用方把 0 当「说不清 → 照旧拉」，与 SMB 分支同一口径。 */
        cJSON *xj = cJSON_GetObjectItem(root, "src_mtime");
        if (cJSON_IsNumber(xj) && out_src_mtime) *out_src_mtime = (long long)xj->valuedouble;
        cJSON_Delete(root);
        return 0;
    }
}

/* ── 删掉远端那一份（v0.5.2.159）──────────────────────────────────────────── */
/* [CN-FORK] v0.5.2.159 只删 `<TID>/<uid>.zip` 与它的 `.meta.json`，
 * **不碰主机上的本地存档、也不碰本地快照** —— 语义与前端确认框里那句话一致。
 * 为什么连 meta 一起删：留着它，`sr_stat` 在 FTP/HTTP 那条读法下会指着一条
 * 已经不存在的存档报出大小与时间（`.145` 的展示就会说谎）；SMB 这边虽然读的是
 * zip 本身，但一个没有主人的 meta 留在共享里就是垃圾。
 * ⚠ meta 删不掉**不算失败** —— `.145` 之前的老存档根本没写过 meta。 */
int sr_delete(const sr_cfg_t *c, const char *tid, const char *uid,
              char *err, size_t errsz) {
    if (err && errsz) err[0] = 0;
    if (!c) {
        if (err) snprintf(err, errsz, "没有配置远程位置");
        return -1;
    }
    if (!sr_seg_ok(tid) || !sr_seg_ok(uid)) {
        if (err) snprintf(err, errsz, "标题 ID 或用户标识不合法");
        return -1;
    }

    if (!strcmp(c->mode, "smb")) {
        smb_sess_t ss;
        if (smb_sess_open(c, tid, &ss, err, errsz) != 0) return -1;
        char nm[200], rp[SR_SMB_PATH_MAX];
        snprintf(nm, sizeof(nm), "%s.zip", uid);
        smb_join(rp, sizeof(rp), ss.dir, nm);
        int r = smb2_unlink(ss.s, rp);
        if (r < 0) {
            if (r == -ENOENT) {
                /* 远端已经没有这一份了 —— 当作目标状态达成，不是失败。 */
                smb_sess_close(&ss);
                return SR_ENOTFOUND;
            }
            if (err) snprintf(err, errsz, "删不掉远程存档：%s", smb_human(ss.s, r));
            smb_sess_close(&ss);
            return -1;
        }
        char mnm[240], metap[SR_SMB_PATH_MAX];
        snprintf(mnm, sizeof(mnm), "%s.meta.json", uid);
        smb_join(metap, sizeof(metap), ss.dir, mnm);
        smb2_unlink(ss.s, metap);   /* 见上面那条 ⚠：失败也继续 */
        smb_sess_close(&ss);
        return 0;
    }

    /* FTP / HTTP：见 save_remote.h 里那段「为什么只有 SMB」。
     * 这句里带上协议名，用户才知道要去换 SMB 还是等这个协议被支持。
     * ⚠ 未知 / 空 mode 要单独回，不能顺着 else 落到「HTTP」上 ——
     *   那等于对着一个没填协议的配置谎报「HTTP 不支持」，排查的人会往 HTTP 找。 */
    if (!strcmp(c->mode, "ftp") || !strcmp(c->mode, "http")) {
        if (err) snprintf(err, errsz, "%s 远程位置还不支持在这里删除（目前只有 SMB）",
                          !strcmp(c->mode, "ftp") ? "FTP" : "HTTP");
        return -1;
    }
    if (err) snprintf(err, errsz, "不支持的传输方式（%s）", c->mode);
    return -1;
}

/* ══ [CN-FORK] v0.6.0.177 一批本地文件 → <base>/<TID>/media/<rel> ═══════════
 * 多媒体远程同步用（设置页「多媒体存档配置」的自动备份 + 多媒体管理页的手动推送）。
 *
 * 为什么不复用 sr_push：那条永远是「一个 tid 一个 zip 整份覆盖」，而媒体是
 * **增量传单个文件** —— 一次同步常常只是新增的一两张截图，为它们重新打包整批
 * 录屏（动辄几个 GB）在真机上等不起，也白烧一遍流量。
 *
 * 远端布局：<base>/<TID>/media/<rel>，rel = content_path 去掉 "/av_contents/" 前缀，
 * 即 photo/<生成器>/<TID>/<hex>/<文件>。**保留那层目录**（不压平成一层）：
 * 压平之后主机上的原路径在远端就没了，回不去；同名冲突也只能靠运气。
 */

/* rel 的白名单。rel 虽然来自 av_content.db（系统只读库），但「库可信」不是
 * 免检理由 —— md_path_ok 在读库那侧挡过一道，这里是**远端路径**的最后一道，
 * 漏了就是往 <TID>/media/ 之外写文件（zip/tar 解包那条路同一个教训）。 */
static int sr_media_rel_ok(const char *rel) {
    if (!rel || !*rel || strlen(rel) > 512) return 0;
    if (rel[0] == '/' || rel[0] == '\\') return 0;
    if (strstr(rel, "..") || strchr(rel, '\\')) return 0;
    int slashes = 0;
    for (const char *p = rel; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20 || ch == 0x7f) return 0;
        if (*p == '/') slashes++;
    }
    /* 真实布局 photo/<生成器>/<TID>/<hex>/<文件> = 4 段；上下都留出固件改布局的余量 */
    if (slashes < 1 || slashes > 8) return 0;
    /* 文件名单独限长：下面逐个上传时文件名是 snprintf 进 name[300] 的，
     * 悄悄截断会写出一个「跟账本上的名字对不上」的远端文件 —— 宁可在这里拒掉。 */
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    size_t bn = strlen(base);
    return bn > 0 && bn <= 200;
}

/* rel → 目录段（去掉最后一段文件名）。sep 让同一个函数同时供
 * FTP/HTTP（'/'）与 SMB（'\\'）用 —— 两处各写一遍必然漂开。 */
static int sr_media_dir_of(const char *rel, char *out, size_t osz, char sep) {
    const char *sl = strrchr(rel, '/');
    if (!sl || sl == rel) { out[0] = 0; return 0; }
    size_t n = (size_t)(sl - rel);
    if (n + 1 > osz) return -1;
    memcpy(out, rel, n);
    out[n] = 0;
    if (sep == '\\')
        for (size_t i = 0; i < n; i++) if (out[i] == '/') out[i] = '\\';
    return 0;
}

/* FTP / HTTP 的公共前缀：<base>/<TID>/media。只拼一次，逐个文件再补 /<目录>/<文件名>。 */
static int sr_media_prefix(const sr_cfg_t *c, const char *tid,
                           char *out, size_t osz, char *err, size_t errsz) {
    out[0] = 0;
    if (!c || !c->mode[0]) {
        if (err) snprintf(err, errsz, "没有配置多媒体的远程位置");
        return -1;
    }
    if (!sr_seg_ok(tid)) {
        if (err) snprintf(err, errsz, "标题 ID 不合法");
        return -1;
    }
    if (!strcmp(c->mode, "ftp")) {
        if (!c->host[0]) {
            if (err) snprintf(err, errsz, "没有填 FTP 主机地址");
            return -1;
        }
        int port = (c->port > 0 && c->port < 65536) ? c->port : 21;
        char dir[700];
        dir[0] = 0;
        if (c->path[0]) {
            const char *p = c->path;
            while (*p == '/') p++;
            snprintf(dir, sizeof(dir), "/%s", p);
            sr_trim_slash(dir);
        }
        int n = snprintf(out, osz, "ftp://%s:%d%s/%s/media", c->host, port, dir, tid);
        if (n <= 0 || (size_t)n >= osz) {
            out[0] = 0;
            if (err) snprintf(err, errsz, "地址过长");
            return -1;
        }
        return 0;
    }
    if (!strcmp(c->mode, "http")) {
        if (!c->url[0]) {
            if (err) snprintf(err, errsz, "没有填 HTTP 地址");
            return -1;
        }
        if (strncmp(c->url, "http://", 7) != 0 && strncmp(c->url, "https://", 8) != 0) {
            if (err) snprintf(err, errsz, "HTTP 地址要以 http:// 或 https:// 开头");
            return -1;
        }
        char base[700];
        snprintf(base, sizeof(base), "%s", c->url);
        sr_trim_slash(base);
        int n = snprintf(out, osz, "%s/%s/media", base, tid);
        if (n <= 0 || (size_t)n >= osz) {
            out[0] = 0;
            if (err) snprintf(err, errsz, "地址过长");
            return -1;
        }
        return 0;
    }
    if (err) snprintf(err, errsz, "不支持的传输方式（%s）", c->mode);
    return -1;
}

/* 单个文件 → url（FTP STOR / HTTP PUT）。
 * 与 sr_push 的 curl 分支同构，只差两处，两处都是**必须**的：
 *   ① 不设 CURLOPT_TIMEOUT —— 录屏几百 MB，180 秒的整段上限会把正常传输掐死；
 *      卡住交给低速熔断（60 秒收不到 1 字节）认，那才是「还在不在动」的判据。
 *   ② 每次新开一个 easy handle —— 一次同步几十上百个文件，handle 复用省的那点
 *      开销远小于「上一个文件的 FTP 会话状态没清干净」这类疑难杂症。 */
static int sr_curl_put_local(const sr_cfg_t *c, const char *url, const char *local,
                             long long *out_bytes, char *err, size_t errsz) {
    struct stat st;
    if (stat(local, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (err) snprintf(err, errsz, "本机文件读不到（%s）", strerror(errno));
        return -1;
    }
    FILE *f = fopen(local, "rb");
    if (!f) {
        if (err) snprintf(err, errsz, "本机文件打不开（%s）", strerror(errno));
        return -1;
    }
    pldmgr_curl_init();
    CURL *cu = curl_easy_init();
    if (!cu) {
        fclose(f);
        if (err) snprintf(err, errsz, "初始化传输失败");
        return -1;
    }
    struct curl_slist *hdrs = NULL;
    curl_easy_setopt(cu, CURLOPT_URL, url);
    curl_easy_setopt(cu, CURLOPT_CONNECTTIMEOUT, SR_CONNECT_TIMEOUT);
    curl_easy_setopt(cu, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(cu, CURLOPT_LOW_SPEED_LIMIT, SR_LOW_SPEED_BYTES);
    curl_easy_setopt(cu, CURLOPT_LOW_SPEED_TIME, SR_LOW_SPEED_SEC);
    curl_easy_setopt(cu, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(cu, CURLOPT_READFUNCTION, sr_read_file);
    curl_easy_setopt(cu, CURLOPT_READDATA, f);
    curl_easy_setopt(cu, CURLOPT_INFILESIZE_LARGE, (curl_off_t)st.st_size);

    if (!strcmp(c->mode, "ftp")) {
        if (c->user[0]) {
            curl_easy_setopt(cu, CURLOPT_USERNAME, c->user);
            curl_easy_setopt(cu, CURLOPT_PASSWORD, c->password);
        }
        /* 远端的 <TID>/media/<…> 第一次推时整条链都不存在 —— 与 sr_push 同理，
         * 不自建就永远推不上，而 FTP 报的还是 550 这种含糊的错。 */
        curl_easy_setopt(cu, CURLOPT_FTP_CREATE_MISSING_DIRS, (long)CURLFTP_CREATE_DIR_RETRY);
        curl_easy_setopt(cu, CURLOPT_FTP_USE_EPSV, 1L);
    } else {
        curl_easy_setopt(cu, CURLOPT_CUSTOMREQUEST, "PUT");
        hdrs = curl_slist_append(hdrs, "Content-Type: application/octet-stream");
        hdrs = curl_slist_append(hdrs, "Expect:"); /* 见 sr_push：自建 HTTP 服务常被 100-continue 卡一秒 */
        if (c->token[0]) {
            char auth[400];
            snprintf(auth, sizeof(auth), "Authorization: Bearer %s", c->token);
            hdrs = curl_slist_append(hdrs, auth);
        }
        curl_easy_setopt(cu, CURLOPT_HTTPHEADER, hdrs);
    }

    CURLcode rc = curl_easy_perform(cu);
    long http = 0;
    curl_easy_getinfo(cu, CURLINFO_RESPONSE_CODE, &http);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(cu);
    fclose(f);

    if (rc != CURLE_OK) {
        if (err) snprintf(err, errsz, "%s", sr_curl_msg(rc));
        return -1;
    }
    if (http >= 400) {
        if (err) snprintf(err, errsz, "远程拒绝了这次上传（HTTP %ld）", http);
        return -1;
    }
    if (out_bytes) *out_bytes = (long long)st.st_size;
    return 0;
}

/* 单个文件 → SMB。dir 是 <TID> 之下的相对目录（已带 "media" 那一段，反斜杠分隔）。
 * 与 sr_smb_push 同一口径：先写 .tmp 再改名 —— 传到一半断线时远端会留半截文件，
 * 谁去拉拿到的都是坏的；改名是原子的，远端那份要么完整要么还是旧的。 */
static int sr_smb_put_local(smb_sess_t *ss, const char *dir, const char *name,
                            const char *local, long long *out_bytes, char *err, size_t errsz) {
    char full[SR_SMB_PATH_MAX], tmpp[SR_SMB_PATH_MAX];
    char nbuf[300], tbuf[320];
    struct smb2fh *fh = NULL;
    uint8_t *buf = NULL;
    FILE *f = NULL;
    long long total = 0;
    int rc = -1;

    if (smb_mkdirs(ss->s, dir, err, errsz) != 0) return -1;

    snprintf(nbuf, sizeof(nbuf), "%s", name);
    snprintf(tbuf, sizeof(tbuf), "%s.tmp", name);
    smb_join(full, sizeof(full), dir, nbuf);
    smb_join(tmpp, sizeof(tmpp), dir, tbuf);

    smb2_unlink(ss->s, tmpp); /* 上一轮崩溃留下的残件，清不掉也无所谓 */
    fh = smb2_open(ss->s, tmpp, O_CREAT | O_WRONLY);
    if (!fh) {
        if (err) snprintf(err, errsz, "在远程建不了临时文件：%s", smb_human(ss->s, 0));
        goto out;
    }

    uint32_t chunk = smb2_get_max_write_size(ss->s);
    if (chunk < SR_SMB_CHUNK_MIN) chunk = SR_SMB_CHUNK_MIN;
    if (chunk > SR_SMB_CHUNK_MAX) chunk = SR_SMB_CHUNK_MAX;
    buf = (uint8_t *)malloc(chunk); /* 堆上分配：MHD 连接线程只有 64KB 栈 */
    if (!buf) {
        if (err) snprintf(err, errsz, "内存不足");
        goto out;
    }
    f = fopen(local, "rb");
    if (!f) {
        if (err) snprintf(err, errsz, "本机文件打不开（%s）", strerror(errno));
        goto out;
    }
    for (;;) {
        size_t got = fread(buf, 1, chunk, f);
        if (got == 0) break;
        int w = smb2_write(ss->s, fh, buf, (uint32_t)got);
        if (w < 0) {
            if (err) snprintf(err, errsz, "写远程失败：%s", smb_human(ss->s, w));
            goto out;
        }
        if ((size_t)w != got) {
            if (err) snprintf(err, errsz, "写远程的字节数对不上（要 %u，实际 %d）",
                              (unsigned)got, w);
            goto out;
        }
        total += w;
    }
    fclose(f);
    f = NULL;
    smb2_fsync(ss->s, fh);
    if (smb2_close(ss->s, fh) != 0) {
        fh = NULL;
        if (err) snprintf(err, errsz, "关闭远程文件失败：%s", smb_human(ss->s, 0));
        goto out;
    }
    fh = NULL;

    int r = smb2_rename(ss->s, tmpp, full);
    if (r == -EEXIST) { /* libsmb2 的 rename 不带 replace，目标已存在会直接失败 */
        smb2_unlink(ss->s, full);
        r = smb2_rename(ss->s, tmpp, full);
    }
    if (r != 0) {
        smb2_unlink(ss->s, tmpp);
        if (err) snprintf(err, errsz, "替换远程文件失败：%s", smb_human(ss->s, r));
        goto out;
    }
    rc = 0;

out:
    if (f) fclose(f);
    if (fh) smb2_close(ss->s, fh);
    if (buf) free(buf);
    if (rc == 0 && out_bytes) *out_bytes = total;
    return rc;
}

/* 把一批媒体文件推到远端。返回：
 *   0  = 全部成功；
 *  -1  = 有文件失败（err = 第一个失败原因；**其余文件照传**，能多救一条是一条）；
 *   1  = 被 step() 中止（取消）。
 * step() 每传完一个文件调用一次（i = 下标，ok = 这个文件成没成），返回非 0 即停。
 * out_done 回成功条数。调用方在 step 里同时做「记账 + 进度 + 取消」三件事 ——
 * 分成三次回调就会出现「进度走完但账没记」这种下次要重传的错位。 */
int sr_push_media(const sr_cfg_t *c, const char *tid,
                  const sr_mfile_t *files, int n,
                  long long *out_bytes, int *out_done,
                  int (*step)(void *ud, int i, int ok), void *ud,
                  char *err, size_t errsz) {
    if (out_bytes) *out_bytes = 0;
    if (out_done) *out_done = 0;
    if (err && errsz) err[0] = 0;
    if (!c || !files || n <= 0) {
        if (err) snprintf(err, errsz, "没有要同步的文件");
        return -1;
    }
    if (!sr_mode_supported(c->mode)) {
        if (err) snprintf(err, errsz, "不支持的传输方式（%s）", c->mode);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (!sr_media_rel_ok(files[i].rel) || !files[i].local || !files[i].local[0]) {
            if (err) snprintf(err, errsz, "第 %d 个文件的路径不合法", i + 1);
            return -1;
        }
    }

    int ok = 0, failed = 0, aborted = 0;
    long long total = 0;
    char first_err[SR_ERR_MAX];
    first_err[0] = 0;

    if (!strcmp(c->mode, "smb")) {
        /* 一个 session 传整批 —— 这与 sr_push「一次一连」的取舍**相反**，
         * 原因是量：存档一次一个包，媒体一次几十上百个文件，
         * 每个文件都重做一遍协商会让同步慢到像卡死。 */
        smb_sess_t ss;
        if (smb_sess_open(c, tid, &ss, err, errsz) != 0) return -1;
        for (int i = 0; i < n && !aborted; i++) {
            char dir[SR_SMB_PATH_MAX], sub[600], mdir[SR_SMB_PATH_MAX], name[300];
            int f_ok = 0;
            if (sr_media_dir_of(files[i].rel, sub, sizeof(sub), '\\') != 0) {
                if (!first_err[0]) snprintf(first_err, sizeof(first_err), "第 %d 个文件的路径过长", i + 1);
                failed++;
            } else {
                const char *sl = strrchr(files[i].rel, '/');
                snprintf(name, sizeof(name), "%s", sl ? sl + 1 : files[i].rel);
                /* dir = <sub>\<tid> \ media [\ <子目录>]。sub 为空时只有一层 media。 */
                smb_join(dir, sizeof(dir), ss.dir, "media");
                if (sub[0]) {
                    smb_join(mdir, sizeof(mdir), dir, sub);
                    snprintf(dir, sizeof(dir), "%s", mdir);
                }
                long long b = 0;
                char e[SR_ERR_MAX];
                e[0] = 0;
                if (sr_smb_put_local(&ss, dir, name, files[i].local, &b, e, sizeof(e)) == 0) {
                    ok++;
                    total += b;
                    f_ok = 1;
                } else {
                    failed++;
                    if (!first_err[0]) snprintf(first_err, sizeof(first_err), "%s", e);
                }
            }
            if (out_done) *out_done = ok;
            if (out_bytes) *out_bytes = total;
            /* 这条失败也要回调：否则调用方的进度会在这里卡一个格子不走，
             * 看着像卡死（步进的账本/进度以「传过这个下标」为准，不管成没成）。 */
            if (step && step(ud, i, f_ok)) aborted = 1;
        }
        smb_sess_close(&ss);
    } else {
        char prefix[SR_URL_MAX];
        if (sr_media_prefix(c, tid, prefix, sizeof(prefix), err, errsz) != 0) return -1;
        for (int i = 0; i < n && !aborted; i++) {
            char sub[600], url[SR_URL_MAX], name[300];
            int f_ok = 0;
            if (sr_media_dir_of(files[i].rel, sub, sizeof(sub), '/') != 0) {
                if (!first_err[0]) snprintf(first_err, sizeof(first_err), "第 %d 个文件的路径过长", i + 1);
                failed++;
            } else {
                const char *sl = strrchr(files[i].rel, '/');
                snprintf(name, sizeof(name), "%s", sl ? sl + 1 : files[i].rel);
                int un = snprintf(url, sizeof(url), "%s%s%s/%s", prefix,
                                  sub[0] ? "/" : "", sub, name);
                if (un <= 0 || (size_t)un >= sizeof(url)) {
                    if (!first_err[0]) snprintf(first_err, sizeof(first_err), "第 %d 个文件的地址过长", i + 1);
                    failed++;
                } else {
                    long long b = 0;
                    char e[SR_ERR_MAX];
                    e[0] = 0;
                    if (sr_curl_put_local(c, url, files[i].local, &b, e, sizeof(e)) == 0) {
                        ok++;
                        total += b;
                        f_ok = 1;
                    } else {
                        failed++;
                        if (!first_err[0]) snprintf(first_err, sizeof(first_err), "%s", e);
                    }
                }
            }
            if (out_done) *out_done = ok;
            if (out_bytes) *out_bytes = total;
            if (step && step(ud, i, f_ok)) aborted = 1;
        }
    }

    if (out_done) *out_done = ok;
    if (out_bytes) *out_bytes = total;
    if (aborted) {
        if (err) snprintf(err, errsz, "已取消");
        return 1;
    }
    if (failed) {
        if (err) snprintf(err, errsz, "%s（%d/%d 个文件失败）",
                          first_err[0] ? first_err : "上传失败", failed, n);
        return -1;
    }
    return 0;
}
