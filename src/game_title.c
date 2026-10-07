/* [CN-FORK] v0.5.2.116 改名 + 磁贴两层图（icon0 / pic0）后端
 * 契约与「为什么存两份」见 include/game_title.h 的头注释。 */

#include "game_title.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "game_mgr.h"      /* game_mgr_sce_sys_dir */
#include "json_helpers.h"  /* ensure_dir_recursive / write_file_text */
#include "pldmgr.h"        /* pldmgr_log */
#include "stb_image.h"
#include "stb_image_write.h"
#include "tile_db.h"       /* 改名 / 背景落进主机那一步（app.db + appinfo.db） */
#include "utils.h"         /* pldmgr_json_escape */

#define TT_BG_W 1920
#define TT_BG_H 1080
#define TT_ICO_EDGE 1024 /* 装到主机上的 icon0.png 尺寸（assets/icon0.png 同规格） */

/* ── 路径与 tid ────────────────────────────────────────────────────────────
 * tid 只允许 6..16 位 alnum —— 拿不到 '/' 也拿不到 '.'，路径穿越在这一行就被挡死。
 * 比 game_mgr.c 的 gm_tid_is_game（还要命中 PPSA/CUSA/… 前缀）松一档是刻意的：
 * 这里管的是**我们自己目录下的文件**，不该因为某个自制 tid 不在那五个前缀里
 * 就让改名静默失败；而真正落到主机上那一步走 game_mgr_sce_sys_dir，
 * 那边仍按严格前缀判定。 */
static int tt_tid_ok(const char *tid) {
    if (!tid) return 0;
    size_t n = strlen(tid);
    if (n < 6 || n > 16) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = tid[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

static int tt_dir(const char *tid, char *out, size_t osz) {
    if (!tt_tid_ok(tid)) return -1;
    int n = snprintf(out, osz, GAME_TITLE_DIR "/%s", tid);
    if (n < 0 || (size_t)n >= osz) return -1;
    return 0;
}

static void tt_path(char *out, size_t osz, const char *dir, const char *leaf) {
    snprintf(out, osz, "%s/%s", dir, leaf);
}

static int tt_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static int tt_is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ── 版本号 ────────────────────────────────────────────────────────────────
 * 存成一个**递增整数**而不是 mtime：同一秒内连传两次（很常见的「我再挑一张」）
 * 用 mtime 会得到同一个版本号，浏览器拿它当 query 参数就不刷新了，
 * 用户会看到「换图没生效」。取 max(now, prev+1) 保证严格单调。 */
static long long tt_ver_read(const char *dir, const char *leaf) {
    char p[768];
    tt_path(p, sizeof(p), dir, leaf);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    char b[32];
    size_t n = fread(b, 1, sizeof(b) - 1, f);
    fclose(f);
    b[n] = 0;
    return (long long)strtoll(b, NULL, 10);
}

static long long tt_ver_bump(const char *dir, const char *leaf) {
    long long prev = tt_ver_read(dir, leaf);
    long long v = (long long)time(NULL);
    if (v <= prev) v = prev + 1;
    char p[768], s[32];
    tt_path(p, sizeof(p), dir, leaf);
    snprintf(s, sizeof(s), "%lld\n", v);
    if (write_file_text(p, s, strlen(s)) != 0) return 0;
    return v;
}

long long game_title_icon_ver(const char *tid) {
    char d[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) return 0;
    return tt_ver_read(d, "icon.ver");
}

long long game_title_bg_ver(const char *tid) {
    char d[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) return 0;
    return tt_ver_read(d, "bg.ver");
}

/* ── [CN-FORK] v0.5.2.132 名字源持久化（写游戏包里的 sce_sys/param.json）─────
 *
 * 真机验证：v0.5.2.118 只改了两层缓存 —— name.txt（面板层）+ app.db/appinfo.db
 * （主机索引层）。但 ShadowMount 一重扫就用自己的源重建注册，②那层全被冲掉，
 * 用户看到「改完名、重启又回去了」。
 *
 * FTP 只读侦察结论（192.168.1.214，_ftp_probe/evidence/recon/recon*.txt）：
 *   1. /user/app 全树 **0 个 NPXS.json、0 个 .sig** —— 这些是 AMPR 解包游戏
 *      （fakelib + sce_sys/keystone），没有签名校验链，改 param.json 零风险。
 *   2. 名字唯一源头是 sce_sys/param.json 的 localizedParameters.<lang>.titleName
 *      （一个游戏 1～10 个语言键，实测 Nioh 3 有 10 个）。
 *   3. SMP 自己的数据目录 /user/data/shadowmnt/ 里一个字的游戏名都没有
 *      （image_index.bin 2.4MB 检索 Onimusha / 鬼武者 / Way of the Sword 全 0 次）
 *      —— SMP 不缓存名字，每次现场从 param.json 读。所以**改源即终局**，
 *      不需要后台守护进程在重扫后重放。
 *   4. 分两类：
 *      folder  source 是 /mnt/ext1/<目录>/，sce_sys/param.json 是普通文件 → 直接写
 *              （已实测 /mnt/ext1、目标 sce_sys、挂载点三处 STOR/RETR/DELE 往返一致）。
 *      image   source 是单个 .ffpfsc（28GB～171GB），名字锁在镜像文件系统内部
 *              → 不可行。返回「不适用」而不是假成功。
 *
 * 用**定点替换**而不是 cJSON 整包重写：真机 param.json 是 CRLF + 2 空格缩进的美化
 * 格式（2.9KB），整包重写会把它压成单行 LF，几千字节的 diff 面全是为了改一个字符串。
 * titleName 在 param.json 的 schema 里**只**出现在 localizedParameters 的语言对象下，
 * 所以全局替换所有 titleName 对就是精确命中 —— 一次改完全部语言，用户不会遇到
 * 「英文改了日文没改」这种一半生效的怪现象。 */

#define TT_PARAM_MAX (512u * 1024u)  /* 实测 1～10KB，512KB 是防呆天花板 */

/* JSON 字符串值的结束位置：p 指向开引号**之后**，avail 是剩余字节数。
 * 处理 \" 与 \\ 转义。返回指向闭引号；不成对返回 NULL。 */
static const unsigned char *tt_json_str_end(const unsigned char *p, size_t avail) {
    size_t i = 0;
    while (i < avail) {
        unsigned char c = p[i];
        if (c == '\\' && i + 1 < avail) { i += 2; continue; }
        if (c == '"') return p + i;
        i++;
    }
    return NULL;
}

/* 把游戏名按 JSON 规则转义。set_name 已经剥光了 <0x20 与 0x7F 控制字符，
 * 所以只剩 " 和 \ 两种需要转义。 */
static int tt_json_escape_name(const char *str, char *out, size_t osz) {
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (k + 3 >= osz) return -1;
        if (*p == '"' || *p == '\\') out[k++] = '\\';
        out[k++] = (char)*p;
    }
    if (k >= osz) return -1;
    out[k] = 0;
    return (int)k;
}

/* 把 buf 里所有  "titleName" : "<value>"  的 value 换成 newname。
 *
 * 逐字节拷贝 + 命中就换值：缩进、CRLF、其他字段的字节**一字节不改**。
 *
 * 契约（测试 test/param_rename_test.c 逐条钉死）：
 *   hits > 0  → *buf 已被换成新分配（旧的已 free），*buflen 更新；
 *   hits == 0 → **调用方的缓冲区原样保留**，*buf 与 *buflen 都不动。
 * 第二条是硬要求：没命中时那个拷贝是白费的，而且调用方传的可能是**栈数组**
 * （见 tt_json_str_end 那条边界用例），free 它会直接崩。宁可白拷贝一次，
 * 也不能让「没找到」变成「缓冲区归属不明」。
 *
 * 返回替换处数；0 = 文件里没有 titleName（不是游戏 param.json）；-1 = 内部错误。 */
static int tt_param_rename(unsigned char **buf, size_t *buflen,
                           const char *newname, char *esc, size_t escsz) {
    if (tt_json_escape_name(newname, esc, escsz) < 0) return -1;
    size_t nl = strlen(esc);
    size_t keylen = 11;                        /* "\"titleName\"" 含两端引号 */
    const unsigned char *end = *buf + *buflen;

    size_t ocap = *buflen + nl + 8192;
    unsigned char *out = (unsigned char *)malloc(ocap);
    if (!out) return -1;
    size_t oc = 0;
    const unsigned char *p = *buf;
    int hits = 0;

    while (p < end) {
        if ((size_t)(end - p) >= keylen && memcmp(p, "\"titleName\"", keylen) == 0) {
            const unsigned char *q = p + keylen;
            while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
            if (q < end && *q == ':') {
                q++;
                while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
                if (q < end && *q == '"') {
                    const unsigned char *ve =
                        tt_json_str_end(q + 1, (size_t)(end - (q + 1)));
                    if (ve) {
                        size_t need = oc + (size_t)(q - p) + nl + 3;
                        if (need > ocap) {
                            ocap = need;
                            unsigned char *nb = (unsigned char *)realloc(out, ocap);
                            if (!nb) { free(out); return -1; }
                            out = nb;
                        }
                        memcpy(out + oc, p, (size_t)(q - p));   /* key + 空白 + : + 空白 */
                        oc += (size_t)(q - p);
                        out[oc++] = '"';
                        memcpy(out + oc, esc, nl);
                        oc += nl;
                        out[oc++] = '"';
                        p = ve + 1;
                        hits++;
                        continue;
                    }
                }
            }
        }
        if (oc + 1 > ocap) {
            ocap *= 2;
            unsigned char *nb = (unsigned char *)realloc(out, ocap);
            if (!nb) { free(out); return -1; }
            out = nb;
        }
        out[oc++] = *p++;
    }

    if (oc + 1 > ocap) {
        ocap = oc + 1;
        unsigned char *nb = (unsigned char *)realloc(out, ocap);
        if (!nb) { free(out); return -1; }
        out = nb;
    }
    out[oc] = 0;
    if (hits > 0) {
        free(*buf);
        *buf = out;
        *buflen = oc;
    } else {
        free(out);            /* 没命中：拷贝扔掉，调用方的缓冲区原样保留 */
    }
    return hits;
}

/* 原子写：先写同目录临时文件再 rename，避免写一半断电留下截断的 param.json。
 * exFAT 上 rename 同目录是本地操作；万一 rename 失败就退化成直接写目标。 */
static int tt_write_atomic(const char *path, const unsigned char *data, size_t len) {
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (write_file_text(tmp, (const char *)data, len) == 0) {
        if (rename(tmp, path) == 0) return 0;
        unlink(tmp);
    }
    return write_file_text(path, (const char *)data, len);
}

/* 改一个 param.json：读入 → 备份原厂件 → 定点替换所有 titleName → 原子写回。
 *
 * 备份必须在改写 buf **之前**做：先改内存再备份会把「我们自己改过的」当成原厂件
 * 存进去，用户就再没有回退点了。备份只在 <GAME_TITLE_DIR>/<TID>/orig/param.json
 * 还不存在时写，跟 icon/bg 那套 tt_backup_to_orig 的判据一致。
 *
 * 成功返回替换处数（>0）；-1 失败，msg 里给人话。 */
static int tt_rename_in_param(const char *tid, const char *param_path,
                              const char *name, char *msg, size_t msgsz) {
    unsigned char *buf = NULL;
    size_t buflen = 0;

    if (read_file_text(param_path, (char **)&buf, &buflen) != 0) {
        snprintf(msg, msgsz, "读不到源文件：%s", param_path);
        return -1;
    }
    if (buflen >= TT_PARAM_MAX) {
        snprintf(msg, msgsz, "源文件过大（%zu 字节），已跳过", buflen);
        free(buf);
        return -1;
    }

    /* ① 先备份原厂件（未改写）。备份失败不致命 —— 参数写成功比留回退点更重要，
     * 只记日志，跟 tile_db 那条链的 best-effort 口径一致。 */
    char dir[768], bdir[860], bpath[1000];
    int have_backup = 0;
    if (tt_dir(tid, dir, sizeof(dir)) == 0) {
        snprintf(bdir, sizeof(bdir), "%s/orig", dir);
        snprintf(bpath, sizeof(bpath), "%s/param.json", bdir);
        if (tt_exists(bpath)) {
            have_backup = 1;
        } else if (ensure_dir_recursive(bdir) == 0) {
            if (write_file_text(bpath, (const char *)buf, buflen) == 0) {
                have_backup = 1;
            } else {
                pldmgr_log("[PLDMGR] title: param.json 备份失败 %s (%s)\n",
                           bpath, strerror(errno));
            }
        }
    }

    /* ② 定点替换所有 titleName（不动缩进与 CRLF） */
    char esc[GAME_TITLE_MAX_NAME * 3 + 16];
    int hits = tt_param_rename(&buf, &buflen, name, esc, sizeof(esc));
    if (hits < 0) {
        snprintf(msg, msgsz, "改写源文件时内存不足");
        free(buf);
        return -1;
    }
    if (hits == 0) {
        snprintf(msg, msgsz, "源文件里没有可改的名字字段");
        free(buf);
        return -1;
    }

    /* ③ 原子写回 */
    if (tt_write_atomic(param_path, buf, buflen) != 0) {
        snprintf(msg, msgsz, "写源文件失败：%s", strerror(errno));
        free(buf);
        return -1;
    }
    free(buf);
    snprintf(msg, msgsz, "已写入 %s（%d 个语言）%s", param_path, hits,
             have_backup ? "" : "，备份未建成");
    return hits;
}



/* ── 改名 ──────────────────────────────────────────────────────────────────
 * name.txt 里的文件内容**不做任何转义**：就是一行 UTF-8 原文。转义发生在进 JSON 的
 * 那一刻（game_mgr.c 的 pldmgr_json_escape），存转义串会让「名字里本来就有反斜杠」
 * 的用户越改越怪 —— 这类 bug 在改名功能上是经典款。 */
int game_title_set_name(const char *tid, const char *name,
                        char *msg, size_t msgsz,
                        int *persisted, char *pnote, size_t pnotesz) {
    if (persisted) *persisted = 0;
    if (pnote && pnotesz) pnote[0] = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!tt_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "标题 ID 不合法");
        return -1;
    }
    if (!name) {
        if (msg) snprintf(msg, msgsz, "名称不能为空");
        return -1;
    }
    /* 先去掉首尾空白与控制字符；中间的换行/制表同样剥掉 —— 磁贴上那行字
     * 出现换行符会把布局顶飞，而 JSON 里它又得被转义成 \n，两头都不好。 */
    char clean[GAME_TITLE_MAX_NAME * 4 + 1];
    size_t k = 0;
    for (size_t i = 0; name[i] && k < sizeof(clean) - 1; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F) continue;
        clean[k++] = (char)c;
    }
    clean[k] = 0;
    /* 去尾空白 */
    while (k > 0 && (clean[k - 1] == ' ' || clean[k - 1] == '\t')) clean[--k] = 0;
    /* 去头空白 */
    char *s = clean;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == 0) {
        if (msg) snprintf(msg, msgsz, "名称不能为空");
        return -1;
    }
    /* 按 UTF-8 **字符数**截断，不能按字节 —— 按字节切会把最后一个汉字切半个，
     * 存进去就是一段非法 UTF-8，之后所有经 JSON 转义的路径都会出怪字符。 */
    size_t chars = 0, end = 0;
    for (size_t i = 0; s[i]; ) {
        if (chars >= GAME_TITLE_MAX_NAME) break;
        unsigned char c = (unsigned char)s[i];
        size_t adv = (c < 0x80) ? 1 : (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        for (size_t j = 0; j < adv && s[i + j]; j++) end = i + j;
        if (s[end]) end++;
        i += adv;
        chars++;
    }
    s[end] = 0;

    char d[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) {
        if (msg) snprintf(msg, msgsz, "标题 ID 不合法");
        return -1;
    }
    if (ensure_dir_recursive(d) != 0) {
        if (msg) snprintf(msg, msgsz, "无法创建目录（%s）", strerror(errno));
        return -1;
    }
    char p[768];
    tt_path(p, sizeof(p), d, "name.txt");
    if (write_file_text(p, s, strlen(s)) != 0) {
        if (msg) snprintf(msg, msgsz, "写入失败（%s）", strerror(errno));
        return -1;
    }
    pldmgr_log("[PLDMGR] title: %s renamed -> %s\n", tid, s);
    /* 改名/换图**必须**让 /game_list 的 60s 快照立刻作废：列表里的 name 与
     * icon/bg 版本号都是从盘上现读的，进了缓存就等于把用户刚做的操作吞掉一分钟。
     * 五个写入口都调这一句 —— 放在路由里会漏（将来从别处调就没人记得），
     * 放在读口（override_name / icon_ver）里会自锁（读它自己就是缓存的来源）。 */
    game_mgr_invalidate();

    /* [CN-FORK] v0.5.2.118 上面那份 name.txt 只改得了**面板**：主屏磁贴的名字根本
     * 不是文件，而是 app.db 的平铺名字列 + appinfo.db 的 TITLE。不补这一句，用户看到
     * 的就是「网页改了、PS5 上没改」。best-effort —— 写不进主机库只记日志，面板那层
     * 照常算成功；为什么只动平铺列、不动 JSON 镜像，见 src/tile_db.c 头注释。 */
    char hmsg[256];
    if (tile_db_set_name(tid, s, hmsg, sizeof(hmsg)) != 0)
        pldmgr_log("[PLDMGR] title: %s 主机端改名未生效：%s\n", tid,
                   hmsg[0] ? hmsg : "见上一条日志");

    /* [CN-FORK] v0.5.2.132 ③ 名字源。上面 name.txt + app.db/appinfo.db 都是**缓存**，
     * ShadowMount 一重扫就用自己的源重建注册，两层全被冲掉 —— 用户看到「改完名、
     * 重启又回去了」。只有把源 param.json 的 titleName 改了才真正不回退，
     * 侦察依据见本文件 tt_param_rename 那节的头注释。
     *
     * best-effort：源写失败**不把整个改名打成失败**。面板层已经生效，用户不该因为
     * 源是镜像或只读就白改一次名；如实把状态回给前端，由它决定要不要提示。 */
    char kind[16], srcpath[600];
    int persisted_now = 0;
    char wmsg[256];
    wmsg[0] = 0;

    if (game_mgr_source_paths(tid, kind, sizeof(kind), srcpath, sizeof(srcpath)) == 0) {
        snprintf(wmsg, sizeof(wmsg), "这个游戏不在游戏列表里");
    } else if (strcmp(kind, "image") == 0) {
        snprintf(wmsg, sizeof(wmsg), "镜像游戏：名字锁在镜像文件内部，无法持久修改");
    } else if (strcmp(kind, "pkg") == 0) {
        snprintf(wmsg, sizeof(wmsg), "系统内置安装：没有可写的游戏源");
    } else {
        char sp[1000];
        snprintf(sp, sizeof(sp), "%s/sce_sys/param.json", srcpath);
        int h = tt_rename_in_param(tid, sp, s, wmsg, sizeof(wmsg));
        if (h > 0) {
            persisted_now = 1;
            /* 顺带同步挂载点那份副本。它是 SMP 的**产物**不是输入，不改也不影响持久性，
             * 但同一份名字两个地方不一致纯属隐患，而它只多写一次 3KB。
             * 路径相同（硬链/同目录）就跳过，免得对自己重复改写。 */
            char m[1000], m2[256];
            snprintf(m, sizeof(m), "%s/%s/sce_sys/param.json", GAME_TITLE_APP_PREFIX, tid);
            if (strcmp(m, sp) != 0 && tt_exists(m)) {
                int h2 = tt_rename_in_param(tid, m, s, m2, sizeof(m2));
                if (h2 <= 0)
                    pldmgr_log("[PLDMGR] title: %s 挂载点副本未更新：%s\n", tid, m2);
            }
        } else {
            snprintf(wmsg, sizeof(wmsg), "源文件未更新：%s", wmsg);
        }
    }

    if (persisted) *persisted = persisted_now;
    if (pnote && pnotesz) snprintf(pnote, pnotesz, "%s", wmsg);
    pldmgr_log("[PLDMGR] title: %s 源持久化 %s：%s\n", tid,
               persisted_now ? "成功" : "未生效", wmsg);
    return 0;
}

int game_title_get_name(const char *tid, char *out, size_t osz) {
    if (!out || !osz) return 0;
    out[0] = 0;
    char d[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) return 0;
    char p[768];
    tt_path(p, sizeof(p), d, "name.txt");
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, osz - 1, f);
    fclose(f);
    out[n] = 0;
    /* 去掉可能的尾换行（老版本/手写文件） */
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) out[--n] = 0;
    return n > 0;
}

const char *game_title_override_name(const char *tid, const char *orig,
                                     char *out, size_t osz) {
    if (!out || !osz) return orig;
    if (game_title_get_name(tid, out, osz)) return out;
    snprintf(out, osz, "%s", orig ? orig : "");
    return out;
}

void game_title_detail_flags(const char *tid, int *icon_custom, int *bg_custom) {
    if (icon_custom) *icon_custom = game_title_icon_ver(tid) > 0;
    if (bg_custom) *bg_custom = game_title_bg_ver(tid) > 0;
}

/* ── 三个 GET 的落点 ─────────────────────────────────────────────────────── */
int game_title_icon_path(const char *tid, char *out, size_t osz) {
    char d[768], p[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) return -1;
    tt_path(p, sizeof(p), d, "icon.png");
    if (!tt_exists(p)) return -1;
    snprintf(out, osz, "%s", p);
    return 0;
}

int game_title_bg_png_path(const char *tid, char *out, size_t osz) {
    char d[768], p[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) return -1;
    tt_path(p, sizeof(p), d, "bg.png");
    if (!tt_exists(p)) return -1;
    snprintf(out, osz, "%s", p);
    return 0;
}

int game_title_bg_dds_path(const char *tid, char *out, size_t osz) {
    char d[768], p[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) return -1;
    tt_path(p, sizeof(p), d, "bg.dds");
    if (!tt_exists(p)) return -1;
    snprintf(out, osz, "%s", p);
    return 0;
}

/* ── 内存小工具（stb 的 write 回调是 push 式） ──────────────────────────── */
typedef struct { unsigned char *p; size_t len, cap; int oom; } tt_mem_t;

static void tt_mem_cb(void *ctx, void *data, int size) {
    tt_mem_t *m = (tt_mem_t *)ctx;
    if (m->oom || size <= 0) return;
    if (m->len + (size_t)size + 1 > m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 65536;
        while (nc < m->len + (size_t)size + 1) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(m->p, nc);
        if (!np) { m->oom = 1; return; }
        m->p = np;
        m->cap = nc;
    }
    memcpy(m->p + m->len, data, (size_t)size);
    m->len += (size_t)size;
    m->p[m->len] = 0;
}

static int tt_is_png(const unsigned char *b, size_t n) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    return n >= 8 && memcmp(b, sig, 8) == 0;
}

/* 把收到的字节变成「确定是 PNG」的字节串。
 * 前端固定传 canvas.toBlob('image/png')，正常路径是直接 memcpy（零额外成本）；
 * 但文件选择器允许选 JPEG，那时候必须转 —— 否则 GET 会回 image/png 的头
 * 却给一段 JPEG，浏览器 <img> 画不出来，而接口还回 {ok:true}。 */
static int tt_as_png(const unsigned char *in, size_t len,
                     unsigned char **out, size_t *out_len, char *msg, size_t msgsz) {
    *out = NULL;
    *out_len = 0;
    if (tt_is_png(in, len)) {
        unsigned char *b = (unsigned char *)malloc(len);
        if (!b) {
            if (msg) snprintf(msg, msgsz, "内存不足");
            return -1;
        }
        memcpy(b, in, len);
        *out = b;
        *out_len = len;
        return 0;
    }
    int w = 0, h = 0, c = 0;
    unsigned char *px = stbi_load_from_memory(in, (int)len, &w, &h, &c, 4);
    if (!px) {
        if (msg) snprintf(msg, msgsz, "图片解码失败（只认 PNG / JPEG）");
        return -1;
    }
    tt_mem_t m;
    memset(&m, 0, sizeof(m));
    int ok = stbi_write_png_to_func(tt_mem_cb, &m, w, h, 4, px, w * 4);
    stbi_image_free(px);
    if (!ok || m.oom || !m.p) {
        free(m.p);
        if (msg) snprintf(msg, msgsz, "重新编码 PNG 失败");
        return -1;
    }
    *out = m.p;
    *out_len = m.len;
    return 0;
}

/* ── 落到主机上 ────────────────────────────────────────────────────────────
 * 只写**已存在的 sce_sys 目录**（见 game_mgr_sce_sys_dir）。
 * 写之前把原图备份到 <dir>/orig/，*_reset 才有东西可还原；
 * 备份只在 orig/ 里还没有那一份时做 —— 否则第二次应用会把「我们自己写的那张」
 * 当成原图备份下来，还原就成了空操作。
 * 全程 best-effort：写不进去只记日志。图仍在我们自己那份里，面板照常显示。 */
/* 读整个文件到堆上。两个都要照看：
 *
 * ① **别在栈上开大数组**。这里原来是 `unsigned char chunk[65536]` —— 64KB，
 *    是全仓最大的栈数组（第二大的才 16KB），而且**只有改磁贴图标 / 背景这条路会走到**
 *    （tt_apply 备份主机原图、tt_restore 还原），改名那条一次都不碰。MHD 是
 *    MHD_USE_THREAD_PER_CONNECTION，每个连接一条线程，栈是 pthread 默认给的，
 *    这一帧叠加 http_on_request(4KB out) → gr_dispatch_post → set_bg → tt_apply
 *    后就顶在边上 —— 真机反馈正是「改图标和背景会死机」，而改名不会。
 *    收到 4KB：一帧小一个数量级，读取次数多 16 倍但每次都是纯 memcpy，无感。
 * ② **按几何倍数扩容**。原来 `realloc(b, cap + n)` 每次只多要一个 chunk，
 *    realloc 得把已读部分整段再拷一遍 → O(n²)：50MB 的 pic0 光搬就要几十 GB 的 memcpy。
 *    换成翻倍后总拷贝量恒定在 2×文件大小。
 * 读出来的就是原文件字节，不做 NUL 结尾（调用方一律用返回的长度）。 */
static unsigned char *tt_read_all(const char *path, size_t *out_len) {
    *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *b = NULL;
    size_t cap = 0, n = 0;
    unsigned char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (n + got > cap) {
            size_t nc = cap ? cap * 2 : 65536;
            while (nc < n + got) nc *= 2;
            unsigned char *nb = (unsigned char *)realloc(b, nc);
            if (!nb) { free(b); fclose(f); return NULL; }
            b = nb;
            cap = nc;
        }
        memcpy(b + n, chunk, got);
        n += got;
    }
    fclose(f);
    if (!b) return NULL;
    *out_len = n;
    return b;
}

static void tt_apply(const char *dir, const char *target,
                     const unsigned char *buf, size_t len) {
    char parent[768];
    snprintf(parent, sizeof(parent), "%s", target);
    char *sl = strrchr(parent, '/');
    if (!sl) return;
    *sl = 0;
    if (!tt_is_dir(parent)) {
        pldmgr_log("[PLDMGR] title: skip apply %s (sce_sys 不在)\n", target);
        return;
    }

    const char *base = strrchr(target, '/');
    base = base ? base + 1 : target;
    char origdir[800], orig[900];
    snprintf(origdir, sizeof(origdir), "%s/orig", dir);
    snprintf(orig, sizeof(orig), "%s/%s", origdir, base);
    if (ensure_dir_recursive(origdir) != 0) {
        pldmgr_log("[PLDMGR] title: backup dir failed for %s (%s)\n", target, strerror(errno));
        return;
    }
    if (!tt_exists(orig) && tt_exists(target)) {
        size_t n = 0;
        unsigned char *ob = tt_read_all(target, &n);
        if (ob) {
            if (write_file_text(orig, (const char *)ob, n) != 0)
                pldmgr_log("[PLDMGR] title: backup failed %s (%s)\n", orig, strerror(errno));
            free(ob);
        }
    }

    if (write_file_text(target, (const char *)buf, len) != 0) {
        /* 只读 PFS / dump 在只读介质 / 权限不够 —— 都是可预期的，别当错误上报 */
        pldmgr_log("[PLDMGR] title: apply %s failed (%s)\n", target, strerror(errno));
        return;
    }
    pldmgr_log("[PLDMGR] title: applied %s (%zu bytes)\n", target, len);
}

static void tt_restore(const char *dir, const char *target) {
    const char *base = strrchr(target, '/');
    base = base ? base + 1 : target;
    char orig[900];
    snprintf(orig, sizeof(orig), "%s/orig/%s", dir, base);
    if (!tt_exists(orig)) {
        /* 没备份过 = 我们从没写进去过，那也不该去动主机上那份（它是系统原图）。 */
        return;
    }
    size_t n = 0;
    unsigned char *b = tt_read_all(orig, &n);
    if (b) {
        if (write_file_text(target, (const char *)b, n) != 0)
            pldmgr_log("[PLDMGR] title: restore %s failed (%s)\n", target, strerror(errno));
        free(b);
    }
    unlink(orig);
}

/* ── 缩放 + DXT1 ───────────────────────────────────────────────────────────
 * 双线性缩到目标尺寸。**同尺寸直接返回 NULL**：正常路径（前端已经缩好 1920×1080）
 * 走的是零重采样分支，像素与入参逐字节一致。 */
static unsigned char *tt_resize(const unsigned char *src, int sw, int sh, int dw, int dh) {
    if (sw == dw && sh == dh) return NULL;
    if (sw <= 0 || sh <= 0) return NULL;
    unsigned char *dst = (unsigned char *)malloc((size_t)dw * dh * 4);
    if (!dst) return NULL;
    for (int y = 0; y < dh; y++) {
        float fy = ((float)y + 0.5f) * (float)sh / (float)dh - 0.5f;
        int y0 = (int)fy;
        if (y0 < 0) y0 = 0;
        int y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        float wy = fy - (float)y0;
        if (wy < 0) wy = 0;
        if (wy > 1) wy = 1;
        for (int x = 0; x < dw; x++) {
            float fx = ((float)x + 0.5f) * (float)sw / (float)dw - 0.5f;
            int x0 = (int)fx;
            if (x0 < 0) x0 = 0;
            int x1 = x0 + 1;
            if (x1 >= sw) x1 = sw - 1;
            float wx = fx - (float)x0;
            if (wx < 0) wx = 0;
            if (wx > 1) wx = 1;
            const unsigned char *p00 = src + ((size_t)y0 * sw + x0) * 4;
            const unsigned char *p01 = src + ((size_t)y0 * sw + x1) * 4;
            const unsigned char *p10 = src + ((size_t)y1 * sw + x0) * 4;
            const unsigned char *p11 = src + ((size_t)y1 * sw + x1) * 4;
            unsigned char *o = dst + ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 4; c++) {
                float t = (1 - wx) * (1 - wy) * p00[c] + wx * (1 - wy) * p01[c] +
                          (1 - wx) * wy * p10[c] + wx * wy * p11[c];
                int v = (int)(t + 0.5f);
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                o[c] = (unsigned char)v;
            }
        }
    }
    return dst;
}

static void tt_put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v & 0xFF); p[1] = (unsigned char)(v >> 8); }
static void tt_put32(unsigned char *p, unsigned v) { tt_put16(p, v & 0xFFFF); tt_put16(p + 2, v >> 16); }

static unsigned tt_rgb565(int r, int g, int b) {
    return (unsigned)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}
static void tt_565rgb(unsigned c, int *r, int *g, int *b) {
    *r = (int)((c >> 11) & 0x1F) << 3;
    *g = (int)((c >> 5) & 0x3F) << 2;
    *b = (int)(c & 0x1F) << 3;
}

/* 一个 4×4 块 → 8 字节 BC1/DXT1。
 * 与 mock-server.js 的 compressDxt1Block 逐行同构 —— 那份的规格锚点是
 * console 已验证的 assets/pic0.dds（make_pic0.py 产出），所以这里不做「优化」：
 * 换成最小二乘之类更好的编码器，规格没问题但字节全变，实机出问题时就没法对版了。 */
static void tt_dxt1_block(const unsigned char *rgba, int w, int h,
                          int bx, int by, unsigned char *out) {
    int mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
    for (int y = 0; y < 4; y++) {
        int py = by * 4 + y;
        if (py >= h) py = h - 1;
        for (int x = 0; x < 4; x++) {
            int px = bx * 4 + x;
            if (px >= w) px = w - 1;
            const unsigned char *q = rgba + ((size_t)py * w + px) * 4;
            for (int c = 0; c < 3; c++) {
                if (q[c] < mn[c]) mn[c] = q[c];
                if (q[c] > mx[c]) mx[c] = q[c];
            }
        }
    }
    unsigned c0 = tt_rgb565(mx[0], mx[1], mx[2]);
    unsigned c1 = tt_rgb565(mn[0], mn[1], mn[2]);
    if (c0 < c1) { unsigned t = c0; c0 = c1; c1 = t; }
    else if (c0 == c1 && c0 < 0xFFFF) c0++; /* 保证 c0 > c1：DXT1 的判定就是拿这个
                                             * 比较选「四色不透明」还是「三色+透明」，
                                             * 相等会落到三色档、索引 3 变透明黑，
                                             * 图上会凭空出黑洞。 */

    int r0, g0, b0, r1, g1, b1;
    tt_565rgb(c0, &r0, &g0, &b0);
    tt_565rgb(c1, &r1, &g1, &b1);
    int pal[4][3];
    pal[0][0] = r0; pal[0][1] = g0; pal[0][2] = b0;
    pal[1][0] = r1; pal[1][1] = g1; pal[1][2] = b1;
    for (int c = 0; c < 3; c++) {
        pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
        pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
    }

    unsigned idx = 0;
    for (int y = 0; y < 4; y++) {
        int py = by * 4 + y;
        if (py >= h) py = h - 1;
        for (int x = 0; x < 4; x++) {
            int px = bx * 4 + x;
            if (px >= w) px = w - 1;
            const unsigned char *q = rgba + ((size_t)py * w + px) * 4;
            int best = 0, bestD = 1 << 30;
            for (int j = 0; j < 4; j++) {
                int dr = (int)q[0] - pal[j][0];
                int dg = (int)q[1] - pal[j][1];
                int db = (int)q[2] - pal[j][2];
                int d = dr * dr + dg * dg + db * db;
                if (d < bestD) { bestD = d; best = j; }
            }
            idx |= (unsigned)best << ((y * 4 + x) * 2);
        }
    }
    tt_put16(out, c0);
    tt_put16(out + 2, c1);
    tt_put32(out + 4, idx);
}

/* DDS 头 —— 与 make_pic0.py 的 build_bc1_header 逐字节一致：
 * pixel-format 块在 76、dwFlags 0x000A1007、dwLinearSize 写**整个**压缩体、
 * mipMapCount 1、bitCount 0、reserved1[0] 写 1（参考图写 1，照抄）。
 * 那张 pic0.dds 是控制台已验证能被读的参考，所以不做任何「顺手改进」。 */
static void tt_dds_header(unsigned char *head, int w, int h) {
    unsigned linear = (unsigned)(((w + 3) >> 2) * ((h + 3) >> 2) * 8);
    memset(head, 0, 128);
    memcpy(head, "DDS ", 4);
    tt_put32(head + 4, 124);
    tt_put32(head + 8, 0x000A1007u);
    tt_put32(head + 12, (unsigned)h);
    tt_put32(head + 16, (unsigned)w);
    tt_put32(head + 20, linear);
    tt_put32(head + 24, 1); /* mipMapCount */
    tt_put32(head + 28, 1); /* reserved1[0] */
    tt_put32(head + 76, 32); /* pf dwSize */
    tt_put32(head + 80, 4);  /* DDPF_FOURCC */
    memcpy(head + 84, "DXT1", 4);
    tt_put32(head + 108, 0x1000); /* DDSCAPS_TEXTURE */
}

/* RGBA → 1920×1080 DXT1 DDS（调用方保证缓冲是 TT_BG_W×TT_BG_H）。 */
static unsigned char *tt_encode_dds(const unsigned char *rgba, size_t *out_len) {
    int bw = (TT_BG_W + 3) >> 2, bh = (TT_BG_H + 3) >> 2;
    size_t total = 128 + (size_t)bw * bh * 8;
    unsigned char *out = (unsigned char *)malloc(total);
    if (!out) return NULL;
    tt_dds_header(out, TT_BG_W, TT_BG_H);
    unsigned char *p = out + 128;
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++, p += 8)
            tt_dxt1_block(rgba, TT_BG_W, TT_BG_H, bx, by, p);
    *out_len = total;
    return out;
}

/* ── 上传入口 ────────────────────────────────────────────────────────────── */
int game_title_set_icon(const char *tid, const unsigned char *png, size_t len,
                        long long *out_bytes, char *msg, size_t msgsz) {
    if (out_bytes) *out_bytes = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!tt_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "标题 ID 不合法");
        return -1;
    }
    if (!png || len == 0) {
        if (msg) snprintf(msg, msgsz, "图片为空");
        return -1;
    }
    if (len > GAME_TITLE_MAX_PNG) {
        if (msg) snprintf(msg, msgsz, "图片过大（上限 64MB）");
        return -1;
    }

    int w = 0, h = 0, c = 0;
    unsigned char *px = stbi_load_from_memory(png, (int)len, &w, &h, &c, 4);
    if (!px) {
        if (msg) snprintf(msg, msgsz, "图片解码失败（只认 PNG / JPEG）");
        return -1;
    }
    /* 1:1 由前端保证（选图时就缩到 1024×1024）。这里只查不做二次缩放 ——
     * 缩了用户挑的那张就和他看到的不一样了，而磁贴上具体要多大由 VSH 决定。 */
    if (w != h)
        pldmgr_log("[PLDMGR] title: %s icon 非 1:1 (%dx%d)，原样存\n", tid, w, h);
    stbi_image_free(px);

    unsigned char *store = NULL;
    size_t store_len = 0;
    if (tt_as_png(png, len, &store, &store_len, msg, msgsz) != 0) return -1;

    char d[768];
    if (tt_dir(tid, d, sizeof(d)) != 0 || ensure_dir_recursive(d) != 0) {
        free(store);
        if (msg) snprintf(msg, msgsz, "无法创建目录（%s）", strerror(errno));
        return -1;
    }
    char p[768];
    tt_path(p, sizeof(p), d, "icon.png");
    if (write_file_text(p, (const char *)store, store_len) != 0) {
        free(store);
        if (msg) snprintf(msg, msgsz, "写入失败（%s）", strerror(errno));
        return -1;
    }
    tt_ver_bump(d, "icon.ver");

    /* 落到主机上要的是「确定是 PNG」的那一份（非 PNG 入参已被 tt_as_png 转过），
     * 所以必须在 free(store) 之前做。 */
    char target[768];
    if (game_mgr_sce_sys_dir(tid, target, sizeof(target))) {
        char t[900];
        snprintf(t, sizeof(t), "%s/icon0.png", target);
        tt_apply(d, t, store, store_len);
    }
    free(store);
    store = NULL;
    if (out_bytes) *out_bytes = (long long)len;
    pldmgr_log("[PLDMGR] title: %s icon set (%zu bytes)\n", tid, len);
    game_mgr_invalidate(); /* 见 game_title_set_name 里那条注释：列表缓存要立刻作废 */
    return 0;
}

int game_title_set_bg(const char *tid, const unsigned char *png, size_t len,
                      long long *out_bytes, long long *out_dds,
                      char *msg, size_t msgsz) {
    if (out_bytes) *out_bytes = 0;
    if (out_dds) *out_dds = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!tt_tid_ok(tid)) {
        if (msg) snprintf(msg, msgsz, "标题 ID 不合法");
        return -1;
    }
    if (!png || len == 0) {
        if (msg) snprintf(msg, msgsz, "图片为空");
        return -1;
    }
    if (len > GAME_TITLE_MAX_PNG) {
        if (msg) snprintf(msg, msgsz, "图片过大（上限 64MB）");
        return -1;
    }

    int w = 0, h = 0, c = 0;
    unsigned char *px = stbi_load_from_memory(png, (int)len, &w, &h, &c, 4);
    if (!px) {
        if (msg) snprintf(msg, msgsz, "图片解码失败（只认 PNG / JPEG）");
        return -1;
    }

    unsigned char *resized = tt_resize(px, w, h, TT_BG_W, TT_BG_H);
    if (!resized) {
        if (w == TT_BG_W && h == TT_BG_H) resized = px; /* 同尺寸：不重采样，直接用 */
        else { stbi_image_free(px); if (msg) snprintf(msg, msgsz, "内存不足"); return -1; }
    }

    size_t dds_len = 0;
    unsigned char *dds = tt_encode_dds(resized, &dds_len);
    /* 装到主机上的 pic0.png：规格就是 1920×1080（make_pic0.py 的文档口径），
     * 所以入参不是这个尺寸时不能原样写，要从缩放后的缓冲重编一张。 */
    unsigned char *pic0 = NULL;
    size_t pic0_len = 0;
    if (w == TT_BG_W && h == TT_BG_H && tt_is_png(png, len)) {
        pic0 = (unsigned char *)malloc(len);
        if (pic0) { memcpy(pic0, png, len); pic0_len = len; }
    }
    if (!pic0) {
        tt_mem_t m;
        memset(&m, 0, sizeof(m));
        if (stbi_write_png_to_func(tt_mem_cb, &m, TT_BG_W, TT_BG_H, 4, resized, TT_BG_W * 4) && !m.oom)
            { pic0 = m.p; pic0_len = m.len; }
        else free(m.p);
    }

    if (resized != px) free(resized);
    stbi_image_free(px);

    if (!dds || !pic0) {
        free(dds);
        free(pic0);
        if (msg) snprintf(msg, msgsz, "内存不足");
        return -1;
    }

    unsigned char *store = NULL;
    size_t store_len = 0;
    if (tt_as_png(png, len, &store, &store_len, msg, msgsz) != 0) {
        free(dds); free(pic0);
        return -1;
    }

    char d[768];
    if (tt_dir(tid, d, sizeof(d)) != 0 || ensure_dir_recursive(d) != 0) {
        free(dds); free(pic0); free(store);
        if (msg) snprintf(msg, msgsz, "无法创建目录（%s）", strerror(errno));
        return -1;
    }
    char p[768];
    tt_path(p, sizeof(p), d, "bg.png");
    int wr = write_file_text(p, (const char *)store, store_len);
    if (wr == 0) { tt_path(p, sizeof(p), d, "bg.dds"); wr = write_file_text(p, (const char *)dds, dds_len); }
    free(store);
    if (wr != 0) {
        free(dds); free(pic0);
        if (msg) snprintf(msg, msgsz, "写入失败（%s）", strerror(errno));
        return -1;
    }
    tt_ver_bump(d, "bg.ver");

    char sdir[768];
    if (game_mgr_sce_sys_dir(tid, sdir, sizeof(sdir))) {
        char t[900];
        snprintf(t, sizeof(t), "%s/pic0.png", sdir);
        tt_apply(d, t, pic0, pic0_len);
        snprintf(t, sizeof(t), "%s/pic0.dds", sdir);
        tt_apply(d, t, dds, dds_len);
        /* [CN-FORK] v0.5.2.118 光写 pic0.dds 主屏**不画**：Shell 只看 app.db 的
         * pic0Info 一列 —— 真机消融结论是「手工装的 app 光写文件、甚至重启都不会
         * 自己补上这一列，必须自己写；写完主屏实时重绘，不用重启」。
         * 这正是「背景在机器端没生效」的另一半。
         * 必须排在 tt_apply 之后：注册进去的 ?ts= 得是这份文件刚写完的 mtime。 */
        char hmsg[256];
        if (tile_db_set_bg(tid, t, hmsg, sizeof(hmsg)) != 0)
            pldmgr_log("[PLDMGR] title: %s 背景没登记进 app.db：%s\n", tid,
                       hmsg[0] ? hmsg : "见上一条日志");
    } else {
        pldmgr_log("[PLDMGR] title: %s 找不到 sce_sys，背景只存在面板里\n", tid);
    }
    free(dds);
    free(pic0);

    if (out_bytes) *out_bytes = (long long)len;
    if (out_dds) *out_dds = (long long)dds_len;
    pldmgr_log("[PLDMGR] title: %s bg set (%zu bytes -> dds %zu)\n", tid, len, dds_len);
    game_mgr_invalidate(); /* 见 game_title_set_name 里那条注释：列表缓存要立刻作废 */
    return 0;
}

/* ── 复位 ────────────────────────────────────────────────────────────────── */
/* 还原主机上那（几）张：先备份还原，再删我们自己这一份。
 * 顺序要紧 —— 先删本地的，game_mgr_sce_sys_dir 那步不受影响，但万一将来
 * sce_sys 的定位要看我们自己的标记，反过来写就会把还原漏掉。 */
static void tt_restore_host(const char *tid, const char *d,
                            const char *leaf1, const char *leaf2) {
    char target[768], p[900];
    if (!game_mgr_sce_sys_dir(tid, target, sizeof(target))) return;
    snprintf(p, sizeof(p), "%s/%s", target, leaf1);
    tt_restore(d, p);
    if (leaf2) {
        snprintf(p, sizeof(p), "%s/%s", target, leaf2);
        tt_restore(d, p);
    }
}

int game_title_reset_icon(const char *tid) {
    char d[768], p[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) return -1;
    tt_restore_host(tid, d, "icon0.png", NULL);
    tt_path(p, sizeof(p), d, "icon.png");
    unlink(p);
    tt_path(p, sizeof(p), d, "icon.ver");
    unlink(p);
    pldmgr_log("[PLDMGR] title: %s icon reset\n", tid);
    game_mgr_invalidate(); /* 版本号从 1 回到 0，列表必须立刻看到，否则还会去取旧图 */
    return 0;
}

int game_title_reset_bg(const char *tid) {
    char d[768], p[768];
    if (tt_dir(tid, d, sizeof(d)) != 0) return -1;
    tt_restore_host(tid, d, "pic0.png", "pic0.dds");
    tt_path(p, sizeof(p), d, "bg.png");
    unlink(p);
    tt_path(p, sizeof(p), d, "bg.dds");
    unlink(p);
    tt_path(p, sizeof(p), d, "bg.ver");
    unlink(p);
    pldmgr_log("[PLDMGR] title: %s bg reset\n", tid);
    game_mgr_invalidate(); /* 见上：版本号回到 0，列表不能再回 304 式的旧图 URL */
    return 0;
}
