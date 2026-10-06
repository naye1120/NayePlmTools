/* [CN-FORK] v0.5.2.115 test/media_scan_test.c — src/game_media.c 的主机回归测试。
 *
 * 夹具由 test/make_media_fixture.py 现造（一个列序照抄真机的 av_content.db + 它引用的
 * 真实文件），五个路径宏用 -D 指到 /tmp/pldmgr_media_test —— 与 -DSAFETY_DIR 同一个手法。
 *
 * 覆盖：
 *   ① 列表：条数 / kind / 分隔符 / JSON 能被 Python 解析 / tid 过滤 / 非法 tid
 *   ② 定位：命中、未命中、路径穿越一律 -1
 *   ③ 原图：fd + 大小 + mime，路径穿越 -1，文件缺失 -2
 *   ④ 缩略图：JPEG 图标解码 + 缩到 200x112；PNG 兜底；HDR .jxr 与 mp4 一律 -2
 *   ⑤ 打包：子集 2 条 / 全量 5 条，产出交 Python zipfile 验 CRC 与内容
 *   ⑥ 删除：tid 不符 -1；成功后原图+副文件消失、库文件本身分毫未动
 *
 * 不断言的部分（真机未验证，见 game_media.h §7）：录屏目录层级、duration 单位、
 * 删除后系统媒体库是否出现幽灵条目 —— 那三件事主机上测不出来。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

#include "game_media.h"
#include "json_helpers.h"
#include "stb_image.h"
#include "stb_image_write.h"

/* log_server.c 的头要 microhttpd.h，主机上没有 —— 这里给个等价实现。
 * 默认静音（72 条路径日志会把断言冲掉），要看就 export MEDIA_TEST_VERBOSE=1。 */
void pldmgr_log(const char *fmt, ...) {
    if (!getenv("MEDIA_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* [CN-FORK] v0.5.2.121 game_media.c 的「游戏名称-截图时间」命名要问
 * game_title_get_name。真身在 src/game_title.c，而那个文件会拖进
 * game_mgr.c（sce_sys 目录）+ tile_db.c + sqlite3（9MB 合并源码）——
 * 对「命名用的是不是游戏名」这件事没有信息量，所以照 pldmgr_log 的老办法给桩，
 * 顺便把名字钉成可断言的值。
 *
 * ⚠️ 注意哪条路会调它：只有**带 tid 过滤**的扫描会（md_scan_locked 里
 * `if (tid && md_tid_ok(tid))`）。game_media_lookup(id) 传的是 tid=NULL，
 * 所以那条路的 name 仍是文件名 —— 下面两组断言刻意分开，正好把这条性质钉住。 */
int game_title_get_name(const char *tid, char *out, size_t osz) {
    if (!out || !osz) return 0;
    out[0] = 0;
    if (!tid) return 0;
    if (strcmp(tid, "PPSA99008") == 0) { snprintf(out, osz, "宇宙机器人"); return 1; }
    if (strcmp(tid, "PPSA00001") == 0) { snprintf(out, osz, "另一个游戏"); return 1; }
    return 0;
}

#define ROOT "/tmp/pldmgr_media_test"

#define C_PNG1 "/av_contents/photo/NPXS40087/PPSA99008/18c/20261005_165454_00474347.png"
#define C_JXR  "/av_contents/photo/NPXS40087/PPSA99008/18c/20261005_170000_00474348.jxr"
#define C_PNG2 "/av_contents/photo/NPXS40087/PPSA99008/18c/20261005_180000_00474349.png"
#define C_VID1 "/av_contents/video/NPXS40087/PPSA99008/18c/20261006_101112_001.mp4"
#define C_VID2 "/av_contents/video/NPXS40087/PPSA99008/18c/20261006_102233_002.mp4"
#define C_OTH  "/av_contents/photo/NPXS40087/PPSA00001/9a1/20261004_090000_00000001.png"
/* 录屏 + .jxr 封面帧（真机那条路，见 make_media_fixture.py 的 JXR_TID 段） */
#define C_VIDJ "/av_contents/video/NPXS40087/PPSA99009/18c/20261007_120000_003.mp4"

#define ICON_REL "/av_contents/thumbnails/photo/NPXS40087/PPSA99008/18c/20261005_165454_00474347.png.jpeg"

static int g_checks, g_fails;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_fails++;                                                       \
            printf("  FAIL L%d: ", __LINE__);                                \
            printf(__VA_ARGS__);                                             \
            putchar('\n');                                                   \
        }                                                                    \
    } while (0)

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* 从列表 JSON 里抠出第一个 "field":"value" 的值。
 * 只够本测试用（不做完整 JSON 解析）—— 但足以把「名字长什么样」钉住，
 * 而 strstr 只能判子串、判不了「时间戳那 15 位都是数字」这类形状。 */
static int field1(const char *json, const char *field, char *out, size_t osz) {
    if (!json || !field || !out || osz == 0) return 0;
    out[0] = 0;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", field);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    const char *e = strchr(p, '"');
    if (!e) return 0;
    size_t n = (size_t)(e - p);
    if (n >= osz) n = osz - 1;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

/* 数目录里的普通文件个数（缩略图缓存有没有真落盘，靠它断言） */
static int count_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char p[700];
        snprintf(p, sizeof(p), "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) n++;
    }
    closedir(d);
    return n;
}

/* ── 造一张系统缩略图（Python 标准库写不出 JPEG，所以由 stb 现写） ─────────── */
typedef struct { unsigned char *p; size_t len, cap; } buf_t;

static void grow(void *ctx, void *data, int len) {
    buf_t *b = (buf_t *)ctx;
    if (len <= 0) return;
    if (b->len + (size_t)len > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 65536;
        while (nc < b->len + (size_t)len) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(b->p, nc);
        if (!np) return;
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->len, data, (size_t)len);
    b->len += (size_t)len;
}

static int write_icon(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", ROOT, ICON_REL);
    /* ensure_dir_recursive 会把最后一段也当目录建掉，所以只喂父目录 */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    if (ensure_dir_recursive(dir) != 0) return -1;

    int w = 480, h = 270;
    unsigned char *px = (unsigned char *)malloc((size_t)w * (size_t)h * 3);
    if (!px) return -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned char *q = px + ((size_t)y * (size_t)w + (size_t)x) * 3;
            q[0] = (unsigned char)(x * 255 / w);
            q[1] = (unsigned char)(y * 255 / h);
            q[2] = 0x40;
        }
    buf_t b;
    memset(&b, 0, sizeof(b));
    stbi_write_jpg_to_func(grow, &b, w, h, 3, px, 85);
    free(px);
    if (!b.p || b.len < 100) { free(b.p); return -1; }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(b.p); return -1; }
    int ok = 1;
    for (size_t off = 0; off < b.len;) {
        ssize_t n = write(fd, b.p + off, b.len - off);
        if (n <= 0) { ok = 0; break; }
        off += (size_t)n;
    }
    close(fd);
    free(b.p);
    return ok ? 0 : -1;
}

/* ── 取出 zip 产出到固定路径，交给 test/media_verify.py 验 ────────────────── */
static int dump_fd(const char *dst, int fd, off_t sz) {
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) return -1;
    char buf[65536];
    off_t left = sz;
    int ok = 1;
    while (left > 0) {
        size_t want = left > (off_t)sizeof(buf) ? sizeof(buf) : (size_t)left;
        ssize_t n = read(fd, buf, want);
        if (n <= 0) { ok = 0; break; }
        for (size_t off = 0; off < (size_t)n;) {
            ssize_t w = write(out, buf + off, (size_t)n - off);
            if (w <= 0) { ok = 0; break; }
            off += (size_t)w;
        }
        if (!ok) break;
        left -= n;
    }
    close(out);
    return ok ? 0 : -1;
}

static int wait_job(long long job, char *out, size_t outsz) {
    for (int i = 0; i < 400; i++) {
        int n = game_media_pack_status(job, out, outsz);
        if (n < 0) return -1;
        if (strstr(out, "\"done\":true") != NULL) return 0;
        usleep(25000);
    }
    return -2;
}

int main(void) {
    /* ── 0. 前置条件 ─────────────────────────────────────────────────── */
    CHECK(write_icon() == 0, "icon 生成失败（stb_image_write）");
    CHECK(exists(ROOT "/av_content.db"), "夹具 DB 不在，先跑 test/make_media_fixture.py");

    struct stat dbst0;
    CHECK(stat(ROOT "/av_content.db", &dbst0) == 0, "stat DB 失败");
    CHECK((dbst0.st_mode & 0777) == 0444, "夹具 DB 应为 0444，实际 %o",
          (unsigned)(dbst0.st_mode & 0777));

    char buf[1 << 17];
    int n;

    /* ── ① 列表 ─────────────────────────────────────────────────────── */
    n = game_media_list_json("PPSA99008", buf, sizeof(buf));
    CHECK(n > 0, "list 失败 rc=%d", n);
    if (n > 0) {
        buf[n] = 0;
        printf("  list.json: %s\n", buf);
        CHECK(strstr(buf, "\"count\":5") != NULL, "count 应为 5: %s", buf);
        CHECK(strstr(buf, "\"kind\":\"screenshot\"") != NULL, "缺 screenshot");
        CHECK(strstr(buf, "\"kind\":\"video\"") != NULL, "缺 video");
        CHECK(strstr(buf, "\"w\":3840") != NULL, "缺 3840 宽");
        CHECK(strstr(buf, "\"duration\":15.0") != NULL, "缺 15.0 秒的录屏时长");
        CHECK(strstr(buf, "\"duration\":12.0") != NULL, "缺 duration 兜底分支的 12.0 秒");
        CHECK(strstr(buf, "\"mtime\":\"20") != NULL, "mtime 应是 YYYY-MM-DD…");

        /* ── 命名（v0.5.2.121 / .122）───────────────────────────────────────
         * 用户口径：「截图下载和视频下载的命名方式是游戏名称-截图时间」。
         * 显示名与下载名**刻意不同**（前者给人看、可以有空格冒号；后者要落盘、
         * 一个都不能有），所以两组都钉住 —— 只钉一组，另一组漂了没人知道。
         * 具体时间戳取决于夹具文件落盘那一刻的 mtime，所以判形状不判全串。 */
        {
            char nm[300], dl[300];
            CHECK(field1(buf, "name", nm, sizeof(nm)), "列表里没有 name 字段");
            CHECK(field1(buf, "dlname", dl, sizeof(dl)), "列表里没有 dlname 字段");
            printf("  name=%s  dlname=%s\n", nm, dl);

            const char *gp = "宇宙机器人-";
            size_t gl = strlen(gp);
            CHECK(strncmp(nm, gp, gl) == 0, "显示名应以「游戏名称-」开头: %s", nm);
            CHECK(nm[gl + 4] == '-' && nm[gl + 7] == '-',
                  "显示名后面应跟 YYYY-MM-DD: %s", nm);
            CHECK(strchr(nm, ':') != NULL, "显示名应带 HH:MM（给人看的）: %s", nm);

            CHECK(strncmp(dl, gp, gl) == 0, "下载名应以「游戏名称-」开头: %s", dl);
            CHECK(strchr(dl, ' ') == NULL && strchr(dl, ':') == NULL,
                  "下载名里不能有空格与冒号（Windows / Shell 不接受）: %s", dl);
            CHECK(strstr(dl, ".png") != NULL, "下载名应带原扩展名 .png: %s", dl);
            /* 时间戳段：8 位日期 + '_' + 6 位时间 + '.' */
            {
                const char *ts = dl + gl;
                int ok = 1;
                for (int i = 0; i < 8; i++) if (!isdigit((unsigned char)ts[i])) ok = 0;
                if (ts[8] != '_') ok = 0;
                for (int i = 9; i < 15; i++) if (!isdigit((unsigned char)ts[i])) ok = 0;
                if (ts[15] != '.') ok = 0;
                CHECK(ok, "下载名时间戳应为 YYYYMMDD_HHMMSS.<ext>: %s", dl);
            }
            /* 录屏那一条也要带上自己的扩展名 */
            CHECK(strstr(buf, ".mp4\"") != NULL, "录屏的下载名应带 .mp4");
        }
        /* 分隔符：first 标志一旦写错，这些形状会立刻出现 */
        CHECK(strstr(buf, "[,") == NULL, "首项前多了逗号");
        CHECK(strstr(buf, ",]") == NULL, "末项后多了逗号");
        CHECK(strstr(buf, ",,") == NULL, "出现连续逗号");
        CHECK(strstr(buf, "]}],") == NULL, "对象结尾异常");

        int fp = open(ROOT "/list.json", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fp >= 0) { ssize_t ign = write(fp, buf, (size_t)n); (void)ign; close(fp); }

        /* tid 过滤：另一个游戏只该看到自己的 1 条 */
        n = game_media_list_json("PPSA00001", buf, sizeof(buf));
        CHECK(n > 0, "list(other) 失败 rc=%d", n);
        if (n > 0) { buf[n] = 0; CHECK(strstr(buf, "\"count\":1") != NULL, "other count: %s", buf); }

        /* 存在但没媒体 → count 0，不是报错 */
        n = game_media_list_json("ZZZZ99999", buf, sizeof(buf));
        CHECK(n > 0, "list(empty) 应成功");
        if (n > 0) { buf[n] = 0; CHECK(strstr(buf, "\"count\":0") != NULL, "empty count: %s", buf); }

        /* 非法 tid：路径字符 / 过短 / NULL 一律 -1 */
        CHECK(game_media_list_json("../../etc", buf, sizeof(buf)) < 0, "非法 tid 未被挡");
        CHECK(game_media_list_json("A B", buf, sizeof(buf)) < 0, "带空格 tid 未被挡");
        CHECK(game_media_list_json(NULL, buf, sizeof(buf)) < 0, "NULL tid 未被挡");
        CHECK(game_media_list_json("PPSA99008", buf, 16) < 0, "过小缓冲区未被识别");
    }

    /* ── ② 定位 ─────────────────────────────────────────────────────── */
    game_media_item_t it;
    memset(&it, 0, sizeof(it));
    CHECK(game_media_lookup(C_PNG1, &it) == 0, "lookup png 失败");
    CHECK(it.w == 3840 && it.h == 2160, "png 宽高 = %dx%d", it.w, it.h);
    CHECK(it.video == 0, "png 不该是 video");
    {
        struct stat st;
        CHECK(stat(ROOT C_PNG1, &st) == 0, "夹具 png 不在");
        CHECK(it.size == (long long)st.st_size, "size=%lld 应为 %lld（stat 真值，不是 DB 的 size_on_storage）",
              it.size, (long long)st.st_size);
    }
    /* lookup 走的是 tid=NULL 的扫描，**不经过**游戏名那条路 → name 仍是文件名。
     * 这与列表里那组（游戏名称-截图时间）互为对照：命名只在带 tid 过滤时发生。
     * 别把这条改成与列表一样的 —— 改了就丢掉了「只有带 tid 才带游戏名」这条性质。 */
    CHECK(strcmp(it.name, "20261005_165454_00474347.png") == 0, "name=%s", it.name);
    CHECK(strcmp(it.dlname, "20261005_165454_00474347.png") == 0, "dlname=%s", it.dlname);
    CHECK(it.icon[0] != 0, "icon_path 该被读出来");
    CHECK(it.stamp[4] == '-' && it.stamp[7] == '-' && it.stamp[10] == ' ',
          "mtime 格式不对: %s", it.stamp);
    CHECK(it.duration == 0.0, "截图时长应为 0，实际 %f", it.duration);

    memset(&it, 0, sizeof(it));
    CHECK(game_media_lookup(C_VID1, &it) == 0, "lookup video 失败");
    CHECK(it.video == 1, "mp4 应标记为 video");
    CHECK(it.duration == 15.0, "duration_ms=15000 → 15.0s，实际 %f", it.duration);

    memset(&it, 0, sizeof(it));
    CHECK(game_media_lookup(C_VID2, &it) == 0, "lookup video2 失败");
    CHECK(it.duration == 12.0, "duration_ms=0 → 退回 duration=12，实际 %f", it.duration);

    /* 库里有、盘上没有 → 找不到（md_cb_row 会 stat） */
    CHECK(game_media_lookup("/av_contents/photo/NPXS40087/PPSA99008/18c/gone.png", &it) < 0,
          "不存在的文件不该被找到");
    /* 路径白名单 */
    CHECK(game_media_lookup("/etc/passwd", &it) < 0, "/etc/passwd 未被挡");
    CHECK(game_media_lookup("/av_contents/photo/../../etc/passwd", &it) < 0, "穿越未被挡");
    CHECK(game_media_lookup("/av_contents//photo/a.png", &it) < 0, "空段未被挡");
    CHECK(game_media_lookup(NULL, &it) < 0, "NULL id 未被挡");

    /* ── ③ 原图 fd 直出 ─────────────────────────────────────────────── */
    int fd = -1;
    off_t sz = 0;
    const char *mime = NULL;
    CHECK(game_media_open(C_PNG1, &fd, &sz, &mime) == 0, "open 失败");
    CHECK(sz > 0, "size=%lld", (long long)sz);
    CHECK(mime && strcmp(mime, "image/png") == 0, "mime=%s", mime ? mime : "(null)");
    if (fd >= 0) close(fd);

    fd = -1;
    CHECK(game_media_open(C_VID1, &fd, &sz, &mime) == 0, "open video 失败");
    CHECK(mime && strcmp(mime, "video/mp4") == 0, "mp4 mime=%s", mime ? mime : "(null)");
    if (fd >= 0) close(fd);

    CHECK(game_media_open("/av_contents/photo/x/y/z/a.png", &fd, &sz, &mime) == -2,
          "不存在的原图应 -2");
    CHECK(game_media_open("/etc/passwd", &fd, &sz, &mime) == -1, "越界原图应 -1");
    CHECK(game_media_open(NULL, &fd, &sz, &mime) == -1, "NULL id 应 -1");

    /* ── ③-b 下载文件名（v0.5.2.122）──────────────────────────────────────
     * 用户口径：「截图下载和视频下载的命名方式是游戏名称-截图时间」。
     * 这条只负责**算名字**；下不下发由 http_server.c 的 add_disposition 管。 */
    {
        char dn[300];
        CHECK(game_media_download_name("PPSA99008", C_PNG1, dn, sizeof(dn)) == 0, "取下载名失败");
        printf("  dlname(PPSA99008)=%s\n", dn);
        CHECK(strncmp(dn, "宇宙机器人-", strlen("宇宙机器人-")) == 0,
              "下载名应以游戏名开头: %s", dn);
        CHECK(strchr(dn, ':') == NULL && strchr(dn, ' ') == NULL,
              "下载名不能有冒号与空格（落盘/命令行都不接受）: %s", dn);
        CHECK(strstr(dn, ".png") != NULL, "下载名应带原扩展名: %s", dn);

        /* 录屏走同一条路，扩展名跟着 content_path 走 */
        CHECK(game_media_download_name("PPSA99008", C_VID2, dn, sizeof(dn)) == 0, "录屏取下载名失败");
        CHECK(strstr(dn, ".mp4") != NULL, "录屏下载名应带 .mp4: %s", dn);

        /* 游戏名取不到（没改过名 / name.txt 不在）→ 退回原文件名，不是空串 */
        CHECK(game_media_download_name("ZZZZ99999", C_PNG1, dn, sizeof(dn)) == 0,
              "没有游戏名时也该给出名字");
        CHECK(strcmp(dn, "20261005_165454_00474347.png") == 0, "无游戏名应退回原名: %s", dn);

        /* 参数非法 / 文件不在 */
        CHECK(game_media_download_name("PPSA99008", "/etc/passwd", dn, sizeof(dn)) == -1,
              "越界 id 应 -1");
        CHECK(game_media_download_name("PPSA99008", "/av_contents/photo/x/y/z/a.png", dn, sizeof(dn)) == -1,
              "盘上没有的文件应 -1");
    }

    /* ── ④ 缩略图 ───────────────────────────────────────────────────── */
    unsigned char *td = NULL;
    size_t tlen = 0;

    /* 有系统图标：解 JPEG → 缩到 320x180 */
    CHECK(game_media_thumb(C_PNG1, 320, &td, &tlen, &mime) == 0, "带图标缩略图失败");
    CHECK(tlen > 100 && memcmp(td, "\xff\xd8\xff", 3) == 0, "产出不是 JPEG");
    CHECK(mime && strcmp(mime, "image/jpeg") == 0, "thumb mime=%s", mime ? mime : "(null)");
    if (td && tlen > 4) {
        int iw = 0, ih = 0, ic = 0;
        unsigned char *px = stbi_load_from_memory(td, (int)tlen, &iw, &ih, &ic, 0);
        CHECK(px != NULL, "产出的 JPEG 解不回来");
        CHECK(iw == 320 && ih == 180, "缩略图尺寸 = %dx%d，应 320x180", iw, ih);
        stbi_image_free(px);
    }
    free(td);
    td = NULL;

    /* 无图标、原图是 PNG → 兜底自己解；want_w=200 → 1920x1080 缩成 200x112 */
    CHECK(game_media_thumb(C_PNG2, 200, &td, &tlen, &mime) == 0, "PNG 兜底缩略图失败");
    if (td && tlen > 4) {
        int iw = 0, ih = 0, ic = 0;
        unsigned char *px = stbi_load_from_memory(td, (int)tlen, &iw, &ih, &ic, 0);
        CHECK(px != NULL, "兜底产出的 JPEG 解不回来");
        CHECK(iw == 200 && ih == 112, "兜底尺寸 = %dx%d，应 200x112", iw, ih);
        stbi_image_free(px);
    }
    free(td);
    td = NULL;

    /* 落盘缓存确实生成了（两条成功的缩略图 → 至少 2 个 .jpg） */
    CHECK(count_files(ROOT "/thumbs") >= 2, "缩略图缓存文件数 = %d，应 >=2",
          count_files(ROOT "/thumbs"));

    /* HDR .jxr 没有 icon、stb 也不认 → -2（HTTP 侧回 404） */
    CHECK(game_media_thumb(C_JXR, 320, &td, &tlen, &mime) == -2, "HDR .jxr 应 -2");
    free(td); td = NULL;

    /* ── 录屏缩略图：必须回**占位图**，不是 404（v0.5.2.120/122）────────────
     * 用户口径：「视频没有预览图片」。真机上是「录屏没有可解封面帧」——
     * 要么 icon_path 为空，要么指向一张 stb 解不动的 .jxr。两种都必须落到占位图。
     * 断言四件：rc==0 / 是 JPEG / 尺寸 320x180 / 中央真有白色播放三角。
     * 只断 rc==0 是不够的：给一张纯黑图也能过，而那在界面上就是「还是没缩略图」。 */
    {
        td = NULL; tlen = 0;
        CHECK(game_media_thumb(C_VID1, 320, &td, &tlen, &mime) == 0,
              "无 icon 的录屏应回占位图而不是 404");
        CHECK(td && tlen > 100 && memcmp(td, "\xff\xd8\xff", 3) == 0, "占位图应是 JPEG");
        if (td && tlen > 4) {
            int iw = 0, ih = 0, ic = 0;
            unsigned char *px = stbi_load_from_memory(td, (int)tlen, &iw, &ih, &ic, 0);
            CHECK(px != NULL, "占位图 JPEG 解不回来");
            CHECK(iw == 320 && ih == 180, "占位图尺寸 = %dx%d，应 320x180", iw, ih);
            if (px && iw == 320 && ih == 180) {
                /* 三角左顶点在中心、向右侧张开 ts 像素；取中心偏右 1/4 处必然在三角内 */
                int ts = (iw < ih ? iw : ih) / 4;
                int sx = iw / 2 + ts / 2, sy = ih / 2;
                const unsigned char *c = px + ((size_t)sy * (size_t)iw + (size_t)sx) * 3;
                CHECK(c[0] > 200 && c[1] > 200 && c[2] > 200,
                      "占位图中央应有白色播放三角，实际 RGB=(%d,%d,%d)", c[0], c[1], c[2]);
            }
            stbi_image_free(px);
        }
        free(td); td = NULL;

        /* ⚠️ 这一条才是用户真机上那个：icon_path **非空**、文件也真的在，
         * 但它是 .jxr（HDR 封面帧），stb 解不动。旧代码只看「icon 在不在」，
         * 于是走进解码分支、失败、回 -2 —— 视频缩略图因此一直空白。
         * 判据必须是「这条是视频」，不是「有没有 icon_path」。 */
        td = NULL; tlen = 0;
        CHECK(game_media_thumb(C_VIDJ, 320, &td, &tlen, &mime) == 0,
              "带 .jxr 封面帧的录屏也应是占位图，不是 404");
        CHECK(td && tlen > 100 && memcmp(td, "\xff\xd8\xff", 3) == 0,
              "带 .jxr 封面帧时产出的也应是 JPEG 占位图");
        free(td); td = NULL;
    }
    /* 越界 id → -1，绝不能去 stat */
    CHECK(game_media_thumb("/etc/passwd", 320, &td, &tlen, &mime) == -1, "越界缩略图应 -1");
    free(td); td = NULL;
    CHECK(game_media_thumb("/av_contents/photo/../../x.png", 320, &td, &tlen, &mime) == -1,
          "穿越缩略图应 -1");
    free(td); td = NULL;

    /* ── ⑤ 打包 ─────────────────────────────────────────────────────── */
    /* 先把请求体边界试一遍（这些都不该起任务） */
    long long job = 0;
    char msg[192];
    {
        const char *bad = "not json";
        CHECK(game_media_pack_start_json(bad, strlen(bad), &job, msg, sizeof(msg)) == -2,
              "非 JSON 应 -2，msg=%s", msg);
        const char *notid = "{\"ids\":[\"x\"]}";
        CHECK(game_media_pack_start_json(notid, strlen(notid), &job, msg, sizeof(msg)) == -2,
              "缺 tid 应 -2，msg=%s", msg);
        const char *evil = "{\"tid\":\"PPSA99008\",\"ids\":[\"/etc/passwd\"]}";
        CHECK(game_media_pack_start_json(evil, strlen(evil), &job, msg, sizeof(msg)) == -2,
              "越界 id 应 -2，msg=%s", msg);
        CHECK(game_media_pack_start_json(NULL, 0, &job, msg, sizeof(msg)) == -2,
              "空请求体应 -2");
    }
    CHECK(game_media_pack_status(99999999, buf, sizeof(buf)) < 0, "未知 job 状态应 -1");
    CHECK(game_media_pack_cancel(99999999) < 0, "未知 job 取消应 -1");

    /* 子集：2 条 */
    job = 0;
    {
        const char *body = "{\"tid\":\"PPSA99008\",\"ids\":[\"" C_PNG1 "\",\"" C_JXR "\"]}";
        n = game_media_pack_start_json(body, strlen(body), &job, msg, sizeof(msg));
    }
    CHECK(n == 0, "子集打包启动失败 rc=%d msg=%s", n, msg);
    CHECK(job > 0, "job_id=%lld", job);

    char sbuf[1024];
    CHECK(wait_job(job, sbuf, sizeof(sbuf)) == 0, "子集任务没跑完: %s", sbuf);
    CHECK(strstr(sbuf, "\"ok\":true") != NULL, "子集任务失败: %s", sbuf);
    CHECK(strstr(sbuf, "\"files_total\":2") != NULL, "files_total 应 2: %s", sbuf);
    CHECK(strstr(sbuf, "\"percent\":100.0") != NULL, "percent 应 100: %s", sbuf);
    /* v0.5.2.122 新增 idle（距上次有进展多少秒）：任务结束后恒 0 ——
     * 前端拿它当「是不是卡住了」的判据，跑完还一直涨就会被自己误导。 */
    CHECK(strstr(sbuf, "\"idle\":0") != NULL, "任务结束后 idle 应为 0: %s", sbuf);

    /* 压缩包命名（v0.5.2.121/122）：游戏名称-打包时间戳.zip，且**状态里透出同名**
     * —— 前端 <a download> 与 http_server 的 Content-Disposition 都取这一处，
     * 两边同源才不会「界面上写 A、落盘叫 B」。 */
    {
        char zn[300];
        CHECK(game_media_pack_name(job, zn, sizeof(zn)) == 0, "取包名失败");
        printf("  pack name=%s\n", zn);
        CHECK(strncmp(zn, "宇宙机器人-", strlen("宇宙机器人-")) == 0, "包名应以游戏名开头: %s", zn);
        CHECK(strstr(zn, ".zip") != NULL, "包名应带 .zip: %s", zn);
        CHECK(strchr(zn, ':') == NULL && strchr(zn, ' ') == NULL, "包名不能有冒号/空格: %s", zn);
        CHECK(strstr(sbuf, "\"name\":\"宇宙机器人-") != NULL, "状态 JSON 应透出包名: %s", sbuf);
    }

    fd = -1; sz = 0;
    CHECK(game_media_pack_open(job, &fd, &sz) == 0, "取子集包失败");
    CHECK(sz > 22, "子集包 size=%lld", (long long)sz);
    if (fd >= 0) {
        CHECK(dump_fd(ROOT "/pack_subset.zip", fd, sz) == 0, "写出子集包失败");
        close(fd);
    }

    /* ── 大文件：跨过 zip_write 的 256 KB 分块边界 ───────────────────────────
     * 用户报的「打包卡在 0 B / 58.7 MB」正落在这条路上，而夹具里的录屏原来只有
     * 4 KB —— 一个分块都读不满，on_bytes 只回调一次，CRC 回填那两行 lseek 也只在
     * 收尾走一遍。等于「大文件 + 多次分块 + 条目收尾」这条路从来没被测过。
     * 现在 v1 是 1536 KiB + 12 字节头，至少 6 次分块。 */
    {
        const char *body = "{\"tid\":\"PPSA99008\",\"ids\":[\"" C_VID1 "\"]}";
        job = 0;
        n = game_media_pack_start_json(body, strlen(body), &job, msg, sizeof(msg));
        CHECK(n == 0, "大文件打包启动失败 rc=%d msg=%s", n, msg);
        CHECK(wait_job(job, sbuf, sizeof(sbuf)) == 0, "大文件任务没跑完: %s", sbuf);
        CHECK(strstr(sbuf, "\"ok\":true") != NULL, "大文件任务失败: %s", sbuf);
        CHECK(strstr(sbuf, "\"files_total\":1") != NULL, "大文件 files_total 应 1: %s", sbuf);
        /* 8 字节 ftyp 头 + 1536 KiB */
        CHECK(strstr(sbuf, "\"bytes_total\":1572876") != NULL,
              "大文件 bytes_total 应为源文件真实大小（1572876）: %s", sbuf);
        fd = -1; sz = 0;
        CHECK(game_media_pack_open(job, &fd, &sz) == 0, "取大文件包失败");
        if (fd >= 0) {
            CHECK(dump_fd(ROOT "/pack_big.zip", fd, sz) == 0, "写出大文件包失败");
            close(fd);
        }
    }

    /* 全量：5 条 */
    {
        const char *body = "{\"tid\":\"PPSA99008\"}";
        job = 0;
        n = game_media_pack_start_json(body, strlen(body), &job, msg, sizeof(msg));
        CHECK(n == 0, "全量打包启动失败 rc=%d msg=%s", n, msg);
    }
    CHECK(wait_job(job, sbuf, sizeof(sbuf)) == 0, "全量任务没跑完: %s", sbuf);
    CHECK(strstr(sbuf, "\"ok\":true") != NULL, "全量任务失败: %s", sbuf);
    CHECK(strstr(sbuf, "\"files_total\":5") != NULL, "files_total 应 5: %s", sbuf);

    fd = -1; sz = 0;
    CHECK(game_media_pack_open(job, &fd, &sz) == 0, "取全量包失败");
    if (fd >= 0) {
        CHECK(dump_fd(ROOT "/pack_all.zip", fd, sz) == 0, "写出全量包失败");
        close(fd);
    }

    /* 任务跑完后状态仍然可查（active=false），取消已结束的不该报错 */
    CHECK(game_media_pack_status(job, sbuf, sizeof(sbuf)) > 0, "结束后状态仍应可查");
    CHECK(strstr(sbuf, "\"active\":false") != NULL, "结束后 active 应 false: %s", sbuf);

    /* ── ⑥ 删除 ─────────────────────────────────────────────────────── */
    CHECK(exists(ROOT C_OTH), "另一个游戏的截图不在，夹具缺文件");
    CHECK(game_media_delete("PPSA99008", C_OTH, msg, sizeof(msg)) < 0,
          "tid 不符不该删成，msg=%s", msg);
    CHECK(exists(ROOT C_OTH), "tid 不符时文件被误删了");

    CHECK(game_media_delete("PPSA00001", C_OTH, msg, sizeof(msg)) == 0,
          "删除失败 rc, msg=%s", msg);
    CHECK(!exists(ROOT C_OTH), "原图没删掉");

    /* 库里的行还在（铁律①：我们不写 av_content.db），但列表不该再吐出来 */
    n = game_media_list_json("PPSA00001", buf, sizeof(buf));
    CHECK(n > 0, "删除后 list(other) 失败");
    if (n > 0) { buf[n] = 0; CHECK(strstr(buf, "\"count\":0") != NULL, "删除后仍被列出: %s", buf); }

    struct stat dbst1;
    CHECK(stat(ROOT "/av_content.db", &dbst1) == 0, "stat DB 失败(尾)");
    CHECK(dbst1.st_size == dbst0.st_size && dbst1.st_mtime == dbst0.st_mtime,
          "av_content.db 被改动了（size %lld→%lld, mtime %ld→%ld）铁律①被破",
          (long long)dbst0.st_size, (long long)dbst1.st_size,
          (long)dbst0.st_mtime, (long)dbst1.st_mtime);

    /* 主集合不受影响 */
    n = game_media_list_json("PPSA99008", buf, sizeof(buf));
    CHECK(n > 0, "主集合删除后 list 失败");
    if (n > 0) { buf[n] = 0; CHECK(strstr(buf, "\"count\":5") != NULL, "主集合被改小: %s", buf); }

    printf("media_scan: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
