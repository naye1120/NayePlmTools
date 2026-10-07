/* [CN-FORK] v0.5.2.142 存档的远程传输层。设计（为什么只有 FTP/HTTP、远端布局、
 * 为什么文件名不带时间戳）见 include/save_remote.h 的文件头。 */

#include "save_remote.h"

#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "game_saves.h"
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

/* ── 协议支持 ───────────────────────────────────────────────────────────── */
int sr_mode_supported(const char *mode) {
    return (mode && (!strcmp(mode, "ftp") || !strcmp(mode, "http"))) ? 1 : 0;
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

    if (!strcmp(c->mode, "smb"))
        snprintf(err, errsz, "当前 SDK 不支持 SMB 传输，请改用 FTP 或 HTTP");
    else if (err)
        snprintf(err, errsz, "不支持的传输方式（%s）", c->mode);
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
    if (game_saves_pack(tid, uid, tmp, pmsg, sizeof(pmsg)) != 0) {
        if (err) snprintf(err, errsz, "%s", pmsg[0] ? pmsg : "打包失败");
        unlink(tmp);
        return -1;
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
    if (out_bytes) *out_bytes = sz;
    return 0;
}

/* ── 拉取 ───────────────────────────────────────────────────────────────── */
int sr_pull(const char *tid, const char *uid, const sr_cfg_t *c,
            long long *out_files, long long *out_bytes, char *err, size_t errsz) {
    if (out_files) *out_files = 0;
    if (out_bytes) *out_bytes = 0;
    if (err && errsz) err[0] = 0;

    char url[SR_URL_MAX];
    if (sr_build_url(c, tid, uid, url, sizeof(url), err, errsz) != 0) return -1;

    char tmp[SR_URL_MAX];
    if (sr_tmp_path("pull", tid, uid, tmp, sizeof(tmp), err, errsz) != 0) return -1;

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        if (err) snprintf(err, errsz, "建不了临时文件（%s）", strerror(errno));
        return -1;
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
        unlink(tmp);
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
        unlink(tmp);
        if (err) snprintf(err, errsz, "远程还没有这个游戏的存档");
        return SR_ENOTFOUND;
    }
    if (http >= 400) {
        unlink(tmp);
        if (err) snprintf(err, errsz, "远程拒绝了这次下载（HTTP %ld）", http);
        return -1;
    }

    /* 落到主机走的是与「上传存档」**同一条**实现（整份替换、旧目录先留一手、
     * 失败能退回来）。传输层不自己写一套落盘逻辑 —— 那必然两边慢慢走偏。 */
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
