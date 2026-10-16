/* [CN-FORK] v0.6.0.177 多媒体的远程备份：配置读取 + 手动同步任务 + 自动增量备份。
 * 文件头注释见 include/media_remote.h（那里的四段就是这个模块的设计说明）。
 *
 * 三条不变量，改这里之前先读：
 *   ① **传输一行都不自己写** —— 全部走 save_remote.c 的 sr_push_media。
 *     传输层已经踩过的坑（.tmp 改名、FTP 建目录、SMB 的 EEXIST、低速熔断）
 *     不该在第二份实现里再踩一遍；两边慢慢走偏就是「存档能传、媒体不能传」。
 *   ② **HTTP 线程绝不停在网络上** —— 手动同步起线程后立刻回 job_id，
 *     自动备份的 kick 也只做「读配置 + 起线程」，网络都在 worker 里。
 *   ③ **自动备份不占任务槽** —— 用户可能正在打包，两个要能同时跑；
 *     互斥靠本文件的 g_mr_mu（手动同步也会拿它，两条路不会同时往远端写）。
 */

#include "media_remote.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "config.h"        /* config_read_media_archive */
#include "game_job.h"      /* 任务槽：进度 / 取消 */
#include "game_media.h"    /* game_media_each：枚举一个 tid 的媒体条目 */
#include "json_helpers.h"  /* ensure_dir_recursive */
#include "pldmgr.h"        /* MEDIA_ARCHIVE_PATH / MEDIA_BACKUP_STATE_DIR / pldmgr_log */

/* ── 小工具 ─────────────────────────────────────────────────────────────── */

/* 与 game_media.c 的 md_tid_ok 同规格（等值比较用，不做 SQL，但仍挡掉能把
 * JSON / 日志打穿的字符 —— tid 会被拼进账本文件名）。 */
static int mr_tid_ok(const char *tid) {
    if (!tid) return 0;
    size_t n = strlen(tid);
    if (n < 4 || n > 16) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = tid[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
            return 0;
    }
    return 1;
}

static void mr_json_str(cJSON *root, const char *key, char *dst, size_t cap) {
    cJSON *f = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(f) && f->valuestring) snprintf(dst, cap, "%s", f->valuestring);
}

/* 把一个 JSON 对象里的字段灌进 sr_cfg_t。**按协议段**灌（见 mr_load_cfg）：
 * 前端存的形状与 ARCHIVE_GLOBAL_CONFIG 完全一致 —— {mode, auto, smb:{…}, ftp:{…}} ——
 * 这样「测试连接」能直接复用 game_saves_test（它按 smb/ftp/http 分段读），
 * 两个弹窗也共用同一套 MODE_FIELDS / toRemoteCfg，不会各写各的字段名。 */
static void mr_fill(cJSON *o, sr_cfg_t *cfg) {
    if (!cJSON_IsObject(o)) return;
    mr_json_str(o, "host", cfg->host, sizeof(cfg->host));
    mr_json_str(o, "path", cfg->path, sizeof(cfg->path));
    mr_json_str(o, "user", cfg->user, sizeof(cfg->user));
    mr_json_str(o, "password", cfg->password, sizeof(cfg->password));
    mr_json_str(o, "url", cfg->url, sizeof(cfg->url));
    mr_json_str(o, "token", cfg->token, sizeof(cfg->token));
    mr_json_str(o, "share", cfg->share, sizeof(cfg->share));
    mr_json_str(o, "sub", cfg->sub, sizeof(cfg->sub));
    /* 端口两种都认：前端 <input> 交出来的是字符串，手写的配置文件里常是数字。 */
    cJSON *p = cJSON_GetObjectItem(o, "port");
    if (cJSON_IsNumber(p)) cfg->port = p->valueint;
    else if (cJSON_IsString(p) && p->valuestring) cfg->port = atoi(p->valuestring);
}

/* ── ① 配置 ─────────────────────────────────────────────────────────────── */

int mr_load_cfg(sr_cfg_t *cfg, int *auto_on, char *err, size_t errsz) {
    if (err && errsz) err[0] = 0;
    if (auto_on) *auto_on = 0;
    if (!cfg) return -1;
    memset(cfg, 0, sizeof(*cfg));

    char *raw = malloc(4096);
    if (!raw) {
        if (err) snprintf(err, errsz, "内存不足");
        return -1;
    }
    if (config_read_media_archive(raw, 4096) < 0) {
        free(raw);
        if (err) snprintf(err, errsz, "还没有配置多媒体的远程位置（设置 → 多媒体存档配置）");
        return -1;
    }
    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) {
        if (err) snprintf(err, errsz, "多媒体存档配置读不出来（文件可能损坏）");
        return -1;
    }

    mr_json_str(root, "mode", cfg->mode, sizeof(cfg->mode));
    if (!cfg->mode[0]) snprintf(cfg->mode, sizeof(cfg->mode), "smb");
    if (!sr_mode_supported(cfg->mode)) {
        cJSON_Delete(root);
        if (err) snprintf(err, errsz, "不支持的传输方式（%s）", cfg->mode);
        return -1;
    }
    /* 协议段（{mode, smb:{…}, ftp:{…}}）优先；老写法那种「全摊在顶层」的平铺 JSON
     * 也认（有人手改过配置文件 / 早期试过一版），段不在就退回顶层读。 */
    cJSON *sec = cJSON_GetObjectItem(root, cfg->mode);
    mr_fill(cJSON_IsObject(sec) ? sec : root, cfg);
    if (auto_on) *auto_on = cJSON_IsTrue(cJSON_GetObjectItem(root, "auto")) ? 1 : 0;
    cJSON_Delete(root);

    /* 最低限度的目标字段要齐 —— 缺了的话传输层会在半路才报错，
     * 用户看到的是「同步失败：没有填主机」，而不是「还没填完」。 */
    if (!strcmp(cfg->mode, "smb") && !(cfg->host[0] && cfg->share[0])) {
        if (err) snprintf(err, errsz, "SMB 还差主机地址或共享名");
        return -1;
    }
    if (!strcmp(cfg->mode, "ftp") && !cfg->host[0]) {
        if (err) snprintf(err, errsz, "没有填 FTP 主机地址");
        return -1;
    }
    if (!strcmp(cfg->mode, "http") && !cfg->url[0]) {
        if (err) snprintf(err, errsz, "没有填 HTTP 地址");
        return -1;
    }
    return 0;
}

/* ── ② 增量账本：MEDIA_BACKUP_STATE_DIR/<tid>.json ─────────────────────────
 * 形状 {"files":{"<rel>":{"s":size}}}。
 * 为什么记 size 不记 mtime：媒体文件**只增不改**（截图/录屏落盘后不会再被写），
 * 而 mtime 在两次读取之间没有任何东西会动它，多记一个字段就多一处会说谎的地方。 */

static void mr_state_path(const char *tid, char *out, size_t osz) {
    snprintf(out, osz, "%s/%s.json", MEDIA_BACKUP_STATE_DIR, tid);
}

static cJSON *mr_state_files(cJSON *root) {
    cJSON *f = cJSON_GetObjectItem(root, "files");
    if (!cJSON_IsObject(f)) {
        f = cJSON_CreateObject();
        if (f) cJSON_AddItemToObject(root, "files", f);
    }
    return f;
}

static cJSON *mr_state_load(const char *tid) {
    char path[300];
    mr_state_path(tid, path, sizeof(path));
    cJSON *root = NULL;
    FILE *f = fopen(path, "rb");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz < (4 << 20)) { /* 账本正常几 KB；4MB 是防坏文件的闸门 */
                char *buf = (char *)malloc((size_t)sz + 1);
                if (buf) {
                    size_t got = fread(buf, 1, (size_t)sz, f);
                    buf[got] = 0;
                    root = cJSON_Parse(buf);
                    free(buf);
                }
            }
        }
        fclose(f);
    }
    if (!root) {
        /* 没有账本 / 账本坏了 —— 都当「一条都没传过」。坏账本的代价只是
         * 重新全量传一遍（幂等），而不是同步永远不工作。 */
        root = cJSON_CreateObject();
        if (root && tid) pldmgr_log("[PLDMGR] media: backup state missing/broken for %s\n", tid);
    }
    return root;
}

/* 写盘：先写 .tmp 再 rename —— 与存档/媒体那套「原子替换」同一个口径，
 * 半截账本比没有账本更坏（解析失败会连已传的记录一起丢）。 */
static int mr_state_save(const char *tid, cJSON *root) {
    if (!root) return -1;
    if (ensure_dir_recursive(MEDIA_BACKUP_STATE_DIR) != 0) return -1;
    char *s = cJSON_PrintUnformatted(root);
    if (!s) return -1;
    char path[300], tmp[320];
    mr_state_path(tid, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int rc = -1;
    FILE *f = fopen(tmp, "wb");
    if (f) {
        /* fclose **必须无条件只调一次**：原写法是 `fwrite && fclose`，
         * fwrite 失败时 fclose 没执行、下面又补一刀 —— 对同一个 FILE* 关两次
         * 是未定义行为（轻则 EBADF，重则下次 fopen 复用了同一个槽位就写错文件）。 */
        size_t n = strlen(s);
        int wr = fwrite(s, 1, n, f) == n;
        if (fclose(f) != 0) wr = 0;
        if (wr && rename(tmp, path) == 0) rc = 0;
        if (rc != 0) unlink(tmp);
    }
    free(s);
    return rc;
}

static long long mr_state_size_of(cJSON *files, const char *rel) {
    cJSON *e = cJSON_GetObjectItem(files, rel);
    cJSON *s = e ? cJSON_GetObjectItem(e, "s") : NULL;
    return cJSON_IsNumber(s) ? (long long)s->valuedouble : -1;
}

static void mr_state_mark(cJSON *files, const char *rel, long long size) {
    if (!files || !rel) return;
    cJSON *e = cJSON_CreateObject();
    if (!e) return;
    cJSON_AddItemToObject(e, "s", cJSON_CreateNumber((double)size));
    cJSON_DeleteItemFromObject(files, rel); /* 同名先删再加：覆盖不叠加 */
    cJSON_AddItemToObject(files, rel, e);
}

/* ── ③ 挑文件 ───────────────────────────────────────────────────────────── */

typedef struct {
    char rel[600];   /* 远端相对路径（<media>/ 之下） */
    char local[900]; /* 本机绝对路径 */
    long long size;
} mr_file_t;

typedef struct {
    mr_file_t *v;
    int n, cap;
    char **ids;      /* 手动选中名单（NULL = 全量） */
    int nids;
    int force;       /* 1 = 忽略账本强推（用户点名的那些） */
    cJSON *files;    /* 账本的 "files" 对象（force 时不用，但为了少一个分支照样带） */
    long long bytes;
    int skipped;
} mr_collect_t;

static int mr_id_in(const mr_collect_t *c, const char *content) {
    for (int i = 0; i < c->nids; i++)
        if (c->ids[i] && strcmp(c->ids[i], content) == 0) return 1;
    return 0;
}

static int mr_collect_cb(const game_media_item_t *it, void *user) {
    mr_collect_t *c = (mr_collect_t *)user;
    if (!it->content[0]) return 0;
    /* md_path_ok 在读库那侧已经挡过 .. / 控制字符；这里只再确认前缀 ——
     * rel 是拿 content 直接 +13 算出来的，前缀不对就会算出一个垃圾路径。 */
    if (strncmp(it->content, "/av_contents/", 13) != 0) return 0;
    if (c->nids && !mr_id_in(c, it->content)) return 0;

    const char *rel = it->content + 13;
    if (!c->force && c->files) {
        long long have = mr_state_size_of(c->files, rel);
        if (have >= 0 && have == it->size) {
            c->skipped++; /* 已经传过、且大小对得上 → 跳过（增量的全部意义） */
            return 0;
        }
    }

    if (c->n == c->cap) {
        int nc = c->cap ? c->cap * 2 : 32;
        mr_file_t *nv = (mr_file_t *)realloc(c->v, (size_t)nc * sizeof(mr_file_t));
        if (!nv) return 1; /* 收不动就停：宁可少同步几条，也别在回调里半路崩 */
        c->v = nv;
        c->cap = nc;
    }
    mr_file_t *e = &c->v[c->n];
    snprintf(e->rel, sizeof(e->rel), "%s", rel);
    snprintf(e->local, sizeof(e->local), GAME_MEDIA_USER_PREFIX "%s", it->content);
    e->size = it->size > 0 ? it->size : 0;
    c->bytes += e->size;
    c->n++;
    return 0;
}

/* 一个同步任务的全部入参（起线程前就拷好，worker 不回头碰请求内存）。 */
typedef struct {
    char tid[32];
    int manual;      /* 1 = 走任务槽（手动同步）；0 = 自动备份 */
    int force;       /* 1 = 忽略账本（手动选中了具体文件） */
    char **ids;
    int nids;
    long long job_id;
} mr_task_t;

static void mr_task_free(mr_task_t *t) {
    if (!t) return;
    for (int i = 0; i < t->nids; i++) free(t->ids[i]);
    free(t->ids);
    free(t);
}

/* sr_push_media 的 step 钩子：每传完一个文件回调一次。
 * 三件事在这里一起做，因为它们必须发生在**同一次**回调里，分开了就会不同步：
 *   ① 记账（这条传上去了 → 下次别再传）
 *   ② 进度（手动同步才更新任务槽）
 *   ③ 取消（手动同步看任务槽；自动备份没有槽，恒不取消） */
typedef struct {
    mr_task_t *t;
    mr_collect_t *c;
} mr_step_t;

static int mr_step(void *ud, int i, int ok) {
    mr_step_t *s = (mr_step_t *)ud;
    if (!s || !s->c || i < 0 || i >= s->c->n) return 1;
    if (ok) {
        mr_state_mark(s->c->files, s->c->v[i].rel, s->c->v[i].size);
        if (s->t->manual) {
            gj_add_bytes(s->c->v[i].size);
            gj_add_file();
        }
    }
    if (s->t->manual && gj_cancelled()) return 1;
    return 0;
}

/* ── ④ worker ───────────────────────────────────────────────────────────── */

/* 互斥：同一时刻只允许一个同步在跑（手动 vs 自动、自动 vs 自动）。
 * 手动用阻塞锁（用户点了按钮就该等），自动用 trylock（等不到就下回再补）。 */
static pthread_mutex_t g_mr_mu = PTHREAD_MUTEX_INITIALIZER;

static void mr_finish_job(mr_task_t *t, int rc, const char *err) {
    if (!t->manual) return;
    if (rc == 1) {
        gj_fail("cancelled", "同步已取消");
    } else if (rc < 0) {
        gj_fail("failed", (err && *err) ? err : "同步失败");
    } else {
        gj_ok_done();
    }
}

static void mr_run(mr_task_t *t) {
    char err[300];
    err[0] = 0;
    sr_cfg_t cfg;
    int auto_on = 0;

    if (mr_load_cfg(&cfg, &auto_on, err, sizeof(err)) != 0) {
        pldmgr_log("[PLDMGR] media: sync %s not configured (%s)\n", t->tid, err);
        mr_finish_job(t, -1, err);
        return;
    }

    mr_collect_t c;
    memset(&c, 0, sizeof(c));
    c.ids = t->ids;
    c.nids = t->nids;
    c.force = t->force;
    cJSON *state = mr_state_load(t->tid);
    c.files = state ? mr_state_files(state) : NULL;

    if (game_media_each(t->tid, mr_collect_cb, &c) != 0) {
        mr_state_save(t->tid, state);
        if (state) cJSON_Delete(state);
        free(c.v);
        pldmgr_log("[PLDMGR] media: sync %s cannot read media library\n", t->tid);
        mr_finish_job(t, -1, "读取系统媒体库失败（av_content.db）");
        return;
    }

    if (t->manual) {
        /* 先把总数写进槽再开传：前端第一拍就要拿到 files_total / bytes_total，
         * 否则进度条第一秒显示 0/0，看起来像没启动。 */
        gj_set_totals(c.bytes, c.n);
        gj_set_phase(c.n ? "syncing" : "done");
        pldmgr_log("[PLDMGR] media: sync %s start (%d files, %lld bytes, skipped %d)\n",
                   t->tid, c.n, c.bytes, c.skipped);
    } else {
        pldmgr_log("[PLDMGR] media: auto backup %s (%d new, skipped %d)\n",
                   t->tid, c.n, c.skipped);
    }

    int rc = 0;
    if (c.n == 0) {
        /* 一条都不用传：账本照样落一次（可能这次是清了旧记录），
         * 手动同步要正常收尾 —— 「没有新文件」不是失败。 */
        rc = 0;
    } else {
        sr_mfile_t *mf = (sr_mfile_t *)calloc((size_t)c.n, sizeof(sr_mfile_t));
        if (!mf) {
            rc = -1;
            snprintf(err, sizeof(err), "内存不足");
        } else {
            for (int i = 0; i < c.n; i++) {
                mf[i].rel = c.v[i].rel;
                mf[i].local = c.v[i].local;
            }
            mr_step_t st = { t, &c };
            long long done_bytes = 0;
            int done_files = 0;
            rc = sr_push_media(&cfg, t->tid, mf, c.n, &done_bytes, &done_files,
                               mr_step, &st, err, sizeof(err));
            free(mf);
        }
    }

    /* 无论成功、失败还是取消，**已传上去的那部分都要记账** —— 下次同步
     * 不该把它们再传一遍（用户取消一次的代价不该是下次全量重来）。 */
    if (state) {
        if (mr_state_save(t->tid, state) != 0)
            pldmgr_log("[PLDMGR] media: backup state save failed for %s\n", t->tid);
        cJSON_Delete(state);
    }
    free(c.v);

    if (rc == 0)
        pldmgr_log("[PLDMGR] media: sync %s done (%d files%s)\n", t->tid, c.n,
                   c.skipped ? ", 已传过的跳过" : "");
    else
        pldmgr_log("[PLDMGR] media: sync %s rc=%d (%s)\n", t->tid, rc, err);
    mr_finish_job(t, rc, err);
}

static void *mr_worker(void *arg) {
    mr_task_t *t = (mr_task_t *)arg;
    pthread_mutex_lock(&g_mr_mu);
    mr_run(t);
    pthread_mutex_unlock(&g_mr_mu);
    mr_task_free(t);
    return NULL;
}

/* ── ⑤ 手动同步（POST /game_media_sync）───────────────────────────────── */

int mr_sync_start(const char *body, size_t len, long long *job_id,
                  char *msg, size_t msgsz) {
    if (job_id) *job_id = 0;
    if (msg && msgsz) msg[0] = 0;

    cJSON *root = cJSON_ParseWithLength(body ? body : "", len);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        if (msg) snprintf(msg, msgsz, "请求体不合法");
        return -2;
    }
    cJSON *jt = cJSON_GetObjectItem(root, "tid");
    if (!cJSON_IsString(jt) || !jt->valuestring || !mr_tid_ok(jt->valuestring)) {
        cJSON_Delete(root);
        if (msg) snprintf(msg, msgsz, "缺少或非法的 tid");
        return -2;
    }
    char tid[32];
    snprintf(tid, sizeof(tid), "%s", jt->valuestring);

    /* ids：数组（只取字符串元素）；缺省 / null / 空数组都当全量。
     * 与 /game_media_pack 同一形状 —— 两个按钮的 body 不该长得不一样。 */
    char **ids = NULL;
    int nids = 0;
    cJSON *ji = cJSON_GetObjectItem(root, "ids");
    if (cJSON_IsArray(ji)) {
        int raw_n = cJSON_GetArraySize(ji);
        if (raw_n > 0) {
            ids = (char **)calloc((size_t)raw_n, sizeof(char *));
            if (!ids) {
                cJSON_Delete(root);
                if (msg) snprintf(msg, msgsz, "内存不足");
                return -2;
            }
            for (int i = 0; i < raw_n; i++) {
                cJSON *el = cJSON_GetArrayItem(ji, i);
                if (!cJSON_IsString(el) || !el->valuestring) continue; /* 非字符串跳过 */
                ids[nids] = strdup(el->valuestring);
                if (ids[nids]) nids++;
            }
        }
    }
    cJSON_Delete(root);

    /* 先确认配置可用再领槽：没配远程就回 400 + 人话，而不是领了槽再失败，
     * 让用户在进度条上看一句莫名其妙的错误。 */
    sr_cfg_t cfg;
    int auto_on = 0;
    char err[300];
    err[0] = 0;
    if (mr_load_cfg(&cfg, &auto_on, err, sizeof(err)) != 0) {
        for (int i = 0; i < nids; i++) free(ids[i]);
        free(ids);
        if (msg) snprintf(msg, msgsz, "%s", err);
        return -2;
    }

    long long id = 0;
    if (gj_begin(GJ_KIND_SYNC, &id, msg, msgsz) != 0) {
        for (int i = 0; i < nids; i++) free(ids[i]);
        free(ids);
        return -1; /* 已有任务在跑（HTTP 侧回 409 EBUSY） */
    }
    /* gj_begin 返回时**仍持锁**，填完字段才能放 —— 见 include/game_job.h。 */
    snprintf(gj_state()->phase, sizeof(gj_state()->phase), "preparing");
    snprintf(gj_state()->tid, sizeof(gj_state()->tid), "%s", tid);
    gj_unlock();

    mr_task_t *t = (mr_task_t *)calloc(1, sizeof(mr_task_t));
    if (!t) {
        gj_fail("failed", "内存不足");
        for (int i = 0; i < nids; i++) free(ids[i]);
        free(ids);
        if (msg) snprintf(msg, msgsz, "内存不足");
        return -2;
    }
    snprintf(t->tid, sizeof(t->tid), "%s", tid);
    t->manual = 1;
    t->force = nids > 0;
    t->ids = ids;
    t->nids = nids;
    t->job_id = id;

    pthread_t th;
    if (pthread_create(&th, NULL, mr_worker, t) != 0) {
        gj_fail("failed", "起不了同步线程");
        mr_task_free(t);
        if (msg) snprintf(msg, msgsz, "起不了同步线程");
        return -2;
    }
    pthread_detach(th);
    if (job_id) *job_id = id;
    return 0;
}

/* ── ⑥ 自动备份（/game_media 列表之后 kick 一下）────────────────────────
 * 不占任务槽、不起 toast、不阻塞 HTTP 线程：读一遍配置，能干就起线程。
 * 已经有同步在跑（trylock 失败）就直接放弃 —— 下次打开列表自然会再补，
 * 「少传一次」永远好过「两路同时往同一个远端写」。 */
void mr_auto_kick(const char *tid) {
    if (!mr_tid_ok(tid)) return;

    sr_cfg_t cfg;
    int auto_on = 0;
    char err[300];
    err[0] = 0;
    if (mr_load_cfg(&cfg, &auto_on, err, sizeof(err)) != 0) return;
    if (!auto_on) return;
    if (pthread_mutex_trylock(&g_mr_mu) != 0) return; /* 有同步在跑：这回不补了 */
    pthread_mutex_unlock(&g_mr_mu);

    mr_task_t *t = (mr_task_t *)calloc(1, sizeof(mr_task_t));
    if (!t) return;
    snprintf(t->tid, sizeof(t->tid), "%s", tid);
    t->manual = 0;
    t->force = 0;

    pthread_t th;
    if (pthread_create(&th, NULL, mr_worker, t) != 0) {
        mr_task_free(t);
        return;
    }
    pthread_detach(th);
}
