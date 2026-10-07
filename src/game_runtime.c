/* [CN-FORK] v0.5.2.116 游戏管理 → 运行时层（删源 / 挂载 / 卸载挂载 / 卸载安装）
 * 契约与三个端点的语义见 include/game_runtime.h。 */

#include "game_runtime.h"

#include <ctype.h>
#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp：镜像扩展名大小写不敏感 */
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "game_job.h"
#include "game_mgr.h"
#include "pldmgr.h"
#include "utils.h" /* pldmgr_json_escape */

/* ── 响应拼装 ─────────────────────────────────────────────────────────────
 * 三个端点都得回**完整的** JSON，而且失败分支也要带 message（前端直接把它
 * 弹给用户）。集中在这里，免得每个分支各写一遍 snprintf 漏掉一处转义。 */
static int gr_fmt(char *buf, size_t size, int *status, int http, const char *fmt, ...) {
    va_list ap;
    if (!buf || !status || size < 8) return -1;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= size) return -1;
    *status = http;
    return 0;
}

static int gr_err(char *buf, size_t size, int *status, int http,
                  const char *code, const char *msg) {
    char e[700];
    pldmgr_json_escape(msg ? msg : "", e, sizeof(e));
    return gr_fmt(buf, size, status, http,
                  "{\"error\":\"%s\",\"message\":\"%s\"}", code, e);
}

/* 409 多一个 reason 机器码（game_active / busy / …），前端拿它分流文案。 */
static int gr_busy(char *buf, size_t size, int *status,
                   const char *code, const char *reason, const char *msg) {
    char e[700], rs[160];
    pldmgr_json_escape(msg ? msg : "", e, sizeof(e));
    pldmgr_json_escape(reason ? reason : "", rs, sizeof(rs));
    return gr_fmt(buf, size, status, 409,
                  "{\"error\":\"%s\",\"reason\":\"%s\",\"message\":\"%s\"}", code, rs, e);
}

/* titleId 校验：9 位 alnum。够挡住所有路径穿越，又不会把 DLC 后缀之类误判成 tid。 */
static int gr_tid_ok(const char *t) {
    if (!t || strlen(t) != 9) return 0;
    for (int i = 0; i < 9; i++)
        if (!isalnum((unsigned char)t[i])) return 0;
    return 1;
}

/* ── 路径安全 ─────────────────────────────────────────────────────────────
 * 删除是**不可逆**的，而且面板没有 undo。所以宁可 403 也不 rm -rf 一条
 * 拿不准的路径 —— 这里每一行拦的都是「一条拼错的路径毁掉整块盘」的可能。 */

/* 前缀匹配且要求边界正好是 '/'，避免 /user/app 顺手匹配上 /user/app_extra。 */
static int gr_under(const char *p, const char *dir) {
    size_t n = strlen(dir);
    return strncmp(p, dir, n) == 0 && (p[n] == '/' || p[n] == '\0');
}

/* 这些地方一律不碰：系统分区与「安装」的领域（/user/app 是卸载的活，不是删源的活）。 */
static int gr_protected(const char *p) {
    if (!p || p[0] != '/') return 1;
    if (!strcmp(p, "/")) return 1;
    if (gr_under(p, "/system") || gr_under(p, "/system_data")) return 1;
    if (gr_under(p, "/user/app")) return 1;
    if (gr_under(p, "/user/home")) return 1;  /* 存档在别处管，不归删除游戏管 */
    if (!strcmp(p, "/user") || !strcmp(p, "/data") || !strcmp(p, "/mnt")) return 1;
    return 0;
}

static int gr_img_ext(const char *p) {
    const char *e = strrchr(p, '.');
    if (!e) return 0;
    return !strcasecmp(e, ".ffpkg") || !strcasecmp(e, ".exfat") ||
           !strcasecmp(e, ".ffpfs") || !strcasecmp(e, ".ffpfsc");
}

/* 白名单：只有两种东西算「可删的物理源」。
 *   ① 注册过的镜像文件（mount_img.lnk 指出来、扩展名符合）；
 *   ② 一个带 sce_sys/param.sfo 的目录，且它的目录名就是 titleId，
 *      或者它落在已知 dump 扫描根之下（用户自己给文件夹改过名也还认）。
 * 两个分支都先过 gr_protected，且都要真的 stat 得到。 */
static int gr_deletable(const char *tid, const char *path) {
    if (!tid || !gr_tid_ok(tid)) return 0;
    if (!path || !path[0]) return 0;
    if (strstr(path, "..")) return 0;   /* 主机路径里的 '..' 只可能是人为拼出来的 */
    if (gr_protected(path)) return 0;

    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISREG(st.st_mode)) return gr_img_ext(path);
    if (!S_ISDIR(st.st_mode)) return 0;

    char sfo[800];
    if (snprintf(sfo, sizeof(sfo), "%s/sce_sys/param.sfo", path) >= (int)sizeof(sfo)) return 0;
    struct stat fs;
    if (stat(sfo, &fs) != 0 || !S_ISREG(fs.st_mode)) return 0;

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!strcmp(base, tid)) return 1;
    return game_mgr_is_scan_path(path);
}

/* ShadowMount 正把镜像挂在这个点上 = 游戏八成还在跑。只有 /mnt/shadowmnt 下的
 * 运行时挂载才算「在运行」：mount.lnk 对裸目录游戏指向的源目录**一直**存在，
 * 拿它当判据会让删源对所有裸目录游戏永远 409。 */
static int gr_running(const char *runtime) {
    if (!runtime || strncmp(runtime, "/mnt/shadowmnt", 14) != 0) return 0;
    if (runtime[14] != '\0' && runtime[14] != '/') return 0;
    struct stat st;
    return stat(runtime, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ── 递归统计与删除 ───────────────────────────────────────────────────────
 * 两者都数**条目**（文件 + 目录 + 符号链接），所以 measure 与 delete 数的是
 * 同一套东西，进度条两端才对得上；只数文件的话，删一个 3 层目录会停在 0% 不动。 */
typedef struct { long long bytes; long long entries; int stop; } gr_acc_t;

#define GR_MAX_DEPTH 32
#define GR_MAX_ENTRIES 1000000

static void gr_measure(const char *path, int depth, gr_acc_t *a) {
    if (a->stop) return;
    if (depth > GR_MAX_DEPTH) { a->stop = 1; return; }
    struct stat st;
    if (lstat(path, &st) != 0) return; /* 中途消失的条目不计入，stat 失败不算错误 */

    if (S_ISDIR(st.st_mode)) {
        a->entries++;
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *e;
        char child[1024];
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child))
                continue;
            if (a->entries >= GR_MAX_ENTRIES || gj_cancelled()) { a->stop = 1; break; }
            gr_measure(child, depth + 1, a);
            if (a->stop) break;
        }
        closedir(d);
    } else {
        a->entries++;
        a->bytes += (long long)st.st_size;
    }
}

/* 0 = 删完，1 = 用户取消，-1 = 出错（err 回一句人话）。
 * 只 unlink/lstat 目录项，**绝不跟随符号链接**（lstat 已保证）。 */
static int gr_rm(const char *path, int depth, char *err, size_t errsz) {
    if (gj_cancelled()) return 1;
    if (depth > GR_MAX_DEPTH) {
        snprintf(err, errsz, "目录层级超过 %d 层，已中止", GR_MAX_DEPTH);
        return -1;
    }
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) return 0; /* 已经没了 = 目标达成，不是失败 */
        snprintf(err, errsz, "读取 %s 失败：%s", path, strerror(errno));
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) {
            snprintf(err, errsz, "打开目录 %s 失败：%s", path, strerror(errno));
            return -1;
        }
        struct dirent *e;
        char child[1024];
        int rc = 0;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child)) {
                snprintf(err, errsz, "路径过长：%s/%s", path, e->d_name);
                rc = -1;
                break;
            }
            if (gj_cancelled()) { rc = 1; break; }
            rc = gr_rm(child, depth + 1, err, errsz);
            if (rc != 0) break;
        }
        closedir(d);
        if (rc != 0) return rc;
        if (rmdir(path) != 0) {
            if (errno == ENOENT) return 0;
            snprintf(err, errsz, "删除目录 %s 失败：%s", path, strerror(errno));
            return -1;
        }
        return 0;
    }

    long long sz = (long long)st.st_size;
    if (unlink(path) != 0) {
        if (errno == ENOENT) return 0;
        snprintf(err, errsz, "删除 %s 失败：%s", path, strerror(errno));
        return -1;
    }
    gj_add_bytes(sz);
    gj_add_file();
    return 0;
}

/* ── 删除任务 ───────────────────────────────────────────────────────────── */
typedef struct {
    char path[700];
    char tid[32];
} gr_task_t;

static void *gr_delete_worker(void *arg) {
    gr_task_t *t = (gr_task_t *)arg;

    gj_set_phase("measuring");
    gr_acc_t a = {0, 0, 0};
    gr_measure(t->path, 0, &a);
    gj_set_totals(a.bytes, a.entries);
    pldmgr_log("[PLDMGR] 删除任务 %s：路径 %s，%lld 条 / %lld 字节\n",
               t->tid, t->path, a.entries, a.bytes);

    if (gj_cancelled()) {
        gj_fail("cancelled", "已取消");
        game_mgr_invalidate();
        free(t);
        return NULL;
    }

    gj_set_phase("deleting");
    char err[512];
    err[0] = 0;
    int rc = gr_rm(t->path, 0, err, sizeof(err));
    if (rc == 1) {
        gj_fail("cancelled", "已取消");
    } else if (rc != 0) {
        gj_fail("failed", err[0] ? err : "删除失败");
        pldmgr_log("[PLDMGR] 删除任务 %s 失败：%s\n", t->tid, err);
        game_mgr_invalidate();
        free(t);
        return NULL;
    }

    /* 列表缓存必须在**成功之后**作废：只有这一刻「这款游戏已经不在了」才成真。
     * 放在开头作废的话，紧接着的那次刷新读到的还是半删状态的目录树。 */
    game_mgr_invalidate();
    gj_set_phase("finalizing");
    gj_ok_done();
    pldmgr_log("[PLDMGR] 删除任务 %s 完成\n", t->tid);
    free(t);
    return NULL;
}

int game_runtime_delete(const char *body, size_t len, char *buf, size_t size, int *status) {
    if (!buf || !status || size < 64) return -1;

    char tid[32] = "";
    int confirm = 0;
    cJSON *root = (body && len) ? cJSON_ParseWithLength(body, len) : NULL;
    if (root) {
        cJSON *f = cJSON_GetObjectItem(root, "tid");
        if (cJSON_IsString(f) && f->valuestring) snprintf(tid, sizeof(tid), "%s", f->valuestring);
        confirm = cJSON_IsTrue(cJSON_GetObjectItem(root, "confirm"));
        cJSON_Delete(root);
    }

    if (!gr_tid_ok(tid))
        return gr_err(buf, size, status, 404, "ENOENT", "主机上找不到这个游戏");
    if (!confirm)
        return gr_err(buf, size, status, 400, "EINVAL", "需要 confirm=true 才允许删除");

    char src[16], path[700], runtime[700];
    if (!game_mgr_source(tid, src, sizeof(src), path, sizeof(path), runtime, sizeof(runtime)))
        return gr_err(buf, size, status, 404, "ENOENT", "主机上找不到这个游戏");

    /* src 是**来源类别**（pkg / image / folder，见 game_mgr_source 的注释）。
     * 只有真·内置安装的 PKG 才没有可删的物理源 —— ShadowMount 的镜像/裸目录游戏
     * 同样在 app.db 里，但它们有物理源（镜像文件 / 外置目录），删得。 */
    if (!strcmp(src, "pkg"))
        return gr_err(buf, size, status, 403, "EPERM",
                      "已安装的 PKG 没有可删除的物理源，请改用「卸载」");

    if (gr_running(runtime))
        return gr_busy(buf, size, status, "EBUSY", "game_active",
                       "这个游戏还在运行（镜像正处于挂载状态），请先退出游戏再删除");

    struct stat st;
    if (stat(path, &st) != 0)
        /* 源已经不在了 —— **不是失败**：契约把它列为 200，前端据此直接关弹窗。
         * 回 409/500 会让用户以为删失败了，然后反复点同一个已经不存在的目标。 */
        return gr_fmt(buf, size, status, 200,
                      "{\"source_missing\":true,\"scan_queued\":true}");

    if (!gr_deletable(tid, path))
        return gr_err(buf, size, status, 403, "EPERM",
                      "这个游戏的源在受保护的路径下，不允许删除");

    long long job_id = 0;
    char msg[192];
    if (gj_begin(GJ_KIND_DELETE, &job_id, msg, sizeof(msg)) != 0)
        return gr_busy(buf, size, status, "EBUSY", "busy",
                       msg[0] ? msg : "已有任务在进行，请等它结束再试");

    gr_task_t *task = (gr_task_t *)calloc(1, sizeof(*task));
    if (!task) {
        gj_unlock(); /* gj_begin 成功后是**持锁**返回的，这里必须还回去 */
        return gr_err(buf, size, status, 500, "ENOMEM", "内存不足，无法开始删除任务");
    }
    snprintf(task->path, sizeof(task->path), "%s", path);
    snprintf(task->tid, sizeof(task->tid), "%s", tid);
    snprintf(gj_state()->aux, sizeof(gj_state()->aux), "%s", path);
    snprintf(gj_state()->tid, sizeof(gj_state()->tid), "%s", tid);
    snprintf(gj_state()->phase, sizeof(gj_state()->phase), "measuring");
    gj_unlock();

    pthread_t th;
    if (pthread_create(&th, NULL, gr_delete_worker, task) != 0) {
        free(task);
        /* 线程起不来必须把槽还回去，否则那个永远停在 measuring 的任务会把
         * 后续每一次打包与删除都挡成 409，而且谁也说不清为什么。 */
        gj_fail("failed", "后台删除线程创建失败");
        return gr_err(buf, size, status, 500, "EFAILED", "后台删除线程创建失败");
    }
    pthread_detach(th);

    return gr_fmt(buf, size, status, 202, "{\"job_id\":%lld,\"scan_queued\":true}", job_id);
}

/* ── ShadowMountPlus 代理（挂载 / 卸载挂载） ─────────────────────────────── */

typedef struct { char *p; size_t n, cap; } gr_body_t;

static size_t gr_write_cb(void *ptr, size_t sz, size_t nm, void *ud) {
    gr_body_t *b = (gr_body_t *)ud;
    size_t k = sz * nm;
    if (k == 0) return 0;
    if (b->n + k + 1 > b->cap) return 0; /* 超预算就让 curl 报写错误，别静默截断半截 JSON */
    memcpy(b->p + b->n, ptr, k);
    b->n += k;
    b->p[b->n] = '\0';
    return k;
}

/* [CN-FORK] v0.5.2.142 curl 全局初始化收到 utils.h 的 pldmgr_curl_init（实现 src/curl_init.c）：
 * save_remote.c（自动存档的传输层）也要 curl，两边各持一把 once 控制块就是「两个线程
 * 各自认为自己是第一个」—— 正好踩中 curl 文档里那个「global_init 不是线程安全的」。
 * 所以这里不再自己搞 once，直接调那一个（它内部也是 pthread_once，且是全进程唯一的一把）。 */

/* 0 = 拿到了 HTTP 响应（*code 有效、resp 已 NUL 结尾）；-1 = 连不上这个服务。 */
static int gr_smp(const char *api, const char *json, long *code, char *resp, size_t respsz) {
    char url[320];
    if (respsz < 8) return -1;
    resp[0] = '\0';
    *code = 0;
    if (snprintf(url, sizeof(url), "%s%s", GR_SMP_BASE, api) >= (int)sizeof(url)) return -1;

    pldmgr_curl_init();
    CURL *c = curl_easy_init();
    if (!c) return -1;

    gr_body_t b = {resp, 0, respsz};
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, "Connection: close");

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(json));
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, gr_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    /* 在线程里必须关掉信号量超时（否则 libcurl 会用 alarm，多线程下直接崩）。 */
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        pldmgr_log("[PLDMGR] ShadowMount+ 代理失败 %s：%s\n", api, curl_easy_strerror(rc));
        return -1;
    }
    *code = http;
    if (b.n == 0) resp[0] = '\0';
    return 0;
}

/* 上游报错时把它 message / error 里的那句话挖出来带给用户 —— 吞掉只会留下
 * 一个「点了一下没反应」的空按钮，而上游往往知道真实原因（PFS 校验失败等）。 */
static int gr_upstream_msg(const char *body, char *out, size_t osz) {
    out[0] = '\0';
    if (!body || !body[0]) return 0;
    cJSON *o = cJSON_Parse(body);
    if (!o) return 0;
    cJSON *m = cJSON_GetObjectItem(o, "message");
    if (!cJSON_IsString(m)) m = cJSON_GetObjectItem(o, "error");
    if (cJSON_IsString(m) && m->valuestring) snprintf(out, osz, "%s", m->valuestring);
    cJSON_Delete(o);
    return out[0] ? 1 : 0;
}

static int gr_unavailable(char *buf, size_t size, int *status) {
    return gr_err(buf, size, status, 503, "EUNAVAILABLE",
                  "连不上 ShadowMount+（127.0.0.1:10101）。"
                  "请先在「管理 Payload」里启动 ShadowMount+，再回来挂载。");
}

static int gr_upstream_fail(char *buf, size_t size, int *status, long http, const char *body) {
    char m[400];
    if (!gr_upstream_msg(body, m, sizeof(m)) || !m[0])
        snprintf(m, sizeof(m), "ShadowMount+ 拒绝了这次操作（HTTP %ld）", http);
    int st = (http >= 400 && http <= 599) ? (int)http : 502;
    return gr_err(buf, size, status, st, "EUPSTREAM", m);
}

static int gr_mount_common(const char *body, size_t len, int want_mount,
                           char *buf, size_t size, int *status) {
    if (!buf || !status || size < 64) return -1;

    char tid[32] = "", mode[8] = "ro";
    cJSON *root = (body && len) ? cJSON_ParseWithLength(body, len) : NULL;
    if (root) {
        cJSON *f = cJSON_GetObjectItem(root, "tid");
        if (cJSON_IsString(f) && f->valuestring) snprintf(tid, sizeof(tid), "%s", f->valuestring);
        f = cJSON_GetObjectItem(root, "mode");
        if (cJSON_IsString(f) && f->valuestring) snprintf(mode, sizeof(mode), "%s", f->valuestring);
        cJSON_Delete(root);
    }

    if (!gr_tid_ok(tid))
        return gr_err(buf, size, status, 404, "ENOENT", "主机上找不到这个游戏");

    int known = 0;
    if (want_mount) {
        if (strcmp(mode, "ro") != 0 && strcmp(mode, "rw") != 0)
            return gr_err(buf, size, status, 400, "EINVAL", "mode 只能是 \"ro\" 或 \"rw\"");
    }

    char src[16], path[700], runtime[700];
    known = game_mgr_source(tid, src, sizeof(src), path, sizeof(path), runtime, sizeof(runtime));
    if (!known)
        return gr_err(buf, size, status, 404, "ENOENT", "主机上找不到这个游戏");

    char payload[256], up[8192];
    long http = 0;
    if (want_mount) {
        char e_tid[32];
        pldmgr_json_escape(tid, e_tid, sizeof(e_tid));
        snprintf(payload, sizeof(payload), "{\"title_id\":\"%s\",\"mode\":\"%s\"}", e_tid, mode);
        if (gr_smp("/api/v1/games/mount", payload, &http, up, sizeof(up)) != 0)
            return gr_unavailable(buf, size, status);
        if (http < 200 || http >= 300) return gr_upstream_fail(buf, size, status, http, up);
        return gr_fmt(buf, size, status, 200, "{\"ok\":true,\"mode\":\"%s\"}", mode);
    }

    char e_tid[32];
    pldmgr_json_escape(tid, e_tid, sizeof(e_tid));
    snprintf(payload, sizeof(payload), "{\"title_id\":\"%s\"}", e_tid);
    if (gr_smp("/api/v1/games/unmount", payload, &http, up, sizeof(up)) != 0)
        return gr_unavailable(buf, size, status);
    if (http < 200 || http >= 300) return gr_upstream_fail(buf, size, status, http, up);
    return gr_fmt(buf, size, status, 200, "{\"ok\":true}");
}

int game_runtime_mount(const char *body, size_t len, char *buf, size_t size, int *status) {
    if (!status) return -1;
    return gr_mount_common(body, len, 1, buf, size, status);
}

int game_runtime_unmount(const char *body, size_t len, char *buf, size_t size, int *status) {
    if (!status) return -1;
    return gr_mount_common(body, len, 0, buf, size, status);
}

/* ── 卸载安装（走系统 API） ───────────────────────────────────────────────
 * 与 src/app_installer.c 用的是同一个库、同一套 Initialize→调用→Terminate 顺序。
 * 单参数签名不是猜的：flatz/ps4_remote_pkg_installer/installer.c 里
 * `ret = sceAppInstUtilAppUnInstall(title_id);` 就是这么调的。 */
int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppUnInstall(const char *);

int game_runtime_uninstall(const char *body, size_t len, char *buf, size_t size, int *status) {
    if (!buf || !status || size < 64) return -1;

    char tid[32] = "";
    cJSON *root = (body && len) ? cJSON_ParseWithLength(body, len) : NULL;
    if (root) {
        cJSON *f = cJSON_GetObjectItem(root, "tid");
        if (cJSON_IsString(f) && f->valuestring) snprintf(tid, sizeof(tid), "%s", f->valuestring);
        cJSON_Delete(root);
    }
    if (!gr_tid_ok(tid))
        return gr_err(buf, size, status, 404, "ENOENT", "主机上找不到这个游戏");

    char src[16], path[700], runtime[700];
    if (!game_mgr_source(tid, src, sizeof(src), path, sizeof(path), runtime, sizeof(runtime)))
        return gr_err(buf, size, status, 404, "ENOENT", "主机上找不到这个游戏");

    if (gr_running(runtime))
        return gr_busy(buf, size, status, "EBUSY", "game_active",
                       "这个游戏还在运行（镜像正处于挂载状态），请先退出游戏再卸载");

    int err = sceAppInstUtilInitialize();
    if (err)
        return gr_fmt(buf, size, status, 500, "{\"error\":\"EUNINSTALL\","
                      "\"message\":\"初始化安装服务失败（0x%08X）\"}", (unsigned)err);

    int rc = sceAppInstUtilAppUnInstall(tid);
    sceAppInstUtilTerminate();

    if (rc)
        /* 系统给的错误码是**唯一**可信的原因：这里编一句话只会误导排障。 */
        return gr_fmt(buf, size, status, 500, "{\"error\":\"EUNINSTALL\","
                      "\"message\":\"卸载失败（0x%08X）\"}", (unsigned)rc);

    game_mgr_invalidate();
    return gr_fmt(buf, size, status, 200, "{\"ok\":true,\"scan_queued\":true}");
}
